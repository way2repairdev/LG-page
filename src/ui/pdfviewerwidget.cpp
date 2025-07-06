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
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

// Include PDF viewer components - use the original high-quality renderer
#include "rendering/pdf-render.h"
// Removed problematic include: #include "core/feature.h"

// Implementation of PDFRendererDeleter
void PDFRendererDeleter::operator()(PDFRenderer* ptr) const {
    delete ptr;
}

// Helper to convert PDFium bitmap to OpenGL texture (from original PDF viewer)
GLuint CreateTextureFromPDFBitmap(FPDF_BITMAP bitmap, int width, int height) {
    qDebug() << "CreateTextureFromPDFBitmap called with size:" << width << "x" << height;
    
    if (!bitmap) {
        qWarning() << "CreateTextureFromPDFBitmap: Invalid bitmap";
        return 0;
    }
    
    void* buffer = FPDFBitmap_GetBuffer(bitmap);
    if (!buffer) {
        qWarning() << "CreateTextureFromPDFBitmap: Failed to get bitmap buffer";
        return 0;
    }
    
    GLuint textureID;
    glGenTextures(1, &textureID);
    if (textureID == 0) {
        qWarning() << "CreateTextureFromPDFBitmap: Failed to generate texture";
        return 0;
    }
    
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qWarning() << "CreateTextureFromPDFBitmap: OpenGL error after setting parameters:" << error;
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
    
    error = glGetError();
    if (error != GL_NO_ERROR) {
        qWarning() << "CreateTextureFromPDFBitmap: OpenGL error after glTexImage2D:" << error;
        glDeleteTextures(1, &textureID);
        return 0;
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    qDebug() << "CreateTextureFromPDFBitmap: Successfully created texture ID:" << textureID;
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
    , m_lastRenderedZoom(DEFAULT_ZOOM)
    , m_zoomChanged(false)
    , m_immediateRenderRequired(false)
    , m_isDragging(false)
    , m_renderTimer(new QTimer(this))
    , m_viewportWidth(0)
    , m_viewportHeight(0)
    , m_scrollOffsetY(0.0f)
    , m_scrollOffsetX(0.0f)
    , m_maxScrollY(0.0f)
    , m_maxScrollX(0.0f)
    , m_minScrollX(0.0f)
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

// Use original PDF viewer's high-quality rendering approach with optimized workflow
void PDFViewerWidget::updateTextures()
{
    qDebug() << "PDFViewerWidget::updateTextures called";
    
    if (!m_renderer || !m_isPDFLoaded) {
        qDebug() << "Renderer not available or PDF not loaded";
        return;
    }
    
    // Check if we have a valid OpenGL context
    if (!context() || !context()->isValid()) {
        qWarning() << "PDFViewerWidget: Invalid OpenGL context in updateTextures()";
        return;
    }
    
    qDebug() << "Cleaning up existing textures...";
    // Clean up existing textures
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    
    m_pageWidths.clear();
    m_pageHeights.clear();
    
    qDebug() << "Creating textures for" << m_pageCount << "pages at zoom level" << m_zoomLevel;
    // Create textures for all pages using standalone viewer's optimized approach
    m_pageTextures.resize(m_pageCount);
    m_pageWidths.resize(m_pageCount);
    m_pageHeights.resize(m_pageCount);
    
    for (int i = 0; i < m_pageCount; ++i) {
        qDebug() << "Rendering page" << i;
        
        // Use standalone viewer's approach: calculate effective zoom for texture resolution
        float effectiveZoom = (m_zoomLevel > 0.5f) ? m_zoomLevel : 0.5f;
        
        // Get base page dimensions first
        int baseWidth, baseHeight;
        m_renderer->GetBestFitSize(i, m_viewportWidth, m_viewportHeight, baseWidth, baseHeight);
        
        // Calculate texture resolution based on effective zoom
        int textureWidth = static_cast<int>(baseWidth * effectiveZoom);
        int textureHeight = static_cast<int>(baseHeight * effectiveZoom);
        
        // Render page at calculated resolution
        FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(i, textureWidth, textureHeight);
        
        if (bitmap) {
            qDebug() << "Page" << i << "rendered successfully at resolution:" << textureWidth << "x" << textureHeight << "for zoom:" << m_zoomLevel;
            
            // Store the base dimensions for layout calculations (without zoom)
            m_pageWidths[i] = baseWidth;
            m_pageHeights[i] = baseHeight;
            
            // Create texture from bitmap
            m_pageTextures[i] = CreateTextureFromPDFBitmap(bitmap, textureWidth, textureHeight);
            
            FPDFBitmap_Destroy(bitmap);
        } else {
            qDebug() << "Failed to render page" << i;
            m_pageWidths[i] = 0;
            m_pageHeights[i] = 0;
            m_pageTextures[i] = 0;
        }
    }
    
    qDebug() << "Calculating page layout...";
    calculatePageLayout();
    qDebug() << "updateTextures complete";
}

// Fast visible page texture update for responsive zooming (like standalone viewer)
void PDFViewerWidget::updateVisibleTextures()
{
    qDebug() << "PDFViewerWidget::updateVisibleTextures called for responsive zoom";
    
    if (!m_renderer || !m_isPDFLoaded) {
        qDebug() << "Renderer not available or PDF not loaded";
        return;
    }
    
    // Check if we have a valid OpenGL context
    if (!context() || !context()->isValid()) {
        qWarning() << "PDFViewerWidget: Invalid OpenGL context in updateVisibleTextures()";
        return;
    }
    
    // Calculate visible page range (simplified for now)
    int firstVisible = 0;
    int lastVisible = std::min(m_pageCount - 1, 2); // Update first few pages for quick response
    
    // TODO: Implement proper visible page range calculation based on scroll position
    
    qDebug() << "Updating visible pages" << firstVisible << "to" << lastVisible << "at zoom" << m_zoomLevel;
    
    for (int i = firstVisible; i <= lastVisible && i < m_pageCount; ++i) {
        // Delete existing texture
        if (i < static_cast<int>(m_pageTextures.size()) && m_pageTextures[i] != 0) {
            glDeleteTextures(1, &m_pageTextures[i]);
        }
        
        // Use standalone viewer's approach: calculate effective zoom for texture resolution
        float effectiveZoom = (m_zoomLevel > 0.5f) ? m_zoomLevel : 0.5f;
        
        // Get base page dimensions
        int baseWidth, baseHeight;
        m_renderer->GetBestFitSize(i, m_viewportWidth, m_viewportHeight, baseWidth, baseHeight);
        
        // Calculate texture resolution based on effective zoom
        int textureWidth = static_cast<int>(baseWidth * effectiveZoom);
        int textureHeight = static_cast<int>(baseHeight * effectiveZoom);
        
        // Render page at calculated resolution
        FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(i, textureWidth, textureHeight);
        
        if (bitmap) {
            // Create texture from bitmap
            m_pageTextures[i] = CreateTextureFromPDFBitmap(bitmap, textureWidth, textureHeight);
            FPDFBitmap_Destroy(bitmap);
            qDebug() << "Updated visible page" << i << "texture at resolution:" << textureWidth << "x" << textureHeight;
        } else {
            m_pageTextures[i] = 0;
            qDebug() << "Failed to update visible page" << i;
        }
    }
    
    // Recalculate layout since we updated some textures
    calculatePageLayout();
    qDebug() << "updateVisibleTextures complete";
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
    // GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    // Simple visibility calculation for now
    // TODO: Implement proper GetVisiblePageRange function
    
    // Render each visible page using original PDF viewer's approach
    float currentY = -m_scrollOffsetY;
    
    for (int i = firstVisible; i <= lastVisible && i < m_pageCount; ++i) {
        if (i >= 0 && i < static_cast<int>(m_pageTextures.size()) && m_pageTextures[i] != 0) {
            
            // Use standalone viewer's approach: base dimensions with zoom scaling
            float pageWidth = m_pageWidths[i] * m_zoomLevel;
            float pageHeight = m_pageHeights[i] * m_zoomLevel;
            
            // Calculate page position (centered horizontally)
            float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
            float pageY = currentY;
            
            // Skip if page is not visible
            if (pageY + pageHeight >= 0 && pageY <= m_viewportHeight) {
                // Render page texture using original PDF viewer's method
                glBindTexture(GL_TEXTURE_2D, m_pageTextures[i]);
                
                // Set texture filtering based on zoom level for best quality
                if (m_zoomLevel > 1.0) {
                    // When zoomed in, use linear filtering for smooth scaling
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                } else {
                    // When zoomed out, use linear filtering for smooth scaling
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                }
                
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
    qDebug() << "PDFViewerWidget::loadPDF called with:" << filePath;
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        qDebug() << "File does not exist or is not readable:" << filePath;
        emit errorOccurred("File does not exist or is not readable: " + filePath);
        return false;
    }
    
    if (fileInfo.suffix().toLower() != "pdf") {
        qDebug() << "File is not a PDF:" << filePath;
        emit errorOccurred("File is not a PDF: " + filePath);
        return false;
    }
    
    // Close any existing PDF
    closePDF();
    
    try {
        qDebug() << "Initializing PDF renderer...";
        // Initialize PDF renderer if not already done
        if (!m_renderer) {
            initializePDFRenderer();
        }
        
        qDebug() << "Loading PDF document...";
        // Load the PDF document
        std::string stdFilePath = filePath.toStdString();
        if (!m_renderer->LoadDocument(stdFilePath)) {
            qDebug() << "Failed to load PDF document:" << filePath;
            emit errorOccurred("Failed to load PDF document: " + filePath);
            return false;
        }
        
        qDebug() << "Getting page count...";
        // Get page count
        m_pageCount = m_renderer->GetPageCount();
        qDebug() << "Page count:" << m_pageCount;
        if (m_pageCount <= 0) {
            qDebug() << "PDF document has no pages:" << filePath;
            emit errorOccurred("PDF document has no pages: " + filePath);
            return false;
        }
        
        qDebug() << "Setting up initial state...";
        // Set up initial state
        m_filePath = filePath;
        m_isPDFLoaded = true;
        m_currentPage = 0;
        m_zoomLevel = DEFAULT_ZOOM;
        m_scrollOffsetX = 0.0f;
        m_scrollOffsetY = 0.0f;
        
        qDebug() << "Initializing scroll state...";
        // Initialize scroll state using original PDF viewer's approach
        if (!m_scrollState) {
            m_scrollState = std::make_unique<PDFScrollState>();
        }
        m_scrollState->scrollPosition = QPointF(0.0f, 0.0f);
        m_scrollState->zoomLevel = m_zoomLevel;
        m_scrollState->viewportSize = QSizeF(m_viewportWidth, m_viewportHeight);
        
        qDebug() << "Updating UI elements...";
        // Update UI
        if (m_pageInput) {
            m_pageInput->setText("1");
        }
        if (m_pageCountLabel) {
            m_pageCountLabel->setText(QString("/ %1").arg(m_pageCount));
        }
        if (m_zoomSlider) {
            m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
        }
        if (m_zoomLabel) {
            m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
        }
        
        qDebug() << "Checking OpenGL context...";
        // Check if OpenGL context is ready
        if (!context() || !context()->isValid()) {
            qDebug() << "OpenGL context not ready, deferring texture update";
            // If context is not ready, defer the OpenGL operations
            QTimer::singleShot(50, this, [this]() {
                if (m_isPDFLoaded) {
                    qDebug() << "Deferred OpenGL operations starting";
                    makeCurrent();
                    updateTextures();
                    update();
                    qDebug() << "Deferred OpenGL operations complete";
                }
            });
        } else {
            qDebug() << "OpenGL context ready, updating textures immediately";
            // Trigger OpenGL update
            makeCurrent();
            updateTextures();
            update();
            qDebug() << "Immediate OpenGL operations complete";
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
    qDebug() << "PDFViewerWidget::initializePDFRenderer called";
    
    if (m_renderer) {
        qDebug() << "PDF renderer already initialized";
        return;
    }
    
    qDebug() << "Creating new PDFRenderer";
    // Create PDFRenderer with custom deleter
    m_renderer = std::unique_ptr<PDFRenderer, PDFRendererDeleter>(new PDFRenderer());
    
    qDebug() << "Initializing PDFRenderer";
    m_renderer->Initialize();
    
    qDebug() << "PDFRenderer initialization complete";
}

void PDFViewerWidget::initializeGL()
{
    qDebug() << "PDFViewerWidget::initializeGL called";
    
    // Check if we have a valid OpenGL context
    if (!context() || !context()->isValid()) {
        qWarning() << "PDFViewerWidget: Invalid OpenGL context in initializeGL()";
        return;
    }
    
    qDebug() << "Initializing OpenGL functions...";
    initializeOpenGLFunctions();
    
    qDebug() << "Setting clear color...";
    // Set clear color
    glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
    
    qDebug() << "OpenGL initialization complete";
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
void PDFViewerWidget::setupUI() 
{
    // Create main layout - this will be handled by the parent widget
    // Just initialize basic UI elements that might be needed
    // The actual UI setup is done in the main window
}

void PDFViewerWidget::setupToolbar() { /* Implement toolbar setup */ }
void PDFViewerWidget::setupSearchBar() { /* Implement search bar setup */ }
void PDFViewerWidget::createContextMenu() { /* Implement context menu */ }

void PDFViewerWidget::closePDF() 
{
    if (m_isPDFLoaded) {
        // Clean up OpenGL resources
        if (context() && context()->isValid()) {
            makeCurrent();
            if (!m_pageTextures.empty()) {
                glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
                m_pageTextures.clear();
            }
            doneCurrent();
        }
        
        // Reset state
        m_isPDFLoaded = false;
        m_pageCount = 0;
        m_currentPage = 0;
        m_zoomLevel = DEFAULT_ZOOM;
        m_scrollOffsetX = 0.0f;
        m_scrollOffsetY = 0.0f;
        m_maxScrollX = 0.0f;
        m_maxScrollY = 0.0f;
        m_minScrollX = 0.0f;
        m_filePath.clear();
        
        // Clear page data
        m_pageWidths.clear();
        m_pageHeights.clear();
        
        // Update UI
        if (m_pageInput) {
            m_pageInput->setText("0");
        }
        if (m_pageCountLabel) {
            m_pageCountLabel->setText("/ 0");
        }
        
        update();
    }
}
void PDFViewerWidget::calculatePageLayout()
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return;
    }
    
    // Calculate total document height and max width
    float totalHeight = 0.0f;
    float maxWidth = 0.0f;
    
    for (int i = 0; i < m_pageCount; ++i) {
        // Use standalone viewer's approach: base dimensions with zoom scaling
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        
        totalHeight += pageHeight;
        if (i < m_pageCount - 1) {
            totalHeight += PAGE_MARGIN;
        }
        
        maxWidth = std::max(maxWidth, pageWidth);
    }
    
    // Update scroll limits - Fixed to handle centering properly
    m_maxScrollY = std::max(0.0f, totalHeight - m_viewportHeight);
    
    // For horizontal scrolling, we need to account for centering
    // The rendering formula is: pageX = (viewportWidth - pageWidth) / 2.0f - scrollOffsetX
    // This means:
    // - When scrollOffsetX = 0, page is centered
    // - When scrollOffsetX > 0, page moves left (we see more of the right side)
    // - When scrollOffsetX < 0, page moves right (we see more of the left side)
    
    if (maxWidth <= m_viewportWidth) {
        // Content fits within viewport - no scrolling needed, keep centered
        m_maxScrollX = 0.0f;
        m_minScrollX = 0.0f;
    } else {
        // Content is larger than viewport - allow scrolling to see all content
        // Maximum scroll: show right edge of content at right edge of viewport
        // pageX = (viewportWidth - pageWidth) / 2.0f - scrollOffsetX
        // For right edge: pageX + pageWidth = viewportWidth
        // So: (viewportWidth - pageWidth) / 2.0f - scrollOffsetX + pageWidth = viewportWidth
        // scrollOffsetX = (viewportWidth - pageWidth) / 2.0f + pageWidth - viewportWidth
        // scrollOffsetX = (pageWidth - viewportWidth) / 2.0f
        m_maxScrollX = (maxWidth - m_viewportWidth) / 2.0f;
        
        // Minimum scroll: show left edge of content at left edge of viewport
        // For left edge: pageX = 0
        // So: (viewportWidth - pageWidth) / 2.0f - scrollOffsetX = 0
        // scrollOffsetX = (viewportWidth - pageWidth) / 2.0f
        m_minScrollX = -(maxWidth - m_viewportWidth) / 2.0f;
    }
    
    // Clamp current scroll position to new limits
    m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0.0f, m_maxScrollY);
    m_scrollOffsetX = std::clamp(m_scrollOffsetX, m_minScrollX, m_maxScrollX);
    
    // Update scroll state
    if (m_scrollState) {
        m_scrollState->scrollPosition = QPointF(m_scrollOffsetX, m_scrollOffsetY);
        m_scrollState->zoomLevel = m_zoomLevel;
        m_scrollState->viewportSize = QSizeF(m_viewportWidth, m_viewportHeight);
    }
}

void PDFViewerWidget::performCursorBasedZoom(const QPoint &cursorPos, bool zoomIn)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Calculate zoom factor
    double zoomFactor = zoomIn ? 1.2 : 0.8;
    double newZoom = std::clamp(m_zoomLevel * zoomFactor, MIN_ZOOM, MAX_ZOOM);
    
    // If zoom level doesn't actually change, nothing to do
    if (std::abs(newZoom - m_zoomLevel) < 0.001) {
        return;
    }
    
    // Store old zoom level
    double oldZoom = m_zoomLevel;
    
    // IMPORTANT: Account for the fact that pages are centered horizontally
    // The rendering logic uses: pageX = (viewportWidth - pageWidth) / 2.0f - scrollOffsetX
    // So we need to calculate the document coordinates relative to the centered page layout
    
    // For horizontal (X) coordinate:
    // Find the widest page to determine the centering reference
    float maxPageWidth = 0.0f;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageWidth = m_pageWidths[i] * oldZoom; // Use base dimensions with zoom
        maxPageWidth = std::max(maxPageWidth, pageWidth);
    }
    
    // Current page position on screen
    float currentPageX = (m_viewportWidth - maxPageWidth) / 2.0f - m_scrollOffsetX;
    
    // Convert cursor position to document coordinates
    // Document X = (cursor X - page X on screen) / zoom scale
    double docX = (cursorPos.x() - currentPageX) / oldZoom;
    double docY = (cursorPos.y() + m_scrollOffsetY) / oldZoom;
    
    // Update zoom level
    m_zoomLevel = newZoom;
    
    // Recalculate page layout with new zoom
    calculatePageLayout();
    
    // Calculate new page width for new zoom level
    float newMaxPageWidth = 0.0f;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageWidth = m_pageWidths[i] * newZoom; // Use base dimensions with zoom
        newMaxPageWidth = std::max(newMaxPageWidth, pageWidth);
    }
    
    // Calculate what the new page position should be to keep the document point under cursor
    float newPageX = cursorPos.x() - docX * newZoom;
    
    // Calculate the scroll offset needed to achieve this page position
    // pageX = (viewportWidth - pageWidth) / 2.0f - scrollOffsetX
    // scrollOffsetX = (viewportWidth - pageWidth) / 2.0f - pageX
    double newScrollX = (m_viewportWidth - newMaxPageWidth) / 2.0f - newPageX;
    double newScrollY = docY * newZoom - cursorPos.y();
    
    // Clamp to valid scroll range using the updated limits
    m_scrollOffsetX = std::clamp(static_cast<float>(newScrollX), m_minScrollX, m_maxScrollX);
    m_scrollOffsetY = std::clamp(static_cast<float>(newScrollY), 0.0f, m_maxScrollY);
    
    // Mark that zoom has changed for smart texture updates
    m_zoomChanged = true;
    m_immediateRenderRequired = true; // Request immediate update for responsive feel
    
    // Check if we need texture updates using standalone viewer's logic
    bool needsFullRegeneration = false;
    bool needsVisibleRegeneration = false;
    
    double zoomDifference = std::abs(newZoom - m_lastRenderedZoom) / m_lastRenderedZoom;
    
    // For immediate responsive zoom, regenerate visible pages with lower threshold (1%)
    if (m_immediateRenderRequired && zoomDifference > 0.01) {
        needsVisibleRegeneration = true;
        m_immediateRenderRequired = false;
    }
    // For full regeneration, use higher threshold (3%) to avoid too frequent full regens
    else if (zoomDifference > 0.03) {
        needsFullRegeneration = true;
        m_lastRenderedZoom = newZoom;
    }
    
    if (needsFullRegeneration || needsVisibleRegeneration) {
        qDebug() << "Texture update triggered:" << (needsFullRegeneration ? "Full" : "Visible") << "zoom:" << m_zoomLevel;
        // Defer texture update to avoid blocking UI
        QTimer::singleShot(10, this, [this, needsFullRegeneration]() {
            if (context() && context()->isValid()) {
                makeCurrent();
                if (needsFullRegeneration) {
                    updateTextures();
                } else {
                    updateVisibleTextures();
                }
                doneCurrent();
            }
        });
    }
    
    // Update UI controls
    if (m_zoomSlider) {
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
    }
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
    }
    
    // Trigger repaint
    update();
    
    emit zoomChanged(m_zoomLevel);
}

