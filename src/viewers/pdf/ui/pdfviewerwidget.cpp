#include "viewers/pdf/pdfviewerwidget.h"
#include "viewers/pdf/PDFViewerEmbedder.h"

#include <QResizeEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QFocusEvent>
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
    , m_actionSlipTab(nullptr)
    , m_actionRotateLeft(nullptr)
    , m_actionRotateRight(nullptr)
    , m_actionPreviousPage(nullptr)
    , m_actionNextPage(nullptr)
    , m_actionZoomIn(nullptr)
    , m_actionZoomOut(nullptr)
    , m_actionFindPrevious(nullptr)
    , m_actionFindNext(nullptr)
    , m_pageLabel(nullptr)
    , m_pageInput(nullptr)
    , m_totalPagesLabel(nullptr)
    , m_searchLabel(nullptr)
    , m_searchInput(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_pdfLoaded(false)
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
    
    // Add viewer container to main layout (takes remaining space)
    m_mainLayout->addWidget(m_viewerContainer, 1);
    
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
    
    // Get the native Windows handle of the viewer container
    HWND containerHwnd = reinterpret_cast<HWND>(m_viewerContainer->winId());
    
    // Verify the container has a valid size
    if (m_viewerContainer->width() <= 0 || m_viewerContainer->height() <= 0) {
        qDebug() << "PDFViewerWidget: Container has invalid size, deferring initialization";
        globalInitializationInProgress = false;
        // Defer initialization until we have proper size
        QTimer::singleShot(100, this, [this]() {
            if (!m_viewerInitialized && m_viewerContainer->width() > 0 && m_viewerContainer->height() > 0) {
                initializePDFViewer();
            }
        });
        return;
    }
    
    // Initialize the embedded PDF viewer
    bool initSuccess = m_pdfEmbedder->initialize(containerHwnd, m_viewerContainer->width(), m_viewerContainer->height());
    
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
    // Note: Connect to your desired slot when you implement the functionality
    // connect(m_actionSlipTab, &QAction::triggered, this, &PDFViewerWidget::slipTabAction);
    
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
        // When editing is finished, force an update of the page display
        if (isPDFLoaded()) {
            int currentPage = getCurrentPage();
            m_pageInput->setText(QString::number(currentPage));
            qDebug() << "PDFViewerWidget: Page input focus lost, updated to current page:" << currentPage;
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
    // Create viewer container widget
    m_viewerContainer = new QWidget(this);
    m_viewerContainer->setMinimumSize(400, 300);
    m_viewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewerContainer->setStyleSheet(
        "QWidget {"
        "    background-color: #ffffff;"
        "    border: 1px solid #cccccc;"
        "}"
    );
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
                
                // Update the input in these cases:
                // 1. If the input doesn't have focus (user isn't typing)
                // 2. If navigation is in progress (programmatic navigation via buttons)
                // 3. If the page has actually changed
                bool shouldUpdate = !m_pageInput->hasFocus() || m_navigationInProgress || 
                                  (currentInputText != currentPageText);
                
                if (shouldUpdate && currentInputText != currentPageText) {
                    m_pageInput->setText(currentPageText);
                    qDebug() << "PDFViewerWidget: Page input updated from" << currentInputText << "to" << currentPageText
                             << "(focus:" << m_pageInput->hasFocus() << ", navigation:" << m_navigationInProgress << ")";
                } else if (m_pageInput->hasFocus() && !m_navigationInProgress) {
                    // Debug when input has focus but no navigation
                    qDebug() << "PDFViewerWidget: Page input has focus, not updating. Current page:" << currentPage;
                }
                
                m_totalPagesLabel->setText(QString("/ %1").arg(pageCount));
                
                // Reset navigation flag after updating
                m_navigationInProgress = false;
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
    
    bool ok;
    int pageNumber = m_pageInput->text().toInt(&ok);
    
    if (ok && pageNumber > 0) {
        goToPage(pageNumber);
    } else {
        // Invalid input, reset to current page
        if (isPDFLoaded()) {
            m_pageInput->setText(QString::number(getCurrentPage()));
        } else {
            m_pageInput->setText("1");
        }
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
        
        if (currentPage < totalPages) {
            // Set flag to indicate programmatic navigation
            m_navigationInProgress = true;
            
            m_pdfEmbedder->nextPage();
            
            // Force immediate update of page display
            QTimer::singleShot(50, this, [this]() {
                int newPage = getCurrentPage();
                qDebug() << "PDFViewerWidget: After next page, current page is:" << newPage;
                
                // Set navigation flag again to ensure update happens
                m_navigationInProgress = true;
                
                // Force update the page input immediately
                if (m_pageInput) {
                    m_pageInput->setText(QString::number(newPage));
                    qDebug() << "PDFViewerWidget: Page input manually updated to:" << newPage;
                }
            });
            
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
        
        if (currentPage > 1) {
            // Set flag to indicate programmatic navigation
            m_navigationInProgress = true;
            
            m_pdfEmbedder->previousPage();
            
            // Force immediate update of page display
            QTimer::singleShot(50, this, [this]() {
                int newPage = getCurrentPage();
                qDebug() << "PDFViewerWidget: After previous page, current page is:" << newPage;
                
                // Set navigation flag again to ensure update happens
                m_navigationInProgress = true;
                
                // Force update the page input immediately
                if (m_pageInput) {
                    m_pageInput->setText(QString::number(newPage));
                    qDebug() << "PDFViewerWidget: Page input manually updated to:" << newPage;
                }
            });
            
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
        m_pdfEmbedder->zoomIn();
        qDebug() << "PDFViewerWidget: Zoom in triggered";
    }
}

void PDFViewerWidget::zoomOut()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
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
    
    if (m_pdfEmbedder && m_viewerInitialized && m_viewerContainer) {
        // Resize the embedded viewer to match the container size
        // Use the container's actual size, not the event size, to account for layout margins
        int containerWidth = m_viewerContainer->width();
        int containerHeight = m_viewerContainer->height();
        
        // Only resize if container has valid dimensions
        if (containerWidth > 0 && containerHeight > 0) {
            m_pdfEmbedder->resize(containerWidth, containerHeight);
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
