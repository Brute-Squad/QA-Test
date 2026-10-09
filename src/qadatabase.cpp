#include "qadatabase.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace
{
    QString now()
    {
        return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }

    // A time of the database as somebody reads it: here, today's without the date.
    QString spoken(const QString &utc)
    {
        const QDateTime when = QDateTime::fromString(utc, Qt::ISODate).toLocalTime();
        if (!when.isValid())
            return utc;
        return when.date() == QDate::currentDate() ? when.toString(QStringLiteral("HH:mm")) : when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }

    // A transaction that is rolled back unless it is committed.
    class Transaction
    {
    public:
        explicit Transaction(QSqlDatabase db) : m_db(db), m_open(m_db.transaction()) {}
        ~Transaction() { if (m_open) m_db.rollback(); }
        bool commit() { const bool ok = m_open && m_db.commit(); m_open = false; return ok; }
    private:
        QSqlDatabase m_db;
        bool m_open = false;
    };

    QaResult resultOf(const QSqlQuery &query)
    {
        QaResult result;
        result.runId      = query.value(0).toLongLong();
        result.caseId     = query.value(1).toLongLong();
        result.suiteName  = query.value(2).toString();
        result.caseKey    = query.value(3).toString();
        result.caseTitle  = query.value(4).toString();
        result.priority   = query.value(5).toString();
        result.area       = query.value(6).toString();
        result.status     = query.value(7).toString();
        result.notes      = query.value(8).toString();
        result.failedStep = query.value(9).toInt();
        result.tester     = query.value(10).toString();
        result.executed   = query.value(11).toString();
        result.assigned   = query.value(12).toString();
        result.attachments = query.value(13).toInt();
        result.defect     = query.value(14).toString();
        return result;
    }

    const char *const kResultColumns =
        "r.run_id, r.case_id, s.name, c.key, c.title, c.priority, c.area, r.status, r.notes, r.failed_step, r.tester, r.executed, r.assigned, "
        "(SELECT COUNT(*) FROM attachments a WHERE a.run_id = r.run_id AND a.case_id = r.case_id), r.defect";

    // A file's name as part of another: letters, digits, dots and dashes.
    QString safeName(const QString &name)
    {
        QString safe;
        for (const QChar character : name.trimmed())
            safe += character.isLetterOrNumber() || character == QLatin1Char('.') || character == QLatin1Char('-') || character == QLatin1Char('_') ? character : QLatin1Char('-');
        while (safe.startsWith(QLatin1Char('.')))
            safe.remove(0, 1);
        return safe.isEmpty() ? QStringLiteral("file") : safe.right(80);
    }
}

QString QaSummary::text() const
{
    if (total == 0)
        return QStringLiteral("No test cases.");
    const int done = total - notRun;
    return QStringLiteral("%1 test %2: %3 passed, %4 failed, %5 blocked, %6 skipped, %7 not run - %8% done")
        .arg(total).arg(total == 1 ? QStringLiteral("case") : QStringLiteral("cases")).arg(passed).arg(failed).arg(blocked).arg(skipped).arg(notRun)
        .arg(done * 100 / total);
}

QString QaImportCounts::text() const
{
    const auto count = [](int n, const char *one, const char *several) {
        return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : several));
    };
    QStringList added;
    if (projects > 0)
        added << count(projects, "project", "projects");
    if (suites > 0)
        added << count(suites, "suite", "suites");
    if (casesAdded > 0)
        added << count(casesAdded, "test case", "test cases");

    QString text;
    if (!added.isEmpty())
    {
        const QString last = added.takeLast();
        text = (added.isEmpty() ? last : added.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last)
               + (projects + suites + casesAdded == 1 ? QStringLiteral(" was added") : QStringLiteral(" were added"));
    }
    if (casesUpdated > 0)
        text += (text.isEmpty() ? QString() : QStringLiteral("; ")) + count(casesUpdated, "test case", "test cases")
                + (casesUpdated == 1 ? QStringLiteral(" was updated") : QStringLiteral(" were updated"));
    if (casesDeleted > 0)
        text += (text.isEmpty() ? QString() : QStringLiteral("; ")) + count(casesDeleted, "test case", "test cases")
                + (casesDeleted == 1 ? QStringLiteral(" was deleted") : QStringLiteral(" were deleted"));
    if (text.isEmpty())
        return casesUnchanged > 0 ? QStringLiteral("Nothing changed: the %1 are as the file says.").arg(count(casesUnchanged, "test case", "test cases")).replace(
                                        QStringLiteral("the 1 test case are"), QStringLiteral("the test case is"))
                                  : QStringLiteral("Nothing was added: the file has no test cases.");
    return text + QLatin1Char('.');
}

QaDatabase::QaDatabase()
    : m_connection(QStringLiteral("qa-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_user(systemUser())
{
}

QString QaDatabase::systemUser()
{
    const QString name = qEnvironmentVariable("USERNAME");
    return name.isEmpty() ? qEnvironmentVariable("USER") : name;
}

bool QaDatabase::reachable(QString &error)
{
    if (m_path == QLatin1String(":memory:"))
        return isOpen();
    if (m_path.isEmpty())
    {
        error = QStringLiteral("No database is open.");
        return false;
    }
    if (!QFileInfo::exists(m_path))
    {
        error = QStringLiteral("The database cannot be reached: %1 is not there. If it is on a shared drive, see that the drive is connected.")
                    .arg(QDir::toNativeSeparators(m_path));
        return false;
    }
    QSqlQuery query;
    return exec(QStringLiteral("SELECT COUNT(*) FROM projects"), {}, error, &query);
}

bool QaDatabase::reopen(QString &error)
{
    const QString path = m_path;
    if (path.isEmpty())
    {
        error = QStringLiteral("No database is open.");
        return false;
    }
    if (path == QLatin1String(":memory:"))
        return isOpen();
    if (!QFileInfo::exists(path))
    {
        error = QStringLiteral("The database cannot be reached: %1 is not there. If it is on a shared drive, see that the drive is connected.")
                    .arg(QDir::toNativeSeparators(path));
        return false;
    }
    const bool opened = open(path, error);
    // Lost again, or still: it stays the database that is meant.
    if (!opened)
        m_path = path;
    return opened;
}

bool QaDatabase::sound(QString &problem)
{
    problem.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("PRAGMA quick_check"), {}, problem, &query))
        return false;
    QStringList found;
    while (query.next())
        found << query.value(0).toString();
    if (found.size() == 1 && found.first().compare(QLatin1String("ok"), Qt::CaseInsensitive) == 0)
        return true;
    problem = found.mid(0, 5).join(QStringLiteral("; "));
    return false;
}

bool QaDatabase::copyTo(const QString &file, QString &error)
{
    if (QFileInfo::exists(file))
    {
        error = QStringLiteral("%1 is there already.").arg(QDir::toNativeSeparators(file));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
    {
        error = QStringLiteral("The folder %1 could not be made.").arg(QDir::toNativeSeparators(QFileInfo(file).absolutePath()));
        return false;
    }
    return exec(QStringLiteral("VACUUM INTO '%1'").arg(QDir::toNativeSeparators(file).replace(QLatin1Char('\''), QStringLiteral("''"))), {}, error);
}

QaDatabase::~QaDatabase()
{
    close();
}

QStringList QaDatabase::statuses()
{
    return { notRun(), passed(), failed(), blocked(), skipped() };
}

QStringList QaDatabase::priorities()
{
    return { QStringLiteral("High"), QStringLiteral("Medium"), QStringLiteral("Low") };
}

bool QaDatabase::isOpen() const
{
    return QSqlDatabase::contains(m_connection) && QSqlDatabase::database(m_connection, false).isOpen();
}

void QaDatabase::close()
{
    if (!QSqlDatabase::contains(m_connection))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
    m_path.clear();
}

bool QaDatabase::open(const QString &path, QString &error)
{
    close();
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
    {
        error = QStringLiteral("This program was built without SQLite (the QSQLITE driver of Qt is missing).");
        return false;
    }
    if (path != QLatin1String(":memory:"))
        QDir().mkpath(QFileInfo(path).absolutePath());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(path);
    // On a shared drive somebody else may be writing: wait for them rather than fail.
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(m_busyTimeoutSeconds * 1000));
    if (!db.open())
    {
        error = QStringLiteral("The database %1 could not be opened: %2").arg(QDir::toNativeSeparators(path), db.lastError().text());
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
        return false;
    }
    m_path = path;
    // What belongs to a record goes with it.
    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON"), {}, error) || !createTables(error))
    {
        close();
        return false;
    }
    return true;
}