void PDFViewerWidget::updateRender() { if (m_isPDFLoaded) update(); }
void PDFViewerWidget::setZoomLevel(double zoom) 
{ 
    double oldZoom = m_zoomLevel;
    m_zoomLevel = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    
    if (std::abs(oldZoom - m_zoomLevel) < 0.001) {
        return; // No significant change
    }
    
    // Recalculate page layout
    calculatePageLayout();
    
    // Mark that zoom has changed for smart texture updates
    m_zoomChanged = true;
    m_immediateRenderRequired = true; // Request immediate update for responsive feel
    
    // Check if we need texture updates using standalone viewer's logic
    double zoomDifference = std::abs(m_zoomLevel - m_lastRenderedZoom) / m_lastRenderedZoom;
    
    // For immediate responsive zoom, regenerate visible pages with lower threshold (1%)
    if (m_immediateRenderRequired && zoomDifference > 0.01) {
        bool needsVisibleRegeneration = true;
        m_immediateRenderRequired = false;
        
        qDebug() << "Texture update in setZoomLevel: Visible update, Old:" << oldZoom << "New:" << m_zoomLevel;
        QTimer::singleShot(10, this, [this]() {
            if (context() && context()->isValid()) {
                makeCurrent();
                updateVisibleTextures();
                doneCurrent();
            }
        });
    }
    // For full regeneration, use higher threshold (3%) to avoid too frequent full regens
    else if (zoomDifference > 0.03) {
        m_lastRenderedZoom = m_zoomLevel;
        
        qDebug() << "Texture update in setZoomLevel: Full update, Old:" << oldZoom << "New:" << m_zoomLevel;
        QTimer::singleShot(10, this, [this]() {
            if (context() && context()->isValid()) {
                makeCurrent();
                updateTextures();
                doneCurrent();
            }
        });
    }
    
    // Update UI controls
    if (m_zoomSlider) {
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
    }
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
    }
    
    // Trigger repaint
    update();
    
    emit zoomChanged(m_zoomLevel);
}
double PDFViewerWidget::getZoomLevel() const { return m_zoomLevel; }
bool PDFViewerWidget::isPDFLoaded() const { return m_isPDFLoaded; }
QString PDFViewerWidget::getCurrentFilePath() const { return m_filePath; }
int PDFViewerWidget::getCurrentPage() const { return m_currentPage + 1; }
int PDFViewerWidget::getPageCount() const { return m_pageCount; }

