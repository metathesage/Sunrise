# Animation Sandbox

A small, **runnable, deterministic** model of the character/weapon animation systems described
in [`../documentation/animation-character-systems.md`](../documentation/animation-character-systems.md).

It exists so the concepts in the doc can be *executed, seen and tested* instead of only read.
It is synthetic: no game data, no assets, no engine. Just the mechanics.

## What it models

| System | Doc section | Notes |
|---|---|---|
| Weapon state machine (Idle/Fire/Reload/ADS) | 3.4 | transitions + priorities |
| Notify / event timeline | 3.5 | `ammo_refill` is the gameplay truth, not clip end |
| Additive recoil spring (camera + weapon) | 3.6 | analytic damped oscillator + recovery hold |
| Sway / breathing / ADS blend | 3.7 | single aim scalar drives FOV + sway |
| Emote priority + masks | 3.10 | full-body blocks movement, upper-body does not |
| Buffered input commands | 3.11 | forgiveness window (default 0.20 s) |
| Layered animation graph | 3.4 | base + additive overlays, weighted per frame |
| Determinism | 8.2 | fixed sub-step; identical replays |

## Build & run (C++)

Requires a C++20 compiler. No Windows toolchain needed.

```bash
cd sandbox
make test      # build + run the invariant tests
make demo      # build + print a scripted trace (ASCII recoil bar)
```

Or with CMake:

```bash
cmake -B build -S sandbox && cmake --build build && ./build/anim_sandbox test
```

Or directly:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isandbox/src \
    -o anim_sandbox sandbox/src/anim_sim.cpp sandbox/src/main.cpp
./anim_sandbox test
```

## Interactive browser sandboxes

There are two self-contained HTML demos.

### 1. `three-sandbox.html` - 3D (Three.js) - the main one

A real 3D sandbox with a **rigged humanoid character**, a first-person **viewmodel**, and an
in-app **developer control panel**:

- **View/Camera:** first-person / third-person toggle, FOV, skeleton overlay, weapon-socket
  gizmos, grid, wireframe. Drag to look/orbit, wheel to zoom, WASD to move.
- **Actions:** Fire, Reload, ADS, Wave (upper-body emote), Dance (full-body emote), Damage, Reset.
  A live **reload notify timeline** with markers, plus an event log.
- **Tuning (runtime):** recoil frequency, damping, pitch kick, recovery hold, camera/weapon split,
  sway amplitude, ADS time, blend time, time scale - applied live.
- **Animation graph:** see the layer tags (loco / weapon / emote) and their blend weights in real
  time; scrub / pause / step the current weapon clip.
- **Tests:** an in-page "Run invariant tests" button.

It loads Three.js from a CDN via an import map, so it needs internet access the first time.

### 2. `animation-sandbox.html` - 2D canvas (lighter)

A minimal 2D canvas version of the same model (viewmodel + HUD + sliders + tests). Useful as a
readable reference next to the C++ core.

> **Opening them:** these are local `.html` files - download and double-click, or if you are in a
> cloud workspace use `htmlpreview.github.io` / the raw URL:
>
> ```
> https://htmlpreview.github.io/?https://raw.githubusercontent.com/metathesage/Sunrise/cline/wygs5kf4/sandbox/three-sandbox.html
> ```

## Tests (mirror doc section 8.2)

The C++ harness (`make test`) and the in-page test buttons both report all invariants passing:

```
reload: ammo unchanged before ammo_refill
reload: refilled AT ammo_refill (not clip end)
cancel: ammo unchanged after interrupt before refill
cancel: returns to Idle
no-dupe: fire during reload consumes nothing
buffer: reload pressed during fire plays after
emote: full-body blocks movement
emote: upper-body allows movement
emote: damage always cancels
recoil: peak equals configured kick
recoil: settles within 1s
recoil: bounded under dt spikes (sub-stepping)
ads: blend time ~ ads_in
determinism: identical transition/event logs across runs
```

## Implementation note: why analytic springs

The naive approach - integrate the spring with Euler (`v += (-k*x - c*v)*dt; x += v*dt`) - is
**unstable** for a stiff 9 Hz recoil spring at 60 fps (an eigenvalue of the update matrix exceeds
1, so the offset explodes exponentially). This sandbox instead uses the **closed-form** solution
of `x'' + 2*zeta*omega*x' + omega^2*x = 0`, which is exact and unconditionally stable at any frame
rate. See `Spring::update` in `src/anim_sim.cpp` (and the JS `Spring` class).

## Files

```
sandbox/
  src/anim_sim.hpp          model declarations
  src/anim_sim.cpp          model implementation (springs, state machine, notifies)
  src/main.cpp              CLI: `test` and `demo`
  Makefile                  portable build
  CMakeLists.txt            CMake build + ctest target
  three-sandbox.html        3D interactive sandbox (Three.js, rigged character + dev panel)
  animation-sandbox.html    2D interactive sandbox (canvas)
```
