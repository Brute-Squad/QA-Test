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
//              when - and for whom the case is meant in this run (assigned).
//              A run's cases are fixed when it is made.
//   attachments  the files that go with a result - a screenshot of what went
//              wrong, a log: a row here, the file itself in the folder
//              "attachments" beside the database (which is what a shared
//              drive shares), under the run's number.
//
// Every function that can fail returns false and says why in `error`.
// Deleting takes along what belongs to the record (a project's suites, a
// suite's cases, a case's steps and results, a run's results).

struct QaProject
{
    qint64  id = 0;
    QString name;
    QString description;
    // What the project is made of, as far as a build of each is tested: "Server,
    // Desktop app, Phone app". A run then says the build of each (QaRun::builds).
    QString components;
    // Where the project's issues are, with %1 for an issue's number:
    // "https://github.com/Brute-Squad/FactoryInventory/issues/%1". A failed
    // result names its issue (QaResult::defect), and this makes a link of it.
    QString issueUrl;
    // The version of the test scripts the project was last brought up to date
    // from ("version" in the file; "" = the file said none, or none was read).
    QString scriptsVersion;
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
    QString tags;           // what it is filed under, parted by commas: "smoke, phone" (QaDatabase::tagList)
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
    // The build of each of the project's components that is tested, a line each:
    // "Server: 0.2.0 Beta, built 2026-10-09 07:50". ("" = the run says only `build`.)
    QString builds;
};

// A file that goes with a result.
struct QaAttachment
{
    qint64  id = 0;
    qint64  runId = 0;
    qint64  caseId = 0;
    QString name;           // what it is called: "screenshot-101502.png"
    QString file;           // where it is, from the attachments folder: "12/45-1a2b3c4d-screenshot-101502.png"
    QString added;          // ISO 8601, UTC
    QString addedBy;
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
    QString assigned;       // whose case it is in this run ("" = nobody's in particular)
    QString defect;         // the issue a failure was reported as: "123", "#123" or a whole address ("" = none yet)
    int     attachments = 0; // how many files go with it
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

// How a project stands, over all its runs (QaDatabase::dashboard).
struct QaRunStanding
{
    QaRun     run;
    QaSummary counts;
    // Of the cases that have a verdict - passed, failed or blocked - how many
    // passed, in percent (-1 = none has one yet).
    int passRate() const;
};

struct QaCaseStanding
{
    qint64  caseId = 0;
    QString key;
    QString title;
    QString suite;
    int     bad = 0;        // in how many runs it failed or was blocked
    int     ran = 0;        // in how many it has a result
    QString lastStatus;     // how it went the last time ("" = never ran)
};

// A case whose newest result is a failure, or blocked: something is still wrong.
struct QaOpenFailure
{
    qint64  caseId = 0;
    QString key;
    QString title;
    QString status;
    QString defect;         // "" = no issue yet
    QString notes;
    QString runName;
    QString tester;
    QString executed;
};

struct QaDashboard
{
    int cases = 0;                      // the project's test cases
    QList<QaRunStanding>  runs;         // the oldest first: how the pass rate went
    QList<QaOpenFailure>  open;         // by issue; those without one last
    QList<QaCaseStanding> failing;      // failed or blocked in two runs or more, the worst first
    QList<QaCaseStanding> neverRun;     // have no result in any run
};

struct QaImportCounts
{
    int projects = 0;       // new ones
    int suites = 0;
    int casesAdded = 0;
    int casesUpdated = 0;   // those that were not as the file says
    int casesUnchanged = 0; // those that were: they are left alone
    int casesDeleted = 0;   // those the file does not have, when that was asked for

    QString text() const;   // "1 project, 12 suites and 64 test cases were added; 3 test cases were updated."
};

// What reading a file of test scripts would do to the database, before it is
// done (QaDatabase::previewImport).
struct QaCaseChange
{
    QString key;
    QString title;
    QString suite;
    QStringList what;       // for a case that would change: "title", "steps", "moved from Login to Parts" ...
};

struct QaImportPreview
{
    QString project;
    bool    newProject = false;         // the database does not have it yet
    QString fileVersion;                // the file's "version" ("" = it says none)
    QString databaseVersion;            // what the project was last brought up to date from
    QStringList newSuites;
    QList<QaCaseChange> added;          // in the file, not in the database
    QList<QaCaseChange> changed;        // in both, and not the same
    QList<QaCaseChange> missing;        // in the database, not in the file: they stay unless asked otherwise
    int unchanged = 0;

    // Would reading the file change a test case or add something?
    bool changesCases() const { return newProject || !newSuites.isEmpty() || !added.isEmpty() || !changed.isEmpty(); }
    // The file's version against the database's: > 0 = the file is newer,
    // < 0 = older, 0 = the same, or one of them says none.
    int versionOrder() const;
    // "12 new, 3 changed, 66 unchanged; 2 are not in the file."
    QString summary() const;
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

