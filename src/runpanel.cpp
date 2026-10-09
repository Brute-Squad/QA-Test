#include "runpanel.h"

#include "report.h"
#include "runmode.h"

#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QStandardPaths>
#include <QUrl>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
    QString localTime(const QString &utc)
    {
        const QDateTime when = QDateTime::fromString(utc, Qt::ISODate);
        return when.isValid() ? QLocale().toString(when.toLocalTime(), QLocale::ShortFormat) : utc;
    }

    // A mark before a result, so that it reads at a glance whatever the colors are.
    QString marked(const QString &status)
    {
        if (status == QaDatabase::passed())
            return QString::fromUtf8("\xE2\x9C\x94 ") + status;
        if (status == QaDatabase::failed())
            return QString::fromUtf8("\xE2\x9C\x98 ") + status;
        if (status == QaDatabase::blocked())
            return QString::fromUtf8("\xE2\x96\xA0 ") + status;
        if (status == QaDatabase::skipped())
            return QString::fromUtf8("\xE2\x80\x93 ") + status;
        return status;
    }
}

RunPanel::RunPanel(QaDatabase *database, QWidget *parent)
    : QWidget(parent)
    , m_database(database)
    , m_tester(QSettings().value(QStringLiteral("Tester")).toString())
{
    // Who is testing here: the name given for a run the last time, else the one they
    // are logged in to Windows with - so that a result in a shared run says whose it is.
    if (m_tester.trimmed().isEmpty())
        m_tester = QaDatabase::systemUser();
    build();
}

