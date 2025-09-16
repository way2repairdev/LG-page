
#include "ui/mainapplication.h"
#include "ui/dualtabwidget.h"
#include "viewers/pdf/pdfviewerwidget.h"
#include "viewers/pcb/PCBViewerWidget.h"
#include "core/memoryfilemanager.h"
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QHeaderView>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QTabWidget>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QShortcut>
#include <QThread>
#include <QToolTip>
#include <QCursor>
#include <QMutex>
#include <QMutexLocker>
#include "toastnotifier.h"
#include <QPainter>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QScrollBar>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QStyle>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// Forward declaration for local logging helper defined later in this file
namespace { void writeTransitionLog(const QString &msg); }

void MainApplication::toggleMaximizeRestore()
{
    // If OS believes we're maximized, treat it as our custom maximized
    if (isMaximized() && !m_customMaximized) {
        m_customMaximized = true;
    }
    writeTransitionLog(QString("toggleMaximizeRestore: isMaximized=%1 custom=%2 geom=%3x%4@(%5,%6)")
                       .arg(isMaximized())
                       .arg(m_customMaximized)
                       .arg(width()).arg(height()).arg(x()).arg(y()));
    if (m_customMaximized) doRestore(); else doMaximize();
}

void MainApplication::doMaximize()
{
    if (m_customMaximized) return;
    // Save a sane normal geometry if not valid or accidentally large
    QRect g = geometry();
    if (!m_savedNormalGeometry.isValid() || g.width() < 200 || g.height() < 150) {
        m_savedNormalGeometry = g;
    }
    QScreen *scr = QGuiApplication::screenAt(frameGeometry().center());
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (scr) {
        const QRect avail = scr->availableGeometry();
        writeTransitionLog(QString("doMaximize: to %1x%2@(%3,%4) on screen '%5'")
                           .arg(avail.width()).arg(avail.height()).arg(avail.x()).arg(avail.y())
                           .arg(scr->name()));
        setGeometry(avail);
    } else {
        writeTransitionLog("doMaximize: no screen found, skipping");
    }
    m_customMaximized = true;
    if (m_titleBar) { m_titleBar->updateMaximizeIcon(); m_titleBar->update(); }
}

void MainApplication::doRestore()
{
    if (!m_customMaximized) return;
    if (m_savedNormalGeometry.isValid()) {
        writeTransitionLog(QString("doRestore: to %1x%2@(%3,%4)")
                           .arg(m_savedNormalGeometry.width()).arg(m_savedNormalGeometry.height())
                           .arg(m_savedNormalGeometry.x()).arg(m_savedNormalGeometry.y()));
        setGeometry(m_savedNormalGeometry);
    } else {
        writeTransitionLog("doRestore: no saved geometry");
    }
    m_customMaximized = false;
    if (m_titleBar) { m_titleBar->updateMaximizeIcon(); m_titleBar->update(); }
}

void MainApplication::maximizeWindow()
{
    // Ensure we have a valid restore geometry captured before maximizing
    if (!m_savedNormalGeometry.isValid()) {
        // Use current geometry or a centered default
        QRect g = geometry();
        if (!g.isValid() || g.width() < 200 || g.height() < 150) {
            if (QScreen *s = QGuiApplication::primaryScreen()) {
                QRect avail = s->availableGeometry();
                QSize sz(1400, 900);
                sz.setWidth(std::min(sz.width(), avail.width()));
                sz.setHeight(std::min(sz.height(), avail.height()));
                QRect centered(QPoint(0,0), sz);
                centered.moveCenter(avail.center());
                g = centered;
                setGeometry(g);
            }
        }
        m_savedNormalGeometry = g;
    }
    writeTransitionLog(QString("maximizeWindow: captured normal %1x%2@(%3,%4)")
                       .arg(m_savedNormalGeometry.width()).arg(m_savedNormalGeometry.height())
                       .arg(m_savedNormalGeometry.x()).arg(m_savedNormalGeometry.y()));
    doMaximize();
}

namespace {
inline void writeTransitionLog(const QString &msg) {
    const QString logPath = QCoreApplication::applicationDirPath() + "/tab_debug.txt";
    QFile f(logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
           << " [main] " << msg << '\n';
    }
}

// Lightweight helper to surface issues without stopping the app
inline void showTreeIssue(MainApplication* self, const QString &context, const QString &detail = QString()) {
    QString msg = context;
    if (!detail.trimmed().isEmpty()) msg += ": " + detail.trimmed();
    // Floating toast + status bar + log
    ToastNotifier::show(self, msg);
    if (self && self->statusBar()) self->statusBar()->showMessage(msg, 5000);
    writeTransitionLog(QString("tree-issue: ") + msg);
}

// Cheap availability check for directories (including UNC shares)
inline bool isDirAvailable(const QString &path) {
    if (path.trimmed().isEmpty()) return false;
    QDir d(path);
    return d.exists(); // Avoid heavy listing to keep it fast and non-blocking
}

// Asynchronous, time-bounded directory availability check to prevent UI stalls on slow/unreachable shares
static void checkDirAvailableAsync(QObject *ctx, const QString &path, int timeoutMs, std::function<void(bool)> callback) {
    // If no path, return quickly
    if (!ctx) { if (callback) callback(false); return; }
    if (path.trimmed().isEmpty()) { if (callback) callback(false); return; }

    // Thread worker using lambda, no signals/slots to avoid automoc requirements
    auto *thread = new QThread(ctx);

    // Completion coordination flag
    auto done = std::make_shared<bool>(false);

    QObject::connect(thread, &QThread::started, thread, [ctx, path, callback, done, thread]() {
        bool ok = false;
        try {
            QDir d(path);
            ok = d.exists();
        } catch (...) {
            ok = false;
        }
        // Post result back to ctx's thread; ignore if timed out already
        QMetaObject::invokeMethod(ctx, [callback, done, ok, thread]() {
            if (*done) return;
            *done = true;
            if (callback) callback(ok);
            thread->quit();
        }, Qt::QueuedConnection);
    });

    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // Timeout guard – resolve false if worker is slow/unreachable
    QTimer::singleShot(timeoutMs, ctx, [done, callback, thread]() mutable {
        if (*done) return;
        *done = true;
        if (callback) callback(false);
        thread->requestInterruption();
        thread->quit();
    });

    thread->start();
}
}

MainApplication::MainApplication(const UserSession &userSession, QWidget *parent)
    : QMainWindow(parent)
    , m_userSession(userSession)
    , m_dbManager(new DatabaseManager(this))
    , m_rootFolderPath("C:\\W2R_Schematics")  // Local default folder
    , m_serverRootPath("\\\\192.168.1.2\\share\\W2R_Schematics") // Default server UNC path
    , m_awsRootPath("") // AWS optional root (e.g., local mount or sync folder)
    // Server-side initialization (commented out for local file loading)
    //, m_networkManager(new QNetworkAccessManager(this))
    //, m_baseUrl("http://localhost/api") // WAMP server API endpoint
{
    writeTransitionLog("ctor: begin");
    writeTransitionLog("ctor: after member initialization");
    // Optional: make the window frameless so we can control the top area height
    // Disabled due to setWindowFlag crash - TODO: investigate further
    /*
#ifdef Q_OS_WIN
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, false);
#endif
    */
    writeTransitionLog("ctor: after window flags");
    setupUI();
    writeTransitionLog("ctor: after setupUI");
    
    // Force light theme application at startup
    applyAppPalette(false); // Always apply light theme
    writeTransitionLog("ctor: after forced light theme");
    
    setupMenuBar();
    setupStatusBar();
    updateUserInfo();
    
    // Setup keyboard shortcuts
    setupKeyboardShortcuts();
    
    // Set window properties
    setWindowTitle("Way2Repair - Equipment Maintenance System");
    setMinimumSize(1200, 800);
    resize(1400, 900);
    
    // Center the window (defensive: primaryScreen can be null on some systems briefly)
    if (QScreen *screen = QApplication::primaryScreen()) {
        const QRect screenGeometry = screen->availableGeometry();
        const int x = (screenGeometry.width() - width()) / 2;
        const int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }

    // Capture initial normal geometry for restore before any maximize
    m_savedNormalGeometry = geometry();
    
    // Set application/window icon from SVG with multiple declared sizes for crisp scaling
    {
        const QString svgPath = ":/icons/images/icons/Way2Repair_Logo.svg";
        QIcon appIcon;
        if (QFile(svgPath).exists()) {
            const QList<QSize> sizes = { {16,16}, {20,20}, {24,24}, {32,32}, {40,40}, {48,48}, {64,64}, {96,96}, {128,128}, {256,256} };
            for (const auto &sz : sizes) {
                appIcon.addFile(svgPath, sz);
            }
        }
        if (!appIcon.isNull()) {
            setWindowIcon(appIcon);
            QApplication::setWindowIcon(appIcon);
        }
    }
    
    // AWS-only mode: do not load any tree until AWS credentials arrive from login
    // (prevents flashing not-configured states at startup).
    
    // Clean up any legacy AWS credentials from previous versions
    autoLoadAwsCredentials();
    // AWS-only mode: defer loading the AWS tree until credentials are provided by the login flow.
    writeTransitionLog("ctor: AWS tree load deferred until credentials are ready");
    
    // Add welcome tab
    addWelcomeTab();
    writeTransitionLog("ctor: after addWelcomeTab");
    writeTransitionLog("ctor: end");
}

// Smooth entrance animation when shown after login
void MainApplication::animateEnter()
{
    // Stop any existing enter animation
    if (m_enterAnim) { m_enterAnim->stop(); m_enterAnim->deleteLater(); m_enterAnim = nullptr; }

    // Prepare a subtle fade + scale from 98.5% to 100%
    setWindowOpacity(0.0);
    QRect endG = geometry();
    QRect startG = endG;
    startG.setWidth(int(endG.width() * 0.985));
    startG.setHeight(int(endG.height() * 0.985));
    startG.moveCenter(endG.center());
    setGeometry(startG);

    auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(200);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);

    auto *scale = new QPropertyAnimation(this, "geometry", this);
    scale->setDuration(200);
    scale->setStartValue(startG);
    scale->setEndValue(endG);
    scale->setEasingCurve(QEasingCurve::OutCubic);

    m_enterAnim = new QParallelAnimationGroup(this);
    m_enterAnim->addAnimation(fade);
    m_enterAnim->addAnimation(scale);
    connect(m_enterAnim, &QParallelAnimationGroup::finished, this, [this]{
        // Ensure final state
        setWindowOpacity(1.0);
        m_enterAnim->deleteLater();
        m_enterAnim = nullptr;
        writeTransitionLog("animateEnter: finished");
    });
    writeTransitionLog("animateEnter: start");
    m_enterAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

MainApplication::~MainApplication()
{
    // Clean up
}

#ifdef Q_OS_WIN
bool MainApplication::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg) return false;

    switch (msg->message) {
    case WM_GETMINMAXINFO: {
        // Ensure correct maximize bounds for frameless window (snap, Win+Up, etc.)
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(msg->lParam);
        // lParam here is a MINMAXINFO*, not coordinates. Use our window's screen.
        QScreen *scr = QGuiApplication::screenAt(frameGeometry().center());
        if (!scr) scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr) scr = QGuiApplication::primaryScreen();
        if (scr) {
            const QRect avail = scr->availableGeometry();
            mmi->ptMaxPosition.x = avail.left();
            mmi->ptMaxPosition.y = avail.top();
            mmi->ptMaxSize.x = avail.width();
            mmi->ptMaxSize.y = avail.height();
            // Respect our minimum size
            const QSize minSz = minimumSize();
            if (minSz.isValid()) {
                mmi->ptMinTrackSize.x = std::max<LONG>(minSz.width(), 200);
                mmi->ptMinTrackSize.y = std::max<LONG>(minSz.height(), 150);
            }
            writeTransitionLog(QString("WM_GETMINMAXINFO: avail %1x%2@(%3,%4) on '%5'")
                               .arg(avail.width()).arg(avail.height()).arg(avail.x()).arg(avail.y())
                               .arg(scr->name()));
        }
        *result = 0;
        return true;
    }
    case WM_NCHITTEST: {
        // Allow resizing from edges/corners and dragging in the title bar area (outside of buttons)
        const QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        const QPoint pos = mapFromGlobal(globalPos);

    // First: edge/corner resize hit testing (disabled when maximized)
    const bool reallyMax = isMaximized() || m_customMaximized;
    if (!reallyMax) {
            const int border = 8; // resize border thickness in device-independent pixels
            const int x = pos.x();
            const int y = pos.y();
            const int w = width();
            const int h = height();

            const bool left   = (x >= 0 && x < border);
            const bool right  = (x <= w && x > w - border);
            const bool top    = (y >= 0 && y < border);
            const bool bottom = (y <= h && y > h - border);

            if (top && left)   { *result = HTTOPLEFT;    return true; }
            if (top && right)  { *result = HTTOPRIGHT;   return true; }
            if (bottom && left){ *result = HTBOTTOMLEFT; return true; }
            if (bottom && right){*result = HTBOTTOMRIGHT;return true; }
            if (left)   { *result = HTLEFT;   return true; }
            if (right)  { *result = HTRIGHT;  return true; }
            if (top)    { *result = HTTOP;    return true; }
            if (bottom) { *result = HTBOTTOM; return true; }
        }

        // Then: treat the custom title bar area as caption for dragging
    if (m_titleBar) {
            QRect tbRect = m_titleBar->geometry();
            if (tbRect.contains(pos)) {
                // Avoid dragging when interacting with buttons
                QWidget *child = childAt(pos);
                if (child && qobject_cast<QToolButton*>(child)) break;
        if (!reallyMax) *result = HTCAPTION; else *result = HTNOWHERE;
                return true;
            }
        }
        break;
    }
    default:
        break;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainApplication::setupUI()
{
    // Create a root container that hosts a custom title bar + menubar + content
    QWidget *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0,0,0,0);
    rootLayout->setSpacing(0);

    // Custom title bar (tall, with big icon) + keep a standard menu bar below it
    setupTitleBar();
    if (m_titleBar) rootLayout->addWidget(m_titleBar);

    // Create a dedicated menu bar we control, do not use the QMainWindow native one
    m_customMenuBar = new QMenuBar(root);
    rootLayout->addWidget(m_customMenuBar);
    // Redirect setupMenuBar to use m_customMenuBar
    QMenuBar *saved = QMainWindow::menuBar(); // not used when frameless
    Q_UNUSED(saved);
    
    m_centralWidget = new QWidget(root);
    // Content margins
    auto *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    
    // Create splitter for tree view and tab widget
    m_splitter = new QSplitter(Qt::Horizontal, this);
    
    // Setup tree view and tab widget
    setupTreeView();
    setupTabWidget();
    
    // Add to splitter
    m_splitter->addWidget(m_treePanel);
    m_splitter->addWidget(m_tabWidget);
    
    // Set splitter proportions (tree view: 25%, tabs: 75%)
    m_splitter->setSizes({300, 900});
    m_splitter->setCollapsible(0, true); // Allow tree view to collapse completely
    m_splitter->setCollapsible(1, false); // Don't allow tab widget to collapse
    
    // Set splitter handle width for better visibility
    m_splitter->setHandleWidth(3);
    m_splitter->setStyleSheet(
        "QSplitter::handle {"
        "    background-color: #d4e1f5;"
        "    border: 1px solid #a0b0c0;"
        "}"
        "QSplitter::handle:hover {"
        "    background-color: #4285f4;"
        "}"
    );
    
    // Initialize tree view state
    m_treeViewVisible = true;
    m_splitterSizes = {300, 900};
    
    mainLayout->addWidget(m_splitter);
    rootLayout->addWidget(m_centralWidget, 1);
    setCentralWidget(root);

    // Create a global loading overlay over the main content area (matches old/global UX)
    if (!m_globalLoadingOverlay) {
        m_globalLoadingOverlay = new LoadingOverlay(m_centralWidget);
        m_globalLoadingOverlay->hideOverlay();
        connect(m_globalLoadingOverlay, &LoadingOverlay::cancelRequested, this, [this]() {
            // Currently used for AWS multi-download queue cancel
            m_cancelAwsQueue = true;
            statusBar()->showMessage("Cancelling pending operations…", 2000);
        });
    }
}

void MainApplication::setupTitleBar()
{
    if (m_titleBar) return;
    m_titleBar = new TitleBarWidget(this);
    // Use requested brand title
    m_titleBar->setTitle(QStringLiteral("Way2Solutions"));
    m_titleBar->setIcon(windowIcon());
    connect(m_titleBar, &TitleBarWidget::minimizeRequested, this, [this]{ animateMinimize(); });
    connect(m_titleBar, &TitleBarWidget::maximizeRestoreRequested, this, [this]{ toggleMaximizeRestore(); });
    connect(m_titleBar, &TitleBarWidget::closeRequested, this, [this]{ animateClose(); });
}
void MainApplication::closeEvent(QCloseEvent *event)
{
    if (m_closingNow) {
        event->accept();
        return;
    }
    event->ignore();
    animateClose();
}

