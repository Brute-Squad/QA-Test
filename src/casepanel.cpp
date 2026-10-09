#include "casepanel.h"

#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
    QString localTime(const QString &utc)
    {
        const QDateTime when = QDateTime::fromString(utc, Qt::ISODate);
        return when.isValid() ? QLocale().toString(when.toLocalTime(), QLocale::ShortFormat) : utc;
    }
}

CasePanel::CasePanel(QaDatabase *database, QWidget *parent)
    : QWidget(parent)
    , m_database(database)
{
    m_heading = new QLabel(this);
    QFont bold = m_heading->font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 1.15);
    m_heading->setFont(bold);

    m_key = new QLineEdit(this);
    m_key->setObjectName(QStringLiteral("caseKey"));
    m_key->setMaxLength(40);
    m_title = new QLineEdit(this);
    m_title->setObjectName(QStringLiteral("caseTitle"));
    m_title->setMaxLength(200);
    m_priority = new QComboBox(this);
    m_priority->setObjectName(QStringLiteral("casePriority"));
    m_priority->addItems(QaDatabase::priorities());
    // Where it is run: the usual ones to pick, anything else to type.
    m_area = new QComboBox(this);
    m_area->setObjectName(QStringLiteral("caseArea"));
    m_area->setEditable(true);
    m_area->addItems({ QString(), QStringLiteral("Desktop"), QStringLiteral("Phone"), QStringLiteral("Desktop and phone"), QStringLiteral("Server"),
                       QStringLiteral("Installer"), QStringLiteral("Web") });
    m_preconditions = new QPlainTextEdit(this);
    m_preconditions->setObjectName(QStringLiteral("casePreconditions"));
    m_preconditions->setPlaceholderText(QStringLiteral("What has to be there before the first step"));
    m_preconditions->setMaximumHeight(70);

    m_steps = new QTableWidget(0, 2, this);
    m_steps->setObjectName(QStringLiteral("caseSteps"));
    m_steps->setHorizontalHeaderLabels({ QStringLiteral("Step: what to do"), QStringLiteral("Expected: what is to happen") });
    m_steps->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_steps->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_steps->setSelectionMode(QAbstractItemView::SingleSelection);
    m_steps->setWordWrap(true);
    m_steps->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    m_addStep = new QPushButton(QStringLiteral("&Add Step"), this);
    m_removeStep = new QPushButton(QStringLiteral("Re&move Step"), this);
    m_up = new QPushButton(QStringLiteral("Move &Up"), this);
    m_down = new QPushButton(QStringLiteral("Move &Down"), this);
    auto *stepButtons = new QHBoxLayout;
    stepButtons->addWidget(m_addStep);
    stepButtons->addWidget(m_removeStep);
    stepButtons->addWidget(m_up);
    stepButtons->addWidget(m_down);
    stepButtons->addStretch(1);

    m_notes = new QPlainTextEdit(this);
    m_notes->setObjectName(QStringLiteral("caseNotes"));
    m_notes->setPlaceholderText(QStringLiteral("Notes for whoever runs it"));
    m_notes->setMaximumHeight(60);

    m_history = new QTableWidget(0, 5, this);
    m_history->setObjectName(QStringLiteral("caseHistory"));
    m_history->setHorizontalHeaderLabels({ QStringLiteral("Run"), QStringLiteral("Result"), QStringLiteral("By"), QStringLiteral("When"), QStringLiteral("Notes") });
    m_history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_history->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_history->verticalHeader()->hide();
    m_history->horizontalHeader()->setStretchLastSection(true);
    m_history->setMaximumHeight(130);

    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("caseProblem"));
    m_problem->setWordWrap(true);
    m_problem->hide();

    m_save = new QPushButton(QStringLiteral("&Save"), this);
    m_save->setObjectName(QStringLiteral("caseSave"));
    m_revert = new QPushButton(QStringLiteral("&Revert"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_revert);
    buttons->addWidget(m_save);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Key:"), m_key);
    form->addRow(QStringLiteral("Title:"), m_title);
    auto *line = new QHBoxLayout;
    line->addWidget(m_priority);
    line->addWidget(new QLabel(QStringLiteral("Run on:"), this));
    line->addWidget(m_area, 1);
    form->addRow(QStringLiteral("Priority:"), line);
    form->addRow(QStringLiteral("Preconditions:"), m_preconditions);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addLayout(form);
    layout->addWidget(m_steps, 1);
    layout->addLayout(stepButtons);
    layout->addWidget(m_notes);
    layout->addWidget(new QLabel(QStringLiteral("How it went:"), this));
    layout->addWidget(m_history);
    layout->addWidget(m_problem);
    layout->addLayout(buttons);

    const auto touched = [this]() {
        if (!m_filling)
            setChanged(true);
    };
    connect(m_key, &QLineEdit::textChanged, this, touched);
    connect(m_title, &QLineEdit::textChanged, this, touched);
    connect(m_priority, &QComboBox::currentIndexChanged, this, touched);
    connect(m_area, &QComboBox::currentTextChanged, this, touched);
    connect(m_preconditions, &QPlainTextEdit::textChanged, this, touched);
    connect(m_notes, &QPlainTextEdit::textChanged, this, touched);
    connect(m_steps, &QTableWidget::itemChanged, this, touched);

    connect(m_addStep, &QPushButton::clicked, this, [this]() {
        const int row = m_steps->currentRow() >= 0 ? m_steps->currentRow() + 1 : m_steps->rowCount();
        addStep(QaStep(), row);
        m_steps->setCurrentCell(row, 0);
        m_steps->editItem(m_steps->item(row, 0));
        setChanged(true);
    });
    connect(m_removeStep, &QPushButton::clicked, this, [this]() {
        if (m_steps->currentRow() >= 0)
        {
            m_steps->removeRow(m_steps->currentRow());
            setChanged(true);
        }
    });
    connect(m_up, &QPushButton::clicked, this, [this]() { moveStep(-1); });
    connect(m_down, &QPushButton::clicked, this, [this]() { moveStep(1); });
    connect(m_save, &QPushButton::clicked, this, &CasePanel::save);
    connect(m_revert, &QPushButton::clicked, this, [this]() {
        if (m_case.id != 0)
            showCase(m_case.id);
        else
            newCase(m_case.suiteId);
    });

    showCase(0);
}