void RunPanel::build()
{
    m_run = new QComboBox(this);
    m_run->setObjectName(QStringLiteral("runChoice"));
    m_run->setMinimumContentsLength(30);
    m_new = new QPushButton(QStringLiteral("&New Run..."), this);
    m_new->setObjectName(QStringLiteral("newRun"));
    m_rerun = new QPushButton(QStringLiteral("Run Failed &Again..."), this);
    m_rerun->setObjectName(QStringLiteral("runRerun"));
    m_rerun->setToolTip(QStringLiteral("A new run of the test cases that failed or were blocked in this one."));
    m_finish = new QPushButton(QStringLiteral("&Finish"), this);
    m_delete = new QPushButton(QStringLiteral("&Delete Run..."), this);
    m_report = new QPushButton(QStringLiteral("&Report..."), this);
    m_csv = new QPushButton(QStringLiteral("&CSV..."), this);
    m_csv->setObjectName(QStringLiteral("runCsv"));
    m_csv->setToolTip(QStringLiteral("The run as a table for a spreadsheet: a line per test case."));
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("Run:"), this));
    top->addWidget(m_run, 1);
    top->addWidget(m_new);
    top->addWidget(m_rerun);
    top->addWidget(m_finish);
    top->addWidget(m_report);
    top->addWidget(m_csv);
    top->addWidget(m_delete);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("runSummary"));
    QFont bold = m_summary->font();
    bold.setBold(true);
    m_summary->setFont(bold);
    m_summary->setWordWrap(true);

    m_filter = new QComboBox(this);
    m_filter->setObjectName(QStringLiteral("runFilter"));
    m_filter->addItem(QStringLiteral("All results"));
    m_filter->addItems(QaDatabase::statuses());
    // Whose cases: a run's cases can be given to testers (Assign...).
    m_whose = new QComboBox(this);
    m_whose->setObjectName(QStringLiteral("runWhose"));
    m_whose->addItems({ QStringLiteral("Everybody's cases"), QStringLiteral("My cases"), QStringLiteral("Nobody's cases") });
    m_whose->setToolTip(QStringLiteral("My cases: those that were assigned to %1 in this run.").arg(m_tester.isEmpty() ? QStringLiteral("you") : m_tester));
    m_assign = new QPushButton(QStringLiteral("Assi&gn..."), this);
    m_assign->setObjectName(QStringLiteral("runAssign"));
    m_assign->setToolTip(QStringLiteral("Say whose the selected test cases are in this run (several with Ctrl or Shift)."));
    m_mode = new QPushButton(QStringLiteral("Run &Mode"), this);
    m_mode->setObjectName(QStringLiteral("runMode"));
    m_mode->setToolTip(QStringLiteral("Work through the cases that are shown, one at a time and large: P passed, F failed, B blocked, S skipped."));
    auto *second = new QHBoxLayout;
    second->addWidget(m_summary, 1);
    second->addWidget(new QLabel(QStringLiteral("Show:"), this));
    second->addWidget(m_filter);
    second->addWidget(m_whose);
    second->addWidget(m_assign);
    second->addWidget(m_mode);
    m_builds = new QLabel(this);
    m_builds->setObjectName(QStringLiteral("runBuilds"));
    m_builds->setWordWrap(true);
    m_builds->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_builds->hide();

    m_table = new QTableWidget(0, 10, this);
    m_table->setObjectName(QStringLiteral("runResults"));
    m_table->setHorizontalHeaderLabels({ QStringLiteral("Suite"), QStringLiteral("Key"), QStringLiteral("Title"), QStringLiteral("Result"), QStringLiteral("By"),
                                         QStringLiteral("When"), QStringLiteral("Notes"), QStringLiteral("For"), QStringLiteral("Files"), QStringLiteral("Issue") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();

    // ---- the case that is selected, to work through
    auto *work = new QWidget(this);
    m_caseTitle = new QLabel(work);
    m_caseTitle->setObjectName(QStringLiteral("runCaseTitle"));
    m_caseTitle->setFont(bold);
    m_caseTitle->setWordWrap(true);
    m_preconditions = new QLabel(work);
    m_preconditions->setObjectName(QStringLiteral("runPreconditions"));
    m_preconditions->setWordWrap(true);
    m_preconditions->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_steps = new QTableWidget(0, 3, work);
    m_steps->setObjectName(QStringLiteral("runSteps"));
    m_steps->setHorizontalHeaderLabels({ QStringLiteral("#"), QStringLiteral("Step: what to do"), QStringLiteral("Expected: what is to happen") });
    m_steps->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_steps->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_steps->setWordWrap(true);
    m_steps->verticalHeader()->hide();
    m_steps->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_steps->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_steps->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_steps->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_notes = new QPlainTextEdit(work);
    m_notes->setObjectName(QStringLiteral("runNotes"));
    m_notes->setPlaceholderText(QStringLiteral("What happened - for a failure: what was seen instead of what was expected"));
    m_notes->setMaximumHeight(70);
    m_failedStep = new QSpinBox(work);
    m_failedStep->setObjectName(QStringLiteral("runFailedStep"));
    m_failedStep->setSpecialValueText(QStringLiteral("not said"));
    m_failedStep->setToolTip(QStringLiteral("For a failure: the number of the step that did not go as expected."));
    m_files = new QLabel(work);
    m_files->setObjectName(QStringLiteral("runFiles"));
    m_files->setWordWrap(true);
    m_files->hide();
    m_problem = new QLabel(work);
    m_problem->setObjectName(QStringLiteral("runProblem"));
    m_problem->setWordWrap(true);
    m_problem->hide();

    // The issue a failure was reported as: its number, or its whole address.
    m_defect = new QLineEdit(work);
    m_defect->setObjectName(QStringLiteral("runDefect"));
    m_defect->setMaxLength(200);
    m_defect->setMaximumWidth(170);
    m_defect->setPlaceholderText(QStringLiteral("its number: 123"));
    m_defect->setToolTip(QStringLiteral("The issue a failed or blocked test case was reported as: stored with Failed or Blocked, and when you leave the field."));
    m_openDefect = new QPushButton(QStringLiteral("&Open"), work);
    m_openDefect->setObjectName(QStringLiteral("runOpenDefect"));
    m_openDefect->setToolTip(QStringLiteral("Open the issue in the browser."));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel(QStringLiteral("Failed at step:"), work));
    buttons->addWidget(m_failedStep);
    buttons->addWidget(new QLabel(QStringLiteral("Issue:"), work));
    buttons->addWidget(m_defect);
    buttons->addWidget(m_openDefect);
    buttons->addStretch(1);
    const QStringList captions { QStringLiteral("&Passed"), QStringLiteral("&Failed"), QStringLiteral("&Blocked"), QStringLiteral("S&kipped"), QStringLiteral("Not R&un") };
    const QStringList statuses { QaDatabase::passed(), QaDatabase::failed(), QaDatabase::blocked(), QaDatabase::skipped(), QaDatabase::notRun() };
    for (int i = 0; i < statuses.size(); ++i)
    {
        auto *button = new QPushButton(captions.at(i), work);
        button->setObjectName(QStringLiteral("mark") + QString(statuses.at(i)).remove(QLatin1Char(' ')));
        const QString status = statuses.at(i);
        connect(button, &QPushButton::clicked, this, [this, status]() { store(status); });
        buttons->addWidget(button);
        m_statusButtons << button;
    }

    auto *workLayout = new QVBoxLayout(work);
    workLayout->setContentsMargins(0, 0, 0, 0);
    workLayout->addWidget(m_caseTitle);
    workLayout->addWidget(m_preconditions);
    workLayout->addWidget(m_steps, 1);
    workLayout->addWidget(m_notes);
    workLayout->addWidget(m_files);
    workLayout->addWidget(m_problem);
    workLayout->addLayout(buttons);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_table);
    splitter->addWidget(work);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(second);
    layout->addWidget(m_builds);
    layout->addWidget(splitter, 1);

    connect(m_run, &QComboBox::currentIndexChanged, this, [this]() { showRun(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this]() { showRun(); });
    connect(m_whose, &QComboBox::currentIndexChanged, this, [this]() { showRun(); });
    connect(m_rerun, &QPushButton::clicked, this, &RunPanel::askForRerun);
    connect(m_assign, &QPushButton::clicked, this, &RunPanel::askToAssign);
    connect(m_mode, &QPushButton::clicked, this, [this]() { openRunMode(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &RunPanel::showSelected);
    connect(m_new, &QPushButton::clicked, this, &RunPanel::askForRun);
    connect(m_delete, &QPushButton::clicked, this, &RunPanel::deleteRun);
    connect(m_finish, &QPushButton::clicked, this, &RunPanel::toggleFinished);
    connect(m_report, &QPushButton::clicked, this, &RunPanel::showReport);
    connect(m_csv, &QPushButton::clicked, this, &RunPanel::exportCsv);
    connect(m_defect, &QLineEdit::editingFinished, this, &RunPanel::storeDefect);
    connect(m_openDefect, &QPushButton::clicked, this, [this]() {
        const QString link = defectLink();
        if (link.isEmpty() || !QDesktopServices::openUrl(QUrl(link)))
        {
            m_problem->setText(m_defect->text().trimmed().isEmpty()
                                   ? QStringLiteral("Type the issue's number first.")
                                   : QStringLiteral("There is no address for that issue: say where the project's issues are with Edit > Project Issue Tracker... - "
                                                    "or type the issue's whole address here."));
            m_problem->show();
        }
    });

    setProject(0, QString());
}

qint64 RunPanel::runId() const
{
    return currentRun().id;
}

QaRun RunPanel::currentRun() const
{
    const int index = m_run->currentIndex();
    return index >= 0 && index < m_runs.size() ? m_runs.at(index) : QaRun();
}

int RunPanel::selectedIndex() const
{
    const int row = m_table->currentRow();
    return row >= 0 && row < m_shown.size() && m_table->selectionModel()->hasSelection() ? m_shown.at(row) : -1;
}

void RunPanel::setProject(qint64 projectId, const QString &projectName)
{
    m_projectId = projectId;
    m_projectName = projectName;
    loadRuns();
}

// What is in the database now - a shared one may have changed - with what is being
// typed about the selected case left as it is.
void RunPanel::reload()
{
    const int before = selectedIndex();
    const qint64 caseId = before >= 0 ? m_results.at(before).caseId : 0;
    const QString typed = m_notes->toPlainText();
    const QString issue = m_defect->text();
    const int step = m_failedStep->value();

    loadRuns(runId());

    const int after = selectedIndex();
    if (caseId != 0 && after >= 0 && m_results.at(after).caseId == caseId)
    {
        m_notes->setPlainText(typed);
        m_defect->setText(issue);
        m_failedStep->setValue(step);
    }
}

void RunPanel::loadRuns(qint64 selectId)
{
    QString error;
    m_runs.clear();
    if (m_projectId != 0)
        m_database->runs(m_projectId, m_runs, error);

    m_run->blockSignals(true);
    m_run->clear();
    int select = m_runs.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_runs.size(); ++i)
    {
        const QaRun &run = m_runs.at(i);
        m_run->addItem(QStringLiteral("%1%2  -  %3%4").arg(run.name, run.build.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(run.build), localTime(run.started),
                                                          run.finished.isEmpty() ? QString() : QStringLiteral("  -  finished")));
        if (run.id == selectId)
            select = i;
    }
    m_run->setCurrentIndex(select);
    m_run->blockSignals(false);
    m_new->setEnabled(m_projectId != 0);
    showRun();
}

void RunPanel::showRun()
{
    const QaRun run = currentRun();
    const qint64 keep = selectedIndex() >= 0 ? m_results.at(selectedIndex()).caseId : 0;

    QString error;
    m_results.clear();
    QaSummary counts;
    if (run.id != 0)
    {
        m_database->results(run.id, m_results, error);
        m_database->summary(run.id, counts, error);
    }
    m_summary->setText(m_projectId == 0 ? QStringLiteral("Choose a project in the list on the left.")
                       : run.id == 0 ? QStringLiteral("%1 has no test run yet. New Run... starts one.").arg(m_projectName)
                                     : counts.text() + (run.finished.isEmpty() ? QString() : QStringLiteral("  (finished %1)").arg(localTime(run.finished))));
    m_finish->setEnabled(run.id != 0);
    m_finish->setText(run.finished.isEmpty() ? QStringLiteral("&Finish") : QStringLiteral("Re&open"));
    m_delete->setEnabled(run.id != 0);
    m_report->setEnabled(run.id != 0);
    m_rerun->setEnabled(counts.failed + counts.blocked > 0);
    // What is tested: the build of each component, or the one build the run says.
    const QString tested = !run.builds.isEmpty() ? run.builds.split(QLatin1Char('\n'), Qt::SkipEmptyParts).join(QString::fromUtf8("   \xC2\xB7   "))
                                                  : run.build;
    m_builds->setText(tested.isEmpty() ? QString() : QStringLiteral("Tested: %1").arg(tested));
    m_builds->setVisible(!tested.isEmpty());

    const QString only = m_filter->currentIndex() > 0 ? m_filter->currentText() : QString();
    const int whose = m_whose->currentIndex();
    m_shown.clear();
    m_table->blockSignals(true);
    m_table->setRowCount(0);
    int selectRow = -1;
    for (int i = 0; i < m_results.size(); ++i)
    {
        const QaResult &result = m_results.at(i);
        if (!only.isEmpty() && result.status != only)
            continue;
        if ((whose == 1 && (m_tester.isEmpty() || result.assigned.compare(m_tester, Qt::CaseInsensitive) != 0)) || (whose == 2 && !result.assigned.isEmpty()))
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_shown << i;
        const QStringList cells { result.suiteName, result.caseKey, result.caseTitle,
                                  result.status == QaDatabase::failed() && result.failedStep > 0 ? QStringLiteral("%1 (step %2)").arg(marked(result.status)).arg(result.failedStep)
                                                                                                 : marked(result.status),
                                  result.tester, localTime(result.executed), result.notes.simplified(), result.assigned,
                                  result.attachments > 0 ? QString::number(result.attachments) : QString(), result.defect };
        for (int column = 0; column < cells.size(); ++column)
            m_table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        if (result.caseId == keep)
            selectRow = row;
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_table->blockSignals(false);
    m_assign->setEnabled(m_table->rowCount() > 0);
    m_mode->setEnabled(m_table->rowCount() > 0 && run.finished.isEmpty());
    if (selectRow >= 0)
        m_table->selectRow(selectRow);
    else if (m_table->rowCount() > 0)
        m_table->selectRow(0);
    showSelected();
}

// The case that is selected: what there is to do, and what was noted of it.
void RunPanel::showSelected()
{
    const int index = selectedIndex();
    const bool has = index >= 0;
    const bool open = has && currentRun().finished.isEmpty();
    m_problem->hide();
    m_files->hide();
    m_steps->setRowCount(0);
    for (QPushButton *button : std::as_const(m_statusButtons))
        button->setEnabled(open);
    m_notes->setEnabled(open);
    m_failedStep->setEnabled(open);
    m_defect->setEnabled(open);
    m_openDefect->setEnabled(has);
    m_csv->setEnabled(currentRun().id != 0);
    if (!has)
    {
        m_caseTitle->setText(currentRun().id == 0 ? QString() : QStringLiteral("Select a test case above."));
        m_preconditions->clear();
        m_notes->clear();
        m_defect->clear();
        m_failedStep->setRange(0, 0);
        return;
    }

    const QaResult &result = m_results.at(index);
    QaCase testCase;
    QString error;
    m_database->loadCase(result.caseId, testCase, error);
    m_caseTitle->setText(QStringLiteral("%1  %2   [%3%4]").arg(result.caseKey, result.caseTitle, result.priority,
                                                             result.area.isEmpty() ? QString() : QStringLiteral(", %1").arg(result.area)));
    QStringList before;
    if (!testCase.preconditions.isEmpty())
        before << QStringLiteral("Before you start: %1").arg(testCase.preconditions);
    if (!testCase.notes.isEmpty())
        before << QStringLiteral("Note: %1").arg(testCase.notes);
    if (!currentRun().finished.isEmpty())
        before << QStringLiteral("The run is finished: reopen it to change a result.");
    m_preconditions->setText(before.join(QLatin1Char('\n')));
    m_preconditions->setVisible(!before.isEmpty());
    for (int i = 0; i < testCase.steps.size(); ++i)
    {
        m_steps->insertRow(i);
        m_steps->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_steps->setItem(i, 1, new QTableWidgetItem(testCase.steps.at(i).action));
        m_steps->setItem(i, 2, new QTableWidgetItem(testCase.steps.at(i).expected));
    }
    m_steps->resizeRowsToContents();
    m_notes->setPlainText(result.notes);
    m_failedStep->setRange(0, int(testCase.steps.size()));
    m_failedStep->setValue(result.failedStep);
    m_defect->setText(result.defect);

    // The files that go with the result - which Run Mode attaches, opens and removes.
    QList<QaAttachment> files;
    m_database->attachments(result.runId, result.caseId, files, error);
    QStringList names;
    for (const QaAttachment &file : std::as_const(files))
        names << file.name;
    m_files->setText(names.isEmpty() ? QString() : QStringLiteral("Files: %1   (Run Mode opens them)").arg(names.join(QStringLiteral(", "))));
    m_files->setVisible(!names.isEmpty());
}

QList<qint64> RunPanel::selectedCaseIds() const
{
    QList<qint64> ids;
    if (!m_table->selectionModel())
        return ids;
    for (const QModelIndex &index : m_table->selectionModel()->selectedRows())
        if (index.row() >= 0 && index.row() < m_shown.size())
            ids << m_results.at(m_shown.at(index.row())).caseId;
    return ids;
}

bool RunPanel::assignSelected(const QString &tester, QString &error)
{
    const QaRun run = currentRun();
    const QList<qint64> ids = selectedCaseIds();
    if (run.id == 0 || ids.isEmpty())
    {
        error = QStringLiteral("Select the test cases first.");
        return false;
    }
    if (!m_database->assign(run.id, ids, tester, error))
        return false;
    showRun();
    return true;
}

// Whose the selected cases are: somebody the run knows already, or a name typed.
void RunPanel::askToAssign()
{
    const QaRun run = currentRun();
    const int cases = int(selectedCaseIds().size());
    if (run.id == 0 || cases == 0)
        return;
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("assignDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Assign"));
    auto *who = new QComboBox(dialog);
    who->setObjectName(QStringLiteral("assignTo"));
    who->setEditable(true);
    QStringList names = m_database->testers(run.id);
    if (!m_tester.isEmpty() && !names.contains(m_tester, Qt::CaseInsensitive))
        names.prepend(m_tester);
    who->addItems(names);
    who->setCurrentText(m_tester);
    auto *problem = new QLabel(dialog);
    problem->setWordWrap(true);
    problem->hide();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    auto *form = new QFormLayout;
    form->addRow(cases == 1 ? QStringLiteral("The selected test case is for:") : QStringLiteral("The %1 selected test cases are for:").arg(cases), who);
    auto *layout = new QVBoxLayout(dialog);
    layout->addLayout(form);
    layout->addWidget(new QLabel(QStringLiteral("Leave the name empty for nobody in particular. \"My cases\" then shows each tester their own."), dialog));
    layout->addWidget(problem);
    layout->addWidget(buttons);
    dialog->setMinimumWidth(460);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, who, problem]() {
        QString error;
        if (assignSelected(who->currentText(), error))
        {
            dialog->accept();
            return;
        }
        problem->setText(error);
        problem->show();
    });
    dialog->open();
}

