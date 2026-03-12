# Transport Contract Baseline

## Purpose

This file is the canonical machine-readable design baseline for low-level transport behavior. The `.docx` design documents must match this file and the Mermaid diagrams in the same folder.

## Maintenance Rules

- Update this file together with any `.docx` change that affects workflow, state machines, packets, watchdogs, timing, ownership, or failure handling.
- Use exact code-facing names for states, packet types, counters, modules, and events.
- Keep Mermaid diagrams and the tables in this file consistent with each other.
- Do not leave important behavior only in screenshots or rendered images.

## Ownership Summary

| Item | Owner | Notes |
| --- | --- | --- |
| `HostLiveInteger` | PC host / upper-level simulator side | Must advance every keep-alive period to prove host forward progress. |
| `DeviceLiveInteger` | Low-level device/bridge side | Returned to let the host detect bridge-side stalls independently. |
| CRC validation | Low-level transport layer | Upper-level controller logic should not re-implement integrity checks. |
| Watchdog enforcement | Low-level transport layer on both sides | Missing forward progress forces transition to `error`. |
| Recovery decision | Supervisory host logic | Only explicit `reset` or `initialize` recovers from `error`. |

## State Definitions

| State | Purpose | Entry Actions | Exit Conditions |
| --- | --- | --- | --- |
| `reset` | Clear session state and run self-test. | Clear counters, buffers, stale link ownership, load parameters. | Self-test complete and parameters available. |
| `initialize` | Prepare transport resources without claiming a healthy link. | Validate configuration, prepare parser, prepare Wi-Fi/TCP roles and timers. | Configuration valid and resources ready, or initialization fault occurs. |
| `connect` | Establish and supervise the active low-level link. | Open/accept session, start keep-alive cadence, enforce CRC and sequencing. | Controlled disconnect or fault. |
| `disconnect` | Perform controlled teardown. | Stop forwarding, close transport cleanly, preserve reason. | Teardown complete or teardown fault occurs. |
| `error` | Latch low-level fault and block normal traffic. | Preserve error reason and last counters, stop forwarding payloads. | Explicit `reset` or `initialize` command only. |

## Client Communication Scan State Definitions

| Scan State | Purpose | Entry Actions | Exit Conditions |
| --- | --- | --- | --- |
| `COMMUNICATION_SCAN_STATE_IDLE` | Hold the last stable scan status when no operator-triggered discovery is running. | Preserve or reset scan text baseline. | Operator presses `Scan for Devices`. |
| `COMMUNICATION_SCAN_STATE_REQUESTED` | Capture the operator request and clear the previous text box content immediately. | Empty visible results and queue Wi-Fi scan start. | Background task starts scan successfully or fails immediately. |
| `COMMUNICATION_SCAN_STATE_IN_PROGRESS` | Keep the scan visible as active during the fixed discovery window. | Start non-blocking Wi-Fi AP scan and record scan start timestamp. | Ten-second scan window expires or Wi-Fi scan start fails. |
| `COMMUNICATION_SCAN_STATE_COMPLETE` | Publish the discovered AP list to the Connection Info text box. | Read AP records, format device list, store count and duration. | Next operator scan request or communication reset. |
| `COMMUNICATION_SCAN_STATE_ERROR` | Latch scan-specific failure while preserving the main transport state. | Preserve scan failure reason and stop the current scan workflow. | Next operator scan request or communication reset. |

## Client Communication Scan Transition Table

