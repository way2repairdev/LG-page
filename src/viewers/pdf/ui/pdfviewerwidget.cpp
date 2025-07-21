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
#include <fstream>

// Enhanced debug logging to file for PDFViewerWidget
void WriteQtDebugToFile(const QString& message) {
    static std::ofstream debugFile;
    static bool fileInitialized = false;
    
    if (!fileInitialized) {
        std::string logPath = "C:/Users/Rathe/Downloads/login/login/LG-page/build/pdf_debug.txt";
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
    , m_toolbarLayout(nullptr)
    , m_viewerContainer(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_pdfLoaded(false)
    , m_usingFallback(false)
    , m_toolbarVisible(true)
    , m_lastPageCount(0)
    , m_lastZoomLevel(1.0)
    , m_lastCurrentPage(1)
{
    setupUI();
    
    // Configure update timer for smooth rendering
    m_updateTimer->setInterval(UPDATE_INTERVAL_MS);
    m_updateTimer->setSingleShot(false);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFViewerWidget::updateViewer);
    
    // Set widget properties
    setMinimumSize(400, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Initialize as not ready until PDF is loaded
    updateToolbarState();
    
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
    
    // Setup toolbar and viewer area
    setupToolbar();
    setupViewerArea();
    
    // Add components to main layout
    m_mainLayout->addWidget(m_toolbar);
    m_mainLayout->addWidget(m_viewerContainer, 1); // Give viewer container all remaining space
    
    // Apply modern styling
    setStyleSheet(
        "PDFViewerWidget {"
        "    background-color: #f5f5f5;"
        "    border: 1px solid #d0d0d0;"
        "}"
    );
}

void PDFViewerWidget::setupToolbar()
{
    m_toolbar = new QWidget();
    m_toolbar->setFixedHeight(TOOLBAR_HEIGHT);
    m_toolbar->setStyleSheet(
        "QWidget {"
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, "
        "                                stop: 0 #f8f9fa, stop: 1 #e9ecef);"
        "    border-bottom: 1px solid #d0d0d0;"
        "}"
        "QPushButton {"
        "    background-color: #ffffff;"
        "    border: 1px solid #ced4da;"
        "    border-radius: 4px;"
        "    padding: 6px 12px;"
        "    font-size: 12px;"
        "    font-weight: 500;"
        "    color: #495057;"
        "    min-width: 24px;"
        "    min-height: 24px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #e8f0fe;"
        "    border-color: #4285f4;"
        "    color: #1a73e8;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #1a73e8;"
        "    color: white;"
        "}"
        "QPushButton:disabled {"
        "    background-color: #f8f9fa;"
        "    color: #6c757d;"
        "    border-color: #e9ecef;"
        "}"
        "QLineEdit {"
        "    border: 1px solid #ced4da;"
        "    border-radius: 4px;"
        "    padding: 6px 8px;"
        "    font-size: 12px;"
        "    background-color: white;"
        "}"
        "QLineEdit:focus {"
        "    border-color: #4285f4;"
        "    outline: none;"
        "}"
    );
    
    m_toolbarLayout = new QHBoxLayout(m_toolbar);
    m_toolbarLayout->setContentsMargins(8, 6, 8, 6);
    m_toolbarLayout->setSpacing(6);
    
    // Page navigation group
    m_prevPageBtn = new QPushButton("◀");
    m_prevPageBtn->setToolTip("Previous Page (Ctrl+Left)");
    m_prevPageBtn->setShortcut(QKeySequence("Ctrl+Left"));
    connect(m_prevPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::previousPage);
    
    m_nextPageBtn = new QPushButton("▶");
    m_nextPageBtn->setToolTip("Next Page (Ctrl+Right)");
    m_nextPageBtn->setShortcut(QKeySequence("Ctrl+Right"));
    connect(m_nextPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::nextPage);
    
    m_pageSpinBox = new QSpinBox();
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setFixedWidth(60);
    m_pageSpinBox->setToolTip("Go to Page");
    connect(m_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &PDFViewerWidget::onPageSpinBoxChanged);
    
    m_pageLabel = new QLabel("/ 0 pages");
    m_pageLabel->setMinimumWidth(60);
    m_pageLabel->setStyleSheet("QLabel { color: #6c757d; font-size: 12px; }");
    
    // Zoom controls group
    m_zoomOutBtn = new QPushButton("−");
    m_zoomOutBtn->setToolTip("Zoom Out (Ctrl+-)");
    m_zoomOutBtn->setShortcut(QKeySequence("Ctrl+-"));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomOut);
    
    m_zoomInBtn = new QPushButton("+");
    m_zoomInBtn->setToolTip("Zoom In (Ctrl++)");
    m_zoomInBtn->setShortcut(QKeySequence("Ctrl++"));
    connect(m_zoomInBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomIn);
    
    m_zoomFitBtn = new QPushButton("Fit");
    m_zoomFitBtn->setToolTip("Zoom to Fit (Ctrl+0)");
    m_zoomFitBtn->setShortcut(QKeySequence("Ctrl+0"));
    connect(m_zoomFitBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToFit);
    
    // Search controls group
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search in document...");
    m_searchEdit->setFixedWidth(180);
    m_searchEdit->setToolTip("Search Text (Ctrl+F)");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &PDFViewerWidget::findNext);
    
    m_searchPrevBtn = new QPushButton("◀");
    m_searchPrevBtn->setToolTip("Previous Result (Shift+F3)");
    m_searchPrevBtn->setShortcut(QKeySequence("Shift+F3"));
    connect(m_searchPrevBtn, &QPushButton::clicked, this, &PDFViewerWidget::findPrevious);
    
    m_searchNextBtn = new QPushButton("▶");
    m_searchNextBtn->setToolTip("Next Result (F3)");
    m_searchNextBtn->setShortcut(QKeySequence("F3"));
    connect(m_searchNextBtn, &QPushButton::clicked, this, &PDFViewerWidget::findNext);
    
    m_clearBtn = new QPushButton("✕");
    m_clearBtn->setToolTip("Clear Selection (Esc)");
    m_clearBtn->setShortcut(QKeySequence("Esc"));
    connect(m_clearBtn, &QPushButton::clicked, this, &PDFViewerWidget::clearSelection);
    
    // Add separators and widgets to toolbar
    m_toolbarLayout->addWidget(m_prevPageBtn);
    m_toolbarLayout->addWidget(m_nextPageBtn);
    m_toolbarLayout->addWidget(m_pageSpinBox);
    m_toolbarLayout->addWidget(m_pageLabel);
    
    // Add separator
    QLabel* sep1 = new QLabel("|");
    sep1->setStyleSheet("QLabel { color: #d0d0d0; margin: 0 4px; }");
    m_toolbarLayout->addWidget(sep1);
    
    m_toolbarLayout->addWidget(m_zoomOutBtn);
    m_toolbarLayout->addWidget(m_zoomInBtn);
    m_toolbarLayout->addWidget(m_zoomFitBtn);
    
    // Add separator
    QLabel* sep2 = new QLabel("|");
    sep2->setStyleSheet("QLabel { color: #d0d0d0; margin: 0 4px; }");
    m_toolbarLayout->addWidget(sep2);
    
    m_toolbarLayout->addWidget(new QLabel("Search:"));
    m_toolbarLayout->addWidget(m_searchEdit);
    m_toolbarLayout->addWidget(m_searchPrevBtn);
    m_toolbarLayout->addWidget(m_searchNextBtn);
    m_toolbarLayout->addWidget(m_clearBtn);
    
    m_toolbarLayout->addStretch(); // Push remaining widgets to the right
}

void PDFViewerWidget::setupViewerArea()
{
    m_viewerContainer = new QWidget();
    m_viewerContainer->setStyleSheet(
        "QWidget {"
        "    background-color: #ffffff;"
        "    border: none;"
        "}"
    );
    
    // Set size policy to expand
    m_viewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // The embedded PDF viewer will be created as a child of this container when needed
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
    
    WriteQtDebugToFile("Step 6: Updating toolbar and display...");
    // Update UI
    updateToolbarState();
    updatePageDisplay();
    updateZoomDisplay();
    
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
    if (m_viewerInitialized) {
        return;
    }
    
    // Get the native Windows handle of the viewer container
    HWND containerHwnd = reinterpret_cast<HWND>(m_viewerContainer->winId());
    
    // Initialize the embedded PDF viewer
    if (!m_pdfEmbedder->initialize(containerHwnd, m_viewerContainer->width(), m_viewerContainer->height())) {
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

// Navigation slots
void PDFViewerWidget::zoomIn()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomIn();
        updateZoomDisplay();
        emit zoomChanged(getCurrentZoom());
    }
}

void PDFViewerWidget::zoomOut()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomOut();
        updateZoomDisplay();
        emit zoomChanged(getCurrentZoom());
    }
}

