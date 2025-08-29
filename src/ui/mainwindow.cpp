#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "ui/mainapplication.h"
#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QIcon>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QCheckBox>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// Append a timestamped line to build/tab_debug.txt (next to the exe)
inline void writeTransitionLog(const QString &msg) {
    const QString logPath = QCoreApplication::applicationDirPath() + "/tab_debug.txt";
    QFile f(logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
           << " [login] " << msg << '\n';
    }
}

// Centralized settings accessor using an INI file in AppData for reliability
inline QSettings appSettings() {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return QSettings(appData + "/settings.ini", QSettings::IniFormat);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_dbManager(new DatabaseManager(this))
    , m_mainApp(nullptr)
    , m_dragging(false)
{
    ui->setupUi(this);
    setupLoginConnections();
    setupDatabaseConnection();
    writeTransitionLog("Login window initialized");
    
    // Set window properties for compact dialog
    setWindowTitle("Way2Repair - Login System");
    setFixedSize(580, 380);  // Compact size matching the screenshot
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // Make outside background transparent so only the white card is visible
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet("background: transparent;");
    if (ui->centralwidget) {
        ui->centralwidget->setAutoFillBackground(false);
        ui->centralwidget->setStyleSheet("background: transparent;");
    }

    // Elevation: subtle drop shadow around the white card for premium feel
    if (ui->loginContainer) {
        auto *shadow = new QGraphicsDropShadowEffect(ui->loginContainer);
        shadow->setBlurRadius(28);
        shadow->setOffset(0, 12);
        shadow->setColor(QColor(17, 24, 39, 45)); // #111827 @ ~18% opacity
        ui->loginContainer->setGraphicsEffect(shadow);
    }
    
    // Center the window on the active screen
    centerOnScreen();
    
    // Remove menu bar and status bar for clean dialog look
    menuBar()->hide();
    statusBar()->hide();
    
    // Initially disable login until database is connected
    enableLoginControls(false);
    
    // Set focus to username field
    ui->usernameLineEdit->setFocus();

    // Ensure login window shows the application icon (taskbar, alt-tab, etc.)
    {
        QIcon appIcon;
        const QString svgPath = ":/icons/images/icons/Way2Repair_Logo.svg";
        if (QFile(svgPath).exists()) {
            const QList<QSize> sizes = { {16,16}, {20,20}, {24,24}, {32,32}, {40,40}, {48,48}, {64,64}, {96,96}, {128,128}, {256,256} };
            for (const auto &sz : sizes) appIcon.addFile(svgPath, sz);
            // Also set the logo inside the card
            if (ui->logoLabel) ui->logoLabel->setPixmap(appIcon.pixmap(36, 36));
        }
        if (!appIcon.isNull()) {
            setWindowIcon(appIcon);
            QApplication::setWindowIcon(appIcon);
        }
    }

    // Add an inline eye icon action inside the password field to toggle visibility
    if (ui->passwordLineEdit) {
        auto *toggleAction = new QAction(this);
        auto updateIcon = [this, toggleAction]() {
            const bool visible = ui->passwordLineEdit->echoMode() == QLineEdit::Normal;
            toggleAction->setIcon(QIcon(visible ? ":/icons/images/icons/eye_off.svg" : ":/icons/images/icons/eye.svg"));
            toggleAction->setToolTip(visible ? "Hide password" : "Show password");
        };
        QObject::connect(toggleAction, &QAction::triggered, this, [this, updateIcon]() mutable {
            auto mode = ui->passwordLineEdit->echoMode();
            ui->passwordLineEdit->setEchoMode(mode == QLineEdit::Password ? QLineEdit::Normal : QLineEdit::Password);
            updateIcon();
        });
        ui->passwordLineEdit->addAction(toggleAction, QLineEdit::TrailingPosition);
        updateIcon();
    }

    // Load any saved credentials BEFORE wiring handlers so programmatic changes don't overwrite values
    loadSavedCredentials();

    // Keep Remember me in sync: on toggle and while typing when enabled
    if (ui->savePasswordCheckBox) {
        QObject::connect(ui->savePasswordCheckBox, &QCheckBox::toggled, this, [this](bool on){
            auto s = appSettings();
            s.setValue("login/remember", on);
            if (on) {
                s.setValue("login/username", ui->usernameLineEdit ? ui->usernameLineEdit->text() : QString());
                s.setValue("login/password", ui->passwordLineEdit ? ui->passwordLineEdit->text() : QString());
                writeTransitionLog(QString("remember toggled=1 -> saved user_len=%1 pass_len=%2")
                                    .arg(ui->usernameLineEdit ? ui->usernameLineEdit->text().length() : 0)
                                    .arg(ui->passwordLineEdit ? ui->passwordLineEdit->text().length() : 0));
            } else {
                s.remove("login/username");
                s.remove("login/password");
                writeTransitionLog("remember toggled=0 -> cleared creds");
            }
            s.sync();
        });
        // Auto-save as user types when remember is checked
        auto saveIfRemembered = [this]() {
            if (!ui->savePasswordCheckBox->isChecked()) return;
            auto s = appSettings();
            s.setValue("login/username", ui->usernameLineEdit ? ui->usernameLineEdit->text() : QString());
            s.setValue("login/password", ui->passwordLineEdit ? ui->passwordLineEdit->text() : QString());
            writeTransitionLog(QString("autosave creds user_len=%1 pass_len=%2")
                                .arg(ui->usernameLineEdit ? ui->usernameLineEdit->text().length() : 0)
                                .arg(ui->passwordLineEdit ? ui->passwordLineEdit->text().length() : 0));
            s.sync();
        };
        QObject::connect(ui->usernameLineEdit, &QLineEdit::textChanged, this, saveIfRemembered);
        QObject::connect(ui->passwordLineEdit, &QLineEdit::textChanged, this, saveIfRemembered);
    }

    // (Loaded earlier to avoid signal side-effects)
}

MainWindow::~MainWindow()
{
    if (m_mainApp) {
        m_mainApp->deleteLater();
        m_mainApp = nullptr;
    }
    delete ui;
}

void MainWindow::setupLoginConnections()
{
    // Connect login button to slot
    connect(ui->loginButton, &QPushButton::clicked, this, &MainWindow::onLoginButtonClicked);
    
    // Connect Enter key press in password field to login
    connect(ui->passwordLineEdit, &QLineEdit::returnPressed, this, &MainWindow::onLoginButtonClicked);
    
    // Connect text change signals for validation
    connect(ui->usernameLineEdit, &QLineEdit::textChanged, this, &MainWindow::onUsernameChanged);
    connect(ui->passwordLineEdit, &QLineEdit::textChanged, this, &MainWindow::onPasswordChanged);
    
    // Connect close button with animated close
    connect(ui->closeButton, &QPushButton::clicked, this, [this]{ animateClose(); });
}

void MainWindow::setupDatabaseConnection()
{
    // Connect database manager signals
    connect(m_dbManager, &DatabaseManager::connectionStatusChanged, 
            this, &MainWindow::onDatabaseConnectionChanged);
    connect(m_dbManager, &DatabaseManager::errorOccurred, 
            this, &MainWindow::onDatabaseError);
    
    // Attempt to connect to WAMP MySQL database
    // Default WAMP settings: localhost, port 3306, user: root, no password
    bool connected = m_dbManager->connectToDatabase(
        "localhost",        // hostname
        "w2r_login",     // database name
        "root",            // username
        "",                // password (empty for default WAMP)
        3306               // port
    );
    
    if (!connected) {
        QMessageBox::warning(this, "Database Connection", 
            "Could not connect to MySQL database.\n\n"
            "Requirements:\n"
            "1. WAMP server running with MySQL service started\n"
            "2. Database 'login_system' created\n"
            "3. MySQL ODBC Driver installed\n"
            "   Download from: https://dev.mysql.com/downloads/connector/odbc/\n\n"
            "The application will continue in offline mode.");
    }
}

void MainWindow::onLoginButtonClicked()
{
    if (!validateInput()) {
        return;
    }
    
    QString username = ui->usernameLineEdit->text().trimmed();
    QString password = ui->passwordLineEdit->text();
    
    performLogin(username, password);
}

void MainWindow::onUsernameChanged()
{
    // Enable/disable login button based on input and database connection
    bool hasInput = !ui->usernameLineEdit->text().trimmed().isEmpty() && 
                    !ui->passwordLineEdit->text().isEmpty();
    bool canLogin = hasInput && (m_dbManager->isConnected() || true); // Allow offline mode
    ui->loginButton->setEnabled(canLogin);
}

void MainWindow::onPasswordChanged()
{
    // Enable/disable login button based on input and database connection
    bool hasInput = !ui->usernameLineEdit->text().trimmed().isEmpty() && 
                    !ui->passwordLineEdit->text().isEmpty();
    bool canLogin = hasInput && (m_dbManager->isConnected() || true); // Allow offline mode
    ui->loginButton->setEnabled(canLogin);
}

bool MainWindow::validateInput()
{
    QString username = ui->usernameLineEdit->text().trimmed();
    QString password = ui->passwordLineEdit->text();
    
    if (username.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please enter a username.");
        ui->usernameLineEdit->setFocus();
        return false;
    }
    
    if (password.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please enter a password.");
        ui->passwordLineEdit->setFocus();
        return false;
    }
    
    if (username.length() < 3) {
        QMessageBox::warning(this, "Invalid Input", "Username must be at least 3 characters long.");
        ui->usernameLineEdit->setFocus();
        return false;
    }
    
    if (password.length() < 4) {
        QMessageBox::warning(this, "Invalid Input", "Password must be at least 4 characters long.");
        ui->passwordLineEdit->setFocus();
        return false;
    }
    
    return true;
}

void MainWindow::performLogin(const QString &username, const QString &password)
{
    writeTransitionLog(QString("performLogin start: user='%1'").arg(username));
    // Disable login controls during authentication
    enableLoginControls(false);
    ui->loginButton->setText("Authenticating...");
    
    // Use database authentication if connected
    if (m_dbManager->isConnected()) {
        bool authenticated = m_dbManager->authenticateUser(username, password);
        
        if (authenticated) {
            // Get user information
            UserInfo userInfo = m_dbManager->getUserInfo(username);
            
            // Launch main application instead of showing message box
            // Persist remember choice if requested
            persistRememberChoice(username, password);
            launchMainApplication(username, userInfo);
            
            qDebug() << "Database login successful for user:" << username;
            return; // Exit early since we're launching the main app
            
        } else {
            QMessageBox::critical(this, "Login Failed", 
                "Invalid username or password.\n\n"
                "Please check your credentials and try again.\n\n"
                "Note: For testing, you can use:\n"
                "- admin / password\n"
                "- user / 1234");
            
            ui->passwordLineEdit->clear();
            ui->usernameLineEdit->setFocus();
        }
    } 
    else {
        // Fallback to hardcoded credentials if database is not available
        QMessageBox::warning(this, "Offline Mode", 
            "Database is not connected. Using offline authentication.");
        
        if ((username == "admin" && password == "password") || 
            (username == "user" && password == "1234")) {
            
            // Create offline user info
            UserInfo offlineUserInfo;
            offlineUserInfo.username = username;
            offlineUserInfo.fullName = (username == "admin") ? "Administrator" : "Regular User";
            offlineUserInfo.email = username + "@localhost.com";
            offlineUserInfo.isActive = true;
            
            // Launch main application in offline mode
            // Persist remember choice if requested
            persistRememberChoice(username, password);
            launchMainApplication(username, offlineUserInfo);
            return; // Exit early since we're launching the main app
        } else {
            QMessageBox::critical(this, "Login Failed", 
                "Invalid username or password.\n\n"
                "Offline mode credentials:\n"
                "- admin / password\n"
                "- user / 1234");
            
            ui->passwordLineEdit->clear();
            ui->usernameLineEdit->setFocus();
        }
    }
    
    // Re-enable login controls
    ui->loginButton->setText("Login");
    enableLoginControls(true);
}

void MainWindow::onDatabaseConnectionChanged(bool connected)
{
    showConnectionStatus(connected);
    enableLoginControls(connected);
    
    if (connected) {
        qDebug() << "Database connected successfully";
        // Update window title to show connection status
        setWindowTitle("XinZhiZao - Login System (Connected)");
    } else {
        qDebug() << "Database disconnected";
        setWindowTitle("XinZhiZao - Login System (Offline)");
    }
}

void MainWindow::onDatabaseError(const QString &error)
{
    qDebug() << "Database error:" << error;
    
    // Only show critical errors to user, log others
    if (error.contains("Access denied") || error.contains("Connection refused")) {
        QMessageBox::critical(this, "Database Error", 
            QString("Database connection error:\n%1\n\n"
                   "Please check your WAMP server configuration.").arg(error));
    }
}

void MainWindow::showConnectionStatus(bool connected)
{
    // You could add a status indicator to the UI here
    // For now, we'll just use the window title
    QString status = connected ? "Connected" : "Offline";
    qDebug() << "Connection status:" << status;
}

void MainWindow::enableLoginControls(bool enabled)
{
    // Enable/disable login controls based on connection status
    ui->loginButton->setEnabled(enabled && !ui->usernameLineEdit->text().trimmed().isEmpty() && 
                                          !ui->passwordLineEdit->text().isEmpty());
    
    // Always keep input fields enabled for offline mode
    ui->usernameLineEdit->setEnabled(true);
    ui->passwordLineEdit->setEnabled(true);
}

void MainWindow::launchMainApplication(const QString &username, const UserInfo &userInfo)
{
    // Create user session
    UserSession session;
    session.username = username;
    session.fullName = userInfo.fullName.isEmpty() ? username : userInfo.fullName;
    session.email = userInfo.email;
    session.loginTime = QDateTime::currentDateTime();
    
    // Create and show main application
    writeTransitionLog("launchMainApplication: constructing MainApplication");
    m_mainApp = new MainApplication(session, nullptr);
    writeTransitionLog("launchMainApplication: MainApplication constructed");
    
    // Connect logout signal to close main app and show login again
    connect(m_mainApp, &MainApplication::logoutRequested, this, &MainWindow::closeLoginWindow);
    writeTransitionLog("launchMainApplication: logoutRequested signal connected");
    
    // Show main application using custom frameless maximize
    m_mainApp->show();
    m_mainApp->maximizeWindow();
    // Smooth entrance animation
    m_mainApp->animateEnter();
    writeTransitionLog("launchMainApplication: MainApplication shown (custom maximized)");
    
    // Hide login window
    this->hide();
    writeTransitionLog("launchMainApplication: login window hidden");
    
    qDebug() << "Main application launched for user:" << username;
}

void MainWindow::closeLoginWindow()
{
    // This slot is called when user logs out from main application
    if (m_mainApp) {
        m_mainApp->deleteLater();
        m_mainApp = nullptr;
    }
    
    // Clear then reload any remembered credentials, and show login window again
    ui->usernameLineEdit->clear();
    ui->passwordLineEdit->clear();
    loadSavedCredentials();
    ui->usernameLineEdit->setFocus();
    
    // Show login window again
    this->show();
    this->raise();
    this->activateWindow();
    
    qDebug() << "User logged out, login window restored";
}

// Mouse event handlers for frameless window dragging
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        // ESC closes with animation
        animateClose();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closingNow) { event->accept(); return; }
    event->ignore();
    animateClose();
}