void MainApplication::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Keep global overlay covering the content area when window resizes
    if (m_globalLoadingOverlay && m_centralWidget) {
        m_globalLoadingOverlay->resize(m_centralWidget->size());
        m_globalLoadingOverlay->move(m_centralWidget->pos());
        m_globalLoadingOverlay->raise();
    }
}

namespace {
// Lightweight top-most ghost that paints a snapshot while the real window is hidden
class MinimizeGhost final : public QWidget {
public:
    explicit MinimizeGhost(const QPixmap &px, QWidget *parent = nullptr)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint)
        , m_px(px)
    {
        Q_UNUSED(parent);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setWindowOpacity(1.0);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(rect(), m_px);
    }
private:
    QPixmap m_px;
};

#ifdef Q_OS_WIN
static QRect taskbarRectForPoint(const QPoint &pt)
{
    // Try to find the Windows taskbar; fallback to bottom strip of the screen
    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", NULL);
    if (hTaskbar) {
        RECT r; GetWindowRect(hTaskbar, &r);
        return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
    }
    QScreen *scr = QGuiApplication::screenAt(pt);
    if (!scr) scr = QGuiApplication::primaryScreen();
    QRect g = scr ? scr->availableGeometry() : QRect(0,0,1280,720);
    return QRect(g.left(), g.bottom() - 56, g.width(), 56);
}
#else
static QRect taskbarRectForPoint(const QPoint &pt)
{
    QScreen *scr = QGuiApplication::screenAt(pt);
    if (!scr) scr = QGuiApplication::primaryScreen();
    QRect g = scr ? scr->availableGeometry() : QRect(0,0,1280,720);
    return QRect(g.left(), g.bottom() - 56, g.width(), 56);
}
#endif
}

void MainApplication::animateMinimize()
{
    // Prevent duplicate animations
    if (isMinimized()) { showMinimized(); return; }
    if (m_minimizeAnim) { m_minimizeAnim->stop(); m_minimizeAnim->deleteLater(); m_minimizeAnim = nullptr; }

    // Take a snapshot of the current window and animate a ghost instead of the real window
    const QRect startFrame = frameGeometry();
    const QPixmap snapshot = this->grab();
    auto *ghost = new MinimizeGhost(snapshot, this);
    ghost->setGeometry(startFrame);
    ghost->show();

    // Hide real window immediately to avoid flicker, then animate ghost toward the taskbar
    this->hide();

    const QRect taskbar = taskbarRectForPoint(startFrame.center());
    const bool horizontal = taskbar.width() >= taskbar.height();

    // Target size reminiscent of macOS minimize (small tile near the bar)
    const int targetW = std::max(120, startFrame.width() / 8);
    const int targetH = std::max(80,  startFrame.height() / 8);
    QRect target(QPoint(0,0), QSize(targetW, targetH));

    QPoint targetCenter;
    if (horizontal) {
        const bool bottomBar = taskbar.center().y() >= startFrame.center().y();
        int y = bottomBar ? taskbar.bottom() - targetH/2 - 6 : taskbar.top() + targetH/2 + 6;
        int x = taskbar.left() + std::min(200, taskbar.width()/4); // left cluster approximation
        targetCenter = QPoint(x, y);
    } else { // vertical bar (left/right)
        const bool rightBar = taskbar.center().x() >= startFrame.center().x();
        int x = rightBar ? taskbar.right() - targetW/2 - 6 : taskbar.left() + targetW/2 + 6;
        int y = taskbar.top() + taskbar.height()/2;
        targetCenter = QPoint(x, y);
    }
    target.moveCenter(targetCenter);

    // Single smooth shrink-and-slide animation (no opacity on the real window)
    auto *geoAnim = new QPropertyAnimation(ghost, "geometry", ghost);
    geoAnim->setDuration(220);
    geoAnim->setStartValue(startFrame);
    geoAnim->setEndValue(target);
    geoAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(geoAnim, &QPropertyAnimation::finished, this, [this, ghost]() {
        ghost->deleteLater();
        // On Windows frameless, explicitly set minimized state then show
        setWindowState(windowState() | Qt::WindowMinimized);
        QTimer::singleShot(0, this, [this]{ showMinimized(); });
    writeTransitionLog("animateMinimize: finished -> showMinimized");
    });
    geoAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainApplication::animateClose()
{
    if (m_closeAnim) {
        m_closeAnim->stop();
        m_closeAnim->deleteLater();
    }
    // Fade + slight shrink
    auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(160);
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);

    QRect startG = geometry();
    QRect endG = startG;
    endG.setWidth(int(startG.width() * 0.98));
    endG.setHeight(int(startG.height() * 0.98));
    endG.moveCenter(startG.center());
    auto *scale = new QPropertyAnimation(this, "geometry", this);
    scale->setDuration(160);
    scale->setStartValue(startG);
    scale->setEndValue(endG);
    scale->setEasingCurve(QEasingCurve::OutCubic);

    m_closeAnim = new QParallelAnimationGroup(this);
    m_closeAnim->addAnimation(fade);
    m_closeAnim->addAnimation(scale);
    connect(m_closeAnim, &QParallelAnimationGroup::finished, this, [this]{
        m_closingNow = true;
        close();
    });
    m_closeAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainApplication::applyMenuBarMaterialStyle()
{
    QMenuBar *menuBar = m_customMenuBar ? m_customMenuBar : QMainWindow::menuBar();
    if (!menuBar) return;
    
    // Force light theme - ignore palette darkness detection
    const QString colBar      = "#ffffff";   // AppBar surface
    const QString colText     = "#1a1a1a";   // On-surface text
    const QString colOutline  = "#e6e8ee";   // Divider/border
    const QString colHover    = "rgba(30,136,229,28)"; // subtle overlay
    const QString colPressed  = "rgba(30,136,229,38)"; // pressed overlay
    const QString colAccent   = "#1E88E5";   // Primary

    QString mbStyle;
    mbStyle += "QMenuBar {"
               "  background: " + colBar + ";"
               "  color: " + colText + ";"
               "  border: none;"
               "  border-bottom: 1px solid " + colOutline + ";"
               "  font-family: 'Segoe UI', Arial, sans-serif;"
               // Increase height so the brand logo is clearly visible
               "  min-height: 14px;"
               "}";
    // MenuBar items with underline indicator and hover overlay
    mbStyle += "QMenuBar::item {"
               // Old compact padding
               "  padding: 6px 12px;"
               "  margin: 0;"
               "  color: " + colText + ";"
               "  border: none;"
               "  border-bottom: 2px solid transparent;"
               "}"
               "QMenuBar::item:selected {"
               "  background: transparent;"
               "  border-bottom: 2px solid " + colAccent + ";"
               "}"
               "QMenuBar::item:pressed {"
               "  background: " + colPressed + ";"
               "  border-bottom: 2px solid " + colAccent + ";"
               "}";
    // Popup menus (Material-ish)
    mbStyle += "QMenu {"
               "  background: #ffffff;"
               "  color: " + colText + ";"
               "  border: 1px solid " + colOutline + ";"
               "  border-radius: 8px;"
               "  padding: 6px;"
               "}"
               "QMenu::separator {"
               "  height: 1px;"
               "  background: " + colOutline + ";"
               "  margin: 6px 8px;"
               "}"
               "QMenu::item {"
               "  padding: 8px 14px;"
               "  border-radius: 6px;"
               "  margin: 2px;"
               "}"
               "QMenu::item:selected {"
               "  background: rgba(30,136,229,14);"
               "  color: " + colText + ";"
               "}";
    menuBar->setStyleSheet(mbStyle);

    // Style right-side buttons to match
    auto styleTool = [&](QToolButton *btn){
        if (!btn) return;
        btn->setStyleSheet(
            QString(
                "QToolButton {"
                "  padding: 4px 8px;"
                "  border: none;"
                "  border-radius: 6px;"
                "  color: %1;"
                "}"
                "QToolButton:hover {"
                "  background: %2;"
                "}"
                "QToolButton:pressed {"
                "  background: %3;"
                "}"
            ).arg(colText, colHover, colPressed)
        );
    };
    styleTool(m_homeButton);
    // m_themeToggle removed - static light mode only
}

void MainApplication::setupMenuBar()
{
    QMenuBar *menuBar = m_customMenuBar ? m_customMenuBar : QMainWindow::menuBar();
    applyMenuBarMaterialStyle();
    
    // Add left-side brand logo prominently (Way2Repair)
    if (!m_brandContainer) {
        m_brandContainer = new QWidget(menuBar);
        auto *bl = new QHBoxLayout(m_brandContainer);
        bl->setContentsMargins(6, 0, 8, 0);
        bl->setSpacing(6);
        m_brandLabel = new QLabel(m_brandContainer);
        m_brandLabel->setObjectName("brandLabel");
        m_brandLabel->setMinimumSize(28, 28); // visible size
        m_brandLabel->setMaximumSize(36, 36);
        m_brandLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        // Load SVG into pixmap at target size for crispness (Qt auto-renders SVG)
        const QString svgPath = ":/icons/images/icons/Way2Repair_Logo.svg";
        QIcon brand(svgPath);
        const int brandSize = 28;
        m_brandLabel->setPixmap(brand.pixmap(brandSize, brandSize));
        m_brandLabel->setToolTip("Way2Repair");
        bl->addWidget(m_brandLabel, 0, Qt::AlignVCenter);
        m_brandContainer->setLayout(bl);
        // Insert as a leading widget in the menu bar via QWidgetAction
        auto *brandAction = new QWidgetAction(menuBar);
        brandAction->setDefaultWidget(m_brandContainer);
        menuBar->addAction(brandAction);
    }
    // File menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    QAction *logoutAction = fileMenu->addAction("&Logout");
    logoutAction->setShortcut(QKeySequence("Ctrl+L"));
    connect(logoutAction, &QAction::triggered, this, &MainApplication::onLogoutClicked);
    
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    
    // View menu
    QMenu *viewMenu = menuBar->addMenu("&View");
    
    QAction *toggleTreeAction = viewMenu->addAction("&Toggle Tree View");
    toggleTreeAction->setShortcut(QKeySequence("Ctrl+T"));
    toggleTreeAction->setCheckable(true);
    toggleTreeAction->setChecked(true);
    connect(toggleTreeAction, &QAction::triggered, this, &MainApplication::toggleTreeView);
    
    QAction *fullScreenPDFAction = viewMenu->addAction("&Full Screen PDF");
    fullScreenPDFAction->setShortcut(QKeySequence("F11"));
    connect(fullScreenPDFAction, &QAction::triggered, this, &MainApplication::toggleFullScreenPDF);
    
    viewMenu->addSeparator();
    QAction *fullUpdateAction = viewMenu->addAction("&Full Update UI");
    fullUpdateAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(fullUpdateAction, &QAction::triggered, this, &MainApplication::onFullUpdateUI);
    
    viewMenu->addSeparator();
    viewMenu->addAction("&Refresh Tree", this, &MainApplication::refreshCurrentTree);
    viewMenu->addAction("&Expand All", this, [this]() { m_treeWidget->expandAll(); });
    viewMenu->addAction("&Collapse All", this, [this]() { m_treeWidget->collapseAll(); });
    
    // Help menu
    QMenu *helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("&About", this, &MainApplication::onAboutClicked);

    // Add right-side container with Home button only (Theme Toggle removed)
    if (!m_homeButton) {
        QWidget *rightContainer = new QWidget(menuBar);
        auto *rightLayout = new QHBoxLayout(rightContainer);
        rightLayout->setContentsMargins(0, 0, 6, 0);
        rightLayout->setSpacing(4);

        // Home
        if (!m_homeButton) {
            m_homeButton = new QToolButton(rightContainer);
            m_homeButton->setText(" Home");
            m_homeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            m_homeButton->setCursor(Qt::PointingHandCursor);
            m_homeButton->setAutoRaise(true);
            QIcon homeIcon;
            if (QFile(":/icons/images/icons/home.svg").exists()) {
                homeIcon.addFile(":/icons/images/icons/home.svg");
            }
            if (homeIcon.isNull()) {
                homeIcon = style()->standardIcon(QStyle::SP_DirHomeIcon);
            }
            m_homeButton->setIcon(homeIcon);
            m_homeButton->setIconSize(QSize(16, 16));
            m_homeButton->setToolTip("Go to Home (root view)");
            connect(m_homeButton, &QToolButton::clicked, this, &MainApplication::onHomeClicked);
        }

        rightLayout->addWidget(m_homeButton, 0, Qt::AlignVCenter);
        rightContainer->setLayout(rightLayout);
        menuBar->setCornerWidget(rightContainer, Qt::TopRightCorner);

        applyMenuBarMaterialStyle();
    }
}

void MainApplication::onHomeClicked()
{
    // Ensure tree is visible and reset to a clean root state
    setTreeViewVisible(true);
    if (m_treeWidget) {
        // Clear search view/results
        if (m_treeSearchEdit) m_treeSearchEdit->clear();
        m_isSearchView = false;
        if (m_searchResultsRoot) m_searchResultsRoot->setHidden(true);

        // Reset and focus the tree
        m_treeWidget->clearSelection();
        m_treeWidget->collapseAll();
        if (m_treeWidget->topLevelItemCount() > 0) {
            auto *first = m_treeWidget->topLevelItem(0);
            if (first) {
                m_treeWidget->setCurrentItem(first);
                m_treeWidget->scrollToItem(first, QAbstractItemView::PositionAtTop);
            }
        }
        m_treeWidget->setFocus();
    }
    statusBar()->showMessage("Home");
}

void MainApplication::onThemeToggleChanged(bool checked)
{
    Q_UNUSED(checked)
    // Function removed - application now uses static light mode only
    // This function is kept for compatibility but does nothing
}

void MainApplication::applyAppPalette(bool dark)
{
    Q_UNUSED(dark)
    // Force light theme - ignore dark parameter
    QPalette pal = qApp->style()->standardPalette();
    
    // Always use light theme colors
    pal.setColor(QPalette::Window, QColor(255, 255, 255));
    pal.setColor(QPalette::Base, QColor(255, 255, 255));
    pal.setColor(QPalette::AlternateBase, QColor(255, 255, 255));
    pal.setColor(QPalette::Highlight, QColor(0, 120, 212));
    pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    
    qApp->setPalette(pal);
}

void MainApplication::setupStatusBar()
{
    m_statusBar = statusBar();
    m_statusBar->setStyleSheet(
        "QStatusBar {"
        "    background-color: #f8f9ff;"
        "    color: #2c3e50;"
        "    border-top: 1px solid #d4e1f5;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "}"
    );
    // Hide the status bar to remove bottom debug/info messages (file name, zoom, etc.)
    // All statusBar()->showMessage(...) calls remain harmless while hidden.
    m_statusBar->hide();
}

void MainApplication::onFullUpdateUI()
{
    if (m_tabWidget) {
        statusBar()->showMessage("Refreshing UI styles…", 1500);
        // Force reapply styles on both tab widgets
        m_tabWidget->forceStyleRefresh();
        // Also update any dynamic visuals
        // updateTabBarVisualState is private to DualTabWidget; forceStyleRefresh re-applies styles.
    }
}

// --- SmoothTreeDelegate paint implementation ---
void SmoothTreeDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    // Determine animation state
    double t = 0.0;
    if (index == m_hovered) {
        t = m_progress; // fade in
    } else if (index == m_last) {
        t = 1.0 - m_progress; // fade out
    }
    t = std::clamp(t, 0.0, 1.0);
    // Ease for smoother feel
    t = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t);

    QColor baseCol = m_base;
    QColor hoverCol = m_hover;
    QColor blended = baseCol;
    blended.setRedF(baseCol.redF() + (hoverCol.redF() - baseCol.redF()) * t);
    blended.setGreenF(baseCol.greenF() + (hoverCol.greenF() - baseCol.greenF()) * t);
    blended.setBlueF(baseCol.blueF() + (hoverCol.blueF() - baseCol.blueF()) * t);
    blended.setAlphaF(baseCol.alphaF() + (hoverCol.alphaF() - baseCol.alphaF()) * t);

    // Selected state already handled by style sheet; only custom paint hover blend when not selected
    bool selected = (opt.state & QStyle::State_Selected);
    if (!selected && t > 0.0) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        // Avoid painting over branch toggle region (indentation + 16px icon)
        const QTreeView *tv = qobject_cast<const QTreeView*>(parent());
        int indentation = tv ? tv->indentation() : 20;
        // Compute depth
        int depth = 0; QModelIndex ancestor = index.parent();
        while (ancestor.isValid()) { depth++; ancestor = ancestor.parent(); }
        int iconArea = depth * indentation + 16; // branch indicator + icon width
        QRect fillRect = opt.rect;
        fillRect.setX(fillRect.x() + iconArea);
        if (fillRect.x() < opt.rect.right()) {
            p->fillRect(fillRect, blended);
        }
        p->restore();
    }
    // Draw default text/icon
    QStyledItemDelegate::paint(p, opt, index);
}

