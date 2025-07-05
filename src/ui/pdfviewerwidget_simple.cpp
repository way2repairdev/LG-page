#include "ui/pdfviewerwidget_simple.h"
#include "rendering/pdf-render-stub.h"
#include <QApplication>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QWheelEvent>
#include <QPixmap>
#include <QPainter>
#include <QDebug>

PDFViewerWidget::PDFViewerWidget(QWidget *parent)
    : QWidget(parent)
    , m_renderer(nullptr)
    , m_isPDFLoaded(false)
    , m_currentPage(0)
    , m_pageCount(0)
    , m_zoomLevel(DEFAULT_ZOOM)
    , m_renderTimer(new QTimer(this))
{
    setupUI();
    createContextMenu();
    
    // Set up render timer
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(100); // Small delay for smooth updates
    connect(m_renderTimer, &QTimer::timeout, this, &PDFViewerWidget::updateRender);
}

PDFViewerWidget::~PDFViewerWidget()
{
    // Clean up
}

void PDFViewerWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    setupToolbar();
    setupSearchBar();
    setupPageDisplay();
    
    setLayout(m_mainLayout);
}

void PDFViewerWidget::setupToolbar()
{
    m_toolbarWidget = new QWidget();
    m_toolbarWidget->setFixedHeight(TOOLBAR_HEIGHT);
    m_toolbarWidget->setStyleSheet(
        "QWidget {"
        "    background-color: #f8f9ff;"
        "    border-bottom: 1px solid #d4e1f5;"
        "}"
        "QPushButton {"
        "    background-color: transparent;"
        "    border: 1px solid transparent;"
        "    border-radius: 3px;"
        "    padding: 4px 8px;"
        "    margin: 2px;"
        "    font-family: 'Segoe UI';"
        "}"
        "QPushButton:hover {"
        "    background-color: #e8f0fe;"
        "    border-color: #4285f4;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #4285f4;"
        "    color: white;"
        "}"
    );
    
    QHBoxLayout *toolbarLayout = new QHBoxLayout(m_toolbarWidget);
    toolbarLayout->setContentsMargins(5, 5, 5, 5);
    
    // Navigation buttons
    m_firstPageBtn = new QPushButton("⏮");
    m_firstPageBtn->setToolTip("First Page");
    connect(m_firstPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToFirstPage);
    
    m_prevPageBtn = new QPushButton("◀");
    m_prevPageBtn->setToolTip("Previous Page");
    connect(m_prevPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::previousPage);
    
    m_pageInput = new QLineEdit();
    m_pageInput->setFixedWidth(50);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setToolTip("Current Page");
    connect(m_pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    
    m_pageCountLabel = new QLabel("/ 0");
    
    m_nextPageBtn = new QPushButton("▶");
    m_nextPageBtn->setToolTip("Next Page");
    connect(m_nextPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::nextPage);
    
    m_lastPageBtn = new QPushButton("⏭");
    m_lastPageBtn->setToolTip("Last Page");
    connect(m_lastPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToLastPage);
    
    // Separator
    QFrame *separator1 = new QFrame();
    separator1->setFrameShape(QFrame::VLine);
    separator1->setFrameShadow(QFrame::Sunken);
    
    // Zoom controls
    m_zoomOutBtn = new QPushButton("-");
    m_zoomOutBtn->setToolTip("Zoom Out");
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomOut);
    
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setMinimum(static_cast<int>(MIN_ZOOM * 100));
    m_zoomSlider->setMaximum(static_cast<int>(MAX_ZOOM * 100));
    m_zoomSlider->setValue(static_cast<int>(DEFAULT_ZOOM * 100));
    m_zoomSlider->setFixedWidth(100);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &PDFViewerWidget::onZoomSliderChanged);
    
    m_zoomInBtn = new QPushButton("+");
    m_zoomInBtn->setToolTip("Zoom In");
    connect(m_zoomInBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomIn);
    
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setFixedWidth(40);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    
    m_zoomFitBtn = new QPushButton("Fit Page");
    connect(m_zoomFitBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToFit);
    
    m_zoomWidthBtn = new QPushButton("Fit Width");
    connect(m_zoomWidthBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToWidth);
    
    // Separator
    QFrame *separator2 = new QFrame();
    separator2->setFrameShape(QFrame::VLine);
    separator2->setFrameShadow(QFrame::Sunken);
    
    // Search button
    m_searchBtn = new QPushButton("🔍 Search");
    m_searchBtn->setToolTip("Search in Document");
    connect(m_searchBtn, &QPushButton::clicked, this, &PDFViewerWidget::startSearch);
    
    // Add widgets to toolbar layout
    toolbarLayout->addWidget(m_firstPageBtn);
    toolbarLayout->addWidget(m_prevPageBtn);
    toolbarLayout->addWidget(m_pageInput);
    toolbarLayout->addWidget(m_pageCountLabel);
    toolbarLayout->addWidget(m_nextPageBtn);
    toolbarLayout->addWidget(m_lastPageBtn);
    toolbarLayout->addWidget(separator1);
    toolbarLayout->addWidget(m_zoomOutBtn);
    toolbarLayout->addWidget(m_zoomSlider);
    toolbarLayout->addWidget(m_zoomInBtn);
    toolbarLayout->addWidget(m_zoomLabel);
    toolbarLayout->addWidget(m_zoomFitBtn);
    toolbarLayout->addWidget(m_zoomWidthBtn);
    toolbarLayout->addWidget(separator2);
    toolbarLayout->addWidget(m_searchBtn);
    toolbarLayout->addStretch();
    
    m_mainLayout->addWidget(m_toolbarWidget);
}

