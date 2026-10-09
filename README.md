# QA Test Tracker

A small desktop program for manual testing: test cases with their steps, test runs, and what passed or failed - all in one SQLite file. Written with Qt 6 (Widgets, C++17).

It comes with the manual test scripts for **Factory Inventory** (`scripts/FactoryInventory.json`: 15 suites, 81 test cases), already imported into `data/qatest.sqlite`.

## Installing it

`installer\out\QATestTracker-<version>-Setup.exe` (made by `installer\build-installer.ps1`, below) installs the program on a PC that has nothing else: no Qt, no compiler, no runtime to install first.

- It installs **for the user who runs it** - into `%LOCALAPPDATA%\Programs\QA Test Tracker` - and asks for no administrator's rights. It makes **QA Test Tracker** in the Start menu and on the desktop, and **Uninstall QA Test Tracker** in the Start menu.
- **The first start brings the test scripts along.** A database with no project in it reads the scripts that came with the program (`scripts\*.json` beside it): Factory Inventory's 81 test cases are there at once. A database that has a project is never touched this way.
- **It asks which database to use** (the page *Your Database*, before it installs):
  - **Create a new database** - where you say; at first `qatest.sqlite` in your application data folder (`%APPDATA%\QATest\QATest`). It starts with the test scripts for Factory Inventory. A name that is taken is refused: nothing is ever written over.
  - **Use a database that is there already** - your own from before, or the one your team shares (`\\server\share\qatest.sqlite`). Its test cases, runs and results are left as they are (a database from an older version gets what this version needs added to it; the older version goes on working in it). A drive letter of a network drive is written down as the share's own name - `M:\QA-Test\...` as `\\nas1\MyMedia\QA-Test\...` - which is the same on every PC. This is what the page starts with when your configuration file names a database, or your own is there.

  Install is there only while the answer makes sense, and the line under the choices says what is wrong. The answer goes into your configuration file, `QATest.ini` in `%APPDATA%\QATest\QATest` (the `Path` line; every other setting is explained there and left as it was). **File > Edit Configuration File...** opens it, and **File > Open Database...** changes your mind later.
- **A newer version is installed over the old one**: run the new installer; it says that an earlier version is there, removes it and goes in its place - in the folder the old one was in (a version that was installed into `C:\Program Files` stays there, and Windows asks for permission). The database and the configuration file are not in the program's folder, so they stay as they are.
- **Uninstalling** removes the program, its Start menu entries and its desktop entry. The database and the configuration file stay; delete the folder `%APPDATA%\QATest` by hand if they are to go too.

The installer is not signed, so Windows may warn about an unknown publisher (More info > Run anyway).

## Using it

Start **QA Test Tracker** from the Start menu - or, from a build, `dist\QATest.exe` (made by `build.bat`, below).

- **On the left:** the projects, their suites and their test cases. A case carries a mark for how it went the last time it was run (✔ passed, ✘ failed, ■ blocked, – skipped).
- **Test Case** tab: the selected case - key, title, priority, where it is run, preconditions, its steps (what to do / what to expect), notes - and how it went in the runs so far. Change it and press **Save**. **Edit > New Test Case** (Ctrl+N) adds one to the selected suite, with the next free key.
- **Test Runs** tab: the runs of the selected project.
  - **New Run...** asks for a name, the build that is tested, who tests, and which suites. The run's cases are fixed when it is made.
  - Select a case in the table, work through its steps, then press **Passed**, **Failed**, **Blocked** or **Skipped**. The result is stored at once and the next case that is not run is selected. A failure needs a note saying what happened; the step that failed can be given.
  - **Show:** filters by result. **Finish** closes a run so that nothing is marked by mistake (**Reopen** takes that back).
  - **Report...** shows the run as a document - what was tested, how it stands, what failed or was blocked with the notes, and every case by suite - to save as HTML or PDF.
- **File > Import Test Scripts...** reads a file of test scripts. Importing a file again brings its cases up to date and adds none twice: a case is known by its key. Results are never touched by an import.
- **File > Export Project...** writes a project's cases as such a file.
- **Edit > Delete...** deletes the selected case, suite or project after a question that says what goes with it.

## The database

One SQLite file. Which one the program uses:

1. the one named on the command line: `QATest --db <file>`;
2. else the one the **configuration file** names (below) - a shared drive, say;
3. else the one opened last with **File > Open Database...**;
4. else `qatest.sqlite` beside the program, or in a folder `data` beside the program's folder - which is `data\qatest.sqlite` here;
5. else `qatest.sqlite` in the user's application data folder.

