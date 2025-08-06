#include "viewers/pdf/pdfviewerwidget.h"
#include "viewers/pdf/PDFViewerEmbedder.h"

#include <QResizeEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QFocusEvent>
#include <QSplitter>
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QSizePolicy>
#include <QTimer>
#include <QThread>
#include <QToolBar>
#include <QAction>
#include <fstream>

// Enhanced debug logging to file for PDFViewerWidget
void WriteQtDebugToFile(const QString& message) {
    static std::ofstream debugFile;
    static bool fileInitialized = false;
    
    if (!fileInitialized) {
        std::string logPath = "build/pdf_debug.txt";  // Write to build directory
        debugFile.open(logPath, std::ios::app);
        fileInitialized = true;
    }
    
    if (debugFile.is_open()) {
        debugFile << "[QT-DEBUG] " << message.toStdString() << std::endl;
        debugFile.flush(); // Ensure immediate write
    }
    
    // Also use Qt's debug system
    qDebug() << "[QT-DEBUG]" << message;
}

PDFViewerWidget::PDFViewerWidget(QWidget *parent)
    : QWidget(parent)
    , m_pdfEmbedder(std::make_unique<PDFViewerEmbedder>())
    , m_mainLayout(nullptr)
    , m_toolbar(nullptr)
    , m_viewerContainer(nullptr)
    , m_splitter(nullptr)
    , m_leftViewerContainer(nullptr)
    , m_rightViewerContainer(nullptr)
    , m_rightPlaceholderLabel(nullptr)
    , m_isSplitView(false)
    , m_leftToolbar(nullptr)
    , m_rightToolbar(nullptr)
    , m_leftPanel(nullptr)
    , m_rightPanel(nullptr)
    , m_actionSlipTab(nullptr)
    , m_actionRotateLeft(nullptr)
    , m_actionRotateRight(nullptr)
    , m_actionPreviousPage(nullptr)
    , m_actionNextPage(nullptr)
    , m_actionZoomIn(nullptr)
    , m_actionZoomOut(nullptr)
    , m_actionFindPrevious(nullptr)
    , m_actionFindNext(nullptr)
    , m_leftActionRotateLeft(nullptr)
    , m_leftActionRotateRight(nullptr)
    , m_leftActionPreviousPage(nullptr)
    , m_leftActionNextPage(nullptr)
    , m_leftActionZoomIn(nullptr)
    , m_leftActionZoomOut(nullptr)
    , m_leftActionFindPrevious(nullptr)
    , m_leftActionFindNext(nullptr)
    , m_rightActionRotateLeft(nullptr)
    , m_rightActionRotateRight(nullptr)
    , m_rightActionPreviousPage(nullptr)
    , m_rightActionNextPage(nullptr)
    , m_rightActionZoomIn(nullptr)
    , m_rightActionZoomOut(nullptr)
    , m_rightActionFindPrevious(nullptr)
    , m_rightActionFindNext(nullptr)
    , m_pageLabel(nullptr)
    , m_pageInput(nullptr)
    , m_totalPagesLabel(nullptr)
    , m_searchLabel(nullptr)
    , m_searchInput(nullptr)
    , m_leftPageLabel(nullptr)
    , m_leftPageInput(nullptr)
    , m_leftTotalPagesLabel(nullptr)
    , m_leftSearchLabel(nullptr)
    , m_leftSearchInput(nullptr)
    , m_rightPageLabel(nullptr)
    , m_rightPageInput(nullptr)
    , m_rightTotalPagesLabel(nullptr)
    , m_rightSearchLabel(nullptr)
    , m_rightSearchInput(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_navigationTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_pdfLoaded(false)
    , m_rightPdfLoaded(false)
    , m_usingFallback(false)
    , m_navigationInProgress(false)
    , m_lastSelectedText()
{
    setupUI();
    
    // Configure update timer for smooth rendering
    m_updateTimer->setInterval(UPDATE_INTERVAL_MS);
    m_updateTimer->setSingleShot(false);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFViewerWidget::updateViewer);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFViewerWidget::checkForSelectedText);
    
    // Configure navigation timer to reset navigation flag after delay
    m_navigationTimer->setSingleShot(true);
    m_navigationTimer->setInterval(100); // 100ms delay
    connect(m_navigationTimer, &QTimer::timeout, this, [this]() {
        m_navigationInProgress = false;
        qDebug() << "PDFViewerWidget: Navigation flag reset";
    });
    
    qDebug() << "PDFViewerWidget: Created with advanced embedded renderer and Qt fallback";
}