RunMode *RunPanel::openRunMode()
{
    const QaRun run = currentRun();
    if (run.id == 0 || m_shown.isEmpty() || !run.finished.isEmpty())
        return nullptr;
    QList<qint64> ids;
    for (const int index : std::as_const(m_shown))
        ids << m_results.at(index).caseId;
    const int selected = selectedIndex();
    auto *mode = new RunMode(m_database, run, m_tester, ids, selected >= 0 ? m_results.at(selected).caseId : ids.first(), this);
    mode->setAttribute(Qt::WA_DeleteOnClose);
    // What is stored there shows here, and in the tree's marks.
    connect(mode, &RunMode::stored, this, [this]() {
        showRun();
        emit resultStored();
    });
    connect(mode, &QDialog::finished, this, [this]() { showRun(); });
    mode->open();
    return mode;
}

bool RunPanel::createRerun(const QString &name, QString &error)
{
    const QaRun from = currentRun();
    if (from.id == 0)
    {
        error = QStringLiteral("There is no test run to run again.");
        return false;
    }
    QaRun run;
    run.name = name;
    run.tester = m_tester;
    if (!m_database->createRerun(run, from.id, { QaDatabase::failed(), QaDatabase::blocked() }, error))
        return false;
    m_filter->setCurrentIndex(0);
    m_whose->setCurrentIndex(0);
    loadRuns(run.id);
    return true;
}

