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
//   running        getting through a run faster: whose a case is, a run of
//                  what failed, the build of each component, files that go
//                  with a result - and Run Mode, one case at a time
//   finding        words looked for in cases and their steps, tags, a run of
//                  a tag, several cases moved, deleted and put into a run at
//                  once, a case and a suite cloned - and the tree's filters
//   reporting      the issue a failure was reported as and its address, how
//                  a project stands over its runs - pass rates, open
//                  failures by issue, what keeps failing, what never ran -
//                  a run as CSV, and the Dashboard tab
//   the scripts'   what a file of test scripts would change, shown before it
//     version      is read - new, changed, not in the file - its version
//                  against the database's, and the line that says the program
//                  brought newer scripts than the database has
//   the window     the program's own window and panels, offscreen, on a
//                  database in memory: the tree, a case edited and saved, a
//                  run made and worked through, its report
#include "casepanel.h"
#include "dashboard.h"
#include "importpreview.h"
#include "mainwindow.h"
#include "qabackup.h"
#include "qaconfig.h"
#include "qashare.h"
#include "qadatabase.h"
#include "report.h"
#include "runmode.h"
#include "runpanel.h"

#include <QAction>
#include <QApplication>
#include <functional>
#include <QCheckBox>
#include <QMessageBox>
#include <QClipboard>
#include <QImage>
#include <QItemSelectionModel>
#include <QListWidget>
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
#include <QTabWidget>
#include <QTextBrowser>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

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
        check(db.importJson(changed, counts, error) && counts.projects == 0 && counts.suites == 0 && counts.casesAdded == 1 && counts.casesUpdated == 1
              && counts.casesUnchanged == 2 && counts.text() == "1 test case was added; 1 test case was updated.",
              "imported again: what is as the file says is left alone: " + error + counts.text());
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
            QList<QaProject> ofFile;
            db.projects(ofFile, error);
            check(ofFile.size() == 1 && !ofFile.at(0).scriptsVersion.isEmpty(), name + " says its version (\"version\"): raise it whenever the file changes");
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
        // ... those that are not as the file says: a case that is, is left alone, and whoever has it open is not disturbed.
        check(pat.loadCase(second, pats, error) && lou.importJson(sampleScripts(), counts, error) && counts.casesUpdated == 1 && counts.casesUnchanged == 2,
              "scripts are imported while a case is open: " + counts.text());
        pats.notes = "Mine";
        check(pat.saveCase(pats, error) && !pat.saveConflicted(), "a case the import left alone is stored as ever: " + error);
        QJsonObject newer = sampleScripts();
        {
            QJsonArray suitesJson = newer.value("suites").toArray();
            QJsonObject loginJson = suitesJson.at(0).toObject();
            QJsonArray casesJson = loginJson.value("cases").toArray();
            QJsonObject wrong = casesJson.at(1).toObject();
            wrong.insert("title", "A wrong password, twice");
            casesJson.replace(1, wrong);
            loginJson.insert("cases", casesJson);
            suitesJson.replace(0, loginJson);
            newer.insert("suites", suitesJson);
        }
        check(pat.loadCase(second, pats, error) && lou.importJson(newer, counts, error) && counts.casesUpdated == 1, "newer scripts are imported while it is open again");
        pats.notes = "Mine again";
        check(!pat.saveCase(pats, error) && pat.saveConflicted() && error.startsWith("Somebody else changed this test case at "), "an import that changes it is a change by nobody in particular: " + error);
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
        check(pat.open(file, error) && lou.open(file, error) && pat.loadCase(second, read, error) && read.revision == 1 && read.changedBy.isEmpty() && read.title == "A wrong password, twice",
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

    // ---- getting through a run faster ---------------------------------------------------------------
    void fastTests()
    {
        QTemporaryDir folder;
        const QString file = folder.filePath("qa/qatest.sqlite");
        QString error;
        QaImportCounts counts;
        QaDatabase db;
        db.setUser("pat");
        check(db.open(file, error), "a database in a file: " + error);

        // ---- what a project is made of
        check(QaDatabase::componentList(" Server, Desktop app,, server ,Phone app ") == QStringList({ "Server", "Desktop app", "Phone app" }) && QaDatabase::componentList("").isEmpty(),
              "what a project is made of, as a list - each once");
        QJsonObject scripts = sampleScripts();
        scripts.insert("components", QJsonArray { "Server", "Desktop app", "Phone app" });
        QList<QaProject> projects;
        check(db.importJson(scripts, counts, error) && db.projects(projects, error) && projects.size() == 1 && projects.at(0).components == "Server, Desktop app, Phone app",
              "test scripts say what their project is made of: " + projects.value(0).components);
        const qint64 projectId = projects.value(0).id;
        QJsonObject written;
        check(db.exportJson(projectId, written, error) && written.value("components").toArray().size() == 3 && written.value("components").toArray().at(1).toString() == "Desktop app",
              "and are written back with it");
        check(db.importJson(sampleScripts(), counts, error) && db.projects(projects, error) && projects.at(0).components == "Server, Desktop app, Phone app",
              "a file that says nothing of it leaves it");
        QaProject changed = projects.at(0);
        changed.components = "Server,  App ";
        check(db.updateProject(changed, error) && db.projects(projects, error) && projects.at(0).components == "Server, App", "it is changed with the project: " + error);

        // ---- a run says the build of each
        QList<QaSuite> suites;
        QList<QaCase> login, parts;
        db.suites(projectId, suites, error);
        db.cases(suites.value(0).id, login, error);
        db.cases(suites.value(1).id, parts, error);
        check(db.lastBuilds(projectId).isEmpty(), "no run has said its builds yet");
        QaRun run;
        run.projectId = projectId;
        run.name = "Beta 3";
        run.build = "0.2.0 Beta";
        run.tester = "pat";
        run.builds = "Server: 0.2.0 Beta, built 2026-10-08 07:50\nApp: 0.2.0 Beta, built 2026-10-09 06:12\n";
        QList<QaRun> runs;
        check(db.createRun(run, {}, error) && db.runs(projectId, runs, error) && runs.size() == 1
              && runs.at(0).builds == "Server: 0.2.0 Beta, built 2026-10-08 07:50\nApp: 0.2.0 Beta, built 2026-10-09 06:12" && runs.at(0).build == "0.2.0 Beta",
              "a run says the build of each component: " + runs.value(0).builds);
        check(db.lastBuilds(projectId) == runs.at(0).builds, "which the next run starts from");
        run.notes = "A note";
        check(db.updateRun(run, error) && db.runs(projectId, runs, error) && runs.at(0).builds.startsWith("Server: 0.2.0 Beta") && runs.at(0).notes == "A note",
              "and keeps when the run is changed");

        // ---- whose a case is
        QList<QaResult> results;
        check(db.results(run.id, results, error) && results.size() == 3 && results.at(0).assigned.isEmpty() && results.at(0).attachments == 0, "a run's cases are nobody's at first");
        check(db.assign(run.id, { login.at(0).id, login.at(1).id }, " lou ", error) && db.results(run.id, results, error) && results.at(0).assigned == "lou"
              && results.at(1).assigned == "lou" && results.at(2).assigned.isEmpty(), "two cases are Lou's: " + error);
        check(!db.assign(run.id, { parts.at(0).id, 999999 }, "pat", error) && error == "That test case is not part of the run." && db.results(run.id, results, error)
              && results.at(2).assigned.isEmpty(), "all of them or none: " + error);
        check(db.assign(run.id, { login.at(1).id }, "", error) && db.results(run.id, results, error) && results.at(1).assigned.isEmpty() && results.at(0).assigned == "lou",
              "nobody's again");
        check(db.assign(run.id, { login.at(1).id }, "lou", error), "and Lou's once more");
        check(db.setResult(run.id, login.at(0).id, "Passed", "", 0, "Lou", error) && db.testers(run.id) == QStringList({ "lou", "pat" }),
              "everybody a run knows, each once: " + db.testers(run.id).join(","));
        check(db.results(run.id, results, error) && results.at(0).assigned == "lou" && results.at(0).tester == "Lou", "a result leaves whose the case is");

        // ---- a run of what failed
        QaRun again;
        again.name = " ";
        check(!db.createRerun(again, run.id, { "Failed", "Blocked" }, error) && error == "A test run needs a name.", "a run needs a name");
        again.name = "Beta 3 - failed again";
        check(!db.createRerun(again, run.id, { "Failed", "Blocked" }, error) && error.startsWith("No test case of that run ended that way") && db.runs(projectId, runs, error)
              && runs.size() == 1, "nothing failed: no run is made: " + error);
        check(db.setResult(run.id, login.at(1).id, "Failed", "Let in", 1, "lou", error) && db.setResult(run.id, parts.at(0).id, "Blocked", "No server", 0, "pat", error),
              "one fails, one is blocked");
        check(db.createRerun(again, run.id, { "Failed", "Blocked" }, error) && again.id > 0 && again.projectId == projectId && db.results(again.id, results, error)
              && results.size() == 2 && results.at(0).caseKey == "S-LOGIN-002" && results.at(0).status == "Not run" && results.at(0).notes.isEmpty()
              && results.at(0).assigned == "lou" && results.at(1).caseKey == "S-PARTS-001" && results.at(1).assigned.isEmpty(),
              "a run of what failed and was blocked: each not run, for whom it was: " + error);
        check(db.runs(projectId, runs, error) && runs.size() == 2 && again.builds.startsWith("Server: 0.2.0 Beta") && again.build == "0.2.0 Beta",
              "of the same builds unless it says its own");
        QList<QaResult> before;
        check(db.results(run.id, before, error) && before.at(1).status == "Failed" && before.at(1).notes == "Let in", "the run it was made from stays as it is");
        QaRun other;
        other.name = "Only what failed, of a new build";
        other.build = "0.2.1";
        check(db.createRerun(other, run.id, { "Failed" }, error) && db.results(other.id, results, error) && results.size() == 1 && other.build == "0.2.1" && other.builds.isEmpty(),
              "only what failed, of another build: " + error);
        check(!db.createRerun(other, 999999, { "Failed" }, error) && error == "That test run is not there any more.", "a run that is not there: " + error);

        // ---- files that go with a result
        const QString source = folder.filePath("what went wrong.log");
        {
            QFile log(source);
            log.open(QIODevice::WriteOnly);
            log.write("line one\nline two\n");
        }
        check(db.attachmentsFolder() == folder.filePath("qa/attachments"), "files are kept beside the database: " + db.attachmentsFolder());
        QaAttachment first, second;
        check(db.attach(run.id, login.at(1).id, source, "", first, error) && first.id > 0 && first.name == "what went wrong.log" && first.addedBy == "pat"
              && first.file.startsWith(QStringLiteral("%1/%2-").arg(run.id).arg(login.at(1).id)) && first.file.endsWith("-what-went-wrong.log")
              && QFile(db.attachmentPath(first)).size() == 18 && db.attachmentPath(first).startsWith(folder.filePath("qa/attachments/")),
              "a copy of a file goes with a result: " + error + " " + first.file);
        check(db.attach(run.id, login.at(1).id, source, "screenshot 1.png", second, error) && second.name == "screenshot 1.png" && second.file != first.file
              && QFileInfo::exists(db.attachmentPath(second)), "the same file twice is two copies, called as said: " + error);
        QList<QaAttachment> files;
        check(db.attachments(run.id, login.at(1).id, files, error) && files.size() == 2 && files.at(0).id == first.id && files.at(1).name == "screenshot 1.png"
              && db.attachments(run.id, login.at(0).id, files, error) && files.isEmpty(), "a result's files, the oldest first");
        check(db.results(run.id, results, error) && results.at(1).attachments == 2 && results.at(0).attachments == 0, "a result says how many it has");
        QaAttachment none;
        check(!db.attach(run.id, 999999, source, "", none, error) && error == "That test case is not part of the run."
              && !db.attach(run.id, login.at(1).id, folder.filePath("no such file.log"), "", none, error) && error.startsWith("There is no file "),
              "not to a case that is not in the run, and not a file that is not there: " + error);
        const QString firstPath = db.attachmentPath(first);
        check(db.removeAttachment(first.id, error) && !QFileInfo::exists(firstPath) && QFileInfo::exists(db.attachmentPath(second)) && db.attachments(run.id, login.at(1).id, files, error)
              && files.size() == 1, "a file is taken away, and its copy with it: " + error);
        // What a file belongs to takes it along.
        QaAttachment onParts;
        check(db.attach(run.id, parts.at(0).id, source, "", onParts, error) && db.attach(again.id, parts.at(0).id, source, "", none, error), "files on another case, in two runs");
        const QString onPartsPath = db.attachmentPath(onParts);
        const QString inAgainPath = db.attachmentPath(none);
        check(db.deleteCase(parts.at(0).id, error) && !QFileInfo::exists(onPartsPath) && !QFileInfo::exists(inAgainPath) && QFileInfo::exists(db.attachmentPath(second)),
              "a case that is deleted takes its files in every run: " + error);
        const QString secondPath = db.attachmentPath(second);
        check(db.deleteRun(run.id, error) && !QFileInfo::exists(secondPath) && !QDir(folder.filePath(QStringLiteral("qa/attachments/%1").arg(run.id))).exists()
              && QFileInfo::exists(source), "a run that is deleted takes its files and its folder - never the file they were copied from: " + error);
        QaDatabase memory;
        QaRun inMemory;
        inMemory.name = "In memory";
        check(memory.open(":memory:", error) && memory.importJson(sampleScripts(), counts, error) && memory.projects(projects, error), "a database in memory");
        inMemory.projectId = projects.value(0).id;
        check(memory.createRun(inMemory, {}, error) && memory.results(inMemory.id, results, error) && memory.attachmentsFolder().isEmpty()
              && !memory.attach(inMemory.id, results.at(0).caseId, source, "", none, error) && error.startsWith("Files are kept beside the database"),
              "has nowhere to keep files, and says so: " + error);

        // ---- a database from before all this
        db.close();
        bool dropped = false;
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "raw2");
            raw.setDatabaseName(file);
            if (raw.open())
            {
                QSqlQuery query(raw);
                dropped = query.exec("DROP INDEX attachments_of_result") && query.exec("DROP TABLE attachments") && query.exec("ALTER TABLE results DROP COLUMN assigned")
                          && query.exec("ALTER TABLE runs DROP COLUMN builds") && query.exec("ALTER TABLE projects DROP COLUMN components");
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase("raw2");
        check(dropped && db.open(file, error) && db.projects(projects, error) && projects.size() == 1 && projects.at(0).components.isEmpty() && db.runs(projects.at(0).id, runs, error)
              && runs.size() == 2 && runs.at(0).builds.isEmpty() && db.results(runs.at(0).id, results, error) && !results.isEmpty() && results.at(0).assigned.isEmpty()
              && results.at(0).attachments == 0, "a database from an older version is brought up to date and keeps its runs: " + error);
    }

    void fastWindowTests()
    {
        QTemporaryDir folder;
        const QString file = folder.filePath("qatest.sqlite");
        QString error;
        QaImportCounts counts;
        QaDatabase db;
        db.setUser("pat");
        QJsonObject scripts = sampleScripts();
        scripts.insert("components", QJsonArray { "Server", "Desktop app", "Phone app" });
        check(db.open(file, error) && db.importJson(scripts, counts, error), "a database for running: " + error);

        MainWindow window(&db);
        RunPanel *runs = window.runPanel();
        auto *table = runs->findChild<QTableWidget *>("runResults");
        auto *filter = runs->findChild<QComboBox *>("runFilter");
        auto *whose = runs->findChild<QComboBox *>("runWhose");
        auto *builds = runs->findChild<QLabel *>("runBuilds");
        auto *rerun = runs->findChild<QPushButton *>("runRerun");
        auto *modeButton = runs->findChild<QPushButton *>("runMode");
        auto *assign = runs->findChild<QPushButton *>("runAssign");
        auto *filesLabel = runs->findChild<QLabel *>("runFiles");
        auto *summary = runs->findChild<QLabel *>("runSummary");
        auto *passed = runs->findChild<QPushButton *>("markPassed");
        auto *newRun = runs->findChild<QPushButton *>("newRun");
        check(table && filter && whose && builds && rerun && modeButton && assign && filesLabel && summary && passed && newRun, "the run panel's pieces for running faster");
        if (!table || !filter || !whose || !builds || !rerun || !modeButton || !assign || !filesLabel || !summary || !passed || !newRun)
            return;
        check(!modeButton->isEnabled() && !assign->isEnabled() && !rerun->isEnabled() && builds->isHidden(), "nothing to run while there is no run");

        // ---- a run that says the build of each component
        check(runs->createRun("Beta 3", "0.2.0", "pat", {}, error, "Server: 0.2.0, built 07:50\nPhone app: 0.2.0, built 06:12") && runs->tester() == "pat", "a run is made: " + error);
        check(!builds->isHidden() && builds->text().startsWith("Tested: Server: 0.2.0, built 07:50") && builds->text().endsWith("Phone app: 0.2.0, built 06:12"),
              "the panel says what is tested: " + builds->text());
        check(table->columnCount() == 10 && table->horizontalHeaderItem(7)->text() == "For" && table->horizontalHeaderItem(8)->text() == "Files" && table->rowCount() == 3
              && modeButton->isEnabled() && assign->isEnabled() && !rerun->isEnabled(), "its cases, with whose they are and their files");

        // ---- whose a case is
        table->clearSelection();
        check(!runs->assignSelected("lou", error) && error == "Select the test cases first.", "nothing selected, nothing assigned");
        table->selectRow(0);
        table->selectionModel()->select(table->model()->index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        check(runs->assignSelected("lou", error) && table->item(0, 7)->text() == "lou" && table->item(1, 7)->text() == "lou" && table->item(2, 7)->text().isEmpty(),
              "two selected cases are Lou's: " + error);
        table->selectRow(2);
        check(runs->assignSelected("PAT", error) && table->item(2, 7)->text() == "PAT", "one is Pat's: " + error);
        whose->setCurrentIndex(1);
        check(table->rowCount() == 1 && table->item(0, 1)->text() == "S-PARTS-001", "My cases shows a tester their own, however the name is written");
        whose->setCurrentIndex(2);
        check(table->rowCount() == 0 && !modeButton->isEnabled(), "Nobody's cases: none here");
        whose->setCurrentIndex(0);
        check(table->rowCount() == 3, "and everybody's again");

        // ---- Run Mode
        table->selectRow(0);
        RunMode *mode = runs->openRunMode();
        check(mode && mode->count() == 3 && mode->position() == 1, "Run Mode on the cases that are shown, at the selected one");
        if (!mode)
            return;
        auto *progress = mode->findChild<QLabel *>("modeProgress");
        auto *title = mode->findChild<QLabel *>("modeTitle");
        auto *beforeLabel = mode->findChild<QLabel *>("modeBefore");
        auto *steps = mode->findChild<QTableWidget *>("modeSteps");
        auto *notes = mode->findChild<QPlainTextEdit *>("modeNotes");
        auto *problem = mode->findChild<QLabel *>("modeProblem");
        auto *fileList = mode->findChild<QListWidget *>("modeFiles");
        check(progress && title && beforeLabel && steps && notes && problem && fileList, "its pieces");
        if (!progress || !title || !beforeLabel || !steps || !notes || !problem || !fileList)
            return;
        check(progress->text().startsWith("Case 1 of 3") && progress->text().contains("3 not run") && progress->text().contains("Login") && progress->text().endsWith("for lou"),
              "where it is: " + progress->text());
        check(title->text() == "S-LOGIN-001  Log in" && steps->rowCount() == 2 && steps->item(1, 2)->text() == "The main window opens"
              && beforeLabel->text().contains("Tested: Server: 0.2.0, built 07:50; Phone app: 0.2.0, built 06:12") && beforeLabel->text().contains("Run on: Desktop")
              && beforeLabel->text().contains("Before you start: A user exists") && title->font().pointSizeF() > window.font().pointSizeF() * 1.4,
              "the case, large, with what is tested and what has to be there: " + beforeLabel->text());
        mode->mark("Passed");
        check(mode->position() == 2 && title->text() == "S-LOGIN-002  A wrong password" && table->item(0, 3)->text().endsWith("Passed") && table->item(0, 4)->text() == "pat"
              && progress->text().contains("2 not run"), "P: stored, by whoever is testing, and on to the next: " + progress->text());
        mode->mark("Failed");
        check(mode->position() == 2 && !problem->isHidden() && problem->text().startsWith("Say in the notes what happened") && table->item(1, 3)->text() == "Not run",
              "F wants a note first");
        notes->setPlainText("It was let in");
        mode->mark("Failed");
        check(mode->position() == 3 && table->item(1, 3)->text().endsWith("Failed") && table->item(1, 6)->text() == "It was let in" && notes->toPlainText().isEmpty(),
              "then it is stored, and the next case starts with empty notes");

        // Files that go with the result.
        const QString log = folder.filePath("server.log");
        {
            QFile written(log);
            written.open(QIODevice::WriteOnly);
            written.write("what the server said");
        }
        check(mode->attachFile(log) && fileList->count() == 1 && fileList->item(0)->text() == "server.log" && table->item(2, 8)->text() == "1",
              "a file goes with the result, and the panel counts it");
        check(!mode->attachFile(folder.filePath("none.log")) && !problem->isHidden() && problem->text().startsWith("There is no file ") && fileList->count() == 1,
              "one that is not there is said: " + problem->text());
        QImage picture(40, 30, QImage::Format_RGB32);
        picture.fill(Qt::white);
        QGuiApplication::clipboard()->setImage(picture);
        if (!QGuiApplication::clipboard()->image().isNull())
        {
            check(mode->pasteScreenshot() && fileList->count() == 2 && fileList->item(1)->text().startsWith("screenshot-") && fileList->item(1)->text().endsWith(".png")
                  && table->item(2, 8)->text() == "2", "a picture on the clipboard is pasted as a screenshot");
            QList<QaAttachment> files;
            db.attachments(runs->runId(), mode->caseId(), files, error);
            const QImage read(db.attachmentPath(files.value(1)));
            check(files.size() == 2 && read.width() == 40 && read.height() == 30, "which is that picture, as a PNG beside the database");
            QGuiApplication::clipboard()->setText("only words");
            check(!mode->pasteScreenshot() && problem->text().startsWith("There is no picture on the clipboard") && fileList->count() == 2, "words are no picture: " + problem->text());
            check(mode->removeAttachment(1) && fileList->count() == 1 && !QFileInfo::exists(db.attachmentPath(files.value(1))), "a file is removed, with its copy");
        }
        mode->mark("Blocked");
        check(mode->position() == 3 && !problem->isHidden() && problem->text().startsWith("That was the last one: every case here has a result")
              && progress->text().contains("all have a result") && progress->text().contains("now: Blocked (pat)") && fileList->count() == 1,
              "the last one says so, and stays: " + progress->text());
        mode->go(1);
        check(mode->position() == 3 && problem->text() == "This is the last case.", "no further");
        mode->go(-1);
        check(mode->position() == 2 && notes->toPlainText() == "It was let in" && problem->isHidden() && fileList->count() == 0, "back to the case before, with what was noted");
        mode->mark("Not run");
        check(mode->position() == 2 && table->item(1, 3)->text() == "Not run" && progress->text().contains("1 not run"), "U takes a result back and stays");
        mode->mark("Failed");
        check(table->item(1, 3)->text().endsWith("Failed") && problem->text().startsWith("That was the last one"), "and it is failed again");
        mode->go(-1);
        mode->go(-1);
        check(mode->position() == 1 && problem->text() == "This is the first case.", "nor before the first");
        mode->accept();

        // The panel shows the files of the selected case.
        table->selectRow(2);
        check(!filesLabel->isHidden() && filesLabel->text().startsWith("Files: server.log"), "the panel names a result's files: " + filesLabel->text());
        table->selectRow(0);
        check(filesLabel->isHidden(), "and nothing where there are none");
        // Run Mode is for what is shown.
        filter->setCurrentIndex(filter->findText("Failed"));
        mode = runs->openRunMode();
        check(mode && mode->count() == 1 && mode->position() == 1 && mode->findChild<QLabel *>("modeTitle")->text().startsWith("S-LOGIN-002"), "Run Mode on the failures alone");
        if (mode)
            mode->accept();
        filter->setCurrentIndex(0);

        // ---- the report
        const QString html = runs->reportHtml();
        check(html.contains("<th width=\"22%\">Server</th><td>0.2.0, built 07:50</td>") && html.contains("<th width=\"22%\">Phone app</th>") && html.contains("[1 file attached]"),
              "the report says the build of each component, and where files were attached");

        // ---- a run of what failed
        const qint64 firstRun = runs->runId();
        check(rerun->isEnabled() && !runs->createRerun(" ", error) && runs->runId() == firstRun, "Run Failed Again wants a name");
        check(runs->createRerun("Beta 3 - failed again", error) && runs->runId() != firstRun && table->rowCount() == 2 && table->item(0, 1)->text() == "S-LOGIN-002"
              && table->item(0, 3)->text() == "Not run" && table->item(0, 7)->text() == "lou" && table->item(1, 1)->text() == "S-PARTS-001" && table->item(1, 7)->text() == "PAT"
              && table->item(1, 8)->text().isEmpty() && summary->text().startsWith("2 test cases: 0 passed") && !rerun->isEnabled()
              && builds->text().startsWith("Tested: Server: 0.2.0, built 07:50"), "a new run of what failed and was blocked: " + error + " " + summary->text());
        // Run Mode begins at the first case that is not run.
        table->selectRow(0);
        passed->click();
        table->selectRow(0);
        mode = runs->openRunMode();
        check(mode && mode->position() == 2, "Run Mode begins where there is something to do");
        if (mode)
            mode->accept();

        // ---- New Run... starts from the builds of the run before
        newRun->click();
        auto *dialog = runs->findChild<QDialog *>("newRunDialog");
        auto *server = dialog ? dialog->findChild<QLineEdit *>("runBuildOfServer") : nullptr;
        auto *desktop = dialog ? dialog->findChild<QLineEdit *>("runBuildOfDesktopapp") : nullptr;
        auto *phone = dialog ? dialog->findChild<QLineEdit *>("runBuildOfPhoneapp") : nullptr;
        check(server && desktop && phone && server->text() == "0.2.0, built 07:50" && desktop->text().isEmpty() && phone->text() == "0.2.0, built 06:12",
              "New Run asks for the build of each component, filled in as the run before said");
        if (dialog)
            dialog->reject();

        // ---- a finished run is not worked through
        QList<QaRun> all;
        db.runs(runs->projectId(), all, error);
        for (QaRun run : std::as_const(all))
        {
            if (run.id != runs->runId())
                continue;
            run.finished = "2026-10-09T10:00:00Z";
            db.updateRun(run, error);
        }
        window.reload();
        check(!modeButton->isEnabled() && runs->openRunMode() == nullptr, "a finished run has no Run Mode");
    }

    // ---- finding and organising ----------------------------------------------------------------------
    void findTests()
    {
        QTemporaryDir folder;
        const QString file = folder.filePath("qatest.sqlite");
        QString error;
        QaImportCounts counts;
        QaDatabase db;
        db.setUser("pat");
        check(db.open(file, error) && db.importJson(sampleScripts(), counts, error), "a database to find things in: " + error);
        QList<QaProject> projects;
        QList<QaSuite> suites;
        QList<QaCase> login, parts;
        db.projects(projects, error);
        const qint64 projectId = projects.value(0).id;
        db.suites(projectId, suites, error);
        const qint64 loginId = suites.value(0).id, partsId = suites.value(1).id;
        db.cases(loginId, login, error);
        db.cases(partsId, parts, error);

        // ---- tags
        check(QaDatabase::tagList(" Smoke, phone,, SMOKE , two  words ") == QStringList({ "Smoke", "phone", "two words" }) && QaDatabase::tagList(" , ").isEmpty(),
              "tags as a list: each once, whatever the capitals");
        check(db.tags(projectId).isEmpty() && login.at(0).tags.isEmpty(), "no case has a tag at first");
        QaCase first, read;
        check(db.loadCase(login.at(0).id, first, error), "a case");
        first.tags = " smoke,Desktop , smoke";
        check(db.saveCase(first, error) && first.tags == "smoke, Desktop" && db.loadCase(first.id, read, error) && read.tags == "smoke, Desktop"
              && db.cases(loginId, login, error) && login.at(0).tags == "smoke, Desktop", "a case's tags are stored tidied: " + read.tags);
        QaCase third;
        check(db.loadCase(parts.at(0).id, third, error), "another");
        third.tags = "Smoke";
        check(db.saveCase(third, error) && db.tags(projectId) == QStringList({ "Desktop", "smoke" }), "every tag of a project, each once: " + db.tags(projectId).join(","));
        // In a file of test scripts.
        QJsonObject written;
        check(db.exportJson(projectId, written, error)
              && written.value("suites").toArray().at(0).toObject().value("cases").toArray().at(0).toObject().value("tags").toArray().size() == 2
              && !written.value("suites").toArray().at(0).toObject().value("cases").toArray().at(1).toObject().contains("tags"), "tags are written with the scripts");
        check(db.importJson(sampleScripts(), counts, error) && db.loadCase(first.id, read, error) && read.tags == "smoke, Desktop", "a file that says no tags leaves a case's");
        QaDatabase other;
        QList<QaProject> otherProjects;
        QList<QaSuite> otherSuites;
        QList<QaCase> otherCases;
        check(other.open(":memory:", error) && other.importJson(written, counts, error) && other.projects(otherProjects, error) && other.suites(otherProjects.at(0).id, otherSuites, error)
              && other.cases(otherSuites.at(0).id, otherCases, error) && otherCases.at(0).tags == "smoke, Desktop" && otherCases.at(1).tags.isEmpty(), "and read back with them");

        // ---- words
        QList<qint64> found;
        check(db.search(projectId, "", found, error) && found.size() == 3, "no words: every case");
        check(db.search(projectId, "PASSWORD", found, error) && found.size() == 2, "a word of a title or of a step, whatever the capitals: " + QString::number(found.size()));
        check(db.search(projectId, "wrong refused", found, error) && found.size() == 1 && found.at(0) == login.at(1).id, "every word has to be there - in the title, in a step");
        check(db.search(projectId, "desktop smoke", found, error) && found.size() == 1 && found.at(0) == first.id, "a tag is found too");
        check(db.search(projectId, "S-PARTS", found, error) && found.size() == 1 && db.search(projectId, "user exists", found, error) && found.size() == 1,
              "and a key, and what has to be there first");
        check(db.search(projectId, "zebra", found, error) && found.isEmpty(), "what is nowhere finds nothing");
        QaCase percent;
        percent.suiteId = partsId;
        percent.key = "S-PARTS-050";
        percent.title = "Count 100% of the stock";
        percent.steps << QaStep { "Open part_list", "It opens" };
        QaCase plain;
        plain.suiteId = partsId;
        plain.key = "S-PARTS-051";
        plain.title = "Count 100 items";
        plain.steps << QaStep { "Open partXlist", "It opens" };
        check(db.saveCase(percent, error) && db.saveCase(plain, error) && db.search(projectId, "100%", found, error) && found.size() == 1 && found.at(0) == percent.id
              && db.search(projectId, "part_list", found, error) && found.size() == 1 && found.at(0) == percent.id, "% and _ mean themselves: " + QString::number(found.size()));
        QaProject second;
        second.name = "Second";
        QaSuite secondSuite;
        secondSuite.name = "Only";
        QaCase secondCase;
        secondCase.key = "X-001";
        secondCase.title = "A wrong password, elsewhere";
        check(db.addProject(second, error), "a second project");
        secondSuite.projectId = second.id;
        check(db.addSuite(secondSuite, error), "with a suite");
        secondCase.suiteId = secondSuite.id;
        check(db.saveCase(secondCase, error) && db.search(projectId, "wrong", found, error) && found.size() == 1 && db.search(0, "wrong", found, error) && found.size() == 2,
              "in one project, or in all of them");

        // ---- a run of a tag
        QaRun smoke;
        smoke.projectId = projectId;
        smoke.name = "Smoke";
        QList<QaResult> results;
        check(db.createRun(smoke, {}, error, "SMOKE") && db.results(smoke.id, results, error) && results.size() == 2 && results.at(0).caseKey == "S-LOGIN-001"
              && results.at(1).caseKey == "S-PARTS-001", "a run of the cases that have a tag, whatever its capitals: " + error);
        QaRun smokeLogin;
        smokeLogin.projectId = projectId;
        smokeLogin.name = "Smoke, login";
        check(db.createRun(smokeLogin, { loginId }, error, "smoke") && db.results(smokeLogin.id, results, error) && results.size() == 1 && results.at(0).caseKey == "S-LOGIN-001",
              "of some suites alone: " + error);
        QaRun none;
        none.projectId = projectId;
        none.name = "None";
        QList<QaRun> runs;
        check(!db.createRun(none, {}, error, "license") && error.startsWith("No test case of those suites has the tag \"license\"") && db.runs(projectId, runs, error) && runs.size() == 2,
              "a tag no case has makes no run: " + error);
        check(!db.createRun(none, { partsId }, error, "Desktop") && db.runs(projectId, runs, error) && runs.size() == 2, "nor one that only other suites have");

        // ---- cases join a run
        int added = -1;
        check(db.addToRun(smokeLogin.id, { login.at(0).id, login.at(1).id, percent.id, secondCase.id }, added, error) && added == 2 && db.results(smokeLogin.id, results, error)
              && results.size() == 3, "cases join a run: not what is in it, not another project's: " + error + " " + QString::number(added));
        check(db.results(smokeLogin.id, results, error) && results.at(1).status == "Not run", "each not run");
        smokeLogin.finished = "2026-10-09T10:00:00Z";
        check(db.updateRun(smokeLogin, error) && !db.addToRun(smokeLogin.id, { plain.id }, added, error) && error.startsWith("That test run is finished") && added == 0,
              "not a run that is finished: " + error);
        check(!db.addToRun(999999, { plain.id }, added, error) && error == "That test run is not there any more.", "nor one that is not there");

        // ---- several are moved
        check(db.loadCase(login.at(1).id, read, error), "a case, before it is moved");
        const int revision = read.revision;
        check(db.moveCases({ login.at(0).id, login.at(1).id }, partsId, error) && db.cases(loginId, login, error) && login.isEmpty() && db.cases(partsId, parts, error) && parts.size() == 5
              && db.loadCase(read.id, read, error) && read.suiteId == partsId && read.revision == revision + 1 && read.key == "S-LOGIN-002" && read.steps.size() == 1,
              "several cases go into another suite, with their keys and steps: " + error);
        check(db.results(smoke.id, results, error) && results.size() == 2 && results.at(0).suiteName == "Parts", "and stay in their runs");
        check(!db.moveCases({ parts.at(0).id, secondCase.id }, loginId, error) && error.startsWith("X-001 belongs to another project") && db.cases(loginId, login, error) && login.isEmpty(),
              "all of them or none - a case stays in its project: " + error);
        check(!db.moveCases({ parts.at(0).id }, 999999, error) && error == "That suite is not there any more.", "not into a suite that is not there");
        check(db.moveCases({ parts.at(0).id, parts.at(1).id }, loginId, error) && db.cases(loginId, login, error) && login.size() == 2 && db.cases(partsId, parts, error), "and back: " + error);

        // ---- a clone
        QaCase copy;
        check(db.cloneCase(login.at(0).id, copy, error) && copy.id != login.at(0).id && copy.key == "S-LOGIN-003" && copy.title == "Log in (copy)" && copy.suiteId == loginId
              && copy.tags == "smoke, Desktop" && copy.steps.size() == 2 && copy.revision == 1, "a case once more: the next key, its steps and tags: " + error + " " + copy.key);
        QList<QaResult> history;
        QStringList names;
        check(db.history(copy.id, history, names, error) && history.isEmpty() && db.history(login.at(0).id, history, names, error) && history.size() == 2
              && db.results(smoke.id, results, error) && results.size() == 2, "not how the first went, and in no run");
        check(!db.cloneCase(999999, copy, error) && error == "That test case is not there any more.", "what is not there is not cloned");
        QaSuite suiteCopy, suiteCopy2;
        QList<QaCase> cloned;
        check(db.cloneSuite(partsId, suiteCopy, error) && suiteCopy.name == "Parts (copy)" && suiteCopy.projectId == projectId && suiteCopy.caseCount == 3
              && db.cases(suiteCopy.id, cloned, error) && cloned.size() == 3 && cloned.at(0).key == "S-PARTS-002" && cloned.at(0).title == "Add a part" && cloned.at(0).tags == "Smoke"
              && cloned.at(2).key == "S-PARTS-004", "a suite once more, with a clone of each case under a key of its own: " + error + " " + cloned.value(0).key);
        check(db.loadCase(cloned.at(1).id, read, error) && read.steps.size() == 1 && read.steps.at(0).action == "Open part_list" && db.cases(partsId, parts, error) && parts.size() == 3,
              "the clones have their steps, and the suite its cases as before");
        check(db.cloneSuite(partsId, suiteCopy2, error) && suiteCopy2.name == "Parts (copy 2)" && db.suites(projectId, suites, error) && suites.size() == 4,
              "a second clone has a name of its own: " + suiteCopy2.name);
        check(!db.cloneSuite(999999, suiteCopy2, error) && error == "That suite is not there any more.", "nor a suite that is not there");

        // ---- several are deleted
        const QString source = folder.filePath("a.log");
        {
            QFile log(source);
            log.open(QIODevice::WriteOnly);
            log.write("x");
        }
        QaAttachment attachment;
        check(db.attach(smoke.id, parts.at(0).id, source, "", attachment, error), "a file on a result of a case that is to go: " + error);
        const QString attached = db.attachmentPath(attachment);
        check(db.deleteSeveral({ parts.at(0).id, copy.id }, { suiteCopy.id }, { second.id }, error) && !db.loadCase(parts.at(0).id, read, error) && !db.loadCase(copy.id, read, error)
              && db.suites(projectId, suites, error) && suites.size() == 3 && db.projects(projects, error) && projects.size() == 1 && !QFileInfo::exists(attached),
              "cases, a suite and a project are deleted together, with the files of their results: " + error);
        check(db.results(smoke.id, results, error) && results.size() == 1 && db.deleteSeveral({}, {}, {}, error), "what is left is left; nothing to delete is no error");

        // ---- a database from before the tags
        db.close();
        bool dropped = false;
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "raw3");
            raw.setDatabaseName(file);
            if (raw.open())
            {
                QSqlQuery query(raw);
                dropped = query.exec("ALTER TABLE cases DROP COLUMN tags");
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase("raw3");
        check(dropped && db.open(file, error) && db.cases(loginId, login, error) && login.size() == 2 && login.at(0).tags.isEmpty() && db.tags(projectId).isEmpty(),
              "a database from an older version gets its tags: " + error);
    }

    QTreeWidgetItem *itemOf(QTreeWidget *tree, const QString &text)
    {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->text(0).contains(text))
                return *it;
        return nullptr;
    }

    void findWindowTests()
    {
        QaDatabase db;
        db.setUser("pat");
        QString error;
        QaImportCounts counts;
        check(db.open(":memory:", error) && db.importJson(sampleScripts(), counts, error), "a database for the tree: " + error);
        QList<QaProject> projects;
        QList<QaSuite> suites;
        QList<QaCase> login, parts;
        db.projects(projects, error);
        db.suites(projects.value(0).id, suites, error);
        const qint64 loginId = suites.value(0).id, partsId = suites.value(1).id;
        db.cases(loginId, login, error);
        db.cases(partsId, parts, error);
        QaCase tagged;
        db.loadCase(login.at(0).id, tagged, error);
        tagged.tags = "smoke, desktop";
        db.saveCase(tagged, error);
        db.loadCase(parts.at(0).id, tagged, error);
        tagged.tags = "Smoke";
        db.saveCase(tagged, error);

        MainWindow window(&db);
        QTreeWidget *tree = window.tree();
        auto *search = window.findChild<QLineEdit *>("treeSearch");
        auto *resultFilter = window.findChild<QComboBox *>("treeResult");
        auto *priorityFilter = window.findChild<QComboBox *>("treePriority");
        auto *tagFilter = window.findChild<QComboBox *>("treeTag");
        auto *foundLabel = window.findChild<QLabel *>("treeFound");
        check(search && resultFilter && priorityFilter && tagFilter && foundLabel, "what is above the tree");
        if (!search || !resultFilter || !priorityFilter || !tagFilter || !foundLabel)
            return;
        QTreeWidgetItem *project = tree->topLevelItem(0);
        check(tree->selectionMode() == QAbstractItemView::ExtendedSelection && foundLabel->isHidden() && tagFilter->count() == 3 && tagFilter->itemText(1) == "desktop"
              && tagFilter->itemText(2) == "smoke" && project->childCount() == 2, "nothing is looked for: everything is shown, and the tags there are can be chosen");

        // ---- words
        search->setText("password");
        project = tree->topLevelItem(0);
        check(project->childCount() == 1 && project->child(0)->text(0) == "Login  (2 of 2)" && project->child(0)->isExpanded() && !foundLabel->isHidden()
              && foundLabel->text() == "2 test cases found", "words: the cases that have them, their suite open, the others out of the way: " + foundLabel->text());
        search->setText("wrong  refused");
        project = tree->topLevelItem(0);
        check(project->childCount() == 1 && project->child(0)->text(0) == "Login  (1 of 2)" && project->child(0)->childCount() == 1
              && project->child(0)->child(0)->text(0).endsWith("S-LOGIN-002  A wrong password") && foundLabel->text() == "1 test case found", "every word, wherever it is");
        search->setText("zebra");
        check(tree->topLevelItem(0)->childCount() == 0 && foundLabel->text() == "0 test cases found", "nothing found is said");
        search->clear();
        project = tree->topLevelItem(0);
        check(project->childCount() == 2 && project->child(0)->text(0) == "Login  (2)" && foundLabel->isHidden(), "and everything again");

        // ---- a tag, a priority, how it went
        tagFilter->setCurrentIndex(tagFilter->findText("smoke"));
        project = tree->topLevelItem(0);
        check(project->childCount() == 2 && project->child(0)->text(0) == "Login  (1 of 2)" && project->child(1)->text(0) == "Parts  (1 of 1)" && foundLabel->text() == "2 test cases found"
              && tagFilter->currentText() == "smoke", "a tag: the cases that have it, whatever its capitals");
        priorityFilter->setCurrentIndex(priorityFilter->findText("High"));
        check(tree->topLevelItem(0)->childCount() == 1 && foundLabel->text() == "1 test case found" && itemOf(tree, "S-LOGIN-001"), "and a priority: both have to fit");
        priorityFilter->setCurrentIndex(0);
        tagFilter->setCurrentIndex(0);
        QaRun run;
        run.projectId = projects.value(0).id;
        run.name = "First";
        check(db.createRun(run, {}, error) && db.setResult(run.id, login.at(1).id, "Failed", "Let in", 0, "pat", error), "a run, and a failure: " + error);
        window.reload();
        resultFilter->setCurrentIndex(resultFilter->findText("Last time: Failed"));
        check(foundLabel->text() == "1 test case found" && itemOf(tree, "S-LOGIN-002") && !itemOf(tree, "S-LOGIN-001"), "how a case went the last time");
        resultFilter->setCurrentIndex(resultFilter->findText("Never run"));
        check(foundLabel->text() == "2 test cases found" && !itemOf(tree, "S-LOGIN-002"), "or that it never ran");
        resultFilter->setCurrentIndex(0);

        // ---- a case's tags, on its panel
        CasePanel *casePanel = window.casePanel();
        auto *tags = casePanel->findChild<QLineEdit *>("caseTags");
        auto *save = casePanel->findChild<QPushButton *>("caseSave");
        check(tags && save && !tags->isEnabled(), "the tags of a case");
        if (!tags || !save)
            return;
        check(window.selectCase("S-LOGIN-001") && tags->isEnabled() && tags->text() == "smoke, desktop", "are shown with it");
        window.selectCase("S-LOGIN-002");
        tags->setText(" License, license ,phone");
        check(save->isEnabled(), "a tag is a change");
        save->click();
        QaCase stored;
        check(db.loadCase(login.at(1).id, stored, error) && stored.tags == "License, phone" && tags->text() == "License, phone" && tagFilter->count() == 5
              && tagFilter->findText("License") > 0, "stored tidied, and there to filter by: " + stored.tags);

        // ---- several at once
        QTreeWidgetItem *one = itemOf(tree, "S-LOGIN-001");
        QTreeWidgetItem *two = itemOf(tree, "S-LOGIN-002");
        tree->setCurrentItem(one);
        two->setSelected(true);
        check(window.selectedCaseIds().size() == 2, "two cases are selected");
        check(window.moveSelectedTo(partsId).isEmpty() && itemOf(tree, "Login  (0)") && itemOf(tree, "Parts  (3)") && itemOf(tree, "Parts  (3)")->isExpanded()
              && db.cases(partsId, parts, error) && parts.size() == 3, "and moved to another suite together");
        tree->setCurrentItem(itemOf(tree, "Parts  (3)"));
        check(window.selectedCaseIds().size() == 3 && window.selectedCaseIds(false).isEmpty(), "a suite stands for its cases");
        // Looked for, a suite stands for what is shown of it.
        search->setText("password");
        tree->setCurrentItem(itemOf(tree, "Parts  (2 of 3)"));
        check(window.selectedCaseIds().size() == 2, "for those that are shown, while something is looked for");
        check(window.moveSelectedTo(loginId).isEmpty(), "which go back");
        search->clear();
        check(itemOf(tree, "Login  (2)") && itemOf(tree, "Parts  (1)"), "to where they were");

        // ---- a clone
        tree->setCurrentItem(itemOf(tree, "S-PARTS-001"));
        check(window.cloneSelected().isEmpty() && tree->currentItem() && tree->currentItem()->text(0) == "S-PARTS-002  Add a part (copy)" && itemOf(tree, "Parts  (2)")
              && casePanel->caseId() != 0 && tags->text() == "Smoke", "a case is cloned, and its clone selected: " + (tree->currentItem() ? tree->currentItem()->text(0) : QString()));
        // ... which is in no run yet: it joins one.
        RunPanel *runs = window.runPanel();
        auto *table = runs->findChild<QTableWidget *>("runResults");
        int added = -1;
        check(table && table->rowCount() == 3 && window.addSelectedToRun(run.id, &added).isEmpty() && added == 1 && table->rowCount() == 4, "the selected case joins a run");
        tree->setCurrentItem(itemOf(tree, "Login  (2)"));
        itemOf(tree, "S-PARTS-002")->setSelected(true);
        check(window.addSelectedToRun(run.id, &added).isEmpty() && added == 0 && table->rowCount() == 4, "what is in it already is not there twice");
        tree->setCurrentItem(itemOf(tree, "Parts  (2)"));
        check(window.cloneSelected().isEmpty() && tree->currentItem() && tree->currentItem()->text(0) == "Parts (copy)  (2)" && itemOf(tree, "S-PARTS-003")
              && itemOf(tree, "S-PARTS-004"), "a suite is cloned with its cases");
        tree->setCurrentItem(tree->topLevelItem(0));
        check(window.cloneSelected() == "Select a test case or a suite to clone.", "a project is not");

        // ---- several are deleted, after one question
        QAction *deleteAction = nullptr;
        for (QAction *action : window.findChildren<QAction *>())
            if (action->text() == "&Delete...")
                deleteAction = action;
        tree->setCurrentItem(itemOf(tree, "Parts (copy)"));
        itemOf(tree, "S-PARTS-002")->setSelected(true);
        itemOf(tree, "S-PARTS-003")->setSelected(true);
        check(deleteAction && deleteAction->isEnabled(), "Delete, with a suite, one of its cases and another case selected");
        if (!deleteAction)
            return;
        deleteAction->trigger();
        auto *box = window.findChild<QMessageBox *>("deleteBox");
        check(box && box->text() == "Delete what is selected: 1 test case, 1 suite with all 2 of their test cases?" && box->informativeText().contains("This cannot be undone"),
              "one question says all of it: " + (box ? box->text() : QString()));
        if (!box)
            return;
        box->button(QMessageBox::Cancel)->click();
        check(itemOf(tree, "Parts (copy)") && itemOf(tree, "S-PARTS-002"), "Cancel deletes nothing");
        tree->setCurrentItem(itemOf(tree, "Parts (copy)"));
        itemOf(tree, "S-PARTS-002")->setSelected(true);
        deleteAction->trigger();
        box = nullptr;
        for (QMessageBox *candidate : window.findChildren<QMessageBox *>("deleteBox"))
            if (candidate->isVisible() || !candidate->testAttribute(Qt::WA_WState_Hidden))
                box = candidate;
        if (box)
            box->button(QMessageBox::Yes)->click();
        check(!itemOf(tree, "Parts (copy)") && !itemOf(tree, "S-PARTS-002") && !itemOf(tree, "S-PARTS-003") && itemOf(tree, "Parts  (1)") && itemOf(tree, "Login  (2)")
              && table->rowCount() == 3 && casePanel->caseId() == 0, "Delete takes them all, and what was in a run goes from it");
        // One alone is asked about as before.
        tree->setCurrentItem(itemOf(tree, "S-PARTS-001"));
        deleteAction->trigger();
        box = nullptr;
        for (QMessageBox *candidate : window.findChildren<QMessageBox *>("deleteBox"))
            if (!candidate->testAttribute(Qt::WA_WState_Hidden))
                box = candidate;
        check(box && box->text().startsWith("Delete the test case \"") && box->text().contains("S-PARTS-001"), "one case is asked about by its name: " + (box ? box->text() : QString()));
        if (box)
            box->button(QMessageBox::Cancel)->click();

        // ---- a run of a tag
        check(runs->createRun("Smoke", "", "pat", {}, error, QString(), "smoke") && table->rowCount() == 2 && table->item(0, 1)->text() == "S-LOGIN-001"
              && table->item(1, 1)->text() == "S-PARTS-001", "a run of the cases with a tag: " + error);
        check(!runs->createRun("None", "", "pat", {}, error, QString(), "nothing") && error.contains("has the tag \"nothing\""), "a tag no case has: " + error);
        auto *newRun = runs->findChild<QPushButton *>("newRun");
        if (newRun)
            newRun->click();
        auto *dialog = runs->findChild<QDialog *>("newRunDialog");
        auto *runTag = dialog ? dialog->findChild<QComboBox *>("runTag") : nullptr;
        check(runTag && runTag->isEnabled() && runTag->count() == 5 && runTag->itemText(0) == "Every test case" && runTag->findText("smoke") > 0,
              "New Run offers the project's tags");
        if (dialog)
            dialog->reject();
    }

    // ---- reporting ---------------------------------------------------------------------------------
    void reportTests()
    {
        const QString github = "https://github.com/Brute-Squad/FactoryInventory/issues/%1";
        // ---- the address of an issue
        check(QaDatabase::defectUrl(github, "123") == "https://github.com/Brute-Squad/FactoryInventory/issues/123" && QaDatabase::defectUrl(github, " #123 ") == QaDatabase::defectUrl(github, "123"),
              "an issue's number in the project's address, with or without its #");
        check(QaDatabase::defectUrl("https://tracker.example/issues", "FI-12") == "https://tracker.example/issues/FI-12"
              && QaDatabase::defectUrl("https://tracker.example/issues/", "7") == "https://tracker.example/issues/7", "an address without a place for it gets it at its end");
        check(QaDatabase::defectUrl("", "https://elsewhere.example/bug/9") == "https://elsewhere.example/bug/9" && QaDatabase::defectUrl(github, "https://elsewhere.example/bug/9")
              == "https://elsewhere.example/bug/9", "a whole address is itself, whatever the project says");
        check(QaDatabase::defectUrl("", "123").isEmpty() && QaDatabase::defectUrl(github, "").isEmpty() && QaDatabase::defectUrl(github, "#").isEmpty()
              && QaDatabase::defectUrl(github, "12 3").isEmpty() && QaDatabase::defectUrl(github, "../../x").isEmpty() && QaDatabase::defectUrl(github, "1?a=b").isEmpty()
              && QaDatabase::defectUrl("file:///C:/x/%1", "1").isEmpty() && QaDatabase::defectUrl(github, "https://a b").isEmpty(),
              "nothing to open: no address, no number, or what is neither");

        QaDatabase db;
        db.setUser("pat");
        QString error;
        QaImportCounts counts;
        QJsonObject scripts = sampleScripts();
        scripts.insert("issueUrl", " https://github.com/o/r/issues/%1 ");
        QList<QaProject> projects;
        check(db.open(":memory:", error) && db.importJson(scripts, counts, error) && db.projects(projects, error) && projects.at(0).issueUrl == "https://github.com/o/r/issues/%1",
              "test scripts say where their project's issues are: " + projects.value(0).issueUrl);
        const qint64 projectId = projects.value(0).id;
        QJsonObject written;
        check(db.exportJson(projectId, written, error) && written.value("issueUrl").toString() == "https://github.com/o/r/issues/%1" && db.importJson(sampleScripts(), counts, error)
              && db.projects(projects, error) && projects.at(0).issueUrl == "https://github.com/o/r/issues/%1", "written back, and left by a file that says nothing of it");
        QaProject changed = projects.at(0);
        changed.issueUrl = " https://tracker.example/%1 ";
        check(db.updateProject(changed, error) && db.projects(projects, error) && projects.at(0).issueUrl == "https://tracker.example/%1" && projects.at(0).name == "Sample",
              "and changed with the project");
        changed.issueUrl = "https://github.com/o/r/issues/%1";
        db.updateProject(changed, error);

        // ---- how an empty project stands
        QaDashboard standing;
        QaProject empty;
        empty.name = "Empty";
        check(db.addProject(empty, error) && db.dashboard(empty.id, standing, error) && standing.cases == 0 && standing.runs.isEmpty() && standing.open.isEmpty()
              && standing.failing.isEmpty() && standing.neverRun.isEmpty(), "a project with nothing in it: " + error);
        QString html = Dashboard::html("Empty", standing, "");
        check(html.contains("How Empty stands") && html.contains("None yet: New Run...") && html.contains("The project has no test cases.") && html.contains("0 never run"),
              "is said to have nothing");
        check(db.dashboard(projectId, standing, error) && standing.cases == 3 && standing.neverRun.size() == 3 && standing.neverRun.at(0).key == "S-LOGIN-001"
              && standing.neverRun.at(0).suite == "Login" && standing.runs.isEmpty(), "cases, and no run yet: all of them never ran");

        // ---- three runs
        QList<QaSuite> suites;
        QList<QaCase> login, parts;
        db.suites(projectId, suites, error);
        db.cases(suites.at(0).id, login, error);
        db.cases(suites.at(1).id, parts, error);
        const qint64 a = login.at(0).id, b = login.at(1).id, c = parts.at(0).id;
        QaRun first, second, third;
        first.projectId = second.projectId = third.projectId = projectId;
        first.name = "Beta 1";
        first.build = "0.1";
        second.name = "Beta 2";
        third.name = "Beta 3";
        check(db.createRun(first, {}, error) && db.createRun(second, {}, error) && db.createRun(third, {}, error), "three runs: " + error);

        // An issue belongs to a failure.
        check(!db.setDefect(first.id, b, "12", error) && error == "An issue belongs to a result that failed or was blocked: this one is \"Not run\".", "no issue on what is not run: " + error);
        check(!db.setDefect(first.id, 999999, "12", error) && error == "That test case is not part of the run.", "nor on a case that is not in the run");
        check(db.setResult(first.id, a, "Passed", "", 0, "pat", error) && db.setResult(first.id, b, "Failed", "He said \"no\", twice\nand again", 1, "pat", error)
              && db.setResult(first.id, c, "Blocked", "No server", 0, "lou", error), "the first run: one passes, one fails, one is blocked");
        QList<QaResult> results;
        check(db.setDefect(first.id, b, "  #12 ", error) && db.setDefect(first.id, c, "13", error) && db.results(first.id, results, error) && results.at(1).defect == "#12"
              && results.at(2).defect == "13" && results.at(0).defect.isEmpty(), "a failure and a blocked case are reported as issues: " + error);
        check(db.setResult(first.id, c, "Blocked", "Still no server", 0, "lou", error) && db.results(first.id, results, error) && results.at(2).defect == "13",
              "which stay while the case is blocked or failed");
        check(db.setResult(first.id, c, "Passed", "", 0, "lou", error) && db.results(first.id, results, error) && results.at(2).defect.isEmpty(), "and go when it passes");
        check(db.setResult(first.id, c, "Blocked", "No server", 0, "lou", error) && db.setDefect(first.id, c, "13", error) && db.setDefect(first.id, c, "", error)
              && db.results(first.id, results, error) && results.at(2).defect.isEmpty(), "or are taken away");
        check(db.setResult(second.id, a, "Passed", "", 0, "pat", error) && db.setResult(second.id, b, "Failed", "Let in again", 0, "pat", error) && db.setDefect(second.id, b, "12", error)
              && db.setResult(second.id, c, "Passed", "", 0, "pat", error), "the second run: the same failure again");
        check(db.setResult(third.id, a, "Failed", "Did not start", 0, "lou", error), "the third: another one fails, the rest is not run");
        QaCase fresh;
        fresh.suiteId = suites.at(1).id;
        fresh.key = "S-PARTS-002";
        fresh.title = "A part nobody has tested";
        check(db.saveCase(fresh, error), "and a case no run has");

        // ---- how the project stands
        check(db.dashboard(projectId, standing, error) && standing.cases == 4 && standing.runs.size() == 3 && standing.runs.at(0).run.name == "Beta 1"
              && standing.runs.at(2).run.name == "Beta 3", "the runs, the oldest first: " + error);
        check(standing.runs.at(0).counts.passed == 1 && standing.runs.at(0).counts.failed == 1 && standing.runs.at(0).counts.blocked == 1 && standing.runs.at(0).passRate() == 33
              && standing.runs.at(1).passRate() == 66 && standing.runs.at(2).passRate() == 0 && standing.runs.at(2).counts.notRun == 2, "each with its pass rate, of what has a verdict");
        QaRunStanding nothing;
        nothing.counts.total = 5;
        nothing.counts.notRun = 4;
        nothing.counts.skipped = 1;
        check(nothing.passRate() == -1, "none where nothing has one");
        check(standing.open.size() == 2 && standing.open.at(0).key == "S-LOGIN-002" && standing.open.at(0).defect == "12" && standing.open.at(0).runName == "Beta 2"
              && standing.open.at(0).notes == "Let in again" && standing.open.at(1).key == "S-LOGIN-001" && standing.open.at(1).defect.isEmpty() && standing.open.at(1).runName == "Beta 3"
              && standing.open.at(1).tester == "lou", "what is wrong now: the cases whose newest result failed - by issue, those without one last");
        check(standing.failing.size() == 1 && standing.failing.at(0).key == "S-LOGIN-002" && standing.failing.at(0).bad == 2 && standing.failing.at(0).ran == 2
              && standing.failing.at(0).lastStatus == "Failed" && standing.failing.at(0).suite == "Login", "what keeps failing: in two runs or more");
        check(standing.neverRun.size() == 1 && standing.neverRun.at(0).key == "S-PARTS-002", "and what never ran");
        // The newest result of a case decides: one that passes again is not open any more.
        check(db.setResult(third.id, b, "Passed", "", 0, "pat", error) && db.dashboard(projectId, standing, error) && standing.open.size() == 1 && standing.open.at(0).key == "S-LOGIN-001"
              && standing.failing.size() == 1 && standing.failing.at(0).lastStatus == "Passed" && standing.failing.at(0).ran == 3, "a case that passes again is no open failure");
        check(db.setResult(third.id, b, "Not run", "", 0, "pat", error) && db.dashboard(projectId, standing, error) && standing.open.size() == 2, "taken back, it is open as before");

        // ---- as a document
        html = Dashboard::html("Sample", standing, "https://github.com/o/r/issues/%1");
        check(html.contains("How Sample stands") && html.contains("4 test cases") && html.contains("3 test runs") && html.contains("2 open failures") && html.contains("1 never run"),
              "the dashboard says how many of everything");
        check(html.contains(QString(7, QChar(0x2588)) + QString(13, QChar(0x2591)) + " 33%") && html.contains(QString(20, QChar(0x2591)) + " 0%")
              && html.contains(QString::fromUtf8("Pass rate, run by run: 33% \xE2\x86\x92 66% \xE2\x86\x92 0%")), "each run's pass rate as a bar, and how it went over time");
        check(html.contains("<b><a href=\"https://github.com/o/r/issues/12\">#12</a></b><br>1 test case") && html.contains("<b>No issue yet</b><br>1 test case")
              && html.indexOf("#12</a>") < html.indexOf("No issue yet") && html.contains("Let in again") && html.contains("Did not start"),
              "open failures under their issue, which is a link");
        check(html.contains("in 2 of 2 runs") && html.contains("1 test case no run has a result for.") && html.contains("A part nobody has tested"),
              "what keeps failing, and what never ran");
        html = Dashboard::html("Sample", standing, "");
        check(html.contains("<b>#12</b>") && !html.contains("<a href"), "an issue is a number where the project does not say where its issues are");

        // ---- a run as a table for a spreadsheet
        check(db.results(first.id, results, error), "the first run's results");
        const QString csv = Report::csv(results);
        const QStringList lines = csv.split("\r\n");
        check(lines.at(0) == "Suite,Key,Title,Priority,Run on,Result,Failed at step,By,When,For,Issue,Files,Notes" && csv.endsWith("\r\n"), "a run as CSV: what the columns are");
        check(lines.at(1).startsWith("Login,S-LOGIN-001,Log in,High,Desktop,Passed,,pat,20") && lines.at(1).endsWith(",,,,"), "a line per test case: " + lines.value(1));
        check(csv.contains("Login,S-LOGIN-002,A wrong password,Medium,,Failed,1,pat,") && csv.contains(",,#12,,\"He said \"\"no\"\", twice\nand again\"\r\n"),
              "text with commas, marks and line breaks in quotation marks, an issue in its column");
        check(csv.contains("Parts,S-PARTS-001,Add a part,Medium,,Blocked,,lou,"), "and every case");
        QTemporaryDir folder;
        const QString file = folder.filePath("run.csv");
        QFile read(file);
        check(Report::saveCsv(csv, file, error) && read.open(QIODevice::ReadOnly) && read.read(3) == QByteArray("\xEF\xBB\xBF") && read.readAll() == csv.toUtf8(),
              "written as UTF-8 that a spreadsheet takes for it: " + error);
        check(!Report::saveCsv(csv, folder.filePath("no/such/folder/run.csv"), error) && !error.isEmpty(), "a folder that is not there is said");

        // ---- the report of a run names the issue
        QaSummary summary;
        db.summary(first.id, summary, error);
        html = Report::html("Sample", first, summary, results, "https://github.com/o/r/issues/%1");
        check(html.contains("<th width=\"10%\">Issue</th>") && html.contains("<a href=\"https://github.com/o/r/issues/12\">#12</a>"), "the report of a run names a failure's issue");
        html = Report::html("Sample", first, summary, results);
        check(html.contains("<td>#12</td>") && !html.contains("<a href"), "as a number, without an address");

        // ---- a database from before the issues
        QTemporaryDir older;
        const QString path = older.filePath("qatest.sqlite");
        QaDatabase onDisk;
        check(onDisk.open(path, error) && onDisk.importJson(sampleScripts(), counts, error), "a database in a file");
        onDisk.close();
        bool dropped = false;
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "raw4");
            raw.setDatabaseName(path);
            if (raw.open())
            {
                QSqlQuery query(raw);
                dropped = query.exec("ALTER TABLE results DROP COLUMN defect") && query.exec("ALTER TABLE projects DROP COLUMN issue_url");
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase("raw4");
        check(dropped && onDisk.open(path, error) && onDisk.projects(projects, error) && projects.size() == 1 && projects.at(0).issueUrl.isEmpty()
              && onDisk.dashboard(projects.at(0).id, standing, error) && standing.cases == 3, "a database from an older version is brought up to date: " + error);
    }

    void reportWindowTests()
    {
        QaDatabase db;
        db.setUser("pat");
        QString error;
        QaImportCounts counts;
        check(db.open(":memory:", error) && db.importJson(sampleScripts(), counts, error), "a database for the dashboard: " + error);
        MainWindow window(&db);
        auto *tabs = window.findChild<QTabWidget *>("tabs");
        DashboardPanel *dashboard = window.dashboard();
        RunPanel *runs = window.runPanel();
        auto *table = runs->findChild<QTableWidget *>("runResults");
        auto *notes = runs->findChild<QPlainTextEdit *>("runNotes");
        auto *defect = runs->findChild<QLineEdit *>("runDefect");
        auto *failed = runs->findChild<QPushButton *>("markFailed");
        auto *passed = runs->findChild<QPushButton *>("markPassed");
        auto *csvButton = runs->findChild<QPushButton *>("runCsv");
        auto *problem = runs->findChild<QLabel *>("runProblem");
        check(tabs && dashboard && table && notes && defect && failed && passed && csvButton && problem, "the pieces for reporting");
        if (!tabs || !dashboard || !table || !notes || !defect || !failed || !passed || !csvButton || !problem)
            return;
        check(tabs->count() == 3 && tabs->tabText(2) == "Dashboard" && dashboard->projectId() == runs->projectId() && dashboard->projectId() != 0,
              "a third tab, for the project that is selected");
        check(dashboard->html().contains("How Sample stands") && dashboard->html().contains("None yet: New Run...") && dashboard->html().contains("3 never run"),
              "which says how it stands before any run");
        check(!csvButton->isEnabled() && runs->csv().isEmpty() && !defect->isEnabled(), "no run: nothing to export, no issue to name");

        // ---- the issue a failure was reported as
        check(runs->createRun("Beta 1", "0.1", "pat", {}, error) && table->columnCount() == 10 && table->horizontalHeaderItem(9)->text() == "Issue" && csvButton->isEnabled()
              && defect->isEnabled(), "a run: " + error);
        table->selectRow(1);
        notes->setPlainText("It was let in");
        defect->setText(" #77 ");
        failed->click();
        QList<QaResult> results;
        check(table->item(1, 9)->text() == "#77" && db.results(runs->runId(), results, error) && results.at(1).defect == "#77" && results.at(1).status == "Failed",
              "Failed stores the issue that was typed with it: " + table->item(1, 9)->text());
        table->selectRow(1);
        check(defect->text() == "#77" && runs->defectLink().isEmpty(), "it is shown with the case; without the project's address it is a number");
        QList<QaProject> projects;
        db.projects(projects, error);
        QaProject project = projects.value(0);
        project.issueUrl = "https://github.com/Brute-Squad/FactoryInventory/issues/%1";
        check(db.updateProject(project, error) && runs->defectLink() == "https://github.com/Brute-Squad/FactoryInventory/issues/77", "with it, an address: " + runs->defectLink());
        // Changed afterwards, it is stored as the field is left.
        defect->setText("78");
        emit defect->editingFinished();
        check(table->item(1, 9)->text() == "78" && defect->text() == "78" && table->currentRow() == 1 && notes->toPlainText() == "It was let in",
              "a change of the issue is stored when the field is left, and the case stays selected");
        // Not on a case that has not failed.
        table->selectRow(2);
        defect->setText("5");
        emit defect->editingFinished();
        check(table->item(2, 9)->text().isEmpty() && db.results(runs->runId(), results, error) && results.at(2).defect.isEmpty(), "an issue waits for Failed or Blocked");
        passed->click();
        check(table->item(2, 9)->text().isEmpty() && table->item(2, 3)->text().endsWith("Passed"), "and is not kept by a case that passes");
        table->selectRow(1);
        passed->click();
        check(table->item(1, 9)->text().isEmpty(), "nor by one that passes after all");

        // ---- Run Mode names it too
        table->selectRow(0);
        RunMode *mode = runs->openRunMode();
        auto *modeDefect = mode ? mode->findChild<QLineEdit *>("modeDefect") : nullptr;
        auto *modeNotes = mode ? mode->findChild<QPlainTextEdit *>("modeNotes") : nullptr;
        check(mode && modeDefect && modeNotes && mode->position() == 1 && modeDefect->text().isEmpty(), "Run Mode has a place for the issue");
        if (!mode || !modeDefect || !modeNotes)
            return;
        modeNotes->setPlainText("No server");
        modeDefect->setText("90");
        mode->mark("Blocked");
        check(table->item(0, 9)->text() == "90" && table->item(0, 3)->text().endsWith("Blocked"), "B stores it with the result");
        mode->go(-1);
        check(mode->position() == 1 && modeDefect->text() == "90", "and shows it when the case comes up again");
        mode->accept();

        // ---- the run as CSV, the report, the dashboard
        const QString csv = runs->csv();
        check(csv.startsWith("Suite,Key,Title,Priority,Run on,Result,Failed at step,By,When,For,Issue,Files,Notes\r\n") && csv.count("\r\n") == 4
              && csv.contains("Login,S-LOGIN-001,Log in,High,Desktop,Blocked,,pat,") && csv.contains(",,90,,No server\r\n"), "the run as CSV: " + csv.section("\r\n", 1, 1));
        check(runs->reportHtml().contains("<a href=\"https://github.com/Brute-Squad/FactoryInventory/issues/90\">90</a>"), "the report links the issue");
        const QString standing = dashboard->html();
        check(standing.contains("1 test run") && standing.contains("1 open failure") && standing.contains("<a href=\"https://github.com/Brute-Squad/FactoryInventory/issues/90\">#90</a>")
              && standing.contains(QString(13, QChar(0x2588)) + QString(7, QChar(0x2591)) + " 66%") && standing.contains("None: every test case has a result in some run."),
              "the dashboard follows: the run, its pass rate, the open failure under its issue");
        auto *view = dashboard->findChild<QTextBrowser *>("dashboardView");
        tabs->setCurrentIndex(2);
        check(view && view->toPlainText().contains("How Sample stands") && view->toPlainText().contains("Open failures, by issue") && view->toPlainText().contains("#90")
              && view->openExternalLinks(), "the tab shows it when it is looked at");
        // ... and again when something changed while it is in front.
        db.setResult(runs->runId(), results.at(0).caseId, "Passed", "", 0, "pat", error);
        window.reload();
        check(view && view->toPlainText().contains("None: no test case's newest result is a failure or blocked.") && !view->toPlainText().contains("#90"),
              "and again when the window reads the database again");
        tabs->setCurrentIndex(0);
    }

    // The sample scripts with something done to them.
    QJsonObject editedScripts(const std::function<void(QJsonArray &login, QJsonArray &parts)> &edit, const QString &version = QString())
    {
        QJsonObject scripts = sampleScripts();
        QJsonArray suitesJson = scripts.value("suites").toArray();
        QJsonObject loginJson = suitesJson.at(0).toObject(), partsJson = suitesJson.at(1).toObject();
        QJsonArray login = loginJson.value("cases").toArray(), parts = partsJson.value("cases").toArray();
        edit(login, parts);
        loginJson.insert("cases", login);
        partsJson.insert("cases", parts);
        suitesJson.replace(0, loginJson);
        suitesJson.replace(1, partsJson);
        scripts.insert("suites", suitesJson);
        if (!version.isEmpty())
            scripts.insert("version", version);
        return scripts;
    }

    // What the next version of the sample scripts says: a title and a step changed, a case moved,
    // one added in a new suite - and one taken out.
    QJsonObject nextScripts(const QString &version)
    {
        QJsonObject scripts = editedScripts([](QJsonArray &login, QJsonArray &parts) {
            QJsonObject first = login.at(0).toObject();
            first.insert("title", "Log in as a user");
            QJsonArray steps = first.value("steps").toArray();
            steps.append(QJsonObject { { "action", "Log out" }, { "expected", "The login dialog is back" } });
            first.insert("steps", steps);
            login.replace(0, first);
            // The second case of Login goes to Parts; Parts' own is taken out.
            parts = QJsonArray { login.at(1) };
            login.removeAt(1);
        }, version);
        QJsonArray suitesJson = scripts.value("suites").toArray();
        suitesJson.append(QJsonObject { { "name", "Stock" }, { "cases", QJsonArray {
            QJsonObject { { "key", "S-STOCK-001" }, { "title", "Count the stock" }, { "steps", QJsonArray { QJsonObject { { "action", "Count" }, { "expected", "It adds up" } } } } } } } });
        scripts.insert("suites", suitesJson);
        return scripts;
    }

    // ---- the scripts' version, and what a file would change ----------------------------------------
    void scriptsVersionTests()
    {
        check(QaDatabase::compareVersions("2026-10-09", "2026-09-30") > 0 && QaDatabase::compareVersions("2026-10-09", "2026-10-09") == 0
              && QaDatabase::compareVersions("2026-9-30", "2026-10-01") < 0, "versions that are dates");
        check(QaDatabase::compareVersions("1.10", "1.9") > 0 && QaDatabase::compareVersions("1.9", "1.10") < 0 && QaDatabase::compareVersions("2", "10") < 0
              && QaDatabase::compareVersions("1.2", "1.2.1") < 0 && QaDatabase::compareVersions(" 3 ", "3") == 0 && QaDatabase::compareVersions("1.0-beta", "1.0-Beta") == 0
              && QaDatabase::compareVersions("1.0-alpha", "1.0-beta") < 0, "and numbers: by their value, not as text");

        QaDatabase db;
        db.setUser("pat");
        QString error;
        QaImportCounts counts;
        QaImportPreview preview;
        check(db.open(":memory:", error), "a database");

        // ---- a project the database does not have
        check(!db.previewImport(QJsonObject(), preview, error) && error.contains("names no project"), "what is no file of scripts has no preview: " + error);
        const QJsonObject first = editedScripts([](QJsonArray &, QJsonArray &) {}, "2026-09-01");
        QList<QaProject> projects;
        check(db.previewImport(first, preview, error) && preview.project == "Sample" && preview.newProject && preview.fileVersion == "2026-09-01" && preview.databaseVersion.isEmpty()
              && preview.added.size() == 3 && preview.changed.isEmpty() && preview.missing.isEmpty() && preview.unchanged == 0 && preview.newSuites.size() == 2
              && preview.changesCases() && preview.versionOrder() == 0 && db.projects(projects, error) && projects.isEmpty(),
              "a new project: everything would be added - and nothing was: " + error);
        check(db.importJson(first, counts, error) && counts.casesAdded == 3 && db.projects(projects, error) && projects.at(0).scriptsVersion == "2026-09-01",
              "read, the project remembers the file's version: " + projects.value(0).scriptsVersion);
        const qint64 projectId = projects.value(0).id;

        // ---- the same file again
        check(db.previewImport(first, preview, error) && !preview.newProject && preview.databaseVersion == "2026-09-01" && preview.versionOrder() == 0 && preview.unchanged == 3
              && preview.added.isEmpty() && preview.changed.isEmpty() && preview.missing.isEmpty() && preview.newSuites.isEmpty() && !preview.changesCases()
              && !ImportPreview::worthImporting(preview) && preview.summary() == "3 unchanged.", "the same file again: nothing would change: " + preview.summary());
        QList<QaSuite> suites;
        QList<QaCase> login;
        db.suites(projectId, suites, error);
        db.cases(suites.at(0).id, login, error);
        QaCase before, after;
        db.loadCase(login.at(0).id, before, error);
        check(db.importJson(first, counts, error) && counts.casesUnchanged == 3 && counts.casesUpdated == 0 && counts.casesAdded == 0
              && counts.text() == "Nothing changed: the 3 test cases are as the file says." && db.loadCase(login.at(0).id, after, error) && after.revision == before.revision
              && after.updated == before.updated, "and nothing does: a case that is as the file says is not touched: " + counts.text());
        QaImportCounts one;
        one.casesUnchanged = 1;
        check(one.text() == "Nothing changed: the test case is as the file says." && QaImportCounts().text() == "Nothing was added: the file has no test cases.", "said for one, and for none");

        // ---- a newer file, and something of the team's own in the database
        QaCase own;
        own.suiteId = suites.at(0).id;
        own.key = "S-LOGIN-090";
        own.title = "Our own case";
        own.steps << QaStep { "Do it", "Done" };
        QaRun run;
        run.projectId = projectId;
        run.name = "Kept";
        check(db.saveCase(own, error) && db.createRun(run, {}, error) && db.setResult(run.id, login.at(1).id, "Failed", "Let in", 0, "pat", error), "a case of the team's own, and a run");
        const QJsonObject next = nextScripts("2026-10-09");
        check(db.previewImport(next, preview, error) && preview.versionOrder() > 0 && preview.fileVersion == "2026-10-09" && preview.databaseVersion == "2026-09-01",
              "a newer file is seen to be newer: " + error);
        check(preview.added.size() == 1 && preview.added.at(0).key == "S-STOCK-001" && preview.added.at(0).suite == "Stock" && preview.newSuites == QStringList({ "Stock" }),
              "what is new: a case, in a new suite");
        check(preview.changed.size() == 2 && preview.changed.at(0).key == "S-LOGIN-001" && preview.changed.at(0).title == "Log in as a user"
              && preview.changed.at(0).what == QStringList({ "title", "steps" }) && preview.changed.at(1).key == "S-LOGIN-002"
              && preview.changed.at(1).what == QStringList({ "moved from Login to Parts" }), "what would change, and in what: " + preview.changed.value(0).what.join(",")
              + " / " + preview.changed.value(1).what.join(","));
        check(preview.missing.size() == 2 && preview.missing.at(0).key == "S-LOGIN-090" && preview.missing.at(1).key == "S-PARTS-001" && preview.unchanged == 0
              && preview.summary() == "1 new, 2 changed, 0 unchanged; 2 are not in the file.", "what the file does not have: " + preview.summary());
        check(db.loadCase(login.at(0).id, after, error) && after.title == "Log in" && after.revision == before.revision, "looking changes nothing");
        // Every kind of difference is named.
        const QJsonObject every = editedScripts([](QJsonArray &loginCases, QJsonArray &) {
            QJsonObject caseJson = loginCases.at(0).toObject();
            caseJson.insert("priority", "Low");
            caseJson.insert("area", "Phone");
            caseJson.insert("preconditions", "Another user exists");
            caseJson.insert("notes", "A note");
            caseJson.insert("tags", QJsonArray { "smoke" });
            loginCases.replace(0, caseJson);
        });
        check(db.previewImport(every, preview, error) && preview.changed.size() == 1
              && preview.changed.at(0).what == QStringList({ "priority", "where it is run", "preconditions", "notes", "tags" }) && preview.fileVersion.isEmpty()
              && preview.versionOrder() == 0, "priority, where it is run, preconditions, notes and tags: " + preview.changed.value(0).what.join(","));

        // ---- the document
        check(db.previewImport(next, preview, error), "the newer file once more");
        QString html = ImportPreview::html(preview, "Sample.json");
        check(html.contains("Test scripts for Sample") && html.contains("The file Sample.json is version <b>2026-10-09</b>.")
              && html.contains("last brought up to date from version <b>2026-09-01</b>.") && html.contains("<b>The file is newer.</b>"), "the preview says which is newer");
        check(html.contains("<h2>1 new test case</h2>") && html.contains("<h2>New suites</h2><p>Stock</p>") && html.contains("<h2>2 test cases would change</h2>")
              && html.contains("<td>title, steps</td>") && html.contains("<td>moved from Login to Parts</td>") && html.contains("<h2>2 test cases are not in the file</h2>")
              && html.contains("Our own case") && html.contains("<b>stay as they are</b>"), "what is new, what would change and in what, and what is not in the file");

        // ---- read: what is not in the file stays
        check(db.importJson(next, counts, error) && counts.casesAdded == 1 && counts.casesUpdated == 2 && counts.casesUnchanged == 0 && counts.casesDeleted == 0 && counts.suites == 1
              && counts.text() == "1 suite and 1 test case were added; 2 test cases were updated.", "the newer file is read: " + error + counts.text());
        check(db.projects(projects, error) && projects.at(0).scriptsVersion == "2026-10-09" && db.loadCase(own.id, after, error) && after.title == "Our own case"
              && db.loadCase(login.at(1).id, after, error) && after.suiteId == suites.at(1).id, "the database is of its version, the team's own case stays, the moved one moved");
        QList<QaResult> results;
        check(db.results(run.id, results, error) && results.size() == 4, "and what was recorded stays");
        check(db.previewImport(next, preview, error) && !preview.changesCases() && preview.unchanged == 3 && preview.missing.size() == 2 && preview.versionOrder() == 0
              && !ImportPreview::worthImporting(preview), "after which the file has nothing more to bring");
        html = ImportPreview::html(preview, "Sample.json");
        check(html.contains("<b>Nothing would change:</b> the database's 3 test cases are as the file says.") && html.contains("They are the same version."), "and the preview says so");

        // ---- an older file
        check(db.previewImport(first, preview, error) && preview.versionOrder() < 0 && ImportPreview::html(preview, "Old.json").contains("<b>The file is OLDER than the database's:</b>"),
              "an older file is said to be older");
        // ---- only the version is new
        const QJsonObject onlyVersion = nextScripts("2026-11-01");
        check(db.previewImport(onlyVersion, preview, error) && !preview.changesCases() && preview.versionOrder() > 0 && ImportPreview::worthImporting(preview)
              && ImportPreview::html(preview, "x.json").contains("Import only marks the database as being of the file's version."), "a file that only has a newer version");
        // A version may be a number.
        QJsonObject numbered = nextScripts("x");
        numbered.insert("version", 7);
        check(db.previewImport(numbered, preview, error) && preview.fileVersion == "7", "a version written as a number: " + preview.fileVersion);

        // ---- read with what is not in the file deleted
        check(db.importJson(next, counts, error, true) && counts.casesDeleted == 2 && counts.casesUnchanged == 3 && counts.text() == "2 test cases were deleted."
              && !db.loadCase(own.id, after, error) && db.results(run.id, results, error) && results.size() == 2, "asked to, the import deletes what the file does not have: " + counts.text());
        check(db.previewImport(next, preview, error) && preview.missing.isEmpty() && preview.summary() == "3 unchanged.", "and then the database is the file");

        // ---- written back, a project says its version
        QJsonObject written;
        check(db.exportJson(projectId, written, error) && written.value("version").toString() == "2026-10-09", "a project is exported with its scripts' version");

        // ---- a database from before the versions
        QTemporaryDir folder;
        const QString path = folder.filePath("qatest.sqlite");
        QaDatabase onDisk;
        check(onDisk.open(path, error) && onDisk.importJson(first, counts, error), "a database in a file");
        onDisk.close();
        bool dropped = false;
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "raw5");
            raw.setDatabaseName(path);
            if (raw.open())
            {
                QSqlQuery query(raw);
                dropped = query.exec("ALTER TABLE projects DROP COLUMN scripts_version");
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase("raw5");
        check(dropped && onDisk.open(path, error) && onDisk.projects(projects, error) && projects.at(0).scriptsVersion.isEmpty() && onDisk.previewImport(first, preview, error)
              && preview.databaseVersion.isEmpty() && preview.versionOrder() == 0 && !preview.changesCases() && ImportPreview::worthImporting(preview),
              "a database from an older version has scripts of no version, and a file can give it one: " + error);
    }

    void writeScripts(const QString &path, const QJsonObject &scripts)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write(QJsonDocument(scripts).toJson());
    }

    void scriptsWindowTests()
    {
        QTemporaryDir folder;
        const QString bundled = folder.filePath("scripts");
        const QString file = bundled + "/Sample.json";
        QaDatabase db;
        db.setUser("pat");
        QString error;
        QaImportCounts counts;
        const QJsonObject first = editedScripts([](QJsonArray &, QJsonArray &) {}, "2026-09-01");
        check(db.open(":memory:", error) && db.importJson(first, counts, error), "a database with the first scripts: " + error);

        // ---- the line that says the program brought newer ones
        QString about;
        check(MainWindow::bundledNotice(db, bundled, &about).isEmpty() && about.isEmpty(), "a program that brought no scripts says nothing");
        writeScripts(file, first);
        check(MainWindow::bundledNotice(db, bundled, &about).isEmpty() && about.isEmpty(), "nor one that brought the ones the database has");
        writeScripts(file, nextScripts("2026-10-09"));
        QString notice = MainWindow::bundledNotice(db, bundled, &about);
        check(notice == "The test scripts that came with this program are newer than this database's: Sample - 1 new and 2 changed test cases (version 2026-10-09; the database has 2026-09-01)."
              && about == file, "newer ones are said, with how much they would bring: " + notice);
        writeScripts(bundled + "/Other.json", QJsonObject { { "project", "Other" }, { "version", "1" }, { "suites", QJsonArray {
            QJsonObject { { "name", "Only" }, { "cases", QJsonArray { QJsonObject { { "key", "O-1" }, { "title", "One" } } } } } } } });
        check(MainWindow::bundledNotice(db, bundled, &about) == notice && about == file, "a project the database does not have is not behind in anything");
        QFile::remove(bundled + "/Other.json");

        MainWindow window(&db);
        auto *bar = window.findChild<QWidget *>("scriptsBar");
        auto *text = window.findChild<QLabel *>("scriptsText");
        auto *show = window.findChild<QPushButton *>("scriptsShow");
        auto *later = window.findChild<QPushButton *>("scriptsLater");
        QTreeWidget *tree = window.tree();
        check(bar && text && show && later && bar->isHidden(), "the window's line for it, hidden while there is nothing to say");
        if (!bar || !text || !show || !later)
            return;
        check(tree->topLevelItem(0)->toolTip(0).endsWith("Test scripts: version 2026-09-01"), "a project says which scripts it has: " + tree->topLevelItem(0)->toolTip(0));
        window.setBundledScripts(bundled);
        check(!bar->isHidden() && text->text() == notice && !show->isHidden(), "the window says it, and offers to show what would change");

        // ---- what would change, before it is read
        show->click();
        auto *dialog = window.findChild<ImportPreviewDialog *>("importPreview");
        auto *view = dialog ? dialog->findChild<QTextBrowser *>("previewView") : nullptr;
        auto *deleteBox = dialog ? dialog->findChild<QCheckBox *>("previewDelete") : nullptr;
        auto *import = dialog ? dialog->findChild<QPushButton *>("previewImport") : nullptr;
        check(dialog && view && deleteBox && import, "the preview opens");
        if (!dialog || !view || !deleteBox || !import)
            return;
        check(view->toPlainText().contains("Test scripts for Sample") && view->toPlainText().contains("The file is newer.") && view->toPlainText().contains("1 new test case")
              && view->toPlainText().contains("2 test cases would change") && view->toPlainText().contains("moved from Login to Parts")
              && view->toPlainText().contains("1 test case is not in the file"), "with what is new, what would change and what is not in the file");
        check(import->isEnabled() && import->text() == "&Import" && !deleteBox->isHidden() && !deleteBox->isChecked() && !dialog->deleteMissing()
              && deleteBox->text() == "Also &delete the test case that is not in the file, with its results in every test run", "Import, and - not ticked - what else could go");
        deleteBox->setChecked(true);
        check(import->text() == "&Import and Delete" && dialog->deleteMissing(), "ticked, the button says that it deletes");
        deleteBox->setChecked(false);
        // Cancel reads nothing.
        dialog->reject();
        QList<QaProject> projects;
        check(db.projects(projects, error) && projects.at(0).scriptsVersion == "2026-09-01" && !bar->isHidden() && tree->topLevelItem(0)->childCount() == 2, "Cancel leaves the database as it is");

        // Import reads it.
        ImportPreviewDialog *again = window.openImportPreview(file);
        check(again != nullptr, "the preview once more");
        if (!again)
            return;
        again->accept();
        check(db.projects(projects, error) && projects.at(0).scriptsVersion == "2026-10-09" && bar->isHidden() && window.tree()->topLevelItem(0)->childCount() == 3
              && window.tree()->topLevelItem(0)->toolTip(0).endsWith("Test scripts: version 2026-10-09"), "Import reads the file: the database is up to date, and the line goes");
        QList<QaSuite> suites;
        QList<QaCase> parts;
        db.suites(projects.at(0).id, suites, error);
        db.cases(suites.at(1).id, parts, error);
        check(parts.size() == 2 && parts.at(0).key == "S-LOGIN-002" && parts.at(1).key == "S-PARTS-001", "what is not in the file stayed");

        // ---- nothing more to bring
        ImportPreviewDialog *nothing = window.openImportPreview(file);
        auto *nothingView = nothing ? nothing->findChild<QTextBrowser *>("previewView") : nullptr;
        auto *nothingImport = nothing ? nothing->findChild<QPushButton *>("previewImport") : nullptr;
        auto *nothingDelete = nothing ? nothing->findChild<QCheckBox *>("previewDelete") : nullptr;
        check(nothing && nothingView && nothingImport && nothingDelete && nothingView->toPlainText().contains("Nothing would change:") && !nothingImport->isEnabled(),
              "read again, there is nothing to import, and no Import to press");
        if (nothing && nothingImport && nothingDelete)
        {
            // ... but what is not in the file can still be asked to go.
            nothingDelete->setChecked(true);
            check(nothingImport->isEnabled() && nothing->deleteMissing(), "unless what the file does not have is to go");
            nothing->accept();
            check(db.cases(suites.at(1).id, parts, error) && parts.size() == 1 && parts.at(0).key == "S-LOGIN-002", "which then goes");
        }

        // ---- a file that cannot be read
        check(window.openImportPreview(folder.filePath("none.json")) == nullptr, "a file that is not there opens no preview");
        {
            QFile bad(folder.filePath("bad.json"));
            bad.open(QIODevice::WriteOnly);
            bad.write("not json");
        }
        check(window.openImportPreview(folder.filePath("bad.json")) == nullptr && db.projects(projects, error) && projects.size() == 1, "nor one that is no file of scripts");

        // ---- a program that is older than the database
        writeScripts(file, first);
        window.setBundledScripts(bundled);
        check(!bar->isHidden() && text->text().startsWith("This database's test scripts are newer than those this program brings: Sample (the database has version 2026-10-09, this program brings 2026-09-01).")
              && text->text().contains("There is a newer QA Test Tracker") && show->isHidden(), "an older program is told that it is: " + text->text());
        // Not Now is for this start of the program.
        writeScripts(file, nextScripts("2026-12-01"));
        QaCase changed;
        db.loadCase(parts.at(0).id, changed, error);
        changed.title = "Changed here";
        db.saveCase(changed, error);
        window.setBundledScripts(bundled);
        check(!bar->isHidden() && text->text().contains("Sample - 1 changed test case (version 2026-12-01; the database has 2026-10-09)") && !show->isHidden(),
              "newer ones again: " + text->text());
        later->click();
        window.setBundledScripts(bundled);
        check(bar->isHidden(), "Not Now: not again until the program is started again");

        // ---- of the same version, what differs was changed here on purpose
        QaDatabase same;
        check(same.open(":memory:", error) && same.importJson(nextScripts("2026-12-01"), counts, error), "a database of the program's version");
        QList<QaSuite> sameSuites;
        QList<QaCase> sameCases;
        same.projects(projects, error);
        same.suites(projects.at(0).id, sameSuites, error);
        same.cases(sameSuites.at(0).id, sameCases, error);
        same.loadCase(sameCases.at(0).id, changed, error);
        changed.title = "Reworded by the team";
        check(same.saveCase(changed, error) && MainWindow::bundledNotice(same, bundled, &about).isEmpty(), "a case the team reworded is not the program's business: " + error);
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
    fastTests();
    fastWindowTests();
    findTests();
    findWindowTests();
    reportTests();
    reportWindowTests();
    scriptsVersionTests();
    scriptsWindowTests();

    QTextStream out(stdout);
    if (g_failures == 0)
        out << "All " << g_checks << " checks passed." << Qt::endl;
    else
        out << g_failures << " of " << g_checks << " checks FAILED." << Qt::endl;
    return g_failures == 0 ? 0 : 1;
}
