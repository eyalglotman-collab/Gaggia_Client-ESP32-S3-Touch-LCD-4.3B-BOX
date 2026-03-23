[CmdletBinding()]
param()

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$TempDir = Join-Path $ProjectRoot ".cache\detailed_design_docx_tmp"
$OutputDocx = Join-Path $ProjectRoot "docs\EyalEspressoDetailedDesign.docx"
$Version = (Get-Content (Join-Path $ProjectRoot "VERSION") -Raw).Trim()
$DiagramDir = Join-Path $ProjectRoot "docs\diagrams"
$MaxImageWidthEmu = 6.2 * 914400

# @brief Resolve a writable output path for the generated docx.
# @details Falls back to a sibling *.generated.docx file if the preferred path
# is currently locked by another process.
# @param[in] PreferredPath Intended destination file.
# @return Writable destination path.
function Resolve-DocxOutputPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PreferredPath
    )

    if (-not (Test-Path $PreferredPath)) {
        return $PreferredPath
    }

    try {
        $stream = [System.IO.File]::Open($PreferredPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        $stream.Dispose()
        return $PreferredPath
    } catch {
        $directory = Split-Path -Parent $PreferredPath
        $fileName = [System.IO.Path]::GetFileNameWithoutExtension($PreferredPath)
        $extension = [System.IO.Path]::GetExtension($PreferredPath)
        return (Join-Path $directory ($fileName + ".generated" + $extension))
    }
}

# @brief Write UTF-8 text into a file path.
# @details Uses UTF-8 without BOM so the generated OpenXML package parts remain
# stable and readable by Word.
# @param[in] Path Destination file path.
# @param[in] Content File text content.
function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $utf8)
}

# @brief Create a docx package from named OpenXML parts.
# @details Writes explicit ZIP entries with forward-slash package names so the
# generated file remains valid for Word and other OpenXML consumers.
# @param[in] OutputDocx Destination .docx path.
# @param[in] Parts Hashtable mapping package entry names to source file paths.
function New-DocxPackage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputDocx,
        [Parameter(Mandatory = $true)]
        [hashtable]$Parts
    )

    if (Test-Path $OutputDocx) {
        Remove-Item $OutputDocx -Force
    }

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $fileStream = [System.IO.File]::Open($OutputDocx, [System.IO.FileMode]::CreateNew)
    try {
        $archive = New-Object System.IO.Compression.ZipArchive(
            $fileStream,
            [System.IO.Compression.ZipArchiveMode]::Create,
            $false
        )
        try {
            foreach ($entryName in $Parts.Keys) {
                $entry = $archive.CreateEntry(
                    $entryName,
                    [System.IO.Compression.CompressionLevel]::Optimal
                )
                $entryStream = $entry.Open()
                try {
                    $bytes = [System.IO.File]::ReadAllBytes($Parts[$entryName])
                    $entryStream.Write($bytes, 0, $bytes.Length)
                } finally {
                    $entryStream.Dispose()
                }
            }
        } finally {
            $archive.Dispose()
        }
    } finally {
        $fileStream.Dispose()
    }
}

# @brief Convert a plain paragraph into WordprocessingML.
# @details Blank strings become empty paragraphs; non-empty strings are escaped
# with preserved spacing so ASCII fragments stay readable.
# @param[in] Text Source paragraph text.
# @return WordprocessingML paragraph fragment.
function ConvertTo-ParagraphXml {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Text
    )

    if ([string]::IsNullOrEmpty($Text)) {
        return "<w:p/>"
    }

    $escaped = [System.Security.SecurityElement]::Escape($Text)
    return "<w:p><w:r><w:t xml:space=`"preserve`">$escaped</w:t></w:r></w:p>"
}

# @brief Build a centered caption paragraph.
# @details Used below each rendered diagram for stable review references.
# @param[in] Caption Caption text.
# @return WordprocessingML paragraph fragment.
function New-CaptionParagraphXml {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Caption
    )

    $escaped = [System.Security.SecurityElement]::Escape($Caption)
    return "<w:p><w:pPr><w:jc w:val=`"center`"/></w:pPr><w:r><w:rPr><w:i/></w:rPr><w:t xml:space=`"preserve`">$escaped</w:t></w:r></w:p>"
}

