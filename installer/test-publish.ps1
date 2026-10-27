# Checks installer\publish.ps1: what it does to a folder the testers get things from, and what
# it refuses. build.bat runs it once the program is in dist\ - the program imports the test
# scripts as it does on the shared drive.
#
#   powershell -ExecutionPolicy Bypass -File installer\test-publish.ps1
#
# Everything happens in folders of its own under the system's temporary folder, which are
# deleted again: the installer is a stand-in (publish.ps1 -Installer), nothing is built, and
# the folder publish.ps1 remembers (publish-to.txt) is not changed. Exit code 0 = every check
# passed.
$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $here
$publish = Join-Path $here "publish.ps1"
$program = Join-Path $repo "dist\QATest.exe"
$remembered = Join-Path $here "publish-to.txt"
if (-not (Test-Path $program)) { "There is no dist\QATest.exe: build.bat makes it."; exit 2 }

$versionLine = Select-String -Path (Join-Path $repo "CMakeLists.txt") -Pattern 'project\(QATest\s+VERSION\s+([0-9.]+)' | Select-Object -First 1
$version = $versionLine.Matches[0].Groups[1].Value
$name = "QATestTracker-$version-Setup.exe"
$scripts = @(Get-ChildItem (Join-Path $repo "scripts") -Filter "*.json" -File)

$script:checks = 0
$script:failures = 0
function Check([bool]$condition, [string]$what) {
    $script:checks++
    if (-not $condition) { $script:failures++; "FAILED: $what" }
}

# publish.ps1 as somebody runs it: what it says, and its exit code.
function Publish([string[]]$arguments) {
    $ErrorActionPreference = "Continue"
    $said = & powershell -NoProfile -ExecutionPolicy Bypass -File $publish @arguments 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    return @{ code = $code; said = $said }
}

# A database as the program makes it: empty, waiting for test scripts.
function NewDatabase([string]$file) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $file) | Out-Null
    $ErrorActionPreference = "Continue"
    & $program --db $file --smoke 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
}

function Files([string]$folder) {
    if (-not (Test-Path $folder)) { return @() }
    return @(Get-ChildItem -LiteralPath $folder -File | ForEach-Object { $_.Name } | Sort-Object)
}

