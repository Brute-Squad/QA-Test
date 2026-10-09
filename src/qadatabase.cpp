#include "qadatabase.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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
        return result;
    }

    const char *const kResultColumns =
        "r.run_id, r.case_id, s.name, c.key, c.title, c.priority, c.area, r.status, r.notes, r.failed_step, r.tester, r.executed, r.assigned, "
        "(SELECT COUNT(*) FROM attachments a WHERE a.run_id = r.run_id AND a.case_id = r.case_id)";

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
    return text.isEmpty() ? QStringLiteral("Nothing was added: the file has no test cases.") : text + QLatin1Char('.');
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
        && addMissingColumn(QStringLiteral("results"), QStringLiteral("assigned"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("runs"), QStringLiteral("builds"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)
        && addMissingColumn(QStringLiteral("projects"), QStringLiteral("components"), QStringLiteral("TEXT NOT NULL DEFAULT ''"), error);
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
    if (!exec(QStringLiteral("SELECT id, name, description, components FROM projects ORDER BY LOWER(name)"), {}, error, &query))
        return false;
    while (query.next())
        list << QaProject { query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toString() };
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
    if (!exec(QStringLiteral("INSERT INTO projects (name, description, components, created) VALUES (?, ?, ?, ?)"),
              { project.name, project.description, project.components, now() }, error, &query))
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
    if (!exec(QStringLiteral("UPDATE projects SET name = ?, description = ?, components = ? WHERE id = ?"),
              { project.name.trimmed(), project.description, componentList(project.components).join(QStringLiteral(", ")), project.id }, error))
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
                             "COALESCE((SELECT r.status FROM results r WHERE r.case_id = c.id AND r.executed <> '' ORDER BY r.executed DESC LIMIT 1), '') "
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
        list << testCase;
    }
    return true;
}