// --- SmoothTreeWidget implementation ---
SmoothTreeWidget::SmoothTreeWidget(QWidget *parent) : QTreeWidget(parent) {
    m_delegate = new SmoothTreeDelegate(this);
    setItemDelegate(m_delegate);
    setMouseTracking(true);
    m_animTimer.setInterval(16); // ~60fps
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        if (m_delegate->advance())
            viewport()->update();
        else if (!underMouse())
            m_animTimer.stop();
    });
}
void SmoothTreeWidget::mouseMoveEvent(QMouseEvent *e) {
    QTreeWidget::mouseMoveEvent(e);
    QModelIndex idx = indexAt(e->pos());
    m_delegate->setHovered(idx);
    if (!m_animTimer.isActive()) m_animTimer.start();
    viewport()->update();
}
void SmoothTreeWidget::leaveEvent(QEvent *e) {
    QTreeWidget::leaveEvent(e);
    m_delegate->setHovered(QModelIndex());
    if (!m_animTimer.isActive()) m_animTimer.start();
    viewport()->update();
}

void MainApplication::setupTreeView()
{
    // Container panel with vertical layout for floating search + tree
    m_treePanel = new QWidget(this);
    m_treePanel->setMinimumWidth(250);
    m_treePanel->setMaximumWidth(420);
    QVBoxLayout *panelLayout = new QVBoxLayout(m_treePanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(6);

    // No title bar row; search bar will be the top row

    // Search bar row (with Server/Local toggle on the right)
    m_treeSearchBar = new QWidget(m_treePanel);
    QHBoxLayout *searchLayout = new QHBoxLayout(m_treeSearchBar);
    searchLayout->setContentsMargins(6, 6, 6, 0);
    searchLayout->setSpacing(6);

    m_treeSearchEdit = new QLineEdit(m_treeSearchBar);
    m_treeSearchEdit->setPlaceholderText("Search files by name… (Enter or Find)");
    // Use the built-in clear icon to avoid an extra button cluttering the UI
    m_treeSearchEdit->setClearButtonEnabled(true);
    m_treeSearchEdit->setMinimumHeight(30);
    // Prevent autofocusing at startup; only focus when clicked or via shortcut
    m_treeSearchEdit->setFocusPolicy(Qt::ClickFocus);

    // Remove separate clear button (kept member for compatibility but not shown)
    m_treeSearchClearButton = new QToolButton(m_treeSearchBar);
    m_treeSearchClearButton->setVisible(false);

    m_treeSearchButton = new QPushButton(m_treeSearchBar);
    m_treeSearchButton->setText(" Find");
    m_treeSearchButton->setCursor(Qt::PointingHandCursor);
    m_treeSearchButton->setMinimumHeight(30);
    m_treeSearchButton->setMinimumWidth(68);
    // Try to use app search icon if available
    QIcon searchIcon;
    if (QFile(":/images/icons/search_next.svg").exists()) {
        searchIcon.addFile(":/images/icons/search_next.svg");
    } else if (QFile(":/images/icons/zoom_in.svg").exists()) {
        searchIcon.addFile(":/images/icons/zoom_in.svg");
    }
    if (!searchIcon.isNull()) {
        m_treeSearchButton->setIcon(searchIcon);
        m_treeSearchButton->setIconSize(QSize(16,16));
    }

    searchLayout->addWidget(m_treeSearchEdit, 1);
    // No explicit clear button inserted; QLineEdit shows its own clear icon
    searchLayout->addWidget(m_treeSearchButton, 0);
    // AWS-only mode: no Local/Server/AWS toggle bar
    m_sourceToggleBar = nullptr;
    m_treeSearchBar->setLayout(searchLayout);

    // Inline thin progress bar shown under the search row during searches
    if (!m_treeSearchProgress) {
        m_treeSearchProgress = new QProgressBar(m_treePanel);
        m_treeSearchProgress->setTextVisible(false);
        m_treeSearchProgress->setFormat("");
        m_treeSearchProgress->setMinimum(0);
        m_treeSearchProgress->setMaximum(1);
        m_treeSearchProgress->setValue(0);
        m_treeSearchProgress->setFixedHeight(3);
        m_treeSearchProgress->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        // Clean thin-line style
        m_treeSearchProgress->setStyleSheet(
            "QProgressBar { border: none; background: transparent; margin-left: 6px; margin-right: 6px; }"
            "QProgressBar::chunk { background-color: #0078d4; border-radius: 2px; }"
        );
        m_treeSearchProgress->setVisible(false);
    }

    // Tree widget
    m_treeWidget = new SmoothTreeWidget(m_treePanel);
    m_treeWidget->setHeaderLabel("Treeview");
    m_treeWidget->setMinimumWidth(250);
    m_treeWidget->setMaximumWidth(400);
    
    applyTreeViewTheme();
    
    // Connect signals
    connect(m_treeWidget, &QTreeWidget::itemClicked, this, &MainApplication::onTreeItemClicked);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &MainApplication::onTreeItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, &MainApplication::onTreeItemExpanded);
    connect(m_treeWidget, &QTreeWidget::itemCollapsed, this, &MainApplication::onTreeItemCollapsed);
    // Search connections
    connect(m_treeSearchButton, &QPushButton::clicked, this, &MainApplication::onTreeSearchTriggered);
    // Built-in clear button handles field clearing; also clear results when text becomes empty
    connect(m_treeSearchEdit, &QLineEdit::textChanged, this, [this](const QString &t){
        if (!t.trimmed().isEmpty()) return;
        m_searchResultPaths.clear();
        m_searchResultIndex = -1;
        if (m_isSearchView) {
            m_isSearchView = false;
            refreshCurrentTree();
        } else if (m_searchResultsRoot) {
            m_searchResultsRoot->takeChildren();
            m_searchResultsRoot->setHidden(true);
            m_searchResultsRoot->setExpanded(false);
        }
        statusBar()->showMessage("Search cleared");
    });
    connect(m_treeSearchEdit, &QLineEdit::returnPressed, this, &MainApplication::onTreeSearchTriggered);

    panelLayout->addWidget(m_treeSearchBar);
    // Place thin progress line right below the search bar
    if (m_treeSearchProgress)
        panelLayout->addWidget(m_treeSearchProgress);
    panelLayout->addWidget(m_treeWidget, 1);
    m_treePanel->setLayout(panelLayout);

    // Prepare loading overlay for tree operations (listing/downloads)
    if (!m_treeLoadingOverlay) {
        m_treeLoadingOverlay = new LoadingOverlay(m_treePanel);
        connect(m_treeLoadingOverlay, &LoadingOverlay::cancelRequested, this, [this]() {
            m_cancelAwsQueue = true;
            statusBar()->showMessage("Cancelling pending downloads…", 2000);
        });
    }
    
    // Tree will be populated after AWS configuration; no local/server population in AWS-only mode
}

void MainApplication::applyTreeViewTheme()
{
    if (!m_treeWidget)
        return;

    // Force light theme - ignore palette darkness detection
    bool dark = false; // Always use light theme

    // Clean Design Palette - Pure White Background
    QString border        = "#d7dbe2";           // Light theme border
    QString bg            = "#ffffff";           // Pure white background - no alternating colors
    QString bgAlt         = "#ffffff";           // Same as main background for uniform look
    QString text          = "#1a1a1a";           // Deep black for high contrast and readability
    QString textDisabled  = "#999999";           // Muted gray for disabled text
    QString placeholder   = "#8b9197";          // Placeholder text color
    QString hover         = "#f3f5f7";           // Neutral hover
    QString selectedBg    = "#0078d4";           // Microsoft blue for selections
    QString selectedBgInactive = "#e6f2ff";     // Light blue for inactive selection
    QString selectedText  = "#ffffff";                              // White text on selection
    QString focusOutline  = "#0078d4";           // Blue focus outline
    QString scrollbarGroove = "#f5f5f5";        // Light gray scrollbar track
    QString scrollbarHandle = "#d0d0d0";        // Medium gray scrollbar handle
    QString scrollbarHandleHover = "#b0b0b0";   // Darker gray on hover
    QString altRow        = "#ffffff";           // Same as main background for uniform appearance
    
    // Build clean stylesheet with smaller fonts and uniform background
    QString style = QString(
        "QTreeWidget {"
        "  border: 1px solid %1;"
        "  border-radius: 8px;"                                     // Slightly larger radius for modern look
        "  background: %2;"
        "  font-family: 'Segoe UI', 'SF Pro Display', -apple-system, BlinkMacSystemFont, sans-serif;" // Premium system fonts
        "  font-size: 12px;"                                        // Smaller font size for compact appearance
        "  font-weight: 500;"                                       // Medium weight for all text
        "  color: %3;"
        "  outline: none;"
        "  show-decoration-selected: 1;"
        "  selection-background-color: %6;"
        "  gridline-color: transparent;"                            // Remove grid lines for cleaner look
        "  alternate-background-color: %2;"                         // Same as main background - no alternating colors
        "}"
        "QTreeView::item, QTreeWidget::item {"
        "  padding: 6px 10px;"                                      // Reduced padding for compact look
        "  margin: 0px;"                                            // No margin for uniform appearance
        "  border: none;"
        "  color: %3;"
        "  font-size: 12px;"                                        // Smaller consistent font size
        "  font-weight: 500;"                                       // Medium weight for all items (files and folders same)
        "  min-height: 20px;"                                       // Smaller minimum height
        "  border-radius: 4px;"                                     // Smaller rounded corners for items
        "  background: transparent;"                                 // Transparent background by default
        "}"
        "QTreeWidget::item:disabled { "
        "  color: %4; "
        "  font-weight: 400;"                                       // Normal weight for disabled items
        "}"
        "QTreeWidget::item:hover {"
        "  background: %5;"                                         // Subtle hover effect
        "  border-radius: 4px;"
        "}"
        "QTreeWidget::item:selected {"
        "  background: %6;"
        "  color: %7;"
        "  border-radius: 4px;"
        "  font-weight: 500;"                                       // Same weight for selected items
        "}"
        "QTreeWidget::item:selected:!active {"
        "  background: %8;"
        "  color: %3;"
        "  border-radius: 4px;"
        "  font-weight: 500;"                                       // Same weight for inactive selection
        "}"
        "QTreeWidget::item:focus {"
        "  outline: none;"
        "  border-radius: 4px;"
        "}"
        "QTreeWidget::item:selected:focus {"
        "  box-shadow: 0 0 0 1px %9;"                               // Subtle focus outline
        "  border-radius: 4px;"
        "}"
        "QTreeWidget::header {"
        "  background: %2;"
        "  border: none;"
        "  font-weight: 600;"                                       // Bold header text
        "  font-size: 13px;"                                        // Slightly larger header font
        "  padding: 6px 10px;"
        "}"
        // Clean scrollbar styling
        "QScrollBar:vertical {"
        "  background: %10;"
        "  width: 12px;"                                            // Standard width
        "  margin: 0;"
        "  border: none;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %11;"
        "  min-height: 24px;"                                       // Standard handle size
        "  border-radius: 6px;"
        "  margin: 1px;"                                            // Small margin for visual separation
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: %12;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal {"
        "  background: %10;"
        "  height: 12px;"
        "  margin: 0;"
        "  border: none;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: %11;"
        "  min-width: 24px;"
        "  border-radius: 6px;"
        "  margin: 1px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "  background: %12;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        ).arg(border)          // %1
         .arg(bg)              // %2
         .arg(text)            // %3
         .arg(textDisabled)    // %4
         .arg(hover)           // %5
         .arg(selectedBg)      // %6
         .arg(selectedText)    // %7
         .arg(selectedBgInactive) // %8
         .arg(focusOutline)       // %9
         .arg(scrollbarGroove)    // %10
         .arg(scrollbarHandle)    // %11
         .arg(scrollbarHandleHover); // %12

    m_treeWidget->setStyleSheet(style);
    m_treeWidget->setIndentation(20);                               // Standard indentation
    m_treeWidget->setIconSize(QSize(16, 16));                       // Standard icon size
    m_treeWidget->setRootIsDecorated(true);                         // Ensure branch decorations are shown
    m_treeWidget->setUniformRowHeights(true);                       // Uniform row heights for cleaner look
    m_treeWidget->setHeaderHidden(false);                           // Show header for professional look
    m_treeWidget->setAnimated(true);                                // Enable smooth expand/collapse animations
    
    // Configure clean styling for uniform appearance
    if (auto *smooth = qobject_cast<SmoothTreeWidget*>(m_treeWidget)) {
        QColor baseCol = QColor(bg);
        QColor hovCol = QColor(hover);
        hovCol.setAlphaF(0.5);                                      // Subtle hover effect
        smooth->smoothDelegate()->setColors(baseCol, hovCol);
    }
    
    // Set up clean uniform background - no alternating rows
    QPalette pal = m_treeWidget->palette();
    pal.setColor(QPalette::Base, QColor(bg));                       // Pure white background
    pal.setColor(QPalette::AlternateBase, QColor(bg));              // Same as base - no alternating colors
    pal.setColor(QPalette::Highlight, QColor(selectedBg));
    pal.setColor(QPalette::HighlightedText, QColor(selectedText));
    pal.setColor(QPalette::Text, QColor(text));
    m_treeWidget->setPalette(pal);
    m_treeWidget->setAlternatingRowColors(false);                   // Disable alternating row colors for uniform look
    
    // Additional clean UI settings
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);  // Smooth scrolling
    m_treeWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_treeWidget->setFocusPolicy(Qt::StrongFocus);
    
    qDebug() << "Applied clean TreeView theme - Light mode only (dark theme disabled)";

    // Style search bar container and widgets to match theme (light/dark aware)
    if (m_treeSearchBar) {
        m_treeSearchBar->setStyleSheet(QString(
            "QWidget {"
            "  background: %1;"
            "}"
        ).arg(bg));
    }
    // Style search widgets to match theme
    if (m_treeSearchBar) {
        // Clean, compact field - explicit background/text for reliable dark/light
        if (m_treeSearchEdit) {
            // Slightly stronger hover border than base
            QString hoverBorder = dark ? "#4a5560" : "#c7ccd6";
            m_treeSearchEdit->setStyleSheet(QString(
                "QLineEdit {"
                "  background: %2;"
                "  color: %3;"
                "  border: 1px solid %1;"
                "  border-radius: 8px;"
                "  padding: 6px 10px;"
                "  selection-background-color: %6;"
                "  selection-color: %7;"
                "}"
                "QLineEdit::placeholder { color: %5; }"
                "QLineEdit:hover { border-color: %8; }"
                "QLineEdit:focus { border: 1px solid %4; }"
                "QLineEdit > QToolButton { border: none; background: transparent; padding: 0 6px; }"
                "QLineEdit > QToolButton:hover { background: rgba(127,127,127,32); border-radius: 6px; }"
            )
            .arg(border, bg, text, focusOutline, placeholder, selectedBg, selectedText, hoverBorder));
        }
    }
    // No title bar to style
    if (m_treeSearchButton) {
        // Primary (contained) button for clear affordance
        QString acc = selectedBg;
        QString accText = selectedText;
        QString accHover = "#1e8fe8";  // hover tint
        QString accPressed = "#0062b1"; // pressed tint
        m_treeSearchButton->setStyleSheet(QString(
            "QPushButton {"
            "  background: %1;"
            "  color: %2;"
            "  border: 1px solid %1;"
            "  border-radius: 8px;"
            "  padding: 6px 12px;"
            "  font-weight: 600;"
            "}"
            "QPushButton:hover { background: %3; border-color: %3; }"
            "QPushButton:pressed { background: %4; border-color: %4; }"
            "QPushButton:disabled { background: %5; color: %6; border-color: %7; }"
        ).arg(acc, accText, accHover, accPressed, bg, textDisabled, border));
    }
    if (m_treeSearchClearButton) {
        // No visible clear button; style reset to avoid theme artifacts if ever shown
        m_treeSearchClearButton->setStyleSheet("QToolButton { border: none; background: transparent; } QToolButton:hover { background: transparent; }");
    }

    // AWS-only: no source toggle buttons
    if (false) {
        auto styleToggleBtn = [&](QPushButton *b){
            b->setStyleSheet(QString(
                "QPushButton { padding: 6px 12px; border: 1px solid %1; background: %2; color: %3; border-radius: 8px; }"
                "QPushButton:hover { background: %4; }"
                "QPushButton:checked { background: %5; color: %6; border-color: %5; }"
            ).arg(border, bg, text, hover, selectedBg, selectedText));
        };
        if (m_btnServer) styleToggleBtn(m_btnServer);
        if (m_btnLocal)  styleToggleBtn(m_btnLocal);
        if (m_btnAws)    styleToggleBtn(m_btnAws);
    }
}

void MainApplication::setupSourceToggleBar()
{
    // AWS-only mode: no toggle bar
    m_sourceToggleBar = new QWidget(m_treePanel);
    m_sourceToggleBar->setVisible(false);
}

// Create and style the search widgets (called within setupTreeView)
void MainApplication::setupTreeSearchBar() { /* kept for future extension if needed */ }

