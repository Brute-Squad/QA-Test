#ifndef QADATABASE_H
#define QADATABASE_H

#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

// The QA database: one SQLite file.
//
//   projects   what is tested ("Factory Inventory")
//   suites     a project's groups of test cases ("Parts and stock")
//   cases      a test case: a key that is its own in the project
//              ("FI-PARTS-001"), a title, a priority, where it is run
//              (Desktop / Phone / Server ...), preconditions, notes
//   steps      a case's steps, in order: what to do, and what to expect
//   runs       a test run of a project: a name, the build that is tested,
//              who tests, when it was started and finished
//   results    one row per case of a run: Not run / Passed / Failed /
//              Blocked / Skipped, with notes, the step that failed, who and
//              when. A run's cases are fixed when it is made.
//
// Every function that can fail returns false and says why in `error`.
// Deleting takes along what belongs to the record (a project's suites, a
// suite's cases, a case's steps and results, a run's results).

struct QaProject
{
    qint64  id = 0;
    QString name;
    QString description;
};

struct QaSuite
{
    qint64  id = 0;
    qint64  projectId = 0;
    QString name;
    QString description;
    int     caseCount = 0;      // told by suites()
};

struct QaStep
{
    QString action;         // what to do
    QString expected;       // what is to happen
};

struct QaCase
{
    qint64  id = 0;
    qint64  suiteId = 0;
    QString key;            // "FI-PARTS-001": once per project
    QString title;
    QString priority;       // one of QaDatabase::priorities()
    QString area;           // where it is run: "Desktop", "Phone", ...
    QString preconditions;
    QString notes;
    QList<QaStep> steps;    // filled by loadCase(), not by cases()
    QString lastStatus;     // told by cases(): its result in the newest run that has one ("" = never run)
    // The database may be shared: a case counts its changes, and says whose the last was.
    int     revision = 0;   // told by loadCase(); saveCase() stores only on the revision that was read (0 = do not ask)
    QString changedBy;      // who stored it last ("" = not known: a script that was imported, an older version)
    QString updated;        // when, ISO 8601, UTC
};

struct QaRun
{
    qint64  id = 0;
    qint64  projectId = 0;
    QString name;
    QString build;          // what is tested: a version, a build time
    QString tester;
    QString started;        // ISO 8601, UTC
    QString finished;       // "" = still going
    QString notes;
};

struct QaResult
{
    qint64  runId = 0;
    qint64  caseId = 0;
    QString suiteName;
    QString caseKey;
    QString caseTitle;
    QString priority;
    QString area;
    QString status;         // one of QaDatabase::statuses()
    QString notes;
    int     failedStep = 0; // 1 = the first step; 0 = not said
    QString tester;
    QString executed;       // ISO 8601, UTC; "" = not run
};

struct QaSummary
{
    int total = 0;
    int passed = 0;
    int failed = 0;
    int blocked = 0;
    int skipped = 0;
    int notRun = 0;

    // "42 cases: 30 passed, 2 failed, 1 blocked, 0 skipped, 9 not run - 79% done"
    QString text() const;
};

struct QaImportCounts
{
    int projects = 0;       // new ones
    int suites = 0;
    int casesAdded = 0;
    int casesUpdated = 0;

    QString text() const;   // "1 project, 12 suites and 64 test cases were added; 3 test cases were updated."
};

class QaDatabase
{
public:
    QaDatabase();
    ~QaDatabase();

    // Opens the file - making it and its tables when they are not there.
    // ":memory:" is a database that lives as long as this object.
    bool open(const QString &path, QString &error);
    void close();
    bool isOpen() const;

    // How long a statement waits while somebody else is writing - the file
    // may be on a shared drive - before it gives up (10 seconds unless set).
    // For the next open().
    void setBusyTimeout(int seconds) { m_busyTimeoutSeconds = seconds < 0 ? 0 : seconds; }
    QString path() const { return m_path; }

