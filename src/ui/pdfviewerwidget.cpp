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
    
    // Use standard linear filtering for compatibility
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qWarning() << "CreateTextureFromPDFBitmap: OpenGL error after setting parameters:" << error;
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
    
    // Skip mipmaps for now to avoid compatibility issues
    // In a production version, we could use QOpenGLFunctions_3_0 for mipmap support
    
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
    , m_wheelZoomMode(false) // Default to Ctrl+Wheel zoom, can be toggled
    , m_useBackgroundLoading(true)
    , m_highZoomMode(false)
    , m_loadingLabel(nullptr)
    , m_isLoadingTextures(false)
{
    // Set focus policy for keyboard events
    setFocusPolicy(Qt::StrongFocus);
    
    // Set up render timer for 60 FPS like standalone viewer
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(16); // 60 FPS for smooth animation
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
        
        // OPTIMIZATION: Cap texture resolution to prevent memory issues at high zoom
        // Use effective zoom but limit it to prevent excessive texture sizes
        float effectiveZoom = (m_zoomLevel > 0.5f) ? m_zoomLevel : 0.5f;
        
        // Cap effective zoom at 3.0x for texture resolution to prevent memory issues
        // At higher zooms, we'll use interpolation rather than higher resolution textures
        // With max zoom of 5.0, this gives us good quality up to 60% of max zoom
        float textureZoom = std::min(effectiveZoom, 3.0f);
        
        // Get base page dimensions first
        int baseWidth, baseHeight;
        m_renderer->GetBestFitSize(i, m_viewportWidth, m_viewportHeight, baseWidth, baseHeight);
        
        // Calculate texture resolution based on capped zoom
        int textureWidth = static_cast<int>(baseWidth * textureZoom);
        int textureHeight = static_cast<int>(baseHeight * textureZoom);
        
        // Additional safety cap on absolute texture size to prevent memory overflow
        const int MAX_TEXTURE_SIZE = 4096; // 4K max texture size
        if (textureWidth > MAX_TEXTURE_SIZE || textureHeight > MAX_TEXTURE_SIZE) {
            float scaleFactor = std::min(
                static_cast<float>(MAX_TEXTURE_SIZE) / textureWidth,
                static_cast<float>(MAX_TEXTURE_SIZE) / textureHeight
            );
            textureWidth = static_cast<int>(textureWidth * scaleFactor);
            textureHeight = static_cast<int>(textureHeight * scaleFactor);
            qDebug() << "Page" << i << "texture size capped to" << textureWidth << "x" << textureHeight;
        }
        
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
    
    // Calculate visible page range based on current scroll position
    int firstVisible = 0;
    int lastVisible = 0;
    
    // Improved visible page calculation
    float currentY = -m_scrollOffsetY;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        
        // Check if page is visible in viewport
        if (currentY + pageHeight >= 0 && currentY <= m_viewportHeight) {
            if (firstVisible == 0 && lastVisible == 0) {
                firstVisible = i;
            }
            lastVisible = i;
        }
        
        currentY += pageHeight + PAGE_MARGIN;
        
        // Stop if we're well below the viewport
        if (currentY > m_viewportHeight + 500) { // Small buffer
            break;
        }
    }
    
    // Add one page before and after for smooth scrolling
    firstVisible = std::max(0, firstVisible - 1);
    lastVisible = std::min(m_pageCount - 1, lastVisible + 1);
    
    qDebug() << "Updating visible pages" << firstVisible << "to" << lastVisible << "at zoom" << m_zoomLevel;
    
    // Use optimized background loading for better performance
    std::vector<int> pagesToUpdate;
    for (int i = firstVisible; i <= lastVisible && i < m_pageCount; ++i) {
        pagesToUpdate.push_back(i);
    }
    
    if (m_useBackgroundLoading) {
        loadTexturesInBackground(pagesToUpdate);
    } else {
        // Fallback to direct loading
        for (int i : pagesToUpdate) {
            scheduleTextureUpdate(i);
        }
    }
    
    // Clean up unused textures to free memory
    if (m_highZoomMode) {
        cleanupUnusedTextures();
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

// Implement the missing zoom mode methods
void PDFViewerWidget::setWheelZoomMode(bool enabled)
{
    m_wheelZoomMode = enabled;
    if (m_zoomModeAction) {
        m_zoomModeAction->setChecked(enabled);
    }
    qDebug() << "Wheel zoom mode set to:" << (enabled ? "Enabled" : "Disabled");
}

bool PDFViewerWidget::getWheelZoomMode() const
{
    return m_wheelZoomMode;
}

// Implement screen to document coordinate conversion
QPointF PDFViewerWidget::screenToDocumentCoordinates(const QPoint &screenPos) const
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return QPointF();
    }
    
    // Find the widest page for horizontal reference
    float maxPageWidth = 0.0f;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        maxPageWidth = std::max(maxPageWidth, pageWidth);
    }
    
    // Calculate page position on screen
    float pageX = (m_viewportWidth - maxPageWidth) / 2.0f - m_scrollOffsetX;
    
    // Convert to document coordinates
    double docX = (screenPos.x() - pageX) / m_zoomLevel;
    double docY = (screenPos.y() + m_scrollOffsetY) / m_zoomLevel;
    
    return QPointF(docX, docY);
}

