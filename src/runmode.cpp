#include "runmode.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>

RunMode::RunMode(QaDatabase *database, const QaRun &run, const QString &tester, const QList<qint64> &caseIds, qint64 startCaseId, QWidget *parent)
    : QDialog(parent)
    , m_database(database)
    , m_run(run)
    , m_tester(tester)
    , m_caseIds(caseIds)
{
    setObjectName(QStringLiteral("runMode"));
    setWindowTitle(QStringLiteral("Run Mode - %1").arg(run.name));
    // Larger than the rest of the program: this is read at arm's length, beside what is tested.
    QFont larger = font();
    larger.setPointSizeF(larger.pointSizeF() * 1.25);
    setFont(larger);
    QFont heading = larger;
    heading.setBold(true);
    heading.setPointSizeF(larger.pointSizeF() * 1.3);

    m_progress = new QLabel(this);
    m_progress->setObjectName(QStringLiteral("modeProgress"));
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("modeTitle"));
    m_title->setFont(heading);
    m_title->setWordWrap(true);
    m_before = new QLabel(this);
    m_before->setObjectName(QStringLiteral("modeBefore"));
    m_before->setWordWrap(true);
    m_before->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_steps = new QTableWidget(0, 3, this);
    m_steps->setObjectName(QStringLiteral("modeSteps"));
    m_steps->setHorizontalHeaderLabels({ QStringLiteral("#"), QStringLiteral("Step: what to do"), QStringLiteral("Expected: what is to happen") });
    m_steps->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_steps->setSelectionMode(QAbstractItemView::NoSelection);
    m_steps->setFocusPolicy(Qt::NoFocus);       // the keys are for the results, not for the table
    m_steps->setWordWrap(true);
    m_steps->verticalHeader()->hide();
    m_steps->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_steps->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_steps->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_steps->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    m_notes = new QPlainTextEdit(this);
    m_notes->setObjectName(QStringLiteral("modeNotes"));
    m_notes->setPlaceholderText(QStringLiteral("What happened - for a failure: what was seen instead of what was expected. (Esc leaves the notes.)"));
    m_notes->setMaximumHeight(90);
    m_notes->setTabChangesFocus(true);
    m_notes->installEventFilter(this);
    m_failedStep = new QSpinBox(this);
    m_failedStep->setObjectName(QStringLiteral("modeFailedStep"));
    m_failedStep->setSpecialValueText(QStringLiteral("not said"));

    m_fileList = new QListWidget(this);
    m_fileList->setObjectName(QStringLiteral("modeFiles"));
    m_fileList->setFlow(QListView::LeftToRight);
    m_fileList->setMaximumHeight(48);
    m_fileList->setFocusPolicy(Qt::ClickFocus);
    auto *paste = new QPushButton(QStringLiteral("Paste Screenshot"), this);
    paste->setObjectName(QStringLiteral("modePaste"));
    paste->setToolTip(QStringLiteral("The picture on the clipboard goes with this result (Ctrl+V). Win+Shift+S takes one."));
    auto *attach = new QPushButton(QStringLiteral("Attach File..."), this);
    auto *remove = new QPushButton(QStringLiteral("Remove File..."), this);
    for (QPushButton *button : { paste, attach, remove })
        button->setFocusPolicy(Qt::NoFocus);
    auto *files = new QHBoxLayout;
    files->addWidget(new QLabel(QStringLiteral("Files:"), this));
    files->addWidget(m_fileList, 1);
    files->addWidget(paste);
    files->addWidget(attach);
    files->addWidget(remove);

    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("modeProblem"));
    m_problem->setWordWrap(true);
    QFont bold = larger;
    bold.setBold(true);
    m_problem->setFont(bold);
    m_problem->hide();

    auto *buttons = new QHBoxLayout;
    auto *previous = new QPushButton(QString::fromUtf8("\xE2\x86\x90 Previous"), this);
    auto *next = new QPushButton(QString::fromUtf8("Next \xE2\x86\x92"), this);
    buttons->addWidget(previous);
    buttons->addWidget(new QLabel(QStringLiteral("Failed at step:"), this));
    buttons->addWidget(m_failedStep);
    buttons->addStretch(1);
    const QStringList captions { QStringLiteral("Passed (P)"), QStringLiteral("Failed (F)"), QStringLiteral("Blocked (B)"), QStringLiteral("Skipped (S)"), QStringLiteral("Not Run (U)") };
    const QStringList statuses { QaDatabase::passed(), QaDatabase::failed(), QaDatabase::blocked(), QaDatabase::skipped(), QaDatabase::notRun() };
    const QList<Qt::Key> keys { Qt::Key_P, Qt::Key_F, Qt::Key_B, Qt::Key_S, Qt::Key_U };
    for (int i = 0; i < statuses.size(); ++i)
    {
        auto *button = new QPushButton(captions.at(i), this);
        button->setObjectName(QStringLiteral("mode") + QString(statuses.at(i)).remove(QLatin1Char(' ')));
        button->setFocusPolicy(Qt::NoFocus);
        button->setMinimumHeight(44);
        const QString status = statuses.at(i);
        connect(button, &QPushButton::clicked, this, [this, status]() { mark(status); });
        // The key alone - which a field that is typed in keeps for itself.
        connect(new QShortcut(QKeySequence(keys.at(i)), this), &QShortcut::activated, this, [this, status]() { mark(status); });
        buttons->addWidget(button);
        m_statusButtons << button;
    }
    buttons->addStretch(1);
    buttons->addWidget(next);
    auto *close = new QPushButton(QStringLiteral("Close"), this);
    buttons->addWidget(close);
    for (QPushButton *button : { previous, next, close })
        button->setFocusPolicy(Qt::NoFocus);

    auto *keysHint = new QLabel(QString::fromUtf8("Keys:  P passed   F failed   B blocked   S skipped   U not run   \xE2\x86\x90 \xE2\x86\x92 the case before / after   "
                                                  "Ctrl+V paste a screenshot   Esc out of the notes, then close"), this);
    keysHint->setWordWrap(true);
    keysHint->setFont(font());

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_progress);
    layout->addWidget(m_title);
    layout->addWidget(m_before);
    layout->addWidget(m_steps, 1);
    layout->addWidget(m_notes);
    layout->addLayout(files);
    layout->addWidget(m_problem);
    layout->addLayout(buttons);
    layout->addWidget(keysHint);
    resize(1100, 760);

    connect(previous, &QPushButton::clicked, this, [this]() { go(-1); });
    connect(next, &QPushButton::clicked, this, [this]() { go(1); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(new QShortcut(QKeySequence(Qt::Key_Left), this), &QShortcut::activated, this, [this]() { go(-1); });
    connect(new QShortcut(QKeySequence(Qt::Key_Right), this), &QShortcut::activated, this, [this]() { go(1); });
    connect(new QShortcut(QKeySequence(QKeySequence::Paste), this), &QShortcut::activated, this, [this]() { pasteScreenshot(); });
    connect(paste, &QPushButton::clicked, this, [this]() { pasteScreenshot(); });
    connect(attach, &QPushButton::clicked, this, [this]() {
        auto *chooser = new QFileDialog(this, QStringLiteral("Attach File"));
        chooser->setAttribute(Qt::WA_DeleteOnClose);
        chooser->setFileMode(QFileDialog::ExistingFile);
        connect(chooser, &QFileDialog::fileSelected, this, [this](const QString &file) { attachFile(file); });
        chooser->open();
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = m_fileList->currentRow();
        if (row < 0 || row >= m_files.size())
        {
            say(QStringLiteral("Click the file to remove in the list first."));
            return;
        }
        auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Remove File"), QStringLiteral("Remove %1 from this result?").arg(m_files.at(row).name),
                                    QMessageBox::Yes | QMessageBox::Cancel, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setInformativeText(QStringLiteral("The copy that is kept with the database is deleted. This cannot be undone."));
        box->button(QMessageBox::Yes)->setText(QStringLiteral("Remove"));
        box->setDefaultButton(QMessageBox::Cancel);
        const qint64 id = m_files.at(row).id;
        connect(box, &QMessageBox::finished, this, [this, id](int answer) {
            for (int i = 0; answer == QMessageBox::Yes && i < m_files.size(); ++i)
                if (m_files.at(i).id == id)
                    removeAttachment(i);
        });
        box->open();
    });
    connect(m_fileList, &QListWidget::itemDoubleClicked, this, [this]() {
        const int row = m_fileList->currentRow();
        if (row >= 0 && row < m_files.size() && !QDesktopServices::openUrl(QUrl::fromLocalFile(m_database->attachmentPath(m_files.at(row)))))
            say(QStringLiteral("The file could not be opened from here: %1").arg(QDir::toNativeSeparators(m_database->attachmentPath(m_files.at(row)))));
    });

    // Where to begin: the case that was selected - or, if that one has its result, the first that has none.
    m_at = m_caseIds.isEmpty() ? -1 : qMax(0, int(m_caseIds.indexOf(startCaseId)));
    showCase();
    if (m_at >= 0 && m_result.status != QaDatabase::notRun())
    {
        QList<QaResult> all;
        QString error;
        m_database->results(m_run.id, all, error);
        for (int step = 0; step < m_caseIds.size(); ++step)
        {
            const int candidate = (m_at + step) % int(m_caseIds.size());
            bool open = false;
            for (const QaResult &result : std::as_const(all))
                open = open || (result.caseId == m_caseIds.at(candidate) && result.status == QaDatabase::notRun());
            if (open)
            {
                m_at = candidate;
                showCase();
                break;
            }
        }
    }
    setFocus();
}

qint64 RunMode::caseId() const
{
    return m_at >= 0 && m_at < m_caseIds.size() ? m_caseIds.at(m_at) : 0;
}

void RunMode::say(const QString &text)
{
    m_problem->setText(text);
    m_problem->setVisible(!text.isEmpty());
}

// Esc in the notes leaves the notes instead of closing; Ctrl+V with a picture on the
// clipboard attaches it instead of pasting nothing.
bool RunMode::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_notes && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape)
        {
            setFocus();
            return true;
        }
        const QMimeData *clip = QGuiApplication::clipboard()->mimeData();
        if (key->matches(QKeySequence::Paste) && clip && clip->hasImage() && !clip->hasText())
        {
            pasteScreenshot();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

// The case at m_at, as it is in the database now.
void RunMode::showCase()
{
    say(QString());
    m_result = QaResult();
    QList<QaResult> all;
    QString error;
    m_database->results(m_run.id, all, error);
    int left = 0;
    for (const QaResult &result : std::as_const(all))
    {
        if (result.caseId == caseId())
            m_result = result;
        if (m_caseIds.contains(result.caseId) && result.status == QaDatabase::notRun())
            ++left;
    }
    const bool has = m_result.caseId != 0;
    for (QPushButton *button : std::as_const(m_statusButtons))
        button->setEnabled(has);
    m_notes->setEnabled(has);
    m_failedStep->setEnabled(has);
    m_steps->setRowCount(0);
    if (!has)
    {
        m_progress->setText(m_run.name);
        m_title->setText(m_caseIds.isEmpty() ? QStringLiteral("There are no test cases to work through here.") : QStringLiteral("This test case is not part of the run any more."));
        m_before->clear();
        m_notes->clear();
        m_files.clear();
        m_fileList->clear();
        if (!error.isEmpty())
            say(error);
        return;
    }

    QStringList where { QStringLiteral("Case %1 of %2").arg(m_at + 1).arg(m_caseIds.size()),
                        left == 0 ? QStringLiteral("all have a result") : QStringLiteral("%1 not run").arg(left), m_result.suiteName };
    if (m_result.status != QaDatabase::notRun())
        where << QStringLiteral("now: %1%2").arg(m_result.status, m_result.tester.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(m_result.tester));
    if (!m_result.assigned.isEmpty())
        where << QStringLiteral("for %1").arg(m_result.assigned);
    m_progress->setText(where.join(QString::fromUtf8("  \xC2\xB7  ")));
    m_title->setText(QStringLiteral("%1  %2").arg(m_result.caseKey, m_result.caseTitle));

    QaCase testCase;
    m_database->loadCase(m_result.caseId, testCase, error);
    QStringList before;
    if (!m_run.builds.isEmpty())
        before << QStringLiteral("Tested: %1").arg(m_run.builds.split(QLatin1Char('\n'), Qt::SkipEmptyParts).join(QStringLiteral("; ")));
    else if (!m_run.build.isEmpty())
        before << QStringLiteral("Tested: %1").arg(m_run.build);
    if (!m_result.area.isEmpty())
        before << QStringLiteral("Run on: %1").arg(m_result.area);
    if (!testCase.preconditions.isEmpty())
        before << QStringLiteral("Before you start: %1").arg(testCase.preconditions);
    if (!testCase.notes.isEmpty())
        before << QStringLiteral("Note: %1").arg(testCase.notes);
    m_before->setText(before.join(QLatin1Char('\n')));
    for (int i = 0; i < testCase.steps.size(); ++i)
    {
        m_steps->insertRow(i);
        m_steps->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_steps->setItem(i, 1, new QTableWidgetItem(testCase.steps.at(i).action));
        m_steps->setItem(i, 2, new QTableWidgetItem(testCase.steps.at(i).expected));
    }
    m_steps->resizeRowsToContents();
    m_notes->setPlainText(m_result.notes);
    m_failedStep->setRange(0, int(testCase.steps.size()));
    m_failedStep->setValue(m_result.failedStep);
    showFiles();
}

void RunMode::showFiles()
{
    QString error;
    m_database->attachments(m_run.id, caseId(), m_files, error);
    m_fileList->clear();
    for (const QaAttachment &file : std::as_const(m_files))
    {
        auto *item = new QListWidgetItem(file.name, m_fileList);
        item->setToolTip(QStringLiteral("%1%2\nDouble-click opens it.").arg(QDir::toNativeSeparators(m_database->attachmentPath(file)),
                                                                             file.addedBy.isEmpty() ? QString() : QStringLiteral("\nadded by %1").arg(file.addedBy)));
    }
}

void RunMode::go(int by)
{
    if (m_caseIds.isEmpty())
        return;
    const int to = m_at + by;
    if (to < 0 || to >= m_caseIds.size())
    {
        say(by < 0 ? QStringLiteral("This is the first case.") : QStringLiteral("This is the last case."));
        return;
    }
    m_at = to;
    showCase();
}

void RunMode::mark(const QString &status)
{
    if (m_result.caseId == 0)
        return;
    // A failure says what went wrong: that is what whoever fixes it reads.
    if (status == QaDatabase::failed() && m_notes->toPlainText().trimmed().isEmpty())
    {
        say(QStringLiteral("Say in the notes what happened instead of what was expected - then press F again."));
        m_notes->setFocus();
        return;
    }
    QString error;
    if (!m_database->setResult(m_run.id, m_result.caseId, status, m_notes->toPlainText().trimmed(), m_failedStep->value(), m_tester.isEmpty() ? m_run.tester : m_tester, error))
    {
        say(error);
        return;
    }
    setFocus();
    emit stored();

    // On to the next case that is not run - after this one, then from the top.
    if (status != QaDatabase::notRun())
    {
        QList<QaResult> all;
        m_database->results(m_run.id, all, error);
        for (int step = 1; step < m_caseIds.size(); ++step)
        {
            const int candidate = (m_at + step) % int(m_caseIds.size());
            for (const QaResult &result : std::as_const(all))
            {
                if (result.caseId == m_caseIds.at(candidate) && result.status == QaDatabase::notRun())
                {
                    m_at = candidate;
                    showCase();
                    return;
                }
            }
        }
    }
    showCase();
    if (status != QaDatabase::notRun())
        say(QStringLiteral("That was the last one: every case here has a result. (Close, or go back with the arrow keys.)"));
}

bool RunMode::attachFile(const QString &file, const QString &name)
{
    if (m_result.caseId == 0)
        return false;
    QaAttachment attachment;
    QString error;
    if (!m_database->attach(m_run.id, m_result.caseId, file, name, attachment, error))
    {
        say(error);
        return false;
    }
    say(QString());
    showFiles();
    emit stored();
    return true;
}

bool RunMode::pasteScreenshot()
{
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull())
    {
        say(QStringLiteral("There is no picture on the clipboard. Win+Shift+S takes one of a part of the screen; then press Ctrl+V here."));
        return false;
    }
    // Written as a file first: what goes with a result is a copy of a file.
    QTemporaryDir folder;
    const QString name = QStringLiteral("screenshot-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString file = folder.filePath(name);
    if (!folder.isValid() || !image.save(file, "PNG"))
    {
        say(QStringLiteral("The picture could not be written."));
        return false;
    }
    return attachFile(file, name);
}

bool RunMode::removeAttachment(int row)
{
    if (row < 0 || row >= m_files.size())
        return false;
    QString error;
    if (!m_database->removeAttachment(m_files.at(row).id, error))
    {
        say(error);
        return false;
    }
    showFiles();
    emit stored();
    return true;
}
