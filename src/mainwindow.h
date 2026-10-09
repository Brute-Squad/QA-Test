#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qadatabase.h"

#include <QMainWindow>

#include <functional>

class CasePanel;
class QAction;
class QLabel;
class QTabWidget;
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

    // For tests: the pieces, and choosing what the tree selects.
    QTreeWidget *tree() const { return m_tree; }
    CasePanel *casePanel() const { return m_case; }
    RunPanel *runPanel() const { return m_runs; }
    bool selectCase(const QString &key);

public slots:
    void reload();

protected:
    // Back in front: what somebody else did to a shared database meanwhile is shown.
    void changeEvent(QEvent *event) override;

private:
    enum Kind { ProjectItem = 1, SuiteItem, CaseItem };

    void fillTree(Kind selectKind = ProjectItem, qint64 selectId = 0);
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

    QAction *m_newSuite = nullptr;
    QAction *m_newCase = nullptr;
    QAction *m_rename = nullptr;
    QAction *m_delete = nullptr;
    QAction *m_export = nullptr;
};

#endif // MAINWINDOW_H