void PDFViewerWidget::setupSearchBar()
{
    m_searchWidget = new QWidget();
    m_searchWidget->setFixedHeight(SEARCH_BAR_HEIGHT);
    m_searchWidget->setStyleSheet(
        "QWidget {"
        "    background-color: #fffacd;"
        "    border-bottom: 1px solid #ddd;"
        "}"
    );
    m_searchWidget->hide(); // Initially hidden
    
    QHBoxLayout *searchLayout = new QHBoxLayout(m_searchWidget);
    searchLayout->setContentsMargins(5, 5, 5, 5);
    
    QLabel *searchLabel = new QLabel("Search:");
    
    m_searchInput = new QLineEdit();
    m_searchInput->setPlaceholderText("Enter search term...");
    connect(m_searchInput, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchTextChanged);
    connect(m_searchInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onSearchNext);
    
    m_searchPrevButton = new QPushButton("◀");
    m_searchPrevButton->setToolTip("Previous Result");
    connect(m_searchPrevButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchPrevious);
    
    m_searchNextButton = new QPushButton("▶");
    m_searchNextButton->setToolTip("Next Result");
    connect(m_searchNextButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchNext);
    
    m_searchResultsLabel = new QLabel("");
    m_searchResultsLabel->setMinimumWidth(80);
    
    m_caseSensitiveCheck = new QCheckBox("Case");
    m_caseSensitiveCheck->setToolTip("Case Sensitive");
    connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleCaseSensitive);
    
    m_wholeWordsCheck = new QCheckBox("Whole");
    m_wholeWordsCheck->setToolTip("Whole Words");
    connect(m_wholeWordsCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleWholeWords);
    
    m_closeSearchButton = new QPushButton("✕");
    m_closeSearchButton->setFixedSize(20, 20);
    m_closeSearchButton->setToolTip("Close Search");
    connect(m_closeSearchButton, &QPushButton::clicked, this, &PDFViewerWidget::clearSearch);
    
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(m_searchInput);
    searchLayout->addWidget(m_searchPrevButton);
    searchLayout->addWidget(m_searchNextButton);
    searchLayout->addWidget(m_searchResultsLabel);
    searchLayout->addWidget(m_caseSensitiveCheck);
    searchLayout->addWidget(m_wholeWordsCheck);
    searchLayout->addStretch();
    searchLayout->addWidget(m_closeSearchButton);
    
    m_mainLayout->addWidget(m_searchWidget);
}

