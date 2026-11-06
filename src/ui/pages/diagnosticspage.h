#ifndef DIAGNOSTICSPAGE_H
#define DIAGNOSTICSPAGE_H

#include <QWidget>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

/**
 * @brief Options page for the log file and how much goes into it: the file switch, where it is
 *        (open the folder, copy the path), the level, per-subsystem debug and the bench diagnostics
 *        that switch themselves off at the next launch. Edits RadioSettings only; main.cpp applies
 *        them through Logging::apply() on RadioSettings::loggingChanged, so changes are live.
 */
class DiagnosticsPage : public QWidget {
    Q_OBJECT

public:
    explicit DiagnosticsPage(QWidget *parent = nullptr);

    void refresh();

private:
    void saveGroups();
    void saveBench();

    QCheckBox *m_fileCheck = nullptr;
    QLabel *m_fileStatus = nullptr;
    QPushButton *m_openFolderButton = nullptr;
    QPushButton *m_copyPathButton = nullptr;
    QComboBox *m_levelCombo = nullptr;
    std::vector<QCheckBox *> m_groupChecks;
    std::vector<QCheckBox *> m_benchChecks;
    QLabel *m_overrideNote = nullptr;
};

#endif // DIAGNOSTICSPAGE_H
