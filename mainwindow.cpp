#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "mainapplication.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_dbManager(new DatabaseManager(this))
    , m_mainApp(nullptr)
{
    ui->setupUi(this);
    setupLoginConnections();
    setupDatabaseConnection();
    
    // Set window properties for compact dialog
    setWindowTitle("Way2Repair - Login System");
    setFixedSize(580, 380);  // Compact size matching the screenshot
    setWindowFlags(Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowCloseButtonHint);
    
    // Center the window on screen
    QWidget *parentWidget = qobject_cast<QWidget*>(parent);
    if (parentWidget) {
        move(parentWidget->geometry().center() - rect().center());
    } else {
        // Fallback - center on a common screen size
        move(640 - width()/2, 360 - height()/2);
    }
    
    // Remove menu bar and status bar for clean dialog look
    menuBar()->hide();
    statusBar()->hide();
    
    // Initially disable login until database is connected
    enableLoginControls(false);
    
    // Set focus to username field
    ui->usernameLineEdit->setFocus();
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
    m_mainApp = new MainApplication(session, nullptr);
    
    // Connect logout signal to close main app and show login again
    connect(m_mainApp, &MainApplication::logoutRequested, this, &MainWindow::closeLoginWindow);
    
    // Show main application maximized
    m_mainApp->showMaximized();
    
    // Hide login window
    this->hide();
    
    qDebug() << "Main application launched for user:" << username;
}

void MainWindow::closeLoginWindow()
{
    // This slot is called when user logs out from main application
    if (m_mainApp) {
        m_mainApp->deleteLater();
        m_mainApp = nullptr;
    }
    
    // Clear login form and show login window again
    ui->usernameLineEdit->clear();
    ui->passwordLineEdit->clear();
    ui->usernameLineEdit->setFocus();
    
    // Show login window again
    this->show();
    this->raise();
    this->activateWindow();
    
    qDebug() << "User logged out, login window restored";
}
