#include "ui/MainWindow.h"
#include "core/Log.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QStyleFactory>
#include <unistd.h>   // geteuid (Qt headers do not provide it portably)
#include <memory>

int main(int argc, char** argv)
{
    // High-DPI before QApplication
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("VoiceCraft Studio"));
    QApplication::setApplicationVersion(QStringLiteral(VOICECRAFT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("VoiceCraft Studio"));
    vc::Theme::applyAppFonts();
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setStyleSheet(vc::Theme::qss());

    // logging to ~/.local/share/VoiceCraftStudio/logs
    const QString logDir = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation)
        + QStringLiteral("/VoiceCraftStudio/logs");
    vc::Log::instance().open(logDir);
    VCINFO(QStringLiteral("VoiceCraft Studio %1 starting").arg(VOICECRAFT_VERSION));

    // hard safety: refuse to run as root (audio daemons + root = broken perms)
    if (::geteuid() == 0) {
        VCERR(QStringLiteral("Refusing to run as root"));
        qCritical("VoiceCraft Studio must not run as root. Exiting.");
        return 3;
    }

    try {
        vc::MainWindow win;
        win.show();
        return app.exec();
    } catch (const std::exception& e) {
        VCERR(QStringLiteral("Fatal: %1").arg(e.what()));
        return 2;
    }
}
