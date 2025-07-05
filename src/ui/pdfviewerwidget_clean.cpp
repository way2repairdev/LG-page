#include "ui/pdfviewerwidget.h"
#include "ui/pdfscrollstate.h"
#include "ui/textsearch.h"
#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QFileDialog>
#include <QProgressDialog>
#include <QDebug>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSpacerItem>
#include <QFrame>
#include <QContextMenuEvent>
#include <QMatrix4x4>
#include <QTimer>
#include <cmath>

// Include PDF viewer components - use the original high-quality renderer
#include "rendering/pdf-render.h"
#include "core/feature.h"

// Helper to convert PDFium bitmap to OpenGL texture (from original PDF viewer)
GLuint CreateTextureFromPDFBitmap(FPDF_BITMAP bitmap, int width, int height) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    void* buffer = FPDFBitmap_GetBuffer(bitmap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}

PDFViewerWidget::PDFViewerWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_renderer(nullptr)
    , m_scrollState(nullptr)
    , m_textSearch(nullptr)
    , m_shaderProgram(nullptr)
    , m_toolbarWidget(nullptr)
    , m_toolbar(nullptr)
    , m_zoomSlider(nullptr)
    , m_zoomLabel(nullptr)
    , m_pageInput(nullptr)
    , m_pageCountLabel(nullptr)
    , m_searchWidget(nullptr)
    , m_searchInput(nullptr)
    , m_searchNextButton(nullptr)
    , m_searchPrevButton(nullptr)
    , m_caseSensitiveCheck(nullptr)
    , m_wholeWordsCheck(nullptr)
    , m_searchResultsLabel(nullptr)
    , m_closeSearchButton(nullptr)
    , m_contextMenu(nullptr)
    , m_isPDFLoaded(false)
    , m_currentPage(0)
    , m_pageCount(0)
    , m_zoomLevel(DEFAULT_ZOOM)
    , m_isDragging(false)
    , m_renderTimer(new QTimer(this))
    , m_viewportWidth(0)
    , m_viewportHeight(0)
    , m_scrollOffsetY(0.0f)
    , m_scrollOffsetX(0.0f)
    , m_maxScrollY(0.0f)
    , m_maxScrollX(0.0f)
{
    // Set focus policy for keyboard events
    setFocusPolicy(Qt::StrongFocus);
    
    // Set up render timer
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(16); // ~60 FPS
    connect(m_renderTimer, &QTimer::timeout, this, &PDFViewerWidget::updateRender);
    
    // Initialize UI
    setupUI();
    createContextMenu();
    
    // Enable mouse tracking for hover effects
    setMouseTracking(true);
}

PDFViewerWidget::~PDFViewerWidget()
{
    // Clean up OpenGL resources
    makeCurrent();
    
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    
    delete m_shaderProgram;
    doneCurrent();
}

// Use original PDF viewer's high-quality rendering approach
void PDFViewerWidget::updateTextures()
{
    if (!m_renderer || !m_isPDFLoaded) {
        return;
    }
    
    // Check if we have a valid OpenGL context
    if (!context() || !context()->isValid()) {
        qWarning() << "PDFViewerWidget: Invalid OpenGL context in updateTextures()";
        return;
    }
    
    // Clean up existing textures
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    
    m_pageWidths.clear();
    m_pageHeights.clear();
    
    // Create textures for all pages using original PDF viewer's approach
    m_pageTextures.resize(m_pageCount);
    m_pageWidths.resize(m_pageCount);
    m_pageHeights.resize(m_pageCount);
    
    for (int i = 0; i < m_pageCount; ++i) {
        int width, height;
        
        // Use original PDF viewer's high-quality rendering
        FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(i, width, height, true); // Always high quality
        
        if (bitmap) {
            m_pageWidths[i] = width;
            m_pageHeights[i] = height;
            
            // Use original PDF viewer's texture creation function
            m_pageTextures[i] = CreateTextureFromPDFBitmap(bitmap, width, height);
            
            FPDFBitmap_Destroy(bitmap);
        } else {
            m_pageWidths[i] = 0;
            m_pageHeights[i] = 0;
            m_pageTextures[i] = 0;
        }
    }
    
    calculatePageLayout();
}

