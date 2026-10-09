#include "mainwindow.h"

#include "casepanel.h"
#include "qabackup.h"
#include "qaconfig.h"
#include "qashare.h"
#include "runpanel.h"

#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace
{
    const int kKindRole = Qt::UserRole;
    const int kIdRole = Qt::UserRole + 1;

    // How a case went the last time, as a mark before its key.
    QString marked(const QaCase &testCase)
    {
        const QString mark = testCase.lastStatus == QaDatabase::passed() ? QString::fromUtf8("\xE2\x9C\x94 ")
                           : testCase.lastStatus == QaDatabase::failed() ? QString::fromUtf8("\xE2\x9C\x98 ")
                           : testCase.lastStatus == QaDatabase::blocked() ? QString::fromUtf8("\xE2\x96\xA0 ")
                           : testCase.lastStatus == QaDatabase::skipped() ? QString::fromUtf8("\xE2\x80\x93 ")
                                                                          : QString();
        return QStringLiteral("%1%2  %3").arg(mark, testCase.key, testCase.title);
    }
}

MainWindow::MainWindow(QaDatabase *database, QWidget *parent)
    : QMainWindow(parent)
    , m_database(database)
{
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("tree"));
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

    m_case = new CasePanel(m_database, this);
    m_runs = new RunPanel(m_database, this);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("tabs"));
    m_tabs->addTab(m_case, QStringLiteral("Test Case"));
    m_tabs->addTab(m_runs, QStringLiteral("Test Runs"));

    auto *splitter = new QSplitter(this);
    splitter->addWidget(m_tree);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    // A line above everything, only while the database cannot be reached.
    m_lostBar = new QWidget(this);
    m_lostBar->setObjectName(QStringLiteral("lostBar"));
    m_lostText = new QLabel(m_lostBar);
    m_lostText->setObjectName(QStringLiteral("lostText"));
    m_lostText->setWordWrap(true);
    QFont lostFont = m_lostText->font();
    lostFont.setBold(true);
    m_lostText->setFont(lostFont);
    auto *again = new QPushButton(QStringLiteral("Try &Again"), m_lostBar);
    again->setObjectName(QStringLiteral("lostRetry"));
    auto *lostLayout = new QHBoxLayout(m_lostBar);
    lostLayout->addWidget(m_lostText, 1);
    lostLayout->addWidget(again);
    m_lostBar->hide();
    connect(again, &QPushButton::clicked, this, &MainWindow::retryDatabase);
    m_retry = new QTimer(this);
    m_retry->setInterval(15000);
    connect(m_retry, &QTimer::timeout, this, &MainWindow::retryDatabase);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->addWidget(m_lostBar);
    centralLayout->addWidget(splitter, 1);
    setCentralWidget(central);
    resize(1280, 800);
    splitter->setSizes({ 420, 860 });

    // ---- menus
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    connect(file->addAction(QStringLiteral("&New Database...")), &QAction::triggered, this, &MainWindow::newDatabase);
    connect(file->addAction(QStringLiteral("&Open Database...")), &QAction::triggered, this, &MainWindow::openDatabase);
    connect(file->addAction(QStringLiteral("Use a &Shared Database...")), &QAction::triggered, this, &MainWindow::chooseShared);
    connect(file->addAction(QStringLiteral("&Where Is the Database?")), &QAction::triggered, this, &MainWindow::whereIsTheDatabase);
    connect(file->addAction(QStringLiteral("Open &Backups Folder")), &QAction::triggered, this, &MainWindow::showBackups);
    connect(file->addAction(QStringLiteral("Edit &Configuration File...")), &QAction::triggered, this, &MainWindow::editConfiguration);
    file->addSeparator();
    connect(file->addAction(QStringLiteral("&Import Test Scripts...")), &QAction::triggered, this, &MainWindow::importScripts);
    m_export = file->addAction(QStringLiteral("&Export Project..."));
    connect(m_export, &QAction::triggered, this, &MainWindow::exportProject);
    file->addSeparator();
    QAction *quit = file->addAction(QStringLiteral("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    connect(edit->addAction(QStringLiteral("New &Project...")), &QAction::triggered, this, &MainWindow::newProject);
    m_newSuite = edit->addAction(QStringLiteral("New &Suite..."));
    connect(m_newSuite, &QAction::triggered, this, &MainWindow::newSuite);
    m_newCase = edit->addAction(QStringLiteral("New &Test Case"));
    m_newCase->setShortcut(QKeySequence::New);
    connect(m_newCase, &QAction::triggered, this, &MainWindow::newCase);
    edit->addSeparator();
    m_rename = edit->addAction(QStringLiteral("&Rename..."));
    connect(m_rename, &QAction::triggered, this, &MainWindow::renameSelected);
    m_delete = edit->addAction(QStringLiteral("&Delete..."));
    connect(m_delete, &QAction::triggered, this, &MainWindow::deleteSelected);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    QAction *refresh = view->addAction(QStringLiteral("&Refresh"));
    refresh->setShortcut(QKeySequence::Refresh);
    connect(refresh, &QAction::triggered, this, &MainWindow::reload);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this]() { onSelected(); });
    connect(m_case, &CasePanel::saved, this, [this](qint64 id) {
        fillTree(CaseItem, id);
        statusBar()->showMessage(QStringLiteral("Test case saved"), 4000);
    });
    connect(m_runs, &RunPanel::resultStored, this, [this]() {
        // The marks in the tree follow; what is selected stays.
        const Kind kind = m_tree->currentItem() ? Kind(m_tree->currentItem()->data(0, kKindRole).toInt()) : ProjectItem;
        fillTree(kind, m_tree->currentItem() ? m_tree->currentItem()->data(0, kIdRole).toLongLong() : 0);
    });

    reload();
}