void PDFViewerWidget::setupPageDisplay()
{
    m_scrollArea = new QScrollArea();
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setStyleSheet(
        "QScrollArea {"
        "    border: none;"
        "    background-color: #f0f0f0;"
        "}"
    );
    
    m_pageLabel = new QLabel();
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setText("No PDF loaded");
    m_pageLabel->setStyleSheet(
        "QLabel {"
        "    background-color: white;"
        "    border: 1px solid #ccc;"
        "    padding: 20px;"
        "    font-size: 16px;"
        "    color: #666;"
        "}"
    );
    
    m_scrollArea->setWidget(m_pageLabel);
    m_mainLayout->addWidget(m_scrollArea);
}

void PDFViewerWidget::createContextMenu()
{
    m_contextMenu = new QMenu(this);
    
    m_zoomInAction = m_contextMenu->addAction("Zoom In");
    connect(m_zoomInAction, &QAction::triggered, this, &PDFViewerWidget::zoomIn);
    
    m_zoomOutAction = m_contextMenu->addAction("Zoom Out");
    connect(m_zoomOutAction, &QAction::triggered, this, &PDFViewerWidget::zoomOut);
    
    m_contextMenu->addSeparator();
    
    m_zoomFitAction = m_contextMenu->addAction("Fit to Page");
    connect(m_zoomFitAction, &QAction::triggered, this, &PDFViewerWidget::zoomToFit);
    
    m_zoomWidthAction = m_contextMenu->addAction("Fit to Width");
    connect(m_zoomWidthAction, &QAction::triggered, this, &PDFViewerWidget::zoomToWidth);
    
    m_contextMenu->addSeparator();
    
    m_searchAction = m_contextMenu->addAction("Search...");
    connect(m_searchAction, &QAction::triggered, this, &PDFViewerWidget::startSearch);
}

bool PDFViewerWidget::loadPDF(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        emit errorOccurred("File does not exist or is not readable: " + filePath);
        return false;
    }
    
    if (fileInfo.suffix().toLower() != "pdf") {
        emit errorOccurred("File is not a PDF: " + filePath);
        return false;
    }
    
    try {
        // Initialize PDF renderer if not already done
        if (!m_renderer) {
            m_renderer = std::make_unique<PDFRenderer>();
            m_renderer->Initialize();
        }
        
        // Load the PDF document
        std::string stdFilePath = filePath.toStdString();
        if (!m_renderer->LoadDocument(stdFilePath)) {
            emit errorOccurred("Failed to load PDF document: " + filePath);
            return false;
        }
        
        // Get page count
        m_pageCount = m_renderer->GetPageCount();
        if (m_pageCount <= 0) {
            emit errorOccurred("PDF document has no pages: " + filePath);
            return false;
        }
        
        // Set up initial state
        m_filePath = filePath;
        m_isPDFLoaded = true;
        m_currentPage = 0;
        m_zoomLevel = DEFAULT_ZOOM;
        
        // Update UI
        updatePageUI();
        updateZoomUI();
        renderCurrentPage();
        
        emit pdfLoaded(filePath);
        emit pageChanged(m_currentPage + 1, m_pageCount);
        emit zoomChanged(m_zoomLevel);
        
        return true;
        
    } catch (const std::exception &e) {
        emit errorOccurred(QString("Exception while loading PDF: %1").arg(e.what()));
        return false;
    } catch (...) {
        emit errorOccurred("Unknown error while loading PDF");
        return false;
    }
}

void PDFViewerWidget::closePDF()
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    m_isPDFLoaded = false;
    m_filePath.clear();
    m_currentPage = 0;
    m_pageCount = 0;
    
    // Update UI
    updatePageUI();
    m_pageLabel->setText("No PDF loaded");
    
    // Hide search if visible
    if (m_searchWidget->isVisible()) {
        clearSearch();
    }
    
    emit pdfClosed();
}

void PDFViewerWidget::renderCurrentPage()
{
    if (!m_isPDFLoaded || !m_renderer) {
        return;
    }
    
    QPixmap pixmap = renderPageToPixmap(m_currentPage);
    if (!pixmap.isNull()) {
        m_pageLabel->setPixmap(pixmap);
        m_pageLabel->resize(pixmap.size());
    }
}

