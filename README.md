| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Eyal_espresso_ESP32 Example

Starts a FreeRTOS task that logs "Eyal_espresso_ESP32" using ESP_LOG macros.

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## Session Release Notes

- Last released version in git: `0.1.6`
- Release commit: `b9c0fd6` (`Release v0.1.6 - add swipe tab navigation`)
- Version numbering reminder for release notes:
  - `X`: major architecture or feature-set changes
  - `Y`: minor functionality additions and bug-fix milestones
  - `Z`: patch/sub-version increments after accepted build+flash cycles
- Latest resolved defect:
  - `DEF-20260306-181051` from `0.1.5`
  - Title: `Screen UI moves right every touch on screen`
  - Current state: resolved by the stable RGB display recipe used for the first UI-stable release.
- What was tried in this session:
  - compared the project against the Waveshare `08_lvgl_Porting` example and aligned the RGB path with the demo where possible
  - enabled `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` in `sdkconfig.defaults`
  - added `psram_trans_align = 64` to the RGB panel config in `main/hardware_init.c`
  - enabled a 10-line RGB bounce buffer in `main/hardware_init.c`; this removed the steady idle flicker and made the screen look stable outside touch/scroll animation
  - tested LVGL direct-mode and full-refresh buffer modes in `main/lvgl_port.h`
  - corrected LVGL flush handoff in `main/lvgl_port.c` to use the active LVGL framebuffer in full-frame modes
  - tried on-demand RGB refresh earlier; it caused a black screen and was reverted
  - tried disabling UI scrolling earlier; it did not fix the defect and was reverted
  - changed the dark-theme UI navigation to use `lv_tabview` swipe behavior again by re-enabling horizontal content scrolling in `main/ui_screen.c`
  - added local wait-sound session helpers in `scripts/start_wait_sound.ps1` and `scripts/stop_wait_sound.ps1`
  - updated the wait-sound worker to stop itself automatically if `Code.exe` is no longer running
- Current technical reading:
  - initial X-offset and redraw corruption were caused by RGB framebuffer handoff / timing, not normal touch callback logic
  - stable baseline found: `psram_trans_align = 64`, 10-line RGB bounce buffer, and `on_bounce_frame_finish` callback registration together produce a visually stable non-animation UI
