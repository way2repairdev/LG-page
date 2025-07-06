#include "ui/hybridpdfviewer.h"
#include "ui/pdfviewerwidget.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QDebug>
#include <QSplitter>
#include <QGridLayout>
#include <QSpacerItem>
#include <QFrame>
#include <QProgressBar>
#include <QTime>
#include <QPdfDocument>
#include <QPdfView>

HybridPDFViewer::HybridPDFViewer(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_toolbarLayout(nullptr)
    , m_tabWidget(nullptr)
    , m_toolbar(nullptr)
    , m_qtViewerWidget(nullptr)
    , m_qtPdfDocument(nullptr)
    , m_qtPdfView(nullptr)
    , m_qtViewerLayout(nullptr)
    , m_customPdfViewer(nullptr)
    , m_currentMode(QtNativeViewer)
    , m_isPDFLoaded(false)
    , m_currentPage(0)
    , m_pageCount(0)
    , m_zoomLevel(1.0)
    , m_performanceTimer(new QTimer(this))
    , m_frameCount(0)
    , m_lastFrameTime(0)
{
    setupUI();
    
    // Set up performance monitoring
    m_performanceTimer->setInterval(1000); // Update every second
    connect(m_performanceTimer, &QTimer::timeout, this, [this]() {
        // Performance monitoring logic can be added here
        qDebug() << "Performance stats - Mode:" << m_currentMode 
                 << "FPS:" << m_frameCount << "Page:" << m_currentPage;
        m_frameCount = 0;
    });
}

HybridPDFViewer::~HybridPDFViewer()
{
    if (m_qtPdfDocument) {
        m_qtPdfDocument->close();
    }
}

void HybridPDFViewer::setupUI()
{
    // Create main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // Setup toolbar
    setupToolbar();
    
    // Setup search controls
    setupSearchControls();
    
    // Setup tab widget
    setupTabWidget();
    
    // Create viewer tabs
    createViewerTabs();
    
    // Connect tab change signal
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &HybridPDFViewer::onTabChanged);
    
    // Initially disable controls
    enableControls(false);
}

void HybridPDFViewer::setupToolbar()
{
    // Create toolbar
    m_toolbar = new QToolBar(this);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setMovable(false);
    
    // Mode indicator
    m_modeLabel = new QLabel("Viewer Mode:", this);
    m_toolbar->addWidget(m_modeLabel);
    
    // Switch button
    m_switchButton = new QPushButton("Switch to OpenGL", this);
    m_switchButton->setToolTip("Switch between Qt native and OpenGL renderers");
    connect(m_switchButton, &QPushButton::clicked, this, &HybridPDFViewer::onSwitchViewer);
    m_toolbar->addWidget(m_switchButton);
    
    m_toolbar->addSeparator();
    
    // Page navigation
    m_prevPageButton = new QPushButton("◀", this);
    m_prevPageButton->setToolTip("Previous Page");
    connect(m_prevPageButton, &QPushButton::clicked, this, &HybridPDFViewer::previousPage);
    m_toolbar->addWidget(m_prevPageButton);
    
    m_pageInput = new QLineEdit(this);
    m_pageInput->setMaximumWidth(60);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setToolTip("Current Page");
    connect(m_pageInput, &QLineEdit::returnPressed, this, &HybridPDFViewer::onPageInputChanged);
    m_toolbar->addWidget(m_pageInput);
    
    m_pageCountLabel = new QLabel("/ 0", this);
    m_toolbar->addWidget(m_pageCountLabel);
    
    m_nextPageButton = new QPushButton("▶", this);
    m_nextPageButton->setToolTip("Next Page");
    connect(m_nextPageButton, &QPushButton::clicked, this, &HybridPDFViewer::nextPage);
    m_toolbar->addWidget(m_nextPageButton);
    
    m_toolbar->addSeparator();
    
    // Zoom controls
    m_toolbar->addWidget(new QLabel("Zoom:", this));
    
    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(10, 500); // 10% to 500%
    m_zoomSlider->setValue(100);
    m_zoomSlider->setMaximumWidth(120);
    m_zoomSlider->setToolTip("Zoom Level");
    connect(m_zoomSlider, &QSlider::valueChanged, this, &HybridPDFViewer::onZoomSliderChanged);
    m_toolbar->addWidget(m_zoomSlider);
    
    m_zoomLabel = new QLabel("100%", this);
    m_zoomLabel->setMinimumWidth(40);
    m_toolbar->addWidget(m_zoomLabel);
    
    m_toolbar->addSeparator();
    
    // Performance mode toggle
    m_performanceMode = new QCheckBox("High Performance", this);
    m_performanceMode->setToolTip("Enable high performance rendering (OpenGL mode only)");
    connect(m_performanceMode, &QCheckBox::toggled, this, &HybridPDFViewer::onPerformanceToggled);
    m_toolbar->addWidget(m_performanceMode);
    
    // Add toolbar to main layout
    m_mainLayout->addWidget(m_toolbar);
}

