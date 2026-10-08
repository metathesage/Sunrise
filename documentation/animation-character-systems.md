# Destiny-Style Character, Animation & Emote Systems
### Engineering Reference & Production Workflow (Sunrise context)

> **Audience:** engine/gameplay engineers, modders, technical animators, and coding agents
> working inside the Sunrise offline-preservation codebase.
>
> **What this document is:** a from-first-principles breakdown of how AAA first-person
> character/animation/emote/weapon systems are architected, how to *study* a legacy build's
> data through the tag/definition pipeline, an exact phase-by-phase build workflow, and a
> library of lead-level agent prompts to drive implementation.
>
> **What this document is not:** a cheat, a crack, or a guide to touching live servers. It
> contains no extracted/copyrighted game data. All game data is read at runtime, per the
> project's contribution rules.

---

## 0. How to use this document

| If you are... | Start at |
|---|---|
| New to the codebase | §1 (scope), §2 (mental model), §4 (data pipeline) |
| A gameplay engineer | §3.4–§3.12, §5 (workflow), §9 (agent prompts) |
| A technical animator | §3.1–§3.3, §3.6–§3.9, §6 (conventions) |
| Driving coding agents | §9 (prompt library) + §5.9 (definition of done) |
| Reviewing a PR | §5.9, §6, §7, §8 |

The document is deliberately **engine-agnostic in the concepts and concrete in the numbers.**
Where a value is a tuning constant, it is given as a starting point with a stated range; the
"feel" lives in those curves and constants, not in proprietary tech.

---

## 1. Scope, intent, and boundary

### 1.1 Intent

We are reverse-engineering the **behavior** of a legacy, offline, local build's character and
animation systems in order to (a) preserve it, (b) re-implement equivalent systems, and
(c) document them. This is interoperability/preservation work on a game the user already
owns and runs locally. It is not circumvention of protection on a live service.

### 1.2 Hard boundaries (non-negotiable)

- **No live-server targeting.** Nothing here talks to Bungie/Sony services or newer builds.
- **No copyrighted data in the repo.** Animation clips, meshes, textures, and definition
  binaries are never committed. They are read at runtime from the user's own install.
- **No assets shipped.** Docs and code only.
- **No anti-cheat circumvention.** The target is an offline legacy build.
- **Attribution.** Cite tools/parsers used (tiger-pkg, alkahest, tachyscope, D2TagParser,
  DestinyUnpacker, etc. — see `README.md` credits).

### 1.3 What "similar results" means here

The *feel* of Destiny 2's gunplay/emotes is ~90% four well-understood techniques:

1. **Layered animation graphs** (base locomotion + weapon + additive overlays + IK).
2. **Event/notify-driven gameplay sync** (the animation is the timeline; events do the work).
3. **Procedural additive motion** (recoil, sway, breathing) as spring-damper systems.
4. **Command-driven input** with client prediction and server authority.

Reproducing these in Sunrise (or any engine) is an engineering problem, not a secrets problem.

---

## 2. Mental model: the layered animation stack

Every modern FPS character is a **stack of layers evaluated bottom-up each frame**. Thinking in
layers (not clips) is the single most important mental shift.

```
                         +------------------------------------------+
                         |  FINAL POSE  (skeleton local transforms)  |
                         +---------------------^--------------------+
                                               |
   +--------------+   +--------------+   +-----+--------+   +----------------+
   |  IK / GROUND |   |  SECONDARY   |   |   ADDITIVE   |   |  BASE + WEAPON |
   |   ADJUSTMENT |<--|  MOTION      |<--|   OVERLAYS   |<--|  STATE MACHINE |
   |  (feet/hands)|   | (cloth,spring|   | (recoil,sway,|   | (locomotion +  |
   |              |   |  bones)      |   |  breathing,  |   |  fire/reload/  |
   |              |   |              |   |  aim offset) |   |  emote layer)  |
   +--------------+   +--------------+   +--------------+   +----------------+
        post            post              additive            blended base
```

**Evaluation order (bottom-up, right-to-left above):**

1. **Base layer** - locomotion / idle, blended by speed + direction (a 1D or 2D blend space).
2. **Weapon layer** - per-archetype state machine (idle/fire/ADS/reload/inspect/draw/holster),
   masked or full-body depending on the weapon and view.
3. **Additive overlays** - recoil, aim offset, breathing, flinch, sway. These *add* delta
   rotations/translations on top of whatever the base produced. They never replace it.
4. **Secondary motion** - springs on cloth, straps, cables, antennae, ponytails.
5. **IK / ground adjustment** - hands to grip points, feet to ground, look-at constraints.

**Why layering matters:** it makes systems independent and tunable. The animator authors a
reload; the combat designer tunes recoil; neither blocks the other. If you hard-code a reload
as one giant clip you will never match the responsiveness of a layered system.

### 2.1 The three clocks

Animation in a shooter runs on three independent clocks. Confusing them is a common failure:

| Clock | Rate | Drives |
|---|---|---|
| **Render/tick** | 30-144 Hz (frame) | Graph evaluation, springs, IK |
| **Animation sample** | clip fps (usually 30) | Base clip playback, notify firing |
| **Gameplay/net** | 20-60 Hz (fixed) | State transitions, replication, authority |

Springs and IK evaluate **every frame**. Notifies fire at **clip sample time**. Gameplay state
changes are **fixed-step** and replicated. Keep them separate or you get jitter and desync.

---

## 3. How the systems work

### 3.1 Character rig & skeleton conventions

- **Skeleton** = hierarchy of joints with local transforms; **mesh** = skinned geometry bound to
  those joints via weights. Nothing here is exotic.
- **Naming conventions** (adopt these or map to them): `root`, `pelvis`/`hips`, `spine_01..03`,
  `neck_01`, `head`, `clavicle_l/r`, `upperarm_l/r`, `lowerarm_l/r`, `hand_l/r`,
  `thigh_l/r`, `calf_l/r`, `foot_l/r`, `ball_l/r`. Prefix left/right consistently.