QPixmap PDFViewerWidget::renderPageToPixmap(int pageIndex)
{
    if (!m_renderer || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QPixmap();
    }
    
    // Get page size
    int width, height;
    PDFRenderer::SimpleBitmap bitmap = m_renderer->RenderPageToBitmap(pageIndex, width, height, false);
    
    if (bitmap.width == 0 || bitmap.height == 0) {
        return QPixmap();
    }
    
    // Scale according to zoom level
    int scaledWidth = static_cast<int>(bitmap.width * m_zoomLevel);
    int scaledHeight = static_cast<int>(bitmap.height * m_zoomLevel);
    
    // Create QImage from bitmap data
    QImage image(bitmap.data.data(), bitmap.width, bitmap.height, QImage::Format_RGBA8888);
    
    // Scale the image
    QImage scaledImage = image.scaled(scaledWidth, scaledHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    return QPixmap::fromImage(scaledImage);
}

void PDFViewerWidget::updatePageUI()
{
    if (m_isPDFLoaded) {
        m_pageInput->setText(QString::number(m_currentPage + 1));
        m_pageCountLabel->setText(QString("/ %1").arg(m_pageCount));
        
        // Enable/disable navigation buttons
        m_firstPageBtn->setEnabled(m_currentPage > 0);
        m_prevPageBtn->setEnabled(m_currentPage > 0);
        m_nextPageBtn->setEnabled(m_currentPage < m_pageCount - 1);
        m_lastPageBtn->setEnabled(m_currentPage < m_pageCount - 1);
    } else {
        m_pageInput->setText("0");
        m_pageCountLabel->setText("/ 0");
        
        // Disable navigation buttons
        m_firstPageBtn->setEnabled(false);
        m_prevPageBtn->setEnabled(false);
        m_nextPageBtn->setEnabled(false);
        m_lastPageBtn->setEnabled(false);
    }
}

void PDFViewerWidget::updateZoomUI()
{
    m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
    m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
}

// Continue with the rest of the implementation...
// (This would include all the slot implementations and other methods)

// Navigation methods
void PDFViewerWidget::goToPage(int pageNumber)
{
    if (!m_isPDFLoaded || pageNumber < 1 || pageNumber > m_pageCount) {
        return;
    }
    
    m_currentPage = pageNumber - 1;
    updatePageUI();
    renderCurrentPage();
    
    emit pageChanged(pageNumber, m_pageCount);
}

void PDFViewerWidget::nextPage()
{
    if (m_currentPage < m_pageCount - 1) {
        goToPage(m_currentPage + 2);
    }
}

void PDFViewerWidget::previousPage()
{
    if (m_currentPage > 0) {
        goToPage(m_currentPage);
    }
}

void PDFViewerWidget::goToFirstPage()
{
    goToPage(1);
}

void PDFViewerWidget::goToLastPage()
{
    goToPage(m_pageCount);
}

// Zoom methods
void PDFViewerWidget::zoomIn()
{
    setZoomLevel(m_zoomLevel + ZOOM_STEP);
}

void PDFViewerWidget::zoomOut()
{
    setZoomLevel(m_zoomLevel - ZOOM_STEP);
}

void PDFViewerWidget::zoomToFit()
{
    calculateZoomToFit();
}

void PDFViewerWidget::zoomToWidth()
{
    calculateZoomToWidth();
}

void PDFViewerWidget::resetZoom()
{
    setZoomLevel(DEFAULT_ZOOM);
}

void PDFViewerWidget::setZoomLevel(double zoom)
{
    double newZoom = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    
    if (newZoom != m_zoomLevel) {
        m_zoomLevel = newZoom;
        updateZoomUI();
        renderCurrentPage();
        emit zoomChanged(m_zoomLevel);
    }
}

double PDFViewerWidget::getZoomLevel() const
{
    return m_zoomLevel;
}

void PDFViewerWidget::calculateZoomToFit()
{
    if (!m_isPDFLoaded || !m_renderer) {
        return;
    }
    
    // Get current page dimensions
    double pageWidth, pageHeight;
    m_renderer->GetOriginalPageSize(m_currentPage, pageWidth, pageHeight);
    
    // Get available space
    QSize availableSize = m_scrollArea->size();
    
    // Calculate zoom to fit
    double widthZoom = availableSize.width() / pageWidth;
    double heightZoom = availableSize.height() / pageHeight;
    
    setZoomLevel(std::min(widthZoom, heightZoom));
}

void PDFViewerWidget::calculateZoomToWidth()
{
    if (!m_isPDFLoaded || !m_renderer) {
        return;
    }
    
    // Get current page dimensions
    double pageWidth, pageHeight;
    m_renderer->GetOriginalPageSize(m_currentPage, pageWidth, pageHeight);
    
    // Get available width
    int availableWidth = m_scrollArea->width();
    
    setZoomLevel(availableWidth / pageWidth);
}

// Getters
bool PDFViewerWidget::isPDFLoaded() const
{
    return m_isPDFLoaded;
}

QString PDFViewerWidget::getCurrentFilePath() const
{
    return m_filePath;
}

int PDFViewerWidget::getCurrentPage() const
{
    return m_currentPage + 1;
}

int PDFViewerWidget::getPageCount() const
{
    return m_pageCount;
}

// Slot implementations
void PDFViewerWidget::onZoomSliderChanged(int value)
{
    double zoom = value / 100.0;
    if (zoom != m_zoomLevel) {
        setZoomLevel(zoom);
    }
}

void PDFViewerWidget::onPageInputChanged()
{
    bool ok;
    int pageNumber = m_pageInput->text().toInt(&ok);
    if (ok && pageNumber >= 1 && pageNumber <= m_pageCount) {
        goToPage(pageNumber);
    } else {
        // Reset to current page if invalid input
        updatePageUI();
    }
}

void PDFViewerWidget::updateRender()
{
    renderCurrentPage();
}

// Search functionality (basic implementation)
void PDFViewerWidget::startSearch()
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    m_searchWidget->show();
    m_searchInput->setFocus();
    m_searchInput->selectAll();
}

