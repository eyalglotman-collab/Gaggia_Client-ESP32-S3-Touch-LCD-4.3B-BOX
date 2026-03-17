param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $root ".idfbuild"

. (Join-Path $PSScriptRoot "setup_idf_env.ps1")

$effectiveArgs = @()
if (-not ($IdfArgs -contains "-B")) {
    $effectiveArgs += @("-B", $buildDir)
}

$effectiveArgs += $IdfArgs

idf.py @effectiveArgs
exit $LASTEXITCODE