$rememberedBefore = if (Test-Path $remembered) { Get-Content -Raw $remembered } else { $null }
$work = Join-Path ([IO.Path]::GetTempPath()) ("qatest-publish-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    $standIn = Join-Path $work "stand-in.exe"
    Set-Content -Encoding ascii -LiteralPath $standIn -Value "a stand-in for the installer"
    $standInHash = (Get-FileHash $standIn -Algorithm SHA256).Hash.ToLower()

    # ---- a folder with the team's database, an older installer and copies from before
    $a = Join-Path $work "a"
    NewDatabase (Join-Path $a "data\qatest.sqlite")
    Set-Content -LiteralPath (Join-Path $a "QATestTracker-1.0-Setup.exe") -Value "old"
    $backups = Join-Path $a "data\backups"
    New-Item -ItemType Directory -Force $backups | Out-Null
    foreach ($n in 10..20) { Set-Content -LiteralPath (Join-Path $backups ("qatest-before-import-2020-01-01-0000{0}.sqlite" -f $n)) -Value "older" }
    Set-Content -LiteralPath (Join-Path $backups "qatest-2020-01-01.sqlite") -Value "a day's copy"
    Set-Content -LiteralPath (Join-Path $backups "notes.txt") -Value "somebody's"
    $r = Publish @("-To", $a, "-Installer", $standIn)
    Check ($r.code -eq 0) "a folder with a database is published to (exit code $($r.code)): $($r.said)"
    Check ($r.said -match "test cases were added") "the scripts go into its database: $($r.said)"
    Check ($r.said -match "copied the database to .*qatest-before-import-") "after a copy of it: $($r.said)"
    $there = Join-Path $a $name
    Check ((Test-Path $there) -and (Get-FileHash $there -Algorithm SHA256).Hash.ToLower() -eq $standInHash) "the installer is there, as it was made"
    Check (-not (Test-Path (Join-Path $a "QATestTracker-1.0-Setup.exe"))) "the older installer is taken away"
    Check ((Get-Content -Raw (Join-Path $a "SHA256.txt")).Trim() -eq "$standInHash  $name") "SHA256.txt says the installer's checksum"
    Check ((Test-Path (Join-Path $a "README.txt"))) "README.txt is there"
    Check ((Files (Join-Path $a "scripts")).Count -eq $scripts.Count) "the test scripts are beside the database"
    $copies = @(Files $backups | Where-Object { $_ -like "qatest-before-import-*" })
    Check ($copies.Count -eq 10 -and $copies[-1] -notlike "qatest-before-import-2020-*" -and $copies[0] -eq "qatest-before-import-2020-01-01-000012.sqlite") "the newest 10 copies from before an import are kept: $($copies -join ' ')"
    Check ((Test-Path (Join-Path $backups "qatest-2020-01-01.sqlite")) -and (Test-Path (Join-Path $backups "notes.txt"))) "nothing else in the folder of copies is touched"
    Check ($r.said -match "deleted the 2 oldest copies") "and it says what it deleted: $($r.said)"

    # ---- the same version again
    $r = Publish @("-To", $a, "-Installer", $standIn)
    Check ($r.code -eq 2 -and $r.said -match "an installer of version $([regex]::Escape($version)) already" -and $r.said -match "Nothing was published") "a version is published once: $($r.said)"
    Check (@(Files $backups | Where-Object { $_ -like "qatest-before-import-*" }).Count -eq 10) "and the database is not touched for it"

    # ---- a newer version there
    $c = Join-Path $work "c"
    NewDatabase (Join-Path $c "data\qatest.sqlite")
    Set-Content -LiteralPath (Join-Path $c "QATestTracker-99.0-Setup.exe") -Value "newer"
    $r = Publish @("-To", $c, "-Installer", $standIn)
    Check ($r.code -eq 2 -and $r.said -match "the newer version 99.0") "a folder with a newer version is not published to: $($r.said)"
    Check (-not (Test-Path (Join-Path $c "data\backups")) -and -not (Test-Path (Join-Path $c $name))) "and nothing is done there"

    # ---- no database
    $d = Join-Path $work "d"
    New-Item -ItemType Directory -Force $d | Out-Null
    $r = Publish @("-To", $d, "-Installer", $standIn)
    Check ($r.code -eq 2 -and $r.said -match "There is no database" -and $r.said -match "-NoImport") "a folder without a database is said, with what to do: $($r.said)"
    Check ((Get-ChildItem -LiteralPath $d).Count -eq 0) "and nothing is put there"

    # ---- the installer alone
    $e = Join-Path $work "e"
    New-Item -ItemType Directory -Force $e | Out-Null
    $r = Publish @("-To", $e, "-Installer", $standIn, "-NoImport")
    Check ($r.code -eq 0 -and (Test-Path (Join-Path $e $name)) -and -not (Test-Path (Join-Path $e "scripts"))) "-NoImport publishes the installer alone: $($r.said)"

    # ---- a file that is no database
    $f = Join-Path $work "f"
    New-Item -ItemType Directory -Force (Join-Path $f "data") | Out-Null
    Set-Content -LiteralPath (Join-Path $f "data\qatest.sqlite") -Value "not a database at all"
    $r = Publish @("-To", $f, "-Installer", $standIn)
    Check ($r.code -eq 4 -and $r.said -match "Nothing was published" -and $r.said -notmatch "RemoteException") "a database that cannot be read is said: $($r.said)"
    Check (-not (Test-Path (Join-Path $f $name)) -and -not (Test-Path (Join-Path $f "scripts"))) "and nothing is published"

    # ---- what is no number of copies
    $r = Publish @("-To", $e, "-Installer", $standIn, "-KeepCopies", "0")
    Check ($r.code -eq 2 -and $r.said -match "-KeepCopies wants at least 1") "keeping no copies is refused: $($r.said)"

    # ---- the folder of the last time is the user's
    $rememberedAfter = if (Test-Path $remembered) { Get-Content -Raw $remembered } else { $null }
    Check ($rememberedAfter -eq $rememberedBefore) "a trial does not change the folder publish.ps1 remembers"
}
finally {
    Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
}

if ($script:failures -eq 0) { "All $($script:checks) checks of publish.ps1 passed."; exit 0 }
"$($script:failures) of $($script:checks) checks of publish.ps1 FAILED."
exit 1