// Navigation methods
void PDFViewerWidget::goToPage(int pageNumber)
{
    if (!m_isPDFLoaded || pageNumber < 1 || pageNumber > m_pageCount) {
        return;
    }
    
    m_currentPage = pageNumber - 1;
    
    // Calculate scroll position to show the page
    float targetY = 0.0f;
    for (int i = 0; i < m_currentPage; ++i) {
        targetY += m_pageHeights[i] * m_zoomLevel + PAGE_MARGIN;
    }
    
    m_scrollOffsetY = std::clamp(targetY, 0.0f, m_maxScrollY);
    
    // Update UI
    if (m_pageInput) {
        m_pageInput->setText(QString::number(pageNumber));
    }
    
    update();
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
    // Use cursor-based zoom with center of view as cursor position
    QPoint centerPos(m_viewportWidth / 2, m_viewportHeight / 2);
    performCursorBasedZoom(centerPos, true);
}

void PDFViewerWidget::zoomOut()
{
    // Use cursor-based zoom with center of view as cursor position
    QPoint centerPos(m_viewportWidth / 2, m_viewportHeight / 2);
    performCursorBasedZoom(centerPos, false);
}

void PDFViewerWidget::zoomToFit()
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return;
    }
    
    // Calculate zoom to fit the first page
    double pageWidth = m_pageWidths[0];
    double pageHeight = m_pageHeights[0];
    
    double zoomX = m_viewportWidth / pageWidth;
    double zoomY = m_viewportHeight / pageHeight;
    
    setZoomLevel(std::min(zoomX, zoomY));
    calculatePageLayout();
    update();
}

