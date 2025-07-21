#include "ui/mainapplication.h"
#include "viewers/pdf/pdfviewerwidget.h"
#include "viewers/pcb/PCBViewerWidget.h"
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
#include <QTimer>
#include <QShortcut>

MainApplication::MainApplication(const UserSession &userSession, QWidget *parent)
    : QMainWindow(parent)
    , m_userSession(userSession)
    , m_dbManager(new DatabaseManager(this))
    , m_rootFolderPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))  // Set to downloads folder
    // Server-side initialization (commented out for local file loading)
    //, m_networkManager(new QNetworkAccessManager(this))
    //, m_baseUrl("http://localhost/api") // WAMP server API endpoint
{
    setupUI();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    updateUserInfo();
    
    // Setup keyboard shortcuts
    setupKeyboardShortcuts();
    
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
    
    QAction *toggleTreeAction = viewMenu->addAction("&Toggle Tree View");
    toggleTreeAction->setShortcut(QKeySequence("Ctrl+T"));
    toggleTreeAction->setCheckable(true);
    toggleTreeAction->setChecked(true);
    connect(toggleTreeAction, &QAction::triggered, this, &MainApplication::toggleTreeView);
    
    QAction *fullScreenPDFAction = viewMenu->addAction("&Full Screen PDF");
    fullScreenPDFAction->setShortcut(QKeySequence("F11"));
    connect(fullScreenPDFAction, &QAction::triggered, this, &MainApplication::toggleFullScreenPDF);
    
    viewMenu->addSeparator();
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
    QAction *refreshAction = m_toolbar->addAction("🔄 Refresh");
    refreshAction->setToolTip("Refresh file tree");
    connect(refreshAction, &QAction::triggered, this, &MainApplication::loadLocalFiles);
    
    QAction *expandAction = m_toolbar->addAction("⊞ Expand All");
    expandAction->setToolTip("Expand all folders");
    connect(expandAction, &QAction::triggered, this, [this]() { m_treeWidget->expandAll(); });
    
    QAction *collapseAction = m_toolbar->addAction("⊟ Collapse All");
    collapseAction->setToolTip("Collapse all folders");
    connect(collapseAction, &QAction::triggered, this, [this]() { m_treeWidget->collapseAll(); });
    
    m_toolbar->addSeparator();
    
    QAction *toggleTreeAction = m_toolbar->addAction("📁 Toggle Tree");
    toggleTreeAction->setToolTip("Toggle tree view (Ctrl+T)");
    toggleTreeAction->setCheckable(true);
    toggleTreeAction->setChecked(true);
    connect(toggleTreeAction, &QAction::triggered, this, &MainApplication::toggleTreeView);
    
    QAction *fullScreenPDFAction = m_toolbar->addAction("📄 PDF Full Screen");
    fullScreenPDFAction->setToolTip("Full screen PDF view (F11)");
    connect(fullScreenPDFAction, &QAction::triggered, this, &MainApplication::toggleFullScreenPDF);
    
    m_toolbar->addSeparator();
    
    QAction *logoutAction = m_toolbar->addAction("🚪 Logout");
    logoutAction->setToolTip("Logout from application");
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
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, &MainApplication::onTreeItemExpanded);
    connect(m_treeWidget, &QTreeWidget::itemCollapsed, this, &MainApplication::onTreeItemCollapsed);
    
    // Tree will be populated via local file loading in constructor
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
        
        // Set up the tree item appearance
        setupTreeItemAppearance(item, entry);
        
        if (entry.isDir()) {
            // This is a directory
            item->setData(0, Qt::UserRole + 1, entry.absoluteFilePath()); // Store folder path
            
            // Add a dummy child item to make the folder expandable
            // The actual children will be loaded when the folder is expanded
            QTreeWidgetItem *dummyItem = new QTreeWidgetItem(item);
            dummyItem->setText(0, "Loading...");
            dummyItem->setData(0, Qt::UserRole + 2, true); // Mark as dummy
            
            // Don't expand folders by default - let user expand them
            item->setExpanded(false);
        } else {
            // This is a file
            item->setData(0, Qt::UserRole, entry.absoluteFilePath()); // Store file path
            
            // Make files non-expandable by ensuring they have no children
            // and can't be expanded
            item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
        }
    }
}