# @brief Build an inline drawing paragraph for an image relationship.
# @details Emits a minimal valid DrawingML block for a PNG stored under
# word/media and linked through document.xml.rels.
# @param[in] RelationshipId Image relationship identifier.
# @param[in] Name Image display name.
# @param[in] WidthEmu Image width in EMUs.
# @param[in] HeightEmu Image height in EMUs.
# @param[in] DocPrId Unique drawing object id.
# @return WordprocessingML paragraph fragment.
function New-DrawingParagraphXml {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelationshipId,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [long]$WidthEmu,
        [Parameter(Mandatory = $true)]
        [long]$HeightEmu,
        [Parameter(Mandatory = $true)]
        [int]$DocPrId
    )

    $escapedName = [System.Security.SecurityElement]::Escape($Name)
    return @"
<w:p>
  <w:pPr><w:jc w:val="center"/></w:pPr>
  <w:r>
    <w:drawing>
      <wp:inline distT="0" distB="0" distL="0" distR="0">
        <wp:extent cx="$WidthEmu" cy="$HeightEmu"/>
        <wp:effectExtent l="0" t="0" r="0" b="0"/>
        <wp:docPr id="$DocPrId" name="$escapedName"/>
        <wp:cNvGraphicFramePr>
          <a:graphicFrameLocks xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" noChangeAspect="1"/>
        </wp:cNvGraphicFramePr>
        <a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">
          <a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">
            <pic:pic xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture">
              <pic:nvPicPr>
                <pic:cNvPr id="$DocPrId" name="$escapedName"/>
                <pic:cNvPicPr/>
              </pic:nvPicPr>
              <pic:blipFill>
                <a:blip r:embed="$RelationshipId"/>
                <a:stretch><a:fillRect/></a:stretch>
              </pic:blipFill>
              <pic:spPr>
                <a:xfrm>
                  <a:off x="0" y="0"/>
                  <a:ext cx="$WidthEmu" cy="$HeightEmu"/>
                </a:xfrm>
                <a:prstGeom prst="rect"><a:avLst/></a:prstGeom>
              </pic:spPr>
            </pic:pic>
          </a:graphicData>
        </a:graphic>
      </wp:inline>
    </w:drawing>
  </w:r>
</w:p>
"@
}

# @brief Collect image metadata needed for OpenXML embedding.
# @details Creates relationship ids, package paths, and scaled dimensions for
# each rendered PNG referenced by the body content.
# @param[in] Markers Unique image marker lines.
# @return Hashtable keyed by file name with embedding metadata.
function Get-ImageMap {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Markers
    )

    Add-Type -AssemblyName System.Drawing
    $imageMap = @{}
    $index = 1
    foreach ($marker in $Markers) {
        $parts = $marker -split "\|", 3
        $fileName = $parts[1]
        if ($imageMap.ContainsKey($fileName)) {
            continue
        }

        $filePath = Join-Path $DiagramDir $fileName
        $image = [System.Drawing.Image]::FromFile($filePath)
        try {
            $widthEmu = [long]($image.Width * 9525)
            $heightEmu = [long]($image.Height * 9525)
            if ($widthEmu -gt $MaxImageWidthEmu) {
                $scale = $MaxImageWidthEmu / $widthEmu
                $widthEmu = [long]($widthEmu * $scale)
                $heightEmu = [long]($heightEmu * $scale)
            }

            $imageMap[$fileName] = @{
                FileName = $fileName
                FilePath = $filePath
                RelationshipId = "rId$index"
                DocPrId = $index + 100
                WidthEmu = $widthEmu
                HeightEmu = $heightEmu
                Target = "media/$fileName"
            }
        } finally {
            $image.Dispose()
        }

        $index += 1
    }

    return $imageMap
}

# @brief Convert ordered body items into document.xml body fragments.
# @details Text lines become paragraphs, while image markers emit a caption and
# a centered inline drawing block.
# @param[in] Items Ordered body items.
# @param[in] ImageMap Hashtable from Get-ImageMap.
# @return Joined XML body fragment.
function ConvertTo-BodyXml {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [AllowEmptyCollection()]
        [string[]]$Items,
        [Parameter(Mandatory = $true)]
        [hashtable]$ImageMap
    )

    $fragments = foreach ($item in $Items) {
        if ($item -match '^\[\[IMAGE\|([^|]+)\|(.+)\]\]$') {
            $fileName = $matches[1]
            $caption = $matches[2]
            $imageInfo = $ImageMap[$fileName]
            New-CaptionParagraphXml -Caption $caption
            New-DrawingParagraphXml `
                -RelationshipId $imageInfo.RelationshipId `
                -Name $fileName `
                -WidthEmu $imageInfo.WidthEmu `
                -HeightEmu $imageInfo.HeightEmu `
                -DocPrId $imageInfo.DocPrId
            "<w:p/>"
            continue
        }

        ConvertTo-ParagraphXml -Text $item
    }

    return ($fragments -join "`n")
}

if (-not (Test-Path $DiagramDir)) {
    throw "Missing diagram directory '$DiagramDir'. Run scripts/generate_transport_diagrams.ps1 first."
}

if (Test-Path $TempDir) {
    Remove-Item $TempDir -Recurse -Force
}

New-Item -ItemType Directory -Force $TempDir | Out-Null
New-Item -ItemType Directory -Force (Join-Path $TempDir "_rels") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $TempDir "word") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $TempDir "word\_rels") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $TempDir "word\media") | Out-Null

