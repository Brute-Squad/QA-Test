// The control script of the installer (config.xml: <ControlScript>). It is
// the one the Factory Inventory installers use, and knows nothing of the
// product but what the installer itself says: its name and its uninstaller.
//
// Installing over a version that is there already: the installer cannot
// write into a folder that holds an installation, and nobody should have to
// know that the old one has to go first. So before anything else it looks
// whether this product is installed - by the entry every installation makes
// under "Uninstall" in the registry, which says where - and, if so, says so
// and removes that version itself: its own uninstaller is run without
// questions (purge), which takes its folder away. Data is not touched: the
// database and the configuration file are in the user's own folder, not in
// the program's, so the new version carries on with them.
// The new version then goes where the old one was.
//
// An upgrade is something ordinary, and is worded that way. The user never
// gets to see the framework's own warning about "an existing, non-empty
// directory" that "will be completely wiped" and where "installing might
// fail": that is what it says when files of the old version are left in the
// folder, and it reads as if upgrading were dangerous. So the folder is
// made empty here, before the framework looks at it:
//
// - The uninstaller cannot delete its own file: when it has done its work
//   and ends, a script it leaves behind takes its file and the folder away,
//   which can take many seconds. The installer waits for that.
// - What is left after that is deleted here - with an administrator's
//   rights if it takes them (Windows asks).
// - What still cannot be deleted is in use: a window of the old version is
//   open. The installer says which files, asks for the program to be closed,
//   and tries again on Retry - as often as it takes. It does not give up
//   half way and leave the leftovers for the next run to stumble over.
// - Leftovers of an earlier attempt - the product's own folder with files
//   in it, but no installation - are cleared the same way.
//
// Nothing is removed without the user's OK; Cancel ends the installer.

// (Plain variables and functions: a page's callback is not called with the
// Controller as its `this`.)
var existingChecked = false;
var installerEnded = false;

function Controller()
{
}

// Ends the installer without installing: Cancel, with its question answered.
// (gui.rejectWithoutPrompt() does nothing as early as the first page's
// callback - the wizard goes on - so the button is pressed, which happens
// once the callback has returned.)
function endInstaller()
{
    installerEnded = true;
    installer.setMessageBoxAutomaticAnswer("cancelInstallation", QMessageBox.Yes);
    gui.clickButton(buttons.CancelButton);
}

