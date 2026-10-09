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
    QString html(const QString &projectName, const QaRun &run, const QaSummary &counts, const QList<QaResult> &results);

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
