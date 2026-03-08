[CmdletBinding()]
param()

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$TempDir = Join-Path $ProjectRoot ".cache\detailed_design_docx_tmp"
$OutputDocx = Join-Path $ProjectRoot "docs\EyalEspressoDetailedDesign.docx"

# @brief Write UTF-8 text into a file path.
# @details Uses UTF-8 without BOM so the generated OpenXML package parts are
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

if (Test-Path $TempDir) {
    Remove-Item $TempDir -Recurse -Force
}

New-Item -ItemType Directory -Force $TempDir | Out-Null
New-Item -ItemType Directory -Force (Join-Path $TempDir "_rels") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $TempDir "word") | Out-Null

$contentTypes = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>
'@

$rels = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>
'@

$documentXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:wpc="http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas" xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" xmlns:o="urn:schemas-microsoft-com:office:office" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math" xmlns:v="urn:schemas-microsoft-com:vml" xmlns:wp14="http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:w10="urn:schemas-microsoft-com:office:word" xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml" xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" xmlns:wpi="http://schemas.microsoft.com/office/word/2010/wordprocessingInk" xmlns:wne="http://schemas.microsoft.com/office/word/2006/wordml" xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape" mc:Ignorable="w14 wp14">
  <w:body>
    <w:p><w:r><w:t>Eyal Espresso Detailed Design</w:t></w:r></w:p>
    <w:p><w:r><w:t>Document Status: Working Design Baseline</w:t></w:r></w:p>
    <w:p><w:r><w:t>Project Version Reference: 0.2.5</w:t></w:r></w:p>
    <w:p><w:r><w:t>Owner: Eyal / Codex</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>1. Software Architecture</w:t></w:r></w:p>
    <w:p><w:r><w:t>Application Entry: main/Eyal_espresso_ESP32_main.c owns startup orchestration, staged initialization, offline or online mode selection handling, failure routing, and transition into the main user interface.</w:t></w:r></w:p>
    <w:p><w:r><w:t>UI Layer: main/ui_screen.c owns LVGL screen creation, tab content, modal overlays, live clock rendering, and user interaction logic.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Display Layer: main/lvgl_port.c and main/lvgl_port.h own LVGL task execution, display flush behavior, touch feed, and panel integration timing.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Hardware Layer: main/hardware_init.c and main/hardware_init.h own board-specific setup such as RGB LCD, CH422G-controlled lines, and low-level panel support.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Peripheral Layer: main/peripherals_manager.c and main/peripherals_manager.h own RTC, TF card, Wi-Fi, RS485/controller communication, version queries, simulated connection state, and connection snapshots exposed to the UI.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Configuration Layer: main/system_constants.c and main/system_constants.h load embedded XML defaults and expose parsed runtime constants to the rest of the system.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Design Rule: hardware-specific state changes should remain in hardware/peripheral layers; UI code should use narrow APIs rather than writing board control state directly.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>2. Interface Design</w:t></w:r></w:p>
    <w:p><w:r><w:t>Main Navigation: the interface uses a top tab bar with Home, Brew, Profiles, and Settings tabs inside the top 90 percent of the screen after initialization succeeds.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Persistent Element: a clock bar remains visible in the bottom 10 percent of the screen and displays full date and time.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Interaction Model: tabs may be selected by pressing the tab names or swiping left and right across the tab content.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Button Design: action and toggle controls are rendered as button-style surfaces with selected and pressed states instead of plain text toggles.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Settings Layout: settings actions are arranged as symmetric pairs with minimum 10 pixel spacing between adjacent interactive controls.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Overlay Screens: Set Clock, Connection Info, and System Constants use full-screen overlays with scrollable content and a bottom Done button to return to the main UI.</w:t></w:r></w:p>
    <w:p><w:r><w:t>System Constants Presentation: constants are shown as an outline/tree-style text hierarchy rather than raw XML to improve readability on the panel.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Initialization Screens: startup begins with an Offline Mode question, then moves through staged initialization status text, optional failure confirmation, and a dedicated Error screen with Reset if initialization fails.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>3. Configuration</w:t></w:r></w:p>
    <w:p><w:r><w:t>Primary Constants File: main/SystemConstants.xml is embedded into the firmware image and loaded during initialization.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Configuration Content: the constants database includes client version, compatible version, connection details, brew limits, profile count, and default brew profile definitions.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Default Profiles: Classic 9 Bar, Turbo Shot, and Light Roast are defined in the constants file and mapped into the Profiles page at runtime.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Runtime Use: UI slider limits, profile names, default profile targets, connection strings, and client compatibility checks are read from the parsed constants snapshot instead of being hardcoded in each screen.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Document Configuration: requirements are maintained in docs/EyalEspressoRequirements and Design.docx and revision notes are maintained in docs/REVISION_HISTORY.doc.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>4. Environment and Compilation Method</w:t></w:r></w:p>
    <w:p><w:r><w:t>Toolchain: the project uses ESP-IDF 5.5.x for target esp32s3 on Windows, with scripts/idfw.cmd as the normal wrapper for build, flash, and monitor actions.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Build Directory: the active build output is generated under .idfbuild.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Primary Commands: scripts/idfw.cmd -DIDF_TARGET=esp32s3 reconfigure, scripts/idfw.cmd build, and scripts/idfw.cmd -p COM9 flash.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Monitoring Method: post-flash verification should capture at least 20 seconds of serial output; on this host, scripts/monitor_capture.ps1 is the approved fallback when idf.py monitor fails.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Verification Workflow: after code changes, reconfigure/build locally; after successful builds, play the configured success sound; after successful flash, run startup log verification before reporting success.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Versioning Method: project version is stored in VERSION using X.Y.Z, where X covers major feature/refactor boundaries, Y covers minor functionality or bug-fix milestones, and Z covers accepted successful build/flash patch releases.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Initialization Verification: the current startup design includes offline simulation mode, controller initialization checks, and controller version compatibility checks before the main UI is shown.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>5. Notes</w:t></w:r></w:p>
    <w:p><w:r><w:t>This document is intended as a software design description baseline. It complements the requirements document and revision history rather than replacing them.</w:t></w:r></w:p>
    <w:sectPr>
      <w:pgSz w:w="12240" w:h="15840"/>
      <w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="708" w:footer="708" w:gutter="0"/>
    </w:sectPr>
  </w:body>
</w:document>
'@

Write-Utf8File -Path (Join-Path $TempDir "[Content_Types].xml") -Content $contentTypes
Write-Utf8File -Path (Join-Path $TempDir "_rels\\.rels") -Content $rels
Write-Utf8File -Path (Join-Path $TempDir "word\\document.xml") -Content $documentXml

if (Test-Path $OutputDocx) {
    Remove-Item $OutputDocx -Force
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($TempDir, $OutputDocx)
Remove-Item $TempDir -Recurse -Force
Get-Item $OutputDocx | Select-Object FullName, Length
