# CLAUDE.md — Claude Code Session Reference

> **This file is owned and maintained by Claude Code.**
> Do NOT edit AGENTS.md or README.md to match this file — only this file is updated.
> Source of truth for project rules: `AGENTS.md` and `README.md`.

---

## Branch Context — `release/0.3.0`

- **Managed by**: Claude Code (this branch)
- **Parallel branch**: Managed by Codex under a separate branch
- **Objective**: Allow Eyal to progress with either Claude or Codex independently, then compare progress and code quality between the two AI agents.
- **Rule**: Changes on this branch are authored and committed by Claude. Do not merge Codex-branch changes here without Eyal's explicit review and decision.

---

## SESSION START PROTOCOL (MANDATORY)

At the start of every new coding session with a fresh context, Claude must:

1. Read `AGENTS.md` (project rules).
2. Read `README.md` (architecture, build, and workspace rules).
3. Compare both files against this `CLAUDE.md`.
4. Update this `CLAUDE.md` to reflect the current state of both files.
5. **Provide a textual GAP assessment** (see below) if any rule, instruction, or section in `AGENTS.md` or `README.md` is not covered by — or conflicts with — this `CLAUDE.md`.

### GAP Assessment Format

When a gap is detected, report it inline at session start using this format:

```
GAP ASSESSMENT — <date>
Source: <AGENTS.md | README.md>
Section: <section name or rule number>
Gap: <description of what is missing or out of date in CLAUDE.md>
Action: <updated | added | flagged for user review>
```

If no gaps are found, confirm: `GAP ASSESSMENT: CLAUDE.md is in sync with AGENTS.md and README.md.`

---

## Project Identity

- **Project**: Eyal Espresso Client
- **Hardware**: Waveshare ESP32-S3-Touch-LCD-4.3B
- **Framework**: ESP-IDF
- **Role**: Embedded client side of the Eyal Espresso system (firmware + LVGL UI + Wi-Fi/TCP transport)
- **Companion project**: `Eyal_espresso_server_simulator` (separate repository, same workspace)

---

## Rules from AGENTS.md

### Rule 1 — Build/Flash Commit Prompt
After every successful `idf.py build flash`, ask Eyal whether to commit current changes and create a sub-version release.

### Rule 2 — Versioning Scheme
- Version format: `X.Y.Z` stored in the `VERSION` file.
- `X` (major): Functionality additions/removals and refactoring-level changes.
- `Y` (minor): Bug fixes and smaller functionality changes.
- `Z` (patch/sub-version): Increment on every successful build+flash cycle when changes are accepted.

### Rule 3 — Function and Header Documentation
Every function declaration and definition must have a short header comment block including:
- `@brief` — purpose
- `@details` — implementation notes
- Parameters and return value where applicable

### Rule 4 — Revision Document
Maintain `docs/REVISION_HISTORY.doc` with:
- Sections grouped by major/minor revisions (`X.Y`)
- A short change summary per entry
- A continuously maintained "Latest Version Feature List" section

### Rule 5 — Rule-Gated Build/Flash Sequence
For any build/flash request, execute this gated sequence and report each gate completion:
- Gate 1: quick compliance check of `AGENTS.md`, `README.md`, and `CLAUDE.md`.
- Gate 2: run `scripts/start_wait_sound.ps1`.
- Gate 3: execute requested build/flash steps sequentially only (never parallel flash).
- Gate 4: run `scripts/stop_wait_sound.ps1`.
- Gate 5: on success, run `scripts/play_build_success_sound.ps1`.
- Post-flash monitor capture is optional and only required when explicitly requested.
- If any gate fails, stop immediately and report: `RULE-GATED SEQUENCE BROKEN: <gate>`.

---

## Rules from README.md

### Architecture — Source Files
| File | Role |
|---|---|
| `main/Eyal_espresso_ESP32_main.c` | Entry point, staged startup, hardware/service init |
| `main/hardware_init.c` | Board bring-up: RGB LCD, GT911 touch, I2C, backlight |
| `main/lvgl_port.c` | LVGL integration with RGB display/touch |
| `main/ui_screen.c` | Full touchscreen UI: splash, tab view, overlays |
| `main/peripherals_manager.c` | RS485, TWAI, RTC, SD, Wi-Fi pre-checks |
| `main/CommunicationFunctions.c` | Wi-Fi/TCP state machine, keepalive, reconnect, snapshot |
| `main/system_constants.c` | Shared machine constants and profile defaults |

