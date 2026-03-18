# Eyal Espresso Client

ESP-IDF firmware for the Waveshare ESP32-S3-Touch-LCD-4.3B display board. This project is the embedded client side of the Eyal Espresso system.

## Purpose

The client runs on the ESP32-S3 and owns:

- Board bring-up for the LCD, touch controller, I2C devices, RTC, SD card, RS485, TWAI, and Wi-Fi support.
- The on-device LVGL user interface shown on the 800x480 touchscreen.
- The low-level Wi-Fi/TCP transport that connects to the espresso server simulator.
- Runtime status reporting and operator-facing recovery flows when initialization or transport fails.

## Verified Architecture

The current architecture in code is:

1. `main/Eyal_espresso_ESP32_main.c`
   The application entry point. It drives staged startup, runs internal checks, initializes hardware and services, and decides whether startup failures are warnings or blocking errors.
2. `main/hardware_init.c`
   Board-specific bring-up for the Waveshare ESP32-S3-Touch-LCD-4.3B hardware, including RGB LCD timing, GT911 touch reset/init, I2C helpers, and backlight control.
3. `main/lvgl_port.c`
   LVGL integration layer that connects the application UI to the RGB display/touch stack.
4. `main/ui_screen.c`
   The full touchscreen UI. It starts with an initialization prompt/splash, then moves into a tabbed application UI with runtime overlays and settings controls.
5. `main/peripherals_manager.c`
   Peripheral services and diagnostics for RS485, TWAI, RTC, SD card, Wi-Fi prechecks, and related runtime helpers.
6. `main/CommunicationFunctions.c`
   The client transport engine. It owns the Wi-Fi/TCP connection state machine, framed message exchange, keepalive/watchdog behavior, scan workflow, reconnect policy, and transport snapshot data for the UI.
7. `main/system_constants.c`
   Shared machine constants and profile defaults used by the UI and runtime logic.

## Transport Model

The client transport is not a generic REST or MQTT client. It is a custom framed protocol over TCP with a mirrored simulator-side implementation.

- Default Wi-Fi SSID: `EyalSimulatorAP`
- Default TCP endpoint: `192.168.4.1:3333`
- Frame start bytes: `0xA5 0x5A`
- Message types include `RESET`, `INITIALIZE`, `CONNECT`, `DISCONNECT`, `KEEPALIVE`, `ERROR`, `ACK`, and `DATA`
- Integrity check: CRC16-CCITT
- Main TopLayer states: `reset -> initialize -> connect -> keepalive_server_receive <-> keepalive_client_send -> error`
- Keepalive supervision uses a 450 ms response window with a 3-window timeout threshold before retry escalation.
- BottomLayer retries are capped at 3, and the Connection Info `Auto Reconnect` toggle controls whether retry exhaustion auto-retries `connect`.
- The UI reads a transport snapshot rather than touching sockets directly

`main/CommunicationFunctions.h` is the clearest public contract for the client link state machine and snapshot model.

## Startup Flow

The boot flow currently works like this:

1. Show the initialization prompt on the LCD.
2. Let the operator choose `Online` or `Offline`.
3. Run internal checks.
4. Initialize board hardware and peripheral managers.
5. Initialize the communication task.
6. Move to the main UI if startup succeeds.
7. If startup fails:
   Online mode treats the failure as blocking and moves to the error flow.
   Offline mode logs the issue as a warning and continues where possible.

One important current behavior from the code: both `Online` and `Offline` modes still keep server communication enabled. The difference today is mainly how startup failures are handled.

## UI Structure

The UI is implemented in `main/ui_screen.c` and is built around a tab view. The current pages are:

- Home
- Brew
- Profiles
- Settings

The UI also includes:

- An initialization splash and failure-confirm flow
- A persistent error screen
- A connection info overlay
- A clock set overlay
- A system constants overlay
- A heartbeat-driven runtime refresh path

## Repository Layout

- `main/`: firmware source code
- `components/`: ESP-IDF components
- `docs/`: requirements, revision history, and transport architecture material
- `scripts/`: local helper scripts for setup, version bumps, docs, sound cues, and build helpers
- `sounds/`: local workflow sound assets
- `VERSION`: project version

