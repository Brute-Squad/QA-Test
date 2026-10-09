// The tests of QA Test Tracker. Exit code 0 = every check passed; what
// failed is printed. Three parts:
//
//   the settings   the configuration file: where the database is - a shared drive -
//                  and what is wrong with a file
//   the database   projects, suites, cases with their steps, runs and their
//                  results, what is refused, what goes with what is deleted,
//                  test scripts read from a file and written back
//   the scripts    every file in scripts/ is read into an empty database:
//                  each can be imported, and each of its cases has steps
//                  that say what to expect
//   the window     the program's own window and panels, offscreen, on a
//                  database in memory: the tree, a case edited and saved, a
//                  run made and worked through, its report
#include "casepanel.h"
#include "mainwindow.h"
#include "qaconfig.h"
#include "qadatabase.h"
#include "report.h"
#include "runpanel.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTreeWidget>

namespace
{
    int g_failures = 0;
    int g_checks = 0;

    void check(bool condition, const QString &what)
    {
        ++g_checks;
        if (condition)
            return;
        ++g_failures;
        QTextStream(stderr) << "FAILED: " << what << Qt::endl;
    }

    QJsonObject sampleScripts()
    {
        const auto step = [](const char *action, const char *expected) {
            return QJsonObject { { "action", action }, { "expected", expected } };
        };
        return QJsonObject {
            { "project", "Sample" }, { "description", "A sample project" },
            { "suites", QJsonArray {
                QJsonObject { { "name", "Login" }, { "description", "Getting in" }, { "cases", QJsonArray {
                    QJsonObject { { "key", "S-LOGIN-001" }, { "title", "Log in" }, { "priority", "High" }, { "area", "Desktop" },
                                  { "preconditions", "A user exists" },
                                  { "steps", QJsonArray { step("Start the app", "The login dialog opens"), step("Type name and password", "The main window opens") } } },
                    QJsonObject { { "key", "S-LOGIN-002" }, { "title", "A wrong password" }, { "priority", "odd" },
                                  { "steps", QJsonArray { step("Type a wrong password", "It is refused"), step("", "") } } } } } },
                QJsonObject { { "name", "Parts" }, { "cases", QJsonArray {
                    QJsonObject { { "key", "S-PARTS-001" }, { "title", "Add a part" },
                                  { "steps", QJsonArray { step("Add a part", "It is in the list") } } } } } } } },
        };
    }