void MainWindow::animateMinimize()
{
    if (isMinimized()) { showMinimized(); return; }
    if (m_minimizeAnim) { m_minimizeAnim->stop(); m_minimizeAnim->deleteLater(); m_minimizeAnim = nullptr; }

    // Use the ghost approach defined in mainapplication.cpp anonymous namespace
    const QRect startFrame = frameGeometry();
    const QPixmap snapshot = this->grab();

    class Ghost : public QWidget {
    public:
        explicit Ghost(const QPixmap &px)
            : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint)
        {
            setAttribute(Qt::WA_TranslucentBackground, true);
            setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_label = new QLabel(this);
            m_label->setScaledContents(true);
            m_label->setPixmap(px);
            m_label->setGeometry(rect());
        }
    protected:
        void resizeEvent(QResizeEvent*) override { if (m_label) m_label->setGeometry(rect()); }
    private:
        QLabel *m_label { nullptr };
    };

    auto *ghost = new Ghost(snapshot);
    ghost->setGeometry(startFrame);
    ghost->show();
    this->hide();

    auto taskbarRectForPoint = [](const QPoint &pt)->QRect {
#ifdef Q_OS_WIN
        HWND hTaskbar = FindWindow(L"Shell_TrayWnd", NULL);
        if (hTaskbar) { RECT r; GetWindowRect(hTaskbar, &r); return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top); }