PDFViewerWidget::~PDFViewerWidget()
{
    qDebug() << "PDFViewerWidget: Destructor called";
    
    // Stop the update timer
    if (m_updateTimer && m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
    
    // Shutdown the PDF embedder
    if (m_pdfEmbedder) {
        m_pdfEmbedder->shutdown();
    }
}

void PDFViewerWidget::setupUI()
{
    // Create main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // Setup toolbar first
    setupToolbar();
    
    // Setup viewer area
    setupViewerArea();
    
    // Add toolbar to main layout (at top, no stretch)
    m_mainLayout->addWidget(m_toolbar, 0);
    
    // Add splitter to main layout (takes remaining space)
    m_mainLayout->addWidget(m_splitter, 1);
    
    // Initially hide individual toolbars (they're shown only in split view)
    if (m_leftToolbar) {
        m_leftToolbar->hide();
    }
    if (m_rightToolbar) {
        m_rightToolbar->hide();
    }
    
    // Apply modern styling
    setStyleSheet(
        "PDFViewerWidget {"
        "    background-color: #f5f5f5;"
        "    border: 1px solid #d0d0d0;"
        "}"
    );
}



bool PDFViewerWidget::loadPDF(const QString& filePath)
{
    WriteQtDebugToFile("=== PDFViewerWidget::loadPDF() CALLED ===");
    WriteQtDebugToFile("File path: " + filePath);
    qDebug() << "PDFViewerWidget: Loading PDF:" << filePath;
    
    WriteQtDebugToFile("Step 1: Validating file existence...");
    // Validate file
    if (!QFileInfo::exists(filePath)) {
        QString error = QString("PDF file does not exist: %1").arg(filePath);
        WriteQtDebugToFile("ERROR: " + error);
        qWarning() << error;
        emit errorOccurred(error);
        return false;
    }
    
    QFileInfo fileInfo(filePath);
    WriteQtDebugToFile("File validation passed - Size: " + QString::number(fileInfo.size()) + " bytes");
    WriteQtDebugToFile("File is readable: " + QString(fileInfo.isReadable() ? "true" : "false"));
    
    WriteQtDebugToFile("Step 2: Starting PDF loading process...");
    // PDF loading process started
    
    WriteQtDebugToFile("Step 3: Checking PDF viewer initialization...");
    // Initialize the embedded PDF viewer if not already done
    if (!m_viewerInitialized) {
        WriteQtDebugToFile("PDF viewer not initialized, calling initializePDFViewer()...");
        qDebug() << "PDFViewerWidget: Initializing embedded PDF viewer";
        initializePDFViewer();
        
        if (!m_viewerInitialized) {
            WriteQtDebugToFile("ERROR: PDF viewer initialization failed");
            QString error = "Failed to initialize PDF viewer";
            emit errorOccurred(error);
            return false;
        }
        WriteQtDebugToFile("PDF viewer initialized successfully");
    } else {
        WriteQtDebugToFile("PDF viewer already initialized");
    }
    
    WriteQtDebugToFile("Step 4: Calling PDFEmbedder->loadPDF()...");
    WriteQtDebugToFile("Converting QString to std::string: " + filePath + " -> " + QString::fromStdString(filePath.toStdString()));
    
    // Load the PDF file
    if (!m_pdfEmbedder->loadPDF(filePath.toStdString())) {
        QString error = QString("Failed to load PDF: %1").arg(filePath);
        WriteQtDebugToFile("ERROR: PDFEmbedder->loadPDF() returned false - " + error);
        qWarning() << error;
        emit errorOccurred(error);
        return false;
    }
    
    WriteQtDebugToFile("Step 5: PDFEmbedder->loadPDF() succeeded, updating UI...");
    
    // Update state
    m_currentFilePath = filePath;
    m_pdfLoaded = true;
    
    WriteQtDebugToFile("Step 6: PDF loading completed successfully");
    
    WriteQtDebugToFile("Step 7: Emitting signals...");
    // Emit success signal
    emit pdfLoaded(filePath);
    emit pageChanged(getCurrentPage(), getPageCount());
    
    WriteQtDebugToFile("=== PDFViewerWidget::loadPDF() COMPLETED SUCCESSFULLY ===");
    WriteQtDebugToFile("Successfully loaded PDF with " + QString::number(getPageCount()) + " pages");
    qDebug() << "PDFViewerWidget: Successfully loaded PDF with" << getPageCount() << "pages";
    return true;
}

bool PDFViewerWidget::loadRightPanelPDF(const QString& filePath)
{
    qDebug() << "PDFViewerWidget: Loading PDF into right panel:" << filePath;
    
    // Validate file
    if (!QFileInfo::exists(filePath)) {
        QString error = QString("File does not exist: %1").arg(filePath);
        qWarning() << error;
        emit errorOccurred(error);
        return false;
    }
    
    // For now, we'll just mark the right panel as having a PDF loaded
    // In the future, this could be extended to support a separate PDF embedder for the right panel
    m_rightPdfLoaded = true;
    
    // If we're currently in split view, show the right toolbar
    if (m_isSplitView && m_rightToolbar) {
        m_rightToolbar->show();
        qDebug() << "PDFViewerWidget: Right toolbar shown (PDF loaded into right panel)";
    }
    
    // Update the right panel's placeholder to show that a PDF is "loaded"
    if (m_rightPlaceholderLabel) {
        m_rightPlaceholderLabel->setText(QString("PDF Loaded: %1\n\n(Full implementation coming soon)").arg(QFileInfo(filePath).fileName()));
    }
    
    qDebug() << "PDFViewerWidget: Right panel PDF loaded successfully";
    return true;
}

void PDFViewerWidget::clearRightPanelPDF()
{
    qDebug() << "PDFViewerWidget: Clearing right panel PDF";
    
    // Mark right panel as not having PDF loaded
    m_rightPdfLoaded = false;
    
    // Hide the right toolbar since no PDF is loaded
    if (m_rightToolbar) {
        m_rightToolbar->hide();
        qDebug() << "PDFViewerWidget: Right toolbar hidden (PDF cleared from right panel)";
    }
    
    // Reset the right panel's placeholder
    if (m_rightPlaceholderLabel) {
        m_rightPlaceholderLabel->setText("Second viewer not implemented yet.\n\nPlease load a PDF file to enable toolbar controls.");
    }
    
    qDebug() << "PDFViewerWidget: Right panel PDF cleared successfully";
}

void PDFViewerWidget::initializePDFViewer()
{
    static bool globalInitializationInProgress = false;
    
    if (m_viewerInitialized) {
        qDebug() << "PDFViewerWidget: Already initialized, skipping";
        return;
    }
    
    // Prevent multiple simultaneous initialization attempts across all instances
    if (globalInitializationInProgress) {
        qDebug() << "PDFViewerWidget: Global initialization in progress, waiting...";
        // Small delay and retry once
        QApplication::processEvents();
        QThread::msleep(50);
        if (m_viewerInitialized || globalInitializationInProgress) {
            return;
        }
    }
    
    globalInitializationInProgress = true;
    qDebug() << "PDFViewerWidget: Starting PDF viewer initialization";
    
    // Get the native Windows handle of the left viewer container (where PDF will be embedded)
    QWidget* activeContainer = m_leftViewerContainer ? m_leftViewerContainer : m_viewerContainer;
    HWND containerHwnd = reinterpret_cast<HWND>(activeContainer->winId());
    
    // Verify the container has a valid size
    if (activeContainer->width() <= 0 || activeContainer->height() <= 0) {
        qDebug() << "PDFViewerWidget: Container has invalid size, deferring initialization";
        globalInitializationInProgress = false;
        // Defer initialization until we have proper size
        QTimer::singleShot(100, this, [this]() {
            QWidget* retryContainer = m_leftViewerContainer ? m_leftViewerContainer : m_viewerContainer;
            if (!m_viewerInitialized && retryContainer->width() > 0 && retryContainer->height() > 0) {
                initializePDFViewer();
            }
        });
        return;
    }
    
    // Initialize the embedded PDF viewer
    bool initSuccess = m_pdfEmbedder->initialize(containerHwnd, activeContainer->width(), activeContainer->height());
    
    globalInitializationInProgress = false;
    
    if (!initSuccess) {
        qCritical() << "PDFViewerWidget: Failed to initialize embedded PDF viewer";
        emit errorOccurred("Failed to initialize PDF rendering engine");
        return;
    }
    
    m_viewerInitialized = true;
    
    // Start the update timer for smooth rendering
    m_updateTimer->start();
    
    qDebug() << "PDFViewerWidget: Embedded PDF viewer initialized successfully";
}

// Public API implementation
bool PDFViewerWidget::isPDFLoaded() const
{
    return m_pdfLoaded && m_pdfEmbedder && m_pdfEmbedder->isPDFLoaded();
}

bool PDFViewerWidget::isRightPanelPDFLoaded() const
{
    return m_rightPdfLoaded;
}

int PDFViewerWidget::getPageCount() const
{
    if (!isPDFLoaded()) return 0;
    return m_pdfEmbedder->getPageCount();
}

double PDFViewerWidget::getCurrentZoom() const
{
    if (!isPDFLoaded()) return 1.0;
    return static_cast<double>(m_pdfEmbedder->getCurrentZoom());
}

int PDFViewerWidget::getCurrentPage() const
{
    if (!isPDFLoaded()) return 1;
    return m_pdfEmbedder->getCurrentPage();
}

bool PDFViewerWidget::isReady() const
{
    return m_viewerInitialized && isPDFLoaded();
}




void PDFViewerWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    // The embedded viewer handles its own painting
}


void PDFViewerWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // The embedded viewer will handle its own hide logic
}

void PDFViewerWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    
    // Forward focus to the embedded viewer
    if (m_viewerInitialized) {
        m_pdfEmbedder->setFocus();
    }
}

void PDFViewerWidget::setupToolbar()
{
    // Create empty toolbar
    m_toolbar = new QToolBar(this);
    m_toolbar->setFixedHeight(30);
    m_toolbar->setIconSize(QSize(30, 30));  // Adjust icon size for 30px height
    m_toolbar->setStyleSheet(
        "QToolBar {"
        "    background-color: #ffffff;"
        "    border: none;"
        "    border-bottom: 1px solid #d0d0d0;"
        "    spacing: 5px;"
        "    padding: 4px;"
        "}"
        "QToolButton {"
        "    background-color: transparent;"
        "    border: 1px solid transparent;"
        "    border-radius: 2px;"
        "    padding: 4px;"
        "    min-width: 30px;"
        "    min-height: 20px;"
        "    font-size: 16px;"
        "}"
        "QToolButton:hover {"
        "    background-color: #e6f3ff;"
        "    border-color: #b3d9ff;"
        "}"
        "QToolButton:pressed {"
        "    background-color: #cce7ff;"
        "    border-color: #99ccff;"
        "}"
    );
    
    // Add slip tab button (using SVG icon from resources)
    m_actionSlipTab = m_toolbar->addAction(QIcon(":/icons/images/icons/slit-tab.png"), "");
    m_actionSlipTab->setToolTip("Slip Tab");
    connect(m_actionSlipTab, &QAction::triggered, this, &PDFViewerWidget::onSlipTabClicked);
    
    // Add separator
    m_toolbar->addSeparator();
    
    // Add rotate left button
    m_actionRotateLeft = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_left.svg"), "");
    m_actionRotateLeft->setToolTip("Rotate Left");
    connect(m_actionRotateLeft, &QAction::triggered, this, &PDFViewerWidget::rotateLeft);
    
    // Add rotate right button
    m_actionRotateRight = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_right.svg"), "");
    m_actionRotateRight->setToolTip("Rotate Right");
    connect(m_actionRotateRight, &QAction::triggered, this, &PDFViewerWidget::rotateRight);
    
    // Add separator
    m_toolbar->addSeparator();
    
    
    
    // Page label
    m_pageLabel = new QLabel("Page:", this);
    m_pageLabel->setStyleSheet("QLabel { color: #333333; font-weight: bold; margin: 0 5px; }");
    m_toolbar->addWidget(m_pageLabel);
    
    // Page input box
    m_pageInput = new QLineEdit(this);
    m_pageInput->setFixedWidth(60);  // Increased from 40 to 60 pixels for better visibility
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setText("1");
    m_pageInput->setToolTip("Enter page number and press Enter");
    m_pageInput->setReadOnly(false);  // Explicitly ensure it's editable
    m_pageInput->setEnabled(true);    // Explicitly ensure it's enabled
    m_pageInput->setStyleSheet(
        "QLineEdit {"
        "    border: 1px solid #cccccc;"
        "    border-radius: 3px;"
        "    padding: 2px 4px;"
        "    font-size: 11px;"  // Increased from 10px to 11px for better readability
        "    background-color: white;"
        "    font-weight: bold;"  // Make the text bold for better visibility
        "}"
        "QLineEdit:focus {"
        "    border-color: #4285f4;"
        "    outline: none;"
        "}"
    );
    connect(m_pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    
    // Add focus out event to refresh page display when user finishes editing
    connect(m_pageInput, &QLineEdit::editingFinished, this, [this]() {
        // When editing is finished, validate the input
        if (isPDFLoaded()) {
            QString inputText = m_pageInput->text().trimmed();
            bool ok;
            int inputPage = inputText.toInt(&ok);
            int totalPages = getPageCount();
            
            // If user entered a valid page number, navigate to it
            if (ok && inputPage >= 1 && inputPage <= totalPages) {
                // User entered a valid page number - don't reset it
                qDebug() << "PDFViewerWidget: Valid page number entered:" << inputPage;
            } else {
                // Invalid input - reset to current page
                int currentPage = getCurrentPage();
                m_pageInput->setText(QString::number(currentPage));
                qDebug() << "PDFViewerWidget: Invalid page input '" << inputText << "' reset to current page:" << currentPage;
            }
        }
    });
    
    m_toolbar->addWidget(m_pageInput);
    
    // Total pages label
    m_totalPagesLabel = new QLabel("/ 0", this);
    m_totalPagesLabel->setStyleSheet("QLabel { color: #666666; margin: 0 5px; }");
    m_toolbar->addWidget(m_totalPagesLabel);

    // Add page navigation controls
    // Previous page button
    m_actionPreviousPage = m_toolbar->addAction(QIcon(":/icons/images/icons/previous.svg"), "");
    m_actionPreviousPage->setToolTip("Previous Page");
    connect(m_actionPreviousPage, &QAction::triggered, this, &PDFViewerWidget::previousPage);
    
    // Next page button
    m_actionNextPage = m_toolbar->addAction(QIcon(":/icons/images/icons/next.svg"), "");
    m_actionNextPage->setToolTip("Next Page");
    connect(m_actionNextPage, &QAction::triggered, this, &PDFViewerWidget::nextPage);
    
    // Add separator
    m_toolbar->addSeparator();
    
    // Add zoom in button (SVG icon from resources)
    m_actionZoomIn = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_in.svg"), "");
    m_actionZoomIn->setToolTip("Zoom In");
    connect(m_actionZoomIn, &QAction::triggered, this, &PDFViewerWidget::zoomIn);
    
    // Add zoom out button (SVG icon from resources)
    m_actionZoomOut = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_out.svg"), "");
    m_actionZoomOut->setToolTip("Zoom Out");
    connect(m_actionZoomOut, &QAction::triggered, this, &PDFViewerWidget::zoomOut);
    
    // Add separator
    m_toolbar->addSeparator();
    
    // Search label
    m_searchLabel = new QLabel("Search:", this);
    m_searchLabel->setStyleSheet("QLabel { color: #333333; font-weight: bold; margin: 0 5px; }");
    m_toolbar->addWidget(m_searchLabel);
    
    // Search input box
    m_searchInput = new QLineEdit(this);
    m_searchInput->setFixedWidth(120);
    m_searchInput->setPlaceholderText("Search text...");
    m_searchInput->setToolTip("Enter search term and press Enter");
    m_searchInput->setStyleSheet(
        "QLineEdit {"
        "    border: 1px solid #cccccc;"
        "    border-radius: 3px;"
        "    padding: 2px 8px;"
        "    font-size: 11px;"
        "    background-color: white;"
        "}"
        "QLineEdit:focus {"
        "    border-color: #4285f4;"
        "    outline: none;"
        "}"
    );
    connect(m_searchInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onSearchInputChanged);
    connect(m_searchInput, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchInputChanged);
    m_toolbar->addWidget(m_searchInput);
    
    // Add search navigation controls
    // Previous search result button
    m_actionFindPrevious = m_toolbar->addAction(QIcon(":/icons/images/icons/search_previous.svg"), "");
    m_actionFindPrevious->setToolTip("Find Previous");
    connect(m_actionFindPrevious, &QAction::triggered, this, &PDFViewerWidget::findPrevious);
    
    // Next search result button
    m_actionFindNext = m_toolbar->addAction(QIcon(":/icons/images/icons/search_next.svg"), "");
    m_actionFindNext->setToolTip("Find Next");
    connect(m_actionFindNext, &QAction::triggered, this, &PDFViewerWidget::findNext);
}




void PDFViewerWidget::setupViewerArea()
{
    // Create splitter for split view support
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    
    // Create left panel (contains toolbar + viewer)
    m_leftPanel = new QWidget(this);
    QVBoxLayout* leftPanelLayout = new QVBoxLayout(m_leftPanel);
    leftPanelLayout->setContentsMargins(0, 0, 0, 0);
    leftPanelLayout->setSpacing(0);
    
    // Create left toolbar
    m_leftToolbar = new QToolBar(m_leftPanel);
    setupIndividualToolbar(m_leftToolbar, true); // true = left panel
    
    // Create left viewer container
    m_leftViewerContainer = new QWidget(m_leftPanel);
    m_leftViewerContainer->setMinimumSize(400, 300);
    m_leftViewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_leftViewerContainer->setStyleSheet(
        "QWidget {"
        "    background-color: #ffffff;"
        "    border: 1px solid #cccccc;"
        "}"
    );
    
    // Add to left panel layout
    leftPanelLayout->addWidget(m_leftToolbar);
    leftPanelLayout->addWidget(m_leftViewerContainer);
    
    // Create right panel (contains toolbar + viewer)
    m_rightPanel = new QWidget(this);
    QVBoxLayout* rightPanelLayout = new QVBoxLayout(m_rightPanel);
    rightPanelLayout->setContentsMargins(0, 0, 0, 0);
    rightPanelLayout->setSpacing(0);
    
    // Create right toolbar
    m_rightToolbar = new QToolBar(m_rightPanel);
    setupIndividualToolbar(m_rightToolbar, false); // false = right panel
    
    // Create right viewer container
    m_rightViewerContainer = new QWidget(m_rightPanel);
    m_rightViewerContainer->setMinimumSize(400, 300);
    m_rightViewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightViewerContainer->setStyleSheet(
        "QWidget {"
        "    background-color: #f0f0f0;"
        "    border: 1px solid #cccccc;"
        "}"
    );
    
    // Create placeholder label for right viewer
    m_rightPlaceholderLabel = new QLabel("Second viewer not implemented yet.\n\nPlease load a PDF file to enable toolbar controls.", m_rightViewerContainer);
    m_rightPlaceholderLabel->setAlignment(Qt::AlignCenter);
    m_rightPlaceholderLabel->setStyleSheet(
        "QLabel {"
        "    color: #666666;"
        "    font-size: 14px;"
        "    font-style: italic;"
        "    background-color: transparent;"
        "}"
    );
    
    // Layout for right container
    QVBoxLayout* rightContainerLayout = new QVBoxLayout(m_rightViewerContainer);
    rightContainerLayout->addWidget(m_rightPlaceholderLabel);
    
    // Add to right panel layout
    rightPanelLayout->addWidget(m_rightToolbar);
    rightPanelLayout->addWidget(m_rightViewerContainer);
    
    // Add panels to splitter
    m_splitter->addWidget(m_leftPanel);
    m_splitter->addWidget(m_rightPanel);
    
    // Set equal sizes initially
    m_splitter->setSizes({400, 400});
    
    // Initially, set to single view mode
    m_viewerContainer = m_leftViewerContainer;  // Main viewer container points to left container
    m_rightPanel->hide();  // Hide right panel initially
    m_isSplitView = false;
    
    // Install event filter to capture mouse clicks and clear page input focus
    m_viewerContainer->installEventFilter(this);
}

void PDFViewerWidget::setupIndividualToolbar(QToolBar* toolbar, bool isLeftPanel)
{
    toolbar->setFixedHeight(30);
    toolbar->setIconSize(QSize(30, 30));
    toolbar->setStyleSheet(
        "QToolBar {"
        "    background-color: #ffffff;"
        "    border: none;"
        "    border-bottom: 1px solid #d0d0d0;"
        "    spacing: 5px;"
        "    padding: 4px;"
        "}"
        "QToolButton {"
        "    background-color: transparent;"
        "    border: 1px solid transparent;"
        "    border-radius: 2px;"
        "    padding: 4px;"
        "    min-width: 30px;"
        "    min-height: 20px;"
        "    font-size: 16px;"
        "}"
        "QToolButton:hover {"
        "    background-color: #e6f3ff;"
        "    border-color: #b3d9ff;"
        "}"
        "QToolButton:pressed {"
        "    background-color: #cce7ff;"
        "    border-color: #99ccff;"
        "}"
    );
    
    QAction** rotateLeftAction = isLeftPanel ? &m_leftActionRotateLeft : &m_rightActionRotateLeft;
    QAction** rotateRightAction = isLeftPanel ? &m_leftActionRotateRight : &m_rightActionRotateRight;
    QAction** previousPageAction = isLeftPanel ? &m_leftActionPreviousPage : &m_rightActionPreviousPage;
    QAction** nextPageAction = isLeftPanel ? &m_leftActionNextPage : &m_rightActionNextPage;
    QAction** zoomInAction = isLeftPanel ? &m_leftActionZoomIn : &m_rightActionZoomIn;
    QAction** zoomOutAction = isLeftPanel ? &m_leftActionZoomOut : &m_rightActionZoomOut;
    
    QLabel** pageLabel = isLeftPanel ? &m_leftPageLabel : &m_rightPageLabel;
    QLineEdit** pageInput = isLeftPanel ? &m_leftPageInput : &m_rightPageInput;
    QLabel** totalPagesLabel = isLeftPanel ? &m_leftTotalPagesLabel : &m_rightTotalPagesLabel;
    QLabel** searchLabel = isLeftPanel ? &m_leftSearchLabel : &m_rightSearchLabel;
    QLineEdit** searchInput = isLeftPanel ? &m_leftSearchInput : &m_rightSearchInput;
    
    QString panelName = isLeftPanel ? "Left" : "Right";
    
    // Add rotate left button
    *rotateLeftAction = toolbar->addAction(QIcon(":/icons/images/icons/rotate_left.svg"), "");
    (*rotateLeftAction)->setToolTip("Rotate Left (" + panelName + ")");
    connect(*rotateLeftAction, &QAction::triggered, this, &PDFViewerWidget::rotateLeft);
    
    // Add rotate right button
    *rotateRightAction = toolbar->addAction(QIcon(":/icons/images/icons/rotate_right.svg"), "");
    (*rotateRightAction)->setToolTip("Rotate Right (" + panelName + ")");
    connect(*rotateRightAction, &QAction::triggered, this, &PDFViewerWidget::rotateRight);
    
    // Add separator
    toolbar->addSeparator();
    
    // Page label
    *pageLabel = new QLabel("Page:", toolbar);
    (*pageLabel)->setStyleSheet("QLabel { color: #333333; font-weight: bold; margin: 0 5px; }");
    toolbar->addWidget(*pageLabel);
    
    // Page input box
    *pageInput = new QLineEdit(toolbar);
    (*pageInput)->setFixedWidth(60);
    (*pageInput)->setAlignment(Qt::AlignCenter);
    (*pageInput)->setText("1");
    (*pageInput)->setToolTip("Enter page number and press Enter (" + panelName + ")");
    (*pageInput)->setStyleSheet(
        "QLineEdit {"
        "    border: 1px solid #cccccc;"
        "    border-radius: 3px;"
        "    padding: 2px 4px;"
        "    font-size: 11px;"
        "    background-color: white;"
        "    font-weight: bold;"
        "}"
        "QLineEdit:focus {"
        "    border-color: #4285f4;"
        "    outline: none;"
        "}"
    );
    connect(*pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    toolbar->addWidget(*pageInput);
    
    // Total pages label
    *totalPagesLabel = new QLabel("/ 0", toolbar);
    (*totalPagesLabel)->setStyleSheet("QLabel { color: #666666; margin: 0 5px; }");
    toolbar->addWidget(*totalPagesLabel);
    
    // Previous page button
    *previousPageAction = toolbar->addAction(QIcon(":/icons/images/icons/previous.svg"), "");
    (*previousPageAction)->setToolTip("Previous Page (" + panelName + ")");
    connect(*previousPageAction, &QAction::triggered, this, &PDFViewerWidget::previousPage);
    
    // Next page button
    *nextPageAction = toolbar->addAction(QIcon(":/icons/images/icons/next.svg"), "");
    (*nextPageAction)->setToolTip("Next Page (" + panelName + ")");
    connect(*nextPageAction, &QAction::triggered, this, &PDFViewerWidget::nextPage);
    
    // Add separator
    toolbar->addSeparator();
    
    // Add zoom in button
    *zoomInAction = toolbar->addAction(QIcon(":/icons/images/icons/zoom_in.svg"), "");
    (*zoomInAction)->setToolTip("Zoom In (" + panelName + ")");
    connect(*zoomInAction, &QAction::triggered, this, &PDFViewerWidget::zoomIn);
    
    // Add zoom out button
    *zoomOutAction = toolbar->addAction(QIcon(":/icons/images/icons/zoom_out.svg"), "");
    (*zoomOutAction)->setToolTip("Zoom Out (" + panelName + ")");
    connect(*zoomOutAction, &QAction::triggered, this, &PDFViewerWidget::zoomOut);
    
    // Add separator
    toolbar->addSeparator();
    
    // Search label
    *searchLabel = new QLabel("Search:", toolbar);
    (*searchLabel)->setStyleSheet("QLabel { color: #333333; font-weight: bold; margin: 0 5px; }");
    toolbar->addWidget(*searchLabel);
    
    // Search input box
    *searchInput = new QLineEdit(toolbar);
    (*searchInput)->setFixedWidth(120);
    (*searchInput)->setPlaceholderText("Search text...");
    (*searchInput)->setToolTip("Enter search term and press Enter (" + panelName + ")");
    toolbar->addWidget(*searchInput);
}

void PDFViewerWidget::syncToolbarStates()
{
    if (!isPDFLoaded()) {
        return;
    }
    
    int currentPage = getCurrentPage();
    int totalPages = getPageCount();
    
    // Update page information in both toolbars
    if (m_leftPageInput) {
        m_leftPageInput->setText(QString::number(currentPage));
    }
    if (m_leftTotalPagesLabel) {
        m_leftTotalPagesLabel->setText(QString("/ %1").arg(totalPages));
    }
    
    if (m_rightPageInput) {
        m_rightPageInput->setText(QString::number(currentPage));
    }
    if (m_rightTotalPagesLabel) {
        m_rightTotalPagesLabel->setText(QString("/ %1").arg(totalPages));
    }
    
    // Update button states
    bool canGoPrevious = currentPage > 1;
    bool canGoNext = currentPage < totalPages;
    
    if (m_leftActionPreviousPage) {
        m_leftActionPreviousPage->setEnabled(canGoPrevious);
    }
    if (m_leftActionNextPage) {
        m_leftActionNextPage->setEnabled(canGoNext);
    }
    
    if (m_rightActionPreviousPage) {
        m_rightActionPreviousPage->setEnabled(canGoPrevious);
    }
    if (m_rightActionNextPage) {
        m_rightActionNextPage->setEnabled(canGoNext);
    }
}



void PDFViewerWidget::updateViewer()
{
    if (m_pdfEmbedder && m_viewerInitialized) {
        m_pdfEmbedder->update();
        
        // Check for state changes and emit signals
        if (isPDFLoaded()) {
            int currentPage = getCurrentPage();
            int pageCount = getPageCount();
            double zoomLevel = getCurrentZoom();
            
            // Update page display
            if (m_pageInput && m_totalPagesLabel) {
                QString currentPageText = QString::number(currentPage);
                QString currentInputText = m_pageInput->text();
                
                // Only update the input if:
                // 1. Input doesn't have focus (user isn't actively typing)
                // 2. OR navigation is in progress (programmatic navigation via buttons/goToPage)
                // But DON'T update if user has focus and is manually typing
                bool userIsTyping = m_pageInput->hasFocus() && !m_navigationInProgress;
                bool shouldUpdate = !userIsTyping && (currentInputText != currentPageText);
                
                if (shouldUpdate) {
                    m_pageInput->setText(currentPageText);
                    qDebug() << "PDFViewerWidget: Page input updated from" << currentInputText << "to" << currentPageText
                             << "(focus:" << m_pageInput->hasFocus() << ", navigation:" << m_navigationInProgress << ")";
                } else if (userIsTyping) {
                    // Debug when user is actively typing - don't update
                    qDebug() << "PDFViewerWidget: User is typing, not updating page input. Current page:" << currentPage << ", Input:" << currentInputText;
                }
                
                m_totalPagesLabel->setText(QString("/ %1").arg(pageCount));
                
                // Don't reset navigation flag here - let the timer handle it
            }
            
            // Update button states
            if (m_actionPreviousPage) {
                m_actionPreviousPage->setEnabled(currentPage > 1);
            }
            if (m_actionNextPage) {
                m_actionNextPage->setEnabled(currentPage < pageCount);
            }
            
            // Update search button states
            if (m_actionFindPrevious && m_actionFindNext && m_searchInput) {
                bool hasSearchTerm = !m_searchInput->text().trimmed().isEmpty();
                m_actionFindPrevious->setEnabled(hasSearchTerm);
                m_actionFindNext->setEnabled(hasSearchTerm);
            }
            
            // Emit signals if values changed
            emit pageChanged(currentPage, pageCount);
            emit zoomChanged(zoomLevel);
        }
    }
}

void PDFViewerWidget::onPageInputChanged()
{
    if (!m_pageInput) return;
    
    QString inputText = m_pageInput->text().trimmed();
    qDebug() << "PDFViewerWidget: onPageInputChanged called with input:" << inputText;
    
    bool ok;
    int pageNumber = inputText.toInt(&ok);
    
    if (ok && pageNumber > 0) {
        qDebug() << "PDFViewerWidget: Valid page number entered:" << pageNumber;
        goToPage(pageNumber);
    } else {
        qDebug() << "PDFViewerWidget: Invalid page input:" << inputText;
        // Invalid input, reset to current page
        if (isPDFLoaded()) {
            int currentPage = getCurrentPage();
            m_pageInput->setText(QString::number(currentPage));
            qDebug() << "PDFViewerWidget: Reset to current page:" << currentPage;
        } else {
            m_pageInput->setText("1");
            qDebug() << "PDFViewerWidget: Reset to page 1 (no PDF loaded)";
        }
    }
    
    // Clear focus from page input after processing Enter key
    m_pageInput->clearFocus();
    qDebug() << "PDFViewerWidget: Page input focus cleared after Enter";
}

void PDFViewerWidget::onSlipTabClicked()
{
    // Toggle between single view and split view
    if (m_isSplitView) {
        // Switch to single view
        m_rightPanel->hide();
        m_isSplitView = false;
        
        // Show main toolbar and hide individual toolbars
        m_toolbar->show();
        m_leftToolbar->hide();
        m_rightToolbar->hide();
        
        // Update tooltip
        if (m_actionSlipTab) {
            m_actionSlipTab->setToolTip("Split View");
        }
        
        qDebug() << "PDFViewerWidget: Switched to single view mode";
    } else {
        // Switch to split view
        m_rightPanel->show();
        m_isSplitView = true;
        
        // Hide main toolbar and show left toolbar (always visible in split mode)
        m_toolbar->hide();
        m_leftToolbar->show();
        
        // Only show right toolbar if a PDF is loaded in the right panel
        if (m_rightPdfLoaded) {
            m_rightToolbar->show();
            qDebug() << "PDFViewerWidget: Right toolbar shown (PDF loaded in right panel)";
        } else {
            m_rightToolbar->hide();
            qDebug() << "PDFViewerWidget: Right toolbar hidden (no PDF in right panel)";
        }
        
        // Set equal sizes for both panels
        m_splitter->setSizes({400, 400});
        
        // Update tooltip
        if (m_actionSlipTab) {
            m_actionSlipTab->setToolTip("Single View");
        }
        
        // Sync the page information to both toolbars
        syncToolbarStates();
        
        qDebug() << "PDFViewerWidget: Switched to split view mode";
    }
    
    // Force resize if PDF viewer is initialized
    if (m_viewerInitialized && m_pdfEmbedder) {
        // Get the current size of the left container and notify the PDF embedder
        QSize leftSize = m_leftViewerContainer->size();
        m_pdfEmbedder->resize(leftSize.width(), leftSize.height());
    }
}

// Page navigation functionality connected to PDF embedder
void PDFViewerWidget::goToPage(int pageNumber)
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        // Validate page number range
        int totalPages = getPageCount();
        if (pageNumber >= 1 && pageNumber <= totalPages) {
            // Set flag to indicate programmatic navigation
            m_navigationInProgress = true;
            
            // Start timer to reset navigation flag after delay
            m_navigationTimer->start();
            
            m_pdfEmbedder->goToPage(pageNumber);
            
            // Update the page input to reflect the actual page
            if (m_pageInput) {
                m_pageInput->setText(QString::number(pageNumber));
            }
            
            qDebug() << "PDFViewerWidget: Go to page" << pageNumber;
        } else {
            qWarning() << "PDFViewerWidget: Invalid page number" << pageNumber << "- must be between 1 and" << totalPages;
            
            // Reset the input to current page
            if (m_pageInput) {
                m_pageInput->setText(QString::number(getCurrentPage()));
            }
        }
    }
}

