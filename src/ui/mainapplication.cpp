#include "ui/mainapplication.h"
#include <QApplication>
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

MainApplication::MainApplication(const UserSession &userSession, QWidget *parent)
    : QMainWindow(parent)
    , m_userSession(userSession)
    , m_dbManager(new DatabaseManager(this))
    , m_rootFolderPath(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))  // Set to desktop folder
    // Server-side initialization (commented out for local file loading)
    //, m_networkManager(new QNetworkAccessManager(this))
    //, m_baseUrl("http://localhost/api") // WAMP server API endpoint
{
    setupUI();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    updateUserInfo();
    
    // Set window properties
    setWindowTitle("Way2Repair - Equipment Maintenance System");
    setMinimumSize(1200, 800);
    resize(1400, 900);
    
    // Center the window
    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - width()) / 2;
    int y = (screenGeometry.height() - height()) / 2;
    move(x, y);
    
    // Set application icon (you can add an icon file later)
    setWindowIcon(QIcon(":/icons/app_icon.png"));
    
    // Load initial file list from local folder
    loadLocalFiles();
    
    // Add welcome tab
    addWelcomeTab();
}

MainApplication::~MainApplication()
{
    // Clean up
}

void MainApplication::setupUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    // Create main layout
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    
    // Create splitter for tree view and tab widget
    m_splitter = new QSplitter(Qt::Horizontal, this);
    
    // Setup tree view and tab widget
    setupTreeView();
    setupTabWidget();
    
    // Add to splitter
    m_splitter->addWidget(m_treeWidget);
    m_splitter->addWidget(m_tabWidget);
    
    // Set splitter proportions (tree view: 25%, tabs: 75%)
    m_splitter->setSizes({300, 900});
    m_splitter->setCollapsible(0, false); // Don't allow tree view to collapse
    
    mainLayout->addWidget(m_splitter);
}

void MainApplication::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();
    menuBar->setStyleSheet(
        "QMenuBar {"
        "    background-color: #f8f9ff;"
        "    color: #2c3e50;"
        "    border-bottom: 1px solid #d4e1f5;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "}"
        "QMenuBar::item {"
        "    padding: 6px 12px;"
        "    background: transparent;"
        "}"
        "QMenuBar::item:selected {"
        "    background-color: #4285f4;"
        "    color: white;"
        "    border-radius: 3px;"
        "}"
    );
    
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
    viewMenu->addAction("&Refresh Tree", this, &MainApplication::loadLocalFiles);
    viewMenu->addAction("&Expand All", this, [this]() { m_treeWidget->expandAll(); });
    viewMenu->addAction("&Collapse All", this, [this]() { m_treeWidget->collapseAll(); });
    
    // Help menu
    QMenu *helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("&About", this, &MainApplication::onAboutClicked);
}

void MainApplication::setupToolBar()
{
    m_toolbar = addToolBar("Main Toolbar");
    m_toolbar->setStyleSheet(
        "QToolBar {"
        "    background-color: #f8f9ff;"
        "    border: 1px solid #d4e1f5;"
        "    spacing: 3px;"
        "}"
        "QToolButton {"
        "    background-color: transparent;"
        "    border: 1px solid transparent;"
        "    border-radius: 4px;"
        "    padding: 6px;"
        "    margin: 2px;"
        "    color: #2c3e50;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "}"
        "QToolButton:hover {"
        "    background-color: #e8f0fe;"
        "    border-color: #4285f4;"
        "}"
        "QToolButton:pressed {"
        "    background-color: #4285f4;"
        "    color: white;"
        "}"
    );
    
    // Add toolbar actions
    QAction *refreshAction = m_toolbar->addAction("Refresh");
    connect(refreshAction, &QAction::triggered, this, &MainApplication::loadLocalFiles);
    
    QAction *expandAction = m_toolbar->addAction("Expand All");
    connect(expandAction, &QAction::triggered, this, [this]() { m_treeWidget->expandAll(); });
    
    QAction *collapseAction = m_toolbar->addAction("Collapse All");
    connect(collapseAction, &QAction::triggered, this, [this]() { m_treeWidget->collapseAll(); });
    
    m_toolbar->addSeparator();
    
    QAction *logoutAction = m_toolbar->addAction("Logout");
    connect(logoutAction, &QAction::triggered, this, &MainApplication::onLogoutClicked);
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
    
    // Add permanent widgets to status bar
    QLabel *userLabel = new QLabel(QString("Logged in as: %1").arg(m_userSession.fullName), this);
    m_statusBar->addPermanentWidget(userLabel);
    
    QLabel *timeLabel = new QLabel(QString("Session started: %1").arg(m_userSession.loginTime.toString("hh:mm:ss")), this);
    m_statusBar->addPermanentWidget(timeLabel);
    
    m_statusBar->showMessage("Ready");
}

