#ifndef IMPORTPREVIEW_H
#define IMPORTPREVIEW_H

#include "qadatabase.h"

#include <QDialog>

class QCheckBox;
class QPushButton;
class QTextBrowser;

// What reading a file of test scripts would do, shown before it is done:
//
//   the file's version and the database's - which is newer
//   new suites, new test cases
//   test cases that would change, each with what is different (title, steps,
//     priority ... moved from one suite to another)
//   test cases the database has and the file does not: they stay as they
//     are - unless the box under the list is ticked, which deletes them with
//     their results
//
// Import does it; Cancel leaves the database as it is. Where nothing would
// change there is nothing to import, and it says so.
//
// A document, black on white whatever the theme.
namespace ImportPreview
{
    QString html(const QaImportPreview &preview, const QString &fileName);
    // Is there anything for Import to do - a case, a suite, or just the version?
    bool worthImporting(const QaImportPreview &preview);
}

class ImportPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    ImportPreviewDialog(const QaImportPreview &preview, const QString &fileName, QWidget *parent = nullptr);

    // Whether the cases the file does not have are to go (the box is ticked).
    bool deleteMissing() const;

private:
    QaImportPreview m_preview;
    QTextBrowser   *m_view = nullptr;
    QCheckBox      *m_delete = nullptr;
    QPushButton    *m_import = nullptr;
};

#endif // IMPORTPREVIEW_H
