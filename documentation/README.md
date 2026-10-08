# Sunrise Documentation

## Animation & character systems

- **[animation-character-systems.md](animation-character-systems.md)** — the source document.
  A from-first-principles engineering reference for Destiny-style character, animation, emote,
  and weapon systems: the layered animation stack, notify/event gameplay sync, the additive
  recoil spring model (with numbers), sway/ADS/IK/secondary motion, the emote system, the input
  command layer, and prediction/authority replication. Also includes the exact phase-by-phase
  build workflow, conventions/budgets, a QA feel harness, and a **lead-level agent prompt
  library** (§9) for driving coding agents in this repo.
- **[animation-character-systems.html](animation-character-systems.html)** — the same document
  as a standalone, styled, self-contained HTML page with a table of contents.

## Runnable sandbox

The systems described in the doc are implemented as a small, testable model in
[`../sandbox/`](../sandbox/README.md):

- **C++ core** (`sandbox/src`) — builds with any C++20 compiler, no Windows toolchain required.
  Run `make test` for the 15 invariant tests from §8.2, or `make demo` for a scripted trace.
- **Interactive HTML** (`sandbox/animation-sandbox.html`) — self-contained browser sandbox with a
  live canvas viewmodel, tuning sliders, HUD, and an in-page "Run tests" button.

### Regenerating the HTML

The HTML is generated from the Markdown (single source of truth) by a dependency-free script:

```bash
python3 tools/md_to_html.py documentation/animation-character-systems.md \
    documentation/animation-character-systems.html \
    --title "Destiny-Style Character, Animation & Emote Systems"
```

> Note: the repository `.gitignore` excludes `/docs/` (official docs live on the project
> website). These working docs are kept in `documentation/` so they are version-controlled.

Edit the Markdown, re-run the command, commit both.

> **Boundary reminder:** these docs describe *behavior and architecture only*. No copyrighted
> game data (clips, meshes, textures, definition binaries) is stored in this repo or the release.
> Game data is read at runtime from the user's own install, per the project's contribution rules.
