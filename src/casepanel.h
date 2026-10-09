#ifndef CASEPANEL_H
#define CASEPANEL_H

#include "qadatabase.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

// One test case, to read and to write: its key, title, priority, where it
// is run, what has to be there first, its steps - what to do and what to
// expect, in order - and notes; under that, how it went in the runs so far.
// Save stores it (enabled once something was changed and it has a key and a
// title), Revert puts back what is stored. The panel talks to the database
// itself and says saved() when the tree is to follow.
class CasePanel : public QWidget
{
    Q_OBJECT

public:
    explicit CasePanel(QaDatabase *database, QWidget *parent = nullptr);

    // Show that case (0 = none: the panel is empty and cannot be typed in).
    void showCase(qint64 caseId);
    // A case that is not stored yet, in that suite, with the next free key.
    void newCase(qint64 suiteId);

    qint64 caseId() const { return m_case.id; }
    bool isChanged() const { return m_changed; }
    // Store what is typed; false - with the reason shown - if it cannot be.
    bool save();

signals:
    void saved(qint64 caseId);
    void changedChanged(bool changed);

private:
    void fill();
    void setChanged(bool changed);
    void collect(QaCase &testCase) const;
    void addStep(const QaStep &step, int row = -1);
    void moveStep(int by);
    void showHistory();
    void showProblem(const QString &text);

    QaDatabase     *m_database = nullptr;
    QaCase          m_case;
    bool            m_changed = false;
    bool            m_filling = false;

    QLabel         *m_heading = nullptr;
    QLineEdit      *m_key = nullptr;
    QLineEdit      *m_title = nullptr;
    QComboBox      *m_priority = nullptr;
    QComboBox      *m_area = nullptr;
    QPlainTextEdit *m_preconditions = nullptr;
    QTableWidget   *m_steps = nullptr;
    QPlainTextEdit *m_notes = nullptr;
    QTableWidget   *m_history = nullptr;
    QLabel         *m_problem = nullptr;
    QPushButton    *m_save = nullptr;
    QPushButton    *m_revert = nullptr;
    QPushButton    *m_addStep = nullptr;
    QPushButton    *m_removeStep = nullptr;
    QPushButton    *m_up = nullptr;
    QPushButton    *m_down = nullptr;
};

#endif // CASEPANEL_H