    void databaseTests()
    {
        QaDatabase db;
        QString error;
        check(!db.isOpen() && db.open(":memory:", error) && db.isOpen(), "a database in memory opens: " + error);

        // ---- projects and suites
        QaProject project;
        project.name = "  Factory  ";
        check(db.addProject(project, error) && project.id > 0 && project.name == "Factory", "a project is added, its name trimmed: " + error);
        QaProject again;
        again.name = "Factory";
        check(!db.addProject(again, error) && error == "There is a project \"Factory\" already.", "a name is a project's own: " + error);
        again.name = " ";
        check(!db.addProject(again, error) && error == "A project needs a name.", "and a project needs one");
        QaSuite stock;
        stock.projectId = project.id;
        stock.name = "Stock";
        QaSuite orders;
        orders.projectId = project.id;
        orders.name = "Orders";
        check(db.addSuite(stock, error) && db.addSuite(orders, error), "two suites: " + error);
        QaSuite twice;
        twice.projectId = project.id;
        twice.name = "Stock";
        check(!db.addSuite(twice, error) && error.contains("has a suite \"Stock\" already"), "a suite's name is its own in the project: " + error);
        QList<QaSuite> suites;
        check(db.suites(project.id, suites, error) && suites.size() == 2 && suites.at(0).name == "Stock" && suites.at(1).name == "Orders" && suites.at(0).caseCount == 0,
              "the suites in the order they were put in");

        // ---- cases and their steps
        check(db.nextKey(stock.id) == "TC-001", "the first key of a suite without cases: " + db.nextKey(stock.id));
        QaCase count;
        count.suiteId = stock.id;
        count.key = "F-STOCK-001";
        count.title = "Count the stock";
        count.priority = "High";
        count.area = "Desktop";
        count.preconditions = "Two lots";
        count.steps = { { "Open Stock Count", "The lots are listed" }, { " ", "" }, { "Type what was found", "It is taken" }, { "Post", "The stock is put right" } };
        check(db.saveCase(count, error) && count.id > 0, "a case is stored: " + error);
        QaCase read;
        check(db.loadCase(count.id, read, error) && read.key == "F-STOCK-001" && read.title == "Count the stock" && read.priority == "High" && read.area == "Desktop"
              && read.preconditions == "Two lots" && read.steps.size() == 3 && read.steps.at(1).action == "Type what was found" && read.steps.at(2).expected == "The stock is put right",
              "and read back with its steps in order, an empty one left out");
        check(db.nextKey(stock.id) == "F-STOCK-002", "the next key follows the suite's keys: " + db.nextKey(stock.id));
        QaCase same;
        same.suiteId = orders.id;
        same.key = "F-STOCK-001";
        same.title = "Another";
        check(!db.saveCase(same, error) && error == "The project has a test case with the key F-STOCK-001 already.", "a key is a case's own in the project: " + error);
        same.key = "";
        check(!db.saveCase(same, error) && error == "A test case needs a key and a title.", "a case needs a key and a title");
        read.title = "Count the stock, twice";
        read.priority = "Whatever";
        read.steps.removeLast();
        read.steps.prepend({ "Log in", "The window opens" });
        check(db.saveCase(read, error) && db.loadCase(count.id, read, error) && read.title == "Count the stock, twice" && read.priority == "Medium"
              && read.steps.size() == 3 && read.steps.first().action == "Log in", "a change is stored - the steps anew, a priority that is none as Medium: " + error);
        QaCase ship;
        ship.suiteId = orders.id;
        ship.key = "F-ORDER-001";
        ship.title = "Ship an order";
        ship.steps = { { "Ship", "It is shipped" } };
        QaCase reserve = ship;
        reserve.key = "F-ORDER-002";
        reserve.title = "Reserve an order";
        check(db.saveCase(ship, error) && db.saveCase(reserve, error), "two more cases: " + error);
        QList<QaCase> cases;
        check(db.cases(orders.id, cases, error) && cases.size() == 2 && cases.at(0).key == "F-ORDER-001" && cases.at(0).lastStatus.isEmpty() && cases.at(0).steps.isEmpty(),
              "a suite's cases by key, never run");

        // ---- runs and results
        QaRun run;
        run.projectId = project.id;
        run.name = " ";
        check(!db.createRun(run, {}, error) && error == "A test run needs a name.", "a run needs a name");
        run.name = "Beta 1";
        run.build = "0.2.0 Beta";
        run.tester = "pat";
        check(db.createRun(run, {}, error) && run.id > 0 && !run.started.isEmpty(), "a run of every suite: " + error);
        QList<QaResult> results;
        check(db.results(run.id, results, error) && results.size() == 3 && results.at(0).caseKey == "F-STOCK-001" && results.at(0).suiteName == "Stock"
              && results.at(1).caseKey == "F-ORDER-001" && results.at(0).status == "Not run" && results.at(0).executed.isEmpty(),
              "its cases, by suite and key, none run");
        QaSummary summary;
        check(db.summary(run.id, summary, error) && summary.total == 3 && summary.notRun == 3
              && summary.text() == "3 test cases: 0 passed, 0 failed, 0 blocked, 0 skipped, 3 not run - 0% done", summary.text());
        check(!db.setResult(run.id, count.id, "Fine", "", 0, "pat", error) && error.contains("is no result"), "what is no result is refused: " + error);
        check(!db.setResult(run.id, 999999, "Passed", "", 0, "pat", error) && error == "That test case is not part of the run.", "nor a case that is not in the run");
        check(db.setResult(run.id, count.id, "Passed", "", 2, "pat", error) && db.setResult(run.id, ship.id, "Failed", "Nothing happened", 1, "pat", error),
              "one passed, one failed: " + error);
        check(db.results(run.id, results, error) && results.at(0).status == "Passed" && results.at(0).failedStep == 0 && results.at(0).tester == "pat"
              && !results.at(0).executed.isEmpty() && results.at(1).status == "Failed" && results.at(1).failedStep == 1 && results.at(1).notes == "Nothing happened",
              "what was stored, a failed step only for a failure");
        check(db.summary(run.id, summary, error) && summary.passed == 1 && summary.failed == 1 && summary.notRun == 1
              && summary.text() == "3 test cases: 1 passed, 1 failed, 0 blocked, 0 skipped, 1 not run - 66% done", summary.text());
        check(db.setResult(run.id, count.id, "Not run", "", 0, "pat", error) && db.results(run.id, results, error) && results.at(0).executed.isEmpty()
              && results.at(0).tester.isEmpty(), "Not run takes back when and by whom");
        check(db.cases(orders.id, cases, error) && cases.at(0).lastStatus == "Failed" && cases.at(1).lastStatus.isEmpty(), "a case says how it went the last time");

        // A run of one suite, and a case's history: the newest run first.
        QaRun second;
        second.projectId = project.id;
        second.name = "Beta 2";
        check(db.createRun(second, { orders.id }, error) && db.results(second.id, results, error) && results.size() == 2, "a run of one suite has its cases alone: " + error);
        check(db.setResult(second.id, ship.id, "Blocked", "No server", 0, "lou", error), "blocked in the second run");
        QList<QaResult> history;
        QStringList names;
        check(db.history(ship.id, history, names, error) && history.size() == 2 && names.contains("Beta 2") && names.contains("Beta 1 (0.2.0 Beta)"),
              "a case's results in the runs: " + names.join(" | "));
        QaRun empty;
        empty.projectId = project.id;
        empty.name = "Nothing";
        QaSuite none;
        none.projectId = project.id;
        none.name = "Empty";
        check(db.addSuite(none, error) && !db.createRun(empty, { none.id }, error) && error.contains("no test cases to run"), "a run of nothing is not made: " + error);
        QList<QaRun> runs;
        check(db.runs(project.id, runs, error) && runs.size() == 2, "and left no run behind");
        second.finished = "2026-10-09T10:00:00Z";
        second.name = "Beta 2, done";
        check(db.updateRun(second, error) && db.runs(project.id, runs, error) && (runs.at(0).name == "Beta 2, done" || runs.at(1).name == "Beta 2, done"), "a run is finished and renamed");

        // ---- what goes with what is deleted
        check(db.deleteCase(ship.id, error) && db.results(run.id, results, error) && results.size() == 2 && db.history(ship.id, history, names, error) && history.isEmpty(),
              "a case takes its steps and results with it: " + error);
        check(db.deleteRun(second.id, error) && db.runs(project.id, runs, error) && runs.size() == 1, "a run is deleted");
        check(db.deleteSuite(orders.id, error) && db.results(run.id, results, error) && results.size() == 1, "a suite takes its cases with it");
        check(db.deleteProject(project.id, error) && db.runs(project.id, runs, error) && runs.isEmpty() && db.suites(project.id, suites, error) && suites.isEmpty()
              && !db.loadCase(count.id, read, error), "a project takes everything of it with it");

        // ---- test scripts as a file
        QaImportCounts counts;
        check(!db.importJson(QJsonObject(), counts, error) && error.contains("names no project"), "what names no project is no file of scripts: " + error);
        check(db.importJson(sampleScripts(), counts, error) && counts.projects == 1 && counts.suites == 2 && counts.casesAdded == 3 && counts.casesUpdated == 0
              && counts.text() == "1 project, 2 suites and 3 test cases were added.", "a file is imported: " + error + counts.text());
        QList<QaProject> projects;
        check(db.projects(projects, error) && projects.size() == 1 && projects.first().name == "Sample" && projects.first().description == "A sample project", "its project");
        const qint64 sampleId = projects.value(0).id;
        check(db.suites(sampleId, suites, error) && suites.size() == 2 && suites.at(0).name == "Login" && suites.at(0).caseCount == 2, "its suites, in the file's order");
        check(db.cases(suites.value(0).id, cases, error) && cases.size() == 2 && db.loadCase(cases.value(1).id, read, error) && read.priority == "Medium"
              && read.steps.size() == 1, "its cases - a priority that is none as Medium, an empty step left out");
        // A second time: brought up to date, nothing twice; a result stays.
        QaRun kept;
        kept.projectId = sampleId;
        kept.name = "Kept";
        check(db.createRun(kept, {}, error) && db.setResult(kept.id, cases.value(0).id, "Passed", "", 0, "pat", error), "a run of the imported cases");
        QJsonObject changed = sampleScripts();
        {
            QJsonArray suitesJson = changed.value("suites").toArray();
            QJsonObject login = suitesJson.at(0).toObject();
            QJsonArray loginCases = login.value("cases").toArray();
            QJsonObject first = loginCases.at(0).toObject();
            first.insert("title", "Log in as a user");
            loginCases.replace(0, first);
            loginCases.append(QJsonObject { { "key", "S-LOGIN-003" }, { "title", "Log out" }, { "steps", QJsonArray { QJsonObject { { "action", "Log out" }, { "expected", "Logged out" } } } } });
            login.insert("cases", loginCases);
            suitesJson.replace(0, login);
            changed.insert("suites", suitesJson);
        }
        check(db.importJson(changed, counts, error) && counts.projects == 0 && counts.suites == 0 && counts.casesAdded == 1 && counts.casesUpdated == 3
              && counts.text() == "1 test case was added; 3 test cases were updated.", "imported again: " + error + counts.text());
        check(db.cases(suites.value(0).id, cases, error) && cases.size() == 3 && cases.at(0).title == "Log in as a user" && cases.at(0).lastStatus == "Passed",
              "the cases are up to date, none is there twice, and what was recorded stays");
        QJsonObject bad = sampleScripts();
        {
            QJsonArray suitesJson = bad.value("suites").toArray();
            suitesJson.append(QJsonObject { { "name", "Broken" }, { "cases", QJsonArray { QJsonObject { { "key", "" }, { "title", "No key" } } } } });
            bad.insert("suites", suitesJson);
        }
        check(!db.importJson(bad, counts, error) && error.contains("no key or no title") && db.suites(sampleId, suites, error) && suites.size() == 2,
              "a file with a case that has no key is not imported, none of it: " + error);
        QJsonObject written;
        check(db.exportJson(sampleId, written, error) && written.value("project").toString() == "Sample" && written.value("suites").toArray().size() == 2
              && written.value("suites").toArray().at(0).toObject().value("cases").toArray().size() == 3
              && written.value("suites").toArray().at(0).toObject().value("cases").toArray().at(0).toObject().value("steps").toArray().size() == 2,
              "a project is written back as a file: " + error);
        QaDatabase other;
        check(other.open(":memory:", error) && other.importJson(written, counts, error) && counts.casesAdded == 4, "which another database reads: " + error);

        // ---- a file on the disk
        QTemporaryDir folder;
        const QString path = folder.filePath("sub/qa.sqlite");
        {
            QaDatabase onDisk;
            check(onDisk.open(path, error) && onDisk.importJson(sampleScripts(), counts, error), "a database in a folder that is not there yet: " + error);
        }
        {
            QaDatabase onDisk;
            check(onDisk.open(path, error) && onDisk.projects(projects, error) && projects.size() == 1, "is still there when it is opened again: " + error);
        }
    }