bool QaDatabase::exec(const QString &sql, const QVariantList &values, QString &error, QSqlQuery *query)
{
    if (!isOpen())
    {
        error = QStringLiteral("No database is open.");
        return false;
    }
    QSqlQuery own(QSqlDatabase::database(m_connection, false));
    QSqlQuery &used = query ? *query : own;
    if (query)
        *query = QSqlQuery(QSqlDatabase::database(m_connection, false));
    if (!used.prepare(sql))
    {
        error = used.lastError().text();
        return false;
    }
    // A text that was never set is an empty text, not NULL: the columns take none.
    for (const QVariant &value : values)
        used.addBindValue(value.typeId() == QMetaType::QString && value.toString().isNull() ? QVariant(QStringLiteral("")) : value);
    if (!used.exec())
    {
        error = used.lastError().text();
        // What a shared file says when it cannot be had, in words for whoever is testing.
        if (error.contains(QLatin1String("unable to open"), Qt::CaseInsensitive) || error.contains(QLatin1String("disk I/O"), Qt::CaseInsensitive)
            || (!m_path.isEmpty() && m_path != QLatin1String(":memory:") && !QFileInfo::exists(m_path)))
            error = QStringLiteral("The database cannot be reached (%1). If it is on a shared drive, see that the drive is connected. "
                                   "What you typed is still here: try again when it is back.").arg(QDir::toNativeSeparators(m_path));
        else if (error.contains(QLatin1String("malformed"), Qt::CaseInsensitive) || error.contains(QLatin1String("not a database"), Qt::CaseInsensitive))
            error = QStringLiteral("The database is damaged (%1). Close the program everywhere and put the newest copy from its folder \"backups\" in its place.")
                        .arg(QDir::toNativeSeparators(m_path));
        else if (error.contains(QLatin1String("locked"), Qt::CaseInsensitive) || error.contains(QLatin1String("busy"), Qt::CaseInsensitive))
            error = QStringLiteral("The database is busy: somebody else is writing to it. Try again in a moment.");
        else if (error.contains(QLatin1String("readonly"), Qt::CaseInsensitive))
            error = QStringLiteral("The database cannot be written: you are not allowed to change files in its folder (%1).")
                        .arg(QDir::toNativeSeparators(QFileInfo(m_path).absolutePath()));
        return false;
    }
    return true;
}

bool QaDatabase::createTables(QString &error)
{
    const QStringList statements {
        QStringLiteral("CREATE TABLE IF NOT EXISTS projects ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE, description TEXT NOT NULL DEFAULT '', created TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS suites ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, "
                       "name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', position INTEGER NOT NULL DEFAULT 0, UNIQUE(project_id, name))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS cases ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, suite_id INTEGER NOT NULL REFERENCES suites(id) ON DELETE CASCADE, "
                       "project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, key TEXT NOT NULL, title TEXT NOT NULL, "
                       "priority TEXT NOT NULL DEFAULT 'Medium', area TEXT NOT NULL DEFAULT '', preconditions TEXT NOT NULL DEFAULT '', "
                       "notes TEXT NOT NULL DEFAULT '', updated TEXT NOT NULL, UNIQUE(project_id, key))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS steps ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE, "
                       "position INTEGER NOT NULL, action TEXT NOT NULL, expected TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS runs ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, "
                       "name TEXT NOT NULL, build TEXT NOT NULL DEFAULT '', tester TEXT NOT NULL DEFAULT '', started TEXT NOT NULL, "
                       "finished TEXT NOT NULL DEFAULT '', notes TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS results ("
                       "run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE, case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE, "
                       "status TEXT NOT NULL DEFAULT 'Not run', notes TEXT NOT NULL DEFAULT '', failed_step INTEGER NOT NULL DEFAULT 0, "
                       "tester TEXT NOT NULL DEFAULT '', executed TEXT NOT NULL DEFAULT '', PRIMARY KEY(run_id, case_id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS attachments ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE, "
                       "case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE, name TEXT NOT NULL, file TEXT NOT NULL, "
                       "added TEXT NOT NULL, added_by TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS attachments_of_result ON attachments(run_id, case_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS steps_of_case ON steps(case_id, position)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS cases_of_suite ON cases(suite_id)"),
    };
    for (const QString &statement : statements)
        if (!exec(statement, {}, error))
            return false;
    // What a database from before does not have yet. (An older program goes on working
    // in the same file: it does not know the columns, and they have their defaults.)
    return addMissingColumn(QStringLiteral("cases"), QStringLiteral("revision"), QStringLiteral("INTEGER NOT NULL DEFAULT 1"), error)
        && addMissingColumn(QStringLiteral("cases"), QStringLiteral("changed_by"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("cases"), QStringLiteral("tags"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("results"), QStringLiteral("assigned"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("runs"), QStringLiteral("builds"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("projects"), QStringLiteral("components"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("projects"), QStringLiteral("issue_url"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("projects"), QStringLiteral("scripts_version"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("results"), QStringLiteral("defect"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error);
}

bool QaDatabase::addMissingColumn(const QString &table, const QString &column, const QString &definition, QString &error)
{
    QSqlQuery query;
    if (!exec(QStringLiteral("PRAGMA table_info(%1)").arg(table), {}, error, &query))
        return false;
    while (query.next())
        if (query.value(1).toString().compare(column, Qt::CaseInsensitive) == 0)
            return true;
    if (exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition), {}, error))
        return true;
    // Two PCs opened the same older database at once, and the other one was first.
    return error.contains(QLatin1String("duplicate column"), Qt::CaseInsensitive);
}

// ---- projects ------------------------------------------------------------------------------------

bool QaDatabase::projects(QList<QaProject> &list, QString &error)
{
    list.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT id, name, description, components, issue_url, scripts_version FROM projects ORDER BY LOWER(name)"), {}, error, &query))
        return false;
    while (query.next())
        list << QaProject { query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toString(), query.value(4).toString(),
                            query.value(5).toString() };
    return true;
}

bool QaDatabase::addProject(QaProject &project, QString &error)
{
    project.name = project.name.trimmed();
    if (project.name.isEmpty())
    {
        error = QStringLiteral("A project needs a name.");
        return false;
    }
    QSqlQuery query;
    project.components = componentList(project.components).join(QStringLiteral(", "));
    project.issueUrl = project.issueUrl.trimmed();
    if (!exec(QStringLiteral("INSERT INTO projects (name, description, components, issue_url, created) VALUES (?, ?, ?, ?, ?)"),
              { project.name, project.description, project.components, project.issueUrl, now() }, error, &query))
    {
        if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive))
            error = QStringLiteral("There is a project \"%1\" already.").arg(project.name);
        return false;
    }
    project.id = query.lastInsertId().toLongLong();
    return true;
}

bool QaDatabase::updateProject(const QaProject &project, QString &error)
{
    if (project.name.trimmed().isEmpty())
    {
        error = QStringLiteral("A project needs a name.");
        return false;
    }
    if (!exec(QStringLiteral("UPDATE projects SET name = ?, description = ?, components = ?, issue_url = ? WHERE id = ?"),
              { project.name.trimmed(), project.description, componentList(project.components).join(QStringLiteral(", ")), project.issueUrl.trimmed(), project.id }, error))
    {
        if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive))
            error = QStringLiteral("There is a project \"%1\" already.").arg(project.name.trimmed());
        return false;
    }
    return true;
}

bool QaDatabase::deleteProject(qint64 id, QString &error)
{
    const QStringList files = attachmentFiles(QStringLiteral("run_id IN (SELECT id FROM runs WHERE project_id = ?)"), { id });
    if (!exec(QStringLiteral("DELETE FROM projects WHERE id = ?"), { id }, error))
        return false;
    removeFiles(files);
    return true;
}

QStringList QaDatabase::componentList(const QString &components)
{
    QStringList list;
    for (const QString &part : components.split(QLatin1Char(',')))
        if (!part.trimmed().isEmpty() && !list.contains(part.trimmed(), Qt::CaseInsensitive))
            list << part.trimmed();
    return list;
}

// ---- suites ----------------------------------------------------------------------------------------

bool QaDatabase::suites(qint64 projectId, QList<QaSuite> &list, QString &error)
{
    list.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT s.id, s.project_id, s.name, s.description, (SELECT COUNT(*) FROM cases c WHERE c.suite_id = s.id) "
                             "FROM suites s WHERE s.project_id = ? ORDER BY s.position, s.id"), { projectId }, error, &query))
        return false;
    while (query.next())
    {
        QaSuite suite;
        suite.id = query.value(0).toLongLong();
        suite.projectId = query.value(1).toLongLong();
        suite.name = query.value(2).toString();
        suite.description = query.value(3).toString();
        suite.caseCount = query.value(4).toInt();
        list << suite;
    }
    return true;
}

bool QaDatabase::addSuite(QaSuite &suite, QString &error)
{
    suite.name = suite.name.trimmed();
    if (suite.name.isEmpty())
    {
        error = QStringLiteral("A suite needs a name.");
        return false;
    }
    QSqlQuery query;
    if (!exec(QStringLiteral("INSERT INTO suites (project_id, name, description, position) "
                             "VALUES (?, ?, ?, (SELECT COALESCE(MAX(position), 0) + 1 FROM suites WHERE project_id = ?))"),
              { suite.projectId, suite.name, suite.description, suite.projectId }, error, &query))
    {
        if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive))
            error = QStringLiteral("The project has a suite \"%1\" already.").arg(suite.name);
        return false;
    }
    suite.id = query.lastInsertId().toLongLong();
    return true;
}

bool QaDatabase::updateSuite(const QaSuite &suite, QString &error)
{
    if (suite.name.trimmed().isEmpty())
    {
        error = QStringLiteral("A suite needs a name.");
        return false;
    }
    if (!exec(QStringLiteral("UPDATE suites SET name = ?, description = ? WHERE id = ?"), { suite.name.trimmed(), suite.description, suite.id }, error))
    {
        if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive))
            error = QStringLiteral("The project has a suite \"%1\" already.").arg(suite.name.trimmed());
        return false;
    }
    return true;
}

bool QaDatabase::deleteSuite(qint64 id, QString &error)
{
    const QStringList files = attachmentFiles(QStringLiteral("case_id IN (SELECT id FROM cases WHERE suite_id = ?)"), { id });
    if (!exec(QStringLiteral("DELETE FROM suites WHERE id = ?"), { id }, error))
        return false;
    removeFiles(files);
    return true;
}