void RunPanel::askForRerun()
{
    const QaRun from = currentRun();
    if (from.id == 0)
        return;
    QaSummary counts;
    QString error;
    m_database->summary(from.id, counts, error);
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("rerunDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Run Failed Again"));
    auto *name = new QLineEdit(QStringLiteral("%1 - failed again").arg(from.name), dialog);
    name->setObjectName(QStringLiteral("rerunName"));
    auto *problem = new QLabel(dialog);
    problem->setWordWrap(true);
    problem->hide();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Start Run"));
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Name:"), name);
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(QStringLiteral("A new run of the %1 test cases that failed or were blocked in \"%2\" - each for whom it was there, and of the same build "
                                                "unless you say another afterwards. The run itself stays as it is.").arg(counts.failed + counts.blocked).arg(from.name), dialog));
    static_cast<QLabel *>(layout->itemAt(0)->widget())->setWordWrap(true);
    layout->addLayout(form);
    layout->addWidget(problem);
    layout->addWidget(buttons);
    dialog->setMinimumWidth(520);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, name, problem]() {
        QString why;
        if (createRerun(name->text(), why))
        {
            dialog->accept();
            return;
        }
        problem->setText(why);
        problem->show();
    });
    dialog->open();
}

// The result of the selected case, stored at once; then on to the next that is not run.
void RunPanel::store(const QString &status)
{
    const int index = selectedIndex();
    const QaRun run = currentRun();
    if (index < 0 || run.id == 0)
        return;
    const QaResult result = m_results.at(index);

    // A failure says what went wrong: that is what whoever fixes it reads.
    if (status == QaDatabase::failed() && m_notes->toPlainText().trimmed().isEmpty())
    {
        m_problem->setText(QStringLiteral("Say in the notes what happened instead of what was expected - then press Failed."));
        m_problem->show();
        m_notes->setFocus();
        return;
    }
    QString error;
    if (!m_database->setResult(run.id, result.caseId, status, m_notes->toPlainText().trimmed(), m_failedStep->value(),
                               m_tester.isEmpty() ? run.tester : m_tester, error))
    {
        m_problem->setText(error);
        m_problem->show();
        return;
    }
    // The issue it was reported as goes with a failure and with what is blocked.
    if (status == QaDatabase::failed() || status == QaDatabase::blocked())
        m_database->setDefect(run.id, result.caseId, m_defect->text(), error);

    // The next case that is not run yet, after this one - or, when there is none, this one stays.
    qint64 next = result.caseId;
    if (status != QaDatabase::notRun())
    {
        for (int step = 1; step < m_results.size(); ++step)
        {
            const QaResult &candidate = m_results.at((index + step) % m_results.size());
            if (candidate.status == QaDatabase::notRun())
            {
                next = candidate.caseId;
                break;
            }
        }
    }
    showRun();
    for (int row = 0; row < m_shown.size(); ++row)
        if (m_results.at(m_shown.at(row)).caseId == next)
            m_table->selectRow(row);
    emit resultStored();
}