    void configTests()
    {
        // No file is no problem and no setting.
        QTemporaryDir folder;
        QaConfig config = QaConfigFile::read(folder.filePath("QATest.ini"));
        check(config.file.isEmpty() && config.databasePath.isEmpty() && config.busyTimeoutSeconds == 10 && config.problem.isEmpty(), "no configuration file: nothing set");
        check(QaConfigFile::fileName() == "QATest.ini", "what the file is called");

        // A network path is written as it is: a backslash is a backslash.
        config = QaConfigFile::parse("; a comment\n# another\n\n[Database]\nPath=\\\\fileserver\\qa\\qatest.sqlite\nBusyTimeoutSeconds = 30\n", "C:/Tools/QATest");
        check(config.databasePath == "//fileserver/qa/qatest.sqlite" && config.busyTimeoutSeconds == 30 && config.problem.isEmpty(),
              "a database on a file server: " + config.databasePath + " " + config.problem);
        config = QaConfigFile::parse("[database]\r\n  PATH = \"Q:\\QA Team\\qatest.sqlite\"  \r\n", "C:/Tools/QATest");
        check(config.databasePath == "Q:/QA Team/qatest.sqlite", "a drive letter, quotation marks, any capitals, Windows line ends: " + config.databasePath);
        // A path that is not absolute is meant from the file's folder.
        config = QaConfigFile::parse("[Database]\nPath=..\\shared\\qa.sqlite\n", "C:/Tools/QATest");
        check(config.databasePath == "C:/Tools/shared/qa.sqlite", "a relative path: " + config.databasePath);
        // A variable of the environment is filled in; one that is not set stays.
        qputenv("QATEST_SHARE", "S:\\Team");
        config = QaConfigFile::parse("[Database]\nPath=%QATEST_SHARE%\\qa.sqlite\n", "C:/Tools");
        check(config.databasePath == "S:/Team/qa.sqlite", "a variable of the environment: " + config.databasePath);
        config = QaConfigFile::parse("[Database]\nPath=%QATEST_NOT_SET%\\qa.sqlite\n", "C:/Tools");
        check(config.databasePath.endsWith("%QATEST_NOT_SET%/qa.sqlite"), "one that is not set is left as written: " + config.databasePath);
        // What is commented out, empty or in another section sets nothing.
        config = QaConfigFile::parse("[Database]\n;Path=X:\\no.sqlite\nPath=\n[Other]\nPath=Y:\\no.sqlite\n", "C:/Tools");
        check(config.databasePath.isEmpty() && config.problem.isEmpty(), "nothing said: " + config.databasePath);
        // What makes no sense is said, and the rest still counts.
        config = QaConfigFile::parse("[Database]\nBusyTimeoutSeconds=soon\nPath=Q:\\qa.sqlite\nthis is no setting\n", "C:/Tools");
        check(config.databasePath == "Q:/qa.sqlite" && config.busyTimeoutSeconds == 10 && config.problem.contains("BusyTimeoutSeconds is \"soon\"")
              && config.problem.contains("line 4"), "what is wrong is said: " + config.problem);

        // The path is set in a file, and taken away again, with everything else left as it is.
        {
            const QString start = QaConfigFile::sample();
            const QString set = QaConfigFile::withDatabasePath(start, "\\\\fileserver\\qa\\team.sqlite");
            QaConfig read = QaConfigFile::parse(set, "C:/Tools");
            check(read.databasePath == "//fileserver/qa/team.sqlite" && read.problem.isEmpty() && set.count("\r\n") == start.count("\r\n")
                  && set.contains("; How many seconds to wait"), "the path is set where the file has its line: " + read.databasePath);
            const QString other = QaConfigFile::withDatabasePath(set, "Q:\\QA\\other.sqlite");
            check(QaConfigFile::parse(other, "C:/Tools").databasePath == "Q:/QA/other.sqlite" && other.count("\nPath=") == 1 && !other.contains("team.sqlite"),
                  "and changed, the line there once");
            const QString cleared = QaConfigFile::withDatabasePath(other, " ");
            check(QaConfigFile::parse(cleared, "C:/Tools").databasePath.isEmpty() && cleared == start, "and taken away: the file is as it started");
            check(QaConfigFile::parse(QaConfigFile::withDatabasePath("[Database]\nBusyTimeoutSeconds=3\n", "Q:\\a.sqlite"), "C:/").databasePath == "Q:/a.sqlite"
                  && QaConfigFile::parse(QaConfigFile::withDatabasePath("[Database]\nBusyTimeoutSeconds=3\n", "Q:\\a.sqlite"), "C:/").busyTimeoutSeconds == 3,
                  "a file without the line gets it under its section");
            check(QaConfigFile::parse(QaConfigFile::withDatabasePath("; only a comment\n", "Q:\\b.sqlite"), "C:/").databasePath == "Q:/b.sqlite"
                  && QaConfigFile::parse(QaConfigFile::withDatabasePath("", "Q:\\c.sqlite"), "C:/").databasePath == "Q:/c.sqlite",
                  "a file without the section, and an empty one, get both");
            check(QaConfigFile::parse(QaConfigFile::withDatabasePath("[Other]\nPath=X:\\keep\n[Database]\n;Path=\n", "Q:\\d.sqlite"), "C:/").databasePath == "Q:/d.sqlite"
                  && QaConfigFile::withDatabasePath("[Other]\nPath=X:\\keep\n[Database]\n;Path=\n", "Q:\\d.sqlite").contains("Path=X:\\keep"),
                  "another section's line of that name is not touched");
        }

        // The file to start from sets nothing, and explains itself.
        config = QaConfigFile::parse(QaConfigFile::sample(), "C:/Tools");
        check(config.databasePath.isEmpty() && config.busyTimeoutSeconds == 10 && config.problem.isEmpty() && QaConfigFile::sample().contains("[Database]")
              && QaConfigFile::sample().contains("Path=\\\\fileserver\\qa\\qatest.sqlite"), "the sample sets nothing: " + config.problem);

        // A file on the disk, naming a database in a folder that two programs then share.
        QDir(folder.path()).mkpath("share");
        QFile file(folder.filePath("QATest.ini"));
        check(file.open(QIODevice::WriteOnly) && file.write("[Database]\nPath=share\\team.sqlite\nBusyTimeoutSeconds=2\n") > 0, "a configuration file is written");
        file.close();
        config = QaConfigFile::read(folder.filePath("QATest.ini"));
        check(config.file == QDir::cleanPath(folder.filePath("QATest.ini")) && config.databasePath == QDir::cleanPath(folder.filePath("share/team.sqlite"))
              && config.busyTimeoutSeconds == 2, "and read: " + config.databasePath);
        {
            QaDatabase first, second;
            QString error;
            first.setBusyTimeout(config.busyTimeoutSeconds);
            second.setBusyTimeout(config.busyTimeoutSeconds);
            check(first.open(config.databasePath, error) && second.open(config.databasePath, error), "two programs open the same database: " + error);
            QaProject project;
            project.name = "Shared";
            QList<QaProject> seen;
            check(first.addProject(project, error) && second.projects(seen, error) && seen.size() == 1 && seen.first().name == "Shared",
                  "what one writes the other reads: " + error);
            QaSuite suite;
            suite.projectId = project.id;
            suite.name = "From the second";
            QList<QaSuite> suites;
            check(second.addSuite(suite, error) && first.suites(project.id, suites, error) && suites.size() == 1, "and the other way round: " + error);
        }
    }

