#include "viewers/pdf/QtPDFViewerWidget.h"
#include "PDFViewerEmbedder.h"

#include <QResizeEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QSplitter>
#include <QGroupBox>

QtPDFViewerWidget::QtPDFViewerWidget(QWidget *parent)
    : QWidget(parent)
    , m_pdfEmbedder(std::make_unique<PDFViewerEmbedder>())
    , m_mainLayout(nullptr)
    , m_toolbarLayout(nullptr)
    , m_viewerContainer(nullptr)
    , m_toolbar(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_lastPageCount(0)
    , m_lastZoomLevel(1.0f)
    , m_lastCurrentPage(1)
{
    setupUI();
    
    // Set up the update timer for the PDF viewer
    m_updateTimer->setInterval(16); // ~60 FPS
    connect(m_updateTimer, &QTimer::timeout, this, &QtPDFViewerWidget::updateViewer);
    
    // Set minimum size
    setMinimumSize(400, 300);
}

QtPDFViewerWidget::~QtPDFViewerWidget()
{
    if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
    
    if (m_pdfEmbedder) {
        m_pdfEmbedder->shutdown();
    }
}

void QtPDFViewerWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(2, 2, 2, 2);
    m_mainLayout->setSpacing(2);
    
    // Create toolbar
    setupControlsToolbar();
    
    // Create viewer container
    setupViewerArea();
    
    // Add to main layout
    m_mainLayout->addWidget(m_toolbar);
    m_mainLayout->addWidget(m_viewerContainer, 1); // Give viewer area all remaining space
}

void QtPDFViewerWidget::setupControlsToolbar()
{
    m_toolbar = new QWidget();
    m_toolbar->setFixedHeight(40);
    m_toolbar->setStyleSheet(
        "QWidget {"
        "    background-color: #f0f0f0;"
        "    border: 1px solid #d0d0d0;"
        "    border-radius: 3px;"
        "}"
        "QPushButton {"
        "    background-color: #ffffff;"
        "    border: 1px solid #c0c0c0;"
        "    border-radius: 3px;"
        "    padding: 4px 8px;"
        "    min-width: 20px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #e8f0fe;"
        "    border-color: #4285f4;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #4285f4;"
        "    color: white;"
        "}"
        "QPushButton:disabled {"
        "    background-color: #f5f5f5;"
        "    color: #a0a0a0;"
        "    border-color: #d5d5d5;"
        "}"
    );
    
    m_toolbarLayout = new QHBoxLayout(m_toolbar);
    m_toolbarLayout->setContentsMargins(5, 5, 5, 5);
    m_toolbarLayout->setSpacing(5);
    
    // Page navigation controls
    m_prevPageBtn = new QPushButton("◀");
    m_prevPageBtn->setToolTip("Previous Page");
    m_prevPageBtn->setEnabled(false);
    connect(m_prevPageBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::previousPage);
    
    m_nextPageBtn = new QPushButton("▶");
    m_nextPageBtn->setToolTip("Next Page");
    m_nextPageBtn->setEnabled(false);
    connect(m_nextPageBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::nextPage);
    
    m_pageSpinBox = new QSpinBox();
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setEnabled(false);
    m_pageSpinBox->setFixedWidth(60);
    connect(m_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &QtPDFViewerWidget::onPageSpinBoxChanged);
    
    m_pageCountLabel = new QLabel("/ 0");
    m_pageCountLabel->setMinimumWidth(40);
    
    // Zoom controls
    m_zoomOutBtn = new QPushButton("−");
    m_zoomOutBtn->setToolTip("Zoom Out");
    m_zoomOutBtn->setEnabled(false);
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::zoomOut);
    
    m_zoomInBtn = new QPushButton("+");
    m_zoomInBtn->setToolTip("Zoom In");
    m_zoomInBtn->setEnabled(false);
    connect(m_zoomInBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::zoomIn);
    
    m_zoomFitBtn = new QPushButton("Fit");
    m_zoomFitBtn->setToolTip("Zoom to Fit");
    m_zoomFitBtn->setEnabled(false);
    connect(m_zoomFitBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::zoomToFit);
    
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(20, 500); // 20% to 500%
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(100);
    m_zoomSlider->setEnabled(false);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &QtPDFViewerWidget::onZoomSliderChanged);
    
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setMinimumWidth(40);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    
    // Search controls
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search text...");
    m_searchEdit->setFixedWidth(150);
    m_searchEdit->setEnabled(false);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &QtPDFViewerWidget::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &QtPDFViewerWidget::onSearchReturnPressed);
    
    m_searchPrevBtn = new QPushButton("◀");
    m_searchPrevBtn->setToolTip("Previous Search Result");
    m_searchPrevBtn->setEnabled(false);
    connect(m_searchPrevBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::findPrevious);
    
    m_searchNextBtn = new QPushButton("▶");
    m_searchNextBtn->setToolTip("Next Search Result");
    m_searchNextBtn->setEnabled(false);
    connect(m_searchNextBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::findNext);
    
    m_clearSelectionBtn = new QPushButton("Clear");
    m_clearSelectionBtn->setToolTip("Clear Selection");
    m_clearSelectionBtn->setEnabled(false);
    connect(m_clearSelectionBtn, &QPushButton::clicked, this, &QtPDFViewerWidget::clearSelection);
    
    // Add widgets to toolbar layout
    m_toolbarLayout->addWidget(m_prevPageBtn);
    m_toolbarLayout->addWidget(m_nextPageBtn);
    m_toolbarLayout->addWidget(m_pageSpinBox);
    m_toolbarLayout->addWidget(m_pageCountLabel);
    
    m_toolbarLayout->addWidget(new QLabel(" | ")); // Separator
    
    m_toolbarLayout->addWidget(m_zoomOutBtn);
    m_toolbarLayout->addWidget(m_zoomInBtn);
    m_toolbarLayout->addWidget(m_zoomFitBtn);
    m_toolbarLayout->addWidget(m_zoomSlider);
    m_toolbarLayout->addWidget(m_zoomLabel);
    
    m_toolbarLayout->addWidget(new QLabel(" | ")); // Separator
    
    m_toolbarLayout->addWidget(new QLabel("Search:"));
    m_toolbarLayout->addWidget(m_searchEdit);
    m_toolbarLayout->addWidget(m_searchPrevBtn);
    m_toolbarLayout->addWidget(m_searchNextBtn);
    m_toolbarLayout->addWidget(m_clearSelectionBtn);
    
    m_toolbarLayout->addStretch(); // Push everything to the left
}

