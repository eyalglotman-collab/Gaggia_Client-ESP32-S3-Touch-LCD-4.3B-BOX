# Continue From Here

Current proven state:
- The client reaches `Initialize -> Connect -> Keepalive`.
- Wi-Fi association to `EyalSimulatorAP` succeeds.
- DHCP succeeds and the client gets `192.168.4.2`.
- TCP connect to `192.168.4.1:3333` succeeds.
- The remaining failure happens after entering `Keepalive`.

Current recurring faults:
- `Server keepalive timeout`
- `keepalive_counter_mismatch`
- eventual `Wait For Com Reset`

What not to re-debug first:
- Do not start from Wi-Fi bring-up.
- Do not start from early TCP connect failures.
- Do not start from RS485; it is intentionally disabled for active startup/runtime use.

Next debug targets:
1. Server simulator Python mirror:
   - `server/sim/link_state_machine.py`
   - Verify the Python layer is only mirroring bridge-owned connect/keepalive behavior and not inventing extra fault/retry logic.
2. ESP32-C3 bridge keepalive ownership:
   - `firmware/esp32c3_bridge/main/bridge_main.c`
   - Add temporary logs for each TCP keepalive cycle:
     - sent `s_server_live_integer`
     - sent `s_client_live_integer`
     - received `frame->host_live_integer`
     - received `frame->device_live_integer`
     - exact mismatch reason before `keepalive_counter_mismatch`
3. ESP32-S3 client keepalive validation:
   - `main/CommunicationFunctions.c`
   - Compare the client's received keepalive values against the bridge's sent values.
   - Focus on keepalive-response frames, not generic ACK frames.

COM-port workflow reminder:
- After every `flash` or `monitor`, release all exact COM-holder PIDs.
- Use only narrow COM-port cleanup targeting the specific COM port and exact monitor/serial-holder processes.
