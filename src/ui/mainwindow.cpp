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
#include "core/memoryfilemanager.h"
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
    setupStaticLightTheme();
    setupLoginConnections();
    // Disable old MySQL path; we'll use Node.js auth service
    // setupDatabaseConnection();
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
                // Do NOT store plaintext password
                s.remove("login/password");
                writeTransitionLog(QString("remember toggled=1 -> saved user_len=%1 (password not stored)")
                                    .arg(ui->usernameLineEdit ? ui->usernameLineEdit->text().length() : 0));
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
            // Do NOT store plaintext password
            s.remove("login/password");
            writeTransitionLog(QString("autosave creds user_len=%1 (password not stored)")
                                .arg(ui->usernameLineEdit ? ui->usernameLineEdit->text().length() : 0));
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

void MainWindow::setupStaticLightTheme()
{
    // Apply a clean, static light theme to the entire application
    const QString lightTheme = R"(
        QMainWindow {
            background-color: #ffffff;
            color: #333333;
        }
        
        QWidget {
            background-color: #ffffff;
            color: #333333;
            font-family: 'Segoe UI', Arial, sans-serif;
            font-size: 9pt;
        }
        
        QPushButton {
            background-color: #007acc;
            color: #ffffff;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-weight: 500;
            min-height: 20px;
        }
        
        QPushButton:hover {
            background-color: #005a9e;
        }
        
        QPushButton:pressed {
            background-color: #004578;
        }
        
        QPushButton:disabled {
            background-color: #cccccc;
            color: #666666;
        }
        
        QLineEdit {
            background-color: #ffffff;
            border: 2px solid #e1e1e1;
            border-radius: 6px;
            padding: 8px 12px;
            color: #333333;
            selection-background-color: #007acc;
            selection-color: #ffffff;
        }
        
        QLineEdit:focus {
            border-color: #007acc;
            outline: none;
        }
        
        QLineEdit:hover {
            border-color: #b3b3b3;
        }
        
        QCheckBox {
            color: #333333;
            spacing: 8px;
        }
        
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 2px solid #e1e1e1;
            border-radius: 3px;
            background-color: #ffffff;
        }
        
        QCheckBox::indicator:hover {
            border-color: #007acc;
        }
        
        QCheckBox::indicator:checked {
            background-color: #007acc;
            border-color: #007acc;
            image: url(:/icons/images/icons/check.svg);
        }
        
        QLabel {
            color: #333333;
            background-color: transparent;
        }
        
        QMessageBox {
            background-color: #ffffff;
            color: #333333;
            border: 1px solid #e1e1e1;
        }
        
        QMessageBox QLabel {
            color: #333333;
            background-color: #ffffff;
        }
        
        QMessageBox QPushButton {
            background-color: #007acc;
            color: #ffffff;
            border: none;
            border-radius: 6px;
            min-width: 80px;
            padding: 8px 16px;
            font-weight: 500;
        }
        
        QMessageBox QPushButton:hover {
            background-color: #005a9e;
        }
        
        QMessageBox QPushButton:pressed {
            background-color: #004578;
        }
        
        QDialog {
            background-color: #ffffff;
            color: #333333;
            border: 1px solid #e1e1e1;
        }
        
        QDialog QLabel {
            color: #333333;
            background-color: #ffffff;
        }
        
        /* Login container specific styling */
        #loginContainer {
            background-color: #ffffff;
            border: 1px solid #e1e1e1;
            border-radius: 8px;
        }
        
        #closeButton {
            background-color: transparent;
            border: none;
            color: #666666;
            font-size: 16px;
            font-weight: bold;
            padding: 4px;
            border-radius: 4px;
        }
        
        #closeButton:hover {
            background-color: #f0f0f0;
            color: #333333;
        }
    )";
    
    // Apply the theme to the entire application
    this->setStyleSheet(lightTheme);
    qApp->setStyleSheet(lightTheme);
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
    
    // Connect AuthService login finished signal (once during initialization)
    connect(&m_auth, &AuthService::loginFinished, this, &MainWindow::onAuthLoginFinished);
}

void MainWindow::setupDatabaseConnection()
{
    // No-op: legacy DB disabled; enable login controls immediately
    onDatabaseConnectionChanged(true);
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

void MainWindow::showStyledMessageBox(QMessageBox::Icon icon, const QString &title, const QString &message)
{
    const QString lightMessageBoxStyle = R"(
        QMessageBox {
            background-color: #ffffff;
            color: #333333;
            border: 1px solid #e1e1e1;
            border-radius: 8px;
        }
        QMessageBox QLabel {
            color: #333333;
            background-color: transparent;
            padding: 10px;
            font-size: 10pt;
        }
        QMessageBox QPushButton {
            background-color: #007acc;
            color: #ffffff;
            border: none;
            border-radius: 6px;
            min-width: 80px;
            padding: 8px 16px;
            font-weight: 500;
            font-size: 9pt;
        }
        QMessageBox QPushButton:hover {
            background-color: #005a9e;
        }
        QMessageBox QPushButton:pressed {
            background-color: #004578;
        }
    )";
    
    QMessageBox msgBox(icon, title, message, QMessageBox::Ok, this);
    msgBox.setStyleSheet(lightMessageBoxStyle);
    msgBox.exec();
}

bool MainWindow::validateInput()
{
    QString username = ui->usernameLineEdit->text().trimmed();
    QString password = ui->passwordLineEdit->text();
    
    // Only check for empty fields - let the server handle validation
    if (username.isEmpty()) {
        showStyledMessageBox(QMessageBox::Warning, "Invalid Input", "Please enter a username.");
        ui->usernameLineEdit->setFocus();
        return false;
    }
    
    if (password.isEmpty()) {
        showStyledMessageBox(QMessageBox::Warning, "Invalid Input", "Please enter a password.");
        ui->passwordLineEdit->setFocus();
        return false;
    }
    
    // Remove length restrictions - let Lambda function handle validation
    // This allows the request to reach the server for proper validation
    return true;
}