    void bundledTests()
    {
        // A database with nothing in it takes the scripts that came with the program; one with a project is left alone.
        QaDatabase db;
        QString error;
        check(db.open(":memory:", error), "a database for the scripts that come with the program");
        QTemporaryDir empty;
        check(MainWindow::importBundled(db, empty.path()).isEmpty() && MainWindow::importBundled(db, empty.filePath("not-there")).isEmpty(),
              "no scripts beside the program: nothing is done");
        const QString said = MainWindow::importBundled(db, QStringLiteral(QA_SCRIPTS_DIR));
        QList<QaProject> projects;
        check(said.startsWith("FactoryInventory.json: 1 project, ") && db.projects(projects, error) && projects.size() == 1, "an empty database starts with them: " + said);
        check(MainWindow::importBundled(db, QStringLiteral(QA_SCRIPTS_DIR)).isEmpty(), "a database that has a project is left as it is");

        // A configuration file to start from, in a folder that is not there yet.
        QTemporaryDir folder;
        const QString path = folder.filePath("QATest/QATest/QATest.ini");
        QString problem;
        check(MainWindow::writeSampleConfig(path, problem) && QFile::exists(path), "the configuration file is made with its folder: " + problem);
        const QaConfig config = QaConfigFile::read(path);
        check(config.file == QDir::cleanPath(path) && config.databasePath.isEmpty() && config.problem.isEmpty(), "and sets nothing");
    }