// ---- cases -----------------------------------------------------------------------------------------

bool QaDatabase::cases(qint64 suiteId, QList<QaCase> &list, QString &error)
{
    list.clear();
    QSqlQuery query;
    // With its result in the newest run that has run it.
    if (!exec(QStringLiteral("SELECT c.id, c.suite_id, c.key, c.title, c.priority, c.area, c.preconditions, c.notes, "
                             "COALESCE((SELECT r.status FROM results r WHERE r.case_id = c.id AND r.executed <> '' ORDER BY r.executed DESC, r.run_id DESC LIMIT 1), ''), c.tags "
                             "FROM cases c WHERE c.suite_id = ? ORDER BY c.key, c.id"), { suiteId }, error, &query))
        return false;
    while (query.next())
    {
        QaCase testCase;
        testCase.id = query.value(0).toLongLong();
        testCase.suiteId = query.value(1).toLongLong();
        testCase.key = query.value(2).toString();
        testCase.title = query.value(3).toString();
        testCase.priority = query.value(4).toString();
        testCase.area = query.value(5).toString();
        testCase.preconditions = query.value(6).toString();
        testCase.notes = query.value(7).toString();
        testCase.lastStatus = query.value(8).toString();
        testCase.tags = query.value(9).toString();
        list << testCase;
    }
    return true;
}

bool QaDatabase::loadCase(qint64 id, QaCase &testCase, QString &error)
{
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT id, suite_id, key, title, priority, area, preconditions, notes, revision, changed_by, updated, tags FROM cases WHERE id = ?"),
              { id }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That test case is not there any more.");
        return false;
    }
    testCase = QaCase();
    testCase.id = query.value(0).toLongLong();
    testCase.suiteId = query.value(1).toLongLong();
    testCase.key = query.value(2).toString();
    testCase.title = query.value(3).toString();
    testCase.priority = query.value(4).toString();
    testCase.area = query.value(5).toString();
    testCase.preconditions = query.value(6).toString();
    testCase.notes = query.value(7).toString();
    testCase.revision = query.value(8).toInt();
    testCase.changedBy = query.value(9).toString();
    testCase.updated = query.value(10).toString();
    testCase.tags = query.value(11).toString();

    if (!exec(QStringLiteral("SELECT action, expected FROM steps WHERE case_id = ? ORDER BY position, id"), { id }, error, &query))
        return false;
    while (query.next())
        testCase.steps << QaStep { query.value(0).toString(), query.value(1).toString() };
    return true;
}

bool QaDatabase::saveCase(QaCase &testCase, QString &error, bool overwrite)
{
    m_conflict = false;
    testCase.key = testCase.key.trimmed();
    testCase.title = testCase.title.trimmed();
    if (testCase.key.isEmpty() || testCase.title.isEmpty())
    {
        error = QStringLiteral("A test case needs a key and a title.");
        return false;
    }
    if (!priorities().contains(testCase.priority))
        testCase.priority = QStringLiteral("Medium");
    testCase.tags = tagList(testCase.tags).join(QStringLiteral(", "));

    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT project_id FROM suites WHERE id = ?"), { testCase.suiteId }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("The suite of the test case is not there any more.");
        return false;
    }
    const qint64 projectId = query.value(0).toLongLong();

    Transaction transaction(QSqlDatabase::database(m_connection, false));
    const bool isNew = testCase.id == 0;
    // A change is stored only on what was read: the revision the case had then is the one it
    // has to have still (0 = whoever stores it did not read it, or says to write over).
    const int expected = overwrite ? 0 : testCase.revision;
    const QString when = now();
    const bool stored = isNew
        ? exec(QStringLiteral("INSERT INTO cases (suite_id, project_id, key, title, priority, area, preconditions, notes, updated, revision, changed_by, tags) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?)"),
               { testCase.suiteId, projectId, testCase.key, testCase.title, testCase.priority, testCase.area, testCase.preconditions, testCase.notes, when, m_user,
                 testCase.tags },
               error, &query)
        : exec(QStringLiteral("UPDATE cases SET suite_id = ?, project_id = ?, key = ?, title = ?, priority = ?, area = ?, preconditions = ?, notes = ?, updated = ?, "
                              "revision = revision + 1, changed_by = ?, tags = ? WHERE id = ? AND (? = 0 OR revision = ?)"),
               { testCase.suiteId, projectId, testCase.key, testCase.title, testCase.priority, testCase.area, testCase.preconditions, testCase.notes, when, m_user,
                 testCase.tags, testCase.id, expected, expected }, error, &query);
    if (!stored)
    {
        if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive))
            error = QStringLiteral("The project has a test case with the key %1 already.").arg(testCase.key);
        return false;
    }
    if (!isNew && query.numRowsAffected() != 1)
    {
        // Nothing was stored: the case is gone, or is not the one that was read any more.
        QSqlQuery there;
        if (!exec(QStringLiteral("SELECT changed_by, updated FROM cases WHERE id = ?"), { testCase.id }, error, &there))
            return false;
        if (!there.next())
        {
            error = QStringLiteral("This test case was deleted while you had it open. What you typed is still here: copy it into a new test case to keep it.");
            return false;
        }
        m_conflict = true;
        const QString who = there.value(0).toString();
        error = QStringLiteral("%1 changed this test case at %2, while you had it open. Nothing was stored.")
                    .arg(who.isEmpty() ? QStringLiteral("Somebody else") : who, spoken(there.value(1).toString()));
        return false;
    }
    const qint64 id = isNew ? query.lastInsertId().toLongLong() : testCase.id;

    // The steps are written anew: their order is their place in the list. A step that says nothing is left out.
    if (!exec(QStringLiteral("DELETE FROM steps WHERE case_id = ?"), { id }, error))
        return false;
    int position = 0;
    for (const QaStep &step : std::as_const(testCase.steps))
    {
        if (step.action.trimmed().isEmpty() && step.expected.trimmed().isEmpty())
            continue;
        if (!exec(QStringLiteral("INSERT INTO steps (case_id, position, action, expected) VALUES (?, ?, ?, ?)"),
                  { id, ++position, step.action.trimmed(), step.expected.trimmed() }, error))
            return false;
    }
    if (!transaction.commit())
    {
        error = QStringLiteral("The test case could not be stored.");
        return false;
    }
    testCase.id = id;
    testCase.revision = 1;
    testCase.changedBy = m_user;
    testCase.updated = when;
    if (!isNew && exec(QStringLiteral("SELECT revision FROM cases WHERE id = ?"), { id }, error, &query) && query.next())
        testCase.revision = query.value(0).toInt();
    return true;
}

bool QaDatabase::deleteCase(qint64 id, QString &error)
{
    const QStringList files = attachmentFiles(QStringLiteral("case_id = ?"), { id });
    if (!exec(QStringLiteral("DELETE FROM cases WHERE id = ?"), { id }, error))
        return false;
    removeFiles(files);
    return true;
}

// ---- finding and organising ------------------------------------------------------------------------

QStringList QaDatabase::tagList(const QString &tags)
{
    QStringList list;
    for (const QString &part : tags.split(QLatin1Char(',')))
        if (!part.trimmed().isEmpty() && !list.contains(part.trimmed(), Qt::CaseInsensitive))
            list << part.simplified();
    return list;
}

QStringList QaDatabase::tags(qint64 projectId)
{
    QStringList all;
    QString error;
    QSqlQuery query;
    if (exec(QStringLiteral("SELECT tags FROM cases WHERE project_id = ? AND tags <> ''"), { projectId }, error, &query))
        while (query.next())
            for (const QString &tag : tagList(query.value(0).toString()))
                if (!all.contains(tag, Qt::CaseInsensitive))
                    all << tag;
    all.sort(Qt::CaseInsensitive);
    return all;
}

bool QaDatabase::search(qint64 projectId, const QString &text, QList<qint64> &caseIds, QString &error)
{
    caseIds.clear();
    QString sql = QStringLiteral("SELECT c.id FROM cases c WHERE (? = 0 OR c.project_id = ?)");
    QVariantList values { projectId, projectId };
    for (const QString &word : text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        // The word as it is, wherever it stands: % and _ mean themselves.
        QString pattern = word;
        pattern.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('%'), QStringLiteral("\\%")).replace(QLatin1Char('_'), QStringLiteral("\\_"));
        pattern = QLatin1Char('%') + pattern + QLatin1Char('%');
        sql += QStringLiteral(" AND (c.key LIKE ? ESCAPE '\\' OR c.title LIKE ? ESCAPE '\\' OR c.preconditions LIKE ? ESCAPE '\\' OR c.notes LIKE ? ESCAPE '\\' "
                              "OR c.tags LIKE ? ESCAPE '\\' OR EXISTS (SELECT 1 FROM steps s WHERE s.case_id = c.id AND (s.action LIKE ? ESCAPE '\\' OR s.expected LIKE ? ESCAPE '\\')))");
        for (int i = 0; i < 7; ++i)
            values << pattern;
    }
    QSqlQuery query;
    if (!exec(sql + QStringLiteral(" ORDER BY c.key"), values, error, &query))
        return false;
    while (query.next())
        caseIds << query.value(0).toLongLong();
    return true;
}

