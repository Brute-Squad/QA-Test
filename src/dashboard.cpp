#include "dashboard.h"

#include <QDateTime>
#include <QLocale>
#include <QPalette>
#include <QTextBrowser>
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

    // A rate as a bar of twenty blocks and a number: read at a glance, whatever the colors.
    QString bar(int percent)
    {
        if (percent < 0)
            return QStringLiteral("no verdict yet");
        const int filled = qBound(0, (percent + 2) / 5, 20);
        return QString(filled, QChar(0x2588)) + QString(20 - filled, QChar(0x2591)) + QStringLiteral(" %1%").arg(percent);
    }

    QString issueText(const QString &issueUrl, const QString &defect)
    {
        const QString url = QaDatabase::defectUrl(issueUrl, defect);
        const QString name = defect.startsWith(QLatin1String("http"), Qt::CaseInsensitive) ? defect
                             : QStringLiteral("#") + QString(defect).remove(QLatin1Char('#'));
        return url.isEmpty() ? escaped(name) : QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), escaped(name));
    }

    QString count(int n, const char *one, const char *several)
    {
        return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : several));
    }
}

QString Dashboard::html(const QString &projectName, const QaDashboard &standing, const QString &issueUrl)
{
    QString html = QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><style>"
        "body { font-family: sans-serif; color: #000000; background: #ffffff; }"
        "h1 { font-size: 18pt; } h2 { font-size: 13pt; margin-top: 18px; }"
        "table { border-collapse: collapse; width: 100%; }"
        "th, td { border: 1px solid #888888; padding: 4px 6px; text-align: left; vertical-align: top; }"
        "th { background: #e8e8e8; }"
        "</style></head><body>");
    html += QStringLiteral("<h1>How %1 stands</h1>").arg(escaped(projectName));
    html += QStringLiteral("<p>%1 &nbsp;&middot;&nbsp; %2 &nbsp;&middot;&nbsp; %3 &nbsp;&middot;&nbsp; %4 never run</p>")
                .arg(count(standing.cases, "test case", "test cases"), count(int(standing.runs.size()), "test run", "test runs"),
                     count(int(standing.open.size()), "open failure", "open failures")).arg(standing.neverRun.size());

    // ---- the runs
    html += QStringLiteral("<h2>Test runs</h2>");
    if (standing.runs.isEmpty())
        html += QStringLiteral("<p>None yet: New Run... on the Test Runs tab starts one.</p>");
    else
    {
        html += QStringLiteral("<table><tr><th>Run</th><th>Build tested</th><th>Started</th><th>Passed</th><th>Failed</th><th>Blocked</th><th>Skipped</th><th>Not run</th>"
                               "<th width=\"30%\">Pass rate</th></tr>");
        QStringList rates;
        for (const QaRunStanding &one : standing.runs)
        {
            html += QStringLiteral("<tr><td>%1%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td><td>%8</td><td>%9</td>")
                        .arg(escaped(one.run.name), one.run.finished.isEmpty() ? QString() : QStringLiteral(" (finished)"), escaped(one.run.build), escaped(localTime(one.run.started)))
                        .arg(one.counts.passed).arg(one.counts.failed).arg(one.counts.blocked).arg(one.counts.skipped).arg(one.counts.notRun);
            html += QStringLiteral("<td><tt>%1</tt></td></tr>").arg(bar(one.passRate()));
            if (one.passRate() >= 0)
                rates << QStringLiteral("%1%").arg(one.passRate());
        }
        html += QStringLiteral("</table>");
        if (rates.size() > 1)
            html += QStringLiteral("<p>Pass rate, run by run: %1</p>").arg(rates.join(QString::fromUtf8(" \xE2\x86\x92 ")));
        html += QStringLiteral("<p>The pass rate is of the test cases that have a verdict - passed, failed or blocked: how many passed.</p>");
    }

    // ---- what is wrong now
    html += QStringLiteral("<h2>Open failures, by issue</h2>");
    if (standing.open.isEmpty())
        html += QStringLiteral("<p>None: no test case's newest result is a failure or blocked.</p>");
    else
    {
        html += QStringLiteral("<table><tr><th width=\"16%\">Issue</th><th width=\"12%\">Key</th><th width=\"24%\">Test case</th><th width=\"9%\">Result</th>"
                               "<th width=\"14%\">In run</th><th>What happened</th></tr>");
        for (int i = 0; i < standing.open.size(); ++i)
        {
            const QaOpenFailure &failure = standing.open.at(i);
            // An issue is said once, with how many cases it holds up.
            QString issue;
            if (i == 0 || standing.open.at(i - 1).defect.compare(failure.defect, Qt::CaseInsensitive) != 0)
            {
                int same = 0;
                for (const QaOpenFailure &other : standing.open)
                    if (other.defect.compare(failure.defect, Qt::CaseInsensitive) == 0)
                        ++same;
                issue = (failure.defect.isEmpty() ? QStringLiteral("<b>No issue yet</b>") : QStringLiteral("<b>%1</b>").arg(issueText(issueUrl, failure.defect)))
                        + QStringLiteral("<br>%1").arg(count(same, "test case", "test cases"));
            }
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                        .arg(issue, escaped(failure.key), escaped(failure.title), escaped(failure.status),
                             escaped(failure.runName) + (failure.tester.isEmpty() ? QString() : QStringLiteral("<br>%1").arg(escaped(failure.tester))), escaped(failure.notes));
        }
        html += QStringLiteral("</table>");
    }

    // ---- what keeps going wrong
    html += QStringLiteral("<h2>Test cases that keep failing</h2>");
    if (standing.failing.isEmpty())
        html += QStringLiteral("<p>None: no test case failed or was blocked in two runs.</p>");
    else
    {
        html += QStringLiteral("<table><tr><th width=\"14%\">Key</th><th>Test case</th><th width=\"20%\">Suite</th><th width=\"18%\">Failed or blocked</th><th width=\"12%\">Last time</th></tr>");
        for (const QaCaseStanding &one : standing.failing)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>in %4 of %5 runs</td><td>%6</td></tr>")
                        .arg(escaped(one.key), escaped(one.title), escaped(one.suite)).arg(one.bad).arg(one.ran).arg(escaped(one.lastStatus));
        html += QStringLiteral("</table>");
    }

    // ---- what nobody has run
    html += QStringLiteral("<h2>Never run</h2>");
    if (standing.neverRun.isEmpty())
        html += standing.cases == 0 ? QStringLiteral("<p>The project has no test cases.</p>") : QStringLiteral("<p>None: every test case has a result in some run.</p>");
    else
    {
        html += QStringLiteral("<p>%1 no run has a result for.</p>").arg(count(int(standing.neverRun.size()), "test case", "test cases"));
        html += QStringLiteral("<table><tr><th width=\"14%\">Key</th><th>Test case</th><th width=\"26%\">Suite</th></tr>");
        const int shown = qMin(100, int(standing.neverRun.size()));
        for (int i = 0; i < shown; ++i)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                        .arg(escaped(standing.neverRun.at(i).key), escaped(standing.neverRun.at(i).title), escaped(standing.neverRun.at(i).suite));
        html += QStringLiteral("</table>");
        if (shown < standing.neverRun.size())
            html += QStringLiteral("<p>... and %1 more. (\"Never run\" above the list on the left shows them all.)</p>").arg(standing.neverRun.size() - shown);
    }
    html += QStringLiteral("</body></html>");
    return html;
}

