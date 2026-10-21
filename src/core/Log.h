#pragma once
#include <QString>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDebug>
#include <QDir>

namespace vc {

// Thread-safe file logger. Never called from the realtime audio thread;
// UI / control paths only. Written to ~/.local/share/VoiceCraftStudio/logs/.
class Log {
public:
    static Log& instance() {
        static Log l;
        return l;
    }

    void open(const QString& dir) {
        QMutexLocker lock(&m_mutex);
        QDir().mkpath(dir);
        const QString name = dir + QStringLiteral("/voicecraft-%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        m_file.setFileName(name);
        if (m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            m_stream.setDevice(&m_file);
        m_stream << "VoiceCraft Studio session started "
                 << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        m_stream.flush();
    }

    template <typename... Args>
    void write(const char* level, const QString& fmt, Args&&... args) {
        QString msg = fmt;
        ((msg = msg.arg(QString::fromStdString(std::forward<Args>(args)))), ...);
        line(level, msg);
    }

    void line(const char* level, const QString& msg) {
        QMutexLocker lock(&m_mutex);
        const QString stamped = QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                                + ' ' + level + ' ' + msg;
        fputs(stamped.toUtf8().constData(), stderr);
        fputc('\n', stderr);
        if (m_stream.device()) {
            m_stream << stamped << '\n';
            m_stream.flush();
        }
    }

private:
    Log() = default;
    QMutex m_mutex;
    QFile m_file;
    QTextStream m_stream;
};

} // namespace vc

#define VCLOG(lvl, ...) ::vc::Log::instance().line(lvl, QString(__VA_ARGS__))
#define VCINFO(...) ::vc::Log::instance().line("INFO ", QString(__VA_ARGS__))
#define VCWARN(...) ::vc::Log::instance().line("WARN ", QString(__VA_ARGS__))
#define VCERR(...)  ::vc::Log::instance().line("ERROR", QString(__VA_ARGS__))
