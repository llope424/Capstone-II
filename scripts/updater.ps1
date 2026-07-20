# OBD Suite update helper.
#
# A running program cannot replace its own files, so the app downloads the
# update ZIP, launches this script detached (from a temp copy), and exits.
# This script waits for the app to close, extracts the update over the
# install folder, and starts the new version. A log is written next to the
# installed exe for troubleshooting.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File updater.ps1
#          -ProcessId <pid> -Zip <downloaded.zip> -Dest <installDir> [-Exe ObdSuite.exe]

param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$Dest,
    [string]$Exe = "ObdSuite.exe"
)

$log = Join-Path $Dest "update-log.txt"
"[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] Updater started (pid $ProcessId, zip $Zip)" |
    Out-File -FilePath $log -Encoding utf8

try {
    Wait-Process -Id $ProcessId -Timeout 60 -ErrorAction Stop
    "App exited; installing." | Out-File $log -Append -Encoding utf8
} catch {
    # Already exited, or still running after 60 s (then extraction will fail
    # on locked files and the old version remains usable).
    "Wait-Process: $($_.Exception.Message)" | Out-File $log -Append -Encoding utf8
}
Start-Sleep -Seconds 1

try {
    Expand-Archive -LiteralPath $Zip -DestinationPath $Dest -Force -ErrorAction Stop
    "Extracted update to $Dest" | Out-File $log -Append -Encoding utf8
    Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
} catch {
    "Extraction FAILED: $($_.Exception.Message). The previous version is unchanged." |
        Out-File $log -Append -Encoding utf8
}

$exePath = Join-Path $Dest $Exe
if (Test-Path $exePath) {
    "Relaunching $exePath" | Out-File $log -Append -Encoding utf8
    Start-Process -FilePath $exePath -WorkingDirectory $Dest
} else {
    "Exe not found at $exePath - not relaunching." | Out-File $log -Append -Encoding utf8
}