## Build and Flash

You must use the local wrapper through `cmd.exe` with an absolute forward-slash path for both build and flash.

```bash
cmd.exe /c C:/Espressif/Eyal_Projects_ESP32_S3/Eyal_espresso_client/scripts/idfw.cmd build
cmd.exe /c C:/Espressif/Eyal_Projects_ESP32_S3/Eyal_espresso_client/scripts/idfw.cmd -p <PORT> flash
```

You must run build and flash sequentially, with `build` first and `flash` second.
For Codex/WSL sessions, this absolute-path `cmd.exe` method is the required build and flash path.

### Claude Code Build Verification (non-interactive shell limitation)

When running inside Claude Code's bash shell, Windows console programs (`idf.py`, `ninja`) write output
to the Windows console buffer rather than the pipe, so no build output is visible and output capture
via `2>&1` or PowerShell redirects does not work.

The build command still runs and exits correctly (exit code 0 = success). Verify the build result using
these two checks instead of looking at idf.py output:

**1. Check the binary exists and has a recent timestamp:**
```bash
ls -la .idfbuild/Eyal_espresso_client.bin
```

**2. Confirm no source changes since the last known-good build:**
```bash
git diff <last-good-commit> HEAD -- main/
```
If the diff is empty, the existing binary in `.idfbuild/` is valid and up to date.

**3. Play the build success sound after confirming a valid binary:**
```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\Eyal_Projects_ESP32_S3\Eyal_espresso_client\scripts\play_build_success_sound.ps1
```

## Session Startup Reminder

For a stable client+server workflow every session:

1. Start the server with `launch_simulator_ui.ps1` from the server repository.
2. If server startup fails, use the terminal diagnostic block printed by the launcher as the source of truth (not only the popup).
3. Keep the server interpreter pinned to `C:\Espressif\Eyal_Projects_ESP32_S3\Eyal_espresso_server_simulator\.venv\Scripts\python.exe` in VS Code.
4. Only after the server is healthy, run client build/flash from this repository using `scripts/idfw.cmd`.

## Current Client/Server Boundary

The client owns the operator UI and the Wi-Fi/TCP client transport. The server side owns the mirrored transport behavior, simulator controls, and host-side serial ownership. The two projects share the same low-level message vocabulary and watchdog assumptions, but they are maintained as separate repositories.

## Architecture Notes

- The codebase already contains transport diagrams and contracts under `docs/architecture/`.
- The framed transport is the critical integration seam between this firmware and the server simulator project.
- The current client design is transport-first. Brew-machine business logic is still lighter than the board/UI/transport foundation.
- You must use the sound cue scripts in `scripts/` as your workflow notifications for build/flash outcomes.
- You must perform all git commits for this repository directly with real git access, not through the sandboxed environment.

You must use this direct-commit method as the reference:

```bash
git add README.md
git -c user.name="Codex" -c user.email="codex@local" commit -m "docs: <message>"
```

## Workspace Review Rules

- You must read all files in the two workspace projects: `Eyal_espresso_client` and `Eyal_espresso_server_simulator`.
- You must review both project architectures and be prepared to make code changes.
- You must read both `README.md` files and follow the rules listed in them.
- You must verify that git is active before making or finalizing changes.

## Session Start Approval Bootstrap

- At the beginning of every new Codex session, before substantial work, Codex must run a pre-approval bootstrap and request saved prefix approvals for common commands.
- Codex must ask for these prefix types first so future commands do not repeatedly prompt:
  - `code --reuse-window --goto <WindowsPath:line:col>`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\start_wait_sound.ps1`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\stop_wait_sound.ps1`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\play_wait_sound.ps1`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Espressif\...\scripts\play_build_success_sound.ps1`
  - Common build/flash wrappers under `C:\Espressif\...\scripts\` that this workspace uses.
- During this bootstrap, Codex must explicitly ask the user to save/remember the prefix rule when the runtime approval UI appears.

You must allow Codex to run any `.exe` file it chooses, as long as that executable only creates or modifies files within `C:\Espressif`.