    void scriptTests()
    {
        const QDir folder(QStringLiteral(QA_SCRIPTS_DIR));
        const QStringList files = folder.entryList({ "*.json" }, QDir::Files);
        check(!files.isEmpty(), "there are test scripts in " + folder.absolutePath());
        for (const QString &name : files)
        {
            QaDatabase db;
            QString error, message;
            check(db.open(":memory:", error), "a database for " + name);
            check(MainWindow::importFile(db, folder.filePath(name), message), name + " is imported: " + message);
            QTextStream(stdout) << name << ": " << message << Qt::endl;

            QList<QaProject> projects;
            db.projects(projects, error);
            check(projects.size() == 1, name + " is one project");
            QList<QaSuite> suites;
            db.suites(projects.value(0).id, suites, error);
            check(suites.size() >= 3, name + " has suites");
            int total = 0;
            QSet<QString> titles;
            for (const QaSuite &suite : std::as_const(suites))
            {
                QList<QaCase> cases;
                db.cases(suite.id, cases, error);
                check(!cases.isEmpty(), name + ": the suite " + suite.name + " has cases");
                for (const QaCase &listed : std::as_const(cases))
                {
                    QaCase testCase;
                    db.loadCase(listed.id, testCase, error);
                    ++total;
                    check(testCase.steps.size() >= 2, name + ": " + testCase.key + " has at least two steps");
                    check(QaDatabase::priorities().contains(testCase.priority) && !testCase.area.isEmpty(), name + ": " + testCase.key + " says its priority and where it is run");
                    check(!titles.contains(testCase.title), name + ": the title of " + testCase.key + " is its own: " + testCase.title);
                    titles << testCase.title;
                    for (int i = 0; i < testCase.steps.size(); ++i)
                        check(!testCase.steps.at(i).action.isEmpty() && !testCase.steps.at(i).expected.isEmpty(),
                              QStringLiteral("%1: step %2 of %3 says what to do and what to expect").arg(name).arg(i + 1).arg(testCase.key));
                }
            }
            check(total >= 20, QStringLiteral("%1 has %2 test cases").arg(name).arg(total));
        }
    }