void QtPDFViewerWidget::setupViewerArea()
{
    m_viewerContainer = new QWidget();
    m_viewerContainer->setStyleSheet(
        "QWidget {"
        "    background-color: #ffffff;"
        "    border: 1px solid #d0d0d0;"
        "    border-radius: 3px;"
        "}"
    );
    
    // The PDF viewer will be embedded as a child window of this container
}

bool QtPDFViewerWidget::loadPDF(const QString& filePath)
{
    if (!QFileInfo::exists(filePath)) {
        QMessageBox::warning(this, "File Error", 
            QString("PDF file does not exist:\n%1").arg(filePath));
        return false;
    }
    
    // Initialize the PDF embedder if not already done
    if (!m_viewerInitialized) {
        // Get the native Windows handle of the viewer container
        HWND containerHwnd = reinterpret_cast<HWND>(m_viewerContainer->winId());
        
        if (!m_pdfEmbedder->initialize(containerHwnd, m_viewerContainer->width(), m_viewerContainer->height())) {
            QMessageBox::critical(this, "Initialization Error", 
                "Failed to initialize PDF viewer.");
            return false;
        }
        
        m_viewerInitialized = true;
        
        // Start the update timer
        m_updateTimer->start();
    }
    
    // Load the PDF
    if (!m_pdfEmbedder->loadPDF(filePath.toStdString())) {
        QMessageBox::critical(this, "Load Error", 
            QString("Failed to load PDF file:\n%1").arg(filePath));
        return false;
    }
    
    m_currentFilePath = filePath;
    
    // Update controls state
    updateControlsState();
    
    // Emit signal
    emit pdfLoaded(filePath, getPageCount());
    
    qDebug() << "QtPDFViewerWidget: Successfully loaded PDF:" << filePath;
    return true;
}

bool QtPDFViewerWidget::isPDFLoaded() const
{
    return m_pdfEmbedder && m_pdfEmbedder->isPDFLoaded();
}

int QtPDFViewerWidget::getPageCount() const
{
    if (!isPDFLoaded()) return 0;
    return m_pdfEmbedder->getPageCount();
}

float QtPDFViewerWidget::getCurrentZoom() const
{
    if (!isPDFLoaded()) return 1.0f;
    return m_pdfEmbedder->getCurrentZoom();
}

int QtPDFViewerWidget::getCurrentPage() const
{
    if (!isPDFLoaded()) return 1;
    return m_pdfEmbedder->getCurrentPage();
}

// Navigation slots
void QtPDFViewerWidget::zoomIn()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomIn();
        updateControlsState();
    }
}

void QtPDFViewerWidget::zoomOut()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomOut();
        updateControlsState();
    }
}

void QtPDFViewerWidget::zoomToFit()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->zoomToFit();
        updateControlsState();
    }
}

void QtPDFViewerWidget::goToPage(int pageNumber)
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->goToPage(pageNumber);
        updateControlsState();
    }
}

