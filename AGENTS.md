# Project Rules

1. Build/Flash Commit Prompt
- After every successful flash cycle (`idf.py flash` with or without `build`), ask whether to commit current changes and create a sub-version release.

2. Versioning Scheme
- The project version uses `X.Y.Z` stored in the `VERSION` file.
- `X` (major): Functionality additions/removals and refactoring-level changes.
- `Y` (minor): Bug fixes and smaller functionality changes.
- `Z` (patch/sub-version): Increment on every accepted successful flash cycle.

3. Function and Header Documentation
- Every function declaration and definition must have a short header comment block.
- The block must include at least:
  - Purpose (`@brief`)
  - Implementation notes (`@details`)
  - Parameters and return value where applicable

4. Revision Document
- Maintain `docs/REVISION_HISTORY.doc` with:
  - Sections grouped by major/minor revisions (`X.Y`)
  - A short change summary per entry
  - A continuously maintained “Latest Version Feature List” section

5. Rule-Gated Build/Flash Sequence (Mandatory)
- For any build/flash request, execute this gated sequence and report each gate completion:
  - Gate 1: quick compliance check of `AGENTS.md`, `README.md`, and `CLAUDE.md`.
  - Gate 2: run `scripts/start_wait_sound.ps1`.
  - Gate 3: execute requested build/flash steps sequentially only (never parallel flash).
  - Gate 4: run `scripts/stop_wait_sound.ps1`.
  - Gate 5: on success, run `scripts/play_build_success_sound.ps1`.
- Post-flash monitor capture is optional and only required when explicitly requested.
- If any gate fails, stop immediately and report: `RULE-GATED SEQUENCE BROKEN: <gate>`.