$bodyItems = @(
    "Eyal Espresso Detailed Design"
    "Document Status: Working Design Baseline"
    "Project Version Reference: $Version"
    "Owner: Eyal / Claude"
    ""
    "Revision History"
    "v0.3.1 - First working version with CLAUDE"
    "  - Fixed INITIALIZE payload: added required data_ver=1 field (bridge rejects without it)"
    "  - Fixed KEEPALIVE response payload: added required rver=1 field (bridge rejects without it)"
    "  - Raised COMMUNICATION_FRAME_MAX_PAYLOAD from 160 to 256 bytes to accept 170-byte binary DATA frames from bridge"
    "  - Transport now sustains RESET -> INITIALIZE -> CONNECT -> KEEPALIVE_SERVER_RECEIVE - KEEPALIVE_CLIENT_SEND continuously"
    ""
    "1. System Context and Planned Split Architecture"
    "Purpose: this document now covers both the current ESP32-S3 client implementation and the planned simulator-backed communication architecture that introduces an ESP32-C3-SuperMini transport controller between the PC simulator host and the client."
    "Deployment Topology:"
    "  PC Simulator Application -> USB COM link -> ESP32-C3-SuperMini transport controller -> Wi-Fi TCP link -> ESP32-S3 espresso client"
    "Architecture Intent: the PC remains the simulator brain and user-facing operator console, while the ESP32-C3 owns low-level transport bridging, watchdog timing, CRC validation, and Wi-Fi TCP session handling."
    "Client Role: the ESP32-S3 client remains the application endpoint that consumes validated transport messages and exposes upper-level machine behavior, UI state, and compatibility logic."
    "Design Rule: high-level workflow, scenario selection, logging, and simulator intelligence stay on the PC host; low-level transport safety and timing enforcement stay on the dedicated ESP32-C3 controller."
    "[[IMAGE|architecture_transport_split.png|Figure 1. Planned split architecture between the PC simulator, ESP32-C3 transport bridge, and ESP32-S3 client.]]"
    "The architecture diagram is normative for responsibility ownership: only the ESP32-C3 bridge owns low-level Wi-Fi/TCP and USB COM transport, while the PC remains the authoritative supervisory host."
    ""
    "2. Client-Side Responsibility Allocation"
    "Application Entry: main/Eyal_espresso_ESP32_main.c owns startup orchestration, staged initialization, offline or online mode selection handling, failure routing, and transition into the main user interface."
    "UI Layer: main/ui_screen.c owns LVGL screen creation, tab content, modal overlays, live clock rendering, and user interaction logic."
    "Display Layer: main/lvgl_port.c and main/lvgl_port.h own LVGL task execution, display flush behavior, touch feed, and panel integration timing."
    "Hardware Layer: main/hardware_init.c and main/hardware_init.h own board-specific setup such as RGB LCD, CH422G-controlled lines, and low-level panel support."
    "Peripheral Layer: main/peripherals_manager.c and main/peripherals_manager.h are the natural integration point for the planned network-controller link because they already own controller communication, version queries, simulated connection state, and connection snapshots exposed to the UI."
    "Configuration Layer: main/system_constants.c and main/system_constants.h load embedded XML defaults and expose parsed runtime constants to the rest of the system."
    "Planned Low-Level Client Transport Layer: a dedicated communication sub-layer should be inserted below the higher-level controller protocol and above the raw Wi-Fi/TCP transport. This layer validates CRC, watchdog counters, sequencing, and connection state before higher-level controller logic sees any payload."
    ""
    "3. Implemented Client State Machines"
    "3.1 Upper-Level Application State Machine"
    "The upper-level client workflow is the application-facing state machine owned by `main/Eyal_espresso_ESP32_main.c` plus `main/ui_screen.c`."
    "Upper-Level States and Stages:"
    "  boot = hardware init, LVGL init, splash screen, and retained-state restoration"
    "  startup_mode_select = operator chooses offline or online mode"
    "  staged_initialization = internal check, RTC, TF card, Wi-Fi baseline, system constants, communication module, controller status, and version compatibility"
    "  main_ui = tabbed LVGL operator interface with overlays such as Connection Info, Set Clock, and system-information panels"
    "  upper-level error routing = failed startup stages are never ignored; they are handled by one centralized result policy"
    "Upper-Level Startup Rule: Online and Offline run the same staged initialization order. Offline does not skip init steps."
    "Upper-Level Failure Rule: when `offline == false`, a failed startup step is an error that routes to the Error screen. When `offline == true`, the same failure is downgraded to a warning log with detailed step text and startup continues."
    "C Try/Catch Equivalent: the client does not use exceptions, so every init-like startup call must return an explicit status and must be processed by the shared startup result handlers in `main/Eyal_espresso_ESP32_main.c`."
    "Upper-Level Rule: this workflow never performs raw CRC validation or framed parsing directly; it consumes only the summarized snapshot produced by the low-level communication module."
    ""
    "3.2 Implemented Low-Level Communication State Machine"
    "The current client low-level communication module is implemented in `main/CommunicationFunctions.c` and mirrors the server-side transport sequence."
    "States:"
    "  reset = clear framed transport bookkeeping, close the TCP socket, disconnect Wi-Fi if needed, and arm a fresh initialize attempt"
    "  initialize = ensure the Wi-Fi stack exists, load SSID/password/IP/port defaults, run the advisory visibility scan, and wait for the STA interface to obtain an IP address"
    "  connect = open the TCP socket to `192.168.4.1:3333`, send INITIALIZE and CONNECT frames, and wait for `client_connected` or equivalent connect success"
    "  keepalive = send framed keepalive messages every configured period and supervise liveness/ACK timing"
    "  send_data = allow application payload transmission while the same keepalive supervision remains healthy"
    "  error = latch the last fault reason and wait for reset-driven recovery"
    "Primary Implemented State Diagram:"
    "  reset -> initialize -> connect -> keepalive -> send_data"
    "  send_data -> keepalive when the session returns to heartbeat-only supervision"
    "  initialize -> error on Wi-Fi timeout or TCP setup failure"
    "  connect -> error on response timeout or transport loss"
    "  keepalive -> error on watchdog timeout, malformed frame, or transport loss"
    "  send_data -> error on watchdog timeout, malformed frame, or transport loss"
    "  error -> reset"
    "[[IMAGE|low_level_state_machine.png|Figure 2. Mirrored low-level state machine shared by the host, ESP32-C3 bridge, and client low-level layers.]]"
    "Reset State Responsibilities:"
    "  - clear `host_live_integer`, `device_live_integer`, `live_integer`, `sequence`, and partial RX-buffer state"
    "  - close the active TCP socket and clear tcp_connected"
    "  - disconnect the STA session so the next initialize cycle is deterministic"
    "Initialize State Responsibilities:"
    "  - ensure the Wi-Fi driver and netif stack are available"
    "  - apply SSID/password/server constants from the active snapshot config"
    "  - run the advisory AP visibility scan"
    "  - keep trying association within the configured timeout window instead of failing immediately on one scan result"
    "Connect State Responsibilities:"
    "  - create the TCP socket and connect to the configured server endpoint"
    "  - serialize and send the framed INITIALIZE payload followed by framed CONNECT"
    "  - wait for a valid framed response that proves the bridge/client session is ready"
    "Keepalive and Send-Data Responsibilities:"
    "  - increment `host_live_integer` on each keepalive period"
    "  - preserve the latest `device_live_integer` returned by the peer"
    "  - reject stale or malformed framed traffic before any upper-level code consumes payload"
    "Error State Responsibilities:"
    "  - preserve `last_error` for UI visibility"
    "  - stop using the transport path until reset is requested"
    ""
    "4. Implemented Low-Level Packet Envelope and Data Model"
    "The current implementation uses one explicit binary frame envelope for RESET, INITIALIZE, CONNECT, KEEPALIVE, DATA, ERROR, and ACK traffic."
    "Implemented Frame Layout in Byte Order:"
    "  bytes 0..1   = SOF = 0xA5 0x5A"
    "  byte 2       = message_type"
    "  bytes 3..4   = payload_length as little-endian uint16"
    "  bytes 5..8   = host_live_integer as little-endian uint32"
    "  bytes 9..12  = device_live_integer as little-endian uint32"
    "  bytes 13..14 = sequence as little-endian uint16"
    "  bytes 15..N  = payload bytes"
    "  final 2 bytes = CRC16-CCITT over every prior frame byte"
    "Implemented Limits and Construction Rules:"
    "  - the client currently builds payloads up to COMMUNICATION_FRAME_MAX_PAYLOAD (256 bytes, matching bridge BRIDGE_FRAME_MAX_PAYLOAD)"
    "  - the sender computes CRC after the header plus payload bytes are fully populated"
    "  - the receiver validates SOF, validates payload length against the configured maximum, validates CRC, and only then interprets message_type and payload"
    "  - integers are serialized little-endian on both sides"
    "CRC Guidance: integrity belongs entirely to this low-level frame layer so the upper-level state machine can trust any payload it receives from the low-level module."
    "Counter Ownership Recommendation: the client increments `host_live_integer` when it sends KEEPALIVE. The peer returns `device_live_integer`, and both counters are exposed to the UI snapshot so liveness can be reviewed visually."
    "Keep-Alive Recommendation: do not rely on generic DATA packets as proof of health. The current implementation uses dedicated KEEPALIVE traffic every 100 mSec by default."
    ""
    "5. Packet Types and Flow Diagrams"
    "5.1 RESET Packet"
    "Intent: force a true reset of the low-level controller and trigger self-test plus parameter reload."
    "Flow Diagram:"
    "  Host/Simulator -> RESET(packet with parameters or parameter reference)"
    "  Low-Level Controller -> clear state, run self-test"
    "  Low-Level Controller -> RESET_ACK(result, self-test summary)"
    "  Upper Layer -> if ACK ok then move to initialize else move to error handling"
    "[[IMAGE|packet_reset_flow.png|Figure 3. RESET packet flow from host command through low-level reset acknowledgement.]]"
    ""
    "5.2 INITIALIZE Packet"
    "Intent: instruct the low-level controller to prepare Wi-Fi/TCP roles, timing, and buffer state without claiming a healthy session yet."
    "Flow Diagram:"
    "  Host/Simulator -> INITIALIZE(connection profile, timing, role)"
    "  Low-Level Controller -> validate settings and ensure Wi-Fi hardware stack is ready"
    "  Low-Level Controller -> run AP visibility precheck for the configured SSID"
    "  Low-Level Controller -> continue into a bounded 5-second Wi-Fi association retry window even if the SSID is not yet visible"
    "  Low-Level Controller -> INITIALIZE_ACK(status, validation result)"
    "  Upper Layer -> if ACK ok then request connect"
    "[[IMAGE|packet_initialize_flow.png|Figure 4. INITIALIZE packet flow used to validate and prepare low-level transport resources.]]"
    ""
    "Client-Side Implementation Note:"
    "  The ESP32-S3 client communication module now performs lazy Wi-Fi hardware initialization through the shared peripheral manager before reset/connect and scan operations. It performs a blocking SSID visibility precheck as advisory input, but still allows a bounded 5-second Wi-Fi association retry window before reporting that the configured AP is unavailable."
    ""
    "5.3 CONNECT Packet"
    "Intent: enter the active low-level session state."
    "Flow Diagram:"
    "  Host/Simulator -> CONNECT(target endpoint details if required)"
    "  Low-Level Controller -> open socket or accept/connect according to role"
    "  Low-Level Controller -> CONNECT_ACK(connected or failure reason)"
    "  Both Sides -> begin periodic keep-alive exchange every 100 mSec"
    "[[IMAGE|packet_connect_flow.png|Figure 5. CONNECT packet flow leading into the active 100 mSec keep-alive session.]]"
    ""
    "5.4 DISCONNECT Packet"
    "Intent: perform a controlled tear-down without calling it an error."
    "Flow Diagram:"
    "  Host/Simulator -> DISCONNECT(reason)"
    "  Low-Level Controller -> stop periodic forwarding and close low-level session"
    "  Low-Level Controller -> DISCONNECT_ACK"
    "  Upper Layer -> return to reset or idle supervisory state"
    "[[IMAGE|packet_disconnect_flow.png|Figure 6. DISCONNECT packet flow for intentional low-level session teardown.]]"
    ""
    "5.5 KEEPALIVE Packet"
    "Intent: prove forward progress of the host and low-level controller independently of payload traffic."
    "Flow Diagram:"
    "  Every 100 mSec Host/Simulator -> KEEPALIVE(host_live_integer incremented)"
    "  Low-Level Controller -> validate that host_live_integer advanced"
    "  Low-Level Controller -> KEEPALIVE_ACK(device_live_integer, link health, transport status)"
    "  Upper Layer -> validate that device_live_integer and timestamps continue to advance"
    "Timeout Rule: if a message is not answered with forward progress of the expected liveness counter within 100 mSec, the low-level controller shall enter error."
    "[[IMAGE|packet_keepalive_flow.png|Figure 7. KEEPALIVE packet flow with HostLiveInteger ownership on the PC and status acknowledgement from the bridge.]]"
    ""
    "5.6 DATA Packet"
    "Intent: carry application payload once the low-level link is healthy."
    "Flow Diagram:"
    "  Upper Layer -> DATA(payload)"
    "  Low-Level Controller -> validate CRC and framing"
    "  Low-Level Controller -> forward payload over TCP"
    "  Peer -> DATA_ACK or application response"
    "  Upper Layer -> consume payload only after low-level validation"
    "[[IMAGE|packet_data_flow.png|Figure 8. DATA packet flow showing low-level validation before application payload handling.]]"
    ""
    "5.7 ERROR Packet"
    "Intent: report a latched low-level fault with enough context for recovery."
    "Flow Diagram:"
    "  Low-Level Controller detects fault"
    "  Low-Level Controller -> ERROR(error_code, state, last counters, transport summary)"
    "  Upper Layer -> log fault and decide whether to send reset or initialize"
    "  Low-Level Controller remains in error until explicit recovery command"
    "[[IMAGE|packet_error_flow.png|Figure 9. ERROR packet flow used to preserve fault context and drive explicit recovery.]]"
    ""
    "6. Keep-Alive, Watchdog, and Timing Policy"
    "Heartbeat Cadence: bridge-originated keep-alive requests are generated every 300 mSec when the session is healthy."
    "Server Responsibility: the ESP32-C3 side sends authoritative even ServerLiveInteger values and owns keepalive request timing."
    "Client Responsibility: the ESP32-S3 client validates each server request and sends exactly one odd ClientLiveInteger response."
    "Watchdog Trigger: if the expected keepalive progress does not arrive within the 450 mSec response window, the receiving side increments timeout counters and schedules retry escalation."
    "Separation of Concerns: keep-alive and watchdog semantics belong to the low-level transport contract; higher-level controller logic should observe the result of that contract rather than implement duplicate link health inference."
    "Design Recommendation: keep the bridge implementation simple. It should never invent host liveness on behalf of the host because that would hide a frozen simulator process."
    ""
    "7. Client-Side Planned Integration Path"
    "Client Receive Path:"
    "  Wi-Fi/TCP bytes -> client low-level parser -> CRC validation -> liveness validation -> sequence validation -> higher-level controller logic"
    "Client Send Path:"
    "  higher-level controller response -> low-level frame builder -> counters and CRC applied -> Wi-Fi/TCP transmission"
    "Client-Side Supervisory Diagram:"
    "  reset -> initialize -> connect -> connected operation"
    "  connected operation -> error on CRC failure, timeout, malformed frame, or transport loss"
    "  error -> reset or initialize only"
    "Planned Code Ownership: the client repository should isolate this low-level communication behavior inside a dedicated transport-oriented module rather than spreading watchdog and CRC checks across unrelated UI files."
    "Two-Layer Connection Design (Implemented):"
    "  TopLayer states: reset -> initialize -> connect -> keepalive_server_receive <-> keepalive_client_send -> error"
    "  BottomLayer responsibilities: Wi-Fi/TCP auto-connect, checksum (CRC) validation, and up to 3 retries on link loss or checksum failure"
    "  TopLayer responsibilities: sequential liveness supervision, async freeze detection, and transition to error when failures require explicit reset"
    "  KeepAlive Rule: connect completion moves to KeepAliveServerReceive, and each cycle alternates one server request and one client response."
    "Design Diagram Source:"
    "  docs/architecture/top_bottom_layer_state_machine.mmd"
    "Diagram:"
    "  TopLayerReset -> TopLayerInitialize -> TopLayerConnect -> TopLayerKeepAliveServerReceive -> TopLayerKeepAliveClientSend"
    "  TopLayerConnect -> TopLayerError when BottomLayerRetries == 3 OR TopLayerFailures == 3"
    "  TopLayerError -> TopLayerReset only by explicit operator reset"
    ""
    "8. Failure Modes and State Diagrams"
    "A low-level transport fault shall always force a transition into the shared error state before higher-level controller logic consumes any additional payload."
    "Startup Failure Classification Rule: staged initialization failures are classified by startup mode rather than by a separate offline sequence. Online startup escalates to the Error screen; Offline startup logs warnings and continues."
    "[[IMAGE|failure_modes_overview.png|Figure 10. Failure modes and recovery overview for CRC faults, watchdog failures, and transport loss.]]"
    "8.1 CRC Failure"
    "Description: received frame fails CRC validation."
    "Diagram:"
    "  connect -> receive bad frame -> error"
    "Recovery:"
    "  error -> reset"
    "  error -> initialize"
    ""
    "8.2 Host Watchdog Failure"
    "Description: HostLiveInteger stops advancing within the required heartbeat window, indicating the upper-level host may be stuck."
    "Diagram:"
    "  connect -> keepalive timeout without host counter progress -> error"
    "Recovery:"
    "  error -> reset or initialize after operator or supervisory decision"
    ""
    "8.3 Device Watchdog Failure"
    "Description: the device-side keep-alive or status response does not arrive in time."
    "Diagram:"
    "  connect -> expected ACK missing -> error"
    "Recovery:"
    "  error -> reset"
    ""
    "8.4 USB COM Link Failure Between PC and ESP32-C3"
    "Description: PC host can no longer reach the ESP32-C3 bridge over USB."
    "Diagram:"
    "  reset/initialize/connect -> USB session lost -> error"
    "Recovery:"
    "  error -> wait for COM port reappearance -> reset"
    ""
    "8.5 Wi-Fi Association Failure on the ESP32-C3 Bridge"
    "Description: transport controller cannot join the configured network."
    "Diagram:"
    "  initialize -> Wi-Fi association timeout after 5 seconds -> error"
    "Recovery:"
    "  error -> initialize with corrected credentials or reset"
    ""
    "8.5A Configured SSID Not Visible During Client Precheck"
    "Description: the client communication module scans the RF environment during initialize and detects that the configured SSID is not currently visible."
    "Client Error Text: Wi-Fi AP '<ssid>' not available after 5000 ms."
    "Diagram:"
    "  initialize -> ensure Wi-Fi hardware ready -> visibility precheck scan -> bounded 5-second retry window -> configured SSID still unavailable -> error"
    "Recovery:"
    "  error -> update credentials or RF environment -> reset -> initialize"
    ""
    "8.5B TCP Server Not Found or Not Listening"
    "Description: the client has valid Wi-Fi transport, but the configured TCP endpoint cannot be reached as an active listener."
    "Client Error Text: TCP server <ip>:<port> not found or not listening (errno=<n>)."
    "Detection Rule: this wording is used only for socket connect failures that map to refusal, timeout, or host/network unreachable conditions."
    "Diagram:"
    "  initialize -> Wi-Fi has IP -> TCP connect attempt -> listener unreachable/refused -> error"
    "Recovery:"
    "  error -> restore server availability or address configuration -> reset -> initialize -> connect"
    ""
    "8.5C COM Port Not Found on the Simulator Host"
    "Description: this fault exists on the PC simulator or ESP32-C3 bridge side when the USB COM bridge cannot be opened."
    "Client Visibility Rule: the ESP32-S3 client must not claim 'COM port not found' unless the simulator bridge protocol explicitly reports that condition over TCP."
    "Diagram:"
    "  PC simulator/bridge initialize -> COM open failure -> bridge-side error -> optional remote status packet -> client-visible specific COM error"
    "Recovery:"
    "  restore COM device or bridge configuration -> report explicit remote status -> reset or initialize"
    ""
    "8.5D Generic Unknown Transport Failure"
    "Description: transport setup fails at a known stage, but the available evidence does not support a more specific diagnosis."
    "Client Error Text: <stage> failure: <detail>."
    "Diagram:"
    "  initialize/connect -> stage-specific API failure without specific classification -> error"
    "Recovery:"
    "  review stage detail, correct configuration or implementation issue, then reset or initialize"
    ""
    "8.6 TCP Session Loss"
    "Description: the Wi-Fi link may be up but the TCP socket is lost."
    "Diagram:"
    "  connect -> TCP socket loss -> error"
    "Recovery:"
    "  error -> initialize -> connect"
    ""
    "8.7 Malformed Packet or Unsupported Protocol Version"
    "Description: parser detects bad length, unknown version, or unknown mandatory structure."
    "Diagram:"
    "  initialize/connect -> parser reject -> error"
    "Recovery:"
    "  error -> reset after protocol review"
    ""
    "8.8 Intentional Disconnect"
    "Description: upper-level controller requests a clean shutdown."
    "Diagram:"
    "  connect -> disconnect -> reset"
    "Recovery:"
    "  reset -> initialize -> connect when needed"
    ""
    "9. Environment and Build Context"
    "Toolchain: the project uses ESP-IDF 5.5.x for target esp32s3 on Windows, with scripts/idfw.cmd as the normal wrapper for build, flash, and monitor actions."
    "Build Directory: the active build output is generated under .idfbuild."
    "Primary Commands: scripts/idfw.cmd -DIDF_TARGET=esp32s3 reconfigure, scripts/idfw.cmd build, and scripts/idfw.cmd -p COM9 flash."
    "Monitoring Method: post-flash verification should capture at least 20 seconds of serial output; on this host, scripts/monitor_capture.ps1 is the approved fallback when idf.py monitor fails."
    "Verification Workflow: after code changes, reconfigure/build locally; after successful builds, play the configured success sound; after successful flash, run startup log verification before reporting success."
    "Versioning Method: project version is stored in VERSION using X.Y.Z, where X covers major feature/refactor boundaries, Y covers minor functionality or bug-fix milestones, and Z covers accepted successful build/flash patch releases."
    "Current Client Error Mapping: the client now distinguishes between AP-offline/not-visible faults, TCP server/listener availability faults, and generic stage failures. A simulator-host COM-port failure remains a remote-side condition and must be reported by protocol before the client may surface it as a specific COM error."
    ""
    "10. Notes"
    "This document is intended as a software design description baseline. It complements the requirements document and revision history rather than replacing them."
    "The transport architecture described above is intentionally more thorough than the current implementation so future client work can converge on a stable, reviewable low-level communication contract before code is fragmented across layers."
)

