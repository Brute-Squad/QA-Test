#include "report.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace
{
    QString localTime(const QString &utc)
    {
        const QDateTime when = QDateTime::fromString(utc, Qt::ISODate);
        return when.isValid() ? QLocale().toString(when.toLocalTime(), QLocale::ShortFormat) : utc;
    }

    QString escaped(const QString &text)
    {
        return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    }

    QString resultText(const QaResult &result)
    {
        return result.status == QaDatabase::failed() && result.failedStep > 0 ? QStringLiteral("%1 at step %2").arg(result.status).arg(result.failedStep) : result.status;
    }

    // " [2 files]" - what was attached is with the database, not in a document.
    QString filesText(const QaResult &result)
    {
        return result.attachments <= 0 ? QString() : result.attachments == 1 ? QStringLiteral(" [1 file attached]") : QStringLiteral(" [%1 files attached]").arg(result.attachments);
    }

    QString fileName(QString title)
    {
        for (QChar &character : title)
            if (!character.isLetterOrNumber() && character != QLatin1Char('-') && character != QLatin1Char('.'))
                character = QLatin1Char(' ');
        return title.simplified().replace(QLatin1Char(' '), QLatin1Char('-'));
    }
}

QString Report::csv(const QList<QaResult> &results)
{
    const auto cell = [](const QString &text) {
        // In quotation marks when it has what would part it: a comma, a mark, a line break - or blanks at its ends.
        const bool quote = text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))
                           || text != text.trimmed();
        return quote ? QLatin1Char('"') + QString(text).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"') : text;
    };
    QString csv = QStringLiteral("Suite,Key,Title,Priority,Run on,Result,Failed at step,By,When,For,Issue,Files,Notes\r\n");
    for (const QaResult &result : results)
    {
        const QDateTime when = QDateTime::fromString(result.executed, Qt::ISODate).toLocalTime();
        const QStringList cells { result.suiteName, result.caseKey, result.caseTitle, result.priority, result.area, result.status,
                                  result.failedStep > 0 ? QString::number(result.failedStep) : QString(), result.tester,
                                  when.isValid() ? when.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString(), result.assigned, result.defect,
                                  result.attachments > 0 ? QString::number(result.attachments) : QString(), result.notes };
        QStringList line;
        for (const QString &text : cells)
            line << cell(text);
        csv += line.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return csv;
}

bool Report::saveCsv(const QString &csv, const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write("\xEF\xBB\xBF") < 0 || file.write(csv.toUtf8()) < 0)
    {
        error = QStringLiteral("%1 could not be written: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString Report::html(const QString &projectName, const QaRun &run, const QaSummary &counts, const QList<QaResult> &results, const QString &issueUrl)
{
    QString html = QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><style>"
        "body { font-family: sans-serif; color: #000000; background: #ffffff; }"
        "h1 { font-size: 18pt; } h2 { font-size: 13pt; margin-top: 18px; }"
        "table { border-collapse: collapse; width: 100%; }"
        "th, td { border: 1px solid #888888; padding: 4px 6px; text-align: left; vertical-align: top; }"
        "th { background: #e8e8e8; }"
        "</style></head><body>");
    html += QStringLiteral("<h1>Test Report: %1</h1>").arg(escaped(projectName));

    html += QStringLiteral("<table>");
    const auto line = [&html](const QString &caption, const QString &value) {
        if (!value.isEmpty())
            html += QStringLiteral("<tr><th width=\"22%\">%1</th><td>%2</td></tr>").arg(caption, escaped(value));
    };
    line(QStringLiteral("Test run"), run.name);
    line(QStringLiteral("Build tested"), run.build);
    // The build of each component, a line each: "Server: 0.2.0 Beta, built ...".
    for (const QString &component : run.builds.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
    {
        const int colon = int(component.indexOf(QStringLiteral(": ")));
        line(colon > 0 ? escaped(component.left(colon)) : QStringLiteral("Build tested"), colon > 0 ? component.mid(colon + 2) : component);
    }
    line(QStringLiteral("Tester"), run.tester);
    line(QStringLiteral("Started"), localTime(run.started));
    line(QStringLiteral("Finished"), run.finished.isEmpty() ? QStringLiteral("not yet") : localTime(run.finished));
    line(QStringLiteral("Result"), counts.text());
    line(QStringLiteral("Notes"), run.notes);
    html += QStringLiteral("</table>");

    // What needs somebody's attention first.
    QString trouble;
    for (const QaResult &result : results)
    {
        if (result.status != QaDatabase::failed() && result.status != QaDatabase::blocked())
            continue;
        // The issue it was reported as: a link, where the project says where its issues are.
        const QString url = QaDatabase::defectUrl(issueUrl, result.defect);
        const QString issue = result.defect.isEmpty() ? QString()
                              : url.isEmpty() ? escaped(result.defect) : QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), escaped(result.defect));
        trouble += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                       .arg(escaped(result.caseKey), escaped(result.caseTitle), escaped(resultText(result)), issue, escaped(result.notes) + filesText(result));
    }
    html += QStringLiteral("<h2>Failed and blocked</h2>");
    html += trouble.isEmpty() ? QStringLiteral("<p>None.</p>")
                              : QStringLiteral("<table><tr><th width=\"14%\">Key</th><th width=\"28%\">Test case</th><th width=\"14%\">Result</th><th width=\"10%\">Issue</th>"
                                               "<th>What happened</th></tr>")
                                    + trouble + QStringLiteral("</table>");

    // Every case, suite by suite.
    QString suite;
    bool open = false;
    for (const QaResult &result : results)
    {
        if (result.suiteName != suite || !open)
        {
            if (open)
                html += QStringLiteral("</table>");
            suite = result.suiteName;
            open = true;
            html += QStringLiteral("<h2>%1</h2><table><tr><th width=\"14%\">Key</th><th width=\"36%\">Test case</th><th width=\"14%\">Result</th>"
                                   "<th width=\"12%\">By</th><th>Notes</th></tr>").arg(escaped(suite));
        }
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(escaped(result.caseKey), escaped(result.caseTitle), escaped(resultText(result)), escaped(result.tester), escaped(result.notes) + filesText(result));
    }
    if (open)
        html += QStringLiteral("</table>");
    html += QStringLiteral("</body></html>");
    return html;
}