// Perform filename search under root and reveal results sequentially
void MainApplication::onTreeSearchTriggered()
{
    const QString term = m_treeSearchEdit ? m_treeSearchEdit->text().trimmed() : QString();
    if (term.isEmpty()) {
        statusBar()->showMessage("Enter a file name to search");
        return;
    }

    // Show searching status immediately with a thin inline progress line
    statusBar()->showMessage("Searching...");
    showInlineSearchProgress();
    
    // Disable search controls to prevent multiple concurrent searches
    if (m_treeSearchEdit) m_treeSearchEdit->setEnabled(false);
    if (m_treeSearchButton) m_treeSearchButton->setEnabled(false);
    
    // Recompute full result list when term changes
    if (term.compare(m_lastSearchTerm, Qt::CaseInsensitive) != 0) {
        
        // Use QTimer to perform search asynchronously and prevent UI blocking
        QTimer::singleShot(10, this, [this, term]() {
            try {
                // Perform search with progress updates for large operations
                m_searchResultPaths = findMatchingFilesAsync(term, -1);
                m_lastSearchTerm = term;
                m_searchResultIndex = -1;

                // Re-enable search controls
                if (m_treeSearchEdit) m_treeSearchEdit->setEnabled(true);
                if (m_treeSearchButton) m_treeSearchButton->setEnabled(true);

                if (m_searchResultPaths.isEmpty()) {
                    hideInlineSearchProgress();
                    // If we were in a search-only view, restore the tree
                    if (m_isSearchView) {
                        m_isSearchView = false;
                        // In AWS-only mode, just refresh the current (AWS) tree
                        QTimer::singleShot(5, this, [this]() { refreshCurrentTree(); });
                    }
                    // Hide Search Results section if present
                    if (m_searchResultsRoot) {
                        m_searchResultsRoot->takeChildren();
                        m_searchResultsRoot->setHidden(true);
                        m_searchResultsRoot->setExpanded(false);
                    }
                    statusBar()->showMessage("No files found");
                    // Clear the search field and refocus when showing the not-found message
                    if (m_treeSearchEdit) {
                        m_treeSearchEdit->clear();
                        m_treeSearchEdit->setFocus(Qt::OtherFocusReason);
                    }
                    // Reset last term so next search recomputes immediately
                    m_lastSearchTerm.clear();
                    showNoticeDialog(QString("No files found for ‘%1’.\nTry a different name.").arg(term), QStringLiteral("Search"));
                    return;
                }

                // Show results asynchronously to prevent UI freezing
                QTimer::singleShot(5, this, [this, term]() {
                    renderSearchResultsFlat(m_searchResultPaths, term);
                    hideInlineSearchProgress();
                    statusBar()->showMessage(QString("%1 match(es)").arg(m_searchResultPaths.size()));
                    // If search was trimmed for performance, surface that gently
                    if (m_lastSearchTrimmed) {
                        showTreeIssue(this, "Search trimmed for performance", "Showing a partial list to keep the app responsive");
                    }
                });
                
            } catch (const std::exception &e) {
                // Re-enable controls on error
                if (m_treeSearchEdit) m_treeSearchEdit->setEnabled(true);
                if (m_treeSearchButton) m_treeSearchButton->setEnabled(true);
                hideInlineSearchProgress();
                statusBar()->showMessage(QString("Search error: %1").arg(e.what()));
                qDebug() << "Search error:" << e.what();
            } catch (...) {
                // Re-enable controls on error
                if (m_treeSearchEdit) m_treeSearchEdit->setEnabled(true);
                if (m_treeSearchButton) m_treeSearchButton->setEnabled(true);
                hideInlineSearchProgress();
                statusBar()->showMessage("Unknown search error occurred");
                qDebug() << "Unknown search error occurred";
            }
        });
        return;
    }

    // Re-enable controls immediately for unchanged search term
    if (m_treeSearchEdit) m_treeSearchEdit->setEnabled(true);
    if (m_treeSearchButton) m_treeSearchButton->setEnabled(true);
    hideInlineSearchProgress();

    // Term unchanged -> do not navigate; just reaffirm results count
    if (m_searchResultPaths.isEmpty()) {
        statusBar()->showMessage("No files found");
    } else {
        statusBar()->showMessage(QString("%1 match(es)").arg(m_searchResultPaths.size()));
    }
}

// Replace the tree with a flat list of search results (AWS-only)
// 'results' contains S3 keys (not local paths)
void MainApplication::renderSearchResultsFlat(const QVector<QString> &results, const QString &term)
{
    try {
        Q_UNUSED(term);
        m_isSearchView = true;
        m_treeWidget->clear();
        // Header label remains "Treeview" (represents AWS bucket content)

        // Create a simple flat list: one top-level item per result (with a safe display cap)
        constexpr int kMaxShow = 5000; // avoid excessive UI nodes
        int shown = 0;
        for (const QString &key : results) {
            if (shown >= kMaxShow) break;
            // Derive name and parent path from key
            const QString normKey = QString(key).replace('\\', '/');
            const int lastSlash = normKey.lastIndexOf('/');
            const QString fileName = (lastSlash >= 0) ? normKey.mid(lastSlash + 1) : normKey;
            const QString parentPath = (lastSlash > 0) ? normKey.left(lastSlash) : QString();
            const QString baseName = QFileInfo(fileName).completeBaseName();
            const QString display = parentPath.isEmpty()
                ? fileName
                : QString("%1 — %2").arg(fileName, parentPath);

            QTreeWidgetItem *it = new QTreeWidgetItem(m_treeWidget);
            it->setText(0, display);
            // Store AWS key (used by double-click AWS queue)
            it->setData(0, Qt::UserRole + 10, normKey);
            // Do NOT set Qt::UserRole (local path) in AWS-only mode
            it->setIcon(0, getFileIcon(fileName));
            it->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
            it->setToolTip(0, baseName);
            ++shown;
        }

        // Sort alphabetically for predictability and apply compact look
        m_treeWidget->sortItems(0, Qt::AscendingOrder);
        m_treeWidget->setAlternatingRowColors(true);
        m_treeWidget->setStyleSheet(m_treeWidget->styleSheet() + QString(
            "\nQTreeWidget::item { padding: 4px 6px; }\n"
            "QTreeWidget::item:selected { border-radius: 8px; }\n"));

        if (results.size() > kMaxShow) {
            showTreeIssue(this, "Search trimmed for performance",
                          QString("%1 total results; showing first %2").arg(results.size()).arg(kMaxShow));
            statusBar()->showMessage(QString("Showing first %1 of %2 matches").arg(kMaxShow).arg(results.size()), 5000);
        }
    } catch (const std::exception &e) {
        showTreeIssue(this, "Error rendering search results", e.what());
    } catch (...) {
        showTreeIssue(this, "Unknown error rendering search results");
    }
}

// AWS-only search: traverse S3 prefixes and collect matching file keys
QVector<QString> MainApplication::findMatchingFiles(const QString &term, int maxResults)
{
    QVector<QString> results;
    if (!m_aws.isReady()) return results;
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    const bool noCap = (maxResults < 0);
    m_lastSearchTrimmed = false;
    const qint64 tStart = QDateTime::currentMSecsSinceEpoch();
    const qint64 timeBudgetMs = 2000; // ~2s budget to keep UI responsive

    // Breadth-first over prefixes (folders)
    QStringList queue; queue << QString(); // root prefix
    QSet<QString> visited;
    int visitedPrefixes = 0;
    const int maxPrefixes = 120;   // tighter safety cap
    int processedItems = 0;
    const int pumpEvery = 200;

    while (!queue.isEmpty() && (noCap || results.size() < maxResults)) {
        const QString prefix = queue.takeFirst();
        if (visited.contains(prefix)) continue;
        visited.insert(prefix);
        if (++visitedPrefixes > maxPrefixes) { m_lastSearchTrimmed = true; break; }

        // Time budget
        if ((QDateTime::currentMSecsSinceEpoch() - tStart) > timeBudgetMs) { m_lastSearchTrimmed = true; break; }

        auto listed = m_aws.list(prefix, 1000);
        if (!listed.has_value()) {
            // Skip on error; continue with other prefixes
            continue;
        }

        for (const auto &e : listed.value()) {
            ++processedItems;
            if ((processedItems % pumpEvery) == 0) {
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
            }

            if (e.isDir) {
                // Continue traversal into this "folder" (prefix ends with '/')
                if (!visited.contains(e.key)) queue << e.key;
                continue;
            }

            if (e.name.contains(term, cs)) {
                results.push_back(e.key);
                if (!noCap && results.size() >= maxResults) break;
            }
        }
    }
    return results;
}

// AWS-only async-ish search (same as above with occasional event pumping)
QVector<QString> MainApplication::findMatchingFilesAsync(const QString &term, int maxResults)
{
    // Reuse the AWS search; it already pumps events periodically
    return findMatchingFiles(term, maxResults);
}

// Show a thin, inline indeterminate progress just below the search input
void MainApplication::showInlineSearchProgress(const QString &msg)
{
    if (m_treeSearchProgress) {
        // Switch to pulsing mode by cycling the chunk across [0..1]
        m_treeSearchProgress->setMinimum(0);
        m_treeSearchProgress->setMaximum(0); // Qt indeterminate mode
        m_treeSearchProgress->setVisible(true);
        // Optional status text already handled via statusBar(); msg reserved for future
        Q_UNUSED(msg);
    }
}

void MainApplication::hideInlineSearchProgress()
{
    if (m_treeSearchProgress) {
        m_treeSearchProgress->setVisible(false);
        // Reset to determinate baseline
        m_treeSearchProgress->setMinimum(0);
        m_treeSearchProgress->setMaximum(1);
        m_treeSearchProgress->setValue(0);
    }
}

// Expand parent folders and select the file item if present (lazy-load children as needed)
bool MainApplication::revealPathInTree(const QString &absPath)
{
    try {
        if (!m_treeWidget) return false;
        QFileInfo fi(absPath);
        if (!fi.exists()) return false;

        // Build list of folder names from root to file's parent
        QString rel = QDir(currentRootPath()).relativeFilePath(fi.absolutePath());
        QStringList parts = rel.split(QDir::separator(), Qt::SkipEmptyParts);

        // Find the root item matching m_rootFolderPath; top-level items correspond to entries of the root
        // For our tree, folders are top-level items initially; iterate accordingly
        auto matchChildByName = [](QTreeWidgetItem *item, const QString &name) -> QTreeWidgetItem* {
            for (int i = 0; i < (item ? item->childCount() : 0); ++i) {
                QTreeWidgetItem *c = item->child(i);
                if (c && c->text(0).compare(name, Qt::CaseInsensitive) == 0) return c;
            }
            return nullptr;
        };

        // Navigate/expand down the path
        QTreeWidgetItem *current = nullptr;
        QTreeWidget *tw = m_treeWidget;
        // At top level, search among tw->topLevelItem(i)
        for (const QString &folderName : parts) {
            if (!current) {
                QTreeWidgetItem *next = nullptr;
                for (int i = 0; i < tw->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *ti = tw->topLevelItem(i);
                    if (ti->data(0, Qt::UserRole + 1).toString().endsWith(folderName, Qt::CaseInsensitive) ||
                        ti->text(0).compare(folderName, Qt::CaseInsensitive) == 0) {
                        next = ti; break;
                    }
                }
                if (!next) return false;
                current = next;
            } else {
                // Ensure children are loaded (expand triggers load)
                if (!current->isExpanded()) {
                    current->setExpanded(true);
                    // If had a dummy, onTreeItemExpanded will populate
                }
                QCoreApplication::processEvents();
                // Refresh to ensure children are accessible
                QTreeWidgetItem *next = matchChildByName(current, folderName);
                if (!next) {
                    // Try after forcing expansion signal to run population
                    onTreeItemExpanded(current);
                    QCoreApplication::processEvents();
                    next = matchChildByName(current, folderName);
                }
                if (!next) return false;
                current = next;
            }
        }

        // Now select the file within current
        if (current && !current->isExpanded()) {
            current->setExpanded(true);
            onTreeItemExpanded(current);
            QCoreApplication::processEvents();
        }

        QString baseName = fi.baseName();
        QTreeWidgetItem *fileItem = nullptr;
        if (!current) {
            // Could be a file at top level
            for (int i = 0; i < tw->topLevelItemCount(); ++i) {
                QTreeWidgetItem *ti = tw->topLevelItem(i);
                if (ti->data(0, Qt::UserRole).toString() == fi.absoluteFilePath() ||
                    ti->text(0).compare(baseName, Qt::CaseInsensitive) == 0) { fileItem = ti; break; }
            }
        } else {
            for (int i = 0; i < current->childCount(); ++i) {
                QTreeWidgetItem *c = current->child(i);
                if (c->data(0, Qt::UserRole).toString() == fi.absoluteFilePath() ||
                    c->text(0).compare(baseName, Qt::CaseInsensitive) == 0) { fileItem = c; break; }
            }
        }
        if (!fileItem) return false;

        expandToItem(fileItem);
        tw->setCurrentItem(fileItem);
        tw->scrollToItem(fileItem, QAbstractItemView::PositionAtCenter);
        return true;
    } catch (const std::exception &e) {
        showTreeIssue(this, "Reveal in tree error", e.what());
        return false;
    } catch (...) {
        showTreeIssue(this, "Unknown error revealing item in tree");
        return false;
    }
}

void MainApplication::expandToItem(QTreeWidgetItem *item)
{
    if (!item) return;
    QTreeWidgetItem *p = item->parent();
    while (p) { p->setExpanded(true); p = p->parent(); }
}

void MainApplication::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::StyleChange) {
    applyMenuBarMaterialStyle();
    applyTreeViewTheme();
    }
}

void MainApplication::setupTabWidget()
{
    m_tabWidget = new DualTabWidget();
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    
    // Force light theme on tab widget
    m_tabWidget->setDarkTheme(false);
    
    // Connect dual tab widget signals
    connect(m_tabWidget, &DualTabWidget::tabCloseRequested, 
            this, &MainApplication::onTabCloseRequestedByType);
    connect(m_tabWidget, &DualTabWidget::currentChanged, 
            this, &MainApplication::onTabChangedByType);
    // Non-blocking notification when user hits tab limit (avoid modal dialog that interrupts rendering)
    connect(m_tabWidget, &DualTabWidget::tabLimitReached, this, [this](DualTabWidget::TabType type, int maxTabs){
        const QString kind = (type == DualTabWidget::PDF_TAB) ? "PDF" : "PCB";
        showNoticeDialog(QString("Tab limit (%1) reached. Close a tab to open more.").arg(maxTabs), kind + QStringLiteral(" Tabs"));
    });
}

void MainApplication::loadLocalFiles()
{
    statusBar()->showMessage("Loading files from local directory...");
    
    // Clear existing tree items
    m_treeWidget->clear();
    m_searchResultsRoot = nullptr;
    
    // Check if root folder exists
    QDir rootDir(m_rootFolderPath);
    if (!rootDir.exists()) {
        // Create the folder if it doesn't exist
        if (!rootDir.mkpath(m_rootFolderPath)) {
            statusBar()->showMessage("Error: Could not create or access folder: " + m_rootFolderPath);
            return;
        }
    }
    
    // Populate tree from directory
    populateTreeFromDirectory(m_rootFolderPath);

    // Add (collapsed) Search Results section
    m_searchResultsRoot = new QTreeWidgetItem(m_treeWidget);
    m_searchResultsRoot->setText(0, "Search Results");
    m_searchResultsRoot->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogContentsView));
    m_searchResultsRoot->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    m_searchResultsRoot->setHidden(true);
    m_searchResultsRoot->setExpanded(false);
    
    // Collapse all folders by default for a clean view
    m_treeWidget->collapseAll();
    
    statusBar()->showMessage(QString("Loaded files from: %1").arg(m_rootFolderPath));
}

void MainApplication::loadServerFiles()
{
    try {
        statusBar()->showMessage("Loading files from server directory...");
        m_treeWidget->clear();
        m_searchResultsRoot = nullptr;

        // Hardened: if not available, fall back to Local immediately
        if (m_serverRootPath.isEmpty() || !isDirAvailable(m_serverRootPath)) {
            showTreeIssue(this, "Server unavailable", m_serverRootPath.isEmpty() ? "Path not set" : m_serverRootPath);
            statusBar()->showMessage("Server not available. Falling back to Local.");
            setTreeSource(TreeSource::Local, false);
            loadLocalFiles();
            return;
        }

        QDir rootDir(m_serverRootPath);
        if (!rootDir.exists()) {
            showTreeIssue(this, "Server path not found", m_serverRootPath);
            statusBar()->showMessage("Server path missing. Showing Local.");
            setTreeSource(TreeSource::Local, false);
            loadLocalFiles();
            return;
        }

        populateTreeFromDirectory(m_serverRootPath);

        m_searchResultsRoot = new QTreeWidgetItem(m_treeWidget);
        m_searchResultsRoot->setText(0, "Search Results");
        m_searchResultsRoot->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogContentsView));
        m_searchResultsRoot->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
        m_searchResultsRoot->setHidden(true);
        m_searchResultsRoot->setExpanded(false);

        m_treeWidget->collapseAll();
        statusBar()->showMessage(QString("Loaded files from: %1").arg(m_serverRootPath));
    } catch (const std::exception &e) {
        showTreeIssue(this, "Server load error", e.what());
        setTreeSource(TreeSource::Local, false);
        loadLocalFiles();
    } catch (...) {
        showTreeIssue(this, "Unknown server load error");
        setTreeSource(TreeSource::Local, false);
        loadLocalFiles();
    }
}