void PDFViewerWidget::nextPage()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        int currentPage = getCurrentPage();
        int totalPages = getPageCount();
        
        qDebug() << "PDFViewerWidget: Next page called - Current:" << currentPage << "Total:" << totalPages;
        
        // Clear focus from page input when navigation button is clicked
        if (m_pageInput && m_pageInput->hasFocus()) {
            m_pageInput->clearFocus();
            qDebug() << "PDFViewerWidget: Page input focus cleared on Next button click";
        }
        
        if (currentPage < totalPages) {
            // Set flag to indicate programmatic navigation
            m_navigationInProgress = true;
            
            // Start timer to reset navigation flag after delay
            m_navigationTimer->start();
            
            m_pdfEmbedder->nextPage();
            
            qDebug() << "PDFViewerWidget: Next page triggered";
        } else {
            qDebug() << "PDFViewerWidget: Already on last page";
        }
    }
}

void PDFViewerWidget::previousPage()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        int currentPage = getCurrentPage();
        
        qDebug() << "PDFViewerWidget: Previous page called - Current:" << currentPage;
        
        // Clear focus from page input when navigation button is clicked
        if (m_pageInput && m_pageInput->hasFocus()) {
            m_pageInput->clearFocus();
            qDebug() << "PDFViewerWidget: Page input focus cleared on Previous button click";
        }
        
        if (currentPage > 1) {
            // Set flag to indicate programmatic navigation
            m_navigationInProgress = true;
            
            // Start timer to reset navigation flag after delay
            m_navigationTimer->start();
            
            m_pdfEmbedder->previousPage();
            
            qDebug() << "PDFViewerWidget: Previous page triggered";
        } else {
            qDebug() << "PDFViewerWidget: Already on first page";
        }
    }
}