bool QaDatabase::moveCases(const QList<qint64> &caseIds, qint64 suiteId, QString &error)
{
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT project_id, name FROM suites WHERE id = ?"), { suiteId }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That suite is not there any more.");
        return false;
    }
    const qint64 projectId = query.value(0).toLongLong();
    Transaction transaction(QSqlDatabase::database(m_connection, false));
    for (const qint64 id : caseIds)
    {
        if (!exec(QStringLiteral("UPDATE cases SET suite_id = ?, updated = ?, revision = revision + 1, changed_by = ? WHERE id = ? AND project_id = ?"),
                  { suiteId, now(), m_user, id, projectId }, error, &query))
            return false;
        if (query.numRowsAffected() != 1)
        {
            QSqlQuery which;
            QString ignored;
            const bool there = exec(QStringLiteral("SELECT key FROM cases WHERE id = ?"), { id }, ignored, &which) && which.next();
            error = there ? QStringLiteral("%1 belongs to another project: a test case is moved within its project. Nothing was moved.").arg(which.value(0).toString())
                          : QStringLiteral("One of the test cases is not there any more. Nothing was moved.");
            return false;
        }
    }
    if (!transaction.commit())
    {
        error = QStringLiteral("The test cases could not be moved.");
        return false;
    }
    return true;
}

bool QaDatabase::deleteSeveral(const QList<qint64> &caseIds, const QList<qint64> &suiteIds, const QList<qint64> &projectIds, QString &error)
{
    // The files that go with their results: read before, removed once everything is gone.
    QStringList files;
    for (const qint64 id : caseIds)
        files << attachmentFiles(QStringLiteral("case_id = ?"), { id });
    for (const qint64 id : suiteIds)
        files << attachmentFiles(QStringLiteral("case_id IN (SELECT id FROM cases WHERE suite_id = ?)"), { id });
    for (const qint64 id : projectIds)
        files << attachmentFiles(QStringLiteral("run_id IN (SELECT id FROM runs WHERE project_id = ?)"), { id });
    {
        Transaction transaction(QSqlDatabase::database(m_connection, false));
        for (const qint64 id : caseIds)
            if (!exec(QStringLiteral("DELETE FROM cases WHERE id = ?"), { id }, error))
                return false;
        for (const qint64 id : suiteIds)
            if (!exec(QStringLiteral("DELETE FROM suites WHERE id = ?"), { id }, error))
                return false;
        for (const qint64 id : projectIds)
            if (!exec(QStringLiteral("DELETE FROM projects WHERE id = ?"), { id }, error))
                return false;
        if (!transaction.commit())
        {
            error = QStringLiteral("Nothing was deleted: it could not be stored.");
            return false;
        }
    }
    removeFiles(files);
    return true;
}

bool QaDatabase::cloneCase(qint64 id, QaCase &copy, QString &error)
{
    if (!loadCase(id, copy, error))
        return false;
    copy.id = 0;
    copy.revision = 0;
    copy.lastStatus.clear();
    copy.key = nextKey(copy.suiteId);
    copy.title = (copy.title + QStringLiteral(" (copy)")).right(200);
    return saveCase(copy, error);
}

bool QaDatabase::cloneSuite(qint64 id, QaSuite &copy, QString &error)
{
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT project_id, name, description FROM suites WHERE id = ?"), { id }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That suite is not there any more.");
        return false;
    }
    copy = QaSuite();
    copy.projectId = query.value(0).toLongLong();
    copy.description = query.value(2).toString();
    // "<name> (copy)", or "(copy 2)" ... while that is taken.
    const QString name = query.value(1).toString();
    bool added = false;
    for (int number = 1; number < 100 && !added; ++number)
    {
        copy.name = number == 1 ? name + QStringLiteral(" (copy)") : QStringLiteral("%1 (copy %2)").arg(name).arg(number);
        added = addSuite(copy, error);
        if (!added && !error.contains(QLatin1String("already")))
            return false;
    }
    if (!added)
        return false;

    QList<QaCase> list;
    bool ok = cases(id, list, error);
    for (int i = 0; ok && i < list.size(); ++i)
    {
        QaCase testCase;
        ok = loadCase(list.at(i).id, testCase, error);
        if (!ok)
            break;
        testCase.id = 0;
        testCase.revision = 0;
        testCase.suiteId = copy.id;
        // The next key that is free in the project, as the suite's keys go.
        testCase.key = nextKey(id);
        ok = saveCase(testCase, error);
    }
    if (!ok)
    {
        // All of it or nothing.
        QString ignored;
        deleteSuite(copy.id, ignored);
        copy = QaSuite();
        return false;
    }
    copy.caseCount = int(list.size());
    return true;
}

bool QaDatabase::addToRun(qint64 runId, const QList<qint64> &caseIds, int &added, QString &error)
{
    added = 0;
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT project_id, finished FROM runs WHERE id = ?"), { runId }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That test run is not there any more.");
        return false;
    }
    if (!query.value(1).toString().isEmpty())
    {
        error = QStringLiteral("That test run is finished: reopen it to add test cases.");
        return false;
    }
    const qint64 projectId = query.value(0).toLongLong();
    Transaction transaction(QSqlDatabase::database(m_connection, false));
    for (const qint64 id : caseIds)
    {
        if (!exec(QStringLiteral("INSERT OR IGNORE INTO results (run_id, case_id) SELECT ?, id FROM cases WHERE id = ? AND project_id = ?"), { runId, id, projectId }, error, &query))
            return false;
        added += qMax(0, query.numRowsAffected());
    }
    if (!transaction.commit())
    {
        error = QStringLiteral("The test cases could not be added.");
        return false;
    }
    return true;
}

QString QaDatabase::nextKey(qint64 suiteId)
{
    QString error;
    QSqlQuery query;
    // What the suite's keys start with: everything up to their last number.
    QString prefix;
    if (exec(QStringLiteral("SELECT key FROM cases WHERE suite_id = ? ORDER BY key DESC LIMIT 1"), { suiteId }, error, &query) && query.next())
    {
        prefix = query.value(0).toString();
        while (!prefix.isEmpty() && prefix.back().isDigit())
            prefix.chop(1);
    }
    if (prefix.isEmpty())
        prefix = QStringLiteral("TC-");

    // The first number no key of the project has.
    QStringList taken;
    if (exec(QStringLiteral("SELECT key FROM cases WHERE project_id = (SELECT project_id FROM suites WHERE id = ?)"), { suiteId }, error, &query))
        while (query.next())
            taken << query.value(0).toString();
    for (int number = 1; number < 100000; ++number)
    {
        const QString key = prefix + QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0'));
        if (!taken.contains(key))
            return key;
    }
    return prefix;
}

bool QaDatabase::history(qint64 caseId, QList<QaResult> &list, QStringList &runNames, QString &error)
{
    list.clear();
    runNames.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT %1, n.name || CASE WHEN n.build <> '' THEN ' (' || n.build || ')' ELSE '' END FROM results r "
                             "JOIN cases c ON c.id = r.case_id JOIN suites s ON s.id = c.suite_id JOIN runs n ON n.id = r.run_id "
                             "WHERE r.case_id = ? ORDER BY n.started DESC, n.id DESC").arg(QLatin1String(kResultColumns)), { caseId }, error, &query))
        return false;
    while (query.next())
    {
        list << resultOf(query);
        runNames << query.value(15).toString();
    }
    return true;
}

// ---- runs ------------------------------------------------------------------------------------------

bool QaDatabase::runs(qint64 projectId, QList<QaRun> &list, QString &error)
{
    list.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT id, project_id, name, build, tester, started, finished, notes, builds FROM runs WHERE project_id = ? ORDER BY started DESC, id DESC"),
              { projectId }, error, &query))
        return false;
    while (query.next())
    {
        QaRun run;
        run.id = query.value(0).toLongLong();
        run.projectId = query.value(1).toLongLong();
        run.name = query.value(2).toString();
        run.build = query.value(3).toString();
        run.tester = query.value(4).toString();
        run.started = query.value(5).toString();
        run.finished = query.value(6).toString();
        run.notes = query.value(7).toString();
        run.builds = query.value(8).toString();
        list << run;
    }
    return true;
}

