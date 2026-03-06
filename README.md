| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Eyal_espresso_ESP32 Example

Starts a FreeRTOS task that logs "Eyal_espresso_ESP32" using ESP_LOG macros.

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## Session Release Notes

- Last verified release in git: `0.1.2`
- Release commit: `83364c9` (`Release v0.1.2 - clean build baseline`)
- Current HEAD documentation commits:
  - `eeedac3` docs rule update for version-revision commit enforcement
  - `53c4bb6` docs rule update requiring immediate README commits
- Work stopped previously after establishing the `0.1.2` clean-build baseline and tightening repository workflow rules in `README.md`.
- Next required local verification for this session: run `ESP-IDF: Reconfigure (S3)` and `ESP-IDF: Build`, then resolve any reported problems before reporting build-ready.

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

- Repository version is tracked in root `VERSION` with format `X.Y.Z`.
- `X`: major functionality/refactoring changes.
- `Y`: minor bug-fix and incremental functionality changes.
- `Z`: sub-version increment for successful build+flash cycles.
- After a successful build+flash, confirm whether to commit and bump `Z`.
- Maintain `DefectRegister.rtf` in the repository root as the running defect log.
- For every defect found by Eyal or Codex, add a new entry to `DefectRegister.rtf`.
- Each defect entry must include:
  - a UID in the format `DEF-YYYYMMDD-HHMMSS`
  - the project version from `VERSION` when the defect was found
  - a title
  - a description
  - a status
- New defects must be recorded with status `Open` unless explicitly changed later.
- Before informing Eyal to run a build, review the VS Code `PROBLEMS` panel and resolve all reported issues.
- After every code change, Codex must perform local update/verification itself before reporting ready:
  - refresh project metadata (`reconfigure` / `compile_commands.json`)
  - run a local build
  - fix all detected issues before asking Eyal to build
- When Eyal asks to create another revision/version, Codex must always commit it to git and verify the commit was created successfully.
- At the start of every coding session, Codex must first load the latest project version from git, then write/update release notes in this README so Eyal can see exactly where work stopped.
- Every time README is changed, Codex must commit README to git immediately, even if there is no project version change.
  

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
- `./scripts/import_waveshare_examples.sh <path>`: imports external Waveshare ESP-IDF demo examples.
- `docs/REVISION_HISTORY.doc`: Word-compatible revision and latest-features tracker.

## ESP32-S3-4.3B Setup Notes

- Hardware/software setup guide: `docs/ESP32_S3_TOUCH_LCD_4_3B_SETUP.md`
- This project is configured for Waveshare ESP32-S3-Touch-LCD-4.3B (`800x480` RGB + GT911 touch).
- Use local cache/build-dir for consistent local builds:
  - `XDG_CACHE_HOME=.cache idf.py -B .idfbuild -DIDF_TARGET=esp32s3 reconfigure`
  - `XDG_CACHE_HOME=.cache idf.py -B .idfbuild build`
- Windows quick commands:
  - `.\scripts\idfw.cmd -DIDF_TARGET=esp32s3 reconfigure`
  - `.\scripts\idfw.cmd build`
  - `.\scripts\idfw.cmd -p COM9 flash monitor`
