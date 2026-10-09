#ifndef RUNPANEL_H
#define RUNPANEL_H

#include "qadatabase.h"

#include <QWidget>

class QComboBox;
class RunMode;
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
//
// To get through a run faster:
// - Run Mode (runmode.h) works through the cases that are shown, one at a
//   time and large, with the result on one key.
// - A case of a run can be somebody's: select cases (several with Ctrl or
//   Shift) and Assign... . "My cases" beside the filter by result shows a
//   tester their own; "Not run" what is left of them.
// - Run Failed Again... makes a new run of the cases that failed or were
//   blocked in this one.
// - A run says the build of each of the project's components (Server,
//   Desktop app, Phone app): New Run... asks for each, starting from what the
//   run before said.
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
    // `builds`: the build of each component, a line each ("Server: 0.2.0, built ...").
    // `tag`: only the cases that have that tag.
    bool createRun(const QString &name, const QString &build, const QString &tester, const QList<qint64> &suiteIds, QString &error,
                   const QString &builds = QString(), const QString &tag = QString());
    // A new run of what failed or was blocked in the run that is shown.
    bool createRerun(const QString &name, QString &error);
    // The selected cases are that tester's in this run ("" = nobody's).
    bool assignSelected(const QString &tester, QString &error);
    // Run Mode on the cases that are shown, beginning at the selected one (nullptr = no run, or none shown).
    RunMode *openRunMode();
    // The run that is shown as a table for a spreadsheet (report.h; "" = no run).
    QString csv() const;
    // The address of the issue typed for the selected case ("" = none, or the
    // project does not say where its issues are).
    QString defectLink() const;
    // Who is testing here.
    QString tester() const { return m_tester; }

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
    void askForRerun();
    void askToAssign();
    QList<qint64> selectedCaseIds() const;
    void deleteRun();
    void toggleFinished();
    void showReport();
    void exportCsv();
    void storeDefect();
    QString issueUrl() const;
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
    QComboBox      *m_whose = nullptr;
    QPushButton    *m_rerun = nullptr;
    QPushButton    *m_assign = nullptr;
    QPushButton    *m_mode = nullptr;
    QLabel         *m_builds = nullptr;
    QLabel         *m_files = nullptr;
    QLineEdit      *m_defect = nullptr;         // the issue a failure was reported as
    QPushButton    *m_openDefect = nullptr;
    QPushButton    *m_csv = nullptr;
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