bool QaDatabase::createRun(QaRun &run, const QList<qint64> &suiteIds, QString &error, const QString &tag)
{
    // Only the cases that have the tag: as a word of their own among their tags, whatever the capitals.
    QList<qint64> tagged;
    if (!tag.trimmed().isEmpty())
    {
        QSqlQuery cases;
        if (!exec(QStringLiteral("SELECT id, tags FROM cases WHERE project_id = ? AND tags <> ''"), { run.projectId }, error, &cases))
            return false;
        while (cases.next())
            if (tagList(cases.value(1).toString()).contains(tag.trimmed(), Qt::CaseInsensitive))
                tagged << cases.value(0).toLongLong();
    }
    const bool byTag = !tag.trimmed().isEmpty();
    run.name = run.name.trimmed();
    if (run.name.isEmpty())
    {
        error = QStringLiteral("A test run needs a name.");
        return false;
    }
    Transaction transaction(QSqlDatabase::database(m_connection, false));
    run.started = now();
    run.finished.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("INSERT INTO runs (project_id, name, build, tester, started, notes, builds) VALUES (?, ?, ?, ?, ?, ?, ?)"),
              { run.projectId, run.name, run.build.trimmed(), run.tester.trimmed(), run.started, run.notes, run.builds.trimmed() }, error, &query))
        return false;
    const qint64 id = query.lastInsertId().toLongLong();

    // Its cases: those of the suites named, or of every suite of the project.
    int cases = 0;
    if (byTag)
    {
        for (const qint64 caseId : std::as_const(tagged))
        {
            // Of the suites named, if some are.
            if (!exec(QStringLiteral("INSERT INTO results (run_id, case_id) SELECT ?, id FROM cases WHERE id = ? AND project_id = ?"), { id, caseId, run.projectId }, error, &query))
                return false;
            cases += query.numRowsAffected();
        }
        if (!suiteIds.isEmpty())
        {
            QStringList marks;
            QVariantList values { id };
            for (const qint64 suiteId : suiteIds)
            {
                marks << QStringLiteral("?");
                values << suiteId;
            }
            if (!exec(QStringLiteral("DELETE FROM results WHERE run_id = ? AND case_id NOT IN (SELECT id FROM cases WHERE suite_id IN (%1))").arg(marks.join(QLatin1Char(','))),
                      values, error, &query))
                return false;
            cases -= query.numRowsAffected();
        }
    }
    else if (suiteIds.isEmpty())
    {
        if (!exec(QStringLiteral("INSERT INTO results (run_id, case_id) SELECT ?, id FROM cases WHERE project_id = ?"), { id, run.projectId }, error, &query))
            return false;
        cases = query.numRowsAffected();
    }
    for (int i = 0; !byTag && i < suiteIds.size(); ++i)
    {
        const qint64 suiteId = suiteIds.at(i);
        if (!exec(QStringLiteral("INSERT INTO results (run_id, case_id) SELECT ?, id FROM cases WHERE suite_id = ? AND project_id = ?"),
                  { id, suiteId, run.projectId }, error, &query))
            return false;
        cases += query.numRowsAffected();
    }
    if (cases == 0)
    {
        error = byTag ? QStringLiteral("No test case of those suites has the tag \"%1\": the run was not made.").arg(tag.trimmed())
                      : QStringLiteral("There are no test cases to run: the run was not made.");
        return false;
    }
    if (!transaction.commit())
    {
        error = QStringLiteral("The test run could not be stored.");
        return false;
    }
    run.id = id;
    return true;
}

bool QaDatabase::updateRun(const QaRun &run, QString &error)
{
    if (run.name.trimmed().isEmpty())
    {
        error = QStringLiteral("A test run needs a name.");
        return false;
    }
    return exec(QStringLiteral("UPDATE runs SET name = ?, build = ?, tester = ?, notes = ?, finished = ?, builds = ? WHERE id = ?"),
                { run.name.trimmed(), run.build.trimmed(), run.tester.trimmed(), run.notes, run.finished, run.builds.trimmed(), run.id }, error);
}

bool QaDatabase::deleteRun(qint64 id, QString &error)
{
    const QStringList files = attachmentFiles(QStringLiteral("run_id = ?"), { id });
    if (!exec(QStringLiteral("DELETE FROM runs WHERE id = ?"), { id }, error))
        return false;
    removeFiles(files);
    return true;
}

bool QaDatabase::createRerun(QaRun &run, qint64 fromRunId, const QStringList &statuses, QString &error)
{
    run.name = run.name.trimmed();
    if (run.name.isEmpty())
    {
        error = QStringLiteral("A test run needs a name.");
        return false;
    }
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT project_id, build, builds FROM runs WHERE id = ?"), { fromRunId }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That test run is not there any more.");
        return false;
    }
    run.projectId = query.value(0).toLongLong();
    if (run.build.trimmed().isEmpty() && run.builds.trimmed().isEmpty())
    {
        run.build = query.value(1).toString();
        run.builds = query.value(2).toString();
    }

    Transaction transaction(QSqlDatabase::database(m_connection, false));
    run.started = now();
    run.finished.clear();
    if (!exec(QStringLiteral("INSERT INTO runs (project_id, name, build, tester, started, notes, builds) VALUES (?, ?, ?, ?, ?, ?, ?)"),
              { run.projectId, run.name, run.build.trimmed(), run.tester.trimmed(), run.started, run.notes, run.builds.trimmed() }, error, &query))
        return false;
    const qint64 id = query.lastInsertId().toLongLong();
    int cases = 0;
    for (const QString &status : statuses)
    {
        if (!exec(QStringLiteral("INSERT INTO results (run_id, case_id, assigned) SELECT ?, case_id, assigned FROM results WHERE run_id = ? AND status = ?"),
                  { id, fromRunId, status }, error, &query))
            return false;
        cases += query.numRowsAffected();
    }
    if (cases == 0)
    {
        error = QStringLiteral("No test case of that run ended that way: there is nothing to run again.");
        return false;
    }
    if (!transaction.commit())
    {
        error = QStringLiteral("The test run could not be stored.");
        return false;
    }
    run.id = id;
    return true;
}

bool QaDatabase::assign(qint64 runId, const QList<qint64> &caseIds, const QString &tester, QString &error)
{
    Transaction transaction(QSqlDatabase::database(m_connection, false));
    for (const qint64 caseId : caseIds)
    {
        QSqlQuery query;
        if (!exec(QStringLiteral("UPDATE results SET assigned = ? WHERE run_id = ? AND case_id = ?"), { tester.trimmed(), runId, caseId }, error, &query))
            return false;
        if (query.numRowsAffected() != 1)
        {
            error = QStringLiteral("That test case is not part of the run.");
            return false;
        }
    }
    if (!transaction.commit())
    {
        error = QStringLiteral("It could not be stored.");
        return false;
    }
    return true;
}

QStringList QaDatabase::testers(qint64 runId)
{
    QStringList names;
    QString error;
    QSqlQuery query;
    if (exec(QStringLiteral("SELECT name FROM (SELECT assigned AS name, 0 AS place FROM results WHERE run_id = ? AND assigned <> '' "
                            "UNION ALL SELECT tester, 1 FROM results WHERE run_id = ? AND tester <> '' "
                            "UNION ALL SELECT tester, 2 FROM runs WHERE id = ? AND tester <> '') ORDER BY place, name"), { runId, runId, runId }, error, &query))
        while (query.next())
            if (!names.contains(query.value(0).toString(), Qt::CaseInsensitive))
                names << query.value(0).toString();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString QaDatabase::lastBuilds(qint64 projectId)
{
    QString error;
    QSqlQuery query;
    if (exec(QStringLiteral("SELECT builds FROM runs WHERE project_id = ? AND builds <> '' ORDER BY started DESC, id DESC LIMIT 1"), { projectId }, error, &query) && query.next())
        return query.value(0).toString();
    return QString();
}

// ---- files that go with a result -------------------------------------------------------------------

QString QaDatabase::attachmentsFolder() const
{
    return m_path.isEmpty() || m_path == QLatin1String(":memory:") ? QString() : QFileInfo(m_path).absoluteDir().absoluteFilePath(QStringLiteral("attachments"));
}

QString QaDatabase::attachmentPath(const QaAttachment &attachment) const
{
    const QString folder = attachmentsFolder();
    return folder.isEmpty() || attachment.file.isEmpty() ? QString() : QDir(folder).absoluteFilePath(attachment.file);
}

bool QaDatabase::attach(qint64 runId, qint64 caseId, const QString &sourceFile, const QString &name, QaAttachment &attachment, QString &error)
{
    attachment = QaAttachment();
    const QString folder = attachmentsFolder();
    if (folder.isEmpty())
    {
        error = QStringLiteral("Files are kept beside the database, and this one is not a file.");
        return false;
    }
    const QFileInfo source(sourceFile);
    if (!source.isFile())
    {
        error = QStringLiteral("There is no file %1.").arg(QDir::toNativeSeparators(sourceFile));
        return false;
    }
    if (source.size() > 50LL * 1024 * 1024)
    {
        error = QStringLiteral("%1 is larger than 50 MB: too large to keep with a result.").arg(source.fileName());
        return false;
    }
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT 1 FROM results WHERE run_id = ? AND case_id = ?"), { runId, caseId }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That test case is not part of the run.");
        return false;
    }

    const QString called = name.trimmed().isEmpty() ? source.fileName() : name.trimmed();
    const QString relative = QStringLiteral("%1/%2-%3-%4").arg(runId).arg(caseId).arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8), safeName(called));
    const QString target = QDir(folder).absoluteFilePath(relative);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(sourceFile, target))
    {
        error = QStringLiteral("The file could not be copied to %1. Everybody who attaches files needs to be allowed to write there.")
                    .arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath()));
        return false;
    }
    const QString when = now();
    if (!exec(QStringLiteral("INSERT INTO attachments (run_id, case_id, name, file, added, added_by) VALUES (?, ?, ?, ?, ?, ?)"),
              { runId, caseId, called, relative, when, m_user }, error, &query))
    {
        QFile::remove(target);
        return false;
    }
    attachment.id = query.lastInsertId().toLongLong();
    attachment.runId = runId;
    attachment.caseId = caseId;
    attachment.name = called;
    attachment.file = relative;
    attachment.added = when;
    attachment.addedBy = m_user;
    return true;
}

bool QaDatabase::attachments(qint64 runId, qint64 caseId, QList<QaAttachment> &list, QString &error)
{
    list.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT id, run_id, case_id, name, file, added, added_by FROM attachments WHERE run_id = ? AND case_id = ? ORDER BY id"),
              { runId, caseId }, error, &query))
        return false;
    while (query.next())
    {
        QaAttachment attachment;
        attachment.id = query.value(0).toLongLong();
        attachment.runId = query.value(1).toLongLong();
        attachment.caseId = query.value(2).toLongLong();
        attachment.name = query.value(3).toString();
        attachment.file = query.value(4).toString();
        attachment.added = query.value(5).toString();
        attachment.addedBy = query.value(6).toString();
        list << attachment;
    }
    return true;
}

