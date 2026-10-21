#include "Theme.h"
#include <QApplication>
#include <QFont>

namespace vc {

QString Theme::qss()
{
    return QStringLiteral(R"(
QMainWindow, QDialog { background: %BG%; }
QWidget { color: %TEXT%; font-size: 13px; }
QTabWidget::pane {
    border: 1px solid %BORDER%; border-radius: 8px;
    background: %PANEL%; top: -1px;
}
QTabBar::tab {
    background: transparent; color: %MUTED%;
    padding: 10px 22px; margin-right: 4px;
    border: none; border-bottom: 2px solid transparent;
    font-weight: 600; font-size: 13px;
}
QTabBar::tab:selected { color: %ACCENT%; border-bottom: 2px solid %ACCENT%; }
QTabBar::tab:hover { color: %TEXT%; }
QGroupBox {
    border: 1px solid %BORDER%; border-radius: 10px;
    margin-top: 14px; padding: 14px 12px 12px 12px;
    background: %PANEL%; font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 14px; top: 2px;
    color: %MUTED%; font-size: 11px; letter-spacing: 2px;
    text-transform: uppercase;
}
QPushButton {
    background: %PANEL_HI%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 9px 18px; font-weight: 600;
}
QPushButton:hover { border-color: %ACCENT%; color: %ACCENT%; }
QPushButton:pressed { background: %BG%; }
QPushButton:disabled { color: %MUTED%; border-color: %BORDER%; background: %PANEL%; }
QPushButton#bigStart {
    font-size: 22px; font-weight: 800; padding: 22px 60px;
    border: 2px solid %ACCENT_2%; color: %ACCENT_2%; border-radius: 44px;
    background: #10251d;
}
QPushButton#bigStop {
    font-size: 22px; font-weight: 800; padding: 22px 60px;
    border: 2px solid %RED%; color: %RED%; border-radius: 44px;
    background: #2a1414;
}
QComboBox {
    background: %PANEL_HI%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 8px 12px; min-width: 220px;
}
QComboBox:hover { border-color: %ACCENT%; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox::down-arrow {
    image: none; border-left: 5px solid transparent;
    border-right: 5px solid transparent; border-top: 6px solid %MUTED%;
    margin-right: 10px;
}
QComboBox QAbstractItemView {
    background: %PANEL_HI%; border: 1px solid %BORDER%;
    selection-background-color: %ACCENT%; selection-color: #10131a;
    outline: none;
}
QSlider::groove:horizontal {
    height: 6px; border-radius: 3px;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %BORDER%, stop:1 %BORDER%);
}
QSlider::handle:horizontal {
    width: 18px; height: 18px; margin: -7px 0;
    border-radius: 9px; background: %ACCENT%;
}
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 3px; }
QSlider:disabled::sub-page:horizontal { background: %BORDER%; }
QSlider:disabled::handle:horizontal { background: %MUTED%; }
QLabel#h1 { font-size: 26px; font-weight: 800; }
QLabel#h2 { font-size: 15px; font-weight: 700; }
QLabel#muted { color: %MUTED%; }
QLabel#mono { font-family: 'DejaVu Sans Mono', monospace; }
QTextEdit, QPlainTextEdit, QLineEdit {
    background: %PANEL_HI%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 8px;
}
QListWidget {
    background: %PANEL_HI%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 4px;
}
QListWidget::item { padding: 10px; border-radius: 6px; }
QListWidget::item:selected { background: %ACCENT%; color: #10131a; }
QListWidget::item:hover { background: %PANEL_HI%; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 18px; height: 18px; border-radius: 5px;
    border: 1px solid %BORDER%; background: %PANEL_HI%;
}
QCheckBox::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; }
QSpinBox, QDoubleSpinBox {
    background: %PANEL_HI%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 6px 10px;
}
QProgressBar {
    background: %PANEL_HI%; border: 1px solid %BORDER%;
    border-radius: 6px; text-align: center; height: 14px;
}
QProgressBar::chunk { background: %ACCENT%; border-radius: 5px; }
QStatusBar { background: %PANEL%; border-top: 1px solid %BORDER%; color: %MUTED%; }
QToolTip {
    background: %PANEL_HI%; color: %TEXT%;
    border: 1px solid %BORDER%; padding: 6px; border-radius: 6px;
}
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %BORDER%; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: %MUTED%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
)")
    .replace("%BG%", Theme::BG)
    .replace("%PANEL%", Theme::PANEL)
    .replace("%PANEL_HI%", Theme::PANEL_HI)
    .replace("%BORDER%", Theme::BORDER)
    .replace("%TEXT%", Theme::TEXT)
    .replace("%MUTED%", Theme::MUTED)
    .replace("%ACCENT%", Theme::ACCENT)
    .replace("%ACCENT_2%", Theme::ACCENT_2)
    .replace("%RED%", Theme::RED);
}

void Theme::applyAppFonts()
{
    QFont f = QApplication::font();
    f.setFamily(QStringLiteral("DejaVu Sans"));
    f.setPointSize(10);
    QApplication::setFont(f);
}

} // namespace vc
