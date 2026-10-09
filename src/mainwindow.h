#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qadatabase.h"

#include <QDate>
#include <QMainWindow>

#include <functional>

class CasePanel;
class QAction;
class QComboBox;
class QLineEdit;
class QLabel;
class QTabWidget;
class QLabel;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class RunPanel;

// The window: on the left the projects with their suites and test cases -
// a case says how it went the last time it was run - and on the right two
// tabs: Test Case (the selected case, to read and to write) and Test Runs
// (the runs of the selected project, and running one).
//
// File: New / Open Database..., Import Test Scripts..., Export Project...
// Edit: New Project / Suite / Test Case, Rename..., Delete...
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QaDatabase *database, QWidget *parent = nullptr);

    // Read a file of test scripts into the database; the message says what
    // came of it. False with the reason when the file could not be read.
    static bool importFile(QaDatabase &database, const QString &path, QString &message);

    // For a database that has no project yet: read every file of test scripts in
    // that folder into it - the ones that came with the program. Says what came of
    // it ("" = nothing was done: the database has projects, or there are no files).
    static QString importBundled(QaDatabase &database, const QString &folder);

    // Write a configuration file to start from (qaconfig.h), making its folder.
    static bool writeSampleConfig(const QString &path, QString &problem);

    // Why this database is the one in use, as a sentence for File > Where Is the
    // Database? - "QATest.ini says so", "it was opened last", ... - and the
    // configuration file there is, or could be.
    void setDatabaseSource(const QString &why, const QString &configFile);

    // A copy of the database a day (qabackup.h): how many to keep (0 = none, which is
    // what a window is made with) and where ("" = beside the database). Makes today's.
    void setBackups(int keep, const QString &folder);

    // File > Use a Shared Database...: opens that database - one that is there - and
    // writes it into the configuration file, so that it is the one from now on.
    // `networkName`: a connected drive (M:) is written as what it is connected to
    // (\\server\share), which is the same on every PC. "" = done; else why not.
    QString useSharedDatabase(const QString &path, bool networkName);

    // The database cannot be reached just now (a shared drive that has gone): the
    // window says so in a line of its own, keeps what it shows, and tries again.
    bool isLost() const;
    void retryDatabase();

    // For tests: the pieces, and choosing what the tree selects.
    QTreeWidget *tree() const { return m_tree; }
    CasePanel *casePanel() const { return m_case; }
    RunPanel *runPanel() const { return m_runs; }
    bool selectCase(const QString &key);

    // ---- finding and organising: what the menus do, once their question is answered.
    // Each says why not ("" = done).
    // The selected cases - and, `withSuites`, the cases that are shown of the selected suites.
    QList<qint64> selectedCaseIds(bool withSuites = true) const;
    QString moveSelectedTo(qint64 suiteId);
    QString deleteSelectedNow();
    QString addSelectedToRun(qint64 runId, int *added = nullptr);
    QString cloneSelected();

public slots:
    void reload();

protected:
    // Back in front: what somebody else did to a shared database meanwhile is shown.
    void changeEvent(QEvent *event) override;

private:
    enum Kind { ProjectItem = 1, SuiteItem, CaseItem };

    void fillTree(Kind selectKind = ProjectItem, qint64 selectId = 0);
    bool checkDatabase();
    void dailyBackup();
    void chooseShared();
    void showBackups();
    void onSelected();
    void updateActions();
    void updateTitle();
    qint64 currentId(Kind kind) const;
    QTreeWidgetItem *find(Kind kind, qint64 id) const;

    void newDatabase();
    void openDatabase();
    void openPath(const QString &path);
    void importScripts();
    void exportProject();
    void whereIsTheDatabase();
    void editConfiguration();
    void newProject();
    void newSuite();
    void newCase();
    void renameSelected();
    void askMove();
    void askAddToRun();
    QList<qint64> selectedIds(Kind kind) const;
    void refill();
    void editComponents();
    void deleteSelected();
    void askText(const QString &title, const QString &label, const QString &text, std::function<bool(const QString &, QString &)> store);
    void say(const QString &title, const QString &text, const QString &details = QString());

    QaDatabase  *m_database = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTabWidget  *m_tabs = nullptr;
    CasePanel   *m_case = nullptr;
    RunPanel    *m_runs = nullptr;
    bool         m_filling = false;
    QString      m_why;             // why this database
    QString      m_configFile;

    QWidget     *m_lostBar = nullptr;       // shown while the database cannot be reached
    QLabel      *m_lostText = nullptr;
    QTimer      *m_retry = nullptr;
    int          m_backupKeep = 0;
    QString      m_backupFolder;
    QString      m_backupOf;                // the database and the day the daily copy was last seen to
    QDate        m_backupDay;
    QString      m_backupNote;              // what came of it, for Where Is the Database?

    QAction *m_newSuite = nullptr;
    QAction *m_newCase = nullptr;
    QAction *m_rename = nullptr;
    QAction *m_move = nullptr;
    QAction *m_addToRun = nullptr;
    QAction *m_clone = nullptr;

    // Above the tree: words to find, and what to show - by how a case went the last
    // time, its priority, a tag.
    QLineEdit *m_search = nullptr;
    QComboBox *m_resultFilter = nullptr;
    QComboBox *m_priorityFilter = nullptr;
    QComboBox *m_tagFilter = nullptr;
    QLabel    *m_found = nullptr;
    QAction *m_components = nullptr;
    QAction *m_delete = nullptr;
    QAction *m_export = nullptr;
};

#endif // MAINWINDOW_H