- **Bind pose** is the reference; all animation deltas are relative to it.
- **Retargeting:** author on a common/mannequin skeleton, retarget to character skeletons. Keep
  a **reference skeleton** so clips are portable.
- **Grip/attachment points:** each weapon defines named sockets (`grip_r`, `grip_l`,
  `muzzle`, `eject`, `mag_well`, `optic`, `muzzle_flash`, `shell_eject`). IK and effects bind to
  these, never to hard-coded offsets.

### 3.2 First-person viewmodel vs world body

This split is essential and often misunderstood.

| Aspect | FP viewmodel | World (3rd-person) body |
|---|---|---|
| Purpose | What the local player sees | What *other* players see |
| Skeleton | Own arms/weapon rig | Full character body |
| Camera | Attached to viewmodel root | N/A (camera is separate) |
| FOV | Often separate/narrower | World FOV |
| Anim set | Full: reload, inspect, ADS, fire | Coarser: locomotion, emote, fire, reload |
| Shadows | Often disabled or fake | Full |
| Cost | Low-poly arms, high detail gun | Full character + weapon |

**Key consequences:**

- A reload can look *better* in FP than the world body, because the FP clip is authored purely
  for the camera. The two are **not** required to match frame-for-frame; they must match in
  **gameplay timing** (when the mag actually refills).
- The FP viewmodel needs its **own light rig / rim light** so it reads in dark environments.
- **Motion is camera-relative.** The viewmodel root follows the camera with its own spring
  (bob, sway) so it lags slightly behind - that lag is a huge part of "game feel."

### 3.3 Camera as a rigged entity

The camera is not a static point. It is a rig with, at minimum:

```
camera_root            (yaw/pitch from player look input)
  +- camera_recoil     (additive recoil impulse, spring-decayed)
      +- camera_shake  (perlin/hit shake, event-driven)
          +- camera_sway (breathing + movement bob)
              +- camera_fov (ADS/zoom lerp + sprint FOV kick)
```

- **Recoil** is applied to `camera_recoil` as an additive pitch/yaw impulse, then **springs back**
  toward zero (this is why you can "pull down" to control a hand cannon).
- **Shake** is event-driven (explosions, taking damage, landing).
- **Sway/breathing** is low-frequency noise so the reticle is never perfectly still.
- **ADS** lerps FOV (e.g. 90 deg -> 55 deg) over ~0.15-0.25 s and blends the weapon to its aim pose.

The player's *aim* (where shots go) and the camera's *presentation* are related but distinct:
hit-scan originates from the camera/reticle ray, while the visible weapon muzzle is cosmetic
until it converges (see section 3.6, muzzle convergence).


### 3.4 The animation graph: layers, states, blend trees

The graph is a **directed structure evaluated every frame**. Three primitives do all the work:

**(a) Blend space (a.k.a. blend tree / 1D-2D blend).** Maps continuous inputs to a weighted mix
of clips. Locomotion example:

```
Input axes:  speed (0..1)  x  direction (-180..180 deg)
Clips:       idle | walk_f | walk_b | walk_l | walk_r | run_f | run_b | strafe_l | strafe_r
Output:      weighted pose
```

**(b) State machine.** Discrete states with transition rules. Weapon example:

```
        +----------------------------------------------------------+
        v                                                          |
   [IDLE] --fire--> [FIRE] --auto/next--> [FIRE] --done--> [IDLE] |
     |  \             |                                          |
     |   \--reload--> [RELOAD] --notify--> [RELOAD_TAC] --done---+|
     |    \--ads-----> [ADS] --fire--> [ADS_FIRE] --done--> [ADS]-+
     |     \--emote--> [EMOTE] --cancel--> (previous state)       |
     \--holster--> [HOLSTER] --> [HOLSTERED]
```

Transition rules are **conditions**, not just triggers: `can_fire`, `ammo > 0`, `ads_held`,
`reload_requested`, `emote_priority_ok`. Priority ordering matters (see emotes, section 3.10).

**(c) Layer mask.** A layer can affect all bones (full-body) or a subset (upper-body mask: spine
up, arms, head). Emotes and aim offsets typically use masks; reloads often use full-body in FP
and a weapon mask in the world body.

**Sync groups.** Group layers so a reload and its weapon-mask layer share a normalized time and
don't drift. Use `sync_group = "reload"` with a leader/follower.

### 3.5 The notify / event system (gameplay sync)

This is the most under-appreciated system. **Animation is the timeline; notifies do the gameplay.**

A **notify** (Unity: Animation Event; Unreal: Anim Notify) is a timestamped callback inside a
clip. When the clip's playhead crosses it, it fires exactly once (or per-loop).

Hand-cannon reload as a notify timeline (times illustrative for a ~2.3 s reload):

```
t=0.00  NOTIFY: reload_begin        -> lock fire, play mag-out SFX, spawn "old mag" prop
t=0.35  NOTIFY: mag_eject           -> detach mag mesh, drop physics prop, SFX
t=0.90  NOTIFY: mag_insert          -> attach new mag mesh, SFX
t=1.60  NOTIFY: ammo_refill         -> *** gameplay: magazine = capacity ***
t=1.85  NOTIFY: chamber_round       -> play hammer/cylinder SFX
t=2.30  NOTIFY: reload_end          -> unlock fire, transition to IDLE/ADS
```

**Rules of notifies:**

- **Gameplay truth lives on a notify, never on clip end.** Clip end is a rendering concept;
  notify time is the contract. If a player swaps weapons at t=1.5, the mag should already be
  full (ammo_refill at 1.6 may be skipped only if the swap is *after* it).
- Notifies must be **deterministic** and **replicated** (send the event, not the clip frame).
- Author **interrupt handling**: which notifies fire if the clip is blended out early.
- Separate **cosmetic notifies** (SFX/VFX) from **gameplay notifies** (ammo, state). Cosmetic
  ones may be skipped on low-LOD remote players.


