#include "client/CrashLog.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include "core/Version.h"

#if defined(__linux__)
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kot::client {

#if defined(__linux__)
namespace {

// Prepared before any crash: a signal handler may not allocate.
char g_logPath[1024] = {0};
char g_header[256] = {0};

void writeAll(int fd, const char* text) {
    if (fd >= 0) (void)!write(fd, text, std::strlen(text));
}

void onCrash(int sig) {
    void* frames[64];
    const int n = backtrace(frames, 64);
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGFPE ? "SIGFPE"
                     : sig == SIGILL ? "SIGILL" : "SIGBUS";
    for (const int fd : {STDERR_FILENO, g_logPath[0] ? open(g_logPath, O_WRONLY | O_CREAT | O_APPEND, 0644) : -1}) {
        if (fd < 0) continue;
        writeAll(fd, "\n==== Killers of Time crashed: ");
        writeAll(fd, name);
        writeAll(fd, g_header);
        writeAll(fd, "\n");
        backtrace_symbols_fd(frames, n, fd);
        if (fd != STDERR_FILENO) close(fd);
    }
    if (g_logPath[0]) {
        writeAll(STDERR_FILENO, "The backtrace was also written to ");
        writeAll(STDERR_FILENO, g_logPath);
        writeAll(STDERR_FILENO, "\n");
    }
    std::signal(sig, SIG_DFL);  // crash on as usual
    raise(sig);
}

}  // namespace
#endif

void installCrashLog() {
#if defined(__linux__)
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/KillersOfTime");
    QDir().mkpath(dir);
    const QByteArray path = (dir + QStringLiteral("/crash.log")).toLocal8Bit();
    std::strncpy(g_logPath, path.constData(), sizeof g_logPath - 1);
    const QByteArray header = QStringLiteral(" (version %1, started %2)")
                                  .arg(QString::fromLatin1(core::versionText()), QDateTime::currentDateTime().toString(Qt::ISODate))
                                  .toLocal8Bit();
    std::strncpy(g_header, header.constData(), sizeof g_header - 1);
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) std::signal(sig, onCrash);
#endif
}

}  // namespace kot::client