### Transport Model
- Wi-Fi SSID: `EyalSimulatorAP`
- TCP endpoint: `192.168.4.1:3333`
- Frame start: `0xA5 0x5A`
- Message types: `RESET`, `INITIALIZE`, `CONNECT`, `DISCONNECT`, `KEEPALIVE`, `ERROR`, `ACK`, `DATA`
- Integrity: CRC16-CCITT
- Top-layer states: `reset → initialize → connect → keepalive_server_receive ↔ keepalive_client_send → error`
- Keepalive window: 450 ms, 3-window timeout threshold before retry escalation
- BottomLayer retries: capped at 3; `Auto Reconnect` toggle controls post-exhaustion behavior
- UI reads transport snapshot — never touches sockets directly
- Public contract: `main/CommunicationFunctions.h`

### Startup Flow
1. Show initialization prompt on LCD.
2. Operator selects `Online` or `Offline`.
3. Run internal checks.
4. Initialize board hardware and peripheral managers.
5. Initialize communication task.
6. Move to main UI on success.
7. On failure: Online → blocking error flow; Offline → warning, continue where possible.
> Note: Both modes currently keep server communication enabled. The difference is failure handling only.

### UI Structure (`main/ui_screen.c`)
- Tabs: Home, Brew, Profiles, Settings
- Overlays: initialization splash, failure-confirm, persistent error, connection info, clock set, system constants
- Heartbeat-driven runtime refresh path
- UI title typography rule: use the Brew title style across all page titles.
- Reference title style: uppercase cinematic treatment with `lv_font_montserrat_48` and added letter spacing.

### Repository Layout
- `main/` — firmware source
- `components/` — ESP-IDF components
- `docs/` — requirements, revision history, transport architecture
- `scripts/` — setup, version bumps, docs, sound cues, build helpers
- `sounds/` — workflow sound assets
- `VERSION` — project version

### Build and Flash (MANDATORY METHOD)
```bash
cmd.exe /c C:/Espressif/Eyal_Projects_ESP32_S3/Eyal_espresso_client/scripts/idfw.cmd -p <PORT> build flash
```
- This is the ONLY approved flash method for all sessions (including Codex/WSL).
- Do not use `scripts/flash_hidden.ps1`.
- Do not use `scripts/idfw.ps1`.
- Do not use direct `idf.py` commands.
- Do not use PowerShell `Start-Process` wrappers for flashing.

### Session Startup — Client + Server Workflow
1. Start the server with `launch_simulator_ui.ps1` from the server repository.
2. If server startup fails, use the terminal diagnostic block (not just the popup) as source of truth.
3. Keep the server interpreter pinned to `C:\Espressif\Eyal_Projects_ESP32_S3\Eyal_espresso_server_simulator\.venv\Scripts\python.exe` in VS Code.
4. Only after the server is healthy, run client build/flash via `scripts/idfw.cmd`.

### Current Client/Server Boundary
The client owns the operator UI and the Wi-Fi/TCP client transport. The server side owns mirrored transport behavior, simulator controls, and host-side serial ownership. The two projects share the same low-level message vocabulary and watchdog assumptions, but they are maintained as separate repositories.

### Sound Cues
Use the sound cue scripts in `scripts/` as workflow notifications for build/flash outcomes.

### Rule-Gated Execution Sequence
For every build/flash task, use the exact gated sequence defined in `AGENTS.md` and `README.md`:
compliance check, `start_wait_sound`, sequential execution, `stop_wait_sound`, and success sound.

### Git Commits
Perform all git commits with real git access (not sandboxed). Reference format:
```bash
git add <file>
git -c user.name="Codex" -c user.email="codex@local" commit -m "docs: <message>"
```

### Workspace Review Rules
- Read all files in both workspace projects: `Eyal_espresso_client` and `Eyal_espresso_server_simulator`.
- Review both project architectures before making code changes.
- Read both `README.md` files and follow rules listed in them.
- Verify git is active before making or finalizing changes.

### Session Start Approval Bootstrap (Codex sessions)
Before substantial work, request saved prefix approvals for:
- `code --reuse-window --goto <WindowsPath:line:col>`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\start_wait_sound.ps1`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\stop_wait_sound.ps1`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\play_wait_sound.ps1`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\play_build_success_sound.ps1`
- Common build/flash wrappers under `C:\Espressif\...\scripts\`
- Any `.exe` file that only creates or modifies files within `C:\Espressif`

---

## Architecture Notes
- The client owns the operator UI and the Wi-Fi/TCP client transport.
- The server side owns mirrored transport behavior, simulator controls, and host-side serial ownership.
- The two projects share the same low-level message vocabulary and watchdog assumptions, but they are maintained as separate repositories.
- Transport diagrams and contracts are under `docs/architecture/`.
- The framed transport is the critical integration seam between firmware and server simulator.
- Current design is transport-first; brew-machine business logic is lighter than board/UI/transport.
- Client owns: operator UI, Wi-Fi/TCP client transport.
- Server owns: mirrored transport behavior, simulator controls, host-side serial ownership.
- Both share the same message vocabulary and watchdog assumptions.

---

*Last synced: 2026-03-21 — Updated to include README build-verification and boundary sections; commit template aligned.*
