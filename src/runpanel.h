#ifndef RUNPANEL_H
#define RUNPANEL_H

#include "qadatabase.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

// The test runs of one project, and running one.
//
// At the top: the run that is shown (the newest first), New Run..., Finish
// / Reopen, Delete Run... and Report...; a line of how it stands ("42 test
// cases: 30 passed, 2 failed ... 79% done"); and a filter by result.
// The table is the run's cases - Suite / Key / Title / Result / By / When /
// Notes. Under it the selected case to work through: what has to be there
// first, its steps with what to expect, a place for notes and - for a
// failure - the step that failed, and Passed / Failed / Blocked / Skipped /
// Not Run. A result is stored at once, and the next case that is not run is
// selected.
class RunPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RunPanel(QaDatabase *database, QWidget *parent = nullptr);

    // Show the runs of that project (0 = none).
    void setProject(qint64 projectId, const QString &projectName);
    qint64 projectId() const { return m_projectId; }
    qint64 runId() const;

    // Make a run without asking (what New Run... does once its dialog is
    // answered): of those suites, or of every suite when none is named.
    bool createRun(const QString &name, const QString &build, const QString &tester, const QList<qint64> &suiteIds, QString &error);

    // The report of the run that is shown, as HTML ("" = no run).
    QString reportHtml();

public slots:
    void reload();

signals:
    // A result was stored: the tree's marks are to follow.
    void resultStored();

private:
    void build();
    void loadRuns(qint64 selectId = 0);
    void showRun();
    void showSelected();
    void store(const QString &status);
    void askForRun();
    void deleteRun();
    void toggleFinished();
    void showReport();
    QaRun currentRun() const;
    int selectedIndex() const;

    QaDatabase     *m_database = nullptr;
    qint64          m_projectId = 0;
    QString         m_projectName;
    QList<QaRun>    m_runs;
    QList<QaResult> m_results;      // of the run that is shown
    QList<int>      m_shown;        // the table's rows: places in m_results
    QString         m_tester;       // who is testing: asked for with a run, kept for the next

    QComboBox      *m_run = nullptr;
    QPushButton    *m_new = nullptr;
    QPushButton    *m_finish = nullptr;
    QPushButton    *m_delete = nullptr;
    QPushButton    *m_report = nullptr;
    QLabel         *m_summary = nullptr;
    QComboBox      *m_filter = nullptr;
    QTableWidget   *m_table = nullptr;
    QLabel         *m_caseTitle = nullptr;
    QLabel         *m_preconditions = nullptr;
    QTableWidget   *m_steps = nullptr;
    QPlainTextEdit *m_notes = nullptr;
    QSpinBox       *m_failedStep = nullptr;
    QLabel         *m_problem = nullptr;
    QList<QPushButton *> m_statusButtons;
};

#endif // RUNPANEL_H