QPoint PDFViewerWidget::documentToScreenCoordinates(const QPointF &docPos) const
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return QPoint();
    }
    
    // Find the widest page for horizontal reference
    float maxPageWidth = 0.0f;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        maxPageWidth = std::max(maxPageWidth, pageWidth);
    }
    
    // Calculate page position on screen
    float pageX = (m_viewportWidth - maxPageWidth) / 2.0f - m_scrollOffsetX;
    
    // Convert to screen coordinates
    int screenX = static_cast<int>(docPos.x() * m_zoomLevel + pageX);
    int screenY = static_cast<int>(docPos.y() * m_zoomLevel - m_scrollOffsetY);
    
    return QPoint(screenX, screenY);
}

void PDFViewerWidget::setupUI() 
{
    // Create main layout - this will be handled by the parent widget
    // Just initialize basic UI elements that might be needed
    // The actual UI setup is done in the main window
}

void PDFViewerWidget::setupToolbar() { /* Implement toolbar setup */ }
void PDFViewerWidget::setupSearchBar() { /* Implement search bar setup */ }
void PDFViewerWidget::createContextMenu() 
{ 
    m_contextMenu = new QMenu(this);
    
    // Zoom actions
    m_zoomInAction = m_contextMenu->addAction("Zoom In");
    m_zoomInAction->setShortcut(QKeySequence("Ctrl+="));
    connect(m_zoomInAction, &QAction::triggered, this, &PDFViewerWidget::zoomIn);
    
    m_zoomOutAction = m_contextMenu->addAction("Zoom Out");
    m_zoomOutAction->setShortcut(QKeySequence("Ctrl+-"));
    connect(m_zoomOutAction, &QAction::triggered, this, &PDFViewerWidget::zoomOut);
    
    m_zoomFitAction = m_contextMenu->addAction("Zoom to Fit");
    m_zoomFitAction->setShortcut(QKeySequence("Ctrl+0"));
    connect(m_zoomFitAction, &QAction::triggered, this, &PDFViewerWidget::zoomToFit);
    
    m_zoomWidthAction = m_contextMenu->addAction("Zoom to Width");
    connect(m_zoomWidthAction, &QAction::triggered, this, &PDFViewerWidget::zoomToWidth);
    
    m_contextMenu->addSeparator();
    
    // Zoom mode toggle
    m_zoomModeAction = m_contextMenu->addAction("Toggle Wheel Zoom Mode");
    m_zoomModeAction->setCheckable(true);
    m_zoomModeAction->setChecked(m_wheelZoomMode);
    connect(m_zoomModeAction, &QAction::triggered, this, [this](bool checked) {
        m_wheelZoomMode = checked;
        toggleZoomMode();
    });
    
    m_contextMenu->addSeparator();
    
    // Search action
    m_searchAction = m_contextMenu->addAction("Search...");
    m_searchAction->setShortcut(QKeySequence("Ctrl+F"));
    connect(m_searchAction, &QAction::triggered, this, &PDFViewerWidget::startSearch);
}

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
    
    // Calculate zoom factor - Use 1.1x like standalone viewer for smoother zooming
    double zoomFactor = zoomIn ? 1.1 : (1.0 / 1.1);
    double newZoom = std::clamp(m_zoomLevel * zoomFactor, MIN_ZOOM, MAX_ZOOM);
    
    // If zoom level doesn't actually change, nothing to do
    if (std::abs(newZoom - m_zoomLevel) < 0.0005) { // Reduced threshold like standalone viewer
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
    
    // IMMEDIATE RENDERING: Trigger immediate repaint for responsive feel like standalone viewer
    update();
    
    // Check if we need texture updates using standalone viewer's logic
    bool needsFullRegeneration = false;
    bool needsVisibleRegeneration = false;
    
    double zoomDifference = std::abs(newZoom - m_lastRenderedZoom) / m_lastRenderedZoom;
    
    // More aggressive thresholds like standalone viewer for smoother zooming
    // At high zoom levels, use more relaxed thresholds to reduce texture regeneration
    float baseThreshold = (m_zoomLevel > 2.5f) ? 0.05f : 0.0005f; // Higher threshold at high zoom
    float fullRegenThreshold = (m_zoomLevel > 2.5f) ? 0.1f : 0.015f; // Much higher threshold at high zoom
    
    if (m_immediateRenderRequired && zoomDifference > baseThreshold) {
        needsVisibleRegeneration = true;
        m_immediateRenderRequired = false;
    }
    // For full regeneration, use adaptive threshold based on zoom level
    else if (zoomDifference > fullRegenThreshold) {
        needsFullRegeneration = true;
        m_lastRenderedZoom = newZoom;
    }
    
    if (needsFullRegeneration || needsVisibleRegeneration) {
        qDebug() << "Texture update triggered:" << (needsFullRegeneration ? "Full" : "Visible") << "zoom:" << m_zoomLevel;
        // Use immediate texture updates for better responsiveness
        if (context() && context()->isValid()) {
            makeCurrent();
            if (needsFullRegeneration) {
                updateTextures();
            } else {
                updateVisibleTextures();
            }
            doneCurrent();
        }
    }
    
    // Update UI controls
    if (m_zoomSlider) {
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
    }
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
    }
    
    emit zoomChanged(m_zoomLevel);
}