### 3.6 Additive recoil model (with numbers)

Recoil is a **critically-damped spring** applied additively to camera + weapon, driven by a
per-shot impulse. This is the actual math to implement.

**State per axis (pitch, yaw, and weapon kick):** `x` (offset), `v` (velocity).

Per frame (dt seconds):

```
// Spring-damper toward rest (0):
//   omega = 2*pi*frequency
//   stiffness k = omega^2 ; damping c = 2*zeta*omega
// Critically damped => zeta = 1
a  = -k * x - c * v
v += a * dt
x += v * dt
```

**Impulse on fire (hand cannon, per shot):**

| Parameter | Symbol | Value | Range |
|---|---|---|---|
| Recoil frequency | `f` | 9.0 Hz | 6-14 |
| Damping ratio | `zeta` | 1.0 (critical) | 0.7-1.0 |
| Pitch kick | `impulse_pitch` | -2.4 deg | -1.5 .. -4.0 |
| Yaw kick (randomized) | `impulse_yaw` | +/- 0.35 deg | 0.1 .. 0.7 |
| Camera vs weapon split | `split` | 60% camera / 40% weapon | 40-80 |
| Recovery delay | `hold` | 0.12 s before spring engages | 0.05-0.25 |

```
// On FIRE event:
v_pitch += impulse_pitch * omega
v_yaw   += rand(-1,1) * impulse_yaw * omega
recoil_hold_timer = hold
// In update: only run the spring when recoil_hold_timer <= 0
// (the hold makes the kick readable before recovery starts)
```

**Muzzle convergence (important for feel):** the cosmetic muzzle and the camera ray converge
after recoil. Implement as a lerp of the *aim direction* toward the *muzzle direction* with a
time constant (~0.08 s), so shots visually "leave the gun" even though hitscan uses the camera.

**Aim punch / view punch:** a short, non-recoverable camera nudge (usually small) so consecutive
shots feel like they *push* you. Keep it separate from the recoverable spring.

### 3.7 Sway, breathing, ADS

- **Breathing:** sum of 2-3 sine/noise terms (0.15-0.4 Hz) applied additively to camera + arms.
  Amplitude scales down while ADS (`breath_scale = 0.35`).
- **Movement sway/bob:** driven by velocity and turn rate; the viewmodel lags the camera with a
  spring (separate from recoil). Turning left, the gun swings right, then settles.
- **Landing/impact:** on land, apply a downward camera impulse + a weapon dip, both spring-recovered.
- **ADS blend:** a single `aim` scalar 0..1 driving:
  - FOV lerp (hip 90 -> aim 55)
  - weapon position/rotation -> aim pose
  - sway amplitude multiplier
  - reticle visibility
  Use `smoothstep` for the scalar, ~0.18 s to aim, ~0.14 s to un-aim (asymmetric = snappier feel).

### 3.8 IK & hand placement

- **Two-bone IK** (upperarm -> lowerarm -> hand) to place the hand on the weapon grip socket.
  Always run IK *after* additive layers so recoil doesn't break the grip.
- **Aim IK / look-at:** constrain the upper body toward the aim direction while the lower body
  keeps locomotion (used for strafing + aiming).
- **Foot IK:** raycast to ground; blend foot height/rotation. Essential for slopes.
- **Weapon-to-hand vs hand-to-weapon:** decide authority. Usually **hand IK follows the weapon**
  in FP (weapon parented to a socket, hands IK to grip), and **weapon follows hand** in the world
  body (weapon parented to hand socket).
- **Solver settings:** iterations 1-2 for two-bone (analytic), max 8-16 for FABRIK chains; clamp
  per-frame IK delta to avoid popping when the target jumps.

### 3.9 Secondary motion (spring bones, cloth)

- **Spring bones:** a chain of joints integrated with a spring-damper following the parent's
  motion (verlet or explicit). Parameters: `stiffness`, `damping`, `gravity`, `drag`.
- Apply to: straps, cables, antennae, ponytails, hood cloth, weapon slings.
- **Cloth:** engine cloth solver (Unreal Chaos, Unity Cloth) for capes/robes; budget to ~2-4 ms.
- **Rule:** secondary motion must never affect gameplay (no collision with hitboxes) and must be
  cheap on remote players (LOD out beyond N meters).


### 3.10 Emote system

An emote is an **authorable clip + a layer/mask decision + an interruption policy + a state ID**.

**Two classes:**

| Class | Mask | Locomotion | Allowed when |
|---|---|---|---|
| Full-body (dance, sit, kneel) | full body | suspended | stationary, social spaces, on ground |
| Upper-body (wave, salute, point) | spine+arms+head | continues | almost anywhere on ground |

**State machine integration:**

```
EMOTE state:
  entry conditions:
    - emote requested AND emotes_enabled (not in combat lock / not airborne)
    - emote priority > current state priority
  during:
    - play clip on emote layer (mask per class)
    - loop if loopable (dance), else hold last frame / return
  exit conditions (ANY):
    - fire / reload / ADS pressed      -> cancel, restore weapon state
    - movement input beyond threshold  -> cancel full-body; upper-body may persist
    - damage taken                     -> cancel (combat overrides social)
    - emote pressed again              -> cancel (toggle)
```

**Priority table (higher cancels lower):**

```
  100  death / stagger
   90  damage flinch
   80  reload / weapon action
   70  fire / ADS
   50  full-body emote
   40  upper-body emote
   10  idle fidget / inspect
```

**Networked as state, not animation:** replicate `(player_id, emote_id, start_time)`. Each client
plays its own local clip. The server never trusts a client-supplied *pose*; only the emote *id*
and the fact it is playing. This is why remote animation can't be spoofed into a hitbox change.

**Data model per emote:**

```
emote_def {
  id            : uint32        // from game definition
  clip_ref      : clip_handle   // resolved at runtime from the user's install
  mask          : FULL | UPPER
  loop          : bool
  priority      : int
  allow_air     : bool
  allow_move    : bool
  cancel_on_fire: bool (true)
  sfx_ref, vfx_ref
}
```