void MainApplication::loadAwsFiles()
{
    try {
        statusBar()->showMessage("Loading files from AWS (S3)...");
        m_treeWidget->clear();
        m_searchResultsRoot = nullptr;

        if (!m_aws.isReady()) {
            showTreeIssue(this, "AWS unavailable", "Credentials/bucket not set");
            statusBar()->showMessage("AWS not configured.");
            return;
        }

        auto list = m_aws.list("", 1000);
        if (!list.has_value()) {
            const QString err = m_aws.lastError();
            showTreeIssue(this, "AWS list error", err.isEmpty()?QStringLiteral("Unable to list root"):err);
            statusBar()->showMessage("AWS list failed.");
            return;
        }

        // Populate root level from S3 (simulate folders via prefixes)
        int count = 0;
        const int kMax = 5000;
        for (const auto &e : list.value()) {
            if (++count > kMax) { showTreeIssue(this, "AWS root trimmed for performance"); break; }
            QTreeWidgetItem *item = new QTreeWidgetItem(m_treeWidget);
            if (e.isDir) {
                item->setText(0, e.name);
                item->setIcon(0, getFolderIcon(false));
                // store prefix in role +11; also mark as folder via +1 to reuse visuals
                item->setData(0, Qt::UserRole + 11, e.key);
                item->setData(0, Qt::UserRole + 1, e.key);
                // dummy child for lazy loading
                QTreeWidgetItem *dummy = new QTreeWidgetItem(item);
                dummy->setText(0, "Loading...");
                dummy->setData(0, Qt::UserRole + 2, true);
                item->setExpanded(false);
                item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                item->setToolTip(0, e.name);
            } else {
                item->setText(0, QFileInfo(e.name).baseName());
                item->setIcon(0, getFileIcon(e.name));
                // store key in role +10 (do not use Qt::UserRole which implies local path)
                item->setData(0, Qt::UserRole + 10, e.key);
                item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
                item->setToolTip(0, QFileInfo(e.name).baseName());
            }
            if ((count % 200) == 0) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }

        m_searchResultsRoot = new QTreeWidgetItem(m_treeWidget);
        m_searchResultsRoot->setText(0, "Search Results");
        m_searchResultsRoot->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogContentsView));
        m_searchResultsRoot->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
        m_searchResultsRoot->setHidden(true);
        m_searchResultsRoot->setExpanded(false);

        m_treeWidget->collapseAll();
        statusBar()->showMessage(QString("Loaded AWS bucket: %1").arg(m_aws.bucket()));
    } catch (const std::exception &e) {
        showTreeIssue(this, "AWS load error", e.what());
    } catch (...) {
        showTreeIssue(this, "Unknown AWS load error");
    }
}

void MainApplication::setServerRootPath(const QString &path)
{
    m_serverRootPath = path;
    if (m_treeSource == TreeSource::Server)
        refreshCurrentTree();
}

void MainApplication::setAwsRootPath(const QString &path)
{
    m_awsRootPath = path;
    if (m_treeSource == TreeSource::AWS)
        refreshCurrentTree();
}

void MainApplication::configureAwsFromAuth(const AuthAwsCreds& creds, const QString& authToken)
{
    if (!authToken.isEmpty() && !creds.bucket.isEmpty()) {
        // Configure AWS client in server-proxied mode only
        // The server handles AWS operations with its own credentials
        writeTransitionLog("configureAwsFromAuth: configuring AWS client in server mode");
        
        QString serverUrl = QStringLiteral("https://uoklh0m767.execute-api.us-east-1.amazonaws.com/dev");
        
        m_aws.setServerMode(true, serverUrl, authToken);
        m_aws.setBucket(creds.bucket);
        
        writeTransitionLog(QString("configureAwsFromAuth: AWS configured in server mode (bucket=%1, tokenPresent=%2)")
                          .arg(creds.bucket).arg(!authToken.isEmpty()));
        
        writeTransitionLog("configureAwsFromAuth: AWS client configured successfully");
        
        // AWS-only: no toggle buttons to update
    } else {
        writeTransitionLog("configureAwsFromAuth: missing auth token or bucket from server");
    }
}

void MainApplication::autoLoadAwsCredentials()
{
    // Direct AWS credential loading disabled - only server-proxied mode is supported
    // AWS configuration will be handled through server authentication
    writeTransitionLog("autoLoadAwsCredentials: Direct AWS credentials disabled, using server-proxied mode only");
    
    // Clear any existing saved direct AWS credentials from previous versions
    QSettings settings;
    if (settings.contains("aws/accessKey") || settings.contains("aws/secretKey")) {
        settings.remove("aws/accessKey");
        settings.remove("aws/secretKey");
        settings.remove("aws/region");
        settings.remove("aws/bucket");
        settings.remove("aws/remember");
        writeTransitionLog("autoLoadAwsCredentials: Cleared legacy direct AWS credentials");
    }
}

void MainApplication::switchToAwsTreeview()
{
    writeTransitionLog("switchToAwsTreeview: programmatically switching to AWS");
    
    // Check if AWS is configured and ready
    if (!m_aws.isReady()) {
        writeTransitionLog("switchToAwsTreeview: AWS not ready, cannot switch");
        return;
    }
    
    // AWS-only: no toggle buttons
    
    // Switch to AWS tree source and load files
    setTreeSource(TreeSource::AWS, true);
    writeTransitionLog("switchToAwsTreeview: successfully switched to AWS treeview");
}

void MainApplication::setTreeSource(TreeSource src, bool forceReload)
{
    Q_UNUSED(src)
    // AWS-only: always load AWS when requested to refresh tree
    if (!forceReload && m_treeSource == TreeSource::AWS) return;
    if (!m_aws.isReady()) {
        // In AWS-only mode, if not ready, keep the tree empty with a neutral message
        m_treeSource = TreeSource::AWS;
        if (m_treeWidget) m_treeWidget->clear();
        statusBar()->showMessage("Waiting for AWS configuration…");
        return;
    }
    m_treeSource = TreeSource::AWS;
    loadAwsFiles();
}

QString MainApplication::currentRootPath() const
{
    // In AWS-only mode, return AWS root if set, otherwise local root used for search helpers
    if (!m_awsRootPath.isEmpty()) return m_awsRootPath;
    return m_rootFolderPath;
}

void MainApplication::refreshCurrentTree()
{
    // Delegate to setTreeSource with force (keeps logic in one place)
    setTreeSource(m_treeSource, true);
}

void MainApplication::populateTreeFromDirectory(const QString &dirPath, QTreeWidgetItem *parentItem)
{
    try {
        QDir dir(dirPath);
        if (!dir.exists()) {
            showTreeIssue(this, "Folder not found", dirPath);
            return;
        }
        // Avoid symlink loops and permission issues
        dir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    // Get all entries (files and directories)
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        
        // Soft cap per directory to avoid UI stalls on huge folders
        const int kMaxPerDir = 5000;
        int count = 0;
        for (const QFileInfo &entry : entries) {
            if (++count > kMaxPerDir) { showTreeIssue(this, "Folder trimmed for performance", dirPath); break; }
            QTreeWidgetItem *item = parentItem ? new QTreeWidgetItem(parentItem)
                                               : new QTreeWidgetItem(m_treeWidget);
            // Set up the tree item appearance
            setupTreeItemAppearance(item, entry);
            if (entry.isDir()) {
                // This is a directory
                item->setData(0, Qt::UserRole + 1, entry.absoluteFilePath()); // Store folder path
                // Add a dummy child item to make the folder expandable (lazy load)
                QTreeWidgetItem *dummyItem = new QTreeWidgetItem(item);
                dummyItem->setText(0, "Loading...");
                dummyItem->setData(0, Qt::UserRole + 2, true); // Mark as dummy
                item->setExpanded(false);
            } else {
                // This is a file
                item->setData(0, Qt::UserRole, entry.absoluteFilePath()); // Store file path
                item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
            }
            // Yield occasionally to keep UI responsive when folders contain many items
            if ((count % 200) == 0) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    } catch (const std::exception &e) {
        showTreeIssue(this, "Error loading folder", e.what());
    } catch (...) {
        showTreeIssue(this, "Unknown error loading folder");
    }
}

void MainApplication::openFileInTab(const QString &filePath)
{
    // Show loading message
    statusBar()->showMessage("Loading file...");
    
    // Check if it's a PDF file
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    
    if (extension == "pdf") {
        openPDFInTab(filePath);
        return;
    }
    
    // Check if it's a PCB file
    if (extension == "xzz" || extension == "pcb" || extension == "xzzpcb") {
        openPCBInTab(filePath);
        return;
    }
    
    // For non-PDF/PCB files, show a message
    statusBar()->showMessage("Only PDF and PCB files are supported in tabs");
    QMessageBox::information(this, "File Type Not Supported", 
        "Only PDF files (.pdf) and PCB files (.xzz, .pcb, .xzzpcb) can be opened in tabs.\n\n"
        "Selected file: " + fileInfo.fileName() + "\n"
        "File type: " + (extension.isEmpty() ? "Unknown" : extension.toUpper()));
}

void MainApplication::openPDFInTab(const QString &filePath)
{
    // First: see if this PDF is already open (should NOT be blocked by limit)
    auto normalizePath = [](QString p){ QString n = p; n.replace("\\", "/"); return n.toLower(); };
    const QString targetNorm = normalizePath(filePath);
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PDF_TAB); ++i) {
        QWidget *tabWidget = m_tabWidget->widget(i, DualTabWidget::PDF_TAB);
        if (tabWidget) {
            QString existing = tabWidget->property("filePath").toString();
            if (!existing.isEmpty() && normalizePath(existing) == targetNorm) {
                m_tabWidget->setCurrentIndex(i, DualTabWidget::PDF_TAB);
                statusBar()->showMessage("PDF already open (switched)", 4000);
                return;
            }
        }
    }

    // Enforce max PDF tab limit ONLY for new tabs
    const int kMaxPdfTabs = 5;
    if (m_tabWidget->count(DualTabWidget::PDF_TAB) >= kMaxPdfTabs) {
        const QString msg = QString("Tab limit (%1) reached. Close a tab to open more.").arg(kMaxPdfTabs);
        showNoticeDialog(msg, QStringLiteral("PDF Tabs"));
        return;
    }

    // Proceed with load
    statusBar()->showMessage("Loading PDF file...");
    
    // (Duplicate check already performed above)
    
    // Verify file exists and is readable
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        statusBar()->showMessage("Error: Cannot access PDF file: " + filePath);
        QMessageBox::warning(this, "PDF Error", "Cannot access PDF file:\n" + filePath);
        return;
    }
    
    // Create PDF viewer widget
    PDFViewerWidget *pdfViewer = new PDFViewerWidget();
    pdfViewer->setProperty("filePath", filePath);
    
    // No toolbar to hide - PDF viewer is now toolbar-free
    
    // Connect PDF viewer signals
    connect(pdfViewer, &PDFViewerWidget::pdfLoaded, this, [this, filePath](const QString &loadedPath) {
        Q_UNUSED(loadedPath)
        QFileInfo fileInfo(filePath);
        statusBar()->showMessage(QString("PDF loaded: %1").arg(fileInfo.fileName()));
    });
    
    connect(pdfViewer, &PDFViewerWidget::errorOccurred, this, [this](const QString &error) {
        statusBar()->showMessage("PDF Error: " + error);
        QMessageBox::warning(this, "PDF Error", error);
    });
    
    connect(pdfViewer, &PDFViewerWidget::pageChanged, this, [this](int currentPage, int totalPages) {
        statusBar()->showMessage(QString("PDF Page %1 of %2").arg(currentPage).arg(totalPages));
    });
    
    connect(pdfViewer, &PDFViewerWidget::zoomChanged, this, [this](double zoomLevel) {
        statusBar()->showMessage(QString("PDF Zoom: %1%").arg(static_cast<int>(zoomLevel * 100)));
    });
    
    // Split view removed: embedding and tree view toggle connections omitted
    
    // Add PDF viewer to PDF tab row (will fail with -1 if limit race condition)
    QString tabName = fileInfo.fileName();
    QIcon tabIcon = getFileIcon(filePath);
    int tabIndex = m_tabWidget->addTab(pdfViewer, tabIcon, tabName, DualTabWidget::PDF_TAB);
        if (tabIndex < 0) {
            // Safety: dual tab widget rejected addition (limit). Clean up and exit.
            pdfViewer->deleteLater();
            showNoticeDialog("Tab limit reached. Close a tab to open more.", QStringLiteral("PDF Tabs"));
            return;
        }
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex, DualTabWidget::PDF_TAB);
    
    // Load the PDF after the widget is properly initialized
    QTimer::singleShot(100, this, [this, pdfViewer, filePath, tabIndex, fileInfo]() {
        // Try to load the PDF after the widget is properly initialized
    pdfViewer->requestLoad(filePath); // Phase 1 async scaffold
    if (false && !pdfViewer->loadPDF(filePath)) { // legacy path disabled
            // If loading fails, remove the tab and show error
            if (tabIndex < m_tabWidget->count(DualTabWidget::PDF_TAB)) {
                m_tabWidget->removeTab(tabIndex, DualTabWidget::PDF_TAB);
            }
            statusBar()->showMessage("Error: Failed to load PDF file: " + filePath);
            
            QString errorMessage = QString(
                "Failed to load PDF file:\n\n"
                "File: %1\n"
                "Path: %2\n\n"
                "This PDF file may be:\n"
                "• Corrupted or invalid\n"
                "• Incompatible with PDFium library\n" 
                "• Not accessible due to permissions\n\n"
                "The PDF viewer's internal tab system has been disabled to work with the main application tabs."
            ).arg(fileInfo.fileName()).arg(filePath);
            
            QMessageBox::warning(this, "PDF Loading Error", errorMessage);
            return;
        }
    });
    
    // Set tab tooltip
    QString tooltip = QString("PDF File: %1\nPath: %2\nSize: %3 bytes")
                        .arg(fileInfo.fileName())
                        .arg(filePath)
                        .arg(fileInfo.size());
    m_tabWidget->setTabToolTip(tabIndex, tooltip, DualTabWidget::PDF_TAB);
    
    statusBar()->showMessage(QString("Opened PDF: %1").arg(fileInfo.fileName()));
    ensureAutoPairing();
    refreshViewerLinkNames();
}

void MainApplication::openPCBInTab(const QString &filePath)
{
    // Duplicate first (not blocked by limit)
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PCB_TAB); ++i) {
        QWidget *tabWidget = m_tabWidget->widget(i, DualTabWidget::PCB_TAB);
        if (tabWidget && tabWidget->property("filePath").toString() == filePath) {
            m_tabWidget->setCurrentIndex(i, DualTabWidget::PCB_TAB);
            statusBar()->showMessage("PCB already open (switched)", 4000);
            return;
        }
    }

    // Limit only for new tabs
    const int kMaxPcbTabs = 5;
    if (m_tabWidget->count(DualTabWidget::PCB_TAB) >= kMaxPcbTabs) {
        const QString msg = QString("Tab limit (%1) reached. Close a tab to open more.").arg(kMaxPcbTabs);
        showNoticeDialog(msg, QStringLiteral("PCB Tabs"));
        return;
    }

    statusBar()->showMessage("Loading PCB file...");
    
    // Verify file exists and is readable
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        statusBar()->showMessage("Error: Cannot access PCB file: " + filePath);
        QMessageBox::warning(this, "PCB Error", "Cannot access PCB file:\n" + filePath);
        return;
    }
    
    // Create PCB viewer widget
    PCBViewerWidget *pcbViewer = new PCBViewerWidget();
    pcbViewer->setProperty("filePath", filePath);
    
    // Start with toolbar hidden - it will be shown when tab becomes active
    pcbViewer->setToolbarVisible(false);
    
    // Connect PCB viewer signals
    connect(pcbViewer, &PCBViewerWidget::pcbLoaded, this, [this, filePath](const QString &loadedPath) {
        Q_UNUSED(loadedPath)
        QFileInfo fileInfo(filePath);
        statusBar()->showMessage(QString("PCB loaded: %1").arg(fileInfo.fileName()));
    });
    
    connect(pcbViewer, &PCBViewerWidget::errorOccurred, this, [this](const QString &error) {
        statusBar()->showMessage("PCB Error: " + error);
        QMessageBox::warning(this, "PCB Error", error);
    });
    
    // Split view removed: embedding and tree view toggle connections omitted
    
    // Add PCB viewer to PCB tab row
    QString tabName = fileInfo.fileName();
    QIcon tabIcon = getFileIcon(filePath);
    int tabIndex = m_tabWidget->addTab(pcbViewer, tabIcon, tabName, DualTabWidget::PCB_TAB);
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex, DualTabWidget::PCB_TAB);
    
    // Load the PCB after the widget is properly initialized
    QTimer::singleShot(100, this, [this, pcbViewer, filePath, tabIndex, fileInfo]() {
        // Try to load the PCB after the widget is properly initialized
    pcbViewer->requestLoad(filePath); // Phase 1 async scaffold
    if (false && !pcbViewer->loadPCB(filePath)) { // legacy path disabled
            // If loading fails, remove the tab and show error
            if (tabIndex < m_tabWidget->count(DualTabWidget::PCB_TAB)) {
                m_tabWidget->removeTab(tabIndex, DualTabWidget::PCB_TAB);
            }
            statusBar()->showMessage("Error: Failed to load PCB file: " + filePath);
            
            QString errorMessage = QString(
                "Failed to load PCB file:\n\n"
                "File: %1\n"
                "Path: %2\n\n"
                "This PCB file may be:\n"
                "• Corrupted or invalid\n"
                "• Incompatible with XZZPCB format\n" 
                "• Not accessible due to permissions\n\n"
                "Supported formats: .xzz, .pcb, .xzzpcb"
            ).arg(fileInfo.fileName()).arg(filePath);
            
            QMessageBox::warning(this, "PCB Loading Error", errorMessage);
            return;
        }
    });
    
    // Set tab tooltip
    QString tooltip = QString("PCB File: %1\nPath: %2\nSize: %3 bytes")
                        .arg(fileInfo.fileName())
                        .arg(filePath)
                        .arg(fileInfo.size());
    m_tabWidget->setTabToolTip(tabIndex, tooltip, DualTabWidget::PCB_TAB);
    
    statusBar()->showMessage(QString("Opened PCB: %1").arg(fileInfo.fileName()));
    ensureAutoPairing();
    refreshViewerLinkNames();
}