    void windowTests()
    {
        QaDatabase db;
        QString error;
        QaImportCounts counts;
        check(db.open(":memory:", error) && db.importJson(sampleScripts(), counts, error), "a database for the window: " + error);

        MainWindow window(&db);
        QTreeWidget *tree = window.tree();
        check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->text(0) == "Sample" && tree->topLevelItem(0)->childCount() == 2
              && tree->topLevelItem(0)->child(0)->text(0) == "Login  (2)" && tree->topLevelItem(0)->child(0)->child(0)->text(0) == "S-LOGIN-001  Log in",
              "the tree: the project, its suites with how many cases, its cases");

        // ---- a case, read and changed
        CasePanel *casePanel = window.casePanel();
        auto *title = casePanel->findChild<QLineEdit *>("caseTitle");
        auto *key = casePanel->findChild<QLineEdit *>("caseKey");
        auto *steps = casePanel->findChild<QTableWidget *>("caseSteps");
        auto *save = casePanel->findChild<QPushButton *>("caseSave");
        check(title && key && steps && save, "the case panel's pieces");
        if (!title || !key || !steps || !save)
            return;
        check(!title->isEnabled() && !save->isEnabled(), "nothing to type while no case is selected");
        check(window.selectCase("S-LOGIN-001") && casePanel->caseId() != 0 && key->text() == "S-LOGIN-001" && title->text() == "Log in" && title->isEnabled()
              && steps->rowCount() == 2 && steps->item(1, 1)->text() == "The main window opens" && !save->isEnabled() && !casePanel->isChanged(),
              "a selected case is shown, with nothing to save");
        title->setText("Log in with a password");
        check(save->isEnabled() && casePanel->isChanged(), "a change is something to save");
        title->setText("");
        check(!save->isEnabled(), "but not without a title");
        title->setText("Log in with a password");
        steps->item(0, 1)->setText("The login dialog is shown");
        save->click();
        QaCase stored;
        check(db.loadCase(casePanel->caseId(), stored, error) && stored.title == "Log in with a password" && stored.steps.value(0).expected == "The login dialog is shown"
              && !casePanel->isChanged() && !save->isEnabled(), "Save stores it: " + error);
        check(tree->currentItem() && tree->currentItem()->text(0) == "S-LOGIN-001  Log in with a password", "and the tree follows, the case still selected");
        // A key another case has is refused, with the reason on the panel.
        key->setText("S-LOGIN-002");
        save->click();
        auto *problem = casePanel->findChild<QLabel *>("caseProblem");
        check(problem && !problem->isHidden() && problem->text() == "The project has a test case with the key S-LOGIN-002 already.", "a key that is taken is said");
        key->setText("S-LOGIN-001");
        save->click();