void MainApplication::setupTreeView()
{
    m_treeWidget = new QTreeWidget();
    m_treeWidget->setHeaderLabel("Files & Folders");
    m_treeWidget->setMinimumWidth(250);
    m_treeWidget->setMaximumWidth(400);
    
    // Style the tree widget
    m_treeWidget->setStyleSheet(
        "QTreeWidget {"
        "    border: 1px solid #d4e1f5;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    selection-background-color: #e8f0fe;"
        "    selection-color: #2c3e50;"
        "}"
        "QTreeWidget::item {"
        "    padding: 4px;"
        "    border: none;"
        "}"
        "QTreeWidget::item:selected {"
        "    background-color: #4285f4;"
        "    color: white;"
        "}"
        "QTreeWidget::item:hover {"
        "    background-color: #f0f7ff;"
        "}"
        "QTreeWidget::branch:has-siblings:!adjoins-item {"
        "    border-image: url(vline.png) 0;"
        "}"
        "QTreeWidget::branch:has-siblings:adjoins-item {"
        "    border-image: url(branch-more.png) 0;"
        "}"
        "QTreeWidget::branch:!has-children:!has-siblings:adjoins-item {"
        "    border-image: url(branch-end.png) 0;"
        "}"
        "QTreeWidget::branch:has-children:!has-siblings:closed,"
        "QTreeWidget::branch:closed:has-children:has-siblings {"
        "    border-image: none;"
        "    image: url(branch-closed.png);"
        "}"
        "QTreeWidget::branch:open:has-children:!has-siblings,"
        "QTreeWidget::branch:open:has-children:has-siblings {"
        "    border-image: none;"
        "    image: url(branch-open.png);"
        "}"
    );
    
    // Connect signals
    connect(m_treeWidget, &QTreeWidget::itemClicked, this, &MainApplication::onTreeItemClicked);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &MainApplication::onTreeItemDoubleClicked);
    
    // Tree will be populated via HTTP request in constructor
}

void MainApplication::setupTabWidget()
{
    m_tabWidget = new QTabWidget();
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setStyleSheet(
        "QTabWidget {"
        "    border: 1px solid #d4e1f5;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #d4e1f5;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
        "QTabWidget::tab-bar {"
        "    alignment: left;"
        "}"
        "QTabBar::tab {"
        "    background-color: #f8f9ff;"
        "    border: 1px solid #d4e1f5;"
        "    border-bottom: none;"
        "    border-radius: 4px 4px 0 0;"
        "    padding: 8px 12px;"
        "    margin-right: 2px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    color: #2c3e50;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: white;"
        "    border-color: #4285f4;"
        "    color: #4285f4;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: #e8f0fe;"
        "}"
        "QTabBar::close-button {"
        "    image: url(:/icons/close.png);"
        "    subcontrol-position: right;"
        "}"
        "QTabBar::close-button:hover {"
        "    background-color: #ff6b6b;"
        "    border-radius: 2px;"
        "}"
    );
    
    // Connect tab close signal
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainApplication::onTabCloseRequested);
}