void PDFViewerWidget::updateRender() { if (m_isPDFLoaded) update(); }
void PDFViewerWidget::setZoomLevel(double zoom) 
{ 
    double oldZoom = m_zoomLevel;
    m_zoomLevel = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    
    // Enable high zoom mode for performance optimizations - adjusted for new zoom limits
    m_highZoomMode = (m_zoomLevel > 2.5); // Since max zoom is now 5.0, use 2.5 as threshold
    
    if (std::abs(oldZoom - m_zoomLevel) < 0.0005) { // More responsive threshold
        return; // No significant change
    }
    
    // Clean up unused textures when entering/leaving high zoom mode
    if ((oldZoom <= 2.5 && m_zoomLevel > 2.5) || (oldZoom > 2.5 && m_zoomLevel <= 2.5)) {
        cleanupUnusedTextures();
    }
    
    // Recalculate page layout
    calculatePageLayout();
    
    // IMMEDIATE RENDERING: Trigger immediate repaint for responsive feel
    update();
    
    // Mark that zoom has changed for smart texture updates
    m_zoomChanged = true;
    m_immediateRenderRequired = true; // Request immediate update for responsive feel
    
    // Check if we need texture updates using standalone viewer's logic
    double zoomDifference = std::abs(m_zoomLevel - m_lastRenderedZoom) / m_lastRenderedZoom;
    
    // More aggressive thresholds for smoother zooming with high zoom optimization
    // At high zoom levels, use more relaxed thresholds to reduce texture regeneration
    float baseThreshold = (m_zoomLevel > 2.5f) ? 0.05f : 0.0005f; // Higher threshold at high zoom
    float fullRegenThreshold = (m_zoomLevel > 2.5f) ? 0.1f : 0.015f; // Much higher threshold at high zoom
    
    if (m_immediateRenderRequired && zoomDifference > baseThreshold) {
        m_immediateRenderRequired = false;
        
        qDebug() << "Texture update in setZoomLevel: Visible update, Old:" << oldZoom << "New:" << m_zoomLevel;
        if (context() && context()->isValid()) {
            makeCurrent();
            updateVisibleTextures();
            doneCurrent();
        }
    }
    // For full regeneration, use adaptive threshold based on zoom level
    else if (zoomDifference > fullRegenThreshold) {
        m_lastRenderedZoom = m_zoomLevel;
        
        qDebug() << "Texture update in setZoomLevel: Full update, Old:" << oldZoom << "New:" << m_zoomLevel;
        if (context() && context()->isValid()) {
            makeCurrent();
            updateTextures();
            doneCurrent();
        }
    }
    
    // Update UI controls
    if (m_zoomSlider) {
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
    }
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
    }
    
    // Trigger final repaint and auto-center if needed
    update();
    autoCenter(); // Auto-center like standalone viewer when zoomed out
    
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
    
    // Check zoom mode: if wheel zoom mode is enabled, or Ctrl is pressed
    bool shouldZoom = m_wheelZoomMode || (event->modifiers() & Qt::ControlModifier);
    
    if (shouldZoom) {
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
        case Qt::Key_Z:
            if (event->modifiers() & Qt::ControlModifier) {
                toggleZoomMode();
                event->accept();
                return;
            }
            break;
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

// Background texture loading for high zoom performance
void PDFViewerWidget::loadTexturesInBackground(const std::vector<int>& pageIndices)
{
    // For now, implement as immediate loading but with memory management
    // In a full implementation, this would use QThread for background loading
    
    for (int pageIndex : pageIndices) {
        if (pageIndex >= 0 && pageIndex < m_pageCount) {
            scheduleTextureUpdate(pageIndex);
        }
    }
}

void PDFViewerWidget::scheduleTextureUpdate(int pageIndex)
{
    if (!m_renderer || pageIndex < 0 || pageIndex >= m_pageCount) {
        return;
    }
    
    // Check memory usage before creating new textures
    if (calculateTextureMemoryUsage() > MAX_TEXTURE_MEMORY) {
        cleanupUnusedTextures();
    }
    
    // Delete existing texture for this page
    if (pageIndex < static_cast<int>(m_pageTextures.size()) && m_pageTextures[pageIndex] != 0) {
        glDeleteTextures(1, &m_pageTextures[pageIndex]);
    }
    
    // Use optimized texture resolution for high zoom
    float effectiveZoom = (m_zoomLevel > 0.5f) ? m_zoomLevel : 0.5f;
    float textureZoom = m_highZoomMode ? std::min(effectiveZoom, 2.5f) : std::min(effectiveZoom, 3.0f);
    
    // Get base page dimensions
    int baseWidth, baseHeight;
    m_renderer->GetBestFitSize(pageIndex, m_viewportWidth, m_viewportHeight, baseWidth, baseHeight);
    
    // Calculate texture resolution with caps
    int textureWidth = static_cast<int>(baseWidth * textureZoom);
    int textureHeight = static_cast<int>(baseHeight * textureZoom);
    
    const int MAX_TEXTURE_SIZE = m_highZoomMode ? 3072 : 4096; // Reduced size in high zoom mode
    if (textureWidth > MAX_TEXTURE_SIZE || textureHeight > MAX_TEXTURE_SIZE) {
        float scaleFactor = std::min(
            static_cast<float>(MAX_TEXTURE_SIZE) / textureWidth,
            static_cast<float>(MAX_TEXTURE_SIZE) / textureHeight
        );
        textureWidth = static_cast<int>(textureWidth * scaleFactor);
        textureHeight = static_cast<int>(textureHeight * scaleFactor);
    }
    
    // Render and create texture
    FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(pageIndex, textureWidth, textureHeight);
    if (bitmap) {
        m_pageTextures[pageIndex] = CreateTextureFromPDFBitmap(bitmap, textureWidth, textureHeight);
        FPDFBitmap_Destroy(bitmap);
    }
}

void PDFViewerWidget::cleanupUnusedTextures()
{
    if (!m_isPDFLoaded || m_pageTextures.empty()) {
        return;
    }
    
    // Calculate visible page range
    int firstVisible = 0;
    int lastVisible = 0;
    
    float currentY = -m_scrollOffsetY;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        
        if (currentY + pageHeight >= 0 && currentY <= m_viewportHeight) {
            if (firstVisible == 0 && lastVisible == 0) {
                firstVisible = i;
            }
            lastVisible = i;
        }
        
        currentY += pageHeight + PAGE_MARGIN;
        
        if (currentY > m_viewportHeight + 1000) { // Extended buffer
            break;
        }
    }
    
    // Keep a buffer of pages around visible area
    int bufferSize = m_highZoomMode ? 1 : 2; // Smaller buffer in high zoom mode
    firstVisible = std::max(0, firstVisible - bufferSize);
    lastVisible = std::min(m_pageCount - 1, lastVisible + bufferSize);
    
    // Clean up textures outside the buffer range
    for (int i = 0; i < m_pageCount; ++i) {
        if ((i < firstVisible || i > lastVisible) && 
            i < static_cast<int>(m_pageTextures.size()) && 
            m_pageTextures[i] != 0) {
            glDeleteTextures(1, &m_pageTextures[i]);
            m_pageTextures[i] = 0;
        }
    }
}