#endif
        QScreen *scr = QGuiApplication::screenAt(pt); if (!scr) scr = QGuiApplication::primaryScreen();
        QRect g = scr ? scr->availableGeometry() : QRect(0,0,1280,720);
        return QRect(g.left(), g.bottom() - 56, g.width(), 56);
    };

    const QRect taskbar = taskbarRectForPoint(startFrame.center());
    const bool horizontal = taskbar.width() >= taskbar.height();
    const int targetW = std::max(120, startFrame.width() / 8);
    const int targetH = std::max(80,  startFrame.height() / 8);
    QRect target(QPoint(0,0), QSize(targetW, targetH));
    QPoint targetCenter;
    if (horizontal) {
        const bool bottomBar = taskbar.center().y() >= startFrame.center().y();
        int y = bottomBar ? taskbar.bottom() - targetH/2 - 6 : taskbar.top() + targetH/2 + 6;
        int x = taskbar.left() + std::min(200, taskbar.width()/4);
        targetCenter = QPoint(x, y);
    } else {
        const bool rightBar = taskbar.center().x() >= startFrame.center().x();
        int x = rightBar ? taskbar.right() - targetW/2 - 6 : taskbar.left() + targetW/2 + 6;
        int y = taskbar.top() + taskbar.height()/2;
        targetCenter = QPoint(x, y);
    }
    target.moveCenter(targetCenter);

    auto *geoAnim = new QPropertyAnimation(ghost, "geometry", ghost);
    geoAnim->setDuration(200);
    geoAnim->setStartValue(startFrame);
    geoAnim->setEndValue(target);
    geoAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(geoAnim, &QPropertyAnimation::finished, this, [this, ghost]() {
        ghost->deleteLater();
        setWindowState(windowState() | Qt::WindowMinimized);
        QTimer::singleShot(0, this, [this]{ showMinimized(); });
    });
    geoAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::animateClose()
{
    if (m_closeAnim) { m_closeAnim->stop(); m_closeAnim->deleteLater(); }
    auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(140);
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);

    QRect startG = geometry();
    QRect endG = startG;
    endG.setWidth(int(startG.width() * 0.985));
    endG.setHeight(int(startG.height() * 0.985));
    endG.moveCenter(startG.center());
    auto *scale = new QPropertyAnimation(this, "geometry", this);
    scale->setDuration(140);
    scale->setStartValue(startG);
    scale->setEndValue(endG);
    scale->setEasingCurve(QEasingCurve::OutCubic);

    m_closeAnim = new QParallelAnimationGroup(this);
    m_closeAnim->addAnimation(fade);
    m_closeAnim->addAnimation(scale);
    connect(m_closeAnim, &QParallelAnimationGroup::finished, this, [this]{
        m_closingNow = true;
        close();
        qApp->quit();
    });
    m_closeAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::centerOnScreen()
{
    // Prefer screen where cursor is; fall back to primary
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0,0,1280,720);
    const QPoint center = avail.center();
    move(center.x() - width()/2, center.y() - height()/2);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_firstShowDone) {
        m_firstShowDone = true;
        // Ensure correct centering in case of DPI/layout changes
        centerOnScreen();
        animateShow();
    }
}

