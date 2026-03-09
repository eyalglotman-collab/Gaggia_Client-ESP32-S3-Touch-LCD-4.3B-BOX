[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Convert-MojibakeToAscii {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $fixed = $Text

    try {
        $cp1252 = [System.Text.Encoding]::GetEncoding(1252)
        $utf8 = [System.Text.Encoding]::UTF8
        $fixed = $utf8.GetString($cp1252.GetBytes($fixed))
    } catch {
        $fixed = $Text
    }

    $fixed = $fixed.Replace([string][char]0x201C, [string][char]34)
    $fixed = $fixed.Replace([string][char]0x201D, [string][char]34)
    $fixed = $fixed.Replace([string][char]0x2018, [string][char]39)
    $fixed = $fixed.Replace([string][char]0x2019, [string][char]39)
    $fixed = $fixed.Replace([string][char]0x2013, "-")
    $fixed = $fixed.Replace([string][char]0x2014, "-")
    $fixed = $fixed.Replace([string][char]0x2026, "...")
    $fixed = $fixed.Replace([string][char]0x00A0, " ")

    return $fixed
}

function Get-DocxDocumentXml {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DocxPath
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $resolvedPath = (Resolve-Path $DocxPath).Path
    $zip = [System.IO.Compression.ZipFile]::OpenRead($resolvedPath)
    try {
        $entry = $zip.GetEntry("word/document.xml")
        if ($null -eq $entry) {
            throw "word/document.xml not found in $DocxPath"
        }

        $stream = $entry.Open()
        try {
            $reader = New-Object System.IO.StreamReader(
                $stream,
                [System.Text.Encoding]::UTF8,
                $true
            )
            try {
                return $reader.ReadToEnd()
            } finally {
                $reader.Dispose()
            }
        } finally {
            $stream.Dispose()
        }
    } finally {
        $zip.Dispose()
    }
}

function Convert-WordXmlToPlainText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$XmlText
    )

    $text = $XmlText
    $text = [System.Text.RegularExpressions.Regex]::Replace($text, "<w:tab\s*/>", "    ")
    $text = [System.Text.RegularExpressions.Regex]::Replace($text, "</w:p>", [Environment]::NewLine)
    $text = [System.Text.RegularExpressions.Regex]::Replace($text, "<[^>]+>", "")
    $text = $text.Replace("&amp;", "&").Replace("&lt;", "<").Replace("&gt;", ">")
    $text = Convert-MojibakeToAscii -Text $text
    return $text.Trim()
}

$xml = Get-DocxDocumentXml -DocxPath $Path
$plainText = Convert-WordXmlToPlainText -XmlText $xml
$plainText