void MainWindow::setDatabaseSource(const QString &why, const QString &configFile)
{
    m_why = why;
    m_configFile = configFile;
}

// The database may be shared: when the window comes back to the front it shows what
// is there now - unless something is being typed, which a reload must not disturb.
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || !isActiveWindow())
        return;
    if (isLost())
        retryDatabase();
    else if (m_database->isOpen() && !m_case->isChanged())
        reload();
    if (!isLost())
        dailyBackup();
}

bool MainWindow::isLost() const
{
    return m_lostBar->isVisibleTo(this);
}

// Can the database be had? If not - a shared drive has gone, a connection dropped -
// the line at the top says so and it is tried again every few seconds; what the
// window shows, and what is being typed, stays as it is meanwhile.
bool MainWindow::checkDatabase()
{
    QString error, ignored;
    if (m_database->reachable(error) || m_database->reopen(ignored))
    {
        if (isLost())
        {
            m_lostBar->hide();
            m_retry->stop();
            statusBar()->showMessage(QStringLiteral("The database is back."), 6000);
        }
        return true;
    }
    m_lostText->setText(error.remove(QStringLiteral(" What you typed is still here: try again when it is back."))
                        + QStringLiteral(" What you see and what you typed stays here; it is tried again every few seconds."));
    m_lostBar->show();
    m_retry->start();
    return false;
}

void MainWindow::retryDatabase()
{
    if (!checkDatabase())
        return;
    // Back: what is there now - but not over what is being typed.
    if (!m_case->isChanged())
        reload();
    else
        updateTitle();
}

void MainWindow::setBackups(int keep, const QString &folder)
{
    m_backupKeep = keep;
    m_backupFolder = folder;
    m_backupOf.clear();
    dailyBackup();
}

// Today's copy of the database, once a day and once per database.
void MainWindow::dailyBackup()
{
    const QDate today = QDate::currentDate();
    if (m_backupKeep <= 0 || !m_database->isOpen() || (m_backupOf == m_database->path() && m_backupDay == today))
        return;
    m_backupOf = m_database->path();
    m_backupDay = today;
    const QaBackupOutcome outcome = QaBackup::daily(*m_database, today, m_backupKeep, m_backupFolder);
    if (!outcome.problem.isEmpty())
    {
        m_backupNote = outcome.problem;
        statusBar()->showMessage(outcome.problem, 20000);
    }
    else if (!outcome.file.isEmpty())
    {
        m_backupNote = QStringLiteral("Today's copy is %1.").arg(QDir::toNativeSeparators(outcome.file));
        if (outcome.made)
            statusBar()->showMessage(QStringLiteral("Today's copy of the database was made: %1").arg(QDir::toNativeSeparators(outcome.file)), 8000);
    }
}

