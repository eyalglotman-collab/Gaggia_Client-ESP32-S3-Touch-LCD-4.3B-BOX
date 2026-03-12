# Startup Initialization Contract

## Purpose

This file is the canonical machine-readable design baseline for the client startup initialization policy. The startup flow uses the same ordered initialization sequence for both `Online` and `Offline` startup modes. The only behavioral fork is how failures are classified and surfaced.

## Mode Contract

| Startup Mode | Sequence | Failure Severity | UI Result | Logging Requirement |
| --- | --- | --- | --- | --- |
| `Online` | Full staged initialization sequence | Error | Initialization stops and the Error screen is shown | Log failure as `ESP_LOGE` with step-specific detail |
| `Offline` | Full staged initialization sequence | Warning | Initialization continues to the next step and the Error screen is not shown for startup-step failures | Log failure as `ESP_LOGW` with step-specific detail |

## Startup Selection Default

- The startup prompt shall default to `Offline`.
- The visible countdown shall update once per second for 5 seconds.
- If the operator does nothing, the client shall auto-select `Offline`.
- This default affects only startup failure classification today. It does not yet change the transport/server interface.

## Shared Startup Sequence

Both startup modes shall run the same ordered sequence:

| Order | Step | Code-Facing Entry Point | Notes |
| --- | --- | --- | --- |
| 1 | Internal ESP32 checks | `espresso_run_internal_checks(bool offline)` | Flash-size and heap sanity checks |
| 2 | RTC initialization | `peripherals_manager_init_rtc_now(bool offline)` | Retained clock kept when valid |
| 3 | TF card initialization | `peripherals_manager_init_tf_card(bool offline)` | Mount plus file I/O smoke test |
| 4 | Wi-Fi initialization | `peripherals_manager_init_wifi(bool offline)` | Support stack plus AP scan |
| 5 | System constants load | `system_constants_load()` | Uses the same step policy even though the API itself has no `offline` argument |
| 6 | Communication task initialization | `communication_functions_init(bool offline)` | Same live communication startup in both modes |
| 7 | Controller BIT and status check | `peripherals_manager_request_controller_init(bool offline, ...)` | Same RS485 probe in both modes |
| 8 | Version compatibility query | `peripherals_manager_request_controller_version(bool offline, ...)` | Same live controller version check in both modes |

## Failure Handling Rule

Every startup step must be handled by the centralized startup result policy in `main/Eyal_espresso_ESP32_main.c`.

| Condition | `offline == false` | `offline == true` |
| --- | --- | --- |
| Step returns `ESP_OK` | Continue | Continue |
| Step returns non-`ESP_OK` | Show initialization failure confirmation and route to Error screen | Log warning with detailed step text and continue |
| Controller handshake returns `OFFLINE`, `UNKNOWN`, or `ERROR` status | Treat as fatal startup error | Log warning with controller-status detail and continue |
| Version mismatch | Treat as fatal startup error | Log warning with compatibility detail and continue |

## Try/Catch Equivalent in C

This firmware does not use exceptions. The required equivalent is:

- every init-like startup function returns an explicit result or status
- every startup call site is wrapped by one shared handler
- no startup init call may fail silently or bypass the shared handler

The code-facing handlers that enforce this rule are:

- `espresso_handle_init_step_result(bool offline, const char *step_name, esp_err_t ret)`
- `espresso_handle_init_step_issue(bool offline, const char *step_name, const char *detail_text)`

These functions are the required "no exceptions" policy for startup initialization.

## API Requirement

Every public function that performs startup initialization or startup controller bring-up shall accept a binary `offline` argument and document it in both declaration and definition comments.

Required APIs:

- `peripherals_manager_start(bool offline)`
- `peripherals_manager_init_rtc_now(bool offline)`
- `peripherals_manager_init_tf_card(bool offline)`
- `peripherals_manager_init_wifi(bool offline)`
- `communication_functions_init(bool offline)`
- `peripherals_manager_request_controller_init(bool offline, peripherals_controller_status_t *out_status)`
- `peripherals_manager_request_controller_version(bool offline, char *out_version, size_t out_len)`

## Logging Requirement

- Online startup failures must log as errors before routing to the Error screen.
- Offline startup failures must log as warnings and must include the failing step name plus human-readable detail text.
- Startup code shall not invent a different sequence for Offline mode unless this file is updated first.
- A later design phase may fork the Gaggia/server-facing interface based on `offline`, but that fork is not part of the current startup contract.