void MainWindow::performLogin(const QString &username, const QString &password)
{
    writeTransitionLog(QString("performLogin start: user='%1'").arg(username));
    // Disable login controls during authentication
    enableLoginControls(false);
    ui->loginButton->setText("Authenticating...");
    
    // Store current login attempt for the callback
    m_currentUsername = username;
    m_currentPassword = password;
    
    // Call Node.js auth service
    // Read base URL from settings, default to AWS API Gateway endpoint if not set
    {
        auto s = appSettings();
        const QString baseUrl = s.value("api/baseUrl", QStringLiteral("https://uoklh0m767.execute-api.us-east-1.amazonaws.com/dev")).toString();
        m_auth.setBaseUrl(baseUrl);
        writeTransitionLog(QString("AuthService baseUrl='%1'").arg(baseUrl));
    }
    
    m_auth.login(username, password);
}

void MainWindow::onAuthLoginFinished(bool success, const AuthResult& result, const QString& error)
{
    writeTransitionLog(QString("onAuthLoginFinished: success=%1, error='%2'").arg(success).arg(error));
    
    if (success) {
        writeTransitionLog("Login successful, launching main application");
        // Persist remember choice if requested (store only username locally; avoid storing password if possible)
        persistRememberChoice(m_currentUsername, m_currentPassword);
        // Build UserInfo
        UserInfo uiInfo; 
        uiInfo.username = result.user.username.isEmpty() ? m_currentUsername : result.user.username; 
        uiInfo.fullName = result.user.fullName; 
        uiInfo.email = result.user.email; 
        uiInfo.isActive = true;
        // Launch main app
        launchMainApplication(m_currentUsername, uiInfo);
        // Configure AWS for the main app using credentials from server
        if (m_mainApp) {
            writeTransitionLog("Configuring AWS credentials for main application");
            configureAwsForMain(m_mainApp, result.aws, m_auth.authToken());
        }
        return;
    }
    
    // Show appropriate error dialog based on error type
    writeTransitionLog(QString("Login failed, showing error dialog: %1").arg(error));
    QString title = "Login Failed";
    QString message = error.isEmpty() ? QStringLiteral("Authentication failed") : error;
    
    if (error.contains("Free Plan Access Restricted")) {
        showStyledMessageBox(QMessageBox::Information, "Upgrade Required", message);
    } else if (error.contains("Account Not Activated")) {
        showStyledMessageBox(QMessageBox::Warning, "Account Activation Required", message);
    } else if (error.contains("Premium Plan Expired")) {
        showStyledMessageBox(QMessageBox::Warning, "Subscription Expired", message);
    } else {
        showStyledMessageBox(QMessageBox::Critical, title, message);
    }
    
    ui->passwordLineEdit->clear();
    ui->usernameLineEdit->setFocus();
    ui->loginButton->setText("Login");
    enableLoginControls(true);
}

void MainWindow::onDatabaseConnectionChanged(bool connected)
{
    Q_UNUSED(connected)
    showConnectionStatus(true);
    enableLoginControls(true);
    setWindowTitle("Way2Repair - Login System");
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
    // Enable/disable login controls
    ui->loginButton->setEnabled(enabled && !ui->usernameLineEdit->text().trimmed().isEmpty() && !ui->passwordLineEdit->text().isEmpty());
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

void MainWindow::configureAwsForMain(MainApplication* app, const AuthAwsCreds& aws, const QString& authToken)
{
    if (!app) {
        writeTransitionLog("configureAwsForMain: app is null");
        return;
    }
    
    // Server-proxied model: we only need an auth token and a bucket.
    // Do NOT require direct AWS credentials (access/secret/region) anymore.
    if (!authToken.isEmpty() && !aws.bucket.isEmpty()) {
        writeTransitionLog(QString("configureAwsForMain: configuring AWS via server (bucket=%1)")
                          .arg(aws.bucket));

        // Pass bucket and token to MainApplication (it configures AwsClient in server mode)
        app->configureAwsFromAuth(aws, authToken);
        // Switch to AWS after UI is ready (single call)
        writeTransitionLog("configureAwsForMain: switching to AWS treeview");
        QTimer::singleShot(200, this, [app]() { app->switchToAwsTreeview(); });
    } else {
        writeTransitionLog("configureAwsForMain: missing auth token or bucket; waiting for AWS configuration (no switch)");
    }
}

void MainWindow::closeLoginWindow()
{
    // This slot is called when user logs out from main application
    if (m_mainApp) {
        m_mainApp->deleteLater();
        m_mainApp = nullptr;
    }

    // Free any in-memory files from the previous session
    MemoryFileManager::instance()->clearAll();
    
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
    writeTransitionLog(QString("loadSavedCredentials remember=1 user='%1' (password not stored)").arg(savedUser));
        if (ui->usernameLineEdit) { QSignalBlocker b(ui->usernameLineEdit); ui->usernameLineEdit->setText(savedUser); }
    if (ui->passwordLineEdit) { QSignalBlocker b(ui->passwordLineEdit); ui->passwordLineEdit->clear(); }
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
    // Do NOT store plaintext password
    s.remove("login/password");
    writeTransitionLog(QString("persistRememberChoice saved user='%1' (password not stored)").arg(username));
    } else {
        s.remove("login/username");
        s.remove("login/password");
    writeTransitionLog("persistRememberChoice cleared creds");
    }
    s.sync();
}