size_t PDFViewerWidget::calculateTextureMemoryUsage() const
{
    size_t totalMemory = 0;
    
    for (int i = 0; i < static_cast<int>(m_pageTextures.size()); ++i) {
        if (m_pageTextures[i] != 0 && i < static_cast<int>(m_pageWidths.size()) && i < static_cast<int>(m_pageHeights.size())) {
            // Estimate memory usage: width * height * 4 bytes per pixel (RGBA)
            float effectiveZoom = (m_zoomLevel > 0.5f) ? m_zoomLevel : 0.5f;
            float textureZoom = std::min(effectiveZoom, 3.0f);
            
            int textureWidth = static_cast<int>(m_pageWidths[i] * textureZoom);
            int textureHeight = static_cast<int>(m_pageHeights[i] * textureZoom);
            
            totalMemory += textureWidth * textureHeight * 4;
        }
    }
    
    return totalMemory;
}

// Add the toggleZoomMode method after other methods
void PDFViewerWidget::toggleZoomMode()
{
    m_wheelZoomMode = !m_wheelZoomMode;
    qDebug() << "Wheel zoom mode:" << (m_wheelZoomMode ? "Enabled (wheel always zooms)" : "Disabled (Ctrl+wheel zooms)");
    
    // Update tooltip or status to inform user
    setToolTip(m_wheelZoomMode ? 
        "Mouse wheel zooms (like standalone viewer)" : 
        "Ctrl+Mouse wheel zooms (like web browser)");
}

// Add auto-centering functionality like standalone viewer
void PDFViewerWidget::autoCenter()
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return;
    }
    
    // Auto-center when zoomed out (100% or less)
    if (m_zoomLevel <= 1.0f) {
        // Find the largest page width
        float maxWidth = 0.0f;
        for (int i = 0; i < m_pageCount; ++i) {
            float pageWidth = m_pageWidths[i] * m_zoomLevel;
            maxWidth = std::max(maxWidth, pageWidth);
        }
        
        // If content fits within viewport, center it
        if (maxWidth <= m_viewportWidth) {
            m_scrollOffsetX = 0.0f; // Center horizontally
        }
        
        // Similar for vertical centering if needed
        calculatePageLayout();
        update();
    }
}