$imageMarkers = $bodyItems | Where-Object { $_ -match '^\[\[IMAGE\|' } | Select-Object -Unique
$imageMap = Get-ImageMap -Markers $imageMarkers
$bodyXml = ConvertTo-BodyXml -Items $bodyItems -ImageMap $imageMap
$ResolvedOutputDocx = Resolve-DocxOutputPath -PreferredPath $OutputDocx

$contentTypes = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Default Extension="png" ContentType="image/png"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>
'@

$packageRels = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>
'@

$documentRelationships = @(
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
)
foreach ($imageInfo in $imageMap.Values | Sort-Object RelationshipId) {
    $documentRelationships += "  <Relationship Id=`"$($imageInfo.RelationshipId)`" Type=`"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image`" Target=`"$($imageInfo.Target)`"/>"
}
$documentRelationships += '</Relationships>'
$documentRelationshipsXml = $documentRelationships -join "`n"

$documentXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:wpc="http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas" xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" xmlns:o="urn:schemas-microsoft-com:office:office" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math" xmlns:v="urn:schemas-microsoft-com:vml" xmlns:wp14="http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:w10="urn:schemas-microsoft-com:office:word" xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml" xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" xmlns:wpi="http://schemas.microsoft.com/office/word/2010/wordprocessingInk" xmlns:wne="http://schemas.microsoft.com/office/word/2006/wordml" xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture" mc:Ignorable="w14 wp14">
  <w:body>
