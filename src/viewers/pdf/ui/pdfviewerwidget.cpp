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
#include <QStyle>
#include <QSizePolicy>
#include <QTimer>
#include <QThread>
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
    , m_viewerContainer(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_selectionTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_pdfLoaded(false)
    , m_usingFallback(false)
    , m_lastPageCount(0)
    , m_lastZoomLevel(1.0)
    , m_lastCurrentPage(1)
    , m_lastSelectedText("")
{
    setupUI();
    
    // Configure update timer for smooth rendering
    m_updateTimer->setInterval(UPDATE_INTERVAL_MS);
    m_updateTimer->setSingleShot(false);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFViewerWidget::updateViewer);
    
    // Configure selection timer to check for selected text
    m_selectionTimer->setInterval(500); // Check every 500ms
    m_selectionTimer->setSingleShot(false);
    connect(m_selectionTimer, &QTimer::timeout, this, &PDFViewerWidget::checkForSelectedText);
    

    
    qDebug() << "PDFViewerWidget: Created with advanced embedded renderer and Qt fallback";
}

PDFViewerWidget::~PDFViewerWidget()
{
    qDebug() << "PDFViewerWidget: Destructor called";
    
    // Stop the update timer
    if (m_updateTimer && m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
    
    // Stop the selection timer
    if (m_selectionTimer && m_selectionTimer->isActive()) {
        m_selectionTimer->stop();
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
    
    // Setup viewer area only
    setupViewerArea();
    
    // Add viewer container to main layout (takes full space)
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
    
    WriteQtDebugToFile("Step 6: Updating display...");
    // Update state only (no toolbar)
    
    // Start the selection timer to check for selected text
    if (!m_selectionTimer->isActive()) {
        m_selectionTimer->start();
        WriteQtDebugToFile("Started selection timer to monitor selected text");
    }
    
    WriteQtDebugToFile("Step 7: PDF loading completed successfully");
    
    WriteQtDebugToFile("Step 8: Emitting signals...");
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




// Search slots
void PDFViewerWidget::findText(const QString& searchTerm)
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->findText(searchTerm.toStdString());
    }
}

void PDFViewerWidget::findNext()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->findNext();
    }
}

void PDFViewerWidget::findPrevious()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->findPrevious();
    }
}

void PDFViewerWidget::clearSelection()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->clearSelection();
    }
}

void PDFViewerWidget::setFullScreen(bool fullScreen)
{
    // Fullscreen mode - no toolbar to toggle
    Q_UNUSED(fullScreen)
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

void PDFViewerWidget::checkForSelectedText()
{
    if (!isPDFLoaded() || !m_pdfEmbedder) {
        return;
    }
    
    // Get currently selected text from the embedded viewer
    QString selectedText = QString::fromStdString(m_pdfEmbedder->getSelectedText());
    
    // Store the current selected text for next comparison
    m_lastSelectedText = selectedText;
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

// Navigation slots
void PDFViewerWidget::zoomIn()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomIn();
    }
}

void PDFViewerWidget::zoomOut()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomOut();
    }
}

void PDFViewerWidget::goToPage(int pageNumber)
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->goToPage(pageNumber);
    }
}

void PDFViewerWidget::nextPage()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->nextPage();
    }
}

void PDFViewerWidget::previousPage()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->previousPage();
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
            
            // Emit signals if values changed
            if (currentPage != m_lastCurrentPage || pageCount != m_lastPageCount) {
                m_lastCurrentPage = currentPage;
                m_lastPageCount = pageCount;
                emit pageChanged(currentPage, pageCount);
            }
            
            if (std::abs(zoomLevel - m_lastZoomLevel) > 0.01) {
                m_lastZoomLevel = zoomLevel;
                emit zoomChanged(zoomLevel);
            }
        }
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