bool RunPanel::createRun(const QString &name, const QString &build, const QString &tester, const QList<qint64> &suiteIds, QString &error, const QString &builds,
                         const QString &tag)
{
    QaRun run;
    run.projectId = m_projectId;
    run.name = name;
    run.build = build;
    run.tester = tester;
    run.builds = builds;
    if (!m_database->createRun(run, suiteIds, error, tag))
        return false;
    // Who tests here is who said so last; no name is no change.
    if (!tester.trimmed().isEmpty())
    {
        m_tester = tester.trimmed();
        QSettings().setValue(QStringLiteral("Tester"), m_tester);
    }
    m_filter->setCurrentIndex(0);
    m_whose->setCurrentIndex(0);
    loadRuns(run.id);
    return true;
}

// Ask what the run is called, what is tested, who tests and which suites - then make it.
void RunPanel::askForRun()
{
    if (m_projectId == 0)
        return;
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("newRunDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("New Test Run"));

    auto *name = new QLineEdit(QStringLiteral("%1 %2").arg(m_projectName, QDate::currentDate().toString(Qt::ISODate)), dialog);
    name->setObjectName(QStringLiteral("runName"));
    auto *build = new QLineEdit(dialog);
    build->setObjectName(QStringLiteral("runBuild"));
    build->setPlaceholderText(QStringLiteral("The version or build that is tested: 0.2.0 Beta, built 2026-10-09"));
    auto *tester = new QLineEdit(m_tester, dialog);
    tester->setObjectName(QStringLiteral("runTester"));

    // The build of each thing the project is made of, starting from what the run before said.
    QStringList components;
    QList<QaProject> projects;
    QString ignored;
    m_database->projects(projects, ignored);
    for (const QaProject &project : std::as_const(projects))
        if (project.id == m_projectId)
            components = QaDatabase::componentList(project.components);
    const QStringList before = m_database->lastBuilds(m_projectId).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QList<QLineEdit *> componentEdits;
    for (const QString &component : std::as_const(components))
    {
        auto *edit = new QLineEdit(dialog);
        edit->setObjectName(QStringLiteral("runBuildOf") + QString(component).remove(QLatin1Char(' ')));
        edit->setPlaceholderText(QStringLiteral("Its version and when it was built - as its About says"));
        for (const QString &line : before)
            if (line.startsWith(component + QStringLiteral(": ")))
                edit->setText(line.mid(component.size() + 2));
        componentEdits << edit;
    }
    if (!components.isEmpty())
        build->setPlaceholderText(QStringLiteral("In a word, for the list of runs: 0.2.0 Beta"));

    // The suites to run: all of them unless some are unticked.
    auto *suites = new QListWidget(dialog);
    suites->setObjectName(QStringLiteral("runSuites"));
    QList<QaSuite> list;
    QString error;
    m_database->suites(m_projectId, list, error);
    for (const QaSuite &suite : std::as_const(list))
    {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(suite.name).arg(suite.caseCount), suites);
        item->setData(Qt::UserRole, suite.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(suite.caseCount > 0 ? Qt::Checked : Qt::Unchecked);
    }

    // Of every case of those suites, or only of those with one tag: a smoke run.
    auto *tag = new QComboBox(dialog);
    tag->setObjectName(QStringLiteral("runTag"));
    tag->addItem(QStringLiteral("Every test case"));
    tag->addItems(m_database->tags(m_projectId));
    tag->setEnabled(tag->count() > 1);
    tag->setToolTip(QStringLiteral("A test case's tags are on the Test Case tab."));

    auto *problem = new QLabel(dialog);
    problem->setWordWrap(true);
    problem->hide();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Start Run"));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Name:"), name);
    form->addRow(QStringLiteral("Build tested:"), build);
    for (int i = 0; i < componentEdits.size(); ++i)
        form->addRow(QStringLiteral("%1:").arg(components.at(i)), componentEdits.at(i));
    form->addRow(QStringLiteral("Tester:"), tester);
    form->addRow(QStringLiteral("Suites:"), suites);
    form->addRow(QStringLiteral("Only tagged:"), tag);
    auto *layout = new QVBoxLayout(dialog);
    layout->addLayout(form);
    layout->addWidget(problem);
    layout->addWidget(buttons);
    dialog->resize(520, 460);

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, name, build, tester, suites, problem, components, componentEdits, tag]() {
        QStringList builds;
        for (int i = 0; i < componentEdits.size(); ++i)
            if (!componentEdits.at(i)->text().trimmed().isEmpty())
                builds << QStringLiteral("%1: %2").arg(components.at(i), componentEdits.at(i)->text().trimmed());
        QList<qint64> chosen;
        for (int i = 0; i < suites->count(); ++i)
            if (suites->item(i)->checkState() == Qt::Checked)
                chosen << suites->item(i)->data(Qt::UserRole).toLongLong();
        QString error;
        if (chosen.isEmpty())
            error = QStringLiteral("Tick the suites to run.");
        else if (createRun(name->text(), build->text(), tester->text(), chosen, error, builds.join(QLatin1Char('\n')), tag->currentIndex() > 0 ? tag->currentText() : QString()))
        {
            dialog->accept();
            return;
        }
        problem->setText(error);
        problem->show();
    });
    dialog->open();
}