**File > Where Is the Database?** says which it is and why; so does `QATest --where`. A copy of it is made on every day the program is used (below: *A copy a day*).

## The configuration file: one database for a team

`QATest.ini` is looked for **beside `QATest.exe`** first and, when none is there, **in your application data folder** (`%APPDATA%\QATest\QATest\QATest.ini`) - which is where an installed program has it, since a new version replaces the program's folder. **File > Edit Configuration File...** opens the one in use, making it first if there is none. (`build.bat` puts one beside `dist\QATest.exe`; `QATest --write-config` and `--write-user-config` make one beside the program or in your own folder by hand. None of them writes over a file that is there. `QATest --set-database <file> [--existing] [--config <file>]` is what the installer does with its answer: it makes the database if it is not there - with the scripts that come with the program - and sets the `Path` line of your configuration file, keeping the rest; with `--existing` the file has to be there.)

```ini
[Database]
; Where the database is. A backslash is written once.
Path=\\fileserver\qa\qatest.sqlite

; How many seconds to wait while somebody else is writing (10 unless said).
BusyTimeoutSeconds=10
```

- `Path` can be a network path (`\\server\share\...`), a mapped drive (`Q:\QA\qatest.sqlite`), or a path from the file's own folder (`..\shared\qa.sqlite`). `%USERPROFILE%` and the like are filled in. Quotation marks around the value are allowed and not needed.
- The file is read when the program starts. What it says goes before what was opened last, so everybody with the same line works in the same database; `--db` on the command line still goes before it. `QATest --config <file>` reads another file than the one beside the program.
- A line the program does not understand is said when it is started from a command line, and the rest of the file still counts.

**To move to a shared drive:** close the program everywhere; copy your `qatest.sqlite` to the share (or let the program make an empty one there: it starts with the scripts that came with it); then choose **File > Use a Shared Database...** on every PC, which writes the `Path` line into `QATest.ini` - or put the whole `dist` folder on the share, with `Path=qatest.sqlite`, and have everybody start it from there. The folder on the share has to exist, and everybody needs the right to change files in it: SQLite keeps a second file beside the database while it writes.

**What to expect of a shared database.** Every result and every change is written at once, so nothing waits in one person's program. The window shows what the others did when it comes back to the front, on **View > Refresh** (F5), and after each thing you store; what you are typing is left alone. While somebody else is writing, the program waits for up to the time set and then says that the database is busy - try again. It is meant for a small team: SQLite on a network drive is dependable when a few people take turns writing, not for dozens at once, and not on a drive that syncs files in the background (OneDrive, Dropbox) - there, each PC ends up with its own copy.

Tables: `projects`, `suites`, `cases`, `steps`, `runs`, `results` (see `src/qadatabase.h`).

## Working in one database with others

Everything here is for a database that several people have open at once - `\\nas1\MyMedia\QA-Test\data\qatest.sqlite`, say.

- **File > Use a Shared Database...** opens a database that is there and writes it into your configuration file, so it is yours from then on; every tester does it once. When the file is chosen on a network drive, the program asks whether to write it down under **the share's own name** (`\\server\share\...`, the same on every PC - the answer in front) or to keep the drive letter, which is this PC's own. The installer's database page and `QATest --set-database` take the share's name by themselves. `QATest --network-name <path>` prints it.
- **Nobody's work is written over without a word.** A test case counts its changes. If somebody else stored the case while you had it open, Save stores nothing and says who and when; what you typed stays. **Revert** shows their version, **Save Mine Anyway** - there only then - stores yours over theirs. Choosing another case does not lose what could not be saved either: the case stays, with the reason. A case you are not typing in shows what others stored whenever the window comes to the front (View > Refresh, F5, does it at once). Under a case it says who changed it last, and when.
- **A result says who recorded it**: the name you are logged in to Windows with - or the one you gave as Tester in New Run... the last time - not the name of whoever made the run. File > Where Is the Database? says who you are here.
- **A share that goes away** - a NAS that sleeps, Wi-Fi that drops - is said in a line at the top of the window. What you see and what you typed stays; the program tries again every few seconds and when you press **Try Again**, and goes on when the database is back. At start, a database that cannot be reached is asked about with **Retry**, not just refused.
- **An older version in the same database** keeps working: this version adds what it needs (two columns of the test cases) and the older one does not mind. Only the protection above is this version's - an older program still stores over what others stored - so put the same version on every PC.

### A copy a day

The first program that opens the database on a day makes that day's copy: `backups\qatest-2026-10-09.sqlite` in the database's folder, complete in itself, made while others go on working. The newest 14 are kept and older ones deleted; only files called like that are ever touched there. A copy that is there is never written over. **File > Open Backups Folder** shows them.

