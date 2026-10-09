// QA Test Tracker: test cases, test runs and their results in one SQLite file.
//
//   QATest                              the window, on the database used last
//   QATest --db <file>                  ... on that database (made if it is not there)
//   QATest --import <scripts.json>      read a file of test scripts into the database
//                                       and exit: 0 = imported, 1 = not (the reason is
//                                       printed). With --db: into that database.
//   QATest --where                      print where the database is and why, and exit
//   QATest --config <file>              read that configuration file instead of the
//                                       program's own
//   QATest --write-config               write a QATest.ini to start from beside the
//                                       program, unless one is there, and exit
//   QATest --write-user-config          the same in the user's own folder
//   QATest --set-database <file>        what the installer does with the user's answer:
//                                       write into the configuration file - the user's
//                                       own, or the one named with --config - that the
//                                       database is that file (the user's own database:
//                                       no path is written), make the database if it is
//                                       not there, and give an empty one the test
//                                       scripts that came with the program. With
//                                       --existing the file has to be there. Exits 0, or
//                                       1 with the reason and nothing written.
//
// The configuration file is QATest.ini beside the program or, when none is
// there, QATest.ini in the user's application data folder.
//
// Which database: the one named with --db; else the one the configuration
// file names ([Database] Path - a shared drive, say; qaconfig.h); else the
// one opened last (File > Open Database...); else `qatest.sqlite` beside the
// program or in a folder `data` beside the program's folder; else
// `qatest.sqlite` in the user's application data folder.
#include "mainwindow.h"
#include "qaconfig.h"
#include "qadatabase.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include "qabackup.h"
#include "qashare.h"
#include <QStatusBar>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QATest"));
    QCoreApplication::setApplicationName(QStringLiteral("QATest"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.1"));

    // The icon of every window: the sizes a title bar and a taskbar ask for.
    QIcon icon;
    for (const int size : { 16, 32, 48, 256 })
        icon.addFile(QStringLiteral(":/icons/qatest-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QA Test Tracker: test cases, test runs and their results in one SQLite file."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dbOption(QStringLiteral("db"), QStringLiteral("The database to use (made if it is not there)."), QStringLiteral("file"));
    const QCommandLineOption importOption(QStringLiteral("import"), QStringLiteral("Read a file of test scripts into the database, and exit."), QStringLiteral("json"));
    const QCommandLineOption whereOption(QStringLiteral("where"), QStringLiteral("Print where the database is, and exit."));
    // (For the build's own check: make the window, show nothing, exit.)
    const QCommandLineOption smokeOption(QStringLiteral("smoke"), QStringLiteral("Make the window and exit: a check that the program starts."));
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("The configuration file to read instead of QATest.ini beside the program."),
                                          QStringLiteral("file"));
    const QCommandLineOption writeConfigOption(QStringLiteral("write-config"),
                                               QStringLiteral("Write a QATest.ini to start from beside the program, unless one is there, and exit."));
    const QCommandLineOption writeUserConfigOption(QStringLiteral("write-user-config"),
                                                   QStringLiteral("Write a QATest.ini to start from into the user's own folder, unless one is there, and exit."));
    const QCommandLineOption setDatabaseOption(QStringLiteral("set-database"),
                                               QStringLiteral("Write into the configuration file where the database is, and make it if it is not there."),
                                               QStringLiteral("file"));
    const QCommandLineOption existingOption(QStringLiteral("existing"), QStringLiteral("With --set-database: the database has to be there already."));
    const QCommandLineOption networkNameOption(QStringLiteral("network-name"),
                                               QStringLiteral("Print a path on a connected drive under its network name (\\\\server\\share\\...), and exit."),
                                               QStringLiteral("path"));
    parser.addOptions({ dbOption, importOption, whereOption, configOption, writeConfigOption, writeUserConfigOption, setDatabaseOption, existingOption,
                        networkNameOption, smokeOption });
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    if (parser.isSet(networkNameOption))
    {
        const QString network = QaShare::networkName(parser.value(networkNameOption));
        out << QDir::toNativeSeparators(network.isEmpty() ? parser.value(networkNameOption) : network) << Qt::endl;
        return 0;
    }

    // The configuration file: the one named; else the one beside the program; else the
    // user's own - which is where an installed program has it, since a new version
    // replaces the program's folder.
    const QString beside = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QaConfigFile::fileName());
    const QString users = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + QaConfigFile::fileName();
    const QString configFile = parser.isSet(configOption) ? QDir::current().absoluteFilePath(parser.value(configOption))
                               : QFileInfo::exists(beside) ? beside : users;
    if (parser.isSet(writeConfigOption) || parser.isSet(writeUserConfigOption))
    {
        const QString target = parser.isSet(writeUserConfigOption) ? users : parser.isSet(configOption) ? configFile : beside;
        if (QFileInfo::exists(target))
        {
            out << QDir::toNativeSeparators(target) << " is there already: it was left as it is." << Qt::endl;
            return 0;
        }
        QString problem;
        if (!MainWindow::writeSampleConfig(target, problem))
        {
            err << problem << Qt::endl;
            return 1;
        }
        out << "wrote " << QDir::toNativeSeparators(target) << Qt::endl;
        return 0;
    }
    const QString ownDatabase = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/qatest.sqlite");
    if (parser.isSet(setDatabaseOption))
    {
        const QString target = parser.isSet(configOption) ? configFile : users;
        // A connected drive is written down as what it is connected to: the same on every PC.
        const QString typed = QDir::cleanPath(QDir::current().absoluteFilePath(parser.value(setDatabaseOption).trimmed()));
        const QString network = QaShare::networkName(typed);
        const QString wanted = network.isEmpty() ? typed : QDir::cleanPath(network);
        if (parser.value(setDatabaseOption).trimmed().isEmpty())
        {
            err << "Say which file the database is: --set-database <file>" << Qt::endl;
            return 1;
        }
        if (parser.isSet(existingOption) && !QFileInfo::exists(wanted))
        {
            err << "There is no database " << QDir::toNativeSeparators(wanted) << Qt::endl;
            return 1;
        }
        // First the database: a file that cannot be made is not written into the configuration.
        QaDatabase chosen;
        QString problem;
        if (!chosen.open(wanted, problem))
        {
            err << problem << Qt::endl;
            return 1;
        }
        const QString brought = MainWindow::importBundled(chosen, QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("scripts")));
        chosen.close();

        // The user's own database needs no line: it is where the program looks anyway.
        const bool own = QDir::cleanPath(ownDatabase).compare(wanted, Qt::CaseInsensitive) == 0;
        QString text = QaConfigFile::sample();
        QFile file(target);
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly))
            {
                err << QDir::toNativeSeparators(target) << " could not be read: " << file.errorString() << Qt::endl;
                return 1;
            }
            text = QString::fromUtf8(file.readAll());
            file.close();
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        const QByteArray written = QaConfigFile::withDatabasePath(text, own ? QString() : QDir::toNativeSeparators(wanted)).toUtf8();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(written) != written.size())
        {
            err << QDir::toNativeSeparators(target) << " could not be written: " << file.errorString() << Qt::endl;
            return 1;
        }
        out << "The database is " << QDir::toNativeSeparators(wanted) << (brought.isEmpty() ? QString() : QStringLiteral(" - ") + brought) << Qt::endl;
        return 0;
    }
    const QaConfig config = QaConfigFile::read(configFile);
    if (parser.isSet(configOption) && config.file.isEmpty())
    {
        err << "There is no configuration file " << QDir::toNativeSeparators(configFile) << Qt::endl;
        return 1;
    }
    if (!config.problem.isEmpty())
        err << QDir::toNativeSeparators(config.file) << ": " << config.problem << Qt::endl;

    // The database named; else the one opened last, if it is still there; else one
    // that travels with the program - qatest.sqlite beside it, or in a folder "data"
    // beside its folder; else the user's own.
    QString path = ownDatabase;
    QString why = QStringLiteral("It is your own: no other was named, opened or found beside the program.");
    const QDir program(QCoreApplication::applicationDirPath());
    for (const QString &candidate : { program.absoluteFilePath(QStringLiteral("../data/qatest.sqlite")), program.absoluteFilePath(QStringLiteral("qatest.sqlite")) })
        if (QFileInfo::exists(candidate))
        {
            path = QDir::cleanPath(candidate);
            why = QStringLiteral("It is the one that stands with the program.");
        }
    const QString last = QSettings().value(QStringLiteral("Database")).toString();
    if (!last.isEmpty() && QFileInfo::exists(last))
    {
        path = last;
        why = QStringLiteral("It is the one that was opened last (File > Open Database...).");
    }
    // What the configuration file says goes before what one user opened: it is how a team shares a database.
    if (!config.databasePath.isEmpty())
    {
        path = config.databasePath;
        why = QStringLiteral("The configuration file says so: %1").arg(QDir::toNativeSeparators(config.file));
    }
    if (parser.isSet(dbOption))
    {
        path = QDir::current().absoluteFilePath(parser.value(dbOption));
        why = QStringLiteral("It was named when the program was started (--db).");
    }
    if (parser.isSet(whereOption))
    {
        out << QDir::toNativeSeparators(path) << Qt::endl << why << Qt::endl;
        return 0;
    }

    QaDatabase database;
    database.setBusyTimeout(config.busyTimeoutSeconds);
    QString error;
    while (!database.open(path, error))
    {
        // A shared drive that is not there is the usual reason: say whose word the path is.
        error += QStringLiteral("\n\n%1").arg(why);
        err << error << Qt::endl;
        if (parser.isSet(importOption) || parser.isSet(smokeOption))
            return 1;
        // ... and it may be there in a moment: a drive that is being connected, a NAS that wakes up.
        QMessageBox box(QMessageBox::Critical, QStringLiteral("QA Test Tracker"), error, QMessageBox::Retry | QMessageBox::Close);
        box.setInformativeText(QStringLiteral("If the database is on a shared drive, see that the drive is connected, then press Retry."));
        if (box.exec() != QMessageBox::Retry)
            return 1;
    }

    if (parser.isSet(importOption))
    {
        QString message;
        const bool ok = MainWindow::importFile(database, QDir::current().absoluteFilePath(parser.value(importOption)), message);
        (ok ? out : err) << message << Qt::endl;
        return ok ? 0 : 1;
    }

    // A database with nothing in it starts with the test scripts that came with the
    // program: the files in "scripts" beside it. (Not for a check that it starts.)
    QString brought;
    if (!parser.isSet(smokeOption))
        brought = MainWindow::importBundled(database, QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("scripts")));

    // Is the file sound? One on a shared drive is written to over the network by everybody.
    QString damage;
    if (!parser.isSet(smokeOption) && !database.sound(damage))
    {
        const QStringList copies = QaBackup::copies(path, config.backupFolder);
        QMessageBox box(QMessageBox::Warning, QStringLiteral("QA Test Tracker"),
                        QStringLiteral("The database is damaged: %1").arg(QDir::toNativeSeparators(path)), QMessageBox::Ok);
        box.setInformativeText(QStringLiteral("%1\n\nDo not go on working in it. Close the program on every PC and put a copy from before in its place%2")
                                   .arg(damage, copies.isEmpty() ? QStringLiteral(" - there is none in %1.").arg(QDir::toNativeSeparators(QaBackup::folderFor(path, config.backupFolder)))
                                                                 : QStringLiteral(": the newest is\n\n%1").arg(QDir::toNativeSeparators(copies.first()))));
        box.exec();
    }

    MainWindow window(&database);
    window.setDatabaseSource(why, configFile);
    if (!parser.isSet(smokeOption))
        window.setBackups(config.backupKeep, config.backupFolder);
    if (!brought.isEmpty())
        window.statusBar()->showMessage(brought, 15000);
    if (parser.isSet(smokeOption))
    {
        out << "started on " << QDir::toNativeSeparators(path) << Qt::endl;
        return 0;
    }
    window.show();
    return app.exec();
}