void PDFViewerWidget::clearSearch()
{
    m_searchWidget->hide();
    m_searchInput->clear();
    m_searchResultsLabel->clear();
}

void PDFViewerWidget::onSearchTextChanged()
{
    performSearch();
}

void PDFViewerWidget::onSearchNext()
{
    searchNext();
}

void PDFViewerWidget::onSearchPrevious()
{
    searchPrevious();
}

void PDFViewerWidget::onToggleCaseSensitive(bool enabled)
{
    Q_UNUSED(enabled)
    performSearch();
}

void PDFViewerWidget::onToggleWholeWords(bool enabled)
{
    Q_UNUSED(enabled)
    performSearch();
}

void PDFViewerWidget::setSearchTerm(const QString &term)
{
    m_searchInput->setText(term);
    if (!term.isEmpty()) {
        startSearch();
        performSearch();
    }
}

void PDFViewerWidget::searchNext()
{
    // Implementation placeholder
}

void PDFViewerWidget::searchPrevious()
{
    // Implementation placeholder
}

void PDFViewerWidget::performSearch()
{
    QString searchTerm = m_searchInput->text();
    if (searchTerm.isEmpty()) {
        m_searchResultsLabel->clear();
        return;
    }
    
    // Implementation placeholder
    m_searchResultsLabel->setText("Search not implemented");
}

void PDFViewerWidget::updateSearchUI()
{
    // Implementation placeholder
}

void PDFViewerWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_contextMenu) {
        m_contextMenu->exec(event->globalPos());
    }
}

void PDFViewerWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom with Ctrl+Wheel
        double zoomFactor = event->angleDelta().y() > 0 ? 1.1 : 0.9;
        setZoomLevel(m_zoomLevel * zoomFactor);
        event->accept();
    } else {
        // Let the scroll area handle normal scrolling
        QWidget::wheelEvent(event);
    }
}