void MainWindow::showBackups()
{
    const QString folder = QaBackup::folderFor(m_database->path(), m_backupFolder);
    const QStringList copies = QaBackup::copies(m_database->path(), m_backupFolder);
    const QString how = QStringLiteral("To go back to a copy: close QA Test Tracker on every PC, then put the copy in the database's place, under the database's name:\n\n%1")
                            .arg(QDir::toNativeSeparators(m_database->path()));
    if (m_backupKeep <= 0)
        say(QStringLiteral("Backups"), QStringLiteral("No copies of the database are made."),
            QStringLiteral("The configuration file says Keep=0 under [Backup] (File > Edit Configuration File...)."));
    else if (copies.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        say(QStringLiteral("Backups"), copies.isEmpty() ? QStringLiteral("There is no copy of the database yet.") : QStringLiteral("The folder could not be opened from here."),
            QStringLiteral("A copy is made on every day the program is used, into\n\n%1\n\n%2%3").arg(QDir::toNativeSeparators(folder), m_backupNote.isEmpty() ? QString() : m_backupNote + QStringLiteral("\n\n"), how));
    else
        say(QStringLiteral("Backups"), QStringLiteral("The folder with the copies is open: %1 of them, the newest of %2.")
                .arg(copies.size()).arg(QFileInfo(copies.first()).completeBaseName().right(10)), how);
}

// ---- a shared database -----------------------------------------------------------------------------

QString MainWindow::useSharedDatabase(const QString &path, bool networkName)
{
    if (m_configFile.isEmpty())
        return QStringLiteral("The program does not know its configuration file.");
    // What a connected drive is connected to is the same on every PC; its letter is not.
    const QString network = networkName ? QaShare::networkName(path) : QString();
    const QString wanted = QDir::cleanPath(network.isEmpty() ? path : network);
    if (!QFileInfo::exists(wanted))
        return QStringLiteral("There is no database %1. If it is on a shared drive, see that the drive is connected.").arg(QDir::toNativeSeparators(wanted));
    if (m_case->isChanged() && m_case->caseId() != 0 && !m_case->save())
        return QStringLiteral("The test case that is open could not be saved. Save or revert it first.");

    // The file, with the line for the database - before anything is opened, so that a
    // file that cannot be written leaves everything as it was.
    QString text = QaConfigFile::sample();
    QFile file(m_configFile);
    if (file.exists())
    {
        if (!file.open(QIODevice::ReadOnly))
            return QStringLiteral("%1 could not be read: %2").arg(QDir::toNativeSeparators(m_configFile), file.errorString());
        text = QString::fromUtf8(file.readAll());
        file.close();
    }
    const QString before = m_database->path();
    QString error;
    if (!m_database->open(wanted, error))
    {
        QString ignored;
        if (!before.isEmpty())
            m_database->open(before, ignored);
        reload();
        return error;
    }
    QDir().mkpath(QFileInfo(m_configFile).absolutePath());
    const QByteArray written = QaConfigFile::withDatabasePath(text, QDir::toNativeSeparators(wanted)).toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(written) != written.size())
    {
        const QString why = file.errorString();
        QString ignored;
        if (!before.isEmpty())
            m_database->open(before, ignored);
        else
            m_database->close();
        reload();
        return QStringLiteral("%1 could not be written: %2. Nothing was changed.").arg(QDir::toNativeSeparators(m_configFile), why);
    }
    file.close();
    // What was opened by hand before does not go first any more: the file says it now.
    QSettings().remove(QStringLiteral("Database"));
    m_why = QStringLiteral("The configuration file says so: %1").arg(QDir::toNativeSeparators(m_configFile));
    m_case->showCase(0);
    m_tree->clear();
    reload();
    dailyBackup();
    return QString();
}