DashboardPanel::DashboardPanel(QaDatabase *database, QWidget *parent)
    : QWidget(parent)
    , m_database(database)
{
    m_view = new QTextBrowser(this);
    m_view->setObjectName(QStringLiteral("dashboardView"));
    // An issue's link opens in the browser.
    m_view->setOpenExternalLinks(true);
    // A document: paper is white, whatever the theme of the program.
    QPalette paper = m_view->palette();
    paper.setColor(QPalette::Base, Qt::white);
    paper.setColor(QPalette::Text, Qt::black);
    m_view->setPalette(paper);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
}

void DashboardPanel::setProject(qint64 projectId, const QString &projectName)
{
    m_projectId = projectId;
    m_projectName = projectName;
}

QString DashboardPanel::html() const
{
    if (m_projectId == 0)
        return QString();
    QaDashboard standing;
    QString error;
    if (!m_database->dashboard(m_projectId, standing, error))
        return QStringLiteral("<html><body><p>%1</p></body></html>").arg(error.toHtmlEscaped());
    QString issueUrl;
    QList<QaProject> projects;
    m_database->projects(projects, error);
    for (const QaProject &project : std::as_const(projects))
        if (project.id == m_projectId)
            issueUrl = project.issueUrl;
    return Dashboard::html(m_projectName, standing, issueUrl);
}

void DashboardPanel::refresh()
{
    const QString document = html();
    m_view->setHtml(document.isEmpty() ? QStringLiteral("<html><body><p>Choose a project in the list on the left.</p></body></html>") : document);
}