void HybridPDFViewer::setupSearchControls()
{
    // Create search widget
    m_searchWidget = new QWidget(this);
    m_searchWidget->setVisible(false);
    
    QHBoxLayout *searchLayout = new QHBoxLayout(m_searchWidget);
    searchLayout->setContentsMargins(5, 2, 5, 2);
    
    // Search input
    searchLayout->addWidget(new QLabel("Search:", this));
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("Enter search term...");
    connect(m_searchInput, &QLineEdit::textChanged, this, &HybridPDFViewer::onSearchTextChanged);
    searchLayout->addWidget(m_searchInput);
    
    // Search buttons
    m_searchButton = new QPushButton("Find", this);
    connect(m_searchButton, &QPushButton::clicked, this, [this]() {
        startSearch(m_searchInput->text());
    });
    searchLayout->addWidget(m_searchButton);
    
    m_searchPrevButton = new QPushButton("◀", this);
    m_searchPrevButton->setToolTip("Previous Result");
    connect(m_searchPrevButton, &QPushButton::clicked, this, &HybridPDFViewer::searchPrevious);
    searchLayout->addWidget(m_searchPrevButton);
    
    m_searchNextButton = new QPushButton("▶", this);
    m_searchNextButton->setToolTip("Next Result");
    connect(m_searchNextButton, &QPushButton::clicked, this, &HybridPDFViewer::searchNext);
    searchLayout->addWidget(m_searchNextButton);
    
    // Case sensitive option
    m_caseSensitiveCheck = new QCheckBox("Case Sensitive", this);
    searchLayout->addWidget(m_caseSensitiveCheck);
    
    // Search results label
    m_searchResultsLabel = new QLabel("", this);
    searchLayout->addWidget(m_searchResultsLabel);
    
    // Close search button
    QPushButton *closeSearchButton = new QPushButton("×", this);
    closeSearchButton->setMaximumWidth(25);
    closeSearchButton->setToolTip("Close Search");
    connect(closeSearchButton, &QPushButton::clicked, this, [this]() {
        m_searchWidget->setVisible(false);
        clearSearch();
    });
    searchLayout->addWidget(closeSearchButton);
    
    // Add to main layout
    m_mainLayout->addWidget(m_searchWidget);
}

void HybridPDFViewer::setupTabWidget()
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::North);
    m_tabWidget->setTabsClosable(false);
    m_tabWidget->setMovable(false);
    
    m_mainLayout->addWidget(m_tabWidget);
}