void MainWindow::chooseShared()
{
    auto *chooser = new QFileDialog(this, QStringLiteral("Use a Shared Database"), QFileInfo(m_database->path()).absolutePath(),
                                    QStringLiteral("QA databases (*.sqlite);;All files (*)"));
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    chooser->setFileMode(QFileDialog::ExistingFile);
    connect(chooser, &QFileDialog::fileSelected, this, [this](const QString &path) {
        const auto use = [this](const QString &chosen, bool networkName) {
            const QString problem = useSharedDatabase(chosen, networkName);
            if (problem.isEmpty())
                say(QStringLiteral("Use a Shared Database"), QStringLiteral("This is your database from now on."),
                    QStringLiteral("%1\n\nIt is written into your configuration file, so the program opens it every time. Everybody who is to work in the same "
                                   "database does the same on their PC: File > Use a Shared Database...").arg(QDir::toNativeSeparators(m_database->path())));
            else
                say(QStringLiteral("Use a Shared Database"), QStringLiteral("The database is not used."), problem);
        };
        const QString network = QaShare::networkName(path);
        if (network.isEmpty())
        {
            use(path, false);
            return;
        }
        // A drive letter is this PC's own: ask once, with the better answer in front.
        auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Use a Shared Database"),
                                    QStringLiteral("%1 is a network drive: on this PC it stands for %2.").arg(path.left(2).toUpper(), QDir::toNativeSeparators(network.left(network.size() - (path.size() - 2)))),
                                    QMessageBox::NoButton, this);
        box->setObjectName(QStringLiteral("networkNameBox"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setInformativeText(QStringLiteral("Another PC may have that drive under another letter, or not at all. The network name is the same everywhere:\n\n%1\n\n"
                                               "Write the database down under its network name?").arg(QDir::toNativeSeparators(network)));
        QPushButton *byName = box->addButton(QStringLiteral("Use the Network Name"), QMessageBox::AcceptRole);
        QPushButton *byLetter = box->addButton(QStringLiteral("Keep %1").arg(path.left(2).toUpper()), QMessageBox::ActionRole);
        box->addButton(QMessageBox::Cancel);
        box->setDefaultButton(byName);
        connect(box, &QMessageBox::finished, this, [box, byName, byLetter, use, path]() {
            if (box->clickedButton() == byName)
                use(path, true);
            else if (box->clickedButton() == byLetter)
                use(path, false);
        });
        box->open();
    });
    chooser->open();
}

void MainWindow::whereIsTheDatabase()
{
    QString details = m_why.isEmpty() ? QString() : m_why + QStringLiteral("\n\n");
    details += QStringLiteral("To work in one database with others: copy the file to a shared drive while nobody has it open, then choose "
                              "File > Use a Shared Database... on every PC. That writes where it is into the configuration file:\n\n%1\n\n"
                              "You are %2 here: that name is written beside what you change and the results you record.")
                   .arg(QDir::toNativeSeparators(m_configFile), m_database->user().isEmpty() ? QStringLiteral("(no name)") : m_database->user());
    if (!m_backupNote.isEmpty())
        details += QStringLiteral("\n\n") + m_backupNote;
    say(QStringLiteral("Where Is the Database?"), QDir::toNativeSeparators(m_database->path()), details);
}

bool MainWindow::writeSampleConfig(const QString &path, QString &problem)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QaConfigFile::sample().toUtf8()) < 0)
    {
        problem = QStringLiteral("%1 could not be written: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

QString MainWindow::importBundled(QaDatabase &database, const QString &folder)
{
    QList<QaProject> projects;
    QString error;
    if (!database.projects(projects, error) || !projects.isEmpty())
        return QString();
    QStringList said;
    const QDir scripts(folder);
    for (const QString &name : scripts.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name))
    {
        QString message;
        said << (importFile(database, scripts.filePath(name), message) ? QStringLiteral("%1: %2").arg(name, message)
                                                                       : QStringLiteral("%1 was not imported: %2").arg(name, message));
    }
    return said.join(QLatin1Char(' '));
}

// The configuration file in the editor Windows has for it - made first, with every
// setting explained, when it is not there yet.
void MainWindow::editConfiguration()
{
    QString problem;
    if (!QFileInfo::exists(m_configFile) && !writeSampleConfig(m_configFile, problem))
    {
        say(QStringLiteral("Configuration File"), QStringLiteral("The configuration file could not be made."), problem);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_configFile)))
    {
        say(QStringLiteral("Configuration File"), QStringLiteral("The file could not be opened from here."),
            QStringLiteral("Open it with Notepad:\n\n%1").arg(QDir::toNativeSeparators(m_configFile)));
        return;
    }
    say(QStringLiteral("Configuration File"), QStringLiteral("The configuration file is open in its editor."),
        QStringLiteral("%1\n\nTo use a database on a shared drive, take the ; off the line \"Path=\" under [Database] and write where the database is. "
                       "Save the file, then close QA Test Tracker and start it again: the file is read when the program starts.")
            .arg(QDir::toNativeSeparators(m_configFile)));
}

