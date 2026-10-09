#include "qashare.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <winnetwk.h>
#endif

namespace
{
    bool hasDriveLetter(const QString &path)
    {
        return path.size() >= 2 && path.at(0).isLetter() && path.at(1) == QLatin1Char(':');
    }
}

QString QaShare::withRemote(const QString &path, const QString &remote)
{
    QString share = QDir::toNativeSeparators(remote.trimmed());
    while (share.endsWith(QLatin1Char('\\')))
        share.chop(1);
    if (!hasDriveLetter(path) || !share.startsWith(QLatin1String("\\\\")) || share.size() < 5)
        return QString();
    QString rest = QDir::toNativeSeparators(path.mid(2));
    if (!rest.isEmpty() && !rest.startsWith(QLatin1Char('\\')))
        rest.prepend(QLatin1Char('\\'));
    return share + rest;
}

QString QaShare::networkName(const QString &path)
{
#ifdef Q_OS_WIN
    if (!hasDriveLetter(path))
        return QString();
    const std::wstring drive = path.left(2).toStdWString();
    wchar_t remote[1024];
    DWORD size = DWORD(sizeof(remote) / sizeof(remote[0]));
    if (WNetGetConnectionW(drive.c_str(), remote, &size) != NO_ERROR)
        return QString();
    return withRemote(path, QString::fromWCharArray(remote));
#else
    Q_UNUSED(path);
    return QString();
#endif
}

bool QaShare::isOnNetwork(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    return native.startsWith(QLatin1String("\\\\")) || !networkName(path).isEmpty();
}