QString Report::casesHtml(const QString &projectName, const QList<CaseOnPaper> &cases, bool roomForResults)
{
    QString html = QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><style>"
        "body { font-family: sans-serif; color: #000000; background: #ffffff; }"
        "h1 { font-size: 18pt; } h2 { font-size: 14pt; margin-top: 18px; } h3 { font-size: 11pt; margin-top: 16px; margin-bottom: 4px; }"
        "p { margin-top: 2px; margin-bottom: 4px; }"
        "table { border-collapse: collapse; width: 100%; }"
        "th, td { border: 1px solid #888888; padding: 4px 6px; text-align: left; vertical-align: top; }"
        "th { background: #e8e8e8; }"
        "</style></head><body>");
    html += QStringLiteral("<h1>Test Cases: %1</h1>").arg(escaped(projectName));

    QStringList suites;
    for (const CaseOnPaper &one : cases)
        if (!suites.contains(one.suite))
            suites << one.suite;
    html += QStringLiteral("<p>%1 in %2 - %3</p>")
                .arg(cases.size() == 1 ? QStringLiteral("1 test case") : QStringLiteral("%1 test cases").arg(cases.size()),
                     suites.size() == 1 ? QStringLiteral("1 suite") : QStringLiteral("%1 suites").arg(suites.size()),
                     QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat));
    if (cases.isEmpty())
        html += QStringLiteral("<p>No test cases.</p>");

    QString suite;
    bool first = true;
    for (const CaseOnPaper &one : cases)
    {
        const QaCase &testCase = one.testCase;
        if (first || one.suite != suite)
        {
            // A suite starts a page of its own: its cases are handed out together.
            html += first ? QStringLiteral("<h2>%1</h2>").arg(escaped(one.suite))
                          : QStringLiteral("<h2 style=\"page-break-before: always;\">%1</h2>").arg(escaped(one.suite));
            suite = one.suite;
            first = false;
        }
        html += QStringLiteral("<h3>%1&nbsp;&nbsp;%2</h3>").arg(escaped(testCase.key), escaped(testCase.title));

        QStringList about;
        if (!testCase.priority.isEmpty())
            about << QStringLiteral("Priority: %1").arg(escaped(testCase.priority));
        if (!testCase.area.isEmpty())
            about << QStringLiteral("Run on: %1").arg(escaped(testCase.area));
        if (!testCase.tags.trimmed().isEmpty())
            about << QStringLiteral("Tags: %1").arg(escaped(testCase.tags.trimmed()));
        if (!about.isEmpty())
            html += QStringLiteral("<p>%1</p>").arg(about.join(QStringLiteral(" &nbsp;&middot;&nbsp; ")));
        if (!testCase.preconditions.trimmed().isEmpty())
            html += QStringLiteral("<p><b>Before:</b> %1</p>").arg(escaped(testCase.preconditions.trimmed()));

        if (testCase.steps.isEmpty())
            html += QStringLiteral("<p>No steps.</p>");
        else
        {
            html += roomForResults ? QStringLiteral("<table><tr><th width=\"5%\">#</th><th width=\"45%\">What to do</th><th width=\"42%\">What to expect</th><th width=\"8%\">OK</th></tr>")
                                   : QStringLiteral("<table><tr><th width=\"5%\">#</th><th width=\"48%\">What to do</th><th>What to expect</th></tr>");
            int number = 0;
            for (const QaStep &step : testCase.steps)
                html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td>%4</tr>")
                            .arg(++number).arg(escaped(step.action), escaped(step.expected), roomForResults ? QStringLiteral("<td>&nbsp;</td>") : QString());
            html += QStringLiteral("</table>");
        }
        if (!testCase.notes.trimmed().isEmpty())
            html += QStringLiteral("<p><b>Notes:</b> %1</p>").arg(escaped(testCase.notes.trimmed()));
        if (roomForResults)
            html += QStringLiteral("<p>Result: &nbsp; [&nbsp;&nbsp;&nbsp;] Passed &nbsp; [&nbsp;&nbsp;&nbsp;] Failed &nbsp; [&nbsp;&nbsp;&nbsp;] Blocked &nbsp; [&nbsp;&nbsp;&nbsp;] Skipped"
                                   " &nbsp; By:&nbsp;__________ &nbsp; Date:&nbsp;__________</p>");
    }
    html += QStringLiteral("</body></html>");
    return html;
}