| Current Scan State | Trigger | Guard / Condition | Action | Next Scan State | Failure Behavior |
| --- | --- | --- | --- | --- | --- |
| `COMMUNICATION_SCAN_STATE_IDLE` | `communication_functions_request_scan()` | Connection Info operator requests discovery | Clear previous text and queue scan start | `COMMUNICATION_SCAN_STATE_REQUESTED` | None |
| `COMMUNICATION_SCAN_STATE_REQUESTED` | Background task tick | Wi-Fi driver accepts scan request | Start non-blocking AP scan, show progress text | `COMMUNICATION_SCAN_STATE_IN_PROGRESS` | Wi-Fi API start failure moves to `COMMUNICATION_SCAN_STATE_ERROR`. |
| `COMMUNICATION_SCAN_STATE_IN_PROGRESS` | Elapsed time reaches 10 seconds | Scan window complete | Stop scan, read AP records, format device list | `COMMUNICATION_SCAN_STATE_COMPLETE` | Record-read failure moves to `COMMUNICATION_SCAN_STATE_ERROR`. |
| `COMMUNICATION_SCAN_STATE_COMPLETE` | New scan request | Operator requests fresh discovery | Empty previous result list and restart workflow | `COMMUNICATION_SCAN_STATE_REQUESTED` | None |
| `COMMUNICATION_SCAN_STATE_ERROR` | New scan request | Operator requests retry | Clear error text and retry scan | `COMMUNICATION_SCAN_STATE_REQUESTED` | Repeated failure stays in `COMMUNICATION_SCAN_STATE_ERROR`. |

## Client Wi-Fi Association Precheck

| Step | Purpose | Action | Success Outcome | Failure Outcome |
| --- | --- | --- | --- | --- |
| `communication_ensure_wifi_stack_ready_locked()` | Guarantee Wi-Fi driver ownership exists before connect/scan logic runs. | Lazily call `peripherals_manager_init_wifi()` from the communication task. | Communication module may proceed into association preparation. | `last_error` records Wi-Fi hardware bring-up failure and the main state machine moves to `error`. |
| `communication_validate_target_ap_visible_locked()` | Give the initialize path an advisory view of AP visibility before association. | Run a blocking AP visibility scan and compare the configured `wifi_ssid` against visible AP records. | The configured SSID is visible, so `esp_wifi_set_config()` and `esp_wifi_connect()` may run with positive precheck evidence. | Empty SSID or scan-path failures still stop initialize immediately; an SSID-not-visible result is advisory only and the client still enters a bounded Wi-Fi retry window. |

## Transition Table

| Current State | Trigger | Guard / Condition | Action | Next State | Timeout / Failure Behavior |
| --- | --- | --- | --- | --- | --- |
| `reset` | Self-test complete | Parameters valid | Prepare initialization inputs | `initialize` | Self-test failure moves to `error`. |
| `initialize` | Initialize command completed | Configuration valid | Arm transport resources and start Wi-Fi association | `connect` | Validation failure, Wi-Fi hardware bring-up failure, or association timeout moves to `error`. A non-visible SSID is tolerated during a bounded retry window. |
| `connect` | Disconnect command | Intentional shutdown requested | Controlled teardown | `disconnect` | Teardown failure moves to `error`. |
| `connect` | Fault detected | CRC fault, watchdog timeout, malformed frame, transport loss | Latch fault and stop forwarding | `error` | Fault is terminal until explicit recovery. |
| `disconnect` | Teardown complete | Resources released | Return to clean baseline | `reset` | Incomplete teardown moves to `error`. |
| `error` | Recovery command | Explicit hard recovery | Clear fault and restart stack | `reset` | No implicit recovery allowed. |
| `error` | Recovery command | Explicit soft recovery | Re-prepare resources without full reset | `initialize` | No implicit recovery allowed. |

## Packet Definitions

| Packet | Purpose | Sender | Receiver | Required Fields | Normal Response | Timeout Rule | Error Handling |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `RESET` | Force hard reset and self-test. | Supervisory host | Low-level peer | `protocol_version`, `message_type`, reset profile/parameters, CRC | `RESET_ACK` | Supervisor expects bounded response time from reset path. | Failure enters `error`. |
| `INITIALIZE` | Prepare low-level resources. | Supervisory host | Low-level peer | endpoint/role parameters, watchdog settings, CRC | `INITIALIZE_ACK` | Initialization must complete before connect window expires. | Validation failure enters `error`. |
| `CONNECT` | Enter active session. | Supervisory host | Low-level peer | connection role or endpoint reference, CRC | `CONNECT_ACK` | Session establishment timeout enters `error`. | Socket/join failure enters `error`. |
| `DISCONNECT` | Controlled teardown. | Supervisory host | Low-level peer | disconnect reason, CRC | `DISCONNECT_ACK` | Teardown must complete in bounded time. | Teardown failure enters `error`. |
| `KEEPALIVE` | Prove host forward progress. | Host side | Low-level peer | incremented `HostLiveInteger`, sequence, CRC | `KEEPALIVE_ACK` with `DeviceLiveInteger` and status | Every 100 mSec. | Missing progress enters `error`. |
| `DATA` | Carry application payload after validation. | Either side | Peer | payload, sequence, CRC | `ACK` or application response | Normal transport timeout policy applies. | Invalid frame is rejected before upper layer sees payload. |
| `ERROR` | Report latched low-level fault. | Faulting side | Supervisory peer | error code, state, last counters, summary, CRC | Recovery command | Immediate supervisory review required. | Link remains in `error`. |
| `SCAN` | Discover available Wi-Fi devices for operator selection. | Client communication task | ESP32-S3 Wi-Fi driver | scan request flag, 10-second window, current STA configuration | formatted AP list in `scan_results` | Operator-visible scan lasts 10 seconds. | Scan API or result-read failure enters `COMMUNICATION_SCAN_STATE_ERROR`. |