// Zoom functionality connected to PDF embedder
void PDFViewerWidget::zoomIn()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        // Clear focus from page input when zoom button is clicked
        if (m_pageInput && m_pageInput->hasFocus()) {
            m_pageInput->clearFocus();
            qDebug() << "PDFViewerWidget: Page input focus cleared on Zoom In button click";
        }
        
        m_pdfEmbedder->zoomIn();
        qDebug() << "PDFViewerWidget: Zoom in triggered";
    }
}

void PDFViewerWidget::zoomOut()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        // Clear focus from page input when zoom button is clicked
        if (m_pageInput && m_pageInput->hasFocus()) {
            m_pageInput->clearFocus();
            qDebug() << "PDFViewerWidget: Page input focus cleared on Zoom Out button click";
        }
        
        m_pdfEmbedder->zoomOut();
        qDebug() << "PDFViewerWidget: Zoom out triggered";
    }
}

// Rotation functionality connected to PDF embedder
void PDFViewerWidget::rotateLeft()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        m_pdfEmbedder->rotateLeft();
        qDebug() << "PDFViewerWidget: Rotate left triggered - all pages rotated counterclockwise";
    }
}

void PDFViewerWidget::rotateRight()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        m_pdfEmbedder->rotateRight();
        qDebug() << "PDFViewerWidget: Rotate right triggered - all pages rotated clockwise";
    }
}

