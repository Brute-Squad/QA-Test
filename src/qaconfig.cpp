#include "qaconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace
{
    // %NAME% -> that variable of the environment; one that is not set stays as it is written.
    QString expanded(const QString &text)
    {
        static const QRegularExpression variable(QStringLiteral("%([A-Za-z_][A-Za-z0-9_()]*)%"));
        const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        QString result;
        int from = 0;
        QRegularExpressionMatchIterator matches = variable.globalMatch(text);
        while (matches.hasNext())
        {
            const QRegularExpressionMatch match = matches.next();
            result += text.mid(from, match.capturedStart() - from);
            result += environment.contains(match.captured(1)) ? environment.value(match.captured(1)) : match.captured(0);
            from = int(match.capturedEnd());
        }
        return result + text.mid(from);
    }

    // A network path (\\server\share\...) or one with a drive or a root: not to be put after a folder.
    bool isAbsolute(const QString &path)
    {
        return path.startsWith(QLatin1String("\\\\")) || path.startsWith(QLatin1String("//")) || QDir::isAbsolutePath(path);
    }
}

QString QaConfigFile::fileName()
{
    return QStringLiteral("QATest.ini");
}

QaConfig QaConfigFile::parse(const QString &text, const QString &folder)
{
    QaConfig config;
    QString section;
    QStringList problems;
    int number = 0;
    for (const QString &raw : text.split(QLatin1Char('\n')))
    {
        ++number;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        const int at = line.indexOf(QLatin1Char('='));
        if (at <= 0)
        {
            problems << QStringLiteral("line %1 is neither a [section] nor a setting (name=value)").arg(number);
            continue;
        }
        const QString name = line.left(at).trimmed().toLower();
        QString value = line.mid(at + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);

        if (section == QLatin1String("backup"))
        {
            if (name == QLatin1String("keep"))
            {
                bool isNumber = false;
                const int copies = value.toInt(&isNumber);
                if (isNumber && copies >= 0 && copies <= 3650)
                    config.backupKeep = copies;
                else
                    problems << QStringLiteral("Keep is \"%1\": it has to be a number of copies from 0 to 3650").arg(value);
            }
            else if (name == QLatin1String("folder"))
            {
                const QString path = expanded(value.trimmed());
                if (!path.isEmpty())
                    config.backupFolder = isAbsolute(path) ? QDir::cleanPath(path) : QDir::cleanPath(QDir(folder).absoluteFilePath(path));
            }
            continue;
        }
        if (section != QLatin1String("database"))
            continue;       // what this version does not know is left alone
        if (name == QLatin1String("path"))
        {
            const QString path = expanded(value.trimmed());
            if (!path.isEmpty())
                config.databasePath = isAbsolute(path) ? QDir::cleanPath(path) : QDir::cleanPath(QDir(folder).absoluteFilePath(path));
        }
        else if (name == QLatin1String("busytimeoutseconds"))
        {
            bool isNumber = false;
            const int seconds = value.toInt(&isNumber);
            if (isNumber && seconds >= 0 && seconds <= 600)
                config.busyTimeoutSeconds = seconds;
            else
                problems << QStringLiteral("BusyTimeoutSeconds is \"%1\": it has to be a number of seconds from 0 to 600").arg(value);
        }
    }
    config.problem = problems.join(QStringLiteral("; "));
    return config;
}

QaConfig QaConfigFile::read(const QString &path)
{
    if (!QFileInfo::exists(path))
        return QaConfig();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        QaConfig config;
        config.file = QDir::cleanPath(path);
        config.problem = QStringLiteral("it could not be read: %1").arg(file.errorString());
        return config;
    }
    QaConfig config = parse(QString::fromUtf8(file.readAll()), QFileInfo(path).absolutePath());
    config.file = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return config;
}

QString QaConfigFile::withDatabasePath(const QString &text, const QString &path)
{
    const QString eol = text.contains(QLatin1String("\r\n")) || text.isEmpty() ? QStringLiteral("\r\n") : QStringLiteral("\n");
    const QString setting = path.trimmed().isEmpty() ? QStringLiteral(";Path=") : QStringLiteral("Path=") + path.trimmed();
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines)
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

    // The line that says the path - set, or commented out - in [Database]; a set one before a comment.
    static const QRegularExpression said(QStringLiteral("^\\s*Path\\s*="), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression commented(QStringLiteral("^\\s*;\\s*Path\\s*=\\s*$"), QRegularExpression::CaseInsensitiveOption);
    int header = -1, setLine = -1, commentLine = -1;
    bool inDatabase = false;
    for (int i = 0; i < lines.size(); ++i)
    {
        const QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            inDatabase = line.mid(1, line.size() - 2).trimmed().compare(QLatin1String("database"), Qt::CaseInsensitive) == 0;
            if (inDatabase && header < 0)
                header = i;
            continue;
        }
        if (!inDatabase)
            continue;
        if (setLine < 0 && said.match(lines.at(i)).hasMatch())
            setLine = i;
        else if (commentLine < 0 && commented.match(lines.at(i)).hasMatch())
            commentLine = i;
    }

    if (setLine >= 0)
        lines[setLine] = setting;
    else if (commentLine >= 0)
        lines[commentLine] = setting;
    else if (header >= 0)
        lines.insert(header + 1, setting);
    else
    {
        if (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
        lines << QStringLiteral("[Database]") << setting << QString();
    }
    return lines.join(eol);
}

QString QaConfigFile::sample()
{
    return QStringLiteral(
        "; QA Test Tracker - configuration. This file stands beside QATest.exe or, for an\r\n"
        "; installed program, in your own QATest folder (File > Edit Configuration File...).\r\n"
        "; A line that starts with ; is a comment: take the ; away to set something.\r\n"
        "\r\n"
        "[Database]\r\n"
        "; Where the database is. To share one database, put it on a shared drive and\r\n"
        "; give everybody this file with the same line. A backslash is written once:\r\n"
        ";\r\n"
        ";   Path=\\\\fileserver\\qa\\qatest.sqlite\r\n"
        ";   Path=Q:\\QA\\qatest.sqlite\r\n"
        ";\r\n"
        "; A path that does not start at a drive or a server is meant from this file's\r\n"
        "; folder. %USERPROFILE% and the like are filled in. The file is made if it is\r\n"
        "; not there; its folder has to be there, and everybody needs to be allowed to\r\n"
        "; change files in it.\r\n"
        ";\r\n"
        "; Without a Path the program uses qatest.sqlite beside itself or in the folder\r\n"
        "; \"data\" beside its own folder, else the one in your own application data.\r\n"
        ";Path=\r\n"
        "\r\n"
        "; How many seconds to wait while somebody else is writing, before saying that\r\n"
        "; the database is busy. 10 unless said.\r\n"
        ";BusyTimeoutSeconds=10\r\n"
        "\r\n"
        "[Backup]\r\n"
        "; A copy of the database is made on every day the program is used, into the\r\n"
        "; folder \"backups\" beside the database, and the oldest copies are deleted.\r\n"
        "; How many copies to keep (14 unless said; 0 = make none):\r\n"
        ";Keep=14\r\n"
        "; Another folder for the copies - on another drive than the database, say:\r\n"
        ";Folder=\r\n");
}
