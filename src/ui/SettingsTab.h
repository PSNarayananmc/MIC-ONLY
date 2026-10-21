#pragma once
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace vc {

class MainWindow;

class SettingsTab : public QWidget {
    Q_OBJECT
public:
    explicit SettingsTab(MainWindow* win, QWidget* parent = nullptr);

    void refreshDeviceList();   // re-populate the input combo

private slots:
    void applySettings();

private:
    MainWindow* m_win;
    QComboBox* m_device = nullptr;
    QComboBox* m_rate = nullptr;
    QComboBox* m_block = nullptr;
    QCheckBox* m_lowCpu = nullptr;
    QCheckBox* m_rtThread = nullptr;
    QComboBox* m_recFormat = nullptr;
    QCheckBox* m_recMono = nullptr;
    QLabel* m_recDir = nullptr;
    QLabel* m_paths = nullptr;
};

} // namespace vc