bool QaDatabase::removeAttachment(qint64 id, QString &error)
{
    const QStringList files = attachmentFiles(QStringLiteral("id = ?"), { id });
    if (!exec(QStringLiteral("DELETE FROM attachments WHERE id = ?"), { id }, error))
        return false;
    removeFiles(files);
    return true;
}

QStringList QaDatabase::attachmentFiles(const QString &where, const QVariantList &values)
{
    QStringList files;
    QString error;
    QSqlQuery query;
    if (exec(QStringLiteral("SELECT file FROM attachments WHERE %1").arg(where), values, error, &query))
        while (query.next())
            files << query.value(0).toString();
    return files;
}

void QaDatabase::removeFiles(const QStringList &files)
{
    const QString folder = attachmentsFolder();
    if (folder.isEmpty())
        return;
    const QDir dir(folder);
    for (const QString &file : files)
    {
        // Only what is inside the folder, whatever a row says.
        const QString path = QDir::cleanPath(dir.absoluteFilePath(file));
        if (!file.isEmpty() && path.startsWith(QDir::cleanPath(folder) + QLatin1Char('/')))
        {
            QFile::remove(path);
            dir.rmdir(QFileInfo(path).absolutePath());      // a run's folder, once it is empty
        }
    }
}

bool QaDatabase::results(qint64 runId, QList<QaResult> &list, QString &error)
{
    list.clear();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT %1 FROM results r JOIN cases c ON c.id = r.case_id JOIN suites s ON s.id = c.suite_id "
                             "WHERE r.run_id = ? ORDER BY s.position, s.id, c.key, c.id").arg(QLatin1String(kResultColumns)), { runId }, error, &query))
        return false;
    while (query.next())
        list << resultOf(query);
    return true;
}

bool QaDatabase::setResult(qint64 runId, qint64 caseId, const QString &status, const QString &notes, int failedStep, const QString &tester, QString &error)
{
    if (!statuses().contains(status))
    {
        error = QStringLiteral("\"%1\" is no result a test case can have.").arg(status);
        return false;
    }
    // "Not run" takes back when and by whom; a step fails only in a failed case.
    const bool run = status != notRun();
    QSqlQuery query;
    // ... and an issue belongs to what failed or was blocked.
    const bool bad = status == failed() || status == blocked();
    if (!exec(QStringLiteral("UPDATE results SET status = ?, notes = ?, failed_step = ?, tester = ?, executed = ?, defect = CASE WHEN ? = 1 THEN defect ELSE '' END "
                             "WHERE run_id = ? AND case_id = ?"),
              { status, notes, status == failed() ? qMax(0, failedStep) : 0, run ? tester.trimmed() : QString(), run ? now() : QString(), bad ? 1 : 0, runId, caseId },
              error, &query))
        return false;
    if (query.numRowsAffected() != 1)
    {
        error = QStringLiteral("That test case is not part of the run.");
        return false;
    }
    return true;
}

bool QaDatabase::setDefect(qint64 runId, qint64 caseId, const QString &defect, QString &error)
{
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT status FROM results WHERE run_id = ? AND case_id = ?"), { runId, caseId }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That test case is not part of the run.");
        return false;
    }
    const QString status = query.value(0).toString();
    if (!defect.trimmed().isEmpty() && status != failed() && status != blocked())
    {
        error = QStringLiteral("An issue belongs to a result that failed or was blocked: this one is \"%1\".").arg(status);
        return false;
    }
    return exec(QStringLiteral("UPDATE results SET defect = ? WHERE run_id = ? AND case_id = ?"), { defect.simplified(), runId, caseId }, error);
}

QString QaDatabase::defectUrl(const QString &issueUrl, const QString &defect)
{
    QString reference = defect.trimmed();
    if (reference.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) || reference.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        return reference.contains(QLatin1Char(' ')) ? QString() : reference;
    while (reference.startsWith(QLatin1Char('#')))
        reference.remove(0, 1);
    const QString where = issueUrl.trimmed();
    // Only what can be part of an address: a number, a key like "FI-12".
    static const QString allowed = QStringLiteral("-_.");
    for (const QChar character : reference)
        if (!character.isLetterOrNumber() && !allowed.contains(character))
            return QString();
    if (reference.isEmpty() || !(where.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) || where.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)))
        return QString();
    if (where.contains(QLatin1String("%1")))
        return QString(where).replace(QLatin1String("%1"), reference);
    return where + (where.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + reference;
}

int QaRunStanding::passRate() const
{
    const int verdicts = counts.passed + counts.failed + counts.blocked;
    return verdicts == 0 ? -1 : counts.passed * 100 / verdicts;
}

bool QaDatabase::dashboard(qint64 projectId, QaDashboard &standing, QString &error)
{
    standing = QaDashboard();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT COUNT(*) FROM cases WHERE project_id = ?"), { projectId }, error, &query))
        return false;
    if (query.next())
        standing.cases = query.value(0).toInt();

    // The runs, the oldest first: how it went over time.
    QList<QaRun> all;
    if (!runs(projectId, all, error))
        return false;
    for (int i = int(all.size()) - 1; i >= 0; --i)
    {
        QaRunStanding one;
        one.run = all.at(i);
        if (!summary(one.run.id, one.counts, error))
            return false;
        standing.runs << one;
    }

    // What is wrong now: the cases whose newest result failed or was blocked - by issue, those without one last.
    const QString newest = QStringLiteral("(SELECT r2.run_id FROM results r2 WHERE r2.case_id = c.id AND r2.executed <> '' ORDER BY r2.executed DESC, r2.run_id DESC LIMIT 1)");
    if (!exec(QStringLiteral("SELECT c.id, c.key, c.title, r.status, r.defect, r.notes, n.name, r.tester, r.executed "
                             "FROM cases c JOIN results r ON r.case_id = c.id JOIN runs n ON n.id = r.run_id "
                             "WHERE c.project_id = ? AND r.run_id = %1 AND r.status IN ('Failed', 'Blocked') "
                             "ORDER BY CASE WHEN r.defect = '' THEN 1 ELSE 0 END, LOWER(r.defect), c.key").arg(newest), { projectId }, error, &query))
        return false;
    while (query.next())
    {
        QaOpenFailure failure;
        failure.caseId = query.value(0).toLongLong();
        failure.key = query.value(1).toString();
        failure.title = query.value(2).toString();
        failure.status = query.value(3).toString();
        failure.defect = query.value(4).toString();
        failure.notes = query.value(5).toString();
        failure.runName = query.value(6).toString();
        failure.tester = query.value(7).toString();
        failure.executed = query.value(8).toString();
        standing.open << failure;
    }

    // What keeps going wrong: failed or blocked in two runs or more.
    if (!exec(QStringLiteral("SELECT c.id, c.key, c.title, s.name, SUM(CASE WHEN r.status IN ('Failed', 'Blocked') THEN 1 ELSE 0 END) AS bad, COUNT(*) AS ran, "
                             "(SELECT r3.status FROM results r3 WHERE r3.case_id = c.id AND r3.run_id = %1) "
                             "FROM results r JOIN cases c ON c.id = r.case_id JOIN suites s ON s.id = c.suite_id "
                             "WHERE c.project_id = ? AND r.executed <> '' GROUP BY c.id HAVING bad >= 2 ORDER BY bad DESC, c.key").arg(newest), { projectId }, error, &query))
        return false;
    while (query.next())
    {
        QaCaseStanding one;
        one.caseId = query.value(0).toLongLong();
        one.key = query.value(1).toString();
        one.title = query.value(2).toString();
        one.suite = query.value(3).toString();
        one.bad = query.value(4).toInt();
        one.ran = query.value(5).toInt();
        one.lastStatus = query.value(6).toString();
        standing.failing << one;
    }

    // What nobody has ever run.
    if (!exec(QStringLiteral("SELECT c.id, c.key, c.title, s.name FROM cases c JOIN suites s ON s.id = c.suite_id "
                             "WHERE c.project_id = ? AND NOT EXISTS (SELECT 1 FROM results r WHERE r.case_id = c.id AND r.executed <> '') "
                             "ORDER BY s.position, s.id, c.key"), { projectId }, error, &query))
        return false;
    while (query.next())
    {
        QaCaseStanding one;
        one.caseId = query.value(0).toLongLong();
        one.key = query.value(1).toString();
        one.title = query.value(2).toString();
        one.suite = query.value(3).toString();
        standing.neverRun << one;
    }
    return true;
}

bool QaDatabase::summary(qint64 runId, QaSummary &counts, QString &error)
{
    counts = QaSummary();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT status, COUNT(*) FROM results WHERE run_id = ? GROUP BY status"), { runId }, error, &query))
        return false;
    while (query.next())
    {
        const QString status = query.value(0).toString();
        const int count = query.value(1).toInt();
        counts.total += count;
        if (status == passed())
            counts.passed += count;
        else if (status == failed())
            counts.failed += count;
        else if (status == blocked())
            counts.blocked += count;
        else if (status == skipped())
            counts.skipped += count;
        else
            counts.notRun += count;
    }
    return true;
}

// ---- test scripts as a file -----------------------------------------------------------------------

