#pragma once
#include <QString>

namespace vc {

// Luxe dark theme: near-black panels, warm amber accent (broadcast console
// feel), generous paddings, subtle borders. One QSS for the whole app.
class Theme {
public:
    static inline const char* BG        = "#0f1115";
    static inline const char* PANEL     = "#171a21";
    static inline const char* PANEL_HI  = "#1e222b";
    static inline const char* BORDER    = "#2a2f3a";
    static inline const char* TEXT      = "#e8eaf0";
    static inline const char* MUTED     = "#8b92a3";
    static inline const char* ACCENT    = "#e8a13c";   // amber
    static inline const char* ACCENT_2  = "#39c48f";   // green (active/ok)
    static inline const char* DANGER    = "#e05555";
    static inline const char* RED       = "#e05555";

    static QString qss();
    static void applyAppFonts();       // QApplication font setup
};

} // namespace vc