    // The file is there and can be read: false, with the reason in words for
    // whoever is testing, when a shared drive has gone.
    bool reachable(QString &error);
    // Opens the same file again - after it was lost and has come back. Never
    // makes one: a database that is not there stays lost.
    bool reopen(QString &error);
    // Is the file sound (SQLite's own check)? `problem` says what it found.
    bool sound(QString &problem);
    // A copy of the database as it is now, complete in itself, into a file
    // that is not there yet (VACUUM INTO: safe while others work in it).
    bool copyTo(const QString &file, QString &error);

    // Who is at this PC: written beside what they change. The name they are
    // logged in to Windows with, unless said.
    static QString systemUser();
    void setUser(const QString &name) { m_user = name.trimmed(); }
    QString user() const { return m_user; }

    static QStringList statuses();      // "Not run", "Passed", "Failed", "Blocked", "Skipped"
    static QStringList priorities();    // "High", "Medium", "Low"
    static QString notRun()  { return QStringLiteral("Not run"); }
    static QString passed()  { return QStringLiteral("Passed"); }
    static QString failed()  { return QStringLiteral("Failed"); }
    static QString blocked() { return QStringLiteral("Blocked"); }
    static QString skipped() { return QStringLiteral("Skipped"); }

    // ---- projects
    bool projects(QList<QaProject> &list, QString &error);
    bool addProject(QaProject &project, QString &error);            // sets its id
    bool updateProject(const QaProject &project, QString &error);
    bool deleteProject(qint64 id, QString &error);

    // ---- suites (in the order they were put in)
    bool suites(qint64 projectId, QList<QaSuite> &list, QString &error);
    bool addSuite(QaSuite &suite, QString &error);
    bool updateSuite(const QaSuite &suite, QString &error);
    bool deleteSuite(qint64 id, QString &error);

    // ---- cases (by key)
    bool cases(qint64 suiteId, QList<QaCase> &list, QString &error);        // without their steps
    bool loadCase(qint64 id, QaCase &testCase, QString &error);             // with them
    // id 0 = a new one; sets its id. A case that somebody else stored since it
    // was read is not written over: false, saveConflicted() is true and the
    // error says who and when - unless `overwrite` says to do it all the same.
    bool saveCase(QaCase &testCase, QString &error, bool overwrite = false);
    bool saveConflicted() const { return m_conflict; }
    bool deleteCase(qint64 id, QString &error);
    // The next free key of a suite's project that starts as that suite's keys do ("FI-PARTS-004").
    QString nextKey(qint64 suiteId);
    // A case's results, the newest run first.
    bool history(qint64 caseId, QList<QaResult> &list, QStringList &runNames, QString &error);

    // ---- runs (the newest first)
    bool runs(qint64 projectId, QList<QaRun> &list, QString &error);
    // A run of those suites' cases (none = every suite of the project), each "Not run".
    bool createRun(QaRun &run, const QList<qint64> &suiteIds, QString &error);
    bool updateRun(const QaRun &run, QString &error);                       // name, build, tester, notes, finished
    bool deleteRun(qint64 id, QString &error);
    bool results(qint64 runId, QList<QaResult> &list, QString &error);      // by suite, then key
    bool setResult(qint64 runId, qint64 caseId, const QString &status, const QString &notes, int failedStep, const QString &tester, QString &error);
    bool summary(qint64 runId, QaSummary &counts, QString &error);

    // ---- test scripts as a file
    //   { "project": "...", "description": "...",
    //     "suites": [ { "name", "description",
    //       "cases": [ { "key", "title", "priority", "area", "preconditions", "notes",
    //         "steps": [ { "action", "expected" } ] } ] } ] }
    // A project and a suite are found by name, a case by its key in the
    // project: importing a file again brings its cases up to date - text and
    // steps - and adds no second one. Results are left alone.
    bool importJson(const QJsonObject &scripts, QaImportCounts &counts, QString &error);
    bool exportJson(qint64 projectId, QJsonObject &scripts, QString &error);

private:
    bool exec(const QString &sql, const QVariantList &values, QString &error, class QSqlQuery *query = nullptr);
    bool createTables(QString &error);
    bool addMissingColumn(const QString &table, const QString &column, const QString &definition, QString &error);

    QString m_connection;
    QString m_path;
    int     m_busyTimeoutSeconds = 10;
    QString m_user;
    bool    m_conflict = false;
};

#endif // QADATABASE_H