### 3.11 Input command layer

Input is a **thin command layer**. Buttons do not play animations; they enqueue **commands** that
the state machine consumes.

```
raw input (device)
   -> binding map (button -> action)
   -> command queue (edge/held/released, buffered)
   -> gameplay state machine (consumes commands, validates)
   -> animation graph (selects clips/states)
   -> notifies (apply gameplay effects)
```

**Command buffering (critical for feel):** buffer inputs for a short window (~0.15-0.25 s) so a
reload pressed just before the current fire finishes still registers. This is why Destiny feels
"forgiving." Implement as a per-action timestamp; consume if `now - pressed_at <= buffer_window`.

**Edge vs held:** `fire` is edge (semi-auto) or held (auto) depending on archetype. `ADS` is
held. `reload` is edge. `emote` is edge.

**Input -> state examples (hand cannon):**

```
on fire_pressed:
  if state == IDLE and ammo > 0:      -> FIRE, emit fire notify, ammo -= 1
  if state == ADS  and ammo > 0:      -> ADS_FIRE
  if ammo == 0:                       -> trigger auto-reload (or dry-fire click)
on reload_pressed:
  if state in {IDLE, ADS} and ammo < capacity and spare_ammo > 0: -> RELOAD
on ads_pressed (held):
  if state in {IDLE, FIRE}:           -> ADS
```

### 3.12 Networking: prediction, authority, replication

- **Client predicts** locally: play the animation immediately, apply cosmetic + tentative gameplay
  effects. Feels instant.
- **Server is authoritative**: validates the gameplay effect (did the mag refill? did the shot
  land?). On mismatch, correct the client (rollback/replay or a snap).
- **Animation is generally NOT networked frame-by-frame.** You replicate **state**:
  `weapon_state`, `emote_id`, `ammo`, `reload_start_time`. Remote clients *re-simulate* the
  animation from that state.
- **Interpolation buffers** smooth remote actors (render ~100 ms in the past) so packet jitter
  doesn't stutter their animation.
- **Determinism:** notifies that affect gameplay must be reproducible from replicated state +
  time. Use server time as the single source of truth for `reload_start_time` etc.

**Replication payload sketch:**

```
PlayerAnimState (replicated, ~16-24 bytes):
  uint8   weapon_state_id
  uint8   emote_id
  uint16  emote_flags
  float   state_start_time   // server clock
  uint8   ammo
  uint8   aim_state          // hip/ads blend quantized
```


---

## 4. Data & definition pipeline (Sunrise context)

The engine that ships Destiny 2 ("Tiger") stores almost everything as **tagged definition
objects** in package files. Characters, animation sets, weapons, and emote definitions are all
tagged data, not hard-coded logic. Sunrise's job is to *read* that data at runtime and drive its
own systems from it.

### 4.1 The extraction path

```
user's game install (own copy, legacy build)
   |
   v
package files (.pkg)  -->  tag container parser
   |                         (tiger-pkg / alkahest / tachyscope / D2TagParser ...)
   v
typed tag objects (definitions)  -->  Sunrise data model (structs)
   |                                    (Sunrise/src/state/content, build_data, ...)
   v
runtime systems (Sunrise/src/client/player, movement, input, hooks)
   |
   v
render / gameplay
```

**Project rules applied to animation data:**

- **Read at runtime.** Never bake animation clips, meshes, or definition binaries into the repo
  or the release. Parse from the user's install each session (cache in memory, optionally in a
  local user-owned cache dir - never committed).
- **No copyrighted assets.** Skeleton/mesh/clip *references* are handles resolved against the
  user's install; the bytes stay on the user's disk.
- **Server focus.** Where a feature belongs to the server (authority, state), route it through
  the server layer rather than a client patch. Client patches only for presentation.

### 4.2 Animation-relevant definition taxonomy

The exact tag names are version/build specific; the *categories* to look for are stable:

| Category | What it gives you | Used for |
|---|---|---|
| **Animation set / state graph def** | States, transitions, blend params | Build the runtime graph |
| **Animation clip / sequence def** | Clip ref, fps, loop, notify list | Clip playback + notifies |
| **Skeleton / rig def** | Joint hierarchy, names, bind transforms | Rig construction |
| **Render model / mesh def** | Skinned mesh, material, bone map | Skinning |
| **Socket / attachment def** | Named attach points + offsets | Grip/muzzle/effects |
| **Weapon def** | Archetype, ammo, fire rate, reload timings | Weapon state machine params |
| **Emote def** | Clip ref, mask, loop, flags | Emote system |
| **Movement/character physics def** | Speeds, accel, jump, air control | Locomotion blend inputs |
| **Damage/impact def** | Flinch magnitudes | Additive flinch layer |

**Recon tip:** when you find a def, dump its fields to a readable form (JSON-ish) and diff across
weapons/characters. Patterns (e.g. "every weapon has a reload_time and a mag_size") reveal the
schema faster than reading the binary parser.

### 4.3 Runtime consumption model

Keep a clean separation:

```
[Content layer]   parsed defs, immutable, versioned, user-owned
      |  (build a read-only registry keyed by tag id)
      v
[Gameplay layer]  state machines, ammo, timers  (server-authoritative where relevant)
      |
      v
[Presentation]    graph evaluation, springs, IK, notifies -> render
```

- **Defs are data, not code.** A new weapon/emote should be *added by data*, not a new branch in
  a switch. If you find yourself special-casing an archetype in code, you probably missed a field.
- **Handles, not pointers into game memory**, where possible: resolve to stable ids so a
  reload/rollback doesn't dangle.
- **Version-guard** parsed schemas; a build bump can change field layout.

### 4.4 What you can implement without any asset data

You can build and test the *entire* animation architecture with **placeholder clips** (a cube on
a joint chain) because the architecture is data-agnostic. Only the final look needs the real
clips. This is how you de-risk: get the systems right against synthetic data, then bind real
defs. It also keeps the repo asset-free.


