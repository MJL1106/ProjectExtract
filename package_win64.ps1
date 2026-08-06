# Packages a Win64 build to $OutDir.
#
# -applocaldirectory is the reason players do not get the "Microsoft Visual C++ 2015-2022
# Redistributable (x64) is required" dialog: it copies the MSVC/UCRT DLLs next to the game
# executable so nothing has to be installed. The IncludeAppLocalPrerequisites checkbox in
# Project Settings does NOT do this from the command line -- the flag is the only lever.
# -prereqs additionally ships vc_redist.x64.exe under Engine/Extras/Redist/en-us as a fallback.
#
# Close the Unreal Editor before running.

param(
    [string]$Config = "Shipping",
    [string]$OutDir = "$env:USERPROFILE\Downloads\ExtractionDemo_$(Get-Date -Format 'MMdd')",
    [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.7"
)

$ErrorActionPreference = "Stop"
$Project = Join-Path $PSScriptRoot "Extraction\Extraction.uproject"

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force $OutDir | Out-Null }

& "$EngineDir\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
    -project="$Project" `
    -noP4 -platform=Win64 -clientconfig=$Config `
    -cook -build -stage -pak -archive -archivedirectory="$OutDir" `
    -prereqs -applocaldirectory="$EngineDir\Engine\Binaries\ThirdParty\AppLocalDependencies" `
    -nocompileeditor -unattended -utf8output

if ($LASTEXITCODE -ne 0) { throw "Packaging failed with exit code $LASTEXITCODE" }

# Proof the runtime DLLs landed next to the executable.
$BinDir = Join-Path $OutDir "Windows\Extraction\Binaries\Win64"
$Runtime = @("msvcp140.dll", "vcruntime140.dll", "ucrtbase.dll")
$Missing = $Runtime | Where-Object { -not (Test-Path (Join-Path $BinDir $_)) }
if ($Missing) { throw "App-local runtime missing from ${BinDir}: $($Missing -join ', ')" }

Write-Output "Packaged $Config to $OutDir (app-local runtime verified)"
