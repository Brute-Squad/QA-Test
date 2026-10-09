#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "qadatabase.h"

#include <QWidget>

class QLabel;
class QTextBrowser;

// How a project stands, over all its test runs - the Dashboard tab:
//
//   Test runs                 each run, the oldest first, with its counts and
//                             its pass rate - of the cases that have a
//                             verdict, how many passed - as a bar, and the
//                             rates in a row: 50% > 75% > 100%
//   Open failures, by issue   the cases whose newest result failed or was
//                             blocked, under the issue they were reported as
//                             (a link, where the project says where its
//                             issues are); those without an issue last
//   Cases that keep failing   failed or blocked in two runs or more
//   Never run                 the cases no run has a result for
//
// A document, black on white whatever the theme - like the report of a run.
namespace Dashboard
{
    QString html(const QString &projectName, const QaDashboard &standing, const QString &issueUrl);
}

class DashboardPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardPanel(QaDatabase *database, QWidget *parent = nullptr);

    // The project it is about (0 = none).
    void setProject(qint64 projectId, const QString &projectName);
    qint64 projectId() const { return m_projectId; }

    // Read the project's standing again and show it.
    void refresh();
    // The document, as it would be shown now ("" = no project).
    QString html() const;

private:
    QaDatabase   *m_database = nullptr;
    qint64        m_projectId = 0;
    QString       m_projectName;
    QTextBrowser *m_view = nullptr;
};

#endif // DASHBOARD_H