void CasePanel::showProblem(const QString &text)
{
    m_problem->setText(text);
    m_problem->setVisible(!text.isEmpty());
}

void CasePanel::setChanged(bool changed)
{
    const bool has = m_case.id != 0 || m_case.suiteId != 0;
    m_changed = changed && has;
    // Save once something was changed and the case has what it needs.
    m_save->setEnabled(m_changed && !m_key->text().trimmed().isEmpty() && !m_title->text().trimmed().isEmpty());
    m_revert->setEnabled(m_changed);
    if (changed)
        showProblem(QString());
    emit changedChanged(m_changed);
}

void CasePanel::addStep(const QaStep &step, int row)
{
    if (row < 0 || row > m_steps->rowCount())
        row = m_steps->rowCount();
    const bool filling = m_filling;
    m_filling = true;
    m_steps->insertRow(row);
    m_steps->setItem(row, 0, new QTableWidgetItem(step.action));
    m_steps->setItem(row, 1, new QTableWidgetItem(step.expected));
    m_filling = filling;
}

void CasePanel::moveStep(int by)
{
    const int row = m_steps->currentRow();
    const int other = row + by;
    if (row < 0 || other < 0 || other >= m_steps->rowCount())
        return;
    for (int column = 0; column < 2; ++column)
    {
        const QString text = m_steps->item(row, column)->text();
        m_steps->item(row, column)->setText(m_steps->item(other, column)->text());
        m_steps->item(other, column)->setText(text);
    }
    m_steps->setCurrentCell(other, 0);
    setChanged(true);
}

void CasePanel::showCase(qint64 caseId)
{
    m_case = QaCase();
    QString error;
    if (caseId != 0 && !m_database->loadCase(caseId, m_case, error))
    {
        m_case = QaCase();
        fill();
        showProblem(error);
        return;
    }
    fill();
}

