#include "mainapplication.h"
#include <QApplication>
#include <QScreen>
#include <QHeaderView>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

MainApplication::MainApplication(const UserSession &userSession, QWidget *parent)
    : QMainWindow(parent)
    , m_userSession(userSession)
    , m_dbManager(new DatabaseManager(this))
    , m_networkManager(new QNetworkAccessManager(this))
    , m_baseUrl("http://localhost/api") // WAMP server API endpoint
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
    
    // Load initial file list from WAMP server
    loadFileList();
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
    
    // Create splitter for tree view and content area
    m_splitter = new QSplitter(Qt::Horizontal, this);
    
    // Setup tree view and content area
    setupTreeView();
    setupContentArea();
    
    // Add to splitter
    m_splitter->addWidget(m_treeWidget);
    m_splitter->addWidget(m_contentWidget);
    
    // Set splitter proportions (tree view: 25%, content: 75%)
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
    viewMenu->addAction("&Refresh Tree", this, &MainApplication::loadFileList);
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
    connect(refreshAction, &QAction::triggered, this, &MainApplication::loadFileList);
    
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

void MainApplication::updateUserInfo()
{
    // Update window title with user info
    setWindowTitle(QString("Way2Repair - Equipment Maintenance System - %1").arg(m_userSession.fullName));
}

// Slot implementations
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
        // This is a file - load its content from the server
        statusBar()->showMessage(QString("Loading file: %1...").arg(itemText));
        loadFileContent(filePath);
    } else if (!folderPath.isEmpty()) {
        // This is a folder - show folder information
        statusBar()->showMessage(QString("Selected folder: %1").arg(itemText));
        
        // Count children for folder info
        int fileCount = 0;
        int folderCount = 0;
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem *child = item->child(i);
            if (!child->data(0, Qt::UserRole).toString().isEmpty()) {
                fileCount++; // Has file path data
            } else if (!child->data(0, Qt::UserRole + 1).toString().isEmpty()) {
                folderCount++; // Has folder path data
            }
        }
        
        QString folderInfo = QString("📁 Folder: %1\n\n").arg(itemText);
        folderInfo += QString("Path: %1\n").arg(folderPath);
        folderInfo += QString("Contains: %1 files, %2 folders\n\n").arg(fileCount).arg(folderCount);
        folderInfo += "Double-click to expand/collapse this folder.\n";
        folderInfo += "Click on files to view their content.";
        
        updateContentArea(QString("Folder: %1").arg(itemText), folderInfo);
    } else {
        // Unknown item type
        statusBar()->showMessage(QString("Selected: %1").arg(itemText));
        updateContentArea(itemText, "Select a file to view its content or a folder to see its information.");
    }
}
void MainApplication::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    
    if (!item) return;
    
    QString itemText = item->text(0);
    statusBar()->showMessage(QString("Opening: %1").arg(itemText));
    
    // In a real application, you would open the file in an external editor
    // or internal editor based on file type
    QMessageBox::information(this, "File Action", 
        QString("In a real application, this would open:\n%1\n\nWith the appropriate application or editor.").arg(itemText));
}

void MainApplication::onAboutClicked()
{
    QMessageBox::about(this, "About Way2Repair",
        "<h2>Way2Repair v4.75</h2>"
        "<p>Inquiry System for Intelligent Terminal Equipment Maintenance</p>"
        "<p>Professional equipment maintenance management solution with file management.</p>"
        "<br>"
        "<p><b>Features:</b></p>"
        "<ul>"
        "<li>File and folder tree navigation</li>"
        "<li>Configuration file viewing</li>"
        "<li>Log file analysis</li>"
        "<li>Equipment data management</li>"
        "<li>User session tracking</li>"
        "</ul>"
        "<br>"
        "<p><b>How to use:</b></p>"
        "<p>• Use the tree view on the left to navigate files and folders<br>"
        "• Click on files to view their contents<br>"
        "• Double-click to open files (simulated)<br>"
        "• Use toolbar buttons to manage the tree view</p>"
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
        statusBar()->showMessage("Logging out...");
        emit logoutRequested();
        close();
    }
}

