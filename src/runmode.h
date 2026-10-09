#ifndef RUNMODE_H
#define RUNMODE_H

#include "qadatabase.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

// Working through a run, one test case at a time: the case large - what has
// to be there first, its steps with what to expect - a place for notes, and
// the result on one key:
//
//   P passed   F failed   B blocked   S skipped   U not run
//   Left / Right   the case before / after
//   Ctrl+V         a picture from the clipboard goes with the result
//   Esc            in the notes: out of the notes; else: close
//
// A result is stored at once and the next case that is not run comes up; at
// the last one it says that everything has a result. The cases are the ones
// the run panel showed when Run Mode was opened - "my cases" that are "not
// run", say - in its order.
//
// Files go with a result: a screenshot pasted from the clipboard, or any
// file (a log). They are copied into the folder "attachments" beside the
// database, so on a shared drive everybody sees them.
class RunMode : public QDialog
{
    Q_OBJECT

public:
    RunMode(QaDatabase *database, const QaRun &run, const QString &tester, const QList<qint64> &caseIds, qint64 startCaseId, QWidget *parent = nullptr);

    // Where it is: the case shown (0 = none), its place (from 1) and how many there are.
    qint64 caseId() const;
    int position() const { return m_at + 1; }
    int count() const { return int(m_caseIds.size()); }

    // What a key or a button does.
    void mark(const QString &status);
    void go(int by);
    // A copy of that file goes with the case shown; false with the reason shown.
    bool attachFile(const QString &file, const QString &name = QString());
    // The picture on the clipboard, as a PNG.
    bool pasteScreenshot();
    // Takes away the file in that row of the list (asked first by the button).
    bool removeAttachment(int row);

signals:
    // A result or a file was stored: the run panel is to follow.
    void stored();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showCase();
    void showFiles();
    void say(const QString &text);

    QaDatabase     *m_database = nullptr;
    QaRun           m_run;
    QString         m_tester;
    QList<qint64>   m_caseIds;
    int             m_at = -1;
    QaResult        m_result;           // of the case shown
    QList<QaAttachment> m_files;

    QLabel         *m_progress = nullptr;
    QLabel         *m_title = nullptr;
    QLabel         *m_before = nullptr;
    QTableWidget   *m_steps = nullptr;
    QPlainTextEdit *m_notes = nullptr;
    QSpinBox       *m_failedStep = nullptr;
    QLineEdit      *m_defect = nullptr;         // the issue a failure was reported as
    QListWidget    *m_fileList = nullptr;
    QLabel         *m_problem = nullptr;
    QList<QPushButton *> m_statusButtons;
};

#endif // RUNMODE_H
