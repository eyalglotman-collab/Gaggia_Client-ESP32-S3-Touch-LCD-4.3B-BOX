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
    <w:p><w:r><w:t>Document Status: Draft</w:t></w:r></w:p>
    <w:p><w:r><w:t>Version: 0.1</w:t></w:r></w:p>
    <w:p><w:r><w:t>Owner: Eyal / Codex</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>1. Product Overview</w:t></w:r></w:p>
    <w:p><w:r><w:t>Purpose:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Problem Statement:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Success Criteria:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>2. Users and Use Cases</w:t></w:r></w:p>
    <w:p><w:r><w:t>Primary Users:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Key Use Cases:</w:t></w:r></w:p>
    <w:p><w:r><w:t>User Journey Notes:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>3. Functional Requirements</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-001:</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-002:</w:t></w:r></w:p>
    <w:p><w:r><w:t>FR-003:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>4. Non-Functional Requirements</w:t></w:r></w:p>
    <w:p><w:r><w:t>Performance:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Reliability:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Safety:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Maintainability:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>5. UX and UI Design</w:t></w:r></w:p>
    <w:p><w:r><w:t>Navigation Model:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Screen List:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Visual Style:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Interaction Notes:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>6. System Design</w:t></w:r></w:p>
    <w:p><w:r><w:t>Architecture Summary:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Module Breakdown:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Hardware Dependencies:</w:t></w:r></w:p>
    <w:p><w:r><w:t>External Interfaces:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>7. Data and Configuration</w:t></w:r></w:p>
    <w:p><w:r><w:t>Persistent Settings:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Profiles/Recipes:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Versioned Config Items:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>8. Validation and Test Plan</w:t></w:r></w:p>
    <w:p><w:r><w:t>Unit Tests:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Hardware Verification:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Acceptance Tests:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>9. Risks and Open Questions</w:t></w:r></w:p>
    <w:p><w:r><w:t>Known Risks:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Open Questions:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Deferred Decisions:</w:t></w:r></w:p>
    <w:p/>
    <w:p><w:r><w:t>10. Change Log</w:t></w:r></w:p>
    <w:p><w:r><w:t>Date:</w:t></w:r></w:p>
    <w:p><w:r><w:t>Change:</w:t></w:r></w:p>
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