void CasePanel::newCase(qint64 suiteId)
{
    m_case = QaCase();
    m_case.suiteId = suiteId;
    m_case.key = m_database->nextKey(suiteId);
    m_case.priority = QStringLiteral("Medium");
    m_case.steps << QaStep();
    fill();
    m_title->setFocus();
}

void CasePanel::fill()
{
    m_filling = true;
    const bool has = m_case.id != 0 || m_case.suiteId != 0;
    m_heading->setText(!has ? QStringLiteral("Choose a test case in the list on the left - or add one.")
                       : m_case.id == 0 ? QStringLiteral("New test case") : QStringLiteral("%1  %2").arg(m_case.key, m_case.title));
    m_key->setText(m_case.key);
    m_title->setText(m_case.title);
    m_priority->setCurrentIndex(qMax(0, m_priority->findText(m_case.priority.isEmpty() ? QStringLiteral("Medium") : m_case.priority)));
    m_area->setCurrentText(m_case.area);
    m_preconditions->setPlainText(m_case.preconditions);
    m_notes->setPlainText(m_case.notes);
    m_steps->setRowCount(0);
    for (const QaStep &step : std::as_const(m_case.steps))
        addStep(step);
    m_steps->resizeRowsToContents();
    for (QWidget *widget : { static_cast<QWidget *>(m_key), static_cast<QWidget *>(m_title), static_cast<QWidget *>(m_priority), static_cast<QWidget *>(m_area),
                             static_cast<QWidget *>(m_preconditions), static_cast<QWidget *>(m_steps), static_cast<QWidget *>(m_notes),
                             static_cast<QWidget *>(m_addStep), static_cast<QWidget *>(m_removeStep), static_cast<QWidget *>(m_up), static_cast<QWidget *>(m_down) })
        widget->setEnabled(has);
    showHistory();
    showProblem(QString());
    m_filling = false;
    // A new case is something to save as soon as it has a title.
    setChanged(false);
    if (m_case.id == 0 && has)
    {
        m_changed = true;
        m_revert->setEnabled(false);
        emit changedChanged(false);
    }
}

void CasePanel::showHistory()
{
    m_history->setRowCount(0);
    if (m_case.id == 0)
        return;
    QList<QaResult> results;
    QStringList runs;
    QString error;
    if (!m_database->history(m_case.id, results, runs, error))
        return;
    for (int i = 0; i < results.size(); ++i)
    {
        const QaResult &result = results.at(i);
        const int row = m_history->rowCount();
        m_history->insertRow(row);
        const QStringList cells { runs.value(i),
                                  result.status == QaDatabase::failed() && result.failedStep > 0 ? QStringLiteral("%1 at step %2").arg(result.status).arg(result.failedStep)
                                                                                                 : result.status,
                                  result.tester, localTime(result.executed), result.notes.simplified() };
        for (int column = 0; column < cells.size(); ++column)
            m_history->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
    m_history->resizeColumnsToContents();
    m_history->horizontalHeader()->setStretchLastSection(true);
}

void CasePanel::collect(QaCase &testCase) const
{
    testCase = m_case;
    testCase.key = m_key->text().trimmed();
    testCase.title = m_title->text().trimmed();
    testCase.priority = m_priority->currentText();
    testCase.area = m_area->currentText().trimmed();
    testCase.preconditions = m_preconditions->toPlainText().trimmed();
    testCase.notes = m_notes->toPlainText().trimmed();
    testCase.steps.clear();
    for (int row = 0; row < m_steps->rowCount(); ++row)
        testCase.steps << QaStep { m_steps->item(row, 0) ? m_steps->item(row, 0)->text() : QString(),
                                   m_steps->item(row, 1) ? m_steps->item(row, 1)->text() : QString() };
}

bool CasePanel::save()
{
    if (m_case.id == 0 && m_case.suiteId == 0)
        return true;
    // What is being typed into a cell is part of it.
    m_steps->setCurrentCell(m_steps->currentRow(), m_steps->currentColumn() == 0 ? 1 : 0);

    QaCase typed;
    collect(typed);
    QString error;
    if (!m_database->saveCase(typed, error))
    {
        showProblem(error);
        return false;
    }
    const qint64 id = typed.id;
    showCase(id);
    emit saved(id);
    return true;
}