void MainWindow::updateTitle()
{
    setWindowTitle(m_database->isOpen() ? QStringLiteral("QA Test Tracker - %1").arg(QDir::toNativeSeparators(m_database->path())) : QStringLiteral("QA Test Tracker"));
}

void MainWindow::reload()
{
    // A database that cannot be had just now: what is shown stays.
    if (!checkDatabase())
        return;
    const Kind kind = m_tree->currentItem() ? Kind(m_tree->currentItem()->data(0, kKindRole).toInt()) : ProjectItem;
    fillTree(kind, m_tree->currentItem() ? m_tree->currentItem()->data(0, kIdRole).toLongLong() : 0);
    // The case that is shown, as it is now - somebody else may have changed it.
    if (!m_case->isChanged() && m_case->caseId() != 0)
        m_case->showCase(m_case->caseId());
    m_runs->reload();
    updateTitle();
}

QTreeWidgetItem *MainWindow::find(Kind kind, qint64 id) const
{
    QTreeWidgetItemIterator it(m_tree);
    for (; *it; ++it)
        if ((*it)->data(0, kKindRole).toInt() == kind && (*it)->data(0, kIdRole).toLongLong() == id)
            return *it;
    return nullptr;
}

qint64 MainWindow::currentId(Kind kind) const
{
    for (QTreeWidgetItem *item = m_tree->currentItem(); item; item = item->parent())
        if (item->data(0, kKindRole).toInt() == kind)
            return item->data(0, kIdRole).toLongLong();
    return 0;
}

// Projects, their suites and their cases; what was open stays open.
void MainWindow::fillTree(Kind selectKind, qint64 selectId)
{
    QSet<qint64> openProjects, openSuites;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->isExpanded())
            ((*it)->data(0, kKindRole).toInt() == ProjectItem ? openProjects : openSuites) << (*it)->data(0, kIdRole).toLongLong();
    const bool first = m_tree->topLevelItemCount() == 0;

    m_filling = true;
    m_tree->clear();
    QString error;
    QList<QaProject> projects;
    m_database->projects(projects, error);
    for (const QaProject &project : std::as_const(projects))
    {
        auto *projectItem = new QTreeWidgetItem(m_tree, { project.name });
        projectItem->setData(0, kKindRole, ProjectItem);
        projectItem->setData(0, kIdRole, project.id);
        projectItem->setToolTip(0, project.description);
        QFont bold = projectItem->font(0);
        bold.setBold(true);
        projectItem->setFont(0, bold);

        QList<QaSuite> suites;
        m_database->suites(project.id, suites, error);
        for (const QaSuite &suite : std::as_const(suites))
        {
            auto *suiteItem = new QTreeWidgetItem(projectItem, { QStringLiteral("%1  (%2)").arg(suite.name).arg(suite.caseCount) });
            suiteItem->setData(0, kKindRole, SuiteItem);
            suiteItem->setData(0, kIdRole, suite.id);
            suiteItem->setToolTip(0, suite.description);

            QList<QaCase> cases;
            m_database->cases(suite.id, cases, error);
            for (const QaCase &testCase : std::as_const(cases))
            {
                auto *caseItem = new QTreeWidgetItem(suiteItem, { marked(testCase) });
                caseItem->setData(0, kKindRole, CaseItem);
                caseItem->setData(0, kIdRole, testCase.id);
                caseItem->setToolTip(0, testCase.lastStatus.isEmpty() ? QStringLiteral("Not run yet") : QStringLiteral("Last time: %1").arg(testCase.lastStatus));
            }
            suiteItem->setExpanded(openSuites.contains(suite.id));
        }
        // A project is open the first time, so that its suites are seen.
        projectItem->setExpanded(first || openProjects.contains(project.id));
    }
    m_filling = false;

    QTreeWidgetItem *select = selectId != 0 ? find(selectKind, selectId) : nullptr;
    if (!select && m_tree->topLevelItemCount() > 0)
        select = m_tree->topLevelItem(0);
    if (select)
    {
        m_filling = true;       // what the panels show is put right below, once
        m_tree->setCurrentItem(select);
        m_filling = false;
    }
    onSelected();
}