function quoted(text)
{
    return "'" + text.replace(/'/g, "''") + "'";
}

// Runs a PowerShell command and gives back what it wrote, without the
// spaces and line breaks around it.
function powershell(command)
{
    var result = installer.execute("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command]);
    return (result && result.length > 0) ? String(result[0]).replace(/^\s+|\s+$/g, "") : "";
}

// PowerShell that writes out the first few files that are in a folder
// (nothing for a folder that is not there, or holds only empty folders).
function listFiles(folderVariable)
{
    return "if (Test-Path -LiteralPath " + folderVariable + ") { "
         + "Get-ChildItem -LiteralPath " + folderVariable + " -Recurse -File -Force -ErrorAction SilentlyContinue | Select-Object -First 4 | ForEach-Object { $_.Name } }";
}

// The files in a folder, as "a.dll, b.exe" ("" = none).
function filesIn(folderPath)
{
    return powershell("$d = " + quoted(folderPath) + "; " + listFiles("$d")).split(/\r?\n/).join(", ");
}

// Deletes a folder with everything in it - as the user, and if that is not
// allowed, once more with an administrator's rights (Windows asks) - and
// gives back the files that are still there: those are in use.
function deleteFolder(folderPath)
{
    return powershell("$d = " + quoted(folderPath) + "; "
        + "if (Test-Path -LiteralPath $d) { "
        + "try { Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction Stop } catch { "
        + "try { Start-Process -FilePath 'cmd.exe' -ArgumentList @('/c', 'rd', '/s', '/q', ('\"' + $d + '\"')) -Verb RunAs -Wait -WindowStyle Hidden } catch { } }; "
        + "for ($i = 0; $i -lt 10 -and (Test-Path -LiteralPath $d); $i++) { Start-Sleep -Milliseconds 300 } }; "
        + listFiles("$d")).split(/\r?\n/).join(", ");
}

// Makes the folder the new version goes into empty, asking for the program
// to be closed for as long as files of it are in use. False if the user
// gave up.
function clearFolder(name, folderPath)
{
    var left = deleteFolder(folderPath);
    while (left !== "") {
        var answer = QMessageBox.warning("existing.inuse", name + " Setup",
            name + " is still open: some of its files are in use (" + left + ").<br><br>"
            + "Close every window of " + name + ", then press <b>Retry</b>. Your data and settings are kept.",
            QMessageBox.Retry | QMessageBox.Cancel);
        if (answer !== QMessageBox.Retry)
            return false;
        left = deleteFolder(folderPath);
    }
    return true;
}

Controller.prototype.IntroductionPageCallback = function()
{
    if (existingChecked || !installer.isInstaller() || systemInfo.productType !== "windows")
        return;
    existingChecked = true;

    var name = installer.value("ProductName");
    var tool = installer.value("MaintenanceToolName") + ".exe";

    // Where this product is installed, one folder per line (the registry of
    // this user and of the machine).
    var found = powershell("$n = " + quoted(name) + "; "
        + "foreach ($r in 'HKCU:', 'HKLM:') { "
        + "Get-ChildItem ($r + '\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall') -ErrorAction SilentlyContinue | ForEach-Object { "
        + "$p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue; "
        + "if ($p.DisplayName -eq $n -and $p.InstallLocation) { $p.InstallLocation } } }");

    // The first of them whose uninstaller is really there.
    var location = "";
    var lines = found.split(/\r?\n/);
    for (var i = 0; i < lines.length && location === ""; ++i) {
        var folder = lines[i].replace(/^\s+|\s+$/g, "").replace(/\\/g, "/").replace(/\/+$/, "");
        if (folder !== "" && installer.fileExists(folder + "/" + tool))
            location = folder;
    }

    if (location === "") {
        // Not installed. But files of an earlier installation may be left in
        // the product's own folder - where this one goes unless the user says
        // otherwise - from an attempt that did not get through.
        var own = String(installer.value("TargetDir")).replace(/\//g, "\\");
        var leftovers = filesIn(own);
        if (leftovers === "")
            return;
        var clear = QMessageBox.question("existing.leftovers", name + " Setup",
            "Files of an earlier installation of " + name + " are left in " + own + ".<br><br>"
            + "They are removed first, and this version is installed in their place. <b>Your data and settings are kept.</b>",
            QMessageBox.Ok | QMessageBox.Cancel);
        if (clear !== QMessageBox.Ok || !clearFolder(name, own))
            endInstaller();
        return;
    }

    var folderPath = location.replace(/\//g, "\\");
    var toolPath = folderPath + "\\" + tool;

    var answer = QMessageBox.question("existing.version", name + " Setup",
        "An earlier version of " + name + " is installed on this PC, in " + folderPath + ".<br><br>"
        + "This installer replaces it with the new version. <b>Your data and settings are kept.</b><br><br>"
        + "Close " + name + " if it is open, then press OK. It takes about a minute; allow it if Windows asks.",
        QMessageBox.Ok | QMessageBox.Cancel);
    if (answer !== QMessageBox.Ok) {
        endInstaller();
        return;
    }

    // The old version's own uninstaller, without its windows and questions.
    // Should it fail as it is - it may not ask Windows for an administrator's
    // rights by itself - it is run once more with them (Windows asks). Only
    // when it has done its work is its file waited for (a minute at most),
    // then the folder (a few seconds more).
    powershell("$t = " + quoted(toolPath) + "; $d = " + quoted(folderPath) + "; "
        + "$a = @('purge', '--confirm-command', '--default-answer'); "
        + "& $t @a | Out-Null; $done = ($LASTEXITCODE -eq 0); "
        + "if (-not $done) { try { $p = Start-Process -FilePath $t -ArgumentList $a -Verb RunAs -Wait -PassThru; $done = ($p.ExitCode -eq 0) } catch { } }; "
        + "if ($done) { "
        + "for ($i = 0; $i -lt 200 -and (Test-Path -LiteralPath $t); $i++) { Start-Sleep -Milliseconds 300 }; "
        + "for ($i = 0; $i -lt 30 -and (Test-Path -LiteralPath $d); $i++) { Start-Sleep -Milliseconds 300 } }");

    if (installer.fileExists(location + "/" + tool)) {
        QMessageBox.warning("existing.notremoved", name + " Setup",
            "The earlier version of " + name + " could not be replaced: Windows did not allow it to be removed.<br><br>"
            + "Nothing was changed. Run this installer again and allow it when Windows asks.",
            QMessageBox.Ok);
        endInstaller();
        return;
    }

    // What it left behind goes too, so that the new version finds its folder
    // empty - asking for the program to be closed if its files are in use.
    if (!clearFolder(name, folderPath)) {
        endInstaller();
        return;
    }

    // The new version goes where the old one was.
    installer.setValue("TargetDir", folderPath);
}
