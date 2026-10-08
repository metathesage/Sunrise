# Animation Sandbox

A small, **runnable, deterministic** model of the character/weapon animation systems described
in [`../documentation/animation-character-systems.md`](../documentation/animation-character-systems.md).

It exists so the concepts in the doc can be *executed and tested* instead of only read. It is
synthetic: no game data, no assets, no engine. Just the mechanics.

## What it models

| System | Doc section | Notes |
|---|---|---|
| Weapon state machine (Idle/Fire/Reload/ADS/AdsFire) | 3.4 | transitions + priorities |
| Notify / event timeline | 3.5 | `ammo_refill` is the gameplay truth, not clip end |
| Additive recoil spring (camera + weapon) | 3.6 | analytic damped oscillator + recovery hold |
| Sway / breathing / ADS blend | 3.7 | single aim scalar drives FOV + sway |
| Emote priority + masks | 3.10 | full-body blocks movement, upper-body does not |
| Buffered input commands | 3.11 | forgiveness window (default 0.20 s) |
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

## Interactive browser sandbox

Open **`animation-sandbox.html`** in any browser (it is fully self-contained). It re-implements
the same model in JavaScript and adds a live canvas viewmodel, HUD, tuning sliders, and a
"Run tests" button that executes the same 15 invariants.

> Note: opening a local `.html` from the sandbox works by downloading it to your machine.
> If you are viewing this in a cloud workspace, use `htmlpreview.github.io` or the raw URL.

## Tests (mirror doc section 8.2)

```
reload: ammo unchanged before ammo_refill notify
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

Both the C++ harness (`make test`) and the in-page "Run tests" button report **15 passed, 0 failed**.

## Implementation note: why analytic springs

The naive approach — integrate the spring with Euler (`v += (-k*x - c*v)*dt; x += v*dt`) — is
**unstable** for a stiff 9 Hz recoil spring at 60 fps (an eigenvalue of the update matrix exceeds
1, so the offset explodes exponentially). This sandbox instead uses the **closed-form** solution of
`x'' + 2*zeta*omega*x' + omega^2*x = 0`, which is exact and unconditionally stable at any frame
rate. See `Spring::update` in `src/anim_sim.cpp`.

## Files

```
sandbox/
  src/anim_sim.hpp          model declarations
  src/anim_sim.cpp          model implementation (springs, state machine, notifies)
  src/main.cpp              CLI: `test` and `demo`
  Makefile                  portable build
  CMakeLists.txt            CMake build + ctest target
  animation-sandbox.html    interactive browser sandbox (self-contained)
```