void MainApplication::onTabCloseRequestedByType(int index, DualTabWidget::TabType type)
{
    if (index >= 0 && index < m_tabWidget->count(type)) {
        QWidget *tabWidget = m_tabWidget->widget(index, type);
        if (tabWidget) {
            QString filePath = tabWidget->property("filePath").toString();
            // If this tab was backed by an in-memory file, free it now
            if (filePath.startsWith("memory://")) {
                MemoryFileManager::instance()->removeFile(filePath);
            }
            
            // Remove the tab
            m_tabWidget->removeTab(index, type);
            
            // Update status bar
            if (!filePath.isEmpty()) {
                QFileInfo fileInfo(filePath);
                QString fileType = (type == DualTabWidget::PDF_TAB) ? "PDF" : "PCB";
                statusBar()->showMessage(QString("Closed %1 file: %2").arg(fileType, fileInfo.fileName()));
            } else {
                statusBar()->showMessage("Closed tab");
            }
        }
    }
}

void MainApplication::openFileFromMemory(const QString &memoryId, const QString &originalKey)
{
    // Determine file type from original key
    QString ext = QFileInfo(originalKey).suffix().toLower();
    bool isPdf = (ext == "pdf");
    bool isPcb = (ext == "xzz" || ext == "pcb" || ext == "xzzpcb" || ext == "brd" || ext == "brd2");
    
    if (isPdf) {
        // Check for duplicate PDF tabs using originalKey for consistent comparison
        auto normalizePath = [](QString p){ QString n = p; n.replace("\\", "/"); return n.toLower(); };
        const QString targetNorm = normalizePath(originalKey);
        
        for (int i = 0; i < m_tabWidget->count(DualTabWidget::PDF_TAB); ++i) {
            QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PDF_TAB);
            if (widget) {
                QString existingPath = widget->property("filePath").toString();
                QString existingKey = widget->property("originalKey").toString();
                
                // Compare both memory ID and original key for comprehensive duplicate detection
                if (existingPath == memoryId || 
                    (!existingKey.isEmpty() && normalizePath(existingKey) == targetNorm) ||
                    (!existingPath.isEmpty() && normalizePath(existingPath) == targetNorm)) {
                    m_tabWidget->setCurrentIndex(i, DualTabWidget::PDF_TAB);
                    statusBar()->showMessage("PDF already open (switched)", 4000);
                    return;
                }
            }
        }

        // Enforce max PDF tab limit (consistent with file loading)
        const int kMaxPdfTabs = 5;
        if (m_tabWidget->count(DualTabWidget::PDF_TAB) >= kMaxPdfTabs) {
            const QString msg = QString("Tab limit (%1) reached. Close a tab to open more.").arg(kMaxPdfTabs);
            showNoticeDialog(msg, QStringLiteral("PDF Tabs"));
            return;
        }

        // Create PDF viewer widget
        PDFViewerWidget *pdfViewer = new PDFViewerWidget();
        pdfViewer->setProperty("filePath", memoryId);
        pdfViewer->setProperty("originalKey", originalKey);  // Store for duplicate checking

        // Connect PDF viewer signals
        connect(pdfViewer, &PDFViewerWidget::pdfLoaded, this, [this, originalKey](const QString &loadedPath) {
            Q_UNUSED(loadedPath)
            QFileInfo fileInfo(originalKey);
            statusBar()->showMessage(QString("PDF loaded from memory: %1").arg(fileInfo.fileName()));
        });

        connect(pdfViewer, &PDFViewerWidget::errorOccurred, this, [this](const QString &error) {
            statusBar()->showMessage("PDF Error: " + error);
            QMessageBox::warning(this, "PDF Error", error);
        });

        // Add PDF viewer to PDF tab row
        QString tabName = QFileInfo(originalKey).fileName();  // Use fileName() for consistency with file loading
        QIcon tabIcon = getFileIcon(originalKey);
    int tabIndex = m_tabWidget->addTab(pdfViewer, tabIcon, tabName, DualTabWidget::PDF_TAB);
    if (tabIndex < 0) { pdfViewer->deleteLater(); showNoticeDialog("Tab limit reached. Close a tab to open more.", QStringLiteral("PDF Tabs")); return; }

        // Switch to the new tab
        m_tabWidget->setCurrentIndex(tabIndex, DualTabWidget::PDF_TAB);

        // Load the PDF from memory
        QTimer::singleShot(100, this, [pdfViewer, memoryId, originalKey]() {
            pdfViewer->loadPDFFromMemory(memoryId, originalKey);
        });
    }
    else if (isPcb) {
        // Check for duplicate PCB tabs using originalKey for consistent comparison
        auto normalizePath = [](QString p){ QString n = p; n.replace("\\", "/"); return n.toLower(); };
        const QString targetNorm = normalizePath(originalKey);
        
        for (int i = 0; i < m_tabWidget->count(DualTabWidget::PCB_TAB); ++i) {
            QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PCB_TAB);
            if (widget) {
                QString existingPath = widget->property("filePath").toString();
                QString existingKey = widget->property("originalKey").toString();
                
                // Compare both memory ID and original key for comprehensive duplicate detection
                if (existingPath == memoryId || 
                    (!existingKey.isEmpty() && normalizePath(existingKey) == targetNorm) ||
                    (!existingPath.isEmpty() && normalizePath(existingPath) == targetNorm)) {
                    m_tabWidget->setCurrentIndex(i, DualTabWidget::PCB_TAB);
                    statusBar()->showMessage("PCB already open (switched)", 4000);
                    return;
                }
            }
        }

        // Enforce max PCB tab limit (consistent with file loading - 5 tabs)
        const int kMaxPcbTabs = 5;
    if (m_tabWidget->count(DualTabWidget::PCB_TAB) >= kMaxPcbTabs) { showNoticeDialog(QString("Tab limit (%1) reached. Close a tab to open more.").arg(kMaxPcbTabs), QStringLiteral("PCB Tabs")); return; }

        // Create PCB viewer widget
        PCBViewerWidget *pcbViewer = new PCBViewerWidget(this);
        pcbViewer->setProperty("filePath", memoryId);
        pcbViewer->setProperty("originalKey", originalKey);  // Store for duplicate checking
        
        // Start with toolbar hidden - it will be shown when tab becomes active (consistent with file loading)
        pcbViewer->setToolbarVisible(false);

        // Connect PCB viewer signals
        connect(pcbViewer, &PCBViewerWidget::pcbLoaded, this, [this, originalKey](const QString &loadedPath) {
            Q_UNUSED(loadedPath)
            QFileInfo fileInfo(originalKey);
            statusBar()->showMessage(QString("PCB loaded from memory: %1").arg(fileInfo.fileName()));
        });

        connect(pcbViewer, &PCBViewerWidget::errorOccurred, this, [this](const QString &error) {
            statusBar()->showMessage("PCB Error: " + error);
            QMessageBox::warning(this, "PCB Error", error);
        });

        // Add PCB viewer to PCB tab row
        QString tabName = QFileInfo(originalKey).fileName();  // Use fileName() for consistency with file loading
        QIcon tabIcon = getFileIcon(originalKey);
    int tabIndex = m_tabWidget->addTab(pcbViewer, tabIcon, tabName, DualTabWidget::PCB_TAB);
    if (tabIndex < 0) { pcbViewer->deleteLater(); showNoticeDialog("Tab limit reached. Close a tab to open more.", QStringLiteral("PCB Tabs")); return; }

        // Switch to the new tab
        m_tabWidget->setCurrentIndex(tabIndex, DualTabWidget::PCB_TAB);

        // Load the PCB from memory
        QTimer::singleShot(100, this, [pcbViewer, memoryId, originalKey]() {
            pcbViewer->loadPCBFromMemory(memoryId, originalKey);
        });
    }
    else {
        statusBar()->showMessage("Unsupported file type for memory loading: " + originalKey);
    }
}

void MainApplication::onTabChangedByType(int index, DualTabWidget::TabType type)
{
    qDebug() << "=== Tab Changed to Index:" << index << "Type:" << (type == DualTabWidget::PDF_TAB ? "PDF" : "PCB") << "===";
    
    // Early validation to prevent invalid operations
    if (index < 0 || index >= m_tabWidget->count(type)) {
        statusBar()->showMessage("No active tab");
        qDebug() << "Invalid tab index, returning early";
        return;
    }
    
    QWidget *currentWidget = m_tabWidget->widget(index, type);
    if (!currentWidget) {
        statusBar()->showMessage("Invalid tab selected");
        qDebug() << "Current widget is null, returning early";
        return;
    }
    
    QString tabName = m_tabWidget->tabText(index, type);
    qDebug() << "Switching to tab:" << tabName;
    
    // Use deferred processing to prevent UI blocking
    QTimer::singleShot(0, this, [this, index, type, currentWidget, tabName]() {
        performTabSwitch(index, type, currentWidget, tabName);
    });
}

void MainApplication::performTabSwitch(int index, DualTabWidget::TabType type, QWidget *currentWidget, const QString &tabName)
{
    // Disable UI during switch to prevent user interactions that could cause issues
    setEnabled(false);
    
    try {
        // Light isolation: manage PDF viewers without blocking
        for (int t = 0; t < m_tabWidget->count(DualTabWidget::PDF_TAB); ++t) {
            if (QWidget *w = m_tabWidget->widget(t, DualTabWidget::PDF_TAB)) {
                if (auto pdf = qobject_cast<PDFViewerWidget*>(w)) {
                    // Defer hide/show operations to prevent OpenGL context issues
                    if (!(type == DualTabWidget::PDF_TAB && t == index)) {
                        QTimer::singleShot(10, pdf, [pdf]() { pdf->hide(); });
                    } else {
                        pdf->show();
                    }
                }
            }
        }
        
        // Hide toolbars asynchronously to prevent blocking
        QTimer::singleShot(5, this, [this]() { hideAllViewerToolbars(); });
        
        // Activate the current widget with proper focus management
        currentWidget->setVisible(true);
        currentWidget->raise();
        
        // Handle widget-specific activation
        if (type == DualTabWidget::PDF_TAB) {
            if (auto pdfViewer = qobject_cast<PDFViewerWidget*>(currentWidget)) {
                qDebug() << "Activating PDF viewer for tab:" << tabName;
                
                // Deferred geometry updates to prevent UI freezing
                QTimer::singleShot(20, this, [pdfViewer]() {
                    pdfViewer->updateGeometry();
                    pdfViewer->update();
                    pdfViewer->ensureViewportSync();
                });
                
                statusBar()->showMessage("PDF viewer active - Use keyboard shortcuts for navigation");
            }
        } else if (type == DualTabWidget::PCB_TAB) {
            if (auto pcbViewer = qobject_cast<PCBViewerWidget*>(currentWidget)) {
                qDebug() << "Activating PCB viewer for tab:" << tabName;
                
                // Enable toolbar with delay to prevent freezing
                QTimer::singleShot(10, this, [pcbViewer]() {
                    pcbViewer->setToolbarVisible(true);
                });
                
                // Deferred geometry and viewport sync
                QTimer::singleShot(20, this, [pcbViewer]() {
                    pcbViewer->updateGeometry();
                    pcbViewer->update();
                    pcbViewer->ensureViewportSync();
                });
                
                statusBar()->showMessage("PCB viewer active - Qt toolbar controls available");
            }
        }
        
        // Final setup with delay to ensure all operations complete
        QTimer::singleShot(50, this, [this, currentWidget]() {
            currentWidget->setFocus();
            currentWidget->activateWindow();
            refreshViewerLinkNames();
            setEnabled(true);  // Re-enable UI
            qDebug() << "=== Tab Change Complete ===";
        });
        
    } catch (const std::exception &e) {
        qDebug() << "Exception during tab switch:" << e.what();
        setEnabled(true);  // Ensure UI is re-enabled even on error
    } catch (...) {
        qDebug() << "Unknown exception during tab switch";
        setEnabled(true);  // Ensure UI is re-enabled even on error
    }
}    int MainApplication::linkedPcbForPdf(int pdfIndex) const {
            for (const auto &l : m_tabLinks) {
                if (l.pdfIndex == pdfIndex)
                    return l.pcbIndex;
            }
            return -1;
    }
    int MainApplication::linkedPdfForPcb(int pcbIndex) const {
            for (const auto &l : m_tabLinks) {
                if (l.pcbIndex == pcbIndex)
                    return l.pdfIndex;
            }
            return -1;
    }
    void MainApplication::ensureAutoPairing() {
        int pdfCount = m_tabWidget->count(DualTabWidget::PDF_TAB);
        int pcbCount = m_tabWidget->count(DualTabWidget::PCB_TAB);
        for (int p = 0; p < pdfCount; ++p) {
            if (linkedPcbForPdf(p) >= 0) continue;
            for (int c = 0; c < pcbCount; ++c) {
                if (linkedPdfForPcb(c) < 0) { m_tabLinks.push_back({p,c}); break; }
            }
        }
    }
    void MainApplication::refreshViewerLinkNames() {
        int pdfCount = m_tabWidget->count(DualTabWidget::PDF_TAB);
        int pcbCount = m_tabWidget->count(DualTabWidget::PCB_TAB);
        for (int p=0; p<pdfCount; ++p) {
            if (auto pdf = qobject_cast<PDFViewerWidget*>(m_tabWidget->widget(p, DualTabWidget::PDF_TAB))) {
                int pcbIdx = linkedPcbForPdf(p); QString name; if (pcbIdx>=0) name = m_tabWidget->tabText(pcbIdx, DualTabWidget::PCB_TAB); pdf->setLinkedPcbFileName(name);
                connect(pdf, &PDFViewerWidget::crossSearchRequest, this, &MainApplication::onCrossSearchRequest, Qt::UniqueConnection);
            }
        }
        for (int c=0; c<pcbCount; ++c) {
            if (auto pcb = qobject_cast<PCBViewerWidget*>(m_tabWidget->widget(c, DualTabWidget::PCB_TAB))) {
                int pdfIdx = linkedPdfForPcb(c); QString name; if (pdfIdx>=0) name = m_tabWidget->tabText(pdfIdx, DualTabWidget::PDF_TAB); pcb->setLinkedPdfFileName(name);
                connect(pcb, &PCBViewerWidget::crossSearchRequest, this, &MainApplication::onCrossSearchRequest, Qt::UniqueConnection);
            }
        }
    }

    void MainApplication::onCrossSearchRequest(const QString &term, bool isNet, bool targetIsOther) {
        Q_UNUSED(targetIsOther);
        auto pdfSender = qobject_cast<PDFViewerWidget*>(sender());
        auto pcbSender = qobject_cast<PCBViewerWidget*>(sender());
        if (pdfSender) {
            int pdfIdx=-1; int pdfCount=m_tabWidget->count(DualTabWidget::PDF_TAB);
            for (int i=0;i<pdfCount;++i) if (m_tabWidget->widget(i, DualTabWidget::PDF_TAB)==pdfSender) { pdfIdx=i; break; }
            // Dynamic target resolution: prefer currently selected PCB tab (independent row selection), fallback to auto-pair mapping
            int pcbIdx = m_tabWidget->getSelectedIndex(DualTabWidget::PCB_TAB);
            if (pcbIdx < 0 || pcbIdx >= m_tabWidget->count(DualTabWidget::PCB_TAB)) {
                pcbIdx = (pdfIdx>=0)? linkedPcbForPdf(pdfIdx) : -1; // fallback
            }
            if (pcbIdx<0) { ToastNotifier::show(this, "Select a PCB tab to target"); return; }
            auto pcbW = qobject_cast<PCBViewerWidget*>(m_tabWidget->widget(pcbIdx, DualTabWidget::PCB_TAB));
            if (!pcbW) { ToastNotifier::show(this, "No linked file found"); return; }
            bool ok = isNet ? pcbW->externalSearchNet(term) : pcbW->externalSearchComponent(term);
            if (!ok) ToastNotifier::show(this, "No matches found"); else m_tabWidget->setCurrentIndex(pcbIdx, DualTabWidget::PCB_TAB);
        } else if (pcbSender) {
            int pcbIdx=-1; int pcbCount=m_tabWidget->count(DualTabWidget::PCB_TAB);
            for (int i=0;i<pcbCount;++i) if (m_tabWidget->widget(i, DualTabWidget::PCB_TAB)==pcbSender) { pcbIdx=i; break; }
            // Dynamic target resolution: prefer currently selected PDF tab, fallback to auto-pair mapping
            int pdfIdx = m_tabWidget->getSelectedIndex(DualTabWidget::PDF_TAB);
            if (pdfIdx < 0 || pdfIdx >= m_tabWidget->count(DualTabWidget::PDF_TAB)) {
                pdfIdx = (pcbIdx>=0)? linkedPdfForPcb(pcbIdx) : -1; // fallback
            }
            if (pdfIdx<0) { ToastNotifier::show(this, "Select a PDF tab to target"); return; }
            auto pdfW = qobject_cast<PDFViewerWidget*>(m_tabWidget->widget(pdfIdx, DualTabWidget::PDF_TAB));
            if (!pdfW) { ToastNotifier::show(this, "No linked file found"); return; }
            bool ok = pdfW->externalFindText(term);
            if (!ok) ToastNotifier::show(this, "No matches found"); else m_tabWidget->setCurrentIndex(pdfIdx, DualTabWidget::PDF_TAB);
        }
    }

