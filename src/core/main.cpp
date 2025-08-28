#include "ui/mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QEvent>
#include <csignal>
#include <exception>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

namespace {
QString logFilePath() {
    // Keep same file as other logs
    return QCoreApplication::applicationDirPath() + "/tab_debug.txt";
}

void appendLog(const QString &tag, const QString &msg) {
    QFile f(logFilePath());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
           << " [" << tag << "] " << msg << '\n';
        ts.flush();
    }
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
    const char *typeStr = "INFO";
    switch (type) {
        case QtDebugMsg: typeStr = "DEBUG"; break;
        case QtInfoMsg: typeStr = "INFO"; break;
        case QtWarningMsg: typeStr = "WARN"; break;
        case QtCriticalMsg: typeStr = "CRIT"; break;
        case QtFatalMsg: typeStr = "FATAL"; break;
    }
    QString ctxStr;
    if (ctx.file && ctx.function) {
        ctxStr = QString(" (%1:%2 %3)").arg(ctx.file).arg(ctx.line).arg(ctx.function);
    }
    appendLog("qt", QString("%1: %2%3").arg(typeStr).arg(msg).arg(ctxStr));
    if (type == QtFatalMsg) {
        // Do not abort. Show a non-blocking critical message and try to keep app running.
        static std::atomic<bool> showing{false};
        if (!showing.exchange(true)) {
            const QString m = QString("A fatal Qt error was reported and was intercepted.\n\n%1\n\nThe application will try to continue running.").arg(msg);
            QMetaObject::invokeMethod(qApp, [m]() {
                QMessageBox::critical(nullptr, "Runtime Error", m);
            }, Qt::QueuedConnection);
            // Allow another fatal to show later
            QTimer::singleShot(2000, qApp, [](){ showing.store(false); });
        }
        // Swallow the fatal to avoid hard aborts.
        return;
    }
}

// Avoid forcing process termination; prefer logging and UI error where possible.
// Note: true OS faults (e.g., access violations) cannot be fully recovered.
void onTerminate() {
    appendLog("crash", "std::terminate called");
    // Try to notify the user, but don't hard-exit here. The runtime may still terminate.
    QMetaObject::invokeMethod(qApp, [](){
        QMessageBox::critical(nullptr, "Unexpected Termination", "An unrecoverable error occurred. The app will attempt to remain open. Please save your work.");
    }, Qt::QueuedConnection);
}

void onSignal(int sig) {
    appendLog("crash", QString("signal caught: %1").arg(sig));
    // Best effort notify; signals like SIGSEGV are not recoverable.
    QMetaObject::invokeMethod(qApp, [sig](){
        QMessageBox::critical(nullptr, "System Signal", QString("The app received signal %1. Operation may be unstable.").arg(sig));
    }, Qt::QueuedConnection);
}

#ifdef _WIN32
// Write a Windows minidump to the application directory for post-mortem debugging
void writeMinidump(EXCEPTION_POINTERS* ep) {
    // Construct a timestamped dump file path in the app dir
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    const QString dumpPath = QCoreApplication::applicationDirPath() + "/crash_" + ts + ".dmp";

    // Create the dump file (use wide-character API)
    std::wstring wpath = dumpPath.toStdWString();
    HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        appendLog("crash", QString("failed to create dump file: %1 (err=%2)").arg(dumpPath).arg(GetLastError()));
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    // Keep dump small but useful; adjust to MiniDumpWithFullMemory if needed
    const MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo);
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType, ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(hFile);

    if (ok) {
        appendLog("crash", QString("minidump written: %1").arg(dumpPath));
    } else {
        appendLog("crash", QString("MiniDumpWriteDump failed (err=%1)").arg(GetLastError()));
    }
}

LONG WINAPI sehFilter(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    appendLog("crash", QString("unhandled SEH exception: 0x%1").arg(code, 0, 16));
    // Attempt to write a minidump for post-mortem analysis (avoid MSVC-specific __try/__except for MinGW)
    writeMinidump(ep);
    // Try to continue execution if possible; if not, OS will still terminate.
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
}

// Subclass QApplication to catch exceptions during Qt event dispatch and keep app alive
class SafeApplication : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject *receiver, QEvent *event) override {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception &e) {
            appendLog("crash", QString("exception in Qt notify: %1").arg(e.what()));
            QMessageBox::critical(nullptr, "Runtime Error", QString("An error occurred and was handled: %1").arg(e.what()));
            return false;
        } catch (...) {
            appendLog("crash", "unknown exception in Qt notify");
            QMessageBox::critical(nullptr, "Runtime Error", "An unknown error occurred and was handled.");
            return false;
        }
    }
};

int main(int argc, char *argv[])
{
    SafeApplication app(argc, argv);

    // Install global diagnostics
    qInstallMessageHandler(qtMessageHandler);
    // Avoid forcing exits; just log. Unhandled exceptions/signals are not fully recoverable but we try to keep UI alive.
    // std::set_terminate(onTerminate); // disabled: can cause immediate hard-exit
    std::signal(SIGABRT, onSignal);
#ifdef SIGSEGV
    std::signal(SIGSEGV, onSignal);
#endif
#ifdef _WIN32
    SetUnhandledExceptionFilter(sehFilter);
#endif
    appendLog("app", "startup");
    
    // Set application properties
    app.setApplicationName("Way2Repair Login System");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Way2Repair Systems");
    app.setOrganizationDomain("way2repair.com");

    // Set a global application icon so taskbar/early windows pick it up
    {
        QIcon appIcon;
        const QString svgPath = ":/icons/images/icons/Way2Repair_Logo.svg";
        if (QFile(svgPath).exists()) {
            const QList<QSize> sizes = { {16,16}, {20,20}, {24,24}, {32,32}, {40,40}, {48,48}, {64,64}, {96,96}, {128,128}, {256,256} };
            for (const auto &sz : sizes) appIcon.addFile(svgPath, sz);
        }
        if (!appIcon.isNull()) QApplication::setWindowIcon(appIcon);
    }
    
    // Set application style
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Create and show main window under a basic safety net
    int rc = 0;
    try {
        MainWindow window;
        window.show();
        appendLog("login", "login window shown");
        rc = app.exec();
    } catch (const std::exception &e) {
        appendLog("crash", QString("uncaught exception: %1").arg(e.what()));
        QMessageBox::critical(nullptr, "Unexpected Error", QString("An unexpected error occurred: %1").arg(e.what()));
        rc = 1;
    } catch (...) {
        appendLog("crash", "uncaught non-standard exception");
        QMessageBox::critical(nullptr, "Unexpected Error", "An unknown error occurred.");
        rc = 2;
    }
    appendLog("app", QString("shutdown rc=%1").arg(rc));
    return rc;
}