// The case panel shows the selected case, the run panel the runs of its project.
void MainWindow::onSelected()
{
    if (m_filling)
        return;
    const qint64 caseId = currentId(CaseItem);
    const qint64 projectId = currentId(ProjectItem);

    if (caseId != m_case->caseId() || caseId == 0)
    {
        // What was typed into another case is not lost without a word.
        if (m_case->isChanged() && m_case->caseId() != 0 && m_case->caseId() != caseId && !m_case->save())
        {
            // It could not be stored - the database is away, or somebody else changed the case:
            // the case stays, with what was typed and the reason, instead of being lost.
            if (QTreeWidgetItem *stay = find(CaseItem, m_case->caseId()))
            {
                m_filling = true;
                m_tree->setCurrentItem(stay);
                m_filling = false;
                m_tabs->setCurrentWidget(m_case);
                updateActions();
                return;
            }
        }
        m_case->showCase(caseId);
    }
    if (projectId != m_runs->projectId())
    {
        QString name;
        if (QTreeWidgetItem *item = find(ProjectItem, projectId))
            name = item->text(0);
        m_runs->setProject(projectId, name);
    }
    updateActions();
}

void MainWindow::updateActions()
{
    const bool project = currentId(ProjectItem) != 0;
    const bool suite = currentId(SuiteItem) != 0;
    m_newSuite->setEnabled(project);
    m_newCase->setEnabled(suite);
    m_rename->setEnabled(project && currentId(CaseItem) == 0);
    m_delete->setEnabled(project);
    m_export->setEnabled(project);
}

bool MainWindow::selectCase(const QString &key)
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
    {
        if ((*it)->data(0, kKindRole).toInt() == CaseItem && (*it)->text(0).contains(key + QStringLiteral("  ")))
        {
            m_tree->setCurrentItem(*it);
            return true;
        }
    }
    return false;
}

void MainWindow::say(const QString &title, const QString &text, const QString &details)
{
    auto *box = new QMessageBox(QMessageBox::Information, title, text, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText(details);
    box->open();
}

// A small dialog for one line of text; `store` says whether it was taken, and why not.
void MainWindow::askText(const QString &title, const QString &label, const QString &text, std::function<bool(const QString &, QString &)> store)
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    auto *edit = new QLineEdit(text, dialog);
    edit->setObjectName(QStringLiteral("askedText"));
    edit->setMaxLength(200);
    auto *problem = new QLabel(dialog);
    problem->setWordWrap(true);
    problem->hide();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    auto *form = new QFormLayout;
    form->addRow(label, edit);
    auto *layout = new QVBoxLayout(dialog);
    layout->addLayout(form);
    layout->addWidget(problem);
    layout->addWidget(buttons);
    dialog->setMinimumWidth(420);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, edit, problem, store]() {
        QString error;
        if (store(edit->text().trimmed(), error))
        {
            dialog->accept();
            return;
        }
        problem->setText(error);
        problem->show();
    });
    dialog->open();
}

// ---- the database ---------------------------------------------------------------------------------

void MainWindow::openPath(const QString &path)
{
    QString error;
    const QString before = m_database->path();
    if (!m_database->open(path, error))
    {
        // What was open stays open.
        QString ignored;
        if (!before.isEmpty())
            m_database->open(before, ignored);
        say(QStringLiteral("Open Database"), QStringLiteral("The database could not be opened."), error);
        reload();
        return;
    }
    QSettings().setValue(QStringLiteral("Database"), path);
    m_why = QStringLiteral("It was opened with File > Open Database... or New Database...");
    m_tree->clear();
    reload();
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(QDir::toNativeSeparators(path)), 6000);
    dailyBackup();
}