The database is checked when the program starts. One that is damaged is said at once, with the newest copy to go back to, and is not copied - the good copies are what matters then, and stay.

**To go back to a copy:** close QA Test Tracker on every PC, then put the copy in the database's place under the database's name.

In `QATest.ini`:

```ini
[Backup]
; how many daily copies to keep (14; 0 = make none)
Keep=14
; another folder than "backups" beside the database - another drive, say
Folder=D:\QA backups
```

## Test scripts as a file

```json
{
  "project": "Factory Inventory",
  "description": "...",
  "suites": [
    { "name": "Parts and stock", "description": "...",
      "cases": [
        { "key": "FI-PARTS-001", "title": "Add, change and delete a part",
          "priority": "High", "area": "Desktop and phone",
          "preconditions": "...", "notes": "",
          "steps": [ { "action": "what to do", "expected": "what is to happen" } ] }
      ] }
  ]
}
```

From the command line: `QATest --import scripts\FactoryInventory.json` (exit code 0 = imported; with `--db <file>` into that database).

## Building

Needs Visual Studio 2022 or newer with the C++ tools, and Qt 6 for MSVC (Core, Gui, Widgets, Sql).

```bat
build.bat
```

It configures with CMake and Ninja into `build\`, builds, runs the tests, and puts the program with Qt's own files and a `QATest.ini` to start from into `dist\` (so that it starts from the Explorer, and the folder can be copied to another PC as it is). Qt is looked for in `C:\Qt\6.11.0\msvc2022_64`; say another place with `set QT_DIR=...` first. `build.bat run` also starts the program.

## Making the installer

```bat
powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
```

Needs the Qt Installer Framework (`C:\Qt\Tools\QtInstallerFramework`; `-Ifw <its bin folder>` says another place). It runs `build.bat` - no installer is made of a program whose tests fail - puts the program into `installer\work` with Qt's files, the Visual C++ runtime's DLLs, the test scripts and this README, starts it from there with nothing but Windows on the path to see that it is whole, and packs it into `installer\out\QATestTracker-<version>-Setup.exe`: one file that downloads nothing. `-NoBuild` uses the program built last. The version is the project's (`project(QATest VERSION ...)` in `CMakeLists.txt`).

`installer\config.xml` says what the installer is and where it installs, `installer\databasepage.ui` is the page that asks for the database, `installer\installscript.qs` shows it, makes the Start menu and desktop entries and has the program act on the answer (`--set-database`), and `installer\controller.qs` removes a version that is there before the new one goes in - and puts the new one where the old one was.

**The icon** - a clipboard with a checklist - is drawn by a small program, `icons\makeicon.cpp`: `icons\make.bat` builds and runs it and writes `icons\qatest.ico` (the program's file and the installer's) and `icons\qatest-*.png` (the windows'). The files it makes are kept in the folder, so a build does not need it; run it after changing the drawing.

## Tests

`build\QATestTests.exe` (run by `build.bat`; exit code 0 = every check passed). It checks the configuration file (network paths, relative paths, what is wrong with a file, two programs on one database), the database (cases, steps, runs, results, what is refused, what a delete takes along, import and export), reads every file in `scripts\` to see that it can be imported and that each case has steps that say what to expect, and drives the program's own window offscreen: the tree, a case edited and saved, a run made and worked through, its report. And what a shared database needs: two programs in one file where the second is told that the first stored the case, a database from an older version, the copy of each day and which are kept, a damaged database, a drive letter as the share's name, and the window while the database is away, with somebody else's change, and with File > Use a Shared Database.

## Layout

| Path | What |
| --- | --- |
| `src/qadatabase.*` | The SQLite database: schema, reading and writing, import / export |
| `src/qaconfig.*` | The configuration file `QATest.ini`: where the database is, how many copies to keep |
| `src/qabackup.*` | A copy of the database a day, and which to keep |
| `src/qashare.*` | A path on a network drive under the share's own name |
| `src/mainwindow.*` | The window: the tree, the menus |
| `src/casepanel.*` | The Test Case tab |
| `src/runpanel.*` | The Test Runs tab |
| `src/report.*` | A run as an HTML document; saving it as HTML or PDF |
| `src/main.cpp` | Start-up and the command line |
| `tests/qatests.cpp` | The tests |
| `installer/` | The installer: its configuration, its database page, its scripts, and `build-installer.ps1` which makes it |
| `icons/` | The icon: the program that draws it, and what it drew |
| `scripts/FactoryInventory.json` | The test scripts for Factory Inventory |
| `data/qatest.sqlite` | The database, with those scripts imported |