void PDFViewerWidget::zoomToFit()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomToFit();
        updateZoomDisplay();
        emit zoomChanged(getCurrentZoom());
    }
}

void PDFViewerWidget::goToPage(int pageNumber)
{
    if (isPDFLoaded() && pageNumber >= 1 && pageNumber <= getPageCount()) {
        m_pdfEmbedder->goToPage(pageNumber);
        updatePageDisplay();
        emit pageChanged(getCurrentPage(), getPageCount());
    }
}

void PDFViewerWidget::nextPage()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->nextPage();
        updatePageDisplay();
        emit pageChanged(getCurrentPage(), getPageCount());
    }
}

void PDFViewerWidget::previousPage()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->previousPage();
        updatePageDisplay();
        emit pageChanged(getCurrentPage(), getPageCount());
    }
}

// Search slots
void PDFViewerWidget::findText(const QString& searchTerm)
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->findText(searchTerm.toStdString());
        m_searchEdit->setText(searchTerm);
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
    // Toggle toolbar visibility for full screen mode
    toggleControls(!fullScreen);
}

void PDFViewerWidget::toggleControls(bool visible)
{
    m_toolbarVisible = visible;
    m_toolbar->setVisible(visible);
}

// Event handlers
void PDFViewerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    
    if (m_viewerInitialized && m_viewerContainer) {
        // Resize the embedded PDF viewer to match the container
        m_pdfEmbedder->resize(m_viewerContainer->width(), m_viewerContainer->height());
    }
}

void PDFViewerWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    // The embedded viewer handles its own painting
}

void PDFViewerWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    
    // Ensure the embedded viewer gets focus when shown
    if (m_viewerInitialized) {
        m_pdfEmbedder->setFocus();
    }
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

// Private slots
void PDFViewerWidget::updateViewer()
{
    if (m_viewerInitialized) {
        // Update the embedded PDF viewer (this drives the rendering loop)
        m_pdfEmbedder->update();
        
        // Check for state changes and update UI accordingly
        if (isPDFLoaded()) {
            int currentPageCount = getPageCount();
            double currentZoom = getCurrentZoom();
            int currentPage = getCurrentPage();
            
            // Update displays if values have changed
            if (currentPageCount != m_lastPageCount || 
                std::abs(currentZoom - m_lastZoomLevel) > 0.01 ||
                currentPage != m_lastCurrentPage) {
                
                updatePageDisplay();
                updateZoomDisplay();
                
                // Emit signals for significant changes
                if (currentPageCount != m_lastPageCount) {
                    emit pageChanged(currentPage, currentPageCount);
                }
                if (std::abs(currentZoom - m_lastZoomLevel) > 0.01) {
                    emit zoomChanged(currentZoom);
                }
                
                m_lastPageCount = currentPageCount;
                m_lastZoomLevel = currentZoom;
                m_lastCurrentPage = currentPage;
            }
        }
    }
}

void PDFViewerWidget::onToolbarButtonClicked()
{
    // Handle generic toolbar button clicks
    updateToolbarState();
}

void PDFViewerWidget::onPageSpinBoxChanged(int value)
{
    goToPage(value);
}

void PDFViewerWidget::onSearchTextChanged()
{
    QString searchText = m_searchEdit->text();
    if (!searchText.isEmpty() && isPDFLoaded()) {
        findText(searchText);
    }
}

void PDFViewerWidget::onSearchButtonClicked()
{
    QString searchText = m_searchEdit->text();
    if (!searchText.isEmpty()) {
        findNext();
    }
}

// Private helper methods
void PDFViewerWidget::updateToolbarState()
{
    bool pdfLoaded = isPDFLoaded();
    int pageCount = getPageCount();
    int currentPage = getCurrentPage();
    
    // Enable/disable navigation controls
    m_prevPageBtn->setEnabled(pdfLoaded && currentPage > 1);
    m_nextPageBtn->setEnabled(pdfLoaded && currentPage < pageCount);
    m_pageSpinBox->setEnabled(pdfLoaded);
    
    // Enable/disable zoom controls
    m_zoomInBtn->setEnabled(pdfLoaded);
    m_zoomOutBtn->setEnabled(pdfLoaded);
    m_zoomFitBtn->setEnabled(pdfLoaded);
    
    // Enable/disable search controls
    m_searchEdit->setEnabled(pdfLoaded);
    m_searchPrevBtn->setEnabled(pdfLoaded);
    m_searchNextBtn->setEnabled(pdfLoaded);
    m_clearBtn->setEnabled(pdfLoaded);
}

void PDFViewerWidget::updatePageDisplay()
{
    if (isPDFLoaded()) {
        int pageCount = getPageCount();
        int currentPage = getCurrentPage();
        
        m_pageSpinBox->setMaximum(pageCount);
        m_pageSpinBox->setValue(currentPage);
        m_pageLabel->setText(QString("/ %1 pages").arg(pageCount));
    } else {
        m_pageSpinBox->setMaximum(1);
        m_pageSpinBox->setValue(1);
        m_pageLabel->setText("/ 0 pages");
    }
}

void PDFViewerWidget::updateZoomDisplay()
{
    // No zoom display widgets to update since we removed zoom slider and label
    // Zoom functionality is still available through buttons and keyboard shortcuts
}
