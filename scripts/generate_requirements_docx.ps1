[CmdletBinding()]
param()

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$TempDir = Join-Path $ProjectRoot ".cache\\requirements_docx_tmp"
$OutputDocx = Join-Path $ProjectRoot "EyalEspressoRequirements and Design.docx"

# @brief Write a text file with UTF-8 content.
# @details Keeps the docx-generation steps readable while creating the minimal
# OpenXML package files needed for a Word document.
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
    <w:p><w:r><w:t>Eyal Espresso Requirements and Design</w:t></w:r></w:p>
    <w:p><w:r><w:t>Document Status: Working Baseline</w:t></w:r></w:p>
    <w:p><w:r><w:t>Version: 0.1.7 baseline</w:t></w:r></w:p>
    <w:p><w:r><w:t>Owner: Eyal / Codex</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>1. Product Overview</w:t></w:r></w:p>
    <w:p><w:r><w:t>Purpose: Build an ESP32-S3 based espresso-machine UI and control application for the Waveshare 4.3 inch touch LCD hardware.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Problem Statement: The application must provide a stable, touch-friendly machine workflow UI while preserving reliable RGB panel behavior on the target hardware.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Success Criteria: Stable display, responsive touch interaction, clear workflow navigation, and maintainable project documentation and revision tracking.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>2. Users and Use Cases</w:t></w:r></w:p>
    <w:p><w:r><w:t>Primary Users: Espresso machine owner/operator, firmware developer, and tester.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Key Use Cases: View machine status, start/stop brew, enable steam mode, switch brew profiles, adjust temperature, adjust preinfusion, and inspect live header metrics.</w:t></w:r></w:p>
    <w:p><w:r><w:t>User Journey Notes: User lands on a dashboard, moves between tabs by touch or swipe, enters Brew for runtime control, Profiles for recipe selection, and Settings for tuning.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>3. Functional Requirements</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-001: The UI shall provide four main tabs named Home, Brew, Profiles, and Settings.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-002: The UI shall support left and right swipe gestures to change the active tab.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-003: The Home tab shall show the active profile and target temperature.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-004: The Brew tab shall expose Brew/Pump and Steam mode controls.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-005: Enabling Brew shall reset shot time and clear Steam mode.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-006: Enabling Steam mode shall clear Brew mode.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-007: The Profiles tab shall allow one-touch selection of three profiles: Classic 9 Bar, Turbo Shot, and Light Roast.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-008: The Settings tab shall allow tuning target temperature in the range 86 C to 98 C.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-009: The Settings tab shall allow tuning preinfusion time in the range 0 s to 12 s.</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-010: The header shall continuously display operating mode, active profile, target temperature, shot timer, and synthetic pressure feedback.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>4. Non-Functional Requirements</w:t></w:r></w:p>
    <w:p><w:r><w:t>Performance: Screen transitions and control updates shall feel immediate on the ESP32-S3 target.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Reliability: The display path shall remain visually stable without right-shift drift or animation flicker under the validated RGB configuration.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Safety: Brew and Steam shall not remain active together in software state.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Maintainability: Requirements, revision history, defects, and workflow rules shall be tracked in repository documents.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>5. UX and UI Design</w:t></w:r></w:p>
    <w:p><w:r><w:t>Navigation Model: Top tab bar plus left/right swipe navigation across a tabview.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Screen List: Dashboard/Home, Brew, Profiles, Settings.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Visual Style: Dark theme with blue accent, muted text for secondary information, and card-based sections.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Interaction Notes: Use large touch targets, maintain clear selected-tab state, and keep active profile visually highlighted.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>6. System Design</w:t></w:r></w:p>
    <w:p><w:r><w:t>Architecture Summary: ESP-IDF application using LVGL 9.x, RGB panel output, and GT911 touch input on the Waveshare ESP32-S3 4.3B board.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Module Breakdown: main app entry, hardware initialization, LVGL port layer, and ui_screen UI composition/state logic.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Hardware Dependencies: Waveshare ESP32-S3-Touch-LCD-4.3B, RGB LCD, GT911 touch controller, ESP32-S3 with PSRAM.</w:t></w:r></w:p>
    <w:p><w:r><w:t>External Interfaces: LVGL events, ESP-IDF display/touch drivers, serial logs, and local build/flash tooling.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>7. Data and Configuration</w:t></w:r></w:p>
    <w:p><w:r><w:t>Persistent Settings: Target temperature and preinfusion are current tunable values and are candidates for future persistence.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Profiles/Recipes: Current predefined profiles are Classic 9 Bar, Turbo Shot, and Light Roast.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Versioned Config Items: Display stability currently depends on `psram_trans_align = 64`, 10-line bounce buffer sizing, and `on_bounce_frame_finish` callback usage.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>8. Validation and Test Plan</w:t></w:r></w:p>
    <w:p><w:r><w:t>Unit Tests: Add targeted checks where practical for UI state transitions and helper scripts.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Hardware Verification: Rebuild, flash, monitor the first 5 seconds of logs, and visually validate display stability plus touch navigation.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Acceptance Tests: Confirm tab switching, swipe behavior, profile selection, brew/steam mutual exclusion, and settings adjustment on the target panel.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>9. Risks and Open Questions</w:t></w:r></w:p>
    <w:p><w:r><w:t>Known Risks: RGB timing/configuration changes can reintroduce drift, flicker, or black-screen regressions.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Open Questions: Which settings and profile data should be persisted, and what machine-control backend should be bound to the current UI mock controls.</w:t></w:r></w:p>
    <w:p><w:r><w:t>Deferred Decisions: Final production information architecture, persistence format, and real sensor/control integration details.</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>10. Change Log</w:t></w:r></w:p>
    <w:p><w:r><w:t>2026-03-07: Initial working requirements baseline created from current implemented UI and project rules.</w:t></w:r></w:p>
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
