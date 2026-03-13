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
| `ServerLiveInteger` | ESP32-C3 server side | The server owns keepalive initiation and sends the next server-side value first. |
| `ClientLiveInteger` | ESP32-S3 client side | The client validates `ServerLiveInteger`, increments `ClientLiveInteger`, and returns it to the server. |
| CRC validation | Low-level transport layer | Upper-level controller logic should not re-implement integrity checks. |
| Watchdog enforcement | Low-level transport layer on both sides | Missing forward progress forces a reconnect attempt through `connect`. |
| `ConnectionFault` latch | ESP32-S3 client side | Latched after 5 sequential keepalive failures and cleared only after a successful keepalive exchange or explicit reset. |

## State Definitions

| State | Purpose | Entry Actions | Exit Conditions |
| --- | --- | --- | --- |
| `reset` | Clear session state and run self-test. | Clear counters, buffers, stale link ownership, load parameters. | Self-test complete and parameters available. |
| `initialize` | Prepare Wi-Fi resources without claiming a healthy link yet. | Validate configuration, prepare parser, prepare Wi-Fi/TCP roles and timers, and zero both counters so the next live value must originate from the server. | Wi-Fi has a valid IP address, or bounded association retries expire. |
| `connect` | Establish or re-establish the active low-level TCP link. | Open/accept session, send `INITIALIZE` and `CONNECT` frames, wait for connect proof. | Connect proof arrives and keepalive may begin, or reconnect must be retried. |
| `keepalive` | Supervise synchronized server/client forward progress. | Validate counters, respond to server keepalive, allow application payloads. | Keepalive failure schedules reconnect through `connect`. |
| `wait_for_com_reset` | Stop automatic reconnect churn after repeated keepalive failures. | Preserve `ConnectionFault`, keep latest failure reason visible, and wait for explicit operator reset. | `Reset Connection` action requests transport reset. |

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
| `communication_ensure_wifi_stack_ready_locked()` | Guarantee Wi-Fi driver ownership exists before connect/scan logic runs. | Lazily call `peripherals_manager_init_wifi()` from the communication task. | Communication module may proceed into association preparation. | `last_error` records Wi-Fi hardware bring-up failure and the main state machine retries through `initialize`. |
| `communication_validate_target_ap_visible_locked()` | Give the initialize path an advisory view of AP visibility before association. | Run a blocking AP visibility scan and compare the configured `wifi_ssid` against visible AP records. | The configured SSID is visible, so `esp_wifi_set_config()` and `esp_wifi_connect()` may run with positive precheck evidence. | Empty SSID or scan-path failures still stop initialize immediately; an SSID-not-visible result is advisory only and the client still enters a bounded Wi-Fi retry window. |

## Transition Table

| Current State | Trigger | Guard / Condition | Action | Next State | Timeout / Failure Behavior |
| --- | --- | --- | --- | --- | --- |
| `reset` | Self-test complete | Parameters valid | Prepare initialization inputs | `initialize` | Self-test failure retries through `initialize`. |
| `initialize` | Wi-Fi association completes | Valid local IP acquired | Hand control to TCP connect logic | `connect` | Wi-Fi association retries continue during the bounded timeout window; timeout returns to `reset`. |
| `connect` | TCP socket open and connect proof received | TCP connected and peer accepted session | Enable low-level link for keepalive supervision | `keepalive` | Missing connect proof closes the socket and retries through `connect`. |
| `keepalive` | Valid server keepalive received | Counter exchange succeeds | Clear `ConnectionFault`, zero failure counter, keep application payloads enabled | `keepalive` | Keepalive loss closes the socket and returns to `connect`. After 5 sequential keepalive failures, `ConnectionFault` is latched and the client enters `wait_for_com_reset`. |
| `wait_for_com_reset` | Operator presses `Reset Connection` | Explicit recovery requested | Clear fault and restart low-level transport | `reset` | No implicit recovery allowed from this state. |

## Packet Definitions

