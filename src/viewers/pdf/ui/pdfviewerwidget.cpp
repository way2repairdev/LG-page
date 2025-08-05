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
    , m_actionZoomIn(nullptr)
    , m_actionZoomOut(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_pdfLoaded(false)
    , m_usingFallback(false)
{
    setupUI();
    
    // Configure update timer for smooth rendering
    m_updateTimer->setInterval(UPDATE_INTERVAL_MS);
    m_updateTimer->setSingleShot(false);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFViewerWidget::updateViewer);
    
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
    
    // Add zoom in button (SVG icon from resources)
    m_actionZoomIn = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_in.svg"), "");
    m_actionZoomIn->setToolTip("Zoom In");
    connect(m_actionZoomIn, &QAction::triggered, this, &PDFViewerWidget::zoomIn);
    
    // Add zoom out button (SVG icon from resources)
    m_actionZoomOut = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_out.svg"), "");
    m_actionZoomOut->setToolTip("Zoom Out");
    connect(m_actionZoomOut, &QAction::triggered, this, &PDFViewerWidget::zoomOut);
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
            
            // Emit signals if values changed
            emit pageChanged(currentPage, pageCount);
            emit zoomChanged(zoomLevel);
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