---

## 5. Exact production workflow

A phased plan with **gates**. Do not start a phase until the previous gate passes. Each phase
lists: inputs, tasks, artifacts, and the **Definition of Done (DoD)**. Times are for a
2-3 person team (engineer + tech animator), compressed if solo.

### Phase 0 - Recon & instrumentation  (3-5 days)

**Inputs:** user's own install, existing Sunrise tag parsers, a debugger, a build that boots
offline.

**Tasks:**

1. Enumerate animation-relevant defs (§4.2). Dump to JSON; count and categorize.
2. Identify the skeleton(s) and socket lists for a test character + a hand cannon.
3. Build a **runtime inspector overlay** (ImGui is already vendored): show current state, current
   clip, clip time, active notifies, ammo, aim scalar, and spring values, live.
4. Log one full reload + one emote end-to-end (timestamps of every notify).

**Artifacts:** `recon/anim-defs.json` (generated locally, **not committed**), a one-page schema
map, inspector overlay, a captured event log.

**DoD (gate 0):**

- [ ] We can list every state of a weapon's animation graph by name.
- [ ] We can see, live, which clip is playing and its normalized time.
- [ ] We can reproduce a reload's notify timings from a log, within +/- 1 frame.

### Phase 1 - Data model & registry  (4-6 days)

**Tasks:**

