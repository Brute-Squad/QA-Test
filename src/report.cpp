#include "report.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
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

    QString fileName(QString title)
    {
        for (QChar &character : title)
            if (!character.isLetterOrNumber() && character != QLatin1Char('-') && character != QLatin1Char('.'))
                character = QLatin1Char(' ');
        return title.simplified().replace(QLatin1Char(' '), QLatin1Char('-'));
    }
}

QString Report::html(const QString &projectName, const QaRun &run, const QaSummary &counts, const QList<QaResult> &results)
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
        trouble += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                       .arg(escaped(result.caseKey), escaped(result.caseTitle), escaped(resultText(result)), escaped(result.notes));
    }
    html += QStringLiteral("<h2>Failed and blocked</h2>");
    html += trouble.isEmpty() ? QStringLiteral("<p>None.</p>")
                              : QStringLiteral("<table><tr><th width=\"14%\">Key</th><th width=\"30%\">Test case</th><th width=\"14%\">Result</th><th>What happened</th></tr>")
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
                    .arg(escaped(result.caseKey), escaped(result.caseTitle), escaped(resultText(result)), escaped(result.tester), escaped(result.notes));
    }
    if (open)
        html += QStringLiteral("</table>");
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

ReportDialog::ReportDialog(const QString &title, const QString &html, QWidget *parent)
    : QDialog(parent)
    , m_title(title)
    , m_html(html)
{
    setWindowTitle(QStringLiteral("Test Report"));
    m_view = new QTextBrowser(this);
    m_view->setObjectName(QStringLiteral("reportView"));
    // Paper is white, whatever the theme of the program.
    QPalette paper = m_view->palette();
    paper.setColor(QPalette::Base, Qt::white);
    paper.setColor(QPalette::Text, Qt::black);
    m_view->setPalette(paper);
    m_view->setHtml(html);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons->addButton(QStringLiteral("Save as &HTML..."), QDialogButtonBox::ActionRole), &QPushButton::clicked, this, [this]() { save(false); });
    connect(buttons->addButton(QStringLiteral("Save as &PDF..."), QDialogButtonBox::ActionRole), &QPushButton::clicked, this, [this]() { save(true); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addWidget(buttons);
    resize(860, 640);
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
