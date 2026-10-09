#ifndef QACONFIG_H
#define QACONFIG_H

#include <QString>

// The program's configuration file: QATest.ini - beside the program or, when
// none is there, in the user's application data folder (main.cpp).
//
//   [Database]
//   ; Where the database is. A file on a shared drive is one database for
//   ; everybody who has this line:
//   Path=\\fileserver\qa\qatest.sqlite
//   ; How long to wait, in seconds, while somebody else is writing (10).
//   BusyTimeoutSeconds=10
//
//   [Backup]
//   ; How many daily copies of the database to keep (14; 0 = make none),
//   ; and where, if not in the folder "backups" beside the database.
//   Keep=14
//   Folder=D:\QA backups
//
// A plain text file, read as it is written: a backslash is a backslash, so
// a network path needs no doubling, and a value may stand in quotation
// marks. Lines that start with ; or # are comments. A path that is not
// absolute is meant from the folder the file is in. %NAME% in the path
// stands for that variable of the environment (%USERPROFILE%).
//
// The file is optional: without it, or without a Path in it, the program
// finds its database as before.
struct QaConfig
{
    QString file;                   // the file that was read ("" = there is none)
    QString databasePath;           // absolute; "" = the file says none
    int     busyTimeoutSeconds = 10;
    int     backupKeep = 14;        // daily copies to keep (qabackup.h); 0 = make none
    QString backupFolder;           // absolute; "" = "backups" beside the database
    QString problem;                // what is wrong with the file ("" = nothing)
};

namespace QaConfigFile
{
    // What the program calls its file.
    QString fileName();             // "QATest.ini"

    // Reads that file. One that is not there is no problem: an empty
    // configuration. One that cannot be read, or says what makes no sense,
    // says so in `problem` - and what it does say right still counts.
    QaConfig read(const QString &path);

    // The same from the file's text; `folder` is where relative paths start.
    QaConfig parse(const QString &text, const QString &folder);

    // A file to start from, with every setting explained and none set.
    QString sample();

    // A file's text with the database's path set to that - written as it is
    // given - or, for an empty one, with none set (the line stays, as a
    // comment). Everything else of the file is left as it is: its comments,
    // its other settings. A file without the line, or without the section,
    // gets them.
    QString withDatabasePath(const QString &text, const QString &path);
}

#endif // QACONFIG_H
