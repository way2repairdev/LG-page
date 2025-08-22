#include "ui/mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QStyleFactory>
#include <csignal>
#include <exception>
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
        // Ensure fatal messages are visible in the log
        ::abort();
    }
}

void onTerminate() {
    appendLog("crash", "std::terminate called");
    std::_Exit(1);
}

void onSignal(int sig) {
    appendLog("crash", QString("signal caught: %1").arg(sig));
    std::_Exit(1);
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
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Install global diagnostics
    qInstallMessageHandler(qtMessageHandler);
    std::set_terminate(onTerminate);
    std::signal(SIGABRT, onSignal);
    std::signal(SIGSEGV, onSignal);
#ifdef _WIN32
    SetUnhandledExceptionFilter(sehFilter);
#endif
    appendLog("app", "startup");
    
    // Set application properties
    app.setApplicationName("Way2Repair Login System");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Way2Repair Systems");
    app.setOrganizationDomain("way2repair.com");
    
    // Set application style
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Create and show main window
    MainWindow window;
    window.show();
    appendLog("login", "login window shown");
    
    const int rc = app.exec();
    appendLog("app", QString("shutdown rc=%1").arg(rc));
    return rc;
}