## Timing Rules

- Wi-Fi association timeout in the client initialize state is `5000 mSec`.
- Keep-alive cadence is `100 mSec`.
- The host must advance `HostLiveInteger` every keep-alive period.
- The device/bridge should return `DeviceLiveInteger` so liveness is observable in both directions.
- If expected liveness progress is not observed in time, the receiver must transition to `error`.

## Failure Modes

| Failure Mode | Detection Point | Required Action | Allowed Recovery |
| --- | --- | --- | --- |
| CRC failure | Low-level frame parser | Drop frame and latch fault | `reset` or `initialize` |
| Host watchdog failure | Device/bridge side | Assume host stalled and latch fault | `reset` or `initialize` after host recovers |
| Device watchdog failure | Host side | Stop trusting link and latch fault | `reset` |
| USB COM loss | Host or bridge | Stop transport and latch fault | `reset` after COM recovery |
| Wi-Fi association failure | Bridge-side initialize/connect | Latch fault with Wi-Fi status | `initialize` or `reset` |
| Configured SSID not visible | Client communication initialize precheck | Continue Wi-Fi association attempts during the bounded initialize retry window | `reset` after timeout or environment changes |
| TCP server not found / not listening | Client communication TCP socket open | Latch fault with configured server IP/port and socket errno when `connect()` fails with reachability or refusal errors | `reset` or corrected server availability |
| COM port not found on simulator host | PC simulator or ESP32-C3 bridge side only | Must be reported by the bridge/simulator protocol if it needs to appear as a distinct client-visible error | Not directly diagnosable by the client without explicit remote status reporting |
| Generic unknown transport failure | Any transport stage not mapped to a more precise category | Latch stage-specific failure text and stop progressing the state machine | `reset`, `initialize`, or implementation-specific review |
| TCP session loss | Bridge-side connect state | Latch fault and stop forwarding | `initialize` then `connect`, or `reset` |
| Malformed packet / unsupported version | Parser | Reject packet and latch fault | `reset` after protocol correction |
| Intentional disconnect | Supervisor | Controlled shutdown | `reset` then normal reconnect sequence |
| Wi-Fi scan start failure | Client communication scan workflow | Preserve scan error text and stop current scan | New operator scan request |
| Wi-Fi scan result-read failure | Client communication scan workflow | Preserve scan error text and stop current scan | New operator scan request |

## Client Error Message Mapping

| Condition | User-Facing Error Text | Notes |
| --- | --- | --- |
| Configured SSID not available during the bounded initialize retry window | `Wi-Fi AP '<ssid>' not available after 5000 ms` | This is reported only after the client exhausts the initialize retry window. |
| TCP `connect()` fails with `ECONNREFUSED`, `ETIMEDOUT`, `EHOSTUNREACH`, or `ENETUNREACH` | `TCP server <ip>:<port> not found or not listening (errno=<n>)` | Used only when socket-layer evidence supports a missing/unreachable listener diagnosis. |
| Wi-Fi/TCP stage fails without a more precise classification | `<stage> failure: <detail>` | Preserves the failing stage without inventing unsupported root-cause claims. |
| Simulator bridge COM port missing | Not directly shown by the client unless the bridge protocol reports it | The client must not claim `COM port not found` based only on local Wi-Fi/TCP observations. |