// Old onTabChanged method - replaced by onTabChangedByType
/*
void MainApplication::onTabChanged(int index)
{
    // This method is no longer used - replaced by onTabChangedByType
    // which handles the dual tab widget system with PDF/PCB type awareness
}
*/

void MainApplication::hideAllViewerToolbars()
{
    // Use thread-safe toolbar management to prevent freezing
    static QMutex toolbarMutex;
    QMutexLocker locker(&toolbarMutex);
    
    qDebug() << "=== Hiding All Viewer Toolbars ===";
    
    // Get current active indices to avoid hiding active toolbars
    int currentPCBIndex = m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
    
    // Process PDF tabs with error handling
    try {
        for (int i = 0; i < m_tabWidget->count(DualTabWidget::PDF_TAB); ++i) {
            QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PDF_TAB);
            if (!widget) continue;
            
            QString tabName = m_tabWidget->tabText(i, DualTabWidget::PDF_TAB);
            
            if (auto pdfViewer = qobject_cast<PDFViewerWidget*>(widget)) {
                qDebug() << "Processing PDF viewer for tab:" << tabName;
                // PDF viewer has no toolbar currently - just ensure geometry is up to date
                pdfViewer->updateGeometry();
                pdfViewer->update();
                qDebug() << "PDF viewer processed for tab:" << i;
            }
        }
    } catch (const std::exception &e) {
        qDebug() << "Exception in PDF toolbar hiding:" << e.what();
    }
    
    // Process PCB tabs with error handling and async operations
    try {
        QList<PCBViewerWidget*> pcbViewers;
        for (int i = 0; i < m_tabWidget->count(DualTabWidget::PCB_TAB); ++i) {
            QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PCB_TAB);
            if (!widget) continue;
            
            if (auto pcbViewer = qobject_cast<PCBViewerWidget*>(widget)) {
                pcbViewers.append(pcbViewer);
            }
        }
        
        // Hide PCB toolbars asynchronously to prevent blocking, but skip the currently active one
        for (int i = 0; i < pcbViewers.size(); ++i) {
            // Skip hiding toolbar for currently active PCB tab
            if (i == currentPCBIndex) {
                qDebug() << "Skipping hide for active PCB tab:" << i;
                continue;
            }
            
            PCBViewerWidget* pcbViewer = pcbViewers[i];
            QString tabName = m_tabWidget->tabText(i, DualTabWidget::PCB_TAB);
            
            // Use timer to defer the hide operation and prevent UI freezing
            QTimer::singleShot(i * 5, this, [pcbViewer, tabName]() {
                if (pcbViewer) {  // Safety check in case widget was deleted
                    qDebug() << "Hiding PCB viewer toolbar for tab:" << tabName;
                    pcbViewer->setToolbarVisible(false);
                    pcbViewer->updateGeometry();
                    pcbViewer->update();
                    qDebug() << "PCB toolbar hidden for tab:" << tabName;
                }
            });
        }
        
    } catch (const std::exception &e) {
        qDebug() << "Exception in PCB toolbar hiding:" << e.what();
    }
    
    // Single event processing to ensure operations are queued properly
    QTimer::singleShot(50, this, []() {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    });
    
    qDebug() << "=== All Viewer Toolbars Hidden (except active) ===";
}

void MainApplication::debugToolbarStates()
{
    qDebug() << "=== Toolbar Debug Info ===";
    qDebug() << "Current PDF tab index:" << m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
    qDebug() << "Current PCB tab index:" << m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
    qDebug() << "Total PDF tabs:" << m_tabWidget->count(DualTabWidget::PDF_TAB);
    qDebug() << "Total PCB tabs:" << m_tabWidget->count(DualTabWidget::PCB_TAB);
    
    // Debug PDF tabs
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PDF_TAB); ++i) {
        QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PDF_TAB);
        QString tabName = m_tabWidget->tabText(i, DualTabWidget::PDF_TAB);
        
        if (qobject_cast<PDFViewerWidget*>(widget)) {
            // PDF viewer now has no toolbar - always report as simplified viewer
            qDebug() << "PDF Tab" << i << "(" << tabName << "): toolbar-free viewer";
        }
    }
    
    // Debug PCB tabs
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PCB_TAB); ++i) {
        QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PCB_TAB);
        QString tabName = m_tabWidget->tabText(i, DualTabWidget::PCB_TAB);
        
        if (auto pcbViewer = qobject_cast<PCBViewerWidget*>(widget)) {
            bool toolbarVisible = pcbViewer->isToolbarVisible();
            qDebug() << "PCB Tab" << i << "(" << tabName << "): toolbar visible:" << toolbarVisible;
        }
    }
    qDebug() << "=========================";
}

void MainApplication::forceToolbarIsolation()
{
    // Simplified: legacy aggressive isolation removed to prevent GL corruption.
    // Retained for backward compatibility if future selective isolation needed.
    hideAllViewerToolbars();
}

/*
// This method was used for the old single content area
// Now replaced by tab-based interface
void MainApplication::setupContentArea()
{
    m_contentWidget = new QWidget();
    m_contentWidget->setStyleSheet(
        "QWidget {"
        "    border: 1px solid #d4e1f5;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
    );
    
    QVBoxLayout *contentLayout = new QVBoxLayout(m_contentWidget);
    
    // Welcome message in content area
    QLabel *welcomeLabel = new QLabel(QString("Welcome to Way2Repair, %1!").arg(m_userSession.fullName));
    welcomeLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 24px;"
        "    font-weight: bold;"
        "    color: #2c3e50;"
        "    padding: 40px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    border: none;"
        "}"
    );
    welcomeLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(welcomeLabel);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(
        "Select a file or folder from the tree view on the left to view its contents.\n\n"
        "Use the toolbar buttons to:\n"
        "• Refresh - Reload the file tree\n"
        "• Expand All - Expand all folders\n"
        "• Collapse All - Collapse all folders\n\n"
        "Double-click on files to open them."
    );
    instructionLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 14px;"
        "    color: #666;"
        "    padding: 20px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    border: none;"
        "    line-height: 1.5;"
        "}"
    );
    instructionLabel->setAlignment(Qt::AlignCenter);
    instructionLabel->setWordWrap(true);
    contentLayout->addWidget(instructionLabel);
    
    contentLayout->addStretch();
}
*/

void MainApplication::addWelcomeTab()
{
    // No welcome tab needed since we only support PDF and PCB files
    // Users will see empty dual tab widget until they open a supported file
    statusBar()->showMessage("Ready - Open PDF or PCB files from the tree view");
}

// ==========================
// SERVER-SIDE METHODS (COMMENTED OUT)
// ==========================

/*
// These methods were used for server-side file loading
// They are commented out to focus on local file loading

void MainApplication::loadFileList()
{
    statusBar()->showMessage("Loading file list from server...");
    
    QString urlString = m_baseUrl + "/files.php";
    QUrl url(urlString);
    
    qDebug() << "Making request to URL:" << urlString;
    qDebug() << "URL is valid:" << url.isValid();
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", "Qt Application");
    
    // Add timeout
    request.setTransferTimeout(10000); // 10 seconds
    
    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("requestType", "fileList");
    
    // Connect to the individual reply's signals instead of the manager's
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHttpRequestFinished(reply);
    });
    connect(reply, &QNetworkReply::errorOccurred, this, &MainApplication::onNetworkError);
    
    qDebug() << "Network request sent successfully";
}

void MainApplication::loadFileContent(const QString &filePath)
{
    statusBar()->showMessage(QString("Loading file: %1").arg(filePath));
    
    qDebug() << "Loading file content for path:" << filePath;
    
    QUrl url(m_baseUrl + "/file_content.php");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    // Add timeout
    request.setTransferTimeout(10000); // 10 seconds
    
    // Send file path as POST data - properly encode for URL form data
    QByteArray postData;
    postData.append("file_path=");
    postData.append(QUrl::toPercentEncoding(filePath));
    
    qDebug() << "POST data:" << postData;
    
    QNetworkReply *reply = m_networkManager->post(request, postData);
    reply->setProperty("requestType", "fileContent");
    reply->setProperty("filePath", filePath);
    
    // Connect to the individual reply's signals
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHttpRequestFinished(reply);
    });
    connect(reply, &QNetworkReply::errorOccurred, this, &MainApplication::onNetworkError);
}

void MainApplication::onHttpRequestFinished(QNetworkReply *reply)
{
    // Server response handling code...
}

void MainApplication::onNetworkError(QNetworkReply::NetworkError error)
{
    // Network error handling code...
}

void MainApplication::parseFileListJson(const QJsonDocument &doc)
{
    // JSON parsing code...
}

void MainApplication::addTreeItem(QTreeWidgetItem *parent, const QJsonObject &item)
{
    // Tree item creation from JSON...
}

void MainApplication::loadFallbackData()
{
    // Fallback data loading...
}

void MainApplication::updateContentArea(const QString &title, const QString &content)
{
    // Content area updating (replaced by tab system)...
}
*/

// ==========================
// SLOT IMPLEMENTATIONS
// ==========================

void MainApplication::onTreeItemClicked(QTreeWidgetItem *item, int column)
{
    try {
        Q_UNUSED(column)
        if (!item) return;
        QString itemText = item->text(0);
        // Check if item has a file path data (files)
        QString filePath = item->data(0, Qt::UserRole).toString();
        // Check if item has a folder path data (folders)
        QString folderPath = item->data(0, Qt::UserRole + 1).toString();
        if (!filePath.isEmpty()) {
            statusBar()->showMessage(QString("Selected: %1 (double-click to open)").arg(itemText));
        } else if (!folderPath.isEmpty()) {
            bool willExpand = !item->isExpanded();
            item->setExpanded(willExpand);
            statusBar()->showMessage(QString("%1 folder: %2").arg(willExpand ? "Expanded" : "Collapsed").arg(itemText));
        } else {
            statusBar()->showMessage(QString("Selected: %1").arg(itemText));
        }
    } catch (const std::exception &e) {
        showTreeIssue(this, "Tree click error", e.what());
    } catch (...) {
        showTreeIssue(this, "Tree click error");
    }
}

void MainApplication::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    try {
        Q_UNUSED(column)
        if (!item) return;
        QString itemText = item->text(0);
        // AWS-only: files store key in UserRole+10
        if (true) {
            // Support multi-selection: queue all selected file keys; process sequentially
            QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
            QStringList keys;
            for (QTreeWidgetItem* it : selected) {
                QString key = it->data(0, Qt::UserRole + 10).toString();
                if (!key.isEmpty()) keys << key;
            }
            // If no multi-select, fallback to the clicked item key
            if (keys.isEmpty()) {
                const QString s3Key = item->data(0, Qt::UserRole + 10).toString();
                if (!s3Key.isEmpty()) keys << s3Key;
            }
            if (!keys.isEmpty()) {
                startAwsDownloadQueue(keys);
                return;
            }
            // If it's a folder, just toggle
            item->setExpanded(!item->isExpanded());
            statusBar()->showMessage(QString("Toggled folder: %1").arg(itemText));
            return;
        }
        // Unreachable in AWS-only mode
    } catch (const std::exception &e) {
        showTreeIssue(this, "Open item error", e.what());
    } catch (...) {
        showTreeIssue(this, "Open item error");
    }
}

// --- AWS multi-selection queue & overlay helpers ---
void MainApplication::showTreeLoading(const QString &message, bool cancellable)
{
    Q_UNUSED(cancellable);
    // Professional, localized overlay inside the tree panel
    if (m_treeLoadingOverlay && m_treePanel) {
        m_treeLoadingOverlay->setParent(m_treePanel);
        m_treeLoadingOverlay->resize(m_treePanel->size());
        m_treeLoadingOverlay->move(QPoint(0, 0));
        m_treeLoadingOverlay->raise();
        m_treeLoadingOverlay->showOverlay(message);
    } else {
        // Fallback to global overlay if tree overlay not available
        showGlobalLoading(message, true);
    }
}

void MainApplication::hideTreeLoading()
{
    if (m_treeLoadingOverlay && m_treeLoadingOverlay->isVisible()) {
        m_treeLoadingOverlay->hideOverlay();
    }
    // Do not hide global overlay here; keep scopes separate
}

void MainApplication::showGlobalLoading(const QString &message, bool cancellable)
{
    Q_UNUSED(cancellable);
    if (m_globalLoadingOverlay) {
        m_globalLoadingOverlay->showOverlay(message);
        m_globalLoadingOverlay->raise();
        m_globalLoadingOverlay->resize(m_centralWidget->size());
    } else if (m_treeLoadingOverlay) {
        // Fallback if global overlay not available
        m_treeLoadingOverlay->showOverlay(message);
    }
}

void MainApplication::hideGlobalLoading()
{
    if (m_globalLoadingOverlay && m_globalLoadingOverlay->isVisible()) {
        m_globalLoadingOverlay->hideOverlay();
    }
    if (m_treeLoadingOverlay && m_treeLoadingOverlay->isVisible()) {
        // Ensure any legacy/tree overlay is also hidden
        m_treeLoadingOverlay->hideOverlay();
    }
}

void MainApplication::showNoticeDialog(const QString &message, const QString &title)
{
    QMessageBox::information(this, title, message);
}

// showNoticeOverlay removed; using modal dialog notices instead

void MainApplication::startAwsDownloadQueue(const QStringList &keys)
{
    if (m_treeBusy) {
        // If already busy, append new keys and keep going
        m_awsQueue << keys;
        return;
    }
    m_treeBusy = true;
    m_cancelAwsQueue = false;
    m_awsQueue = keys;
    m_awsQueueIndex = 0;
    showGlobalLoading(QString("Downloading %1 file%2 from AWS…")
                        .arg(m_awsQueue.size())
                        .arg(m_awsQueue.size()>1?"s":""), true);
    processNextAwsDownload();
}

void MainApplication::processNextAwsDownload()
{
    // Enforce tab limits by TYPE for the next item only. Do not block PDF when only PCB is full, and vice versa.
    const int kMaxPdfTabs = 5;
    const int kMaxPcbTabs = 5;
    if (m_tabWidget && m_awsQueueIndex < m_awsQueue.size()) {
        const QString nextKey = m_awsQueue.at(m_awsQueueIndex);
        const QString ext = QFileInfo(nextKey).suffix().toLower();
        const bool isPdf = (ext == QLatin1String("pdf"));
        const bool isPcb = (ext == QLatin1String("xzz") || ext == QLatin1String("pcb") || ext == QLatin1String("xzzpcb") || ext == QLatin1String("brd") || ext == QLatin1String("brd2"));
        if (isPdf && m_tabWidget->count(DualTabWidget::PDF_TAB) >= kMaxPdfTabs) {
            hideGlobalLoading();
            m_treeBusy = false;
            m_cancelAwsQueue = true;
            showNoticeDialog(QString("Tab limit (%1) reached. Close a tab to open more.").arg(kMaxPdfTabs), QStringLiteral("PDF Tabs"));
            m_awsQueue.clear();
            statusBar()->showMessage("Stopped AWS downloads due to PDF tab limit", 3000);
            return;
        }
        if (isPcb && m_tabWidget->count(DualTabWidget::PCB_TAB) >= kMaxPcbTabs) {
            hideGlobalLoading();
            m_treeBusy = false;
            m_cancelAwsQueue = true;
            showNoticeDialog(QString("Tab limit (%1) reached. Close a tab to open more.").arg(kMaxPcbTabs), QStringLiteral("PCB Tabs"));
            m_awsQueue.clear();
            statusBar()->showMessage("Stopped AWS downloads due to PCB tab limit", 3000);
            return;
        }
    }

    if (m_cancelAwsQueue) {
    hideGlobalLoading();
        m_treeBusy = false;
        m_awsQueue.clear();
        statusBar()->showMessage("Download cancelled", 2000);
        return;
    }
    if (m_awsQueueIndex >= m_awsQueue.size()) {
    hideGlobalLoading();
        m_treeBusy = false;
        statusBar()->showMessage("All downloads complete", 2500);
        return;
    }

    const QString key = m_awsQueue.at(m_awsQueueIndex);
    const int cur = m_awsQueueIndex + 1;
    const int total = m_awsQueue.size();
    showGlobalLoading(QString("Downloading %1 of %2…\n%3").arg(cur).arg(total).arg(QFileInfo(key).fileName()), true);

    // Synchronous call per existing API; if later made async, adapt with callbacks
    auto data = m_aws.downloadToMemory(key);
    if (!data.has_value()) {
        const QString err = m_aws.lastError();
        showTreeIssue(this, "AWS download failed", err.isEmpty()?key:err);
        // Continue to next file even if one fails
        ++m_awsQueueIndex;
        QTimer::singleShot(0, this, [this]() { processNextAwsDownload(); });
        return;
    }

    // Store and open
    MemoryFileManager* memMgr = MemoryFileManager::instance();
    QString memoryId = memMgr->storeFileData(key, data.value());
    openFileFromMemory(memoryId, key);

    // Advance and continue with a tiny delay to keep UI responsive
    ++m_awsQueueIndex;
    QTimer::singleShot(0, this, [this]() { processNextAwsDownload(); });
}

