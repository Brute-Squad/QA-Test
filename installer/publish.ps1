# Puts QA Test Tracker where the testers get it: a folder on the shared drive - the installer,
# and the test scripts into the team's database.
#
#   powershell -ExecutionPolicy Bypass -File installer\publish.ps1 -To M:\QA-Test
#   powershell -ExecutionPolicy Bypass -File installer\publish.ps1          the folder of the last time
#   ... -Database <file>   the team's database, if it is not <folder>\data\qatest.sqlite
#   ... -NoImport          the installer alone: the database and the scripts are left as they are
#   ... -NoBuild           the installer that is in installer\out, as it is
#
# What it does there:
#   data\qatest.sqlite                  the test scripts (scripts\*.json) are imported into it -
#                                       after a copy of it is made beside its daily copies
#                                       (data\backups\qatest-before-import-<date>-<time>.sqlite);
#                                       an import only adds and updates test cases: runs and
#                                       results are left as they are
#   scripts\*.json                      the test scripts, as they were imported
#   QATestTracker-<version>-Setup.exe   the installer of the version the project says
#   README.txt                          how to install and use it
#   SHA256.txt                          the installer's checksum
# and it takes away the QATestTracker-*-Setup.exe of older versions that it put there before.
#
# A version is published once. Testers tell installers apart by their version: when an
# installer of the same version is there already - or of a newer one - nothing is published.
# Raise the version (CMakeLists.txt: project(QATest VERSION ...)) and publish again.
#
# What it never touches: everything else in that folder - the daily copies of the database
# (data\backups\) and the files of results (attachments\) least of all.
#
# The installer is made first (build-installer.ps1, which builds the program and runs its
# tests) unless -NoBuild says to take the one that is there. The copies are read back and
# compared with what was made: a shared drive that lost a part of one is said.
param(
    [string]$To = "",
    [string]$Database = "",
    [switch]$NoImport,
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

# A version is published once: before anything is built, see what is there.
foreach ($there in Get-ChildItem -LiteralPath $To -Filter "QATestTracker-*-Setup.exe" -File) {
    if ($there.Name -notmatch '^QATestTracker-([0-9]+(\.[0-9]+)+)-Setup\.exe$') { continue }
    $thereVersion = $Matches[1]
    if ([version]$thereVersion -lt [version]$version) { continue }
    if ([version]$thereVersion -eq [version]$version) { "$To has an installer of version $version already ($($there.Name)): testers could not tell another one of $version from it." }
    else { "$To has the newer version $thereVersion ($($there.Name)): $version would take the testers back." }
    "Raise the version in CMakeLists.txt - project(QATest VERSION ...) - and publish again. Nothing was published."
    exit 2
}

# The team's database: the one said, else data\qatest.sqlite in that folder.
if (-not $NoImport) {
    if ($Database -eq "") { $Database = Join-Path $To "data\qatest.sqlite" }
    if (-not (Test-Path -LiteralPath $Database -PathType Leaf)) {
        "There is no database $Database. Say which is the team's with -Database <file>, or publish the installer alone with -NoImport. Nothing was published."
        exit 2
    }
    $scripts = @(Get-ChildItem (Join-Path $repo "scripts") -Filter "*.json" -File)
    if ($scripts.Count -eq 0) { "There are no test scripts in scripts\*.json. Nothing was published."; exit 2 }
}

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here "build-installer.ps1")
    if ($LASTEXITCODE -ne 0) { "The installer was not made. Nothing was published."; exit 3 }
}
if (-not (Test-Path $installer)) { "There is no installer\out\${name}: run without -NoBuild."; exit 3 }

# First the test scripts, into the team's database - with the program the installer is made
# of. It makes a copy of the database first, and imports nothing if it cannot.
if (-not $NoImport) {
    $program = Join-Path $repo "dist\QATest.exe"
    if (-not (Test-Path $program)) { "There is no dist\QATest.exe: run without -NoBuild. Nothing was published."; exit 3 }
    $first = $true
    foreach ($script in $scripts) {
        $arguments = @("--db", $Database, "--import", $script.FullName)
        if ($first) { $arguments += "--copy-first" }
        # (Its answer is read, which also waits for it: it is a program with a window.)
        $ErrorActionPreference = "Continue"
        $said = & $program @arguments 2>&1 | ForEach-Object { if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" } } | Where-Object { $_.Trim() -ne "" }
        $imported = $LASTEXITCODE
        $ErrorActionPreference = "Stop"
        $said | ForEach-Object { "  $_" }
        if ($imported -ne 0) {
            if ($first) { "$($script.Name) was not imported into $Database. Nothing was published." }
            else { "$($script.Name) was not imported into $Database (the scripts before it were). The installer was not published." }
            exit 4
        }
        $first = $false
    }

    # The scripts beside it, as they were imported.
    $scriptsThere = Join-Path $To "scripts"
    New-Item -ItemType Directory -Force $scriptsThere | Out-Null
    foreach ($script in $scripts) {
        $target = Join-Path $scriptsThere $script.Name
        Copy-Item -LiteralPath $script.FullName -Destination $target -Force
        if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $script.FullName).Hash) {
            "The copy of $($script.Name) in $scriptsThere is not what was imported: the shared drive lost a part of it. The installer was not published; publish again."
            exit 4
        }
    }
    "copied the test scripts to $scriptsThere"
}

# The installer, then its checksum and the README - and the copy is what was made.
$hash = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLower()
$target = Join-Path $To $name
Copy-Item -LiteralPath $installer -Destination $target -Force
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLower() -ne $hash) {
    Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
    "The copy in $To was not what was made: the shared drive lost a part of it, and it was taken away again. Publish again."
    exit 5
}
Copy-Item -LiteralPath (Join-Path $repo "README.md") -Destination (Join-Path $To "README.txt") -Force
"$hash  $name" | Set-Content -Encoding ascii -LiteralPath (Join-Path $To "SHA256.txt")

# The installers of older versions that are there: only files called like ours, only in that folder.
Get-ChildItem -LiteralPath $To -Filter "QATestTracker-*-Setup.exe" -File | Where-Object { $_.Name -ne $name } | ForEach-Object {
    Remove-Item -LiteralPath $_.FullName -Force
    "took away the older $($_.Name)"
}

Set-Content -Encoding utf8 -LiteralPath $remembered -Value $To
"published $name  ($([math]::Round((Get-Item $installer).Length / 1MB, 1)) MB) to $To"
if ($NoImport) { "  with README.txt and SHA256.txt; the database and the test scripts there were not touched." }
else { "  with README.txt and SHA256.txt, the test scripts imported into $Database; nothing else there was touched." }
"Testers run $target - it replaces the version they have and keeps their database."
exit 0