void MainApplication::openFileInTab(const QString &filePath)
{
    // Show loading message
    statusBar()->showMessage("Loading file...");
    
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
    
    // Read file content for non-PDF files
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
    if (isCodeFile(extension)) {
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
    
    // Add tab with file name and icon
    QString tabName = fileInfo.fileName();
    QIcon tabIcon = getFileIcon(filePath);
    int tabIndex = m_tabWidget->addTab(textEdit, tabIcon, tabName);
    
    // Set tab tooltip
    QString tooltip = QString("File: %1\nPath: %2\nSize: %3 bytes\nType: %4")
                        .arg(fileInfo.fileName())
                        .arg(filePath)
                        .arg(fileInfo.size())
                        .arg(extension.isEmpty() ? "Unknown" : extension.toUpper());
    m_tabWidget->setTabToolTip(tabIndex, tooltip);
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex);
    
    statusBar()->showMessage(QString("Opened file: %1").arg(fileInfo.fileName()));
}

void MainApplication::openPDFInTab(const QString &filePath)
{
    // Show loading message
    statusBar()->showMessage("Loading PDF file...");
    
    // Check if PDF file is already open in a tab
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QWidget *tabWidget = m_tabWidget->widget(i);
        if (tabWidget->property("filePath").toString() == filePath) {
            // File is already open, just switch to that tab
            m_tabWidget->setCurrentIndex(i);
            statusBar()->showMessage("PDF file already open in tab");
            return;
        }
    }
    
    // Verify file exists and is readable
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        statusBar()->showMessage("Error: Cannot access PDF file: " + filePath);
        QMessageBox::warning(this, "PDF Error", "Cannot access PDF file:\n" + filePath);
        return;
    }
    
    // Create PDF viewer widget (using the existing PDFViewerWidget but without internal tabs)
    PDFViewerWidget *pdfViewer = new PDFViewerWidget();
    pdfViewer->setProperty("filePath", filePath);
    
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
    
    // Add PDF viewer to tab first
    QString tabName = fileInfo.fileName();
    QIcon tabIcon = getFileIcon(filePath);
    int tabIndex = m_tabWidget->addTab(pdfViewer, tabIcon, tabName);
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex);
    
    // Load the PDF directly (the key fix is to bypass the internal tab system)
    QTimer::singleShot(100, this, [this, pdfViewer, filePath, tabIndex, fileInfo]() {
        // Try to load the PDF after the widget is properly initialized
        if (!pdfViewer->loadPDF(filePath)) {
            // If loading fails, remove the tab and show error
            m_tabWidget->removeTab(tabIndex);
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
    m_tabWidget->setTabToolTip(tabIndex, tooltip);
    
    statusBar()->showMessage(QString("Opened PDF: %1").arg(fileInfo.fileName()));
}

void MainApplication::openPCBInTab(const QString &filePath)
{
    // Show loading message
    statusBar()->showMessage("Loading PCB file...");
    
    // Check if PCB file is already open in a tab
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QWidget *tabWidget = m_tabWidget->widget(i);
        if (tabWidget->property("filePath").toString() == filePath) {
            // File is already open, just switch to that tab
            m_tabWidget->setCurrentIndex(i);
            statusBar()->showMessage("PCB file already open in tab");
            return;
        }
    }
    
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
    
    connect(pcbViewer, &PCBViewerWidget::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage("PCB: " + message);
    });
    
    connect(pcbViewer, &PCBViewerWidget::zoomChanged, this, [this](double zoomLevel) {
        statusBar()->showMessage(QString("PCB Zoom: %1%").arg(static_cast<int>(zoomLevel * 100)));
    });
    
    connect(pcbViewer, &PCBViewerWidget::pinSelected, this, [this](const QString &pinName, const QString &netName) {
        statusBar()->showMessage(QString("PCB: Selected pin %1 on net %2").arg(pinName, netName));
    });
    
    // Add PCB viewer to tab first
    QString tabName = fileInfo.fileName();
    QIcon tabIcon = getFileIcon(filePath);
    int tabIndex = m_tabWidget->addTab(pcbViewer, tabIcon, tabName);
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex);
    
    // Load the PCB after the widget is properly initialized
    QTimer::singleShot(100, this, [this, pcbViewer, filePath, tabIndex, fileInfo]() {
        // Try to load the PCB after the widget is properly initialized
        if (!pcbViewer->loadPCB(filePath)) {
            // If loading fails, remove the tab and show error
            m_tabWidget->removeTab(tabIndex);
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
    m_tabWidget->setTabToolTip(tabIndex, tooltip);
    
    statusBar()->showMessage(QString("Opened PCB: %1").arg(fileInfo.fileName()));
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

void MainApplication::onTreeItemExpanded(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Update folder icon to "open" state
    updateTreeItemIcon(item, true);
    
    // Check if this folder has dummy children and needs to be populated
    if (item->childCount() == 1) {
        QTreeWidgetItem *child = item->child(0);
        if (child && child->data(0, Qt::UserRole + 2).toBool()) {
            // This is a dummy item, remove it and load actual contents
            delete child;
            
            // Load the actual folder contents
            QString folderPath = item->data(0, Qt::UserRole + 1).toString();
            if (!folderPath.isEmpty()) {
                statusBar()->showMessage(QString("Loading folder: %1...").arg(item->text(0)));
                populateTreeFromDirectory(folderPath, item);
                statusBar()->showMessage(QString("Loaded folder: %1").arg(item->text(0)));
            }
        }
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
    item->setText(0, fileInfo.fileName());
    
    if (fileInfo.isDir()) {
        // This is a directory
        item->setIcon(0, getFolderIcon(false));
        
        // Add tooltip with folder info
        QString tooltip = QString("Folder: %1\nPath: %2\nModified: %3")
                            .arg(fileInfo.fileName())
                            .arg(fileInfo.absoluteFilePath())
                            .arg(fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
        item->setToolTip(0, tooltip);
        
        // Set folder-specific properties
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    } else {
        // This is a file
        item->setIcon(0, getFileIcon(fileInfo.absoluteFilePath()));
        
        // Add tooltip with file info
        QString extension = getFileExtension(fileInfo.absoluteFilePath());
        QString tooltip = QString("File: %1\nSize: %2 bytes\nType: %3\nModified: %4")
                            .arg(fileInfo.fileName())
                            .arg(fileInfo.size())
                            .arg(extension.isEmpty() ? "Unknown" : extension.toUpper())
                            .arg(fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
        item->setToolTip(0, tooltip);
        
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
        return style()->standardIcon(QStyle::SP_FileDialogListView);
    } else if (extension == "txt" || extension == "log" || extension == "md") {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else {
        // Default file icon
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}

QIcon MainApplication::getFolderIcon(bool isOpen)
{
    if (isOpen) {
        return style()->standardIcon(QStyle::SP_DirOpenIcon);
    } else {
        return style()->standardIcon(QStyle::SP_DirClosedIcon);
    }
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

void MainApplication::toggleTreeView()
{
    setTreeViewVisible(!m_treeViewVisible);
}

void MainApplication::toggleFullScreenPDF()
{
    // Check if current tab is a PDF viewer
    QWidget *currentWidget = m_tabWidget->currentWidget();
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
    
    if (visible) {
        // Show tree view - restore original sizes
        m_treeWidget->show();
        m_splitter->setSizes(m_splitterSizes);
        statusBar()->showMessage("Tree view shown");
    } else {
        // Hide tree view - save current sizes and collapse completely
        m_splitterSizes = m_splitter->sizes();
        m_treeWidget->hide();
        
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
    
    // Update toolbar action state
    QList<QAction*> toolbarActions = m_toolbar->actions();
    for (QAction *action : toolbarActions) {
        if (action->text().contains("Toggle Tree")) {
            action->setChecked(visible);
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
        int currentIndex = m_tabWidget->currentIndex();
        int nextIndex = (currentIndex + 1) % m_tabWidget->count();
        m_tabWidget->setCurrentIndex(nextIndex);
    });
    
    QShortcut *prevTabShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
    connect(prevTabShortcut, &QShortcut::activated, this, [this]() {
        int currentIndex = m_tabWidget->currentIndex();
        int prevIndex = (currentIndex - 1 + m_tabWidget->count()) % m_tabWidget->count();
        m_tabWidget->setCurrentIndex(prevIndex);
    });
}