void MainApplication::onTreeItemExpanded(QTreeWidgetItem *item)
{
    try {
        if (!item) return;
        updateTreeItemIcon(item, true);
        // Check if this folder has dummy children and needs to be populated
        if (item->childCount() == 1) {
            QTreeWidgetItem *child = item->child(0);
            if (child && child->data(0, Qt::UserRole + 2).toBool()) {
                // This is a dummy item, remove it and load actual contents
                delete child;
                // Load the actual folder contents
                if (true) { // AWS-only mode
                    const QString prefix = item->data(0, Qt::UserRole + 11).toString();
                    if (!prefix.isEmpty()) {
                        statusBar()->showMessage(QString("Listing: %1...").arg(item->text(0)));
                        auto list = m_aws.list(prefix, 1000);
                        if (!list.has_value()) {
                            const QString err = m_aws.lastError();
                            showTreeIssue(this, "AWS list failed", err.isEmpty()?prefix:err);
                        } else {
                            int count = 0; const int kMax = 5000;
                            for (const auto &e : list.value()) {
                                if (++count > kMax) { showTreeIssue(this, "Folder trimmed for performance"); break; }
                                QTreeWidgetItem *childItem = new QTreeWidgetItem(item);
                                if (e.isDir) {
                                    childItem->setText(0, e.name);
                                    childItem->setIcon(0, getFolderIcon(false));
                                    childItem->setData(0, Qt::UserRole + 11, e.key);
                                    childItem->setData(0, Qt::UserRole + 1, e.key);
                                    QTreeWidgetItem *dummy = new QTreeWidgetItem(childItem);
                                    dummy->setText(0, "Loading...");
                                    dummy->setData(0, Qt::UserRole + 2, true);
                                    childItem->setExpanded(false);
                                    childItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                                    childItem->setToolTip(0, e.name);
                                } else {
                                    childItem->setText(0, QFileInfo(e.name).baseName());
                                    childItem->setIcon(0, getFileIcon(e.name));
                                    childItem->setData(0, Qt::UserRole + 10, e.key);
                                    childItem->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
                                    childItem->setToolTip(0, QFileInfo(e.name).baseName());
                                }
                                if ((count % 200) == 0) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                            }
                        }
                        statusBar()->showMessage(QString("Loaded: %1").arg(item->text(0)));
                    }
                } else { /* no local/server branch in AWS-only mode */ }
            }
        }
    } catch (const std::exception &e) {
        showTreeIssue(this, "Expand folder error", e.what());
    } catch (...) {
        showTreeIssue(this, "Expand folder error");
    }
}

void MainApplication::onTreeItemCollapsed(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Update folder icon to "closed" state
    updateTreeItemIcon(item, false);
}

void MainApplication::setupTreeItemAppearance(QTreeWidgetItem *item, const QFileInfo &fileInfo)
{
    // Set display name - show full name for directories, base name (no extension) for files
    if (fileInfo.isDir()) {
        item->setText(0, fileInfo.fileName());
    } else {
        item->setText(0, fileInfo.baseName()); // Hide file extension
    }
    
    if (fileInfo.isDir()) {
        // This is a directory
        item->setIcon(0, getFolderIcon(false));
        
        // Tooltip should only show the folder name
        item->setToolTip(0, fileInfo.fileName());
        
        // Set folder-specific properties
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    } else {
        // This is a file
        item->setIcon(0, getFileIcon(fileInfo.absoluteFilePath()));
        
        // Tooltip should only show the file name without extension
        item->setToolTip(0, fileInfo.baseName());
        
        // Set file-specific properties
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }
}

void MainApplication::updateTreeItemIcon(QTreeWidgetItem *item, bool isExpanded)
{
    if (!item) return;
    
    // Check if this is a folder
    QString folderPath = item->data(0, Qt::UserRole + 1).toString();
    if (!folderPath.isEmpty()) {
        item->setIcon(0, getFolderIcon(isExpanded));
    }
}

QIcon MainApplication::getFileIcon(const QString &filePath)
{
    QString extension = getFileExtension(filePath);
    
    // Use Qt's built-in standard icons based on file type
    if (isCodeFile(extension)) {
        return style()->standardIcon(QStyle::SP_ComputerIcon);
    } else if (isImageFile(extension)) {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else if (isArchiveFile(extension)) {
        return style()->standardIcon(QStyle::SP_DriveHDIcon);
    } else if (isOfficeFile(extension)) {
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    } else if (extension == "pdf") {
        // Custom PDF icon from resources (added PDF_icon.svg)
        QIcon pdfIcon(":/icons/images/icons/PDF_icon.svg");
        if (!pdfIcon.isNull()) return pdfIcon;
        return style()->standardIcon(QStyle::SP_FileDialogListView);
    } else if (extension == "pcb" || extension == "xzz" || extension == "xzzpcb") {
        // Custom PCB icon
        QIcon pcbIcon(":/icons/images/icons/PCB_icon.svg");
        if (!pcbIcon.isNull()) return pcbIcon;
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else if (extension == "txt" || extension == "log" || extension == "md") {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else {
        // Default file icon
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}

QIcon MainApplication::getFolderIcon(bool isOpen)
{
    // Choose themed variant (could later switch to dark-specific artwork if added)
    QString basePath = ":/icons/images/icons/";
    QString file = isOpen ? "folder_open.svg" : "folder_closed.svg";
    QIcon icon(basePath + file);
    if (!icon.isNull())
        return icon;
    // Fallback to system icon if resource missing
    return style()->standardIcon(isOpen ? QStyle::SP_DirOpenIcon : QStyle::SP_DirClosedIcon);
}

QString MainApplication::getFileExtension(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    return fileInfo.suffix().toLower();
}

bool MainApplication::isCodeFile(const QString &extension)
{
    static const QStringList codeExtensions = {
        "cpp", "c", "h", "hpp", "cc", "cxx", "hxx",
        "js", "ts", "jsx", "tsx", "py", "java", "cs",
        "html", "htm", "css", "scss", "sass", "less",
        "json", "xml", "yaml", "yml", "sql", "php",
        "rb", "go", "rs", "swift", "kt", "dart", "r",
        "m", "mm", "scala", "groovy", "pl", "sh", "bat",
        "ps1", "cmake", "make", "makefile", "pro", "pri"
    };
    return codeExtensions.contains(extension);
}

bool MainApplication::isImageFile(const QString &extension)
{
    static const QStringList imageExtensions = {
        "png", "jpg", "jpeg", "gif", "bmp", "tiff", "tif",
        "svg", "ico", "webp", "psd", "ai", "eps"
    };
    return imageExtensions.contains(extension);
}

bool MainApplication::isArchiveFile(const QString &extension)
{
    static const QStringList archiveExtensions = {
        "zip", "rar", "7z", "tar", "gz", "bz2", "xz",
        "cab", "iso", "dmg", "pkg", "deb", "rpm"
    };
    return archiveExtensions.contains(extension);
}

bool MainApplication::isOfficeFile(const QString &extension)
{
    static const QStringList officeExtensions = {
        "doc", "docx", "xls", "xlsx", "ppt", "pptx",
        "odt", "ods", "odp", "rtf", "pages", "numbers", "keynote"
    };
    return officeExtensions.contains(extension);
}

void MainApplication::onAboutClicked()
{
    QMessageBox::about(this, "About Way2Repair",
        "<h2>Way2Repair v4.75</h2>"
        "<p>Inquiry System for Intelligent Terminal Equipment Maintenance</p>"
        "<p>Professional equipment maintenance management solution with local file management.</p>"
        "<br>"
        "<p><b>New Features:</b></p>"
        "<ul>"
        "<li>Local file browser with tab interface</li>"
        "<li>Multiple file viewing support</li>"
        "<li>Drag and drop tab reordering</li>"
        "<li>Syntax highlighting for code files</li>"
        "<li>Real-time file tree refresh</li>"
        "</ul>"
        "<br>"
        "<p><b>How to use:</b></p>"
        "<p>• Use the tree view on the left to navigate local files<br>"
        "• Click on files to open them in tabs<br>"
        "• Close tabs using the X button<br>"
        "• Use toolbar buttons to refresh and manage the tree view</p>"
        "<br>"
        "<p>© 2025 Way2Repair Systems. All rights reserved.</p>"
    );
}

void MainApplication::onLogoutClicked()
{
    QMessageBox::StandardButton reply = QMessageBox::question(this, 
        "Logout Confirmation",
        "Are you sure you want to logout?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Close all PDF tabs first
        while (m_tabWidget->count(DualTabWidget::PDF_TAB) > 0) {
            m_tabWidget->removeTab(0, DualTabWidget::PDF_TAB);
        }
        
        // Close all PCB tabs
        while (m_tabWidget->count(DualTabWidget::PCB_TAB) > 0) {
            m_tabWidget->removeTab(0, DualTabWidget::PCB_TAB);
        }
        
        // Emit logout signal
        emit logoutRequested();
        
        // Close the application
        this->close();
    }
}

void MainApplication::updateUserInfo()
{
    // Update window title with user info
    setWindowTitle(QString("Way2Repair - Equipment Maintenance System - %1").arg(m_userSession.fullName));
}

void MainApplication::toggleTreeView()
{
    setTreeViewVisible(!m_treeViewVisible);
}

void MainApplication::toggleFullScreenPDF()
{
    if (!m_tabWidget) {
        statusBar()->showMessage("No tabs available");
        return;
    }
    // Check which tab type is currently active and get the current widget
    DualTabWidget::TabType currentType = m_tabWidget->getCurrentTabType();
    QWidget *currentWidget = nullptr;
    
    if (currentType == DualTabWidget::PDF_TAB) {
        int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
        if (currentIndex >= 0) {
            currentWidget = m_tabWidget->widget(currentIndex, DualTabWidget::PDF_TAB);
        }
    } else if (currentType == DualTabWidget::PCB_TAB) {
        int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
        if (currentIndex >= 0) {
            currentWidget = m_tabWidget->widget(currentIndex, DualTabWidget::PCB_TAB);
        }
    }
    
    PDFViewerWidget *pdfViewer = qobject_cast<PDFViewerWidget*>(currentWidget);
    
    if (pdfViewer) {
        // It's a PDF tab - toggle full screen mode
        if (m_treeViewVisible) {
            setTreeViewVisible(false);
            statusBar()->showMessage("PDF in full screen mode - Press F11 or Ctrl+T to restore tree view");
        } else {
            setTreeViewVisible(true);
            statusBar()->showMessage("PDF in normal mode - Tree view restored");
        }
    } else {
        // Not a PDF tab, just hide tree view for full screen experience
        setTreeViewVisible(false);
        statusBar()->showMessage("Full screen mode - Press F11 or Ctrl+T to restore tree view");
    }
}

void MainApplication::setTreeViewVisible(bool visible)
{
    if (m_treeViewVisible == visible) {
        return; // Already in desired state
    }
    
    m_treeViewVisible = visible;
    
    // Defensive: splitter must exist before we try to resize panes
    if (!m_splitter) {
        if (visible) {
            if (m_treePanel) m_treePanel->show();
        } else {
            if (m_treePanel) m_treePanel->hide();
        }
        return;
    }

    if (visible) {
        // Show tree view - restore original sizes
        if (m_treePanel) m_treePanel->show();
        if (!m_splitterSizes.isEmpty()) {
            m_splitter->setSizes(m_splitterSizes);
        }
        statusBar()->showMessage("Tree view shown");
    } else {
        // Hide tree view - save current sizes and collapse completely
        m_splitterSizes = m_splitter->sizes();
        if (m_treePanel) m_treePanel->hide();
        
        // Force all space to the tab widget (PDF viewer)
        QList<int> fullScreenSizes;
        fullScreenSizes << 0 << this->width(); // Give all width to tab widget
        m_splitter->setSizes(fullScreenSizes);
        
        statusBar()->showMessage("Tree view hidden - Full screen PDF mode");
    }
    
    // Update menu action state if it exists
    QList<QAction*> actions = menuBar()->actions();
    for (QAction *action : actions) {
        if (action->text().contains("View")) {
            QMenu *viewMenu = action->menu();
            if (viewMenu) {
                QList<QAction*> viewActions = viewMenu->actions();
                for (QAction *viewAction : viewActions) {
                    if (viewAction->text().contains("Toggle Tree")) {
                        viewAction->setChecked(visible);
                        break;
                    }
                }
            }
            break;
        }
    }
}

bool MainApplication::isTreeViewVisible() const
{
    return m_treeViewVisible;
}

void MainApplication::setupKeyboardShortcuts()
{
    // Tree view toggle shortcut
    QShortcut *toggleTreeShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
    connect(toggleTreeShortcut, &QShortcut::activated, this, &MainApplication::toggleTreeView);
    
    // PDF full screen shortcut
    QShortcut *fullScreenShortcut = new QShortcut(QKeySequence("F11"), this);
    connect(fullScreenShortcut, &QShortcut::activated, this, &MainApplication::toggleFullScreenPDF);
    
    // Quick tree view restore (for when in PDF full screen)
    QShortcut *showTreeShortcut = new QShortcut(QKeySequence("Ctrl+Shift+T"), this);
    connect(showTreeShortcut, &QShortcut::activated, this, [this]() {
        setTreeViewVisible(true);
    });
    
    // Tab navigation shortcuts
    QShortcut *nextTabShortcut = new QShortcut(QKeySequence("Ctrl+Tab"), this);
    connect(nextTabShortcut, &QShortcut::activated, this, [this]() {
        DualTabWidget::TabType currentType = m_tabWidget->getCurrentTabType();
        
        if (currentType == DualTabWidget::PDF_TAB && m_tabWidget->count(DualTabWidget::PDF_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
            int nextIndex = (currentIndex + 1) % m_tabWidget->count(DualTabWidget::PDF_TAB);
            m_tabWidget->setCurrentIndex(nextIndex, DualTabWidget::PDF_TAB);
        } else if (currentType == DualTabWidget::PCB_TAB && m_tabWidget->count(DualTabWidget::PCB_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
            int nextIndex = (currentIndex + 1) % m_tabWidget->count(DualTabWidget::PCB_TAB);
            m_tabWidget->setCurrentIndex(nextIndex, DualTabWidget::PCB_TAB);
        }
    });
    
    QShortcut *prevTabShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
    connect(prevTabShortcut, &QShortcut::activated, this, [this]() {
        DualTabWidget::TabType currentType = m_tabWidget->getCurrentTabType();
        
        if (currentType == DualTabWidget::PDF_TAB && m_tabWidget->count(DualTabWidget::PDF_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
            int prevIndex = (currentIndex - 1 + m_tabWidget->count(DualTabWidget::PDF_TAB)) % m_tabWidget->count(DualTabWidget::PDF_TAB);
            m_tabWidget->setCurrentIndex(prevIndex, DualTabWidget::PDF_TAB);
        } else if (currentType == DualTabWidget::PCB_TAB && m_tabWidget->count(DualTabWidget::PCB_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
            int prevIndex = (currentIndex - 1 + m_tabWidget->count(DualTabWidget::PCB_TAB)) % m_tabWidget->count(DualTabWidget::PCB_TAB);
            m_tabWidget->setCurrentIndex(prevIndex, DualTabWidget::PCB_TAB);
        }
    });

    // Focus tree search on demand
    QShortcut *focusSearchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(focusSearchShortcut, &QShortcut::activated, this, [this]() {
        if (m_treeSearchEdit) {
            m_treeSearchEdit->setFocus();
            m_treeSearchEdit->selectAll();
        }
    });
}
