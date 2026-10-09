#ifndef REPORT_H
#define REPORT_H

#include "qadatabase.h"

#include <QDialog>

class QTextBrowser;

// A test run on paper: what was tested, by whom and when, how it stands,
// what failed or was blocked - with the notes - and every case's result by
// suite. HTML, black on white whatever the theme: it is a document.
namespace Report
{
    // `issueUrl`: where the project's issues are (QaProject::issueUrl) - a failure's issue is then a link.
    QString html(const QString &projectName, const QaRun &run, const QaSummary &counts, const QList<QaResult> &results, const QString &issueUrl = QString());

    // A run as a table for a spreadsheet: a line per test case - suite, key,
    // title, priority, where it is run, result, the step that failed, by
    // whom, when (local time), for whom, issue, files, notes. CSV as Excel
    // reads it: commas, text in quotation marks where it needs them, CRLF.
    QString csv(const QList<QaResult> &results);
    // Written as UTF-8 with the mark that tells Excel so.
    bool saveCsv(const QString &csv, const QString &path, QString &error);

    // Write a document as an HTML file, or as a PDF (A4). False with the reason.
    bool saveHtml(const QString &html, const QString &path, QString &error);
    bool savePdf(const QString &html, const QString &path, QString &error);
}

// The report on the screen, with Save as HTML... and Save as PDF...
class ReportDialog : public QDialog
{
    Q_OBJECT

public:
    ReportDialog(const QString &title, const QString &html, QWidget *parent = nullptr);

private:
    void save(bool pdf);

    QString       m_title;
    QString       m_html;
    QTextBrowser *m_view = nullptr;
};

#endif // REPORT_H