void PDFViewerWidget::zoomToWidth()
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return;
    }
    
    // Calculate zoom to fit page width
    double pageWidth = m_pageWidths[0];
    setZoomLevel(m_viewportWidth / pageWidth);
    calculatePageLayout();
    update();
}

void PDFViewerWidget::resetZoom()
{
    setZoomLevel(DEFAULT_ZOOM);
    calculatePageLayout();
    update();
}

// Search methods
void PDFViewerWidget::startSearch() 
{
    if (m_searchWidget) {
        m_searchWidget->setVisible(true);
        if (m_searchInput) {
            m_searchInput->setFocus();
        }
    }
}

void PDFViewerWidget::searchNext() 
{
    // TODO: Implement search functionality
}

void PDFViewerWidget::searchPrevious() 
{
    // TODO: Implement search functionality
}

void PDFViewerWidget::setSearchTerm(const QString &term) 
{
    if (m_searchInput) {
        m_searchInput->setText(term);
    }
}

void PDFViewerWidget::clearSearch() 
{
    if (m_searchInput) {
        m_searchInput->clear();
    }
    if (m_searchWidget) {
        m_searchWidget->setVisible(false);
    }
}

// Slot methods
void PDFViewerWidget::onPageInputChanged()
{
    if (!m_pageInput) return;
    
    bool ok;
    int pageNumber = m_pageInput->text().toInt(&ok);
    if (ok && pageNumber >= 1 && pageNumber <= m_pageCount) {
        goToPage(pageNumber);
    }
}