- What to know when starting fresh next time:
  - start from this README note and inspect `DEF-20260306-181051` in `DefectRegister.rtf`
  - verify the active render mode in `main/lvgl_port.h` before changing flush logic again
  - re-check `main/lvgl_port.c` flush behavior against LVGL v9 buffer ownership rules and the Waveshare example
  - do not disable the 1-second heartbeat timer in `main/ui_screen.c` again as an isolation step; that test resulted in a totally white screen
  - compare panel timing values in `main/hardware_init.c` against the exact board example and test porch/burst changes one at a time
  - after every flash, capture the first 5 seconds of logs and remember there is still an existing startup warning about flash-size mismatch
  - for waits that need user input, use `.\scripts\start_wait_sound.ps1` before asking and `.\scripts\stop_wait_sound.ps1` after the next user reply

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **Eyal_espresso_ESP32** contains one source file in C language [Eyal_espresso_ESP32_main.c](main/Eyal_espresso_ESP32_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_Eyal_espresso_ESP32.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── Eyal_espresso_ESP32_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.

## Project Workflow Rules

- Product requirements and application design shall be maintained in `EyalEspressoRequirements and Design.docx`.
- `README.md` is the workflow/session handoff file; the requirements/design document is the primary place for application requirements, UX intent, architecture decisions, and planned features.
- Repository version is tracked in root `VERSION` with format `X.Y.Z`.
- `X`: major functionality/refactoring changes.
- `Y`: minor bug-fix and incremental functionality changes.
- `Z`: sub-version increment for successful build+flash cycles.
- After a successful build+flash, confirm whether to commit and bump `Z`.
- After every flash, Codex must monitor the target and read at least the first 5 seconds of serial log output.
- Codex must verify that the first 5 seconds of post-flash logs contain no warnings or errors before reporting success.
- Maintain `DefectRegister.rtf` in the repository root as the running defect log.
- For every defect found by Eyal or Codex, add a new entry to `DefectRegister.rtf`.
- Each defect entry must include:
  - a UID in the format `DEF-YYYYMMDD-HHMMSS`
  - the project version from `VERSION` when the defect was found
  - a title
  - a description
  - a status
- New defects must be recorded with status `Open` unless explicitly changed later.
- For every released version, the release notes must include a list of defects resolved in that version.
- Before informing Eyal to run a build, review the VS Code `PROBLEMS` panel and resolve all reported issues.
- After every code change, Codex must perform local update/verification itself before reporting ready:
  - refresh project metadata (`reconfigure` / `compile_commands.json`)
  - run a local build
  - fix all detected issues before asking Eyal to build
- When Eyal asks to create another revision/version, Codex must always commit it to git and verify the commit was created successfully.
- At the start of every coding session, Codex must first load the latest project version from git, then write/update release notes in this README so Eyal can see exactly where work stopped.
- Every time README is changed, Codex must commit README to git immediately, even if there is no project version change.
- When a build succeeds, play the project celebration sound from `sounds/build-success-monkey-1p5x.wav`.
- The selected celebration sound source is `sounds/cartoon_candidates/mixkit-cartoon-monkey-preview.mp3` from Mixkit's monkey/cartoon effects page: `https://mixkit.co/free-sound-effects/monkey/`.
- The stored project playback file is a 1.5x faster version of the selected monkey clip so the pitch is higher and the cue is shorter.
- When waiting for Eyal to do anything required to continue, including replying to a prompt, answering a question, approving a request, or sending the next instruction after work is finished and Codex is idle, play the project wait sound from `sounds/WaitSound.wav`.
- For any such waiting state, play `sounds/WaitSound.wav` once immediately when the wait begins, then if 3 minutes pass without a response from Eyal, play it again and keep repeating it every additional 3 minutes until a response arrives or the task resumes.
- Session hook for the wait sound:
  - at session start, run `.\scripts\stop_wait_sound.ps1` once to clear any stale wait-sound worker from a previous session
  - immediately before sending a prompt, question, or approval request that requires Eyal to respond, run `.\scripts\start_wait_sound.ps1`
  - immediately after Eyal responds, run `.\scripts\stop_wait_sound.ps1`
  - if VS Code closes, the wait-sound worker must terminate itself automatically so no orphan background sound process remains
  - the current working implementation uses `sounds/WaitSound.wav` for runtime playback and a dedicated playback helper in `.\scripts\play_wait_sound.ps1`
  - the repeat path was verified locally with a 20-second test interval before returning to the normal 3-minute rule
- Important inconsistencies, mismatches, or stale notes discovered during work must be explicitly pointed out in project notes before they are forgotten.
  

### Codex and VS Code `PROBLEMS` (Session Rule)

- Codex currently cannot directly read the live VS Code `PROBLEMS` UI panel state by itself in-session.
- Therefore, Codex must use task/build output plus problem matchers as the machine-readable source of diagnostics.
- Required workflow for every coding session:
  - Run `ESP-IDF: Reconfigure (S3)` task.
  - Run `ESP-IDF: Build` task.
  - Verify zero active problems from task output and fix all issues before saying build-ready.
  - If UI-only diagnostics still appear, Eyal should paste the `PROBLEMS` entries and Codex must resolve them before proceeding.
- VS Code tasks in `.vscode/tasks.json` are configured with `presentation.revealProblems: "onProblem"` and GCC problem matcher for build.

References:
- OpenAI Codex issue tracker (feature request for Problems visibility): https://github.com/openai/codex/issues/7078
- VS Code tasks documentation (problem matchers and Problems integration): https://code.visualstudio.com/docs/editor/tasks
- VS Code tasks schema (`problemMatcher`, `presentation.revealProblems`): https://code.visualstudio.com/docs/reference/tasks-appendix

### Helper Scripts

- `./scripts/build_flash_prompt.sh`: runs `idf.py build flash`, then prompts to commit + bump sub-version.
- `./scripts/bump_subversion.sh`: bumps only `Z` in `VERSION`.
- `./scripts/setup_idf_env.sh`: prints/exports recommended local cache + build dir environment.
- `.\scripts\idfw.cmd <idf.py args...>`: Windows wrapper that activates ESP-IDF with execution-policy bypass and forwards arguments to `idf.py`.
- `.\scripts\setup_idf_env.ps1`: Windows PowerShell environment activation helper.
- `.\scripts\build_flash_prompt.ps1`: Windows build+flash wrapper with commit/sub-version prompt.
- `.\scripts\bump_subversion.ps1`: Windows patch-version bump helper.
- `.\scripts\start_wait_sound.ps1`: plays `sounds/WaitSound.wav` once immediately, then starts the hidden repeating wait-sound worker and writes its PID handle to `.cache\wait_sound.pid`.
- `.\scripts\start_wait_sound.ps1`: also exits automatically if no `Code.exe` process remains, which covers VS Code shutdown.
- `.\scripts\play_wait_sound.ps1`: one-shot WAV playback helper used by the repeating wait worker for reliable replay.
- `.\scripts\stop_wait_sound.ps1`: stops the hidden wait-sound worker referenced by `.cache\wait_sound.pid`.
- `./scripts/import_waveshare_examples.sh <path>`: imports external Waveshare ESP-IDF demo examples.
- `docs/REVISION_HISTORY.doc`: Word-compatible revision and latest-features tracker.
- `EyalEspressoRequirements and Design.docx`: primary requirements and design template for the application.

## ESP32-S3-4.3B Setup Notes

- Hardware/software setup guide: `docs/ESP32_S3_TOUCH_LCD_4_3B_SETUP.md`
- This project is configured for Waveshare ESP32-S3-Touch-LCD-4.3B (`800x480` RGB + GT911 touch).
- Known-good RGB display recipe for the first stable UI version:
  - `psram_trans_align = 64`
  - `bounce_buffer_size_px = LCD_H_RES * 10`
  - register RGB completion on `on_bounce_frame_finish` when bounce buffering is enabled
- Build-success sound selection:
  - source file: `sounds/cartoon_candidates/mixkit-cartoon-monkey-preview.mp3`
  - project playback file: `sounds/build-success-monkey-1p5x.wav`
  - processing: played faster at `1.5x`, which also raises the pitch
- Wait sound selection:
  - source file: `sounds/waiting_candidates/orange-game-start-countdown.mp3`
  - project playback files: `sounds/WaitSound.mp3` (source copy) and `sounds/WaitSound.wav` (runtime playback file)
  - usage: play once immediately when waiting begins, then repeat every 3 minutes while still waiting
- Use local cache/build-dir for consistent local builds:
  - `XDG_CACHE_HOME=.cache idf.py -B .idfbuild -DIDF_TARGET=esp32s3 reconfigure`
  - `XDG_CACHE_HOME=.cache idf.py -B .idfbuild build`
- Windows quick commands:
  - `.\scripts\idfw.cmd -DIDF_TARGET=esp32s3 reconfigure`
  - `.\scripts\idfw.cmd build`
  - `.\scripts\idfw.cmd -p COM9 flash monitor`