        // ---- a run, worked through
        RunPanel *runs = window.runPanel();
        auto *summary = runs->findChild<QLabel *>("runSummary");
        auto *table = runs->findChild<QTableWidget *>("runResults");
        auto *notes = runs->findChild<QPlainTextEdit *>("runNotes");
        auto *runSteps = runs->findChild<QTableWidget *>("runSteps");
        auto *passed = runs->findChild<QPushButton *>("markPassed");
        auto *failed = runs->findChild<QPushButton *>("markFailed");
        auto *failedStep = runs->findChild<QSpinBox *>("runFailedStep");
        auto *runProblem = runs->findChild<QLabel *>("runProblem");
        auto *filter = runs->findChild<QComboBox *>("runFilter");
        check(summary && table && notes && runSteps && passed && failed && failedStep && runProblem && filter, "the run panel's pieces");
        if (!summary || !table || !notes || !runSteps || !passed || !failed || !failedStep || !runProblem || !filter)
            return;
        check(runs->projectId() != 0 && summary->text() == "Sample has no test run yet. New Run... starts one." && !passed->isEnabled(), "no run yet: " + summary->text());
        check(!runs->createRun("", "", "", {}, error) && runs->createRun("First", "1.0", "pat", {}, error) && runs->runId() != 0, "a run is made: " + error);
        check(table->rowCount() == 3 && summary->text() == "3 test cases: 0 passed, 0 failed, 0 blocked, 0 skipped, 3 not run - 0% done", "its cases: " + summary->text());
        check(table->currentRow() == 0 && runSteps->rowCount() == 2 && runSteps->item(0, 1)->text() == "Start the app" && passed->isEnabled(),
              "the first case is selected, with its steps to work through");
        passed->click();
        check(table->item(0, 3)->text().endsWith("Passed") && table->item(0, 4)->text() == "pat" && table->currentRow() == 1, "Passed is stored, and the next case selected");
        // A failure says what happened.
        failed->click();
        check(!runProblem->isHidden() && runProblem->text().startsWith("Say in the notes what happened") && table->item(1, 3)->text() == "Not run", "Failed wants a note first");
        notes->setPlainText("It was let in");
        failedStep->setValue(1);
        failed->click();
        check(table->item(1, 3)->text().endsWith("Failed (step 1)") && table->item(1, 6)->text() == "It was let in" && table->currentRow() == 2, "then it is stored with its step");
        check(summary->text() == "3 test cases: 1 passed, 1 failed, 0 blocked, 0 skipped, 1 not run - 66% done", summary->text());
        check(tree->topLevelItem(0)->child(0)->child(0)->text(0).endsWith("S-LOGIN-001  Log in with a password")
              && tree->topLevelItem(0)->child(0)->child(0)->text(0) != "S-LOGIN-001  Log in with a password", "the tree marks how a case went");
        filter->setCurrentIndex(filter->findText("Failed"));
        check(table->rowCount() == 1 && table->item(0, 1)->text() == "S-LOGIN-002", "the filter shows the failures alone");
        filter->setCurrentIndex(0);
        check(table->rowCount() == 3, "and everything again");
        // What is being typed about a case stays when the lists are read again (the database may be shared).
        notes->setPlainText("Half a sentence");
        failedStep->setValue(1);
        const int selected = table->currentRow();
        window.reload();
        check(table->currentRow() == selected && notes->toPlainText() == "Half a sentence" && failedStep->value() == 1, "a reload keeps what is being typed");
        notes->clear();
        failedStep->setValue(0);