void PDFViewerWidget::onZoomSliderChanged(int value)
{
    double newZoom = value / 100.0;
    setZoomLevel(newZoom);
    calculatePageLayout();
    update();
    
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(value));
    }
    
    emit zoomChanged(newZoom);
}

void PDFViewerWidget::onSearchTextChanged(const QString &text) 
{
    // TODO: Implement search text changed
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
    // TODO: Implement toggle case sensitive
}

void PDFViewerWidget::onToggleWholeWords(bool enabled) 
{
    // TODO: Implement toggle whole words
}

// Event handlers
void PDFViewerWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Check if Ctrl is pressed for zooming
    if (event->modifiers() & Qt::ControlModifier) {
        // Cursor-based zoom implementation
        QPoint cursorPos = event->position().toPoint();
        
        // Calculate zoom direction
        bool zoomIn = event->angleDelta().y() > 0;
        
        // Perform cursor-based zoom
        performCursorBasedZoom(cursorPos, zoomIn);
        
        event->accept();
    } else {
        // Scroll vertically
        float scrollDelta = -event->angleDelta().y() * 0.5f;
        m_scrollOffsetY = std::clamp(m_scrollOffsetY + scrollDelta, 0.0f, m_maxScrollY);
        update();
        event->accept();
    }
}

void PDFViewerWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    event->accept();
}

void PDFViewerWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - m_lastPanPoint;
        
        // Pan the view with updated limits that handle centering
        m_scrollOffsetX = std::clamp(m_scrollOffsetX - delta.x(), m_minScrollX, m_maxScrollX);
        m_scrollOffsetY = std::clamp(m_scrollOffsetY - delta.y(), 0.0f, m_maxScrollY);
        
        m_lastPanPoint = event->pos();
        update();
    }
    event->accept();
}

void PDFViewerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        setCursor(Qt::OpenHandCursor);
    }
    event->accept();
}

void PDFViewerWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            if (event->modifiers() & Qt::ControlModifier) {
                zoomIn();
                event->accept();
                return;
            }
            break;
        case Qt::Key_Minus:
            if (event->modifiers() & Qt::ControlModifier) {
                zoomOut();
                event->accept();
                return;
            }
            break;
        case Qt::Key_0:
            if (event->modifiers() & Qt::ControlModifier) {
                resetZoom();
                event->accept();
                return;
            }
            break;
        case Qt::Key_PageUp:
            previousPage();
            event->accept();
            return;
        case Qt::Key_PageDown:
            nextPage();
            event->accept();
            return;
        case Qt::Key_Home:
            if (event->modifiers() & Qt::ControlModifier) {
                goToFirstPage();
            } else {
                m_scrollOffsetY = 0.0f;
                update();
            }
            event->accept();
            return;
        case Qt::Key_End:
            if (event->modifiers() & Qt::ControlModifier) {
                goToLastPage();
            } else {
                m_scrollOffsetY = m_maxScrollY;
                update();
            }
            event->accept();
            return;
    }
    
    QOpenGLWidget::keyPressEvent(event);
}

void PDFViewerWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_contextMenu) {
        m_contextMenu->exec(event->globalPos());
    }
    event->accept();
}

void PDFViewerWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    
    // Update viewport dimensions
    m_viewportWidth = event->size().width();
    m_viewportHeight = event->size().height() - TOOLBAR_HEIGHT - (m_searchWidget && m_searchWidget->isVisible() ? SEARCH_BAR_HEIGHT : 0);
    
    if (m_isPDFLoaded) {
        calculatePageLayout();
        update();
    }
}