// Search functionality connected to PDF embedder
void PDFViewerWidget::searchText()
{
    if (isPDFLoaded() && m_pdfEmbedder && m_searchInput) {
        QString searchTerm = m_searchInput->text().trimmed();
        if (!searchTerm.isEmpty()) {
            bool found = m_pdfEmbedder->findText(searchTerm.toStdString());
            qDebug() << "PDFViewerWidget: Search triggered for term:" << searchTerm << "- Found:" << found;
        }
    }
}

void PDFViewerWidget::findNext()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        m_pdfEmbedder->findNext();
        qDebug() << "PDFViewerWidget: Find next triggered";
    }
}

void PDFViewerWidget::findPrevious()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        m_pdfEmbedder->findPrevious();
        qDebug() << "PDFViewerWidget: Find previous triggered";
    }
}

void PDFViewerWidget::onSearchInputChanged()
{
    if (!m_searchInput)
        return;
    
    QString searchTerm = m_searchInput->text().trimmed();
    
    if (searchTerm.isEmpty()) {
        // Clear search when input is empty
        if (isPDFLoaded() && m_pdfEmbedder) {
            m_pdfEmbedder->clearSelection();
        }
    } else {
        // Start new search
        searchText();
    }
}

void PDFViewerWidget::checkForSelectedText()
{
    if (!isPDFLoaded() || !m_pdfEmbedder || !m_searchInput)
        return;
    
    // Get the currently selected text from the PDF viewer
    std::string selectedTextStd = m_pdfEmbedder->getSelectedText();
    QString selectedText = QString::fromStdString(selectedTextStd);
    
    // Check if there's new selected text and it's different from the last check
    if (!selectedText.isEmpty() && selectedText != m_lastSelectedText) {
        m_lastSelectedText = selectedText;
        
        // Update the search input with the selected text (without triggering search yet)
        bool oldState = m_searchInput->blockSignals(true);  // Prevent triggering onSearchInputChanged
        m_searchInput->setText(selectedText);
        m_searchInput->blockSignals(oldState);
        
        // Trigger search with the selected text
        if (m_pdfEmbedder->findText(selectedTextStd)) {
            qDebug() << "PDFViewerWidget: Auto-searching for selected text:" << selectedText;
        }
    }
    // Reset tracking when no text is selected
    else if (selectedText.isEmpty()) {
        m_lastSelectedText.clear();
    }
}