| Packet | Purpose | Sender | Receiver | Required Fields | Normal Response | Timeout Rule | Error Handling |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `RESET` | Force hard reset and self-test. | Supervisory host | Low-level peer | `protocol_version`, `message_type`, reset profile/parameters, CRC | `RESET_ACK` | Supervisor expects bounded response time from reset path. | Failure enters `error`. |
| `INITIALIZE` | Prepare low-level resources. | Supervisory host | Low-level peer | endpoint/role parameters, watchdog settings, CRC | `INITIALIZE_ACK` | Initialization must complete before connect window expires. | The client zeros both counters before sending `INITIALIZE`, so the first live value after initialization must come from the server. |
| `CONNECT` | Enter active session. | Supervisory host | Low-level peer | connection role or endpoint reference, CRC | `CONNECT_ACK` followed by keepalive traffic | Session establishment timeout closes the socket and retries through `connect`. | Socket/join failure retries through `connect` or `initialize`. |
| `KEEPALIVE` | Prove synchronized server/client forward progress. | ESP32-C3 server side | ESP32-S3 client side | current `ServerLiveInteger`, latest `ClientLiveInteger`, sequence, CRC | client returns `KEEPALIVE` with incremented `ClientLiveInteger`; server validates it and advances the next `ServerLiveInteger` | Every 100 mSec. | Missing counter progression closes the socket, retries through `connect`, and latches `ConnectionFault` into `wait_for_com_reset` after 5 repeated failures. |
| `DATA` | Carry application payload after validation. | Either side | Peer | payload, sequence, CRC | `ACK` or application response | Normal transport timeout policy applies. | Invalid frame is rejected before upper layer sees payload. |
| `SCAN` | Discover available Wi-Fi devices for operator selection. | Client communication task | ESP32-S3 Wi-Fi driver | scan request flag, 10-second window, current STA configuration | formatted AP list in `scan_results` | Operator-visible scan lasts 10 seconds. | Scan API or result-read failure enters `COMMUNICATION_SCAN_STATE_ERROR`. |

## Timing Rules

- Wi-Fi association timeout in the client initialize state is `5000 mSec`.
- Keep-alive cadence is `100 mSec`.
- The ESP32-C3 server side owns keepalive initiation and resets both counters to `0` every time it enters `connect`.
- The ESP32-S3 client side zeros both counters during `initialize`.
- The ESP32-S3 client side increments `ClientLiveInteger` only after validating the current `ServerLiveInteger`.
- After validating the returned `ClientLiveInteger`, the server advances `ServerLiveInteger` and sends the next keepalive.
- Both counters wrap to `0` on overflow.

## Failure Modes

| Failure Mode | Detection Point | Required Action | Allowed Recovery |
| --- | --- | --- | --- |
| CRC failure | Low-level frame parser | Drop frame, close socket, retry through `connect` | automatic reconnect |
| Host watchdog failure | Device/bridge side | Assume host stalled and retry transport | `connect` or `reset` after host recovers |
| Device watchdog failure | Host side | Stop trusting link, count keepalive failure, retry through `connect` | automatic reconnect until the 5-failure threshold, then explicit reset |
| USB COM loss | Host or bridge | Stop transport and retry after COM recovery | `reset` after COM recovery |
| Wi-Fi association failure | Client initialize state | Retry association during bounded initialize window | automatic retry, then `reset` |
| Configured SSID not visible | Client communication initialize precheck | Continue Wi-Fi association attempts during the bounded initialize retry window | `reset` after timeout or environment changes |
| TCP server not found / not listening | Client communication TCP socket open | Latch fault with configured server IP/port and socket errno when `connect()` fails with reachability or refusal errors | `reset` or corrected server availability |
| COM port not found on simulator host | PC simulator or ESP32-C3 bridge side only | Must be reported by the bridge/simulator protocol if it needs to appear as a distinct client-visible error | Not directly diagnosable by the client without explicit remote status reporting |
| Generic unknown transport failure | Any transport stage not mapped to a more precise category | Preserve stage-specific failure text and retry through `connect` or `initialize` | automatic retry |
| TCP session loss | Bridge-side connect or keepalive state | Close socket and retry through `connect` | automatic retry |
| Malformed packet / unsupported version | Parser | Reject packet and latch fault | `reset` after protocol correction |
| Wi-Fi scan start failure | Client communication scan workflow | Preserve scan error text and stop current scan | New operator scan request |
| Wi-Fi scan result-read failure | Client communication scan workflow | Preserve scan error text and stop current scan | New operator scan request |

## Client Error Message Mapping

| Condition | User-Facing Error Text | Notes |
| --- | --- | --- |
| Configured SSID not available during the bounded initialize retry window | `Wi-Fi AP '<ssid>' not available after 5000 ms` | This is reported only after the client exhausts the initialize retry window. |
| TCP `connect()` fails with `ECONNREFUSED`, `ETIMEDOUT`, `EHOSTUNREACH`, or `ENETUNREACH` | `TCP server <ip>:<port> not found or not listening (errno=<n>)` | Used only when socket-layer evidence supports a missing/unreachable listener diagnosis. |
| Wi-Fi/TCP stage fails without a more precise classification | `<stage> failure: <detail>` | Preserves the failing stage without inventing unsupported root-cause claims. |
| Simulator bridge COM port missing | Not directly shown by the client unless the bridge protocol reports it | The client must not claim `COM port not found` based only on local Wi-Fi/TCP observations. |
