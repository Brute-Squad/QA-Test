# Makes the installer of QA Test Tracker: installer\out\QATestTracker-<version>-Setup.exe
#
#   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
#   ... -NoBuild      use the program that build.bat made last time
#
# What it does:
#   1. builds the program and runs its tests (build.bat) - no installer is made
#      of a program whose tests fail;
#   2. puts the program into a folder of its own with everything it needs: Qt's
#      files, the Visual C++ runtime's DLLs beside it (so that nothing else has to
#      be installed on the PC it goes to), the test scripts that come with it
#      (scripts\*.json) and the README;
#   3. starts the program from that folder with nothing but Windows on the path,
#      to see that it is whole;
#   4. packs it with the Qt Installer Framework (binarycreator) into one file that
#      downloads nothing.
#
# The installer installs for the user who runs it (no administrator's rights),
# replaces a version that is there, and leaves the user's database and
# configuration file alone - see README.md.
param(
    [switch]$NoBuild,
    [string]$QtDir = $(if ($env:QT_DIR) { $env:QT_DIR } else { "C:\Qt\6.11.0\msvc2022_64" }),
    [string]$Ifw = ""
)
$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $here
$work = Join-Path $here "work"
$out = Join-Path $here "out"
$package = "com.qatest.tracker"

if ($Ifw -eq "") {
    $found = Get-ChildItem "C:\Qt\Tools\QtInstallerFramework" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if ($found) { $Ifw = Join-Path $found.FullName "bin" }
}
$binarycreator = Join-Path $Ifw "binarycreator.exe"
if (-not (Test-Path $binarycreator)) { "The Qt Installer Framework was not found (binarycreator.exe; looked in C:\Qt\Tools\QtInstallerFramework). Say where with -Ifw <its bin folder>."; exit 2 }

# The version: the one the project says (CMakeLists.txt).
$versionLine = Select-String -Path (Join-Path $repo "CMakeLists.txt") -Pattern 'project\(QATest\s+VERSION\s+([0-9.]+)' | Select-Object -First 1
if (-not $versionLine) { "The version was not found in CMakeLists.txt."; exit 2 }
$version = $versionLine.Matches[0].Groups[1].Value
$date = Get-Date -Format "yyyy-MM-dd"

# ---- 1. build, and test ------------------------------------------------------
if (-not $NoBuild) {
    "building the program and running its tests ..."
    $env:QT_DIR = $QtDir
    $ErrorActionPreference = "Continue"
    & cmd.exe /c "`"$repo\build.bat`"" 2>&1 | Out-File -Encoding utf8 (Join-Path $here "build.log")
    $built = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($built -ne 0) { "The build or its tests failed (see installer\build.log). No installer was made."; exit 3 }
}
$dist = Join-Path $repo "dist"
$program = Join-Path $dist "QATest.exe"
if (-not (Test-Path $program)) { "There is no dist\QATest.exe: run build.bat first."; exit 3 }
# Not older than what it is made of.
$newest = Get-ChildItem (Join-Path $repo "src") -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ((Get-Item $program).LastWriteTime -lt $newest.LastWriteTime) {
    "Not up to date: dist\QATest.exe is older than src\$($newest.Name). Run without -NoBuild."; exit 3
}

# ---- 2. the program in a folder of its own ---------------------------------------
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
$data = Join-Path $work "packages\$package\data"
$meta = Join-Path $work "packages\$package\meta"
$config = Join-Path $work "config"
New-Item -ItemType Directory -Force $data, $meta, $config, $out | Out-Null

# Everything of dist but what is this PC's own: a configuration file, a database,
# and the runtime's installer (its DLLs go in instead, which needs no rights).
Copy-Item (Join-Path $dist "*") $data -Recurse -Exclude "QATest.ini", "*.sqlite", "vc_redist*.exe", "deployed-*.txt"

# The Visual C++ runtime, beside the program.
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path $vswhere) { (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1) } else { "" }
$crt = if ($vs) { Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Recurse -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
                      Where-Object { $_.FullName -match '\\x64\\' -and $_.FullName -notmatch 'onecore|debug' } | Sort-Object FullName -Descending | Select-Object -First 1 } else { $null }
if (-not $crt) { "The Visual C++ runtime's DLLs were not found (VC\Redist\MSVC\...\x64\Microsoft.VC*.CRT)."; exit 2 }
Copy-Item (Join-Path $crt.FullName "*.dll") $data

# The test scripts that come with the program: an empty database starts with them.
New-Item -ItemType Directory -Force (Join-Path $data "scripts") | Out-Null
Copy-Item (Join-Path $repo "scripts\*.json") (Join-Path $data "scripts")
Copy-Item (Join-Path $repo "README.md") (Join-Path $data "README.txt")

# ---- 3. is it whole? ------------------------------------------------------------
# Started from that folder with nothing but Windows on the path, on a database of
# its own in the work folder: a file that is missing shows here, not at a tester's.
$trial = Join-Path $work "trial.sqlite"
$oldPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$started = Start-Process -FilePath (Join-Path $data "QATest.exe") -ArgumentList @("--db", "`"$trial`"", "--smoke") -Wait -PassThru -WindowStyle Hidden
$env:PATH = $oldPath
if ($started.ExitCode -ne 0 -or -not (Test-Path $trial)) { "The program does not start from its own folder (exit code $($started.ExitCode)): something it needs is missing. No installer was made."; exit 4 }
Remove-Item $trial -Force

# ---- 4. pack it -------------------------------------------------------------------
function Fill($from, $to) {
    (Get-Content $from -Raw).Replace("@VERSION@", $version).Replace("@DATE@", $date) | Set-Content -Encoding utf8 $to
}
Fill (Join-Path $here "config.xml") (Join-Path $config "config.xml")
Fill (Join-Path $here "package.xml") (Join-Path $meta "package.xml")
Copy-Item (Join-Path $here "controller.qs") $config
# The program's icon, for the installer's own file and window.
Copy-Item (Join-Path $repo "icons\qatest.ico") (Join-Path $config "qatest.ico")
Copy-Item (Join-Path $repo "icons\qatest-256.png") (Join-Path $config "qatest.png")
Copy-Item (Join-Path $here "installscript.qs") $meta
# The page that asks for the database (installscript.qs shows it).
Copy-Item (Join-Path $here "databasepage.ui") $meta

$target = Join-Path $out "QATestTracker-$version-Setup.exe"
if (Test-Path $target) { Remove-Item $target -Force }
$ErrorActionPreference = "Continue"
& cmd.exe /s /c "`"`"$binarycreator`" --offline-only -c `"$config\config.xml`" -p `"$work\packages`" `"$target`" > `"$here\binarycreator.log`" 2>&1`""
$packed = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($packed -ne 0 -or -not (Test-Path $target)) { "binarycreator failed (see installer\binarycreator.log)."; exit 5 }

# The installers of other versions that were made here before are of no use beside this one.
Get-ChildItem $out -Filter "QATestTracker-*-Setup.exe" | Where-Object { $_.FullName -ne $target } | ForEach-Object { Remove-Item $_.FullName -Force; "removed the older $($_.Name)" }

$files = (Get-ChildItem $data -Recurse -File).Count
"made installer\out\$(Split-Path -Leaf $target)  ($([math]::Round((Get-Item $target).Length / 1MB, 1)) MB, version $version, $files files)"
exit 0
