# Puts the installer of QA Test Tracker where the testers get it: a folder on the shared drive.
#
#   powershell -ExecutionPolicy Bypass -File installer\publish.ps1 -To M:\QA-Test
#   powershell -ExecutionPolicy Bypass -File installer\publish.ps1          the folder of the last time
#   ... -NoBuild      the installer that is in installer\out, as it is
#
# What it puts there:
#   QATestTracker-<version>-Setup.exe   the installer of the version the project says
#   README.txt                          how to install and use it
#   SHA256.txt                          the installer's checksum
# and it takes away the QATestTracker-*-Setup.exe of other versions that it put there before.
#
# What it never touches: everything else in that folder - the database (data\), its daily
# copies (backups\) and the files of results (attachments\) least of all.
#
# The installer is made first (build-installer.ps1, which builds the program and runs its
# tests) unless -NoBuild says to take the one that is there. The copy is read back and
# compared with what was made: a shared drive that lost a part of it is said.
param(
    [string]$To = "",
    [switch]$NoBuild
)
$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $here
$remembered = Join-Path $here "publish-to.txt"

# Where to: the folder said, else the one of the last time (kept in publish-to.txt, this PC's own).
if ($To -eq "" -and (Test-Path $remembered)) { $To = (Get-Content $remembered -TotalCount 1).Trim() }
if ($To -eq "") { "Say where the testers get the installer: -To <folder on the shared drive>, for example -To M:\QA-Test"; exit 2 }
if (-not (Test-Path -LiteralPath $To -PathType Container)) { "There is no folder $To. If it is on a shared drive, see that the drive is connected. Nothing was published."; exit 2 }

$versionLine = Select-String -Path (Join-Path $repo "CMakeLists.txt") -Pattern 'project\(QATest\s+VERSION\s+([0-9.]+)' | Select-Object -First 1
if (-not $versionLine) { "The version was not found in CMakeLists.txt."; exit 2 }
$version = $versionLine.Matches[0].Groups[1].Value
$name = "QATestTracker-$version-Setup.exe"
$installer = Join-Path $here "out\$name"

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here "build-installer.ps1")
    if ($LASTEXITCODE -ne 0) { "The installer was not made. Nothing was published."; exit 3 }
}
if (-not (Test-Path $installer)) { "There is no installer\out\${name}: run without -NoBuild."; exit 3 }

# The installer, then its checksum and the README - and the copy is what was made.
$hash = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLower()
$target = Join-Path $To $name
Copy-Item -LiteralPath $installer -Destination $target -Force
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLower() -ne $hash) { "The copy in $To is not what was made: the shared drive lost a part of it. Publish again."; exit 4 }
Copy-Item -LiteralPath (Join-Path $repo "README.md") -Destination (Join-Path $To "README.txt") -Force
"$hash  $name" | Set-Content -Encoding ascii -LiteralPath (Join-Path $To "SHA256.txt")

# The installers of other versions that are there: only files called like ours, only in that folder.
Get-ChildItem -LiteralPath $To -Filter "QATestTracker-*-Setup.exe" -File | Where-Object { $_.Name -ne $name } | ForEach-Object {
    Remove-Item -LiteralPath $_.FullName -Force
    "took away the older $($_.Name)"
}

Set-Content -Encoding utf8 -LiteralPath $remembered -Value $To
"published $name  ($([math]::Round((Get-Item $installer).Length / 1MB, 1)) MB) to $To"
"  with README.txt and SHA256.txt; nothing else there was touched."
"Testers run $target - it replaces the version they have and keeps their database."
exit 0
