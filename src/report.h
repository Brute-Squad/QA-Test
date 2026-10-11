#ifndef REPORT_H
#define REPORT_H

#include "qadatabase.h"

#include <QDialog>

class QPrinter;
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

    // Test cases on paper, to read or to work through away from the program:
    // suite by suite (each on a page of its own, after the first), a case with
    // its key and title, its priority, where it is run and its tags, what has
    // to be there first, its steps - what to do, what to expect - and its
    // notes. `roomForResults`: under each case a line to tick how it went,
    // and beside each step a box. The cases come with their steps
    // (QaDatabase::loadCase), in the order they are to stand in.
    struct CaseOnPaper
    {
        QString suite;
        QaCase  testCase;
    };
    QString casesHtml(const QString &projectName, const QList<CaseOnPaper> &cases, bool roomForResults = true);

    // Write a document as an HTML file, or as a PDF (A4). False with the reason.
    bool saveHtml(const QString &html, const QString &path, QString &error);
    bool savePdf(const QString &html, const QString &path, QString &error);

    // A document on that printer (which may be one that writes a file).
    void print(const QString &html, QPrinter *printer);
}

// A document on the screen, with Print..., Save as HTML... and Save as PDF...
// `title` is what a file of it is called; `caption` the window's name
// ("Test Report" unless said).
class ReportDialog : public QDialog
{
    Q_OBJECT

public:
    ReportDialog(const QString &title, const QString &html, QWidget *parent = nullptr, const QString &caption = QString());

private:
    void save(bool pdf);
    void print();

    QString       m_title;
    QString       m_html;
    QTextBrowser *m_view = nullptr;
};

#endif // REPORT_H
