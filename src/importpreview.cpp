#include "importpreview.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPalette>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace
{
    QString escaped(const QString &text)
    {
        return text.toHtmlEscaped();
    }

    QString count(int n, const char *one, const char *several)
    {
        return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : several));
    }
}

bool ImportPreview::worthImporting(const QaImportPreview &preview)
{
    return preview.changesCases() || (!preview.fileVersion.isEmpty() && preview.fileVersion != preview.databaseVersion);
}

QString ImportPreview::html(const QaImportPreview &preview, const QString &fileName)
{
    QString html = QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><style>"
        "body { font-family: sans-serif; color: #000000; background: #ffffff; }"
        "h1 { font-size: 16pt; } h2 { font-size: 12pt; margin-top: 16px; }"
        "table { border-collapse: collapse; width: 100%; }"
        "th, td { border: 1px solid #888888; padding: 3px 6px; text-align: left; vertical-align: top; }"
        "th { background: #e8e8e8; }"
        "</style></head><body>");
    html += QStringLiteral("<h1>Test scripts for %1</h1>").arg(escaped(preview.project));

    // ---- which is newer
    QString versions = QStringLiteral("The file %1 %2. ").arg(escaped(fileName), preview.fileVersion.isEmpty() ? QStringLiteral("says no version")
                                                                                                               : QStringLiteral("is version <b>%1</b>").arg(escaped(preview.fileVersion)));
    if (preview.newProject)
        versions += QStringLiteral("The database does not have this project yet: it would be added.");
    else
    {
        versions += preview.databaseVersion.isEmpty() ? QStringLiteral("The database's test scripts are not marked with a version.")
                                                      : QStringLiteral("The database was last brought up to date from version <b>%1</b>.").arg(escaped(preview.databaseVersion));
        if (preview.versionOrder() > 0)
            versions += QStringLiteral(" <b>The file is newer.</b>");
        else if (preview.versionOrder() < 0)
            versions += QStringLiteral(" <b>The file is OLDER than the database's:</b> importing it takes the test cases back to what the file says.");
        else if (!preview.fileVersion.isEmpty() && !preview.databaseVersion.isEmpty())
            versions += QStringLiteral(" They are the same version.");
    }
    html += QStringLiteral("<p>%1</p>").arg(versions);

    if (!preview.changesCases())
    {
        html += QStringLiteral("<p><b>Nothing would change:</b> the database's %1 as the file says.%2</p>")
                    .arg(preview.unchanged == 1 ? QStringLiteral("test case is") : QStringLiteral("%1 test cases are").arg(preview.unchanged),
                         worthImporting(preview) ? QStringLiteral(" Import only marks the database as being of the file's version.") : QString());
    }
    else if (!preview.newProject)
        html += QStringLiteral("<p>Of the file's test cases: <b>%1</b></p>").arg(escaped(preview.summary()));

    if (!preview.newSuites.isEmpty() && !preview.newProject)
        html += QStringLiteral("<h2>New suites</h2><p>%1</p>").arg(escaped(preview.newSuites.join(QStringLiteral(", "))));

    if (!preview.added.isEmpty())
    {
        html += QStringLiteral("<h2>%1</h2>").arg(preview.newProject ? count(int(preview.added.size()), "test case would be added", "test cases would be added")
                                                                     : count(int(preview.added.size()), "new test case", "new test cases"));
        html += QStringLiteral("<table><tr><th width=\"16%\">Key</th><th>Test case</th><th width=\"28%\">Suite</th></tr>");
        for (const QaCaseChange &one : preview.added)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(escaped(one.key), escaped(one.title), escaped(one.suite));
        html += QStringLiteral("</table>");
    }
    if (!preview.changed.isEmpty())
    {
        html += QStringLiteral("<h2>%1</h2>").arg(count(int(preview.changed.size()), "test case would change", "test cases would change"));
        html += QStringLiteral("<p>What somebody changed in the database is replaced by what the file says. Results stay.</p>");
        html += QStringLiteral("<table><tr><th width=\"16%\">Key</th><th>Test case</th><th width=\"36%\">What is different</th></tr>");
        for (const QaCaseChange &one : preview.changed)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(escaped(one.key), escaped(one.title), escaped(one.what.join(QStringLiteral(", "))));
        html += QStringLiteral("</table>");
    }
    if (!preview.missing.isEmpty())
    {
        html += QStringLiteral("<h2>%1</h2>").arg(count(int(preview.missing.size()), "test case is not in the file", "test cases are not in the file"));
        html += QStringLiteral("<p>Added here, or taken out of the scripts since. They <b>stay as they are</b> unless you tick the box below.</p>");
        html += QStringLiteral("<table><tr><th width=\"16%\">Key</th><th>Test case</th><th width=\"28%\">Suite</th></tr>");
        for (const QaCaseChange &one : preview.missing)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(escaped(one.key), escaped(one.title), escaped(one.suite));
        html += QStringLiteral("</table>");
    }
    html += QStringLiteral("</body></html>");
    return html;
}

ImportPreviewDialog::ImportPreviewDialog(const QaImportPreview &preview, const QString &fileName, QWidget *parent)
    : QDialog(parent)
    , m_preview(preview)
{
    setObjectName(QStringLiteral("importPreview"));
    setWindowTitle(QStringLiteral("Import Test Scripts"));
    m_view = new QTextBrowser(this);
    m_view->setObjectName(QStringLiteral("previewView"));
    // A document: paper is white, whatever the theme of the program.
    QPalette paper = m_view->palette();
    paper.setColor(QPalette::Base, Qt::white);
    paper.setColor(QPalette::Text, Qt::black);
    m_view->setPalette(paper);
    m_view->setHtml(ImportPreview::html(preview, fileName));

    // Deleting is never the default, and says what goes with it.
    m_delete = new QCheckBox(preview.missing.size() == 1
                                 ? QStringLiteral("Also &delete the test case that is not in the file, with its results in every test run")
                                 : QStringLiteral("Also &delete the %1 test cases that are not in the file, with their results in every test run").arg(preview.missing.size()), this);
    m_delete->setObjectName(QStringLiteral("previewDelete"));
    m_delete->setVisible(!preview.missing.isEmpty() && !preview.newProject);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_import = buttons->addButton(QStringLiteral("&Import"), QDialogButtonBox::AcceptRole);
    m_import->setObjectName(QStringLiteral("previewImport"));
    const auto enable = [this]() {
        // Nothing to do, nothing to press - unless what is not in the file is to go.
        m_import->setEnabled(ImportPreview::worthImporting(m_preview) || m_delete->isChecked());
        m_import->setText(m_delete->isChecked() ? QStringLiteral("&Import and Delete") : QStringLiteral("&Import"));
    };
    enable();
    connect(m_delete, &QCheckBox::toggled, this, enable);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_delete);
    layout->addWidget(buttons);
    resize(820, 620);
}

bool ImportPreviewDialog::deleteMissing() const
{
    return m_delete->isChecked() && !m_preview.missing.isEmpty();
}