void MainWindow::newDatabase()
{
    auto *chooser = new QFileDialog(this, QStringLiteral("New Database"), QFileInfo(m_database->path()).absolutePath(), QStringLiteral("QA databases (*.sqlite)"));
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    chooser->setAcceptMode(QFileDialog::AcceptSave);
    chooser->setDefaultSuffix(QStringLiteral("sqlite"));
    connect(chooser, &QFileDialog::fileSelected, this, [this](const QString &path) {
        // A new one is an empty one: a file that is there is not written over.
        if (QFileInfo::exists(path))
        {
            say(QStringLiteral("New Database"), QStringLiteral("That file is there already."),
                QStringLiteral("Choose another name for the new database, or open this one with Open Database..."));
            return;
        }
        openPath(path);
    });
    chooser->open();
}

void MainWindow::openDatabase()
{
    auto *chooser = new QFileDialog(this, QStringLiteral("Open Database"), QFileInfo(m_database->path()).absolutePath(),
                                    QStringLiteral("QA databases (*.sqlite);;All files (*)"));
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    chooser->setFileMode(QFileDialog::ExistingFile);
    connect(chooser, &QFileDialog::fileSelected, this, &MainWindow::openPath);
    chooser->open();
}

bool MainWindow::importFile(QaDatabase &database, const QString &path, QString &message)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        message = QStringLiteral("%1 could not be read: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!document.isObject())
    {
        message = QStringLiteral("%1 is not a file of test scripts: %2").arg(QDir::toNativeSeparators(path),
                                                                               parse.error == QJsonParseError::NoError ? QStringLiteral("it is no JSON object")
                                                                                                                       : parse.errorString());
        return false;
    }
    QaImportCounts counts;
    QString error;
    if (!database.importJson(document.object(), counts, error))
    {
        message = error;
        return false;
    }
    message = counts.text();
    return true;
}

void MainWindow::importScripts()
{
    auto *chooser = new QFileDialog(this, QStringLiteral("Import Test Scripts"), QString(), QStringLiteral("Test scripts (*.json);;All files (*)"));
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    chooser->setFileMode(QFileDialog::ExistingFile);
    connect(chooser, &QFileDialog::fileSelected, this, [this](const QString &path) {
        QString message;
        const bool ok = importFile(*m_database, path, message);
        reload();
        say(QStringLiteral("Import Test Scripts"), ok ? QStringLiteral("The test scripts were imported.") : QStringLiteral("Nothing was imported."), message);
    });
    chooser->open();
}

void MainWindow::exportProject()
{
    const qint64 projectId = currentId(ProjectItem);
    if (projectId == 0)
        return;
    const QString name = find(ProjectItem, projectId)->text(0);
    auto *chooser = new QFileDialog(this, QStringLiteral("Export Project"), QString(name).replace(QLatin1Char(' '), QString()) + QStringLiteral(".json"),
                                    QStringLiteral("Test scripts (*.json)"));
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    chooser->setAcceptMode(QFileDialog::AcceptSave);
    chooser->setDefaultSuffix(QStringLiteral("json"));
    connect(chooser, &QFileDialog::fileSelected, this, [this, projectId](const QString &path) {
        QJsonObject scripts;
        QString error;
        QFile file(path);
        if (!m_database->exportJson(projectId, scripts, error))
            say(QStringLiteral("Export Project"), QStringLiteral("The project was not exported."), error);
        else if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(scripts).toJson()) < 0)
            say(QStringLiteral("Export Project"), QStringLiteral("The project was not exported."), file.errorString());
        else
            statusBar()->showMessage(QStringLiteral("Exported to %1").arg(QDir::toNativeSeparators(path)), 6000);
    });
    chooser->open();
}

// ---- projects, suites, cases ------------------------------------------------------------------------

void MainWindow::newProject()
{
    askText(QStringLiteral("New Project"), QStringLiteral("Project:"), QString(), [this](const QString &name, QString &error) {
        QaProject project;
        project.name = name;
        if (!m_database->addProject(project, error))
            return false;
        fillTree(ProjectItem, project.id);
        return true;
    });
}