void MainApplication::loadLocalFiles()
{
    statusBar()->showMessage("Loading files from local directory...");
    
    // Clear existing tree items
    m_treeWidget->clear();
    
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
    
    // Expand first level by default
    m_treeWidget->expandToDepth(0);
    
    statusBar()->showMessage(QString("Loaded files from: %1").arg(m_rootFolderPath));
}

void MainApplication::populateTreeFromDirectory(const QString &dirPath, QTreeWidgetItem *parentItem)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }
    
    // Get all entries (files and directories)
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    
    for (const QFileInfo &entry : entries) {
        QTreeWidgetItem *item;
        
        if (parentItem) {
            item = new QTreeWidgetItem(parentItem);
        } else {
            item = new QTreeWidgetItem(m_treeWidget);
        }
        
        item->setText(0, entry.fileName());
        
        if (entry.isDir()) {
            // This is a directory
            item->setIcon(0, QIcon(":/icons/folder.png"));
            item->setData(0, Qt::UserRole + 1, entry.absoluteFilePath()); // Store folder path
            
            // Recursively populate subdirectory
            populateTreeFromDirectory(entry.absoluteFilePath(), item);
        } else {
            // This is a file
            item->setIcon(0, QIcon(":/icons/file.png"));
            item->setData(0, Qt::UserRole, entry.absoluteFilePath()); // Store file path
            
            // Add tooltip with file info
            QString tooltip = QString("File: %1\nSize: %2 bytes\nModified: %3")
                                .arg(entry.fileName())
                                .arg(entry.size())
                                .arg(entry.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
            item->setToolTip(0, tooltip);
        }
    }
}

void MainApplication::openFileInTab(const QString &filePath)
{
    // Check if file is already open in a tab
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QWidget *tabWidget = m_tabWidget->widget(i);
        if (tabWidget->property("filePath").toString() == filePath) {
            // File is already open, just switch to that tab
            m_tabWidget->setCurrentIndex(i);
            statusBar()->showMessage("File already open in tab");
            return;
        }
    }
    
    // Read file content
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusBar()->showMessage("Error: Could not open file: " + filePath);
        QMessageBox::warning(this, "File Error", "Could not open file:\n" + filePath);
        return;
    }
    
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();
    
    // Create new tab content
    QTextEdit *textEdit = new QTextEdit();
    textEdit->setPlainText(content);
    textEdit->setReadOnly(true); // Make it read-only for now
    textEdit->setProperty("filePath", filePath);
    
    // Set appropriate font based on file type
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    
    if (extension == "cpp" || extension == "h" || extension == "c" || extension == "hpp" ||
        extension == "js" || extension == "py" || extension == "html" || extension == "css" ||
        extension == "json" || extension == "xml" || extension == "sql") {
        // Code files - use monospace font
        QFont codeFont("Consolas", 10);
        codeFont.setStyleHint(QFont::Monospace);
        textEdit->setFont(codeFont);
        textEdit->setLineWrapMode(QTextEdit::NoWrap);
    } else {
        // Text files - use default font
        QFont textFont("Segoe UI", 10);
        textEdit->setFont(textFont);
        textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    }
    
    // Style the text edit
    textEdit->setStyleSheet(
        "QTextEdit {"
        "    border: none;"
        "    background-color: white;"
        "    color: #2c3e50;"
        "    font-family: 'Consolas', monospace;"
        "    selection-background-color: #4285f4;"
        "    selection-color: white;"
        "}"
    );
    
    // Add tab with file name
    QString tabName = fileInfo.fileName();
    int tabIndex = m_tabWidget->addTab(textEdit, tabName);
    
    // Set tab tooltip
    QString tooltip = QString("File: %1\nPath: %2\nSize: %3 bytes")
                        .arg(fileInfo.fileName())
                        .arg(filePath)
                        .arg(fileInfo.size());
    m_tabWidget->setTabToolTip(tabIndex, tooltip);
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex);
    
    statusBar()->showMessage(QString("Opened file: %1").arg(fileInfo.fileName()));
}