// Use original PDF viewer's rendering approach
void PDFViewerWidget::renderPDF()
{
    if (!m_renderer || !m_isPDFLoaded || m_pageTextures.empty()) {
        return;
    }
    
    // Set up OpenGL state like the original PDF viewer
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    
    // Set up orthographic projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_viewportWidth, m_viewportHeight, 0, -1, 1);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // Calculate visible page range using original PDF viewer's approach
    int firstVisible = 0, lastVisible = m_pageCount - 1;
    GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    
    // Render each visible page using original PDF viewer's approach
    float currentY = -m_scrollOffsetY;
    
    for (int i = firstVisible; i <= lastVisible && i < m_pageCount; ++i) {
        if (i >= 0 && i < static_cast<int>(m_pageTextures.size()) && m_pageTextures[i] != 0) {
            
            // Get page dimensions
            float pageWidth = m_pageWidths[i] * m_zoomLevel;
            float pageHeight = m_pageHeights[i] * m_zoomLevel;
            
            // Calculate page position (centered horizontally)
            float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
            float pageY = currentY;
            
            // Skip if page is not visible
            if (pageY + pageHeight >= 0 && pageY <= m_viewportHeight) {
                // Render page texture using original PDF viewer's method
                glBindTexture(GL_TEXTURE_2D, m_pageTextures[i]);
                
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(pageX, pageY);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(pageX + pageWidth, pageY);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(pageX + pageWidth, pageY + pageHeight);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(pageX, pageY + pageHeight);
                glEnd();
                
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            
            currentY += pageHeight + PAGE_MARGIN;
        }
    }
    
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

// The rest of the implementation will use the original methods but integrated with Qt...
// I'll add the essential methods that are missing

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
    
    // Close any existing PDF
    closePDF();
    
    try {
        // Initialize PDF renderer if not already done
        if (!m_renderer) {
            initializePDFRenderer();
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
        m_scrollOffsetX = 0.0f;
        m_scrollOffsetY = 0.0f;
        
        // Initialize scroll state using original PDF viewer's approach
        if (!m_scrollState) {
            m_scrollState = std::make_unique<PDFScrollState>();
        }
        m_scrollState->scrollOffset = 0.0f;
        m_scrollState->zoomScale = m_zoomLevel;
        m_scrollState->viewportWidth = m_viewportWidth;
        m_scrollState->viewportHeight = m_viewportHeight;
        
        // Update UI
        m_pageInput->setText("1");
        m_pageCountLabel->setText(QString("/ %1").arg(m_pageCount));
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
        
        // Check if OpenGL context is ready
        if (!context() || !context()->isValid()) {
            // If context is not ready, defer the OpenGL operations
            QTimer::singleShot(50, this, [this]() {
                if (m_isPDFLoaded) {
                    makeCurrent();
                    updateTextures();
                    update();
                }
            });
        } else {
            // Trigger OpenGL update
            makeCurrent();
            updateTextures();
            update();
        }
        
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

// Add all the missing essential methods here...
// I'll implement the key ones to get this working

void PDFViewerWidget::initializePDFRenderer()
{
    if (m_renderer) {
        return;
    }
    
    m_renderer = std::make_unique<PDFRenderer>();
    m_renderer->Initialize();
}

void PDFViewerWidget::initializeGL()
{
    // Check if we have a valid OpenGL context
    if (!context() || !context()->isValid()) {
        qWarning() << "PDFViewerWidget: Invalid OpenGL context in initializeGL()";
        return;
    }
    
    initializeOpenGLFunctions();
    
    // Set clear color
    glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
}

void PDFViewerWidget::resizeGL(int w, int h)
{
    m_viewportWidth = w;
    m_viewportHeight = h - TOOLBAR_HEIGHT - (m_searchWidget && m_searchWidget->isVisible() ? SEARCH_BAR_HEIGHT : 0);
    
    glViewport(0, 0, w, h);
    
    if (m_isPDFLoaded) {
        calculatePageLayout();
        update();
    }
}

void PDFViewerWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (!m_isPDFLoaded) {
        return;
    }
    
    renderPDF();
}

// Add minimal implementations for the missing methods
void PDFViewerWidget::setupUI() { /* Implement toolbar and search bar setup */ }
void PDFViewerWidget::setupToolbar() { /* Implement toolbar setup */ }
void PDFViewerWidget::setupSearchBar() { /* Implement search bar setup */ }
void PDFViewerWidget::createContextMenu() { /* Implement context menu */ }
void PDFViewerWidget::closePDF() { /* Implement PDF cleanup */ }
void PDFViewerWidget::calculatePageLayout() { /* Implement page layout calculation */ }
void PDFViewerWidget::updateRender() { if (m_isPDFLoaded) update(); }
void PDFViewerWidget::setZoomLevel(double zoom) { m_zoomLevel = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM); }
double PDFViewerWidget::getZoomLevel() const { return m_zoomLevel; }
bool PDFViewerWidget::isPDFLoaded() const { return m_isPDFLoaded; }
QString PDFViewerWidget::getCurrentFilePath() const { return m_filePath; }
int PDFViewerWidget::getCurrentPage() const { return m_currentPage + 1; }
int PDFViewerWidget::getPageCount() const { return m_pageCount; }

// Navigation methods
void PDFViewerWidget::goToPage(int pageNumber) { /* Implement page navigation */ }
void PDFViewerWidget::nextPage() { /* Implement next page */ }
void PDFViewerWidget::previousPage() { /* Implement previous page */ }
void PDFViewerWidget::goToFirstPage() { /* Implement go to first page */ }
void PDFViewerWidget::goToLastPage() { /* Implement go to last page */ }

// Zoom methods
void PDFViewerWidget::zoomIn() { /* Implement zoom in */ }
void PDFViewerWidget::zoomOut() { /* Implement zoom out */ }
void PDFViewerWidget::zoomToFit() { /* Implement zoom to fit */ }
void PDFViewerWidget::zoomToWidth() { /* Implement zoom to width */ }
void PDFViewerWidget::resetZoom() { /* Implement reset zoom */ }

// Search methods
void PDFViewerWidget::startSearch() { /* Implement start search */ }
void PDFViewerWidget::searchNext() { /* Implement search next */ }
void PDFViewerWidget::searchPrevious() { /* Implement search previous */ }
void PDFViewerWidget::setSearchTerm(const QString &term) { /* Implement set search term */ }
void PDFViewerWidget::clearSearch() { /* Implement clear search */ }

// Slot methods
void PDFViewerWidget::onPageInputChanged() { /* Implement page input changed */ }
void PDFViewerWidget::onZoomSliderChanged(int value) { /* Implement zoom slider changed */ }
void PDFViewerWidget::onSearchTextChanged(const QString &text) { /* Implement search text changed */ }
void PDFViewerWidget::onSearchNext() { /* Implement search next */ }
void PDFViewerWidget::onSearchPrevious() { /* Implement search previous */ }
void PDFViewerWidget::onToggleCaseSensitive(bool enabled) { /* Implement toggle case sensitive */ }
void PDFViewerWidget::onToggleWholeWords(bool enabled) { /* Implement toggle whole words */ }

// Event handlers
void PDFViewerWidget::wheelEvent(QWheelEvent *event) { /* Implement wheel event */ }
void PDFViewerWidget::mousePressEvent(QMouseEvent *event) { /* Implement mouse press event */ }
void PDFViewerWidget::mouseMoveEvent(QMouseEvent *event) { /* Implement mouse move event */ }
void PDFViewerWidget::mouseReleaseEvent(QMouseEvent *event) { /* Implement mouse release event */ }
void PDFViewerWidget::keyPressEvent(QKeyEvent *event) { /* Implement key press event */ }
void PDFViewerWidget::contextMenuEvent(QContextMenuEvent *event) { /* Implement context menu event */ }
