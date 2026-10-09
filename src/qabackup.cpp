#include "qabackup.h"

#include "qadatabase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

QString QaBackup::folderFor(const QString &databasePath, const QString &folder)
{
    return folder.trimmed().isEmpty() ? QFileInfo(databasePath).absoluteDir().absoluteFilePath(QStringLiteral("backups")) : QDir::cleanPath(folder.trimmed());
}

QString QaBackup::fileNameFor(const QString &databasePath, const QDate &day)
{
    return QStringLiteral("%1-%2.sqlite").arg(QFileInfo(databasePath).completeBaseName(), day.toString(QStringLiteral("yyyy-MM-dd")));
}

QStringList QaBackup::copies(const QString &databasePath, const QString &folder)
{
    // Only what is called as a copy of this database is: nothing else in the folder is ever touched.
    const QRegularExpression name(QStringLiteral("^%1-\\d{4}-\\d{2}-\\d{2}\\.sqlite$").arg(QRegularExpression::escape(QFileInfo(databasePath).completeBaseName())),
                                  QRegularExpression::CaseInsensitiveOption);
    const QDir dir(folderFor(databasePath, folder));
    QStringList found;
    for (const QString &file : dir.entryList(QDir::Files, QDir::Name | QDir::Reversed))
        if (name.match(file).hasMatch())
            found << dir.absoluteFilePath(file);
    return found;
}

QaBackupOutcome QaBackup::daily(QaDatabase &database, const QDate &day, int keep, const QString &folder)
{
    QaBackupOutcome outcome;
    const QString path = database.path();
    if (keep <= 0 || path.isEmpty() || path == QLatin1String(":memory:"))
        return outcome;

    const QDir dir(folderFor(path, folder));
    outcome.file = dir.absoluteFilePath(fileNameFor(path, day));
    if (!QFileInfo::exists(outcome.file))
    {
        QString problem;
        if (!database.sound(problem))
        {
            outcome.problem = QStringLiteral("No copy was made today: the database is not sound (%1).").arg(problem);
            return outcome;
        }
        // Under another name first: a copy that is half written is never taken for one, and two
        // PCs that start at the same moment do not write into each other's.
        const QString making = dir.absoluteFilePath(QStringLiteral("making-%1.tmp").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!database.copyTo(making, problem))
        {
            QFile::remove(making);
            outcome.problem = QStringLiteral("No copy was made today, in %1: %2").arg(QDir::toNativeSeparators(dir.absolutePath()), problem);
            return outcome;
        }
        if (QFile::rename(making, outcome.file))
            outcome.made = true;
        else
            QFile::remove(making);      // somebody else's copy of today got there first
        if (!QFileInfo::exists(outcome.file))
        {
            outcome.problem = QStringLiteral("No copy was made today: %1 could not be written.").arg(QDir::toNativeSeparators(outcome.file));
            return outcome;
        }
    }

    // The oldest beyond those to keep go.
    const QStringList there = copies(path, folder);
    for (int i = keep; i < there.size(); ++i)
        if (QFile::remove(there.at(i)))
            ++outcome.removed;
    return outcome;
}