namespace
{
    // A file of test scripts, read and tidied - what the database would hold of it.
    struct ScriptCase
    {
        QaCase testCase;        // key, title, priority, area, preconditions, notes, tags, steps
        bool   saysTags = false;
    };
    struct ScriptSuite
    {
        QString name;
        QString description;
        QList<ScriptCase> cases;
    };
    struct Scripts
    {
        QString project;
        QString description;
        QString components;
        QString issueUrl;
        QString version;
        QList<ScriptSuite> suites;
    };

    // The steps that say something, tidied.
    QList<QaStep> tidied(const QList<QaStep> &steps)
    {
        QList<QaStep> said;
        for (const QaStep &step : steps)
            if (!step.action.trimmed().isEmpty() || !step.expected.trimmed().isEmpty())
                said << QaStep { step.action.trimmed(), step.expected.trimmed() };
        return said;
    }

    bool readScripts(const QJsonObject &json, Scripts &scripts, QString &error)
    {
        scripts = Scripts();
        scripts.project = json.value(QLatin1String("project")).toString().trimmed();
        if (scripts.project.isEmpty())
        {
            error = QStringLiteral("The file names no project (\"project\"): it is not a file of test scripts.");
            return false;
        }
        scripts.description = json.value(QLatin1String("description")).toString();
        // What the project is made of ("components": [ "Server", "Desktop app" ]), where the file says.
        QStringList parts;
        for (const QJsonValue &part : json.value(QLatin1String("components")).toArray())
            parts << part.toString();
        scripts.components = QaDatabase::componentList(parts.join(QLatin1Char(','))).join(QStringLiteral(", "));
        scripts.issueUrl = json.value(QLatin1String("issueUrl")).toString().trimmed();
        // A version may be written as a text or as a number.
        const QJsonValue version = json.value(QLatin1String("version"));
        scripts.version = version.isDouble() ? QString::number(version.toDouble()) : version.toString().trimmed();

        for (const QJsonValue &suiteValue : json.value(QLatin1String("suites")).toArray())
        {
            const QJsonObject suiteJson = suiteValue.toObject();
            ScriptSuite suite;
            suite.name = suiteJson.value(QLatin1String("name")).toString().trimmed();
            suite.description = suiteJson.value(QLatin1String("description")).toString();
            if (suite.name.isEmpty())
            {
                error = QStringLiteral("A suite of the file has no name. Nothing was imported.");
                return false;
            }
            for (const QJsonValue &caseValue : suiteJson.value(QLatin1String("cases")).toArray())
            {
                const QJsonObject caseJson = caseValue.toObject();
                ScriptCase one;
                QaCase &testCase = one.testCase;
                testCase.key = caseJson.value(QLatin1String("key")).toString().trimmed();
                testCase.title = caseJson.value(QLatin1String("title")).toString().trimmed();
                testCase.priority = caseJson.value(QLatin1String("priority")).toString(QStringLiteral("Medium"));
                if (!QaDatabase::priorities().contains(testCase.priority))
                    testCase.priority = QStringLiteral("Medium");
                testCase.area = caseJson.value(QLatin1String("area")).toString();
                testCase.preconditions = caseJson.value(QLatin1String("preconditions")).toString();
                testCase.notes = caseJson.value(QLatin1String("notes")).toString();
                QStringList caseTags;
                for (const QJsonValue &tagValue : caseJson.value(QLatin1String("tags")).toArray())
                    caseTags << tagValue.toString();
                one.saysTags = caseJson.contains(QLatin1String("tags"));
                testCase.tags = QaDatabase::tagList(caseTags.join(QLatin1Char(','))).join(QStringLiteral(", "));
                QList<QaStep> steps;
                for (const QJsonValue &stepValue : caseJson.value(QLatin1String("steps")).toArray())
                    steps << QaStep { stepValue.toObject().value(QLatin1String("action")).toString(), stepValue.toObject().value(QLatin1String("expected")).toString() };
                testCase.steps = tidied(steps);
                if (testCase.key.isEmpty() || testCase.title.isEmpty())
                {
                    error = QStringLiteral("A test case of the suite \"%1\" has no key or no title. Nothing was imported.").arg(suite.name);
                    return false;
                }
                suite.cases << one;
            }
            scripts.suites << suite;
        }
        return true;
    }

    // In what a case that is stored is not as the file says ("" = in nothing). A file
    // that says no tags says nothing about them.
    QStringList whatDiffers(const QaCase &stored, const QString &storedSuite, const ScriptCase &file, const QString &fileSuite)
    {
        QStringList what;
        if (stored.title != file.testCase.title)
            what << QStringLiteral("title");
        if (stored.priority != file.testCase.priority)
            what << QStringLiteral("priority");
        if (stored.area != file.testCase.area)
            what << QStringLiteral("where it is run");
        if (stored.preconditions != file.testCase.preconditions)
            what << QStringLiteral("preconditions");
        if (stored.notes != file.testCase.notes)
            what << QStringLiteral("notes");
        if (file.saysTags && QaDatabase::tagList(stored.tags).join(QStringLiteral(", ")) != file.testCase.tags)
            what << QStringLiteral("tags");
        const QList<QaStep> steps = tidied(stored.steps);
        bool same = steps.size() == file.testCase.steps.size();
        for (int i = 0; same && i < steps.size(); ++i)
            same = steps.at(i).action == file.testCase.steps.at(i).action && steps.at(i).expected == file.testCase.steps.at(i).expected;
        if (!same)
            what << QStringLiteral("steps");
        if (storedSuite != fileSuite)
            what << QStringLiteral("moved from %1 to %2").arg(storedSuite, fileSuite);
        return what;
    }
}

int QaDatabase::compareVersions(const QString &first, const QString &second)
{
    // Numbers by their value, what stands between them as text: "1.10" is after "1.9".
    static const QRegularExpression piece(QStringLiteral("(\\d+|\\D+)"));
    const auto pieces = [](const QString &version) {
        QStringList list;
        QRegularExpressionMatchIterator matches = piece.globalMatch(version.trimmed());
        while (matches.hasNext())
            list << matches.next().captured(1);
        return list;
    };
    const QStringList a = pieces(first), b = pieces(second);
    for (int i = 0; i < qMax(a.size(), b.size()); ++i)
    {
        if (i >= a.size())
            return -1;
        if (i >= b.size())
            return 1;
        bool aNumber = false, bNumber = false;
        const qulonglong aValue = a.at(i).toULongLong(&aNumber), bValue = b.at(i).toULongLong(&bNumber);
        if (aNumber && bNumber)
        {
            if (aValue != bValue)
                return aValue < bValue ? -1 : 1;
            continue;
        }
        const int order = a.at(i).compare(b.at(i), Qt::CaseInsensitive);
        if (order != 0)
            return order < 0 ? -1 : 1;
    }
    return 0;
}

int QaImportPreview::versionOrder() const
{
    return fileVersion.isEmpty() || databaseVersion.isEmpty() ? 0 : QaDatabase::compareVersions(fileVersion, databaseVersion);
}

QString QaImportPreview::summary() const
{
    QStringList parts;
    if (!added.isEmpty())
        parts << QStringLiteral("%1 new").arg(added.size());
    if (!changed.isEmpty())
        parts << QStringLiteral("%1 changed").arg(changed.size());
    parts << QStringLiteral("%1 unchanged").arg(unchanged);
    QString text = parts.join(QStringLiteral(", "));
    if (!missing.isEmpty())
        text += QStringLiteral("; %1 not in the file").arg(missing.size() == 1 ? QStringLiteral("1 is") : QStringLiteral("%1 are").arg(missing.size()));
    return text + QLatin1Char('.');
}

bool QaDatabase::previewImport(const QJsonObject &json, QaImportPreview &preview, QString &error)
{
    preview = QaImportPreview();
    Scripts scripts;
    if (!readScripts(json, scripts, error))
        return false;
    preview.project = scripts.project;
    preview.fileVersion = scripts.version;

    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT id, scripts_version FROM projects WHERE name = ?"), { scripts.project }, error, &query))
        return false;
    qint64 projectId = 0;
    if (query.next())
    {
        projectId = query.value(0).toLongLong();
        preview.databaseVersion = query.value(1).toString();
    }
    preview.newProject = projectId == 0;

    QStringList inFile;
    for (const ScriptSuite &suite : std::as_const(scripts.suites))
    {
        bool suiteThere = false;
        if (projectId != 0)
        {
            if (!exec(QStringLiteral("SELECT 1 FROM suites WHERE project_id = ? AND name = ?"), { projectId, suite.name }, error, &query))
                return false;
            suiteThere = query.next();
        }
        if (!suiteThere && !preview.newSuites.contains(suite.name))
            preview.newSuites << suite.name;
        for (const ScriptCase &one : suite.cases)
        {
            inFile << one.testCase.key;
            qint64 id = 0;
            QString storedSuite;
            if (projectId != 0)
            {
                if (!exec(QStringLiteral("SELECT c.id, s.name FROM cases c JOIN suites s ON s.id = c.suite_id WHERE c.project_id = ? AND c.key = ?"),
                          { projectId, one.testCase.key }, error, &query))
                    return false;
                if (query.next())
                {
                    id = query.value(0).toLongLong();
                    storedSuite = query.value(1).toString();
                }
            }
            if (id == 0)
            {
                preview.added << QaCaseChange { one.testCase.key, one.testCase.title, suite.name, {} };
                continue;
            }
            QaCase stored;
            if (!loadCase(id, stored, error))
                return false;
            const QStringList what = whatDiffers(stored, storedSuite, one, suite.name);
            if (what.isEmpty())
                ++preview.unchanged;
            else
                preview.changed << QaCaseChange { one.testCase.key, one.testCase.title, suite.name, what };
        }
    }
    // What the database has of the project and the file does not.
    if (projectId != 0)
    {
        if (!exec(QStringLiteral("SELECT c.key, c.title, s.name FROM cases c JOIN suites s ON s.id = c.suite_id WHERE c.project_id = ? ORDER BY s.position, s.id, c.key"),
                  { projectId }, error, &query))
            return false;
        while (query.next())
            if (!inFile.contains(query.value(0).toString()))
                preview.missing << QaCaseChange { query.value(0).toString(), query.value(1).toString(), query.value(2).toString(), {} };
    }
    return true;
}