    // ---- finding and organising
    // "Smoke, phone,, SMOKE" -> { "Smoke", "phone" }: each once, whatever the capitals.
    static QStringList tagList(const QString &tags);
    // Every tag the project's cases have, each once, by name.
    QStringList tags(qint64 projectId);
    // The cases - of that project, or of every one (0) - that have every word
    // of the text somewhere: in the key, the title, what has to be there
    // first, the notes, the tags or a step. Capitals do not matter.
    bool search(qint64 projectId, const QString &text, QList<qint64> &caseIds, QString &error);
    // Those cases go into that suite - of their own project: a key is a
    // case's own in its project only. All of them or none.
    bool moveCases(const QList<qint64> &caseIds, qint64 suiteId, QString &error);
    // Those cases, suites and projects, with what belongs to them: all or none.
    bool deleteSeveral(const QList<qint64> &caseIds, const QList<qint64> &suiteIds, const QList<qint64> &projectIds, QString &error);
    // The case once more, in its suite: the next free key, "<title> (copy)",
    // its steps and everything that describes it - not how it went.
    bool cloneCase(qint64 id, QaCase &copy, QString &error);
    // The suite once more, in its project - "<name> (copy)" - with a clone of each of its cases.
    bool cloneSuite(qint64 id, QaSuite &copy, QString &error);
    // Those cases join that run, each "Not run"; what is in it already, or
    // belongs to another project, is left out. `added` = how many joined.
    bool addToRun(qint64 runId, const QList<qint64> &caseIds, int &added, QString &error);
    // The next free key of a suite's project that starts as that suite's keys do ("FI-PARTS-004").
    QString nextKey(qint64 suiteId);
    // A case's results, the newest run first.
    bool history(qint64 caseId, QList<QaResult> &list, QStringList &runNames, QString &error);

    // ---- runs (the newest first)
    bool runs(qint64 projectId, QList<QaRun> &list, QString &error);
    // A run of those suites' cases (none = every suite of the project), each "Not run".
    // `tag`: only the cases that have that tag ("" = every case of those suites).
    bool createRun(QaRun &run, const QList<qint64> &suiteIds, QString &error, const QString &tag = QString());
    bool updateRun(const QaRun &run, QString &error);                       // name, build, tester, notes, finished
    bool deleteRun(qint64 id, QString &error);
    // A run of the cases of another run that ended one of those ways there
    // (failed and blocked, say): each "Not run", for whom it was there, with
    // that run's builds unless the new one says its own. Not made when there
    // are none.
    bool createRerun(QaRun &run, qint64 fromRunId, const QStringList &statuses, QString &error);
    // Those cases of the run are that tester's ("" = nobody's in particular).
    bool assign(qint64 runId, const QList<qint64> &caseIds, const QString &tester, QString &error);
    // Everybody a run knows: who its cases are for, and who recorded results.
    QStringList testers(qint64 runId);

    // ---- files that go with a result
    // Where they are: "attachments" beside the database ("" = a database in
    // memory has none).
    QString attachmentsFolder() const;
    QString attachmentPath(const QaAttachment &attachment) const;
    // A copy of that file goes with the result of that case in that run
    // (`name` = what to call it; "" = as the file is called). 50 MB at most.
    bool attach(qint64 runId, qint64 caseId, const QString &sourceFile, const QString &name, QaAttachment &attachment, QString &error);
    bool attachments(qint64 runId, qint64 caseId, QList<QaAttachment> &list, QString &error);     // the oldest first
    bool removeAttachment(qint64 id, QString &error);

    // "Server, Desktop app,, " -> { "Server", "Desktop app" }
    static QStringList componentList(const QString &components);
    // The newest run of the project that says builds: what to start the next from.
    QString lastBuilds(qint64 projectId);
    bool results(qint64 runId, QList<QaResult> &list, QString &error);      // by suite, then key
    bool setResult(qint64 runId, qint64 caseId, const QString &status, const QString &notes, int failedStep, const QString &tester, QString &error);
    bool summary(qint64 runId, QaSummary &counts, QString &error);
    // The issue a failed or blocked result was reported as ("" = none). A
    // result that is neither has none: setResult() takes it away.
    bool setDefect(qint64 runId, qint64 caseId, const QString &defect, QString &error);
    // The address of an issue: the project's issueUrl with the number in it -
    // "#123" and "123" are the same - or the reference itself when it is an
    // address already. "" = there is nothing to open.
    static QString defectUrl(const QString &issueUrl, const QString &defect);
    // How a project stands over all its runs.
    bool dashboard(qint64 projectId, QaDashboard &standing, QString &error);

    // ---- test scripts as a file
    //   { "project": "...", "description": "...",
    //     "suites": [ { "name", "description",
    //       "cases": [ { "key", "title", "priority", "area", "preconditions", "notes",
    //         "steps": [ { "action", "expected" } ] } ] } ] }
    // A project and a suite are found by name, a case by its key in the
    // project: importing a file again brings its cases up to date - text and
    // steps - and adds no second one. Results are left alone.
    // A case that is as the file says is left alone: it is not "updated", and
    // somebody who has it open is not disturbed. `deleteMissing`: the
    // project's cases that the file does not have are deleted, with their
    // results - only when asked; else they stay as they are.
    // The file may say its version ("version": "2026-10-09"): the project
    // remembers which it was last brought up to date from.
    bool importJson(const QJsonObject &scripts, QaImportCounts &counts, QString &error, bool deleteMissing = false);
    // What that would do, without doing it.
    bool previewImport(const QJsonObject &scripts, QaImportPreview &preview, QString &error);
    // Two versions - "2026-10-09", "1.10", "12" - by their numbers and words:
    // < 0, 0 or > 0 as the first is older, the same or newer.
    static int compareVersions(const QString &first, const QString &second);
    bool exportJson(qint64 projectId, QJsonObject &scripts, QString &error);

private:
    bool exec(const QString &sql, const QVariantList &values, QString &error, class QSqlQuery *query = nullptr);
    bool createTables(QString &error);
    bool addMissingColumn(const QString &table, const QString &column, const QString &definition, QString &error);
    // The files of the attachments that statement finds (SELECT file ...): read
    // before what they belong to is deleted, removed after.
    QStringList attachmentFiles(const QString &where, const QVariantList &values);
    void removeFiles(const QStringList &files);

    QString m_connection;
    QString m_path;
    int     m_busyTimeoutSeconds = 10;
    QString m_user;
    bool    m_conflict = false;
};

#endif // QADATABASE_H
