// QA Test Tracker: the page that asks which database to use, the entries in
// the Start menu and on the desktop, and the user's configuration file.
//
// The database page (databasepage.ui, shown before the installation starts):
//
//   ( ) Create a new database              [ path ] [Choose Folder...]
//   ( ) Use a database that is there already [ path ] [Choose File...]
//
// What it starts with: the database the user's configuration file names, if
// it names one (an earlier installation's - a shared drive, say), else the
// user's own database if there is one, else a new one in the user's own
// folder. Install is there only while the answer makes sense: a new database
// needs a file name that is not taken, an existing one has to be there - and
// the line under the choices says what is wrong.
//
// The answer is acted on by the program itself once it is installed
// (QATest --set-database, src/main.cpp): it makes the database if need be,
// gives a new one the test scripts that come with the program, and writes
// where the database is into the user's configuration file - which is in the
// user's own folder, so a newer version, which replaces the program's
// folder, leaves it alone.

// (Plain variables and functions: a signal's handler is not called with the
// Component as its `this`.)
var ownFolder = "";         // %APPDATA%\QATest\QATest
var ownDatabase = "";       // ... \qatest.sqlite
var configFile = "";        // ... \QATest.ini

function backslashes(path)
{
    return String(path).replace(/\//g, "\\");
}

function trimmed(text)
{
    return String(text).replace(/^\s+|\s+$/g, "");
}

// The database the user's configuration file names ("" = none, or no file).
function configuredDatabase()
{
    if (!installer.fileExists(configFile))
        return "";
    var text = String(installer.readFile(configFile, "UTF-8"));
    var lines = text.split(/\r?\n/);
    var inDatabase = false;
    for (var i = 0; i < lines.length; ++i) {
        var line = trimmed(lines[i]);
        if (line.charAt(0) === "[") {
            inDatabase = line.toLowerCase() === "[database]";
            continue;
        }
        var found = inDatabase ? /^path\s*=\s*(.*)$/i.exec(line) : null;
        if (found) {
            var path = trimmed(found[1]).replace(/^"(.*)"$/, "$1");
            if (path !== "")
                return path;
        }
    }
    return "";
}

function databasePage()
{
    return gui.pageWidgetByObjectName("DynamicDatabasePage");
}

// What is wrong with the answer ("" = nothing), and the answer itself in the
// installer's values, for createOperations().
function checkDatabasePage()
{
    var page = databasePage();
    if (!page)
        return;
    var creating = page.createRadio.checked;
    page.createPath.enabled = creating;
    page.createBrowse.enabled = creating;
    page.existingPath.enabled = !creating;
    page.existingBrowse.enabled = !creating;

    var path = backslashes(trimmed(creating ? page.createPath.text : page.existingPath.text));
    var problem = "";
    if (path === "")
        problem = creating ? "Say where the new database is to be." : "Say which database to use.";
    else if (creating && !/\.sqlite$/i.test(path))
        problem = "Write the file's name too, ending in .sqlite - for example " + path.replace(/\\+$/, "") + "\\qatest.sqlite";
    else if (creating && installer.fileExists(path))
        problem = "There is a database of that name already. To use it, choose \"Use a database that is there already\"; for a new one, choose another name or folder.";
    else if (!creating && !installer.fileExists(path))
        problem = "There is no such file. If it is on a shared drive, see that the drive is connected.";

    page.problem.text = problem;
    page.complete = (problem === "");
    installer.setValue("QaDatabase", path);
    installer.setValue("QaDatabaseExisting", creating ? "false" : "true");
}

function chooseFolder()
{
    var page = databasePage();
    var start = backslashes(trimmed(page.createPath.text)).replace(/\\[^\\]*$/, "");
    var folder = QFileDialog.getExistingDirectory("The folder for the new database", start);
    if (folder && folder !== "")
        page.createPath.text = backslashes(folder).replace(/\\+$/, "") + "\\qatest.sqlite";
}

function chooseFile()
{
    var page = databasePage();
    var start = backslashes(trimmed(page.existingPath.text)).replace(/\\[^\\]*$/, "");
    var file = QFileDialog.getOpenFileName("The database to use", start, "QA databases (*.sqlite);;All files (*)");
    if (file && file !== "")
        page.existingPath.text = backslashes(file);
}

function Component()
{
    if (!installer.isInstaller() || systemInfo.productType !== "windows")
        return;

    ownFolder = backslashes(installer.environmentVariable("APPDATA")) + "\\QATest\\QATest";
    ownDatabase = ownFolder + "\\qatest.sqlite";
    configFile = ownFolder + "\\QATest.ini";
    installer.setValue("QaDatabase", ownDatabase);
    installer.setValue("QaDatabaseExisting", "false");

    component.loaded.connect(this, Component.prototype.addDatabasePage);
}

Component.prototype.addDatabasePage = function()
{
    if (!installer.addWizardPage(component, "DatabasePage", QInstaller.ReadyForInstallation))
        return;
    var page = databasePage();
    if (!page)
        return;

    // What the page starts with.
    var configured = configuredDatabase();
    var existing = configured !== "" ? backslashes(configured) : (installer.fileExists(ownDatabase) ? ownDatabase : "");
    page.createPath.text = ownDatabase;
    page.existingPath.text = existing;
    if (existing !== "") {
        page.existingRadio.checked = true;
        // A new one would need another name than the one that is there.
        if (existing.toLowerCase() === ownDatabase.toLowerCase())
            page.createPath.text = ownFolder + "\\qatest-new.sqlite";
    } else {
        page.createRadio.checked = true;
    }

    page.createRadio.toggled.connect(checkDatabasePage);
    page.existingRadio.toggled.connect(checkDatabasePage);
    page.createPath.textChanged.connect(checkDatabasePage);
    page.existingPath.textChanged.connect(checkDatabasePage);
    page.createBrowse.clicked.connect(chooseFolder);
    page.existingBrowse.clicked.connect(chooseFile);
    checkDatabasePage();
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType !== "windows")
        return;

    // (A trial run of the installer - QaTrial=1 on its command line - makes no entries.)
    if (installer.value("QaTrial") !== "1") {
        component.addOperation("CreateShortcut", "@TargetDir@/QATest.exe", "@StartMenuDir@/QA Test Tracker.lnk",
                               "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/QATest.exe", "iconId=0",
                               "description=QA Test Tracker");
        component.addOperation("CreateShortcut", "@TargetDir@/QATest.exe", "@DesktopDir@/QA Test Tracker.lnk",
                               "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/QATest.exe", "iconId=0",
                               "description=QA Test Tracker");

        // Uninstalling, where people look for it. (The database and the configuration
        // file are the user's and stay.)
        component.addOperation("CreateShortcut", "@TargetDir@/Uninstall QA Test Tracker.exe", "@StartMenuDir@/Uninstall QA Test Tracker.lnk",
                               "workingDirectory=@TargetDir@", "description=Remove QA Test Tracker from this PC");
    }

    // The database the user chose: made if it is new, and written into the user's
    // configuration file - by the program itself, which knows both.
    var database = installer.value("QaDatabase");
    if (!database || database === "")
        database = ownDatabase;
    if (installer.value("QaDatabaseExisting") === "true")
        component.addOperation("Execute", "@TargetDir@/QATest.exe", "--config", configFile, "--set-database", database, "--existing");
    else
        component.addOperation("Execute", "@TargetDir@/QATest.exe", "--config", configFile, "--set-database", database);
}