$bodyXml
    <w:sectPr>
      <w:pgSz w:w="12240" w:h="15840"/>
      <w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="708" w:footer="708" w:gutter="0"/>
    </w:sectPr>
  </w:body>
</w:document>
"@

$contentTypesPath = Join-Path $TempDir "[Content_Types].xml"
$packageRelsPath = Join-Path $TempDir "_rels\.rels"
$documentPath = Join-Path $TempDir "word\document.xml"
$documentRelsPath = Join-Path $TempDir "word\_rels\document.xml.rels"

Write-Utf8File -Path $contentTypesPath -Content $contentTypes
Write-Utf8File -Path $packageRelsPath -Content $packageRels
Write-Utf8File -Path $documentPath -Content $documentXml
Write-Utf8File -Path $documentRelsPath -Content $documentRelationshipsXml

$parts = @{
    "[Content_Types].xml" = $contentTypesPath
    "_rels/.rels" = $packageRelsPath
    "word/document.xml" = $documentPath
    "word/_rels/document.xml.rels" = $documentRelsPath
}

foreach ($imageInfo in $imageMap.Values) {
    $parts["word/media/$($imageInfo.FileName)"] = $imageInfo.FilePath
}

New-DocxPackage -OutputDocx $ResolvedOutputDocx -Parts $parts

Remove-Item $TempDir -Recurse -Force
Get-Item $ResolvedOutputDocx | Select-Object FullName, Length