void HybridPDFViewer::createViewerTabs()
{
    // Create Qt PDF viewer tab
    m_qtViewerWidget = new QWidget();
    m_qtViewerLayout = new QVBoxLayout(m_qtViewerWidget);
    m_qtViewerLayout->setContentsMargins(0, 0, 0, 0);
    
    // Create Qt PDF document and view
    m_qtPdfDocument = new QPdfDocument(this);
    m_qtPdfView = new QPdfView(m_qtViewerWidget);
    m_qtPdfView->setDocument(m_qtPdfDocument);
    
    // Connect Qt PDF signals
    connect(m_qtPdfDocument, &QPdfDocument::statusChanged, this, &HybridPDFViewer::onQtPdfLoaded);
    connect(m_qtPdfDocument, &QPdfDocument::errorOccurred, this, &HybridPDFViewer::onQtPdfError);
    connect(m_qtPdfView, &QPdfView::currentPageChanged, this, &HybridPDFViewer::onQtPdfPageChanged);
    
    m_qtViewerLayout->addWidget(m_qtPdfView);
    m_tabWidget->addTab(m_qtViewerWidget, "Qt Native PDF");
    
    // Create custom OpenGL PDF viewer tab
    m_customPdfViewer = new PDFViewerWidget(this);
    
    // Connect custom PDF viewer signals
    connect(m_customPdfViewer, &PDFViewerWidget::pdfLoaded, this, &HybridPDFViewer::onCustomPdfLoaded);
    connect(m_customPdfViewer, &PDFViewerWidget::errorOccurred, this, &HybridPDFViewer::onCustomPdfError);
    connect(m_customPdfViewer, &PDFViewerWidget::pageChanged, this, &HybridPDFViewer::onCustomPageChanged);
    connect(m_customPdfViewer, &PDFViewerWidget::zoomChanged, this, &HybridPDFViewer::onCustomZoomChanged);
    
    m_tabWidget->addTab(m_customPdfViewer, "OpenGL High Performance");
    
    // Set initial tab
    m_tabWidget->setCurrentIndex(0);
}

bool HybridPDFViewer::loadPDF(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
    
    m_currentFilePath = filePath;
    
    // Load in Qt PDF viewer
    m_qtPdfDocument->load(filePath);
    
    // Load in custom PDF viewer
    bool customLoaded = m_customPdfViewer->loadPDF(filePath);
    
    if (customLoaded) {
        m_isPDFLoaded = true;
        m_pageCount = m_customPdfViewer->getPageCount();
        m_currentPage = 1;
        
        // Update UI
        enableControls(true);
        updateToolbarFromActiveViewer();
        
        emit pdfLoaded(filePath);
        return true;
    }
    
    return false;
}

void HybridPDFViewer::setViewerMode(ViewerMode mode)
{
    if (m_currentMode != mode) {
        m_currentMode = mode;
        
        // Switch to appropriate tab
        m_tabWidget->setCurrentIndex(mode == QtNativeViewer ? 0 : 1);
        
        // Update UI
        m_switchButton->setText(mode == QtNativeViewer ? "Switch to OpenGL" : "Switch to Qt Native");
        m_performanceMode->setEnabled(mode == CustomOpenGLViewer);
        
        // Sync viewer states
        syncViewerStates();
        
        emit viewerModeChanged(mode);
    }
}

void HybridPDFViewer::syncViewerStates()
{
    if (!m_isPDFLoaded) return;
    
    // Sync page numbers
    if (m_currentMode == QtNativeViewer) {
        m_qtPdfView->setCurrentPage(m_currentPage - 1); // Qt uses 0-based indexing
    } else {
        m_customPdfViewer->goToPage(m_currentPage);
    }
    
    // Sync zoom levels
    if (m_currentMode == CustomOpenGLViewer) {
        m_customPdfViewer->setZoomLevel(m_zoomLevel);
    }
}

void HybridPDFViewer::onTabChanged(int index)
{
    ViewerMode newMode = (index == 0) ? QtNativeViewer : CustomOpenGLViewer;
    setViewerMode(newMode);
}

void HybridPDFViewer::onSwitchViewer()
{
    ViewerMode newMode = (m_currentMode == QtNativeViewer) ? CustomOpenGLViewer : QtNativeViewer;
    setViewerMode(newMode);
}

void HybridPDFViewer::enableControls(bool enabled)
{
    m_prevPageButton->setEnabled(enabled);
    m_nextPageButton->setEnabled(enabled);
    m_pageInput->setEnabled(enabled);
    m_zoomSlider->setEnabled(enabled);
    m_searchButton->setEnabled(enabled);
    m_searchInput->setEnabled(enabled);
}

void HybridPDFViewer::updateToolbarFromActiveViewer()
{
    if (!m_isPDFLoaded) return;
    
    // Update page count
    m_pageCountLabel->setText(QString("/ %1").arg(m_pageCount));
    
    // Update page input
    m_pageInput->setText(QString::number(m_currentPage));
    
    // Update zoom
    m_zoomLabel->setText(QString("%1%").arg(qRound(m_zoomLevel * 100)));
    m_zoomSlider->setValue(qRound(m_zoomLevel * 100));
}