// Event handlers
void PDFViewerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    
    if (m_pdfEmbedder && m_viewerInitialized) {
        // Use the left viewer container for PDF embedder sizing (it contains the actual PDF viewer)
        QWidget* activeContainer = m_leftViewerContainer ? m_leftViewerContainer : m_viewerContainer;
        
        if (activeContainer) {
            // Resize the embedded viewer to match the active container size
            int containerWidth = activeContainer->width();
            int containerHeight = activeContainer->height();
            
            // Only resize if container has valid dimensions
            if (containerWidth > 0 && containerHeight > 0) {
                m_pdfEmbedder->resize(containerWidth, containerHeight);
            }
        }
    }
}

void PDFViewerWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    
    // Only initialize when actually shown and has proper size
    if (!m_viewerInitialized && isVisible() && width() > 0 && height() > 0) {
        qDebug() << "PDFViewerWidget: Widget shown, initializing PDF viewer";
        // Small delay to ensure the widget is fully ready
        QTimer::singleShot(10, this, [this]() {
            if (!m_viewerInitialized && isVisible()) {
                initializePDFViewer();
            }
        });
    }
}

bool PDFViewerWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Clear page input focus when clicking in the viewer area
    if (watched == m_viewerContainer && event->type() == QEvent::MouseButtonPress) {
        if (m_pageInput && m_pageInput->hasFocus()) {
            m_pageInput->clearFocus();
            qDebug() << "PDFViewerWidget: Page input focus cleared on viewer area click";
        }
        // Also clear search input focus
        if (m_searchInput && m_searchInput->hasFocus()) {
            m_searchInput->clearFocus();
            qDebug() << "PDFViewerWidget: Search input focus cleared on viewer area click";
        }
    }
    
    // Continue with default event processing
    return QWidget::eventFilter(watched, event);
}