void QtPDFViewerWidget::nextPage()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->nextPage();
        updateControlsState();
    }
}

void QtPDFViewerWidget::previousPage()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->previousPage();
        updateControlsState();
    }
}

// Search slots
void QtPDFViewerWidget::findText(const QString& searchTerm)
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->findText(searchTerm.toStdString());
    }
}

void QtPDFViewerWidget::findNext()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->findNext();
    }
}

void QtPDFViewerWidget::findPrevious()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->findPrevious();
    }
}

void QtPDFViewerWidget::clearSelection()
{
    if (isPDFLoaded()) {
        m_pdfEmbedder->clearSelection();
    }
}

// Event handlers
void QtPDFViewerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    
    if (m_viewerInitialized && m_viewerContainer) {
        // Resize the embedded PDF viewer
        m_pdfEmbedder->resize(m_viewerContainer->width(), m_viewerContainer->height());
    }
}

void QtPDFViewerWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
}

void QtPDFViewerWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    
    // Ensure proper focus for the PDF viewer
    if (m_viewerInitialized) {
        m_pdfEmbedder->setFocus();
    }
}

// Private slots
void QtPDFViewerWidget::updateViewer()
{
    if (m_viewerInitialized) {
        m_pdfEmbedder->update();
        
        // Check if state has changed and update controls if needed
        if (isPDFLoaded()) {
            int currentPageCount = getPageCount();
            float currentZoom = getCurrentZoom();
            int currentPage = getCurrentPage();
            
            if (currentPageCount != m_lastPageCount || 
                std::abs(currentZoom - m_lastZoomLevel) > 0.01f ||
                currentPage != m_lastCurrentPage) {
                
                updateControlsState();
                
                if (currentPageCount != m_lastPageCount) {
                    emit pageChanged(currentPage, currentPageCount);
                }
                if (std::abs(currentZoom - m_lastZoomLevel) > 0.01f) {
                    emit zoomChanged(currentZoom);
                }
                
                m_lastPageCount = currentPageCount;
                m_lastZoomLevel = currentZoom;
                m_lastCurrentPage = currentPage;
            }
        }
    }
}

void QtPDFViewerWidget::onZoomSliderChanged(int value)
{
    if (!isPDFLoaded()) return;
    
    float zoomLevel = value / 100.0f;
    // Set zoom level in PDF viewer - you may need to add this method to PDFViewerEmbedder
    // For now, we'll use the existing zoom methods
    
    updateControlsState();
}

void QtPDFViewerWidget::onPageSpinBoxChanged(int value)
{
    if (!isPDFLoaded()) return;
    
    goToPage(value);
}

void QtPDFViewerWidget::onSearchTextChanged()
{
    QString searchText = m_searchEdit->text();
    if (!searchText.isEmpty() && isPDFLoaded()) {
        findText(searchText);
    }
}

void QtPDFViewerWidget::onSearchReturnPressed()
{
    if (isPDFLoaded()) {
        findNext();
    }
}

void QtPDFViewerWidget::updateControlsState()
{
    bool pdfLoaded = isPDFLoaded();
    int pageCount = getPageCount();
    int currentPage = getCurrentPage();
    float zoomLevel = getCurrentZoom();
    
    // Enable/disable controls based on PDF state
    m_prevPageBtn->setEnabled(pdfLoaded && currentPage > 1);
    m_nextPageBtn->setEnabled(pdfLoaded && currentPage < pageCount);
    m_pageSpinBox->setEnabled(pdfLoaded);
    m_zoomInBtn->setEnabled(pdfLoaded);
    m_zoomOutBtn->setEnabled(pdfLoaded);
    m_zoomFitBtn->setEnabled(pdfLoaded);
    m_zoomSlider->setEnabled(pdfLoaded);
    m_searchEdit->setEnabled(pdfLoaded);
    m_searchPrevBtn->setEnabled(pdfLoaded);
    m_searchNextBtn->setEnabled(pdfLoaded);
    m_clearSelectionBtn->setEnabled(pdfLoaded);
    
    if (pdfLoaded) {
        // Update page controls
        m_pageSpinBox->setMaximum(pageCount);
        m_pageSpinBox->setValue(currentPage);
        m_pageCountLabel->setText(QString("/ %1").arg(pageCount));
        
        // Update zoom controls
        int zoomPercent = static_cast<int>(zoomLevel * 100);
        m_zoomSlider->setValue(zoomPercent);
        m_zoomLabel->setText(QString("%1%").arg(zoomPercent));
    } else {
        // Reset controls for no PDF
        m_pageSpinBox->setMaximum(1);
        m_pageSpinBox->setValue(1);
        m_pageCountLabel->setText("/ 0");
        m_zoomSlider->setValue(100);
        m_zoomLabel->setText("100%");
    }
}