void MainApplication::onTabCloseRequested(int index)
{
    if (index >= 0 && index < m_tabWidget->count()) {
        QWidget *tabWidget = m_tabWidget->widget(index);
        QString filePath = tabWidget->property("filePath").toString();
        
        // Remove the tab
        m_tabWidget->removeTab(index);
        
        // Update status bar
        if (!filePath.isEmpty()) {
            QFileInfo fileInfo(filePath);
            statusBar()->showMessage(QString("Closed file: %1").arg(fileInfo.fileName()));
        } else {
            statusBar()->showMessage("Closed tab");
        }
    }
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
    // Create welcome content
    QWidget *welcomeWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(welcomeWidget);
    
    // Welcome message
    QLabel *welcomeLabel = new QLabel(QString("Welcome to Way2Repair, %1!").arg(m_userSession.fullName));
    welcomeLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 24px;"
        "    font-weight: bold;"
        "    color: #2c3e50;"
        "    padding: 40px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "}"
    );
    welcomeLabel->setAlignment(Qt::AlignCenter);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(
        "Desktop File Browser is now active!\n\n"
        "Features:\n"
        "• Browse files from your Desktop folder\n"
        "• Click on files in the tree view to open them in tabs\n"
        "• Multiple files can be open simultaneously\n"
        "• Tabs are closable and movable\n"
        "• Syntax highlighting for code files\n"
        "• Real-time file tree refresh\n\n"
        "Instructions:\n"
        "• Select a file from the tree view on the left\n"
        "• File content will open in a new tab\n"
        "• Close tabs using the 'X' button\n"
        "• Expand/collapse folders in the tree view\n"
        "• Use toolbar buttons to refresh and manage the tree view\n\n"
        "Current folder: " + m_rootFolderPath
    );
    instructionLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 14px;"
        "    color: #666;"
        "    padding: 20px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    line-height: 1.5;"
        "}"
    );
    instructionLabel->setAlignment(Qt::AlignCenter);
    instructionLabel->setWordWrap(true);
    
    layout->addWidget(welcomeLabel);
    layout->addWidget(instructionLabel);
    layout->addStretch();
    
    // Add welcome tab
    m_tabWidget->addTab(welcomeWidget, "Welcome");
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
    Q_UNUSED(column)
    
    if (!item) return;
    
    QString itemText = item->text(0);
    
    // Check if item has a file path data (files)
    QString filePath = item->data(0, Qt::UserRole).toString();
    // Check if item has a folder path data (folders)
    QString folderPath = item->data(0, Qt::UserRole + 1).toString();
    
    if (!filePath.isEmpty()) {
        // This is a file - open it in a new tab
        statusBar()->showMessage(QString("Opening file: %1...").arg(itemText));
        openFileInTab(filePath);
    } else if (!folderPath.isEmpty()) {
        // This is a folder - show folder information in status bar
        statusBar()->showMessage(QString("Selected folder: %1").arg(itemText));
    } else {
        // No specific data, just show the item name
        statusBar()->showMessage(QString("Selected: %1").arg(itemText));
    }
}

void MainApplication::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    
    if (!item) return;
    
    QString itemText = item->text(0);
    QString filePath = item->data(0, Qt::UserRole).toString();
    
    if (!filePath.isEmpty()) {
        // This is a file - open it in a new tab (same as single click)
        statusBar()->showMessage(QString("Opening file: %1...").arg(itemText));
        openFileInTab(filePath);
    } else {
        // This is a folder - toggle expansion
        if (item->isExpanded()) {
            item->setExpanded(false);
        } else {
            item->setExpanded(true);
        }
        statusBar()->showMessage(QString("Toggled folder: %1").arg(itemText));
    }
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
        // Close all tabs first
        while (m_tabWidget->count() > 0) {
            m_tabWidget->removeTab(0);
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
