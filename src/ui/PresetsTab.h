#pragma once
#include <QWidget>

class QListWidget;
class QLabel;
class QPushButton;

namespace vc {

class MainWindow;

class PresetsTab : public QWidget {
    Q_OBJECT
public:
    explicit PresetsTab(MainWindow* win, QWidget* parent = nullptr);

    void refreshList();
    void selectPreset(const QString& name);

private:
    MainWindow* m_win;
    QListWidget* m_list = nullptr;
    QLabel* m_desc = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_del = nullptr;
};

} // namespace vc