1. Define C++ structs mirroring §4.2 categories. Keep them POD-ish and versioned.
2. Build a read-only registry keyed by tag id with lazy resolution.
3. Write a **schema-version guard** and a graceful "unknown def" path (never crash on a new build).
4. Unit-test the parser against recorded dumps (fixtures live in the user's cache, not the repo).

**DoD (gate 1):**

- [ ] Given a weapon def id, code returns archetype, ammo, fire interval, reload time.
- [ ] Given an emote def id, code returns mask, loop, priority, flags.
- [ ] Registry is immutable after load; no gameplay code holds raw game-memory pointers.

### Phase 2 - State machine core  (5-8 days)

**Tasks:**

1. Implement the generic **hierarchical state machine** (states, transitions, conditions,
   priorities, sync groups).
2. Implement **clip playback + notify dispatch** on the animation clock (§2.1).
3. Implement **command queue + buffering** (§3.11).
4. Wire an **event bus**: state entered/exited, notify fired, command consumed.

**DoD (gate 2):**

- [ ] A synthetic weapon (placeholder clip) transitions IDLE->FIRE->IDLE and IDLE->RELOAD->IDLE
      with correct notify firing.
- [ ] Buffered reload within the window registers and plays after the current fire.
- [ ] Emote priority correctly cancels/gets-cancelled by combat states (§3.10 table).

### Phase 3 - Hand-cannon vertical slice  (8-12 days)

The **first real content**. Do one archetype end-to-end before generalizing.

**Tasks:**

1. Bind a real hand-cannon def: fire interval, mag size, reload time, spread, recoil params.
2. Author/derive the reload notify timeline (§3.5) and wire gameplay effects to notifies.
3. Implement the **additive recoil spring** (§3.6) on camera + weapon with the tuning table.
4. Implement **muzzle convergence**, aim punch, and dry-fire.
5. Implement **ADS blend** (§3.7) with the aim scalar.

**DoD (gate 3):**

- [ ] Reload refills the mag at the `ammo_refill` notify, not at clip end.
- [ ] Interrupting a reload at any time leaves ammo in the correct state (no duplication/dupe bug).
- [ ] Recoil is spring-recovered; holding fire produces a readable, controllable climb.
- [ ] ADS blend has no pop; FOV and weapon pose move together.
- [ ] All tuning constants are **data-driven** (a config), not literals in code.


### Phase 4 - Procedural overlays  (5-7 days)

**Tasks:**

1. Breathing + movement sway/bob springs (§3.7).
2. Landing/impact impulses.
3. Flinch additive layer driven by damage defs.
4. Secondary motion spring bones on straps/cables (§3.9), LOD'd for remote players.

**DoD (gate 4):**

- [ ] Reticle is never perfectly still; sway amplitude scales with ADS.
- [ ] Turning/strafe visibly lags the viewmodel then settles.
- [ ] Secondary motion is disabled/cheap beyond N meters and never affects hitboxes.

### Phase 5 - Emote system  (4-6 days)

**Tasks:**

1. Emote def registry + state machine integration (§3.10).
2. Full-body vs upper-body masks; loop + hold-last behavior.
3. Interruption policy + priority table; toggle-cancel.
4. Wheel/menu binding (data-driven list, not hard-coded).

**DoD (gate 5):**

- [ ] Full-body emote suspends locomotion; upper-body emote coexists with walking.
- [ ] Fire/damage/move cancel correctly per the priority table.
- [ ] Adding a new emote requires **data only**.

### Phase 6 - Input + replication  (6-9 days)

**Tasks:**

1. Finalize the command layer (§3.11) with rebindable actions.
2. Implement `PlayerAnimState` replication (§3.12); route authority through the server layer.
3. Prediction + correction for local player; interpolation for remote players.
4. Deterministic notifies from replicated state + server time.

**DoD (gate 6):**

- [ ] Remote players' reloads/emotes play from replicated state, not streamed frames.
- [ ] Local actions feel instant (predicted) and correct on server mismatch.
- [ ] No animation-driven gameplay effect can be spoofed by a client.

### Phase 7 - Tuning tools & QA  (ongoing)

**Tasks:**

1. **Live tuning panel** (ImGui): edit every constant in §6 live, hot-reload from config.
2. **Recorder/replayer** for input sequences (regression tests for feel).
3. **Feel validation harness** (§8): fixed input scripts -> captured outputs -> diffs.

**DoD (gate 7):**

- [ ] A designer can tune recoil/ADS/sway without a rebuild.
- [ ] A recorded input script replays identically across runs (within tolerance).
- [ ] CI runs the headless feel harness on synthetic data.

### 5.9 Cross-phase Definition of Done (applies to every PR)

- [ ] **One feature per PR** (project rule).
- [ ] Feature is **complete** - no non-functional stubs (project rule).
- [ ] Data-driven; no magic numbers in logic.
- [ ] No copyrighted data committed; runtime extraction only.
- [ ] Clang-format / clang-tidy clean; follows existing comment/doc style.
- [ ] PR description explains *what*, *why*, and *effects in detail*.
- [ ] Server-authoritative where gameplay truth is involved; client patch only for presentation.
- [ ] Inspector overlay updated to expose the new state.


---

## 6. Conventions: naming, curves, tuning knobs, budgets

### 6.1 Clip naming

```
<archetype>_<state>_<variant>_<dir>
  hc_reload_full_a            // hand cannon, full reload, variant a
  hc_fire_a
  hc_ads_in / hc_ads_loop / hc_ads_out
  rifle_reload_tactical_a
  emote_dance_loop_a
  locomotion_run_fwd
```

### 6.2 Animation curve channels (drive procedural systems from the clip)

Use named curves on clips to author procedural behavior in the graph:

| Curve | Purpose |
|---|---|
| `recoil_alpha` | how much recoil applies this frame of the fire clip |
| `hand_ik_alpha` | 1 = hard-lock hand to grip, 0 = free |
| `weapon_attach_alpha` | weapon parented vs free (reload can detach) |
| `sway_scale` | local sway multiplier (e.g. lower during ADS) |
| `mag_attach` | 1 = mag attached to weapon, 0 = held in hand / world |

### 6.3 Central tuning config (data-driven)

```jsonc
{
  "recoil": {
    "frequency_hz": 9.0,
    "damping_ratio": 1.0,
    "pitch_deg": -2.4,
    "yaw_deg": 0.35,
    "cam_weapon_split": 0.6,
    "recovery_hold_s": 0.12
  },
  "ads": { "fov_hip": 90.0, "fov_aim": 55.0, "in_s": 0.18, "out_s": 0.14 },
  "sway": { "breath_hz": 0.25, "breath_amp_deg": 0.6, "ads_scale": 0.35 },
  "input": { "buffer_window_s": 0.20 }
}
```

### 6.4 Budgets (per frame, PC mid-tier)

| System | Budget |
|---|---|
| Base + weapon graph eval (local) | < 0.4 ms |
| Additive overlays + springs | < 0.2 ms |
| IK (hands + feet) | < 0.3 ms |
| Secondary motion (local) | < 0.5 ms |
| Remote character anim (each, LOD0) | < 0.3 ms |
| Total animation (typical scene) | < 4 ms |

---

## 7. Performance & profiling

- **Profile first.** Use the platform profiler; measure graph eval, skinning, IK separately.
- **LOD animation:** distant characters sample at 15-30 Hz and skip IK/secondary motion.
- **Update rate scaling:** run springs at fixed sub-steps if dt spikes; clamp dt to <= 33 ms to
  avoid spring explosion.
- **Budget for remote players:** animation cost scales with player count; cap detail by distance.
- **Avoid per-frame allocation** in graph/spring code (hot path).
- **Skinning:** GPU skinning for high counts; keep bone counts sane (~60-120 for a character).

---

## 8. QA / feel validation harness

Feel is testable if you define it as **captured output for fixed input**.

### 8.1 Deterministic input scripts

Record an input timeline (buttons + look deltas + dt), replay it, and capture:

- state transitions (with frame timestamps),
- notify firings,
- spring values per frame,
- final camera/weapon transforms.

Store as golden files (synthetic/placeholder data only, safe to commit).

### 8.2 Invariants to assert (unit/regression tests)

```
- reload: after ammo_refill notify, mag == capacity ; before, mag == previous
- interrupt: canceling reload before ammo_refill leaves mag unchanged
- no-dupe: firing during reload never consumes ammo
- buffer: reload pressed within window always plays after current action
- emote: full-body emote never coexists with locomotion displacement > epsilon
- priority: damage always cancels any emote
- determinism: same input script => same transition sequence (within frame tolerance)
```

### 8.3 Feel metrics (quantitative)

| Metric | Target |
|---|---|
| Input-to-first-frame-of-anim latency | < 1 frame (predicted) |
| ADS full-blend time | 0.14-0.20 s |
| Recoil settle to <5% | 0.35-0.55 s |
| Reload cancel responsiveness | immediate |
| Reticle max deviation at rest (sway) | 0.4-1.0 deg |


---

## 9. Agent prompt library (lead-dev level)

These are **ready-to-paste prompts** for a coding agent working in the Sunrise repo. Each has:
role, context, inputs, constraints, deliverable, and acceptance criteria. The shared preamble
below is assumed prepended to every prompt.

### 9.0 Shared preamble (prepend to all prompts)

```
ROLE
You are a senior gameplay/engine engineer working in the "Sunrise" C++20 codebase
(Destiny 2 offline-preservation mod). You write production-quality, data-driven,
server-authoritative-where-relevant code that follows the repo's clang-format,
clang-tidy, and comment style. C++20, CMake, Windows/MSVC primary, Linux cross-build.

HARD CONSTRAINTS
- No copyrighted game data in the repo or release. Read game data at runtime from the
  user's own install only. Never commit clips/meshes/def binaries.
- No live-server targeting, no anti-cheat circumvention. Offline legacy build only.
- One feature per PR. Complete implementations only - no non-functional stubs.
- No magic numbers in logic: all tuning values come from a data-driven config.
- Client patches only for presentation; gameplay truth routes through the server layer.
- Keep the hot path allocation-free.

REPO MAP (use these)
- Sunrise/src/state/content, build_data, content_manifest  -> parsed defs / registry
- Sunrise/src/client/player, movement, input, hooks        -> runtime presentation
- Sunrise/src/server/gameplay, activity                    -> authority
- Sunrise/src/core/ui (ImGui vendored)                     -> debug overlays

OUTPUT FORMAT
1) A short design note (what/why/effects, per repo PR rules).
2) The code as focused diffs/files.
3) Tests + how to run them.
4) A checklist of the acceptance criteria, each marked pass/fail with evidence.
Ask clarifying questions ONLY if a required input is genuinely missing.
```

### 9.1 Prompt - Recon / def enumeration

```
TASK: Build a read-only animation-definition recon tool.
- Add a debug command that walks the parsed content registry and dumps every def whose
  category is in {animation set, animation clip/sequence, skeleton/rig, render model,
  socket/attachment, weapon, emote, movement/character physics, damage/impact}.
- Emit a stable, diffable JSON (sorted keys) to a user-owned cache dir. Do NOT commit it.
- Include: tag id, category, name, and all scalar fields; mark nested refs as ids.
- Fail soft: unknown/opaque defs are logged and skipped, never crash.
ACCEPTANCE:
- Running it on the user's install produces a categorized JSON with counts per category.
- Zero repo files contain extracted data (verify with git status).
- Schema is version-stamped; a build bump logs a clear mismatch warning.
```

### 9.2 Prompt - Data model + registry

```
TASK: Implement versioned POD structs + an immutable registry for animation-relevant defs.
- Structs: WeaponDef, EmoteDef, AnimSetDef, AnimClipDef, SkeletonDef, SocketDef,
  MovementDef, ImpactDef. Fields per the recon output; version-stamp each.
- Registry: load-once, immutable, keyed by tag id, lazy nested resolution.
- Provide typed accessors: archetype/ammo/fire_interval/reload_time for weapons;
  mask/loop/priority/flags for emotes.
- No raw game-memory pointers escape the content layer; use stable ids/handles.
- Unit tests using recorded JSON fixtures from the user cache (not committed).
ACCEPTANCE:
- WeaponDef lookup returns correct values for >=3 sample weapons.
- EmoteDef lookup returns mask/loop/priority for >=3 sample emotes.
- Registry has no mutators after load; ASan/UBSan clean on tests.
```

### 9.3 Prompt - Hierarchical state machine core

```
TASK: Implement a generic hierarchical state machine + clip playback + notify dispatch.
- States: id, parent, on_enter/on_exit, transitions (condition predicates), priority,
  sync_group (leader/follower normalized time).
- Clip playback on the animation clock; fire notifies once per crossing; support loop.
- Notify kinds: GAMEPLAY (replicated, deterministic) vs COSMETIC (may be skipped on LOD).
- Command queue with per-action buffering (window from config, default 0.20 s).
- Event bus: state_entered/exited, notify_fired, command_consumed.
ACCEPTANCE:
- Synthetic weapon transitions IDLE->FIRE->IDLE and IDLE->RELOAD->IDLE; notifies fire once.
- Buffered reload within window plays after the current action.
- Emote priority cancels/gets-cancelled per the priority table.
- Determinism test: same input timeline => identical transition sequence.
```

### 9.4 Prompt - Hand-cannon vertical slice

```
TASK: Implement one hand-cannon end-to-end, data-driven.
- Bind real defs: fire interval, mag size, reload time, spread, recoil params.
- Reload notify timeline: reload_begin, mag_eject, mag_insert, ammo_refill,
  chamber_round, reload_end. Gameplay truth ONLY on notifies, never on clip end.
- Interrupt handling: canceling before ammo_refill leaves mag unchanged (no dupe).
- Dry-fire when ammo==0; auto-reload option from config.
ACCEPTANCE:
- ammo_refill (not clip end) sets mag to capacity.
- Cancel at every phase leaves ammo correct; fire-during-reload consumes nothing.
- All constants come from config; changing config changes behavior with no rebuild.
- Inspector overlay shows weapon_state, clip, clip time, ammo, active notifies.
```


### 9.5 Prompt - Additive recoil spring

```
TASK: Implement additive, critically-damped recoil for camera + weapon.
- Per-axis state (pitch/yaw/kick): x, v. Update: a=-k*x-c*v; v+=a*dt; x+=v*dt.
  omega=2*pi*f; k=omega^2; c=2*zeta*omega.
- On FIRE: v += impulse * omega; set recovery_hold timer (default 0.12 s); spring idles
  during hold so the kick is readable first.
- Split impulse between camera and weapon by config (default 60/40).
- Add muzzle convergence: lerp aim dir toward muzzle dir (tau ~0.08 s).
- Separate aim punch (non-recoverable) from recoverable spring.
- Clamp dt to <= 33 ms; fixed sub-step if needed. Allocation-free update.
ACCEPTANCE:
- Holding fire yields readable, controllable climb that settles in 0.35-0.55 s.
- No spring explosion on dt spikes (verified with a 200 ms frame injection).
- All params live in config; a live overlay can edit them.
```

### 9.6 Prompt - Sway / breathing / ADS

```
TASK: Implement sway, breathing, landing impulse, and ADS blend.
- Breathing: 2-3 sine/noise terms (0.15-0.4 Hz) added to camera+arms; scale by config.
- Sway/bob from velocity + turn rate; viewmodel lags camera via spring (distinct from recoil).
- Landing: downward camera impulse + weapon dip, spring-recovered.
- ADS: single aim scalar 0..1 via smoothstep; drives FOV lerp, weapon aim pose,
  sway multiplier, reticle visibility. Asymmetric in/out times (0.18/0.14 s).
ACCEPTANCE:
- Reticle never perfectly still; sway amplitude scales down while ADS.
- Turning/strafe visibly lags the viewmodel then settles.
- ADS blend has no pop; FOV and weapon pose are synchronized.
```

### 9.7 Prompt - IK & secondary motion

```
TASK: Implement hand/foot IK and spring-bone secondary motion.
- Two-bone analytic IK for hands to weapon grip sockets; run AFTER additive layers.
- Foot IK via ground raycast; blend height/rotation; clamp per-frame delta (no pop).
- Spring bones (verlet or explicit) for straps/cables/antennae with stiffness/damping/
  gravity/drag from config; LOD out beyond N meters; never affect hitboxes.
ACCEPTANCE:
- Hands stay on grips under max recoil (no visible slip).
- Feet conform to slopes; no popping on stairs.
- Secondary motion disabled beyond LOD distance and has zero gameplay effect.
```

### 9.8 Prompt - Emote system

```
TASK: Implement the emote system per section 3.10.
- EmoteDef registry: id, clip_ref, mask (FULL/UPPER), loop, priority, allow_air,
  allow_move, cancel_on_fire, sfx/vfx refs.
- State integration with the priority table; toggle-cancel; hold-last vs loop.
- Full-body suspends locomotion; upper-body coexists with walking.
- Data-driven emote list for the wheel/menu (no hard-coded entries).
ACCEPTANCE:
- Full-body emote blocks locomotion displacement; upper-body does not.
- Fire/damage/move cancel per priority; damage always wins.
- Adding a new emote requires data only (no code change).
```

### 9.9 Prompt - Input command layer + replication

```
TASK: Finalize input command layer and replicate animation state.
- Rebindable actions; edge vs held semantics per archetype; buffered commands.
- PlayerAnimState replication: weapon_state_id, emote_id, emote_flags, state_start_time
  (server clock), ammo, quantized aim_state. Route authority through the server layer.
- Local prediction + correction; remote interpolation (~100 ms buffer).
- Deterministic notifies computed from replicated state + server time.
ACCEPTANCE:
- Remote reloads/emotes play from replicated state, not streamed frames.
- Local actions feel instant; mismatch triggers a correct rollback/snap.
- No client-supplied pose/effect is trusted; spoofing is impossible.
```

### 9.10 Prompt - Live tuning + feel harness

```
TASK: Build live tuning + a deterministic feel harness.
- ImGui panel to edit every constant in section 6.3 live, hot-reload from config.
- Recorder/replayer for input timelines (buttons + look deltas + dt).
- Harness: replay script -> capture transitions/notifies/springs/transforms -> golden diff.
- Assert the invariants in section 8.2; report metrics from section 8.3.
ACCEPTANCE:
- A designer can tune recoil/ADS/sway without a rebuild.
- A recorded script replays identically across runs (within frame tolerance).
- Harness runs headless in CI on synthetic data; no copyrighted data committed.
```


---

## 10. Appendices

### 10.A Consolidated tuning table (starting points)

| System | Constant | Start | Range | Notes |
|---|---|---|---|---|
| Recoil | frequency_hz | 9.0 | 6-14 | higher = snappier |
| Recoil | damping_ratio | 1.0 | 0.7-1.0 | <1 adds bounce |
| Recoil | pitch_deg | -2.4 | -1.5..-4.0 | hand cannon |
| Recoil | yaw_deg | 0.35 | 0.1-0.7 | randomized |
| Recoil | cam_weapon_split | 0.6 | 0.4-0.8 | camera share |
| Recoil | recovery_hold_s | 0.12 | 0.05-0.25 | readability |
| ADS | in_s / out_s | 0.18 / 0.14 | - | asymmetric |
| ADS | fov_hip / fov_aim | 90 / 55 | - | deg |
| Sway | breath_hz | 0.25 | 0.15-0.4 | - |
| Sway | breath_amp_deg | 0.6 | 0.3-1.2 | - |
| Sway | ads_scale | 0.35 | 0.2-0.5 | - |
| Input | buffer_window_s | 0.20 | 0.15-0.25 | forgiveness |
| IK | two_bone_iters | 2 | 1-2 | analytic |
| Secondary | stiffness | 0.35 | - | spring bones |
| Secondary | damping | 0.15 | - | spring bones |

### 10.B Glossary

- **Blend space / blend tree** - weighted mix of clips driven by continuous inputs.
- **Additive layer** - a delta pose added on top of the base; never replaces it.
- **Mask** - subset of bones a layer affects (e.g. upper body).
- **Notify / event** - timestamped callback in a clip; carries gameplay truth.
- **Sync group** - layers sharing normalized time so they don't drift.
- **Viewmodel** - the first-person arms/weapon rig.
- **Socket** - named attachment point on a skeleton (grip, muzzle, eject).
- **Spring-damper** - second-order system used for recoil, sway, secondary motion.
- **Muzzle convergence** - lerp of aim ray toward cosmetic muzzle direction.
- **Prediction / authority** - client predicts, server validates.
- **Definition (def) / tag** - a data object parsed from the game's package files.

### 10.C References (public, non-copyrighted)

- Unreal Engine **Lyra** sample (modern shooter animation architecture, free).
- Unreal docs: Animation Blueprints, Control Rig, Motion Warping, Anim Notifies.
- Unity docs: Animator, Playables, Animation Rigging, Cloth.
- GDC Vault: search "shooter animation", "weapon feel", "procedural recoil",
  "locomotion", "networking the gameplay" (Halo/Destiny-era talks).
- Game animation blogs on spring-damper recoil and additive aim offsets.
- Sunrise `README.md` credits list: tiger-pkg, alkahest, tachyscope, D2TagParser,
  DestinyUnpacker, and friends (parsers/tools - cite them).

### 10.D One-page summary

1. **Layers, not clips.** Base + weapon + additive + secondary + IK, evaluated bottom-up.
2. **Notifies carry gameplay.** Clip end is cosmetic; the contract is the notify time.
3. **Springs create feel.** Recoil/sway/breathing are second-order systems with tunable constants.
4. **Commands, not buttons.** Input is buffered commands consumed by a state machine.
5. **State is replicated, not animation.** Remote players re-simulate from state.
6. **Data-driven everything.** New weapons/emotes are data, never code branches.
7. **Read at runtime, never commit assets.** Preservation, not piracy.
8. **Test feel quantitatively.** Fixed inputs -> captured outputs -> invariants + metrics.

---

*End of document. Generated HTML companion: `documentation/animation-character-systems.html`.*

