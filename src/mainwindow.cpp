#include "mainwindow.h"

#include "casepanel.h"
#include "qaconfig.h"
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
    setCentralWidget(splitter);
    resize(1280, 800);
    splitter->setSizes({ 420, 860 });

    // ---- menus
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    connect(file->addAction(QStringLiteral("&New Database...")), &QAction::triggered, this, &MainWindow::newDatabase);
    connect(file->addAction(QStringLiteral("&Open Database...")), &QAction::triggered, this, &MainWindow::openDatabase);
    connect(file->addAction(QStringLiteral("&Where Is the Database?")), &QAction::triggered, this, &MainWindow::whereIsTheDatabase);
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
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_database->isOpen() && !m_case->isChanged())
        reload();
}

void MainWindow::whereIsTheDatabase()
{
    QString details = m_why.isEmpty() ? QString() : m_why + QStringLiteral("\n\n");
    details += QStringLiteral("To put the database on a shared drive, so that several people work in the same one: copy the file there while nobody has it "
                              "open, and say where it is in the configuration file - a line \"Path=...\" under [Database] - on every PC:\n\n%1\n\n"
                              "File > Edit Configuration File... opens it. It explains its settings, and is read when the program starts.")
                   .arg(QDir::toNativeSeparators(m_configFile));
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
    const Kind kind = m_tree->currentItem() ? Kind(m_tree->currentItem()->data(0, kKindRole).toInt()) : ProjectItem;
    fillTree(kind, m_tree->currentItem() ? m_tree->currentItem()->data(0, kIdRole).toLongLong() : 0);
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
        if (m_case->isChanged() && m_case->caseId() != 0 && m_case->caseId() != caseId)
            m_case->save();
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