        // ---- the report
        const QString html = runs->reportHtml();
        check(html.contains("Test Report: Sample") && html.contains("First") && html.contains("1.0") && html.contains("Failed and blocked")
              && html.contains("Failed at step 1") && html.contains("It was let in") && html.contains("<h2>Login</h2>") && html.contains("<h2>Parts</h2>"),
              "the report says the run, what failed and every case by suite");
        QTemporaryDir folder;
        check(Report::saveHtml(html, folder.filePath("report.html"), error) && QFile(folder.filePath("report.html")).size() > 500, "written as HTML: " + error);
        check(Report::savePdf(html, folder.filePath("report.pdf"), error) && QFile(folder.filePath("report.pdf")).size() > 1000, "and as a PDF: " + error);
        check(!Report::saveHtml(html, folder.filePath("no/such/folder/report.html"), error) && !error.isEmpty(), "a folder that is not there is said");
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // Nothing of the tests is kept in the user's settings.
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QCoreApplication::setOrganizationName(QStringLiteral("QATestTests"));
    QCoreApplication::setApplicationName(QStringLiteral("QATestTests"));

    databaseTests();
    configTests();
    scriptTests();
    bundledTests();
    windowTests();

    QTextStream out(stdout);
    if (g_failures == 0)
        out << "All " << g_checks << " checks passed." << Qt::endl;
    else
        out << g_failures << " of " << g_checks << " checks FAILED." << Qt::endl;
    return g_failures == 0 ? 0 : 1;
}