void MainWindow::newSuite()
{
    const qint64 projectId = currentId(ProjectItem);
    if (projectId == 0)
        return;
    askText(QStringLiteral("New Suite"), QStringLiteral("Suite:"), QString(), [this, projectId](const QString &name, QString &error) {
        QaSuite suite;
        suite.projectId = projectId;
        suite.name = name;
        if (!m_database->addSuite(suite, error))
            return false;
        fillTree(SuiteItem, suite.id);
        return true;
    });
}

void MainWindow::newCase()
{
    const qint64 suiteId = currentId(SuiteItem);
    if (suiteId == 0)
        return;
    if (m_case->isChanged() && m_case->caseId() != 0)
        m_case->save();
    m_tabs->setCurrentWidget(m_case);
    m_case->newCase(suiteId);
}

void MainWindow::renameSelected()
{
    const qint64 suiteId = currentId(SuiteItem);
    const qint64 projectId = currentId(ProjectItem);
    if (suiteId != 0)
    {
        QList<QaSuite> suites;
        QString error;
        m_database->suites(projectId, suites, error);
        for (const QaSuite &suite : std::as_const(suites))
        {
            if (suite.id != suiteId)
                continue;
            askText(QStringLiteral("Rename Suite"), QStringLiteral("Suite:"), suite.name, [this, suite](const QString &name, QString &why) {
                QaSuite renamed = suite;
                renamed.name = name;
                if (!m_database->updateSuite(renamed, why))
                    return false;
                fillTree(SuiteItem, suite.id);
                return true;
            });
        }
    }
    else if (projectId != 0)
    {
        QList<QaProject> projects;
        QString error;
        m_database->projects(projects, error);
        for (const QaProject &project : std::as_const(projects))
        {
            if (project.id != projectId)
                continue;
            askText(QStringLiteral("Rename Project"), QStringLiteral("Project:"), project.name, [this, project](const QString &name, QString &why) {
                QaProject renamed = project;
                renamed.name = name;
                if (!m_database->updateProject(renamed, why))
                    return false;
                fillTree(ProjectItem, project.id);
                m_runs->setProject(project.id, renamed.name.trimmed());
                return true;
            });
        }
    }
}

// The selected case, suite or project - after a question that says what goes with it.
void MainWindow::deleteSelected()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;
    const Kind kind = Kind(item->data(0, kKindRole).toInt());
    const qint64 id = item->data(0, kIdRole).toLongLong();
    const QString what = kind == CaseItem ? QStringLiteral("test case") : kind == SuiteItem ? QStringLiteral("suite") : QStringLiteral("project");

    auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Delete"), QStringLiteral("Delete the %1 \"%2\"?").arg(what, item->text(0).trimmed()),
                                QMessageBox::Yes | QMessageBox::Cancel, this);
    box->setObjectName(QStringLiteral("deleteBox"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText((kind == CaseItem ? QStringLiteral("Its steps and its results in every test run go with it.")
                             : kind == SuiteItem ? QStringLiteral("Its %1 test cases go with it, with their steps and their results in every test run.").arg(item->childCount())
                                                 : QStringLiteral("Its suites, test cases and test runs go with it: everything that was written and recorded for the project."))
                            + QStringLiteral("\n\nThis cannot be undone."));
    box->button(QMessageBox::Yes)->setText(QStringLiteral("Delete"));
    box->setDefaultButton(QMessageBox::Cancel);
    connect(box, &QMessageBox::finished, this, [this, kind, id](int answer) {
        if (answer != QMessageBox::Yes)
            return;
        QString error;
        const bool done = kind == CaseItem ? m_database->deleteCase(id, error) : kind == SuiteItem ? m_database->deleteSuite(id, error) : m_database->deleteProject(id, error);
        if (!done)
            say(QStringLiteral("Delete"), QStringLiteral("It could not be deleted."), error);
        // Nothing of what is gone is shown or saved afterwards.
        m_case->showCase(0);
        m_tree->clear();
        reload();
    });
    box->open();
}