// HTTP-based file loading from WAMP server

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
    if (!reply) {
        return;
    }
    
    QString requestType = reply->property("requestType").toString();
    
    if (requestType == "fileList") {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QString jsonString = QString::fromUtf8(data);
            
            QJsonDocument doc = QJsonDocument::fromJson(data);
            
            if (!doc.isNull()) {
                parseFileListJson(doc);
                statusBar()->showMessage("File list loaded successfully from server");
            } else {
                statusBar()->showMessage("Failed to parse file list from server - invalid JSON");
                loadFallbackData();
            }
        } else {
            statusBar()->showMessage(QString("Failed to load file list: %1").arg(reply->errorString()));
            loadFallbackData();
        }
    } else if (requestType == "fileContent") {
        QString filePath = reply->property("filePath").toString();
        
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            
            if (!doc.isNull() && doc.isObject()) {
                // Handle JSON response from file_content.php
                QJsonObject obj = doc.object();
                
                if (obj["success"].toBool()) {
                    QString content = obj["content"].toString();
                    QString fileName = QFileInfo(filePath).fileName();
                    QString fileType = obj["type"].toString();
                    int fileSize = obj["size"].toInt();
                    QString modifiedDate = obj["modified"].toString();
                    
                    // Create enhanced content with file info
                    QString enhancedContent = QString("File: %1\n").arg(fileName);
                    enhancedContent += QString("Type: %1\n").arg(fileType);
                    enhancedContent += QString("Size: %1 bytes\n").arg(fileSize);
                    enhancedContent += QString("Modified: %1\n").arg(modifiedDate);
                    enhancedContent += QString("\n%1").arg(QString("-").repeated(50));
                    enhancedContent += QString("\n\n%1").arg(content);
                    
                    updateContentArea(fileName, enhancedContent);
                    statusBar()->showMessage(QString("File loaded: %1 (%2 bytes)").arg(fileName).arg(fileSize));
                } else {
                    QString errorMsg = obj["error"].toString();
                    updateContentArea(filePath, QString("Error loading file: %1").arg(errorMsg));
                    statusBar()->showMessage(QString("Failed to load file: %1").arg(errorMsg));
                }
            } else {
                // Try as plain text if not JSON
                QString content = QString::fromUtf8(data);
                QString fileName = QFileInfo(filePath).fileName();
                updateContentArea(fileName, content);
                statusBar()->showMessage(QString("File loaded: %1").arg(fileName));
            }
        } else {
            QString fileName = QFileInfo(filePath).fileName();
            updateContentArea(fileName, QString("Failed to load file: %1").arg(reply->errorString()));
            statusBar()->showMessage(QString("Error loading %1: %2").arg(fileName).arg(reply->errorString()));
        }
    }
    
    reply->deleteLater();
}

void MainApplication::onNetworkError(QNetworkReply::NetworkError error)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    QString errorMessage = QString("Network error: %1 (%2)").arg(reply->errorString()).arg(error);
    statusBar()->showMessage(errorMessage);
    
    qDebug() << "Network error occurred:" << errorMessage;
    qDebug() << "HTTP status code:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "HTTP reason phrase:" << reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    
    // Read the response body for more details
    QByteArray responseData = reply->readAll();
    if (!responseData.isEmpty()) {
        qDebug() << "Server response:" << responseData;
        
        QString requestType = reply->property("requestType").toString();
        if (requestType == "fileContent") {
            QString filePath = reply->property("filePath").toString();
            QString fileName = QFileInfo(filePath).fileName();
            QString errorDetails = QString("Server error details:\n%1").arg(QString::fromUtf8(responseData));
            updateContentArea(fileName, errorDetails);
        }
    }
    
    QString requestType = reply->property("requestType").toString();
    if (requestType == "fileList") {
        statusBar()->showMessage("Server not available, showing fallback data");
        loadFallbackData();
    }
}

void MainApplication::parseFileListJson(const QJsonDocument &doc)
{
    m_treeWidget->clear();
    
    if (!doc.isObject() && !doc.isArray()) {
        statusBar()->showMessage("Invalid JSON format from server");
        return;
    }
    
    QTreeWidgetItem *rootItem = m_treeWidget->invisibleRootItem();
    
    if (doc.isArray()) {
        // Handle array format: [{"name": "file1.txt", "type": "file", "path": "/path/to/file1.txt"}, ...]
        QJsonArray items = doc.array();
        
        for (const QJsonValue &value : items) {
            if (value.isObject()) {
                addTreeItem(rootItem, value.toObject());
            }
        }
    } else {
        // Handle object format from our PHP API
        QJsonObject root = doc.object();
        
        // Check if the request was successful
        if (root.contains("success") && !root["success"].toBool()) {
            QString errorMsg = root["error"].toString("Unknown error");
            statusBar()->showMessage(QString("Server error: %1").arg(errorMsg));
            return;
        }
        
        // Our PHP API returns all items (files and folders) in the "folders" array
        if (root.contains("folders") && root["folders"].isArray()) {
            QJsonArray items = root["folders"].toArray();
            
            for (const QJsonValue &value : items) {
                if (value.isObject()) {
                    addTreeItem(rootItem, value.toObject());
                }
            }
        }
        
        // Also handle legacy format with separate files array
        if (root.contains("files") && root["files"].isArray()) {
            QJsonArray files = root["files"].toArray();
            
            for (const QJsonValue &value : files) {
                if (value.isObject()) {
                    addTreeItem(rootItem, value.toObject());
                }
            }
        }
    }
    
    // Expand first level by default
    m_treeWidget->expandToDepth(0);
    
    // Show count in status bar
    int itemCount = rootItem->childCount();
    statusBar()->showMessage(QString("Loaded %1 items from server").arg(itemCount));
}