void MainWindow::animateShow()
{
    if (m_showAnim) { m_showAnim->stop(); m_showAnim->deleteLater(); }

    setWindowOpacity(0.0);
    QRect startG = geometry();
    QRect endG = startG;
    // Slight pop-in scale from 96% to 100%
    startG.setWidth(int(endG.width() * 0.96));
    startG.setHeight(int(endG.height() * 0.96));
    startG.moveCenter(endG.center());
    setGeometry(startG);

    auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(180);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);

    auto *scale = new QPropertyAnimation(this, "geometry", this);
    scale->setDuration(180);
    scale->setStartValue(startG);
    scale->setEndValue(endG);
    scale->setEasingCurve(QEasingCurve::OutCubic);

    m_showAnim = new QParallelAnimationGroup(this);
    m_showAnim->addAnimation(fade);
    m_showAnim->addAnimation(scale);
    connect(m_showAnim, &QParallelAnimationGroup::finished, this, [this]{
        if (m_showAnim) { m_showAnim->deleteLater(); m_showAnim = nullptr; }
    });
    m_showAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// --- Remember me persistence ---
void MainWindow::loadSavedCredentials()
{
    auto s = appSettings();
    // Log where settings.ini lives to aid troubleshooting
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    writeTransitionLog(QString("settings path: %1/settings.ini").arg(appData));
    const bool remember = s.value("login/remember", false).toBool();
    if (ui->savePasswordCheckBox) {
        QSignalBlocker b(ui->savePasswordCheckBox);
        ui->savePasswordCheckBox->setChecked(remember);
    }

    if (remember) {
        const QString savedUser = s.value("login/username").toString();
        const QString savedPass = s.value("login/password").toString();
        writeTransitionLog(QString("loadSavedCredentials remember=1 user='%1' pass_len=%2").arg(savedUser).arg(savedPass.length()));
        if (ui->usernameLineEdit) { QSignalBlocker b(ui->usernameLineEdit); ui->usernameLineEdit->setText(savedUser); }
        if (ui->passwordLineEdit) { QSignalBlocker b(ui->passwordLineEdit); ui->passwordLineEdit->setText(savedPass); }
        onUsernameChanged();
        onPasswordChanged();
    } else {
        writeTransitionLog("loadSavedCredentials remember=0");
    }
}

void MainWindow::persistRememberChoice(const QString &username, const QString &password)
{
    if (!ui->savePasswordCheckBox) return;
    const bool remember = ui->savePasswordCheckBox->isChecked();
    auto s = appSettings();
    s.setValue("login/remember", remember);
    if (remember) {
        s.setValue("login/username", username);
        s.setValue("login/password", password);
    writeTransitionLog(QString("persistRememberChoice saved user='%1' pass_len=%2").arg(username).arg(password.length()));
    } else {
        s.remove("login/username");
        s.remove("login/password");
    writeTransitionLog("persistRememberChoice cleared creds");
    }
    s.sync();
}
