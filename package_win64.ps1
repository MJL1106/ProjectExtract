# Packages a Win64 build to $OutDir.
#
# -applocaldirectory is the reason players do not get the "Microsoft Visual C++ 2015-2022
# Redistributable (x64) is required" dialog: it copies the MSVC DLLs next to the game
# executable so nothing has to be installed. The IncludeAppLocalPrerequisites checkbox in
# Project Settings does NOT do this from the command line -- the flag is the only lever.
# -prereqs additionally ships vc_redist.x64.exe under Engine/Extras/Redist/en-us as a fallback.
#
# The engine's app-local folder also carries the UCRT (ucrtbase.dll + the api-ms-win-* stubs),
# which is for pre-Windows-10 only. On Win10/11 the UCRT is an OS component; an older app-local
# copy shadows the system one and crashes the game at startup. Those files are stripped below.
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
    -noP4 -platform=Win64 -clientconfig="$Config" `
    -cook -build -stage -pak -archive -archivedirectory="$OutDir" `
    -prereqs -applocaldirectory="$EngineDir\Engine\Binaries\ThirdParty\AppLocalDependencies" `
    -CrashReporter -nocompileeditor -unattended -utf8output

if ($LASTEXITCODE -ne 0) { throw "Packaging failed with exit code $LASTEXITCODE" }

$BinDir = Join-Path $OutDir "Windows\Extraction\Binaries\Win64"

# Strip the app-local UCRT. The delete list is read from the engine folder that produced it,
# so a VC runtime DLL can never be caught by it.
$UcrtSource = Join-Path $EngineDir "Engine\Binaries\ThirdParty\AppLocalDependencies\Win64\x64\UCRT"
if (Test-Path $UcrtSource) {
    Get-ChildItem $UcrtSource -Filter *.dll | ForEach-Object {
        $Staged = Join-Path $BinDir $_.Name
        if (Test-Path $Staged) { Remove-Item $Staged -Force }
    }
}

# Proof the VC runtime landed next to the executable and the UCRT did not.
$Runtime = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
$Missing = $Runtime | Where-Object { -not (Test-Path (Join-Path $BinDir $_)) }
if ($Missing) { throw "App-local runtime missing from ${BinDir}: $($Missing -join ', ')" }
$Leftover = Get-ChildItem $BinDir -Filter "api-ms-win-*.dll" -ErrorAction SilentlyContinue
if ($Leftover -or (Test-Path (Join-Path $BinDir "ucrtbase.dll"))) { throw "App-local UCRT still staged in $BinDir" }

Write-Output "Packaged $Config to $OutDir (VC runtime app-local, UCRT stripped)"