bool Report::saveHtml(const QString &html, const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(html.toUtf8()) < 0)
    {
        error = QStringLiteral("%1 could not be written: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool Report::savePdf(const QString &html, const QString &path, QString &error)
{
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            error = QStringLiteral("%1 could not be written: %2").arg(path, file.errorString());
            return false;
        }
    }
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    QTextDocument document;
    document.setHtml(html);
    document.print(&writer);
    return true;
}

void Report::print(const QString &html, QPrinter *printer)
{
    QTextDocument document;
    document.setHtml(html);
    document.print(printer);
}

ReportDialog::ReportDialog(const QString &title, const QString &html, QWidget *parent, const QString &caption)
    : QDialog(parent)
    , m_title(title)
    , m_html(html)
{
    setWindowTitle(caption.isEmpty() ? QStringLiteral("Test Report") : caption);
    m_view = new QTextBrowser(this);
    m_view->setObjectName(QStringLiteral("reportView"));
    // Paper is white, whatever the theme of the program.
    QPalette paper = m_view->palette();
    paper.setColor(QPalette::Base, Qt::white);
    paper.setColor(QPalette::Text, Qt::black);
    m_view->setPalette(paper);
    m_view->setHtml(html);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *toPrinter = buttons->addButton(QStringLiteral("P&rint..."), QDialogButtonBox::ActionRole);
    toPrinter->setObjectName(QStringLiteral("reportPrint"));
    connect(toPrinter, &QPushButton::clicked, this, &ReportDialog::print);
    connect(buttons->addButton(QStringLiteral("Save as &HTML..."), QDialogButtonBox::ActionRole), &QPushButton::clicked, this, [this]() { save(false); });
    connect(buttons->addButton(QStringLiteral("Save as &PDF..."), QDialogButtonBox::ActionRole), &QPushButton::clicked, this, [this]() { save(true); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addWidget(buttons);
    resize(860, 640);
}

// The system's own dialog chooses the printer and the pages; the document goes there when it is accepted.
void ReportDialog::print()
{
    auto *printer = new QPrinter(QPrinter::HighResolution);
    printer->setDocName(m_title);
    printer->setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    auto *dialog = new QPrintDialog(printer, this);
    dialog->setWindowTitle(QStringLiteral("Print %1").arg(windowTitle()));
    connect(dialog, &QDialog::finished, this, [this, dialog, printer](int result) {
        if (result == QDialog::Accepted)
            Report::print(m_html, printer);
        dialog->deleteLater();
        delete printer;
    });
    dialog->open();
}

void ReportDialog::save(bool pdf)
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    auto *chooser = new QFileDialog(this, pdf ? QStringLiteral("Save Report as PDF") : QStringLiteral("Save Report as HTML"),
                                    QStringLiteral("%1/%2.%3").arg(folder, fileName(m_title), pdf ? QStringLiteral("pdf") : QStringLiteral("html")),
                                    pdf ? QStringLiteral("PDF (*.pdf)") : QStringLiteral("HTML (*.html)"));
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    chooser->setAcceptMode(QFileDialog::AcceptSave);
    chooser->setDefaultSuffix(pdf ? QStringLiteral("pdf") : QStringLiteral("html"));
    connect(chooser, &QFileDialog::fileSelected, this, [this, pdf](const QString &path) {
        QString error;
        if (!(pdf ? Report::savePdf(m_html, path, error) : Report::saveHtml(m_html, path, error)))
        {
            auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Test Report"), QStringLiteral("The report was not saved."), QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setInformativeText(error);
            box->open();
        }
    });
    chooser->open();
}