bool QaDatabase::loadCase(qint64 id, QaCase &testCase, QString &error)
{
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT id, suite_id, key, title, priority, area, preconditions, notes, revision, changed_by, updated FROM cases WHERE id = ?"),
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
        ? exec(QStringLiteral("INSERT INTO cases (suite_id, project_id, key, title, priority, area, preconditions, notes, updated, revision, changed_by) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?)"),
               { testCase.suiteId, projectId, testCase.key, testCase.title, testCase.priority, testCase.area, testCase.preconditions, testCase.notes, when, m_user },
               error, &query)
        : exec(QStringLiteral("UPDATE cases SET suite_id = ?, project_id = ?, key = ?, title = ?, priority = ?, area = ?, preconditions = ?, notes = ?, updated = ?, "
                              "revision = revision + 1, changed_by = ? WHERE id = ? AND (? = 0 OR revision = ?)"),
               { testCase.suiteId, projectId, testCase.key, testCase.title, testCase.priority, testCase.area, testCase.preconditions, testCase.notes, when, m_user,
                 testCase.id, expected, expected }, error, &query);
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
        runNames << query.value(14).toString();
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

bool QaDatabase::createRun(QaRun &run, const QList<qint64> &suiteIds, QString &error)
{
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
    if (suiteIds.isEmpty())
    {
        if (!exec(QStringLiteral("INSERT INTO results (run_id, case_id) SELECT ?, id FROM cases WHERE project_id = ?"), { id, run.projectId }, error, &query))
            return false;
        cases = query.numRowsAffected();
    }
    for (const qint64 suiteId : suiteIds)
    {
        if (!exec(QStringLiteral("INSERT INTO results (run_id, case_id) SELECT ?, id FROM cases WHERE suite_id = ? AND project_id = ?"),
                  { id, suiteId, run.projectId }, error, &query))
            return false;
        cases += query.numRowsAffected();
    }
    if (cases == 0)
    {
        error = QStringLiteral("There are no test cases to run: the run was not made.");
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
    if (!exec(QStringLiteral("UPDATE results SET status = ?, notes = ?, failed_step = ?, tester = ?, executed = ? WHERE run_id = ? AND case_id = ?"),
              { status, notes, status == failed() ? qMax(0, failedStep) : 0, run ? tester.trimmed() : QString(), run ? now() : QString(), runId, caseId }, error, &query))
        return false;
    if (query.numRowsAffected() != 1)
    {
        error = QStringLiteral("That test case is not part of the run.");
        return false;
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

bool QaDatabase::importJson(const QJsonObject &scripts, QaImportCounts &counts, QString &error)
{
    counts = QaImportCounts();
    const QString projectName = scripts.value(QLatin1String("project")).toString().trimmed();
    if (projectName.isEmpty())
    {
        error = QStringLiteral("The file names no project (\"project\"): it is not a file of test scripts.");
        return false;
    }
    if (!isOpen())
    {
        error = QStringLiteral("No database is open.");
        return false;
    }

    // All of the file, or none of it.
    Transaction transaction(QSqlDatabase::database(m_connection, false));
    QSqlQuery query;

    // What the project is made of ("components": [ "Server", "Desktop app" ]), where the file says.
    QStringList parts;
    for (const QJsonValue &part : scripts.value(QLatin1String("components")).toArray())
        parts << part.toString();
    const QString components = componentList(parts.join(QLatin1Char(','))).join(QStringLiteral(", "));

    qint64 projectId = 0;
    if (!exec(QStringLiteral("SELECT id FROM projects WHERE name = ?"), { projectName }, error, &query))
        return false;
    if (query.next())
    {
        projectId = query.value(0).toLongLong();
        // What the file says the project is, where it says something.
        const QString description = scripts.value(QLatin1String("description")).toString();
        if (!description.isEmpty() && !exec(QStringLiteral("UPDATE projects SET description = ? WHERE id = ?"), { description, projectId }, error))
            return false;
        if (!components.isEmpty() && !exec(QStringLiteral("UPDATE projects SET components = ? WHERE id = ?"), { components, projectId }, error))
            return false;
    }
    else
    {
        QaProject project;
        project.name = projectName;
        project.description = scripts.value(QLatin1String("description")).toString();
        project.components = components;
        if (!addProject(project, error))
            return false;
        projectId = project.id;
        ++counts.projects;
    }

    for (const QJsonValue &suiteValue : scripts.value(QLatin1String("suites")).toArray())
    {
        const QJsonObject suiteJson = suiteValue.toObject();
        QaSuite suite;
        suite.projectId = projectId;
        suite.name = suiteJson.value(QLatin1String("name")).toString().trimmed();
        suite.description = suiteJson.value(QLatin1String("description")).toString();
        if (suite.name.isEmpty())
        {
            error = QStringLiteral("A suite of the file has no name. Nothing was imported.");
            return false;
        }
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

        for (const QJsonValue &caseValue : suiteJson.value(QLatin1String("cases")).toArray())
        {
            const QJsonObject caseJson = caseValue.toObject();
            QaCase testCase;
            testCase.suiteId = suite.id;
            testCase.key = caseJson.value(QLatin1String("key")).toString().trimmed();
            testCase.title = caseJson.value(QLatin1String("title")).toString().trimmed();
            testCase.priority = caseJson.value(QLatin1String("priority")).toString(QStringLiteral("Medium"));
            testCase.area = caseJson.value(QLatin1String("area")).toString();
            testCase.preconditions = caseJson.value(QLatin1String("preconditions")).toString();
            testCase.notes = caseJson.value(QLatin1String("notes")).toString();
            for (const QJsonValue &stepValue : caseJson.value(QLatin1String("steps")).toArray())
                testCase.steps << QaStep { stepValue.toObject().value(QLatin1String("action")).toString(),
                                           stepValue.toObject().value(QLatin1String("expected")).toString() };
            if (testCase.key.isEmpty() || testCase.title.isEmpty())
            {
                error = QStringLiteral("A test case of the suite \"%1\" has no key or no title. Nothing was imported.").arg(suite.name);
                return false;
            }

            // The case of that key, wherever in the project it is: it moves to the suite the file says.
            if (!exec(QStringLiteral("SELECT id FROM cases WHERE project_id = ? AND key = ?"), { projectId, testCase.key }, error, &query))
                return false;
            const bool known = query.next();
            if (known)
                testCase.id = query.value(0).toLongLong();

            // saveCase has a transaction of its own inside this one: SQLite then takes its begin as said
            // already and its commit as ours - so its work is done by hand here.
            const bool stored = known
                ? exec(QStringLiteral("UPDATE cases SET suite_id = ?, title = ?, priority = ?, area = ?, preconditions = ?, notes = ?, updated = ?, "
                                    "revision = revision + 1, changed_by = '' WHERE id = ?"),
                       { testCase.suiteId, testCase.title, priorities().contains(testCase.priority) ? testCase.priority : QStringLiteral("Medium"), testCase.area,
                         testCase.preconditions, testCase.notes, now(), testCase.id }, error)
                : exec(QStringLiteral("INSERT INTO cases (suite_id, project_id, key, title, priority, area, preconditions, notes, updated) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                       { testCase.suiteId, projectId, testCase.key, testCase.title, priorities().contains(testCase.priority) ? testCase.priority : QStringLiteral("Medium"),
                         testCase.area, testCase.preconditions, testCase.notes, now() }, error, &query);
            if (!stored)
                return false;
            if (!known)
                testCase.id = query.lastInsertId().toLongLong();
            if (!exec(QStringLiteral("DELETE FROM steps WHERE case_id = ?"), { testCase.id }, error))
                return false;
            int position = 0;
            for (const QaStep &step : std::as_const(testCase.steps))
            {
                if (step.action.trimmed().isEmpty() && step.expected.trimmed().isEmpty())
                    continue;
                if (!exec(QStringLiteral("INSERT INTO steps (case_id, position, action, expected) VALUES (?, ?, ?, ?)"),
                          { testCase.id, ++position, step.action.trimmed(), step.expected.trimmed() }, error))
                    return false;
            }
            ++(known ? counts.casesUpdated : counts.casesAdded);
        }
    }

    if (!transaction.commit())
    {
        error = QStringLiteral("The test scripts could not be stored. Nothing was imported.");
        return false;
    }
    return true;
}

bool QaDatabase::exportJson(qint64 projectId, QJsonObject &scripts, QString &error)
{
    scripts = QJsonObject();
    QSqlQuery query;
    if (!exec(QStringLiteral("SELECT name, description, components FROM projects WHERE id = ?"), { projectId }, error, &query))
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
            casesJson << QJsonObject {
                { QStringLiteral("key"), testCase.key }, { QStringLiteral("title"), testCase.title }, { QStringLiteral("priority"), testCase.priority },
                { QStringLiteral("area"), testCase.area }, { QStringLiteral("preconditions"), testCase.preconditions }, { QStringLiteral("notes"), testCase.notes },
                { QStringLiteral("steps"), stepsJson },
            };
        }
        suitesJson << QJsonObject { { QStringLiteral("name"), suite.name }, { QStringLiteral("description"), suite.description }, { QStringLiteral("cases"), casesJson } };
    }
    scripts.insert(QStringLiteral("suites"), suitesJson);
    return true;
}