bool QaDatabase::importJson(const QJsonObject &json, QaImportCounts &counts, QString &error, bool deleteMissing)
{
    counts = QaImportCounts();
    Scripts scripts;
    if (!readScripts(json, scripts, error))
        return false;
    if (!isOpen())
    {
        error = QStringLiteral("No database is open.");
        return false;
    }

    QStringList files;      // of the results of cases that go: removed once everything is stored
    {
        // All of the file, or none of it.
        Transaction transaction(QSqlDatabase::database(m_connection, false));
        QSqlQuery query;

        qint64 projectId = 0;
        if (!exec(QStringLiteral("SELECT id FROM projects WHERE name = ?"), { scripts.project }, error, &query))
            return false;
        if (query.next())
        {
            projectId = query.value(0).toLongLong();
            // What the file says the project is, where it says something.
            if (!scripts.description.isEmpty() && !exec(QStringLiteral("UPDATE projects SET description = ? WHERE id = ?"), { scripts.description, projectId }, error))
                return false;
            if (!scripts.components.isEmpty() && !exec(QStringLiteral("UPDATE projects SET components = ? WHERE id = ?"), { scripts.components, projectId }, error))
                return false;
            if (!scripts.issueUrl.isEmpty() && !exec(QStringLiteral("UPDATE projects SET issue_url = ? WHERE id = ?"), { scripts.issueUrl, projectId }, error))
                return false;
        }
        else
        {
            QaProject project;
            project.name = scripts.project;
            project.description = scripts.description;
            project.components = scripts.components;
            project.issueUrl = scripts.issueUrl;
            if (!addProject(project, error))
                return false;
            projectId = project.id;
            ++counts.projects;
        }
        // Which scripts the project is now up to date with.
        if (!scripts.version.isEmpty() && !exec(QStringLiteral("UPDATE projects SET scripts_version = ? WHERE id = ?"), { scripts.version, projectId }, error))
            return false;

        QStringList inFile;
        for (const ScriptSuite &fromFile : std::as_const(scripts.suites))
        {
            QaSuite suite;
            suite.projectId = projectId;
            suite.name = fromFile.name;
            suite.description = fromFile.description;
            if (!exec(QStringLiteral("SELECT id FROM suites WHERE project_id = ? AND name = ?"), { projectId, suite.name }, error, &query))
                return false;
            if (query.next())
            {
                suite.id = query.value(0).toLongLong();
                if (!suite.description.isEmpty() && !updateSuite(suite, error))
                    return false;
            }
            else
            {
                if (!addSuite(suite, error))
                    return false;
                ++counts.suites;
            }

            for (const ScriptCase &one : fromFile.cases)
            {
                QaCase testCase = one.testCase;
                testCase.suiteId = suite.id;
                inFile << testCase.key;

                // The case of that key, wherever in the project it is: it moves to the suite the file says.
                if (!exec(QStringLiteral("SELECT c.id, s.name FROM cases c JOIN suites s ON s.id = c.suite_id WHERE c.project_id = ? AND c.key = ?"),
                          { projectId, testCase.key }, error, &query))
                    return false;
                const bool known = query.next();
                if (known)
                {
                    testCase.id = query.value(0).toLongLong();
                    // One that is as the file says is left alone: nothing of it changes, and whoever has it open is not disturbed.
                    const QString storedSuite = query.value(1).toString();
                    QaCase stored;
                    if (!loadCase(testCase.id, stored, error))
                        return false;
                    if (whatDiffers(stored, storedSuite, one, suite.name).isEmpty())
                    {
                        ++counts.casesUnchanged;
                        continue;
                    }
                }

                // saveCase has a transaction of its own inside this one: SQLite then takes its begin as said
                // already and its commit as ours - so its work is done by hand here.
                const bool stored = known
                    ? exec(QStringLiteral("UPDATE cases SET suite_id = ?, title = ?, priority = ?, area = ?, preconditions = ?, notes = ?, updated = ?, "
                                          "revision = revision + 1, changed_by = '' WHERE id = ?"),
                           { testCase.suiteId, testCase.title, testCase.priority, testCase.area, testCase.preconditions, testCase.notes, now(), testCase.id }, error)
                    : exec(QStringLiteral("INSERT INTO cases (suite_id, project_id, key, title, priority, area, preconditions, notes, updated) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                           { testCase.suiteId, projectId, testCase.key, testCase.title, testCase.priority, testCase.area, testCase.preconditions, testCase.notes, now() },
                           error, &query);
                if (!stored)
                    return false;
                if (!known)
                    testCase.id = query.lastInsertId().toLongLong();
                // Its tags, where the file says any: one that says none leaves those the case has.
                if (one.saysTags && !exec(QStringLiteral("UPDATE cases SET tags = ? WHERE id = ?"), { testCase.tags, testCase.id }, error))
                    return false;
                if (!exec(QStringLiteral("DELETE FROM steps WHERE case_id = ?"), { testCase.id }, error))
                    return false;
                int position = 0;
                for (const QaStep &step : std::as_const(testCase.steps))
                    if (!exec(QStringLiteral("INSERT INTO steps (case_id, position, action, expected) VALUES (?, ?, ?, ?)"),
                              { testCase.id, ++position, step.action, step.expected }, error))
                        return false;
                ++(known ? counts.casesUpdated : counts.casesAdded);
            }
        }

        // What the file does not have: only when that was asked for.
        if (deleteMissing)
        {
            QList<qint64> gone;
            if (!exec(QStringLiteral("SELECT id, key FROM cases WHERE project_id = ?"), { projectId }, error, &query))
                return false;
            while (query.next())
                if (!inFile.contains(query.value(1).toString()))
                    gone << query.value(0).toLongLong();
            for (const qint64 id : std::as_const(gone))
            {
                files << attachmentFiles(QStringLiteral("case_id = ?"), { id });
                if (!exec(QStringLiteral("DELETE FROM cases WHERE id = ?"), { id }, error))
                    return false;
                ++counts.casesDeleted;
            }
        }

        if (!transaction.commit())
        {
            error = QStringLiteral("The test scripts could not be stored. Nothing was imported.");
            return false;
        }
    }
    removeFiles(files);
    return true;
}

bool QaDatabase::exportJson(qint64 projectId, QJsonObject &scripts, QString &error)
{
    scripts = QJsonObject();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT name, description, components, issue_url, scripts_version FROM projects WHERE id = ?"), { projectId }, error, &query))
        return false;
    if (!query.next())
    {
        error = QStringLiteral("That project is not there any more.");
        return false;
    }
    scripts.insert(QStringLiteral("project"), query.value(0).toString());
    scripts.insert(QStringLiteral("description"), query.value(1).toString());
    if (!componentList(query.value(2).toString()).isEmpty())
        scripts.insert(QStringLiteral("components"), QJsonArray::fromStringList(componentList(query.value(2).toString())));
    if (!query.value(3).toString().isEmpty())
        scripts.insert(QStringLiteral("issueUrl"), query.value(3).toString());
    if (!query.value(4).toString().isEmpty())
        scripts.insert(QStringLiteral("version"), query.value(4).toString());

    QList<QaSuite> suiteList;
    if (!suites(projectId, suiteList, error))
        return false;
    QJsonArray suitesJson;
    for (const QaSuite &suite : std::as_const(suiteList))
    {
        QList<QaCase> caseList;
        if (!cases(suite.id, caseList, error))
            return false;
        QJsonArray casesJson;
        for (const QaCase &listed : std::as_const(caseList))
        {
            QaCase testCase;
            if (!loadCase(listed.id, testCase, error))
                return false;
            QJsonArray stepsJson;
            for (const QaStep &step : std::as_const(testCase.steps))
                stepsJson << QJsonObject { { QStringLiteral("action"), step.action }, { QStringLiteral("expected"), step.expected } };
            QJsonObject caseJson {
                { QStringLiteral("key"), testCase.key }, { QStringLiteral("title"), testCase.title }, { QStringLiteral("priority"), testCase.priority },
                { QStringLiteral("area"), testCase.area }, { QStringLiteral("preconditions"), testCase.preconditions }, { QStringLiteral("notes"), testCase.notes },
                { QStringLiteral("steps"), stepsJson },
            };
            if (!tagList(testCase.tags).isEmpty())
                caseJson.insert(QStringLiteral("tags"), QJsonArray::fromStringList(tagList(testCase.tags)));
            casesJson << caseJson;
        }
        suitesJson << QJsonObject { { QStringLiteral("name"), suite.name }, { QStringLiteral("description"), suite.description }, { QStringLiteral("cases"), casesJson } };
    }
    scripts.insert(QStringLiteral("suites"), suitesJson);
    return true;
}
