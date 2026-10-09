#ifndef QASHARE_H
#define QASHARE_H

#include <QString>

// A database on a shared drive is named so that every PC finds it.
//
// "M:\QA\qatest.sqlite" means something on the PC where M: was connected to
// the file server - and nothing, or another folder, on the next one. The
// share's own name, \\server\share\QA\qatest.sqlite, is the same everywhere:
// that is what is written into a configuration file.
namespace QaShare
{
    // The path with its drive letter replaced by what that drive is connected
    // to - \\server\share\... - or "" when the drive is not a network drive
    // (or the path has no drive letter, or this is not Windows).
    QString networkName(const QString &path);

    // The same, for a drive whose connection is known: `remote` is what the
    // drive letter of `path` stands for ("\\server\share"). "" if the path
    // has no drive letter or `remote` is no share.
    QString withRemote(const QString &path, const QString &remote);

    // Is that a path on the network - \\server\... or a connected drive?
    bool isOnNetwork(const QString &path);
}

#endif // QASHARE_H