// Implement other slot functions...
void HybridPDFViewer::onQtPdfLoaded() { /* Implementation */ }
void HybridPDFViewer::onQtPdfError(QPdfDocument::Error error) { /* Implementation */ }
void HybridPDFViewer::onQtPdfPageChanged(int page) { /* Implementation */ }
void HybridPDFViewer::onCustomPdfLoaded(const QString &filePath) { /* Implementation */ }
void HybridPDFViewer::onCustomPdfError(const QString &error) { /* Implementation */ }
void HybridPDFViewer::onCustomPageChanged(int currentPage, int totalPages) { /* Implementation */ }
void HybridPDFViewer::onCustomZoomChanged(double zoomLevel) { /* Implementation */ }

// Implement navigation functions...
void HybridPDFViewer::nextPage()
{
    if (m_currentPage < m_pageCount) {
        goToPage(m_currentPage + 1);
    }
}

void HybridPDFViewer::previousPage()
{
    if (m_currentPage > 1) {
        goToPage(m_currentPage - 1);
    }
}

void HybridPDFViewer::goToPage(int pageNumber)
{
    if (pageNumber >= 1 && pageNumber <= m_pageCount) {
        m_currentPage = pageNumber;
        
        if (m_currentMode == QtNativeViewer) {
            m_qtPdfView->setCurrentPage(pageNumber - 1);
        } else {
            m_customPdfViewer->goToPage(pageNumber);
        }
        
        updateToolbarFromActiveViewer();
        emit pageChanged(m_currentPage, m_pageCount);
    }
}

void HybridPDFViewer::zoomIn()
{
    if (m_currentMode == CustomOpenGLViewer) {
        m_customPdfViewer->zoomIn();
    } else {
        m_qtPdfView->setZoomFactor(m_qtPdfView->zoomFactor() * 1.25);
    }
}

void HybridPDFViewer::zoomOut()
{
    if (m_currentMode == CustomOpenGLViewer) {
        m_customPdfViewer->zoomOut();
    } else {
        m_qtPdfView->setZoomFactor(m_qtPdfView->zoomFactor() / 1.25);
    }
}

void HybridPDFViewer::startSearch(const QString &searchTerm)
{
    if (!m_isPDFLoaded || searchTerm.isEmpty()) return;
    
    m_currentSearchTerm = searchTerm;
    m_searchWidget->setVisible(true);
    
    if (m_currentMode == CustomOpenGLViewer) {
        m_customPdfViewer->setSearchTerm(searchTerm);
        m_customPdfViewer->startSearch();
    } else {
        // Qt PDF search implementation would go here
        // Note: Qt PDF search API may vary by version
    }
}

// Implement remaining functions...
void HybridPDFViewer::closePDF() { /* Implementation */ }
bool HybridPDFViewer::isPDFLoaded() const { return m_isPDFLoaded; }
QString HybridPDFViewer::getCurrentFilePath() const { return m_currentFilePath; }
HybridPDFViewer::ViewerMode HybridPDFViewer::getViewerMode() const { return m_currentMode; }
int HybridPDFViewer::getCurrentPage() const { return m_currentPage; }
int HybridPDFViewer::getPageCount() const { return m_pageCount; }
void HybridPDFViewer::zoomToFit() { /* Implementation */ }
void HybridPDFViewer::resetZoom() { /* Implementation */ }
void HybridPDFViewer::searchNext() { /* Implementation */ }
void HybridPDFViewer::searchPrevious() { /* Implementation */ }
void HybridPDFViewer::clearSearch() { /* Implementation */ }
void HybridPDFViewer::onZoomSliderChanged(int value) { /* Implementation */ }
void HybridPDFViewer::onPageInputChanged() { /* Implementation */ }
void HybridPDFViewer::onSearchTextChanged() { /* Implementation */ }
void HybridPDFViewer::onPerformanceToggled(bool enabled) { /* Implementation */ }