void RunPanel::deleteRun()
{
    const QaRun run = currentRun();
    if (run.id == 0)
        return;
    auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Delete Test Run"), QStringLiteral("Delete the test run \"%1\"?").arg(run.name),
                                QMessageBox::Yes | QMessageBox::Cancel, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText(QStringLiteral("Its results go with it: what passed, what failed and the notes. The test cases themselves stay.\n\nThis cannot be undone."));
    box->button(QMessageBox::Yes)->setText(QStringLiteral("Delete"));
    box->setDefaultButton(QMessageBox::Cancel);
    connect(box, &QMessageBox::finished, this, [this, run](int answer) {
        if (answer != QMessageBox::Yes)
            return;
        QString error;
        m_database->deleteRun(run.id, error);
        loadRuns();
        emit resultStored();
    });
    box->open();
}

// Finished: its results stand, and nothing is marked by mistake. Reopen takes that back.
void RunPanel::toggleFinished()
{
    QaRun run = currentRun();
    if (run.id == 0)
        return;
    run.finished = run.finished.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : QString();
    QString error;
    m_database->updateRun(run, error);
    loadRuns(run.id);
}

QString RunPanel::reportHtml()
{
    const QaRun run = currentRun();
    if (run.id == 0)
        return QString();
    QaSummary counts;
    QString error;
    m_database->summary(run.id, counts, error);
    return Report::html(m_projectName, run, counts, m_results, issueUrl());
}