void MainApplication::addTreeItem(QTreeWidgetItem *parent, const QJsonObject &item)
{
    QString name = item["name"].toString();
    QString type = item["type"].toString();
    QString path = item["path"].toString();
    
    if (name.isEmpty()) return;
    
    QTreeWidgetItem *treeItem = new QTreeWidgetItem(parent, QStringList(name));
    
    // Store file path in item data for files
    if (type == "file" && !path.isEmpty()) {
        treeItem->setData(0, Qt::UserRole, path);
    } else if (type == "folder" && !path.isEmpty()) {
        treeItem->setData(0, Qt::UserRole + 1, path);
    }
    
    // Add children if they exist
    if (item.contains("children") && item["children"].isArray()) {
        QJsonArray children = item["children"].toArray();
        
        for (const QJsonValue &child : children) {
            if (child.isObject()) {
                addTreeItem(treeItem, child.toObject());
            }
        }
    }
}

void MainApplication::loadFallbackData()
{
    // Fallback to sample data if server is not available
    m_treeWidget->clear();
    QTreeWidgetItem *rootItem = m_treeWidget->invisibleRootItem();
    
    // Sample server files structure
    QTreeWidgetItem *configFolder = new QTreeWidgetItem(rootItem, QStringList("Server Config"));
    
    QTreeWidgetItem *apacheConfig = new QTreeWidgetItem(configFolder, QStringList("apache_config.txt"));
    apacheConfig->setData(0, Qt::UserRole, "/config/apache_config.txt");
    
    QTreeWidgetItem *phpConfig = new QTreeWidgetItem(configFolder, QStringList("php.ini"));
    phpConfig->setData(0, Qt::UserRole, "/config/php.ini");
    
    QTreeWidgetItem *logsFolder = new QTreeWidgetItem(rootItem, QStringList("Server Logs"));
    
    QTreeWidgetItem *accessLog = new QTreeWidgetItem(logsFolder, QStringList("access.log"));
    accessLog->setData(0, Qt::UserRole, "/logs/access.log");
    
    QTreeWidgetItem *errorLog = new QTreeWidgetItem(logsFolder, QStringList("error.log"));
    errorLog->setData(0, Qt::UserRole, "/logs/error.log");
    
    m_treeWidget->expandToDepth(0);
    statusBar()->showMessage("Showing fallback data - server not available");
}

void MainApplication::updateContentArea(const QString &title, const QString &content)
{
    QVBoxLayout *contentLayout = qobject_cast<QVBoxLayout*>(m_contentWidget->layout());
    if (!contentLayout) {
        contentLayout = new QVBoxLayout(m_contentWidget);
    }
    
    // Clear existing content
    QLayoutItem *child;
    while ((child = contentLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    
    // Determine if this is a file or folder display
    bool isFile = title.contains("File:") || (!title.contains("Folder:") && content.contains("Type:"));
    
    // Add title with appropriate icon
    QString displayTitle = title;
    if (isFile && !title.startsWith("File:")) {
        displayTitle = QString("📄 File: %1").arg(title);
    } else if (!isFile && !title.startsWith("Folder:")) {
        displayTitle = QString("📁 %1").arg(title);
    }
    
    QLabel *titleLabel = new QLabel(displayTitle);
    titleLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    color: #2c3e50;"
        "    padding: 15px 20px 10px 20px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    border: none;"
        "    background-color: #f8f9ff;"
        "    border-bottom: 2px solid #e1e8f5;"
        "}"
    );
    
    // Add content with appropriate formatting
    QTextEdit *contentText = new QTextEdit();
    
    // Choose font based on content type
    QString fontFamily;
    if (isFile) {
        // Use monospace font for file content
        fontFamily = "'Consolas', 'Courier New', 'Monaco', monospace";
    } else {
        // Use regular font for folder info
        fontFamily = "'Segoe UI', Arial, sans-serif";
    }
    
    contentText->setStyleSheet(QString(
        "QTextEdit {"
        "    border: 1px solid #e0e0e0;"
        "    border-radius: 6px;"
        "    padding: 15px;"
        "    font-family: %1;"
        "    font-size: %2px;"
        "    background-color: white;"
        "    line-height: 1.4;"
        "}"
        "QScrollBar:vertical {"
        "    border: none;"
        "    background-color: #f0f0f0;"
        "    width: 12px;"
        "    border-radius: 6px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background-color: #c0c0c0;"
        "    border-radius: 6px;"
        "    min-height: 20px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background-color: #a0a0a0;"
        "}"
    ).arg(fontFamily).arg(isFile ? 11 : 12));
    
    contentText->setPlainText(content);
    contentText->setReadOnly(true);
    
    // Set word wrap based on content type
    if (isFile) {
        contentText->setLineWrapMode(QTextEdit::NoWrap); // Don't wrap code/file content
    } else {
        contentText->setLineWrapMode(QTextEdit::WidgetWidth); // Wrap folder descriptions
    }
    
    contentLayout->addWidget(titleLabel);
    contentLayout->addWidget(contentText);
}
