#ifndef QABACKUP_H
#define QABACKUP_H

#include <QDate>
#include <QString>
#include <QStringList>

class QaDatabase;

// A copy of the database a day, kept beside it.
//
// A database on a shared drive is one file that everybody writes to over the
// network: if it is ever damaged, or somebody deletes what was needed, the
// copy of the day before is what is left. The first program that opens the
// database on a day makes that day's copy:
//
//   <the database's folder>\backups\<its name>-2026-10-09.sqlite
//
// - complete in itself, made by SQLite while others go on working - and the
// oldest copies beyond those to keep are deleted. A copy that is there is
// never written over, and none is made of a database that is not sound (the
// good copies are then what matters, and stay).
//
// To go back to a copy: close the program everywhere, and put the copy in
// the database's place under the database's name.
struct QaBackupOutcome
{
    bool    made = false;       // today's copy was made now
    QString file;               // today's copy (made now, or there already)
    int     removed = 0;        // old copies deleted
    QString problem;            // why no copy was made ("" = nothing wrong)
};

namespace QaBackup
{
    // Where a database's copies are: the folder said, or "backups" beside it.
    QString folderFor(const QString &databasePath, const QString &folder = QString());
    // What that day's copy is called: "qatest-2026-10-09.sqlite".
    QString fileNameFor(const QString &databasePath, const QDate &day);
    // The copies there are, the newest first (full paths).
    QStringList copies(const QString &databasePath, const QString &folder = QString());

    // Makes that day's copy unless it is there, and deletes the oldest beyond
    // `keep` (0 = keep no copies: nothing is made and nothing deleted).
    QaBackupOutcome daily(QaDatabase &database, const QDate &day, int keep, const QString &folder = QString());
}

#endif // QABACKUP_H
