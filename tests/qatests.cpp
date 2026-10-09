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
//   sharing        what a database on a shared drive needs: two people in
//                  one file - a case one of them stored is not written over
//                  by the other - a database from before, a copy a day, a
//                  drive letter as the share's own name, the window while
//                  the database is away, File > Use a Shared Database
//   the window     the program's own window and panels, offscreen, on a
//                  database in memory: the tree, a case edited and saved, a
//                  run made and worked through, its report
#include "casepanel.h"
#include "mainwindow.h"
#include "qabackup.h"
#include "qaconfig.h"
#include "qashare.h"
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
#include <QSqlDatabase>
#include <QSqlQuery>
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

    // ---- a database several people work in ---------------------------------------------------------
    void sharedTests()
    {
        QTemporaryDir folder;
        const QString file = folder.filePath("team/qatest.sqlite");
        QString error;
        QaImportCounts counts;

        // ---- two people in one file
        QaDatabase pat, lou;
        pat.setUser("pat");
        lou.setUser(" lou ");
        check(QaDatabase().user() == QaDatabase::systemUser() && !QaDatabase::systemUser().isEmpty() && lou.user() == "lou",
              "who is at this PC: the name they are logged in with, unless said");
        check(pat.open(file, error) && pat.importJson(sampleScripts(), counts, error) && lou.open(file, error), "two programs in one database: " + error);
        QList<QaProject> projects;
        QList<QaSuite> suites;
        QList<QaCase> cases;
        pat.projects(projects, error);
        pat.suites(projects.value(0).id, suites, error);
        pat.cases(suites.value(0).id, cases, error);
        const qint64 id = cases.value(0).id;
        const qint64 second = cases.value(1).id;
        QaCase pats, lous, read;
        check(pat.loadCase(id, pats, error) && lou.loadCase(id, lous, error) && pats.revision == 1 && lous.revision == 1 && pats.changedBy.isEmpty() && !pats.updated.isEmpty(),
              "both read the same case, at the same revision");
        lous.title = "Log in, as Lou wrote it";
        check(lou.saveCase(lous, error) && !lou.saveConflicted() && lous.revision == 2 && lous.changedBy == "lou", "the first to store it does: " + error);
        pats.title = "Log in, as Pat wrote it";
        pats.steps.clear();
        check(!pat.saveCase(pats, error) && pat.saveConflicted() && error.startsWith("lou changed this test case at ") && error.endsWith("while you had it open. Nothing was stored."),
              "the second is told who was first: " + error);
        check(pat.loadCase(id, read, error) && read.title == "Log in, as Lou wrote it" && read.steps.size() == 2 && read.revision == 2,
              "and nothing of the first's is written over - not the steps either");
        check(pat.saveCase(pats, error, true) && !pat.saveConflicted() && pats.revision == 3 && pat.loadCase(id, read, error) && read.title == "Log in, as Pat wrote it"
              && read.changedBy == "pat" && read.revision == 3, "unless the second says so: " + error);
        check(lou.loadCase(id, lous, error), "read again");
        lous.notes = "Read again";
        check(lou.saveCase(lous, error) && !lou.saveConflicted() && lous.revision == 4, "what was read again is stored as ever: " + error);
        // A key that is taken is refused as ever - that is no conflict.
        check(lou.loadCase(id, lous, error), "read once more");
        lous.key = "S-LOGIN-002";
        check(!lou.saveCase(lous, error) && !lou.saveConflicted() && error.contains("with the key S-LOGIN-002 already"), "a key that is taken is no conflict: " + error);
        // Test scripts that are read in again change their cases too.
        check(pat.loadCase(second, pats, error) && lou.importJson(sampleScripts(), counts, error), "scripts are imported while a case is open");
        pats.notes = "Mine";
        check(!pat.saveCase(pats, error) && pat.saveConflicted() && error.startsWith("Somebody else changed this test case at "), "an import is a change by nobody in particular: " + error);
        // A case that is gone.
        check(pat.loadCase(id, pats, error) && lou.deleteCase(id, error) && !pat.saveCase(pats, error) && !pat.saveConflicted()
              && error.startsWith("This test case was deleted while you had it open."), "a case that was deleted meanwhile: " + error);
        QaCase fresh;
        fresh.suiteId = suites.value(0).id;
        fresh.key = "S-LOGIN-009";
        fresh.title = "A new one";
        check(pat.saveCase(fresh, error) && fresh.revision == 1 && pat.loadCase(fresh.id, read, error) && read.revision == 1 && read.changedBy == "pat",
              "a new case starts at revision 1, with who made it: " + error);
        // A case whoever stores it did not read (revision 0) is stored without asking.
        read.revision = 0;
        read.title = "Stored unread";
        check(pat.saveCase(read, error) && read.revision == 2, "what was not read is not asked about: " + error);

        // ---- a database from before the revisions
        pat.close();
        lou.close();
        bool dropped = false;
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "raw");
            raw.setDatabaseName(file);
            if (raw.open())
            {
                QSqlQuery query(raw);
                dropped = query.exec("ALTER TABLE cases DROP COLUMN revision") && query.exec("ALTER TABLE cases DROP COLUMN changed_by");
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase("raw");
        check(dropped, "a database as an older version left it");
        check(pat.open(file, error) && lou.open(file, error) && pat.loadCase(second, read, error) && read.revision == 1 && read.changedBy.isEmpty() && read.title == "A wrong password",
              "is brought up to date when it is opened, by whoever is first - and keeps its cases: " + error);
        read.notes = "After the update";
        check(pat.saveCase(read, error) && read.revision == 2, "and its cases count their changes from then on: " + error);

        // ---- is it there, is it sound
        QString problem;
        check(pat.reachable(error) && pat.reopen(error) && pat.path() == file && pat.loadCase(second, read, error) && read.notes == "After the update",
              "a database that is there can be reached, and opened again: " + error);
        check(pat.sound(problem) && problem.isEmpty(), "and is sound: " + problem);
        QaDatabase none, memory;
        check(!none.reachable(error) && error == "No database is open." && !none.reopen(error) && !none.isOpen(), "none is open: " + error);
        check(memory.open(":memory:", error) && memory.reachable(error) && memory.reopen(error) && memory.isOpen(), "one in memory is always there");
        const QString copy = folder.filePath("copies/one.sqlite");
        QaDatabase other;
        check(pat.copyTo(copy, error) && other.open(copy, error) && other.projects(projects, error) && projects.size() == 1 && projects.at(0).name == "Sample",
              "a copy is a database of its own: " + error);
        other.close();
        check(!pat.copyTo(copy, error) && error.endsWith("is there already."), "and is never written over a file that is there: " + error);

        // ---- a copy a day
        const QDate day(2026, 10, 9);
        const QString backups = folder.filePath("team/backups");
        check(QaBackup::fileNameFor(file, day) == "qatest-2026-10-09.sqlite" && QaBackup::folderFor(file) == backups && QaBackup::folderFor(file, " D:/Copies/ ") == "D:/Copies"
              && QaBackup::copies(file).isEmpty(), "what a day's copy is called, and where it goes: " + QaBackup::folderFor(file));
        QaBackupOutcome outcome = QaBackup::daily(pat, day, 3);
        check(outcome.made && outcome.problem.isEmpty() && outcome.file == backups + "/qatest-2026-10-09.sqlite" && QFileInfo::exists(outcome.file) && outcome.removed == 0,
              "the first start of a day makes the day's copy: " + outcome.problem + " " + outcome.file);
        check(other.open(outcome.file, error) && other.projects(projects, error) && projects.size() == 1, "which is the database as it was: " + error);
        other.close();
        const QDateTime written = QFileInfo(outcome.file).lastModified();
        outcome = QaBackup::daily(lou, day, 3);
        check(!outcome.made && outcome.problem.isEmpty() && outcome.file == backups + "/qatest-2026-10-09.sqlite" && QFileInfo(outcome.file).lastModified() == written,
              "the next start of that day - anybody's - leaves it alone");
        // What else is in the folder is nobody's business.
        for (const QString &name : { QString("notes.txt"), QString("other-2026-01-01.sqlite"), QString("qatest-old.sqlite") })
        {
            QFile foreign(backups + "/" + name);
            foreign.open(QIODevice::WriteOnly);
            foreign.write("not ours");
        }
        int removed = 0;
        for (int later = 1; later <= 4; ++later)
        {
            outcome = QaBackup::daily(pat, day.addDays(later), 3);
            check(outcome.made && outcome.problem.isEmpty(), QStringLiteral("day %1 has its copy: %2").arg(later).arg(outcome.problem));
            removed += outcome.removed;
        }
        const QStringList copies = QaBackup::copies(file);
        check(copies.size() == 3 && removed == 2 && copies.at(0).endsWith("qatest-2026-10-13.sqlite") && copies.at(2).endsWith("qatest-2026-10-11.sqlite"),
              "the newest three are kept, the oldest deleted: " + copies.join(" "));
        check(QFileInfo::exists(backups + "/notes.txt") && QFileInfo::exists(backups + "/other-2026-01-01.sqlite") && QFileInfo::exists(backups + "/qatest-old.sqlite")
              && QDir(backups).entryList({ "*.tmp" }).isEmpty(), "nothing else in the folder is touched, and nothing half made is left");
        outcome = QaBackup::daily(pat, day.addDays(20), 0);
        check(!outcome.made && outcome.file.isEmpty() && outcome.problem.isEmpty() && QaBackup::copies(file).size() == 3, "keep 0: no copies are made, and none deleted");
        outcome = QaBackup::daily(memory, day, 3);
        check(!outcome.made && outcome.file.isEmpty() && outcome.problem.isEmpty(), "nor of a database in memory");
        outcome = QaBackup::daily(pat, day, 2, folder.filePath("elsewhere"));
        check(outcome.made && outcome.file == folder.filePath("elsewhere/qatest-2026-10-09.sqlite") && QaBackup::copies(file, folder.filePath("elsewhere")).size() == 1
              && QaBackup::copies(file).size() == 3, "another folder for the copies: " + outcome.problem);

        // A database that is not sound is not copied over the good copies.
        pat.close();
        lou.close();
        {
            QFile damaged(file);
            if (damaged.open(QIODevice::ReadWrite) && damaged.size() > 3 * 4096)
            {
                damaged.seek(4096);
                damaged.write(QByteArray(int(damaged.size()) - 4096, char(0xAB)));
            }
        }
        if (pat.open(file, error))
        {
            const bool isSound = pat.sound(problem);
            outcome = QaBackup::daily(pat, day.addDays(30), 3);
            check(!isSound && !problem.isEmpty() && !outcome.made && outcome.problem.startsWith("No copy was made today: the database is not sound")
                  && QaBackup::copies(file).size() == 3 && QaBackup::copies(file).at(0).endsWith("qatest-2026-10-13.sqlite"),
                  "a damaged database is said, not copied, and the copies from before stay: " + problem + " / " + outcome.problem);
            pat.close();
        }
        else
            check(error.contains("damaged") || error.contains("could not be opened"), "a damaged database is said: " + error);

        // ---- a drive letter is this PC's own; the share's name is everybody's
        check(QaShare::withRemote("M:\\QA-Test\\data\\qatest.sqlite", "\\\\nas1\\MyMedia") == "\\\\nas1\\MyMedia\\QA-Test\\data\\qatest.sqlite",
              "a path on a connected drive, under the share's name");
        check(QaShare::withRemote("m:/QA/x.sqlite", "\\\\nas1\\MyMedia\\") == "\\\\nas1\\MyMedia\\QA\\x.sqlite" && QaShare::withRemote("M:", "//nas1/MyMedia") == "\\\\nas1\\MyMedia",
              "however it is written");
        check(QaShare::withRemote("\\\\a\\b\\c.sqlite", "\\\\x\\y").isEmpty() && QaShare::withRemote("M:\\x.sqlite", "").isEmpty()
              && QaShare::withRemote("M:\\x.sqlite", "C:\\Somewhere").isEmpty() && QaShare::withRemote("data\\x.sqlite", "\\\\x\\y").isEmpty(),
              "what has no drive letter, or a drive that is no share, has no such name");
        check(QaShare::networkName(folder.path()).isEmpty() && QaShare::networkName("data/x.sqlite").isEmpty() && QaShare::networkName("\\\\server\\share\\x.sqlite").isEmpty(),
              "a folder of this PC's own has none");
        check(QaShare::isOnNetwork("\\\\server\\share\\x.sqlite") && QaShare::isOnNetwork("//server/share/x.sqlite") && !QaShare::isOnNetwork(folder.path()),
              "what is on the network, and what is not");

        // ---- the configuration: the copies
        QaConfig config = QaConfigFile::parse("", "C:/Tools/QATest");
        check(config.backupKeep == 14 && config.backupFolder.isEmpty(), "unless said: 14 copies, beside the database");
        config = QaConfigFile::parse("[Backup]\nKeep=30\nFolder=D:\\QA backups\n[Database]\nPath=x.sqlite\n", "C:/Tools/QATest");
        check(config.backupKeep == 30 && config.backupFolder == "D:/QA backups" && config.databasePath == "C:/Tools/QATest/x.sqlite" && config.problem.isEmpty(),
              "how many copies, and where: " + config.backupFolder + " " + config.problem);
        config = QaConfigFile::parse("[backup]\r\nfolder = copies\r\nkeep = 0\r\n", "C:/Tools/QATest");
        check(config.backupKeep == 0 && config.backupFolder == "C:/Tools/QATest/copies", "none at all; a folder from the file's own: " + config.backupFolder);
        config = QaConfigFile::parse("[Backup]\nKeep=many\n", "C:/Tools/QATest");
        check(config.backupKeep == 14 && config.problem.contains("Keep is \"many\""), "what is no number is said: " + config.problem);
        config = QaConfigFile::parse(QaConfigFile::sample(), "C:/Tools/QATest");
        check(config.backupKeep == 14 && config.backupFolder.isEmpty() && config.problem.isEmpty() && QaConfigFile::sample().contains("[Backup]")
              && QaConfigFile::sample().contains("\n;Keep=14"), "the file to start from explains them and sets none");
        config = QaConfigFile::parse(QaConfigFile::withDatabasePath(QaConfigFile::sample(), "\\\\nas1\\MyMedia\\QA-Test\\data\\qatest.sqlite"), "C:/Tools/QATest");
        check(config.databasePath == "//nas1/MyMedia/QA-Test/data/qatest.sqlite" && config.backupKeep == 14 && config.problem.isEmpty(),
              "a database under its share's name, written into that file: " + config.databasePath);
    }

    // ---- the window, with somebody else in the same database ---------------------------------------
    void sharedWindowTests()
    {
        QTemporaryDir folder;
        const QString file = folder.filePath("qatest.sqlite");
        const QString configFile = folder.filePath("config/QATest.ini");
        QString error;
        QaImportCounts counts;
        QaDatabase db, lou;
        db.setUser("pat");
        lou.setUser("lou");
        check(db.open(file, error) && db.importJson(sampleScripts(), counts, error) && lou.open(file, error), "a database for two: " + error);

        MainWindow window(&db);
        window.setDatabaseSource("It is the tests' own.", configFile);
        QTreeWidget *tree = window.tree();
        CasePanel *casePanel = window.casePanel();
        auto *title = casePanel->findChild<QLineEdit *>("caseTitle");
        auto *save = casePanel->findChild<QPushButton *>("caseSave");
        auto *revert = casePanel->findChild<QPushButton *>("caseRevert");
        auto *overwrite = casePanel->findChild<QPushButton *>("caseOverwrite");
        auto *problem = casePanel->findChild<QLabel *>("caseProblem");
        auto *changedBy = casePanel->findChild<QLabel *>("caseChangedBy");
        auto *lostText = window.findChild<QLabel *>("lostText");
        auto *retry = window.findChild<QPushButton *>("lostRetry");
        check(title && save && revert && overwrite && problem && changedBy && lostText && retry, "the pieces for a shared database");
        if (!title || !save || !revert || !overwrite || !problem || !changedBy || !lostText || !retry)
            return;

        // ---- who tests here
        RunPanel *runs = window.runPanel();
        auto *table = runs->findChild<QTableWidget *>("runResults");
        auto *passed = runs->findChild<QPushButton *>("markPassed");
        QList<QaProject> projects;
        db.projects(projects, error);
        QaRun run;
        run.projectId = projects.value(0).id;
        run.name = "Somebody else's run";
        run.tester = "lou";
        check(table && passed && lou.createRun(run, {}, error), "a run somebody else made: " + error);
        window.reload();
        if (table && passed && runs->runId() == run.id && table->rowCount() == 3)
        {
            passed->click();
            check(table->item(0, 4)->text() == QaDatabase::systemUser() && !QaDatabase::systemUser().isEmpty(),
                  "a result says who recorded it - the name they are logged in with - not who made the run: " + table->item(0, 4)->text());
        }
        else
            check(false, "the run somebody else made is shown");

        // ---- somebody else stores the case that is open here
        check(window.selectCase("S-LOGIN-001") && overwrite->isHidden() && changedBy->text().startsWith("Last changed ") && !changedBy->text().contains(" by "),
              "a case that was imported says when it was changed: " + changedBy->text());
        const qint64 id = casePanel->caseId();
        QaCase lous, read;
        check(lou.loadCase(id, lous, error), "Lou has it open too");
        lous.title = "Lou's title";
        check(lou.saveCase(lous, error), "and stores it: " + error);
        title->setText("Pat's title");
        save->click();
        check(!problem->isHidden() && problem->text().startsWith("lou changed this test case at ") && problem->text().contains("Save Mine Anyway stores yours over theirs")
              && !overwrite->isHidden() && revert->isEnabled() && title->text() == "Pat's title" && casePanel->isChanged(),
              "Save says who was first, and what was typed stays: " + problem->text());
        check(lou.loadCase(id, read, error) && read.title == "Lou's title", "nothing of theirs is written over");
        // Going to another case does not lose it either.
        window.selectCase("S-LOGIN-002");
        check(casePanel->caseId() == id && title->text() == "Pat's title" && tree->currentItem() && tree->currentItem()->text(0).contains("S-LOGIN-001") && !overwrite->isHidden(),
              "choosing another case leaves the one that could not be saved, with what was typed");
        overwrite->click();
        check(lou.loadCase(id, read, error) && read.title == "Pat's title" && read.changedBy == "pat" && overwrite->isHidden() && problem->isHidden() && !casePanel->isChanged()
              && changedBy->text().startsWith("Last changed by pat, "), "Save Mine Anyway stores it over theirs: " + changedBy->text());
        // The other way: theirs.
        check(lou.loadCase(id, lous, error), "Lou reads it again");
        lous.title = "Lou's second title";
        lou.saveCase(lous, error);
        title->setText("Pat's second title");
        save->click();
        check(!overwrite->isHidden(), "the same again");
        revert->click();
        check(title->text() == "Lou's second title" && overwrite->isHidden() && problem->isHidden() && !casePanel->isChanged() && changedBy->text().startsWith("Last changed by lou, "),
              "Revert shows their version: " + title->text());
        // With nothing typed, the window simply follows.
        check(lou.loadCase(id, lous, error), "and again");
        lous.title = "Lou's third title";
        lou.saveCase(lous, error);
        window.reload();
        check(title->text() == "Lou's third title" && tree->currentItem() && tree->currentItem()->text(0).endsWith("S-LOGIN-001  Lou's third title"),
              "a case nobody is typing in shows what somebody else stored");

        // ---- the database goes away, and comes back
        check(!window.isLost(), "the database is there");
        title->setText("Typed while the share was gone");
        const int shown = tree->topLevelItem(0)->childCount();
        db.close();
        window.reload();
        check(window.isLost() && lostText->text().contains("No database is open") && lostText->text().contains("What you see and what you typed stays here")
              && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == shown && title->text() == "Typed while the share was gone" && casePanel->isChanged(),
              "a database that cannot be had is said in a line of its own, and what is shown and typed stays: " + lostText->text());
        save->click();
        check(!problem->isHidden() && title->text() == "Typed while the share was gone" && casePanel->isChanged(), "Save says why not, and keeps it: " + problem->text());
        retry->click();
        check(window.isLost(), "it is still away");
        check(db.open(file, error), "the share is back: " + error);
        retry->click();
        check(!window.isLost() && title->text() == "Typed while the share was gone" && casePanel->isChanged(), "Try Again finds it, and does not put anything over what was typed");
        save->click();
        check(db.loadCase(id, read, error) && read.title == "Typed while the share was gone" && !casePanel->isChanged(), "which is then stored: " + error + " " + problem->text());

        // ---- File > Use a Shared Database
        const QString shared = folder.filePath("share/team.sqlite");
        {
            QaDatabase team;
            QaProject project;
            project.name = "Team";
            check(team.open(shared, error) && team.addProject(project, error), "a database on the share: " + error);
        }
        QSettings().setValue("Database", file);
        QString said = window.useSharedDatabase(folder.filePath("share/none.sqlite"), false);
        check(said.startsWith("There is no database ") && db.path() == file && !QFileInfo::exists(configFile) && !QFileInfo::exists(folder.filePath("share/none.sqlite")),
              "one that is not there is not made, and nothing changes: " + said);
        said = window.useSharedDatabase(shared, true);
        check(said.isEmpty() && db.path() == shared && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->text(0) == "Team" && casePanel->caseId() == 0,
              "the shared database is the one in use: " + said);
        const QaConfig config = QaConfigFile::read(configFile);
        check(config.databasePath == shared && config.problem.isEmpty() && config.backupKeep == 14 && !QSettings().contains("Database"),
              "and the configuration file says so from now on: " + config.databasePath + " " + config.problem);
        // Into a file that is there: its other settings stay.
        {
            QFile ini(configFile);
            ini.open(QIODevice::WriteOnly | QIODevice::Truncate);
            ini.write("; ours\r\n[Database]\r\nPath=C:\\old.sqlite\r\nBusyTimeoutSeconds=42\r\n[Backup]\r\nKeep=5\r\n");
        }
        said = window.useSharedDatabase(file, false);
        const QaConfig kept = QaConfigFile::read(configFile);
        check(said.isEmpty() && db.path() == file && kept.databasePath == file && kept.busyTimeoutSeconds == 42 && kept.backupKeep == 5,
              "a file that is there keeps what else it says: " + said);

        // ---- the day's copy
        check(QaBackup::copies(file).isEmpty(), "a window makes no copies unless told how many to keep");
        window.setBackups(2, QString());
        const QStringList copies = QaBackup::copies(file);
        check(copies.size() == 1 && copies.at(0) == folder.filePath("backups/" + QaBackup::fileNameFor(file, QDate::currentDate())), "today's copy is made when the program starts");
        window.setBackups(2, QString());
        check(QaBackup::copies(file).size() == 1, "once");
        check(window.useSharedDatabase(shared, false).isEmpty() && QaBackup::copies(shared).size() == 1, "and of a database that is opened later");
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
    sharedTests();
    sharedWindowTests();
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