QString RunPanel::issueUrl() const
{
    QList<QaProject> projects;
    QString error;
    m_database->projects(projects, error);
    for (const QaProject &project : std::as_const(projects))
        if (project.id == m_projectId)
            return project.issueUrl;
    return QString();
}

QString RunPanel::defectLink() const
{
    return QaDatabase::defectUrl(issueUrl(), m_defect->text());
}

// The issue typed for the selected case, stored as the field is left - where the case
// failed or was blocked; elsewhere it waits for Failed or Blocked to be pressed.
void RunPanel::storeDefect()
{
    const int index = selectedIndex();
    const QaRun run = currentRun();
    if (index < 0 || run.id == 0 || !run.finished.isEmpty())
        return;
    const QaResult result = m_results.at(index);
    const QString typed = m_defect->text().simplified();
    if (typed == result.defect || (result.status != QaDatabase::failed() && result.status != QaDatabase::blocked()))
        return;
    QString error;
    if (!m_database->setDefect(run.id, result.caseId, typed, error))
    {
        m_problem->setText(error);
        m_problem->show();
        return;
    }
    // The table follows; what is typed about the case stays.
    reload();
}

QString RunPanel::csv() const
{
    return currentRun().id == 0 ? QString() : Report::csv(m_results);
}

void RunPanel::exportCsv()
{
    const QaRun run = currentRun();
    if (run.id == 0)
        return;
    QString name = QStringLiteral("%1 - %2").arg(m_projectName, run.name);
    for (QChar &character : name)
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') && character != QLatin1Char('.'))
            character = QLatin1Char(' ');
    auto *chooser = new QFileDialog(this, QStringLiteral("Save the Run as CSV"),
                                    QStringLiteral("%1/%2.csv").arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                                    name.simplified().replace(QLatin1Char(' '), QLatin1Char('-'))),
                                    QStringLiteral("CSV (*.csv)"));
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    chooser->setAcceptMode(QFileDialog::AcceptSave);
    chooser->setDefaultSuffix(QStringLiteral("csv"));
    connect(chooser, &QFileDialog::fileSelected, this, [this](const QString &path) {
        QString error;
        if (!Report::saveCsv(csv(), path, error))
        {
            m_problem->setText(error);
            m_problem->show();
        }
    });
    chooser->open();
}

void RunPanel::showReport()
{
    const QString html = reportHtml();
    if (html.isEmpty())
        return;
    auto *dialog = new ReportDialog(QStringLiteral("%1 - %2").arg(m_projectName, currentRun().name), html, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}
