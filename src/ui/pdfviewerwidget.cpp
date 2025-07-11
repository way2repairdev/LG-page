#include "ui/pdfviewerwidget.h"
#include "ui/pdfscrollstate.h"
#include "ui/textextraction.h"
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
#include <QDateTime>
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
    , m_textExtractor(std::make_unique<TextExtractor>())
    , m_textSelection(std::make_unique<TextSelection>())
    , m_textExtractionComplete(false)
    , m_shaderProgram(nullptr)
    , m_toolbarWidget(nullptr)
    , m_toolbar(nullptr)
    , m_zoomSlider(nullptr)
    , m_zoomLabel(nullptr)
    , m_pageInput(nullptr)
    , m_pageCountLabel(nullptr)
    , m_selectedTextInput(nullptr)
    , m_verticalScrollBar(nullptr)
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
    , m_isTextSelecting(false)
    , m_lastMousePos(0, 0)
    , m_viewportWidth(0)
    , m_viewportHeight(0)
    , m_scrollOffsetY(0.0f)
    , m_scrollOffsetX(0.0f)
    , m_maxScrollY(0.0f)
    , m_maxScrollX(0.0f)
    , m_minScrollX(0.0f)
    , m_useBackgroundLoading(true)
    , m_highZoomMode(false)
    , m_loadingLabel(nullptr)
    , m_isLoadingTextures(false)
    , m_wheelThrottleTimer(new QTimer(this))
    , m_wheelAccelTimer(new QTimer(this))
    , m_pendingZoomDelta(0.0)
    , m_pendingWheelCursor(0, 0)
    , m_wheelEventCount(0)
    , m_lastWheelTime(0)
    , m_wheelThrottleActive(false)
    , m_panThrottleTimer(new QTimer(this))
    , m_pendingPanDelta(0, 0)
    , m_panThrottleActive(false)
    , m_lastPanTime(0)
    , m_panEventCount(0)
    , m_progressiveRenderTimer(new QTimer(this))
    , m_progressiveRenderActive(false)
{
    // Set focus policy for keyboard events
    setFocusPolicy(Qt::StrongFocus);
    
    // Set up render timer for 60 FPS like standalone viewer
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(16); // 60 FPS for smooth animation
    connect(m_renderTimer, &QTimer::timeout, this, &PDFViewerWidget::updateRender);
    
    // Set up wheel throttling timers
    m_wheelThrottleTimer->setSingleShot(true);
    m_wheelThrottleTimer->setInterval(WHEEL_THROTTLE_MS);
    connect(m_wheelThrottleTimer, &QTimer::timeout, this, &PDFViewerWidget::processThrottledWheelEvent);
    
    m_wheelAccelTimer->setSingleShot(true);
    m_wheelAccelTimer->setInterval(WHEEL_ACCEL_RESET_MS);
    connect(m_wheelAccelTimer, &QTimer::timeout, this, &PDFViewerWidget::resetWheelAcceleration);
    
    // Set up panning throttling timer
    m_panThrottleTimer->setSingleShot(true);
    m_panThrottleTimer->setInterval(PAN_THROTTLE_MS);
    connect(m_panThrottleTimer, &QTimer::timeout, this, &PDFViewerWidget::processThrottledPanEvent);
    
    // Set up progressive render timer
    m_progressiveRenderTimer->setSingleShot(false);
    m_progressiveRenderTimer->setInterval(25); // 40 FPS for progressive updates
    connect(m_progressiveRenderTimer, &QTimer::timeout, this, &PDFViewerWidget::processProgressiveRender);
    
    // Initialize UI
    setupUI();
    createContextMenu();
    
    // Connect text selection signal to update the input box
    connect(this, &PDFViewerWidget::textSelectionChanged, 
            this, [this](const QString& selectedText) {
        if (m_selectedTextInput) {
            if (selectedText.isEmpty()) {
                m_selectedTextInput->clear();
                m_selectedTextInput->setPlaceholderText("Selected text will appear here...");
            } else {
                m_selectedTextInput->setText(selectedText);
                m_selectedTextInput->setPlaceholderText("");
            }
        }
    });
    
    // Enable mouse tracking for hover effects
    setMouseTracking(true);
}

PDFViewerWidget::~PDFViewerWidget()
{
    // Cancel any progressive rendering
    cancelProgressiveRender();
    
    // Stop all timers
    if (m_wheelThrottleTimer) {
        m_wheelThrottleTimer->stop();
    }
    if (m_wheelAccelTimer) {
        m_wheelAccelTimer->stop();
    }
    if (m_panThrottleTimer) {
        m_panThrottleTimer->stop();
    }
    
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
    int firstVisible = -1;
    int lastVisible = -1;
    
    // Improved visible page calculation
    float currentY = -m_scrollOffsetY;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        
        // Check if page is visible in viewport
        if (currentY + pageHeight >= 0 && currentY <= m_viewportHeight) {
            if (firstVisible == -1) {
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
    
    // Fallback if no visible pages found
    if (firstVisible == -1) {
        firstVisible = 0;
        lastVisible = 0;
    }
    
    // Add one page before and after for smooth scrolling
    firstVisible = std::max(0, firstVisible - 1);
    lastVisible = std::min(m_pageCount - 1, lastVisible + 1);
    
    qDebug() << "Updating visible pages" << firstVisible << "to" << lastVisible << "at zoom" << m_zoomLevel;
    
    // Ensure pageTextures vector is large enough
    if (static_cast<int>(m_pageTextures.size()) < m_pageCount) {
        m_pageTextures.resize(m_pageCount, 0);
    }
    
    // CRITICAL: Generate missing textures for all visible pages immediately
    std::vector<int> pagesToUpdate;
    for (int i = firstVisible; i <= lastVisible && i < m_pageCount; ++i) {
        // Always update texture if missing or if zoom has changed significantly
        bool needsUpdate = (m_pageTextures[i] == 0) || 
                          (std::abs(m_zoomLevel - m_lastRenderedZoom) > 0.015);
        
        if (needsUpdate) {
            pagesToUpdate.push_back(i);
            qDebug() << "Page" << i << "needs texture update (missing:" << (m_pageTextures[i] == 0) 
                     << ", zoom changed:" << (std::abs(m_zoomLevel - m_lastRenderedZoom) > 0.015) << ")";
        }
    }
    
    // Update textures immediately for visible pages to prevent blank content
    for (int pageIndex : pagesToUpdate) {
        if (pageIndex >= 0 && pageIndex < m_pageCount) {
            // Use the same logic as scheduleTextureUpdate but execute immediately
            float effectiveZoom = (m_zoomLevel > 0.5f) ? m_zoomLevel : 0.5f;
            float textureZoom = m_highZoomMode ? std::min(effectiveZoom, 2.5f) : std::min(effectiveZoom, 3.0f);
            
            int baseWidth, baseHeight;
            m_renderer->GetBestFitSize(pageIndex, m_viewportWidth, m_viewportHeight, baseWidth, baseHeight);
            
            int textureWidth = static_cast<int>(baseWidth * textureZoom);
            int textureHeight = static_cast<int>(baseHeight * textureZoom);
            
            const int MAX_TEXTURE_SIZE = m_highZoomMode ? 3072 : 4096;
            if (textureWidth > MAX_TEXTURE_SIZE || textureHeight > MAX_TEXTURE_SIZE) {
                float scaleFactor = std::min(
                    static_cast<float>(MAX_TEXTURE_SIZE) / textureWidth,
                    static_cast<float>(MAX_TEXTURE_SIZE) / textureHeight
                );
                textureWidth = static_cast<int>(textureWidth * scaleFactor);
                textureHeight = static_cast<int>(textureHeight * scaleFactor);
            }
            
            // Delete existing texture for this page if it exists
            if (m_pageTextures[pageIndex] != 0) {
                glDeleteTextures(1, &m_pageTextures[pageIndex]);
                m_pageTextures[pageIndex] = 0;
            }
            
            // Render and create texture
            FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(pageIndex, textureWidth, textureHeight);
            if (bitmap) {
                m_pageTextures[pageIndex] = CreateTextureFromPDFBitmap(bitmap, textureWidth, textureHeight);
                FPDFBitmap_Destroy(bitmap);
                qDebug() << "Successfully updated texture for page" << pageIndex;
            } else {
                qWarning() << "Failed to generate bitmap for page" << pageIndex;
                m_pageTextures[pageIndex] = 0;
            }
        }
    }
    
    // Update the last rendered zoom level
    if (!pagesToUpdate.empty()) {
        m_lastRenderedZoom = m_zoomLevel;
    }
    
    // Clean up unused textures to free memory (but be more conservative)
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
    
    // Render each visible page - generate missing textures on demand
    float currentY = -m_scrollOffsetY;
    
    for (int i = firstVisible; i <= lastVisible && i < m_pageCount; ++i) {
        // Calculate page position first to determine if page is actually visible
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
        float pageY = currentY;
        
        // Only process pages that are actually visible in the viewport
        if (pageY + pageHeight >= 0 && pageY <= m_viewportHeight) {
            // CRITICAL FIX: Generate texture immediately if missing
            if (i >= static_cast<int>(m_pageTextures.size()) || m_pageTextures[i] == 0) {
                qDebug() << "Generating missing texture for visible page" << i << "during render";
                
                // Ensure the pageTextures vector is large enough
                if (i >= static_cast<int>(m_pageTextures.size())) {
                    m_pageTextures.resize(m_pageCount, 0);
                }
                
                // Generate texture immediately using the same logic as scheduleTextureUpdate
                float effectiveZoom = (m_zoomLevel > 0.5f) ? m_zoomLevel : 0.5f;
                float textureZoom = m_highZoomMode ? std::min(effectiveZoom, 2.5f) : std::min(effectiveZoom, 3.0f);
                
                int baseWidth, baseHeight;
                m_renderer->GetBestFitSize(i, m_viewportWidth, m_viewportHeight, baseWidth, baseHeight);
                
                int textureWidth = static_cast<int>(baseWidth * textureZoom);
                int textureHeight = static_cast<int>(baseHeight * textureZoom);
                
                const int MAX_TEXTURE_SIZE = m_highZoomMode ? 3072 : 4096;
                if (textureWidth > MAX_TEXTURE_SIZE || textureHeight > MAX_TEXTURE_SIZE) {
                    float scaleFactor = std::min(
                        static_cast<float>(MAX_TEXTURE_SIZE) / textureWidth,
                        static_cast<float>(MAX_TEXTURE_SIZE) / textureHeight
                    );
                    textureWidth = static_cast<int>(textureWidth * scaleFactor);
                    textureHeight = static_cast<int>(textureHeight * scaleFactor);
                }
                
                // Render and create texture
                FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(i, textureWidth, textureHeight);
                if (bitmap) {
                    m_pageTextures[i] = CreateTextureFromPDFBitmap(bitmap, textureWidth, textureHeight);
                    FPDFBitmap_Destroy(bitmap);
                    qDebug() << "Successfully generated texture for page" << i;
                } else {
                    qWarning() << "Failed to generate bitmap for page" << i;
                    m_pageTextures[i] = 0;
                }
            }
            
            // Now render the page if we have a valid texture
            if (i < static_cast<int>(m_pageTextures.size()) && m_pageTextures[i] != 0) {
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
            } else {
                // Fallback: render a placeholder rectangle if texture generation failed
                qDebug() << "Rendering placeholder for page" << i;
                glDisable(GL_TEXTURE_2D);
                glColor3f(0.95f, 0.95f, 0.95f); // Light gray placeholder
                glBegin(GL_QUADS);
                glVertex2f(pageX, pageY);
                glVertex2f(pageX + pageWidth, pageY);
                glVertex2f(pageX + pageWidth, pageY + pageHeight);
                glVertex2f(pageX, pageY + pageHeight);
                glEnd();
                glColor3f(1.0f, 1.0f, 1.0f); // Reset color
                glEnable(GL_TEXTURE_2D);
            }
        }
        
        currentY += pageHeight + PAGE_MARGIN;
    }
    
    glDisable(GL_TEXTURE_2D);
    
    // Render text selection highlighting
    renderTextSelection();
    
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
        
        // Extract text from all pages
        extractTextFromAllPages();
        
        // CRITICAL FIX: Synchronize page dimensions with text extraction dimensions
        // This ensures mouse coordinates and text coordinates use the SAME scale
        for (int i = 0; i < m_pageCount && i < static_cast<int>(m_pageTexts.size()); ++i) {
            if (!m_pageTexts[i].isEmpty()) {
                qDebug() << "COORDINATE FIX: Synchronizing page" << i << "dimensions";
                qDebug() << "  Old renderer dimensions:" << m_pageWidths[i] << "x" << m_pageHeights[i];
                qDebug() << "  New PDF dimensions:" << m_pageTexts[i].pageWidth << "x" << m_pageTexts[i].pageHeight;
                
                // Use PDF dimensions from text extraction for ALL coordinate calculations
                m_pageWidths[i] = static_cast<int>(m_pageTexts[i].pageWidth);
                m_pageHeights[i] = static_cast<int>(m_pageTexts[i].pageHeight);
                
                qDebug() << "  Updated to PDF dimensions:" << m_pageWidths[i] << "x" << m_pageHeights[i];
            }
        }
        
        emit pdfLoaded(filePath);
        emit pageChanged(m_currentPage + 1, m_pageCount);
        emit zoomChanged(m_zoomLevel);
        
        // Initialize scroll bar after successful PDF load
        updateScrollBar();
        
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
    const int selectedTextHeight = 40;
    const int margin = 8;
    
    m_viewportWidth = w;
    m_viewportHeight = h - selectedTextHeight - 2 * margin;
    
    // Set OpenGL viewport to account for selected text input area
    glViewport(0, 0, w, h - selectedTextHeight - 2 * margin);
    
    if (m_isPDFLoaded) {
        // Use updateViewport to properly handle visible texture updates on OpenGL resize
        updateViewport();
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

QPointF PDFViewerWidget::screenToPDFCoordinates(const QPointF &screenPoint) const {
    if (!m_isPDFLoaded || m_pageCount == 0 || m_pageWidths.empty()) {
        return QPointF();
    }
    
    // Use the EXACT same logic as renderPDF() for coordinate conversion
    float currentY = -m_scrollOffsetY;
    
    qDebug() << "screenToPDFCoordinates DEBUG: screen" << screenPoint << "zoom" << m_zoomLevel;
    
    for (int i = 0; i < m_pageCount; ++i) {
        // NOW all page dimensions are synchronized - use m_pageWidths/Heights directly
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
        float pageY = currentY;
        
        qDebug() << "screenToPDFCoordinates DEBUG: page" << i << "pagePos(" << pageX << "," << pageY 
                 << ") size(" << pageWidth << "," << pageHeight << ")";
        
        // Check if screen point is within this page
        if (screenPoint.x() >= pageX && screenPoint.x() <= pageX + pageWidth &&
            screenPoint.y() >= pageY && screenPoint.y() <= pageY + pageHeight) {
            
            // Convert screen coordinates to PDF page coordinates 
            float pagePointX = (screenPoint.x() - pageX) / m_zoomLevel;
            float pagePointY = (screenPoint.y() - pageY) / m_zoomLevel;
            
            qDebug() << "screenToPDFCoordinates DEBUG: FOUND page" << i << "-> pagePoint(" << pagePointX << "," << pagePointY << ")";
            qDebug() << "COORDINATE FIX: Using synchronized PDF dimensions";
            
            // Return page coordinates in PDF coordinate system (same as text extraction)
            return QPointF(pagePointX, pagePointY);
        }
        
        // Move to next page
        currentY += pageHeight + PAGE_MARGIN;
    }
    
    // Point is not over any page
    qDebug() << "screenToPDFCoordinates DEBUG: NO PAGE FOUND";
    return QPointF();
}

QPointF PDFViewerWidget::pdfToPageCoordinates(const QPointF &pdfPoint, int pageIndex) const {
    if (!m_isPDFLoaded || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QPointF();
    }
    
    // Calculate page offset in the document
    float pageOffsetY = 0.0f;
    for (int i = 0; i < pageIndex; ++i) {
        if (i < static_cast<int>(m_pageHeights.size())) {
            pageOffsetY += m_pageHeights[i] + PAGE_MARGIN;
        }
    }
    
    // Convert PDF coordinates to page-relative coordinates
    float x = pdfPoint.x();
    float y = pdfPoint.y() - pageOffsetY;
    
    // Text coordinates are already flipped to top-left origin in text extraction,
    // so mouse coordinates should match directly without additional flipping
    qDebug() << "pdfToPageCoordinates: PDF point" << pdfPoint << "-> Page" << pageIndex << "-> Page point" << QPointF(x, y);
    
    return QPointF(x, y);
}

int PDFViewerWidget::getPageAtPoint(const QPointF &screenPoint) const {
    if (!m_isPDFLoaded || m_pageCount == 0) {
        return -1;
    }
    
    // Use the EXACT same logic as renderPDF() to find which page the screen point is on
    float currentY = -m_scrollOffsetY;
    
    qDebug() << "getPageAtPoint DEBUG: screen point" << screenPoint << "viewport" << m_viewportWidth << "x" << m_viewportHeight;
    qDebug() << "getPageAtPoint DEBUG: scroll offsets X:" << m_scrollOffsetX << "Y:" << m_scrollOffsetY;
    
    for (int i = 0; i < m_pageCount; ++i) {
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
        float pageY = currentY;
        
        qDebug() << "getPageAtPoint DEBUG: page" << i << "bounds: X[" << pageX << "to" << (pageX + pageWidth) 
                 << "] Y[" << pageY << "to" << (pageY + pageHeight) << "]";
        
        // Check both X and Y coordinates
        if (screenPoint.x() >= pageX && screenPoint.x() <= pageX + pageWidth &&
            screenPoint.y() >= pageY && screenPoint.y() <= pageY + pageHeight) {
            qDebug() << "getPageAtPoint DEBUG: FOUND page" << i;
            return i;
        }
        
        currentY += pageHeight + PAGE_MARGIN;
    }
    
    qDebug() << "getPageAtPoint DEBUG: NO PAGE FOUND";
    return -1; // Point is not over any page
}

void PDFViewerWidget::setupUI() 
{
    // Create selected text input box at the top
    m_selectedTextInput = new QLineEdit(this);
    m_selectedTextInput->setPlaceholderText("Selected text will appear here...");
    m_selectedTextInput->setReadOnly(false); // Allow user to select and copy text
    m_selectedTextInput->setStyleSheet(
        "QLineEdit {"
        "    background-color: #f8f9fa;"
        "    border: 2px solid #dee2e6;"
        "    border-radius: 6px;"
        "    padding: 8px 12px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    font-size: 12px;"
        "    color: #495057;"
        "    selection-background-color: #007acc;"
        "    selection-color: white;"
        "}"
        "QLineEdit:focus {"
        "    border-color: #007acc;"
        "    background-color: #ffffff;"
        "    outline: none;"
        "}"
        "QLineEdit:hover {"
        "    border-color: #adb5bd;"
        "}"
    );
    m_selectedTextInput->setFixedHeight(40);
    m_selectedTextInput->setVisible(true);
    
    // Add context menu for copy/select all
    m_selectedTextInput->setContextMenuPolicy(Qt::ActionsContextMenu);
    
    QAction* copyAction = new QAction("Copy", m_selectedTextInput);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, m_selectedTextInput, &QLineEdit::copy);
    m_selectedTextInput->addAction(copyAction);
    
    QAction* selectAllAction = new QAction("Select All", m_selectedTextInput);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, m_selectedTextInput, &QLineEdit::selectAll);
    m_selectedTextInput->addAction(selectAllAction);
    
    QAction* clearAction = new QAction("Clear", m_selectedTextInput);
    connect(clearAction, &QAction::triggered, [this]() {
        m_selectedTextInput->clear();
        clearTextSelection();
    });
    m_selectedTextInput->addAction(clearAction);

    // Create and setup vertical scroll bar
    m_verticalScrollBar = new QScrollBar(Qt::Vertical, this);
    m_verticalScrollBar->setVisible(false); // Hide initially until PDF is loaded
    m_verticalScrollBar->setMinimum(0);
    m_verticalScrollBar->setMaximum(100);
    m_verticalScrollBar->setValue(0);
    m_verticalScrollBar->setSingleStep(10);
    m_verticalScrollBar->setPageStep(50);
    
    // Connect scroll bar signal
    connect(m_verticalScrollBar, QOverload<int>::of(&QScrollBar::valueChanged),
            this, &PDFViewerWidget::onVerticalScrollBarChanged);
    
    // Position the widgets - this will be updated in resizeEvent
}

void PDFViewerWidget::setupToolbar() { /* Implement toolbar setup */ }
void PDFViewerWidget::createContextMenu() 
{ 
    m_contextMenu = new QMenu(this);
    
    // Zoom actions
    m_zoomInAction = m_contextMenu->addAction("Zoom In (Mouse Wheel Up)");
    m_zoomInAction->setShortcut(QKeySequence("Ctrl+="));
    connect(m_zoomInAction, &QAction::triggered, this, &PDFViewerWidget::zoomIn);
    
    m_zoomOutAction = m_contextMenu->addAction("Zoom Out (Mouse Wheel Down)");
    m_zoomOutAction->setShortcut(QKeySequence("Ctrl+-"));
    connect(m_zoomOutAction, &QAction::triggered, this, &PDFViewerWidget::zoomOut);
    
    m_zoomFitAction = m_contextMenu->addAction("Zoom to Fit");
    m_zoomFitAction->setShortcut(QKeySequence("Ctrl+0"));
    connect(m_zoomFitAction, &QAction::triggered, this, &PDFViewerWidget::zoomToFit);
    
    m_zoomWidthAction = m_contextMenu->addAction("Zoom to Width");
    connect(m_zoomWidthAction, &QAction::triggered, this, &PDFViewerWidget::zoomToWidth);
    
    m_contextMenu->addSeparator();
    
    // Control explanations
    QAction* controlsAction = m_contextMenu->addAction("Controls:");
    controlsAction->setEnabled(false); // Make it a label
    
    QAction* zoomHelpAction = m_contextMenu->addAction("  • Mouse Wheel = Zoom");
    zoomHelpAction->setEnabled(false);
    
    QAction* scrollHelpAction = m_contextMenu->addAction("  • Ctrl + Mouse Wheel = Scroll");
    scrollHelpAction->setEnabled(false);
    
    QAction* panHelpAction = m_contextMenu->addAction("  • Right Mouse Button = Pan");
    panHelpAction->setEnabled(false);
    
    QAction* menuHelpAction = m_contextMenu->addAction("  • Ctrl + Right Click = Menu");
    menuHelpAction->setEnabled(false);
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
    
    // Check if we need texture updates using optimized high zoom logic
    bool needsFullRegeneration = false;
    bool needsVisibleRegeneration = false;
    
    double zoomDifference = std::abs(newZoom - m_lastRenderedZoom) / m_lastRenderedZoom;
    
    // More aggressive thresholds like standalone viewer for smoother zooming
    // At high zoom levels, use progressive rendering instead of immediate texture updates
    if (m_zoomLevel > 2.5f) {
        // High zoom mode: use progressive rendering for smooth experience
        if (zoomDifference > 0.02f) { // Lower threshold for better quality
            startProgressiveRender();
        }
    } else {
        // Low zoom mode: use immediate texture updates
        float baseThreshold = 0.0005f;
        float fullRegenThreshold = 0.015f;
        
        if (m_immediateRenderRequired && zoomDifference > baseThreshold) {
            needsVisibleRegeneration = true;
            m_immediateRenderRequired = false;
        }
        else if (zoomDifference > fullRegenThreshold) {
            needsFullRegeneration = true;
            m_lastRenderedZoom = newZoom;
        }
        
        if (needsFullRegeneration || needsVisibleRegeneration) {
            qDebug() << "Texture update triggered:" << (needsFullRegeneration ? "Full" : "Visible") << "zoom:" << m_zoomLevel;
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
    
    // Check if we need texture updates using optimized high zoom logic
    double zoomDifference = std::abs(m_zoomLevel - m_lastRenderedZoom) / m_lastRenderedZoom;
    
    // Use progressive rendering at high zoom for smooth experience
    if (m_zoomLevel > 2.5f) {
        // High zoom mode: use progressive rendering
        if (zoomDifference > 0.02f) {
            startProgressiveRender();
        }
    } else {
        // Low zoom mode: use immediate texture updates with optimized thresholds
        float baseThreshold = 0.0005f;
        float fullRegenThreshold = 0.015f;
        
        if (m_immediateRenderRequired && zoomDifference > baseThreshold) {
            m_immediateRenderRequired = false;
            
            qDebug() << "Texture update in setZoomLevel: Visible update, Old:" << oldZoom << "New:" << m_zoomLevel;
            if (context() && context()->isValid()) {
                makeCurrent();
                updateVisibleTextures();
                doneCurrent();
            }
        }
        else if (zoomDifference > fullRegenThreshold) {
            m_lastRenderedZoom = m_zoomLevel;
            
            qDebug() << "Texture update in setZoomLevel: Full update, Old:" << oldZoom << "New:" << m_zoomLevel;
            if (context() && context()->isValid()) {
                makeCurrent();
                updateTextures();
                doneCurrent();
            }
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
    
    // Update scroll bar since zoom changes affect scroll range
    updateScrollBar();
    
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

void PDFViewerWidget::onVerticalScrollBarChanged(int value)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Convert scroll bar value (0-100) to actual scroll offset
    float scrollRange = m_maxScrollY;
    float newScrollY = (value / 100.0f) * scrollRange;
    
    // Update scroll position
    m_scrollOffsetY = newScrollY;
    
    // Update scroll state and trigger texture updates
    updateScrollState();
    
    // CRITICAL: Trigger texture updates immediately for scroll bar navigation
    // This ensures visible content is always rendered during scrolling
    if (context() && context()->isValid()) {
        makeCurrent();
        updateVisibleTextures();
        doneCurrent();
    }
    
    // Use progressive rendering for smooth scrolling at high zoom
    if (m_zoomLevel > 2.5f && !m_progressiveRenderActive) {
        startProgressiveRender();
    } else {
        update();
    }
}

/*
 * ====================================================================
 * EVENT HANDLING - NEW KEY MAPPINGS FOR IMPROVED UX
 * ====================================================================
 * 
 * NEW CONTROL SCHEME (matching user requirements):
 * 1. Mouse wheel alone           → Zooming (no Ctrl required)
 * 2. Ctrl + Mouse wheel          → Vertical scrolling  
 * 3. Right mouse button drag     → Panning (horizontal & vertical)
 * 4. Ctrl + Right click          → Context menu (zoom options, search, etc.)
 * 
 * PERFORMANCE OPTIMIZATIONS:
 * - Wheel and pan event throttling at high zoom levels (>2.5x)
 * - Progressive rendering for smooth experience
 * - Batch processing of rapid events to prevent lag
 * - Optimized texture updates for visible content only
 * 
 * NOTE: Regular right-click no longer shows context menu to avoid
 * interference with panning. Use Ctrl + Right-click for menu access.
 * ====================================================================
 */

// Event handlers
void PDFViewerWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // NEW KEY MAPPING:
    // - Mouse wheel alone = Zooming (like standalone viewer)
    // - Ctrl + Mouse wheel = Scrolling
    bool shouldScroll = (event->modifiers() & Qt::ControlModifier);
    
    if (shouldScroll) {
        // Scroll vertically with optimized visible texture updates when Ctrl is pressed
        float scrollDelta = -event->angleDelta().y() * 0.5f;
        
        // At high zoom, use smaller scroll steps for precision
        if (m_zoomLevel > 2.5f) {
            scrollDelta *= 0.5f; // Finer control at high zoom
        }
        
        m_scrollOffsetY = std::clamp(m_scrollOffsetY + scrollDelta, 0.0f, m_maxScrollY);
        
        // Update scroll state and trigger optimized visible texture updates
        updateScrollState();
        
        // Update scroll bar position
        updateScrollBar();
        
        // CRITICAL: Trigger texture updates for wheel scrolling to ensure visible content
        if (context() && context()->isValid()) {
            makeCurrent();
            updateVisibleTextures();
            doneCurrent();
        }
        
        // Use progressive rendering for smooth scrolling at high zoom
        if (m_zoomLevel > 2.5f && !m_progressiveRenderActive) {
            startProgressiveRender();
        } else {
            update();
        }
        
        event->accept();
    } else {
        // DEFAULT: Mouse wheel alone always zooms (no Ctrl required, like standalone viewer)
        // OPTIMIZED HIGH ZOOM WHEEL HANDLING
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        
        // At high zoom levels, enable throttling for better performance
        bool enableThrottling = (m_zoomLevel > 2.5f);
        
        if (enableThrottling) {
            // Accumulate wheel delta for batch processing
            double delta = event->angleDelta().y() > 0 ? 1.1 : (1.0 / 1.1);
            m_pendingZoomDelta *= delta; // Multiply for compound effect
            m_pendingWheelCursor = event->position().toPoint();
            m_wheelEventCount++;
            m_lastWheelTime = currentTime;
            
            // Start throttle timer if not active
            if (!m_wheelThrottleActive) {
                m_wheelThrottleActive = true;
                m_wheelThrottleTimer->start();
                m_pendingZoomDelta = delta; // Initialize on first event
            }
            
            // Reset acceleration timer
            m_wheelAccelTimer->start();
            
            // Limit events per batch to prevent lag
            if (m_wheelEventCount >= MAX_WHEEL_EVENTS_PER_BATCH) {
                processThrottledWheelEvent();
            }
        } else {
            // Standard immediate processing for low zoom levels
            QPoint cursorPos = event->position().toPoint();
            bool zoomIn = event->angleDelta().y() > 0;
            performCursorBasedZoom(cursorPos, zoomIn);
        }
        
        event->accept();
    }
}

void PDFViewerWidget::mousePressEvent(QMouseEvent *event)
{
    // NEW KEY MAPPING: Right mouse button for panning
    if (event->button() == Qt::RightButton) {
        m_isDragging = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    
    // Left mouse button for text selection
    if (event->button() == Qt::LeftButton) {
        if (m_isPDFLoaded && m_textSelection) {
            // Convert screen coordinates to page coordinates
            QPointF pagePoint = screenToPDFCoordinates(QPointF(event->pos()));
            int pageIndex = getPageAtPoint(QPointF(event->pos())); // Use screen coordinates for page detection
            
            qDebug() << "=== MOUSE CLICK DEBUG ===";
            qDebug() << "Screen pos:" << event->pos();
            qDebug() << "Page index:" << pageIndex;
            qDebug() << "Page point:" << pagePoint;
            if (pageIndex >= 0 && pageIndex < m_pageTexts.size()) {
                qDebug() << "Page dimensions:" << m_pageTexts[pageIndex].pageWidth << "x" << m_pageTexts[pageIndex].pageHeight;
                qDebug() << "Characters on page:" << m_pageTexts[pageIndex].characters.size();
                if (!m_pageTexts[pageIndex].characters.empty()) {
                    qDebug() << "First char bounds:" << m_pageTexts[pageIndex].characters[0].bounds;
                    qDebug() << "Last char bounds:" << m_pageTexts[pageIndex].characters.back().bounds;
                }
            }
            qDebug() << "======================";
            
            if (pageIndex >= 0) {
                // Start text selection with page coordinates (no additional conversion needed)
                m_textSelection->startSelection(pageIndex, pagePoint);
                m_isTextSelecting = true;
                m_lastMousePos = QPointF(event->pos());
                
                qDebug() << "Started text selection at page" << pageIndex << "Page point" << pagePoint;
                update(); // Trigger repaint
            }
        }
        event->accept();
        return;
    }
    
    event->accept();
}

void PDFViewerWidget::mouseMoveEvent(QMouseEvent *event)
{
    // Handle text selection dragging
    if (m_isTextSelecting && (event->buttons() & Qt::LeftButton)) {
        if (m_textSelection) {
            // Convert screen coordinates to page coordinates
            QPointF pagePoint = screenToPDFCoordinates(QPointF(event->pos()));
            int pageIndex = getPageAtPoint(QPointF(event->pos())); // Use screen coordinates for page detection
            
            qDebug() << "Mouse drag at screen:" << event->pos() << "-> Page:" << pageIndex << "-> PagePoint:" << pagePoint;
            
            if (pageIndex >= 0) {
                // Update text selection with page coordinates (no additional conversion needed)
                m_textSelection->updateSelection(pageIndex, pagePoint);
                update(); // Trigger repaint to show selection
            }
        }
        event->accept();
        return;
    }
    
    // NEW KEY MAPPING: Right mouse button for panning
    if (m_isDragging && (event->buttons() & Qt::RightButton)) {
        QPoint delta = event->pos() - m_lastPanPoint;
        
        // OPTIMIZED HIGH ZOOM PANNING HANDLING
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        
        // At high zoom levels, enable throttling for better performance
        bool enableThrottling = (m_zoomLevel > 2.5f);
        
        if (enableThrottling) {
            // Accumulate pan delta for batch processing
            m_pendingPanDelta += delta;
            m_panEventCount++;
            m_lastPanTime = currentTime;
            
            // Start throttle timer if not active
            if (!m_panThrottleActive) {
                m_panThrottleActive = true;
                m_panThrottleTimer->start();
            }
            
            // Limit events per batch to prevent lag
            if (m_panEventCount >= MAX_PAN_EVENTS_PER_BATCH) {
                processThrottledPanEvent();
            }
            
            // For immediate feedback, do a lightweight scroll update without texture regeneration
            float newScrollX = m_scrollOffsetX - delta.x();
            float newScrollY = m_scrollOffsetY - delta.y();
            m_scrollOffsetX = std::clamp(newScrollX, m_minScrollX, m_maxScrollX);
            m_scrollOffsetY = std::clamp(newScrollY, 0.0f, m_maxScrollY);
            
            // Update scroll bar for immediate visual feedback
            updateScrollBar();
            
            update(); // Quick repaint without texture updates
        } else {
            // Standard immediate processing for low zoom levels
            handlePanning(delta);
        }
        
        m_lastPanPoint = event->pos();
    }
    event->accept();
}

void PDFViewerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // NEW KEY MAPPING: Right mouse button for panning
    if (event->button() == Qt::RightButton && m_isDragging) {
        m_isDragging = false;
        setCursor(Qt::ArrowCursor); // Return to normal cursor
        
        // Process any remaining panning events when drag ends
        if (m_panThrottleActive) {
            processThrottledPanEvent();
        }
        
        // If we're at high zoom and have been panning, ensure final texture update
        if (m_zoomLevel > 2.5f && !m_progressiveRenderActive) {
            startProgressiveRender();
        }
        
        event->accept();
        return;
    }
    
    // Handle left mouse button release for text selection
    if (event->button() == Qt::LeftButton) {
        if (m_isTextSelecting && m_textSelection) {
            // End text selection
            m_textSelection->endSelection();
            m_isTextSelecting = false;
            
            // Get selected text and emit signal
            QString selectedText = getSelectedText();
            if (!selectedText.isEmpty()) {
                emit textSelectionChanged(selectedText);
                qDebug() << "Text selection completed:" << selectedText.left(50) + "...";
            }
            
            update(); // Final repaint
        }
        event->accept();
        return;
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
                updateScrollState();
                updateScrollBar();
                if (m_zoomLevel > 2.5f && !m_progressiveRenderActive) {
                    startProgressiveRender();
                } else {
                    update();
                }
            }
            event->accept();
            return;
        case Qt::Key_End:
            if (event->modifiers() & Qt::ControlModifier) {
                goToLastPage();
            } else {
                m_scrollOffsetY = m_maxScrollY;
                updateScrollState();
                updateScrollBar();
                if (m_zoomLevel > 2.5f && !m_progressiveRenderActive) {
                    startProgressiveRender();
                } else {
                    update();
                }
            }
            event->accept();
            return;
        case Qt::Key_Z:
            // Removed old zoom mode toggle functionality
            // Mouse wheel now directly controls zoom without mode switching
            break;
    }
    
    QOpenGLWidget::keyPressEvent(event);
}

void PDFViewerWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // NEW BEHAVIOR: Only show context menu with Ctrl+Right-click
    // Regular right-click is reserved for panning
    if (event->modifiers() & Qt::ControlModifier) {
        if (m_contextMenu) {
            m_contextMenu->exec(event->globalPos());
        }
    }
    // If no Ctrl modifier, just accept the event without showing menu
    // This allows right-click to be used purely for panning
    event->accept();
}

void PDFViewerWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    
    const int selectedTextHeight = 40;
    const int margin = 8;
    
    // Position the selected text input box at the top
    if (m_selectedTextInput) {
        m_selectedTextInput->setGeometry(
            margin,
            margin,
            event->size().width() - 2 * margin,
            selectedTextHeight
        );
    }
    
    // Update viewport dimensions (reserve space for selected text input)
    m_viewportWidth = event->size().width();
    m_viewportHeight = event->size().height() - selectedTextHeight - 2 * margin;
    
    // Position the vertical scroll bar on the right side
    if (m_verticalScrollBar) {
        const int scrollBarWidth = 16; // Standard scroll bar width
        const int scrollBarMargin = 2; // Small margin from edge
        
        // Reserve space for scroll bar in viewport width
        if (m_verticalScrollBar->isVisible()) {
            m_viewportWidth -= (scrollBarWidth + scrollBarMargin);
        }
        
        // Position scroll bar at the right edge (below selected text input)
        m_verticalScrollBar->setGeometry(
            width() - scrollBarWidth - scrollBarMargin,
            selectedTextHeight + 2 * margin,
            scrollBarWidth,
            height() - selectedTextHeight - 2 * margin
        );
    }
    
    if (m_isPDFLoaded) {
        // Use updateViewport to properly handle visible texture updates on resize
        updateViewport();
    }
}

/*
 * NEW KEY MAPPINGS FOR PDF VIEWER:
 * 
 * ZOOMING:
 * - Mouse Wheel (no modifiers) = Zoom in/out at cursor position
 * - Ctrl + Plus/Minus keys = Zoom in/out
 * - Ctrl + 0 = Reset zoom to fit
 * 
 * SCROLLING:
 * - Ctrl + Mouse Wheel = Scroll up/down
 * - Page Up/Down keys = Previous/Next page
 * - Home/End keys = Scroll to top/bottom
 * - Ctrl + Home/End = Go to first/last page
 * 
 * PANNING:
 * - Right Mouse Button + Drag = Pan document
 */

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

// Auto-centering functionality like standalone viewer
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

// View management functions for scroll/pan texture updates
void PDFViewerWidget::updateScrollState()
{
    if (!m_isPDFLoaded || !m_scrollState) {
        return;
    }
    
    // Update scroll state with current viewport and position
    m_scrollState->scrollPosition = QPointF(m_scrollOffsetX, m_scrollOffsetY);
    m_scrollState->zoomLevel = m_zoomLevel;
    m_scrollState->viewportSize = QSizeF(m_viewportWidth, m_viewportHeight);
    
    // OPTIMIZED: Only trigger texture updates at specific intervals during high zoom panning
    static QPointF lastScrollPos = QPointF(0, 0);
    static double lastZoom = 0.0;
    static qint64 lastTextureUpdate = 0;
    
    QPointF currentPos = m_scrollState->scrollPosition;
    double scrollDelta = QLineF(lastScrollPos, currentPos).length();
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    // Use different thresholds based on zoom level and throttle status
    bool shouldUpdateTextures = false;
    
    if (m_zoomLevel > 2.5f) {
        // High zoom: be more conservative about texture updates
        // Only update if significant movement AND enough time has passed
        bool significantMovement = scrollDelta > 100.0; // Higher threshold
        bool enoughTimePassed = (currentTime - lastTextureUpdate) > 150; // 150ms minimum interval
        bool zoomChanged = std::abs(lastZoom - m_zoomLevel) > 0.01;
        
        shouldUpdateTextures = (significantMovement && enoughTimePassed) || zoomChanged;
    } else {
        // Low zoom: use original logic for responsive updates
        shouldUpdateTextures = scrollDelta > 50.0 || std::abs(lastZoom - m_zoomLevel) > 0.01;
    }
    
    if (shouldUpdateTextures) {
        // Don't update textures if we're actively throttling pan events
        if (!m_panThrottleActive) {
            updateVisibleTextures();
            lastTextureUpdate = currentTime;
        }
        lastScrollPos = currentPos;
        lastZoom = m_zoomLevel;
    }
}

void PDFViewerWidget::updateViewport()
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Recalculate viewport-dependent values
    calculatePageLayout();
    
    // Update scroll limits
    float totalHeight = 0.0f;
    float maxWidth = 0.0f;
    
    for (int i = 0; i < m_pageCount; ++i) {
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        totalHeight += pageHeight + PAGE_MARGIN;
        maxWidth = std::max(maxWidth, pageWidth);
    }
    
    // Update scroll boundaries
    m_maxScrollY = std::max(0.0f, totalHeight - m_viewportHeight);
    m_maxScrollX = std::max(0.0f, maxWidth - m_viewportWidth);
    m_minScrollX = (maxWidth <= m_viewportWidth) ? 0.0f : -std::max(0.0f, (m_viewportWidth - maxWidth) / 2.0f);
    
    // Clamp current scroll position to new boundaries
    m_scrollOffsetX = std::clamp(m_scrollOffsetX, m_minScrollX, m_maxScrollX);
    m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0.0f, m_maxScrollY);
    
    // Update visible textures for new viewport
    updateVisibleTextures();
    
    // Update scroll bar
    updateScrollBar();
}

void PDFViewerWidget::updateScrollBar()
{
    if (!m_verticalScrollBar || !m_isPDFLoaded) {
        return;
    }
    
    // Show scroll bar only if there's content to scroll
    bool hasVerticalScroll = m_maxScrollY > 0.0f;
    m_verticalScrollBar->setVisible(hasVerticalScroll);
    
    if (hasVerticalScroll) {
        // Block signals to prevent recursive calls
        m_verticalScrollBar->blockSignals(true);
        
        // Update scroll bar range and value
        m_verticalScrollBar->setMinimum(0);
        m_verticalScrollBar->setMaximum(100); // Use percentage-based scrolling
        
        // Calculate current scroll position as percentage
        int currentValue = (m_maxScrollY > 0) ? (int)((m_scrollOffsetY / m_maxScrollY) * 100) : 0;
        m_verticalScrollBar->setValue(currentValue);
        
        // Calculate page step based on viewport height
        float pageStepPercent = (m_viewportHeight / (m_maxScrollY + m_viewportHeight)) * 100;
        m_verticalScrollBar->setPageStep(qMax(1, (int)pageStepPercent));
        m_verticalScrollBar->setSingleStep(5); // 5% per step
        
        m_verticalScrollBar->blockSignals(false);
    }
}

void PDFViewerWidget::handlePanning(const QPoint &delta)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Apply panning delta with proper clamping
    float newScrollX = m_scrollOffsetX - delta.x();
    float newScrollY = m_scrollOffsetY - delta.y();
    
    m_scrollOffsetX = std::clamp(newScrollX, m_minScrollX, m_maxScrollX);
    m_scrollOffsetY = std::clamp(newScrollY, 0.0f, m_maxScrollY);
    
    // Update scroll state
    updateScrollState();
    
    // Update scroll bar position
    updateScrollBar();
    
    // CRITICAL: Trigger texture updates for panning to ensure visible content
    if (context() && context()->isValid()) {
        makeCurrent();
        updateVisibleTextures();
        doneCurrent();
    }
    
    // At high zoom, use progressive rendering for smooth experience
    if (m_zoomLevel > 2.5f && !m_progressiveRenderActive) {
        startProgressiveRender();
    } else {
        // Immediate redraw for low zoom
        update();
    }
}

// ==============================================================================
// HIGH ZOOM PERFORMANCE OPTIMIZATION FUNCTIONS
// ==============================================================================

void PDFViewerWidget::processThrottledWheelEvent()
{
    if (!m_isPDFLoaded || m_pendingZoomDelta == 0.0) {
        m_wheelThrottleActive = false;
        return;
    }
    
    // Process the accumulated zoom delta
    handleWheelEventBatch(m_pendingZoomDelta, m_pendingWheelCursor);
    
    // Reset state
    m_pendingZoomDelta = 0.0;
    m_wheelEventCount = 0;
    m_wheelThrottleActive = false;
}

void PDFViewerWidget::resetWheelAcceleration()
{
    // Reset acceleration if no wheel events for a while
    m_wheelEventCount = 0;
    
    // If throttle is still active, process remaining events
    if (m_wheelThrottleActive) {
        processThrottledWheelEvent();
    }
}

void PDFViewerWidget::handleWheelEventBatch(double totalDelta, const QPoint& cursorPos)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Calculate the new zoom level from accumulated delta
    double newZoom = std::clamp(m_zoomLevel * totalDelta, MIN_ZOOM, MAX_ZOOM);
    
    // If zoom level doesn't actually change, nothing to do
    if (std::abs(newZoom - m_zoomLevel) < 0.0005) {
        return;
    }
    
    // Store old zoom level
    double oldZoom = m_zoomLevel;
    
    // Convert cursor position to document coordinates (same as performCursorBasedZoom)
    float maxPageWidth = 0.0f;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageWidth = m_pageWidths[i] * oldZoom;
        maxPageWidth = std::max(maxPageWidth, pageWidth);
    }
    
    float currentPageX = (m_viewportWidth - maxPageWidth) / 2.0f - m_scrollOffsetX;
    double docX = (cursorPos.x() - currentPageX) / oldZoom;
    double docY = (cursorPos.y() + m_scrollOffsetY) / oldZoom;
    
    // Update zoom level
    m_zoomLevel = newZoom;
    
    // Recalculate page layout with new zoom
    calculatePageLayout();
    
    // Calculate new scroll position to keep the document point under cursor
    float newMaxPageWidth = 0.0f;
    for (int i = 0; i < m_pageCount; ++i) {
        float pageWidth = m_pageWidths[i] * newZoom;
        newMaxPageWidth = std::max(newMaxPageWidth, pageWidth);
    }
    
    float newPageX = cursorPos.x() - docX * newZoom;
    double newScrollX = (m_viewportWidth - newMaxPageWidth) / 2.0f - newPageX;
    double newScrollY = docY * newZoom - cursorPos.y();
    
    // Clamp to valid scroll range using the updated limits
    m_scrollOffsetX = std::clamp(static_cast<float>(newScrollX), m_minScrollX, m_maxScrollX);
    m_scrollOffsetY = std::clamp(static_cast<float>(newScrollY), 0.0f, m_maxScrollY);
    
    // IMMEDIATE RENDERING for responsive feel
    update();
    
    // Use progressive rendering for texture updates at high zoom
    if (m_zoomLevel > 2.5f) {
        startProgressiveRender();
    } else {
        // Standard texture update logic for low zoom
        double zoomDifference = std::abs(newZoom - m_lastRenderedZoom) / m_lastRenderedZoom;
        if (zoomDifference > 0.015f) {
            if (context() && context()->isValid()) {
                makeCurrent();
                updateVisibleTextures();
                doneCurrent();
            }
            m_lastRenderedZoom = newZoom;
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

void PDFViewerWidget::startProgressiveRender()
{
    if (m_progressiveRenderActive || !m_isPDFLoaded) {
        return;
    }
    
    // Cancel any existing progressive render
    cancelProgressiveRender();
    
    // Calculate visible page range for progressive updating
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
        if (currentY > m_viewportHeight + 500) {
            break;
        }
    }
    
    // Add buffer pages for smooth scrolling
    firstVisible = std::max(0, firstVisible - 1);
    lastVisible = std::min(m_pageCount - 1, lastVisible + 1);
    
    // Set up progressive rendering
    m_pendingTextureUpdates.clear();
    for (int i = firstVisible; i <= lastVisible; ++i) {
        m_pendingTextureUpdates.push_back(i);
    }
    
    m_progressiveRenderActive = true;
    m_progressiveRenderTimer->start();
}

void PDFViewerWidget::processProgressiveRender()
{
    if (m_pendingTextureUpdates.empty() || !m_isPDFLoaded) {
        cancelProgressiveRender();
        return;
    }
    
    // Process one page at a time for smooth experience
    int pageIndex = m_pendingTextureUpdates.front();
    m_pendingTextureUpdates.erase(m_pendingTextureUpdates.begin());
    
    if (pageIndex >= 0 && pageIndex < m_pageCount) {
        // Update texture for this page only
        if (context() && context()->isValid()) {
            makeCurrent();
            
            // Use optimized high zoom rendering
            float effectiveZoom = std::min(static_cast<float>(m_zoomLevel), 3.0f);
            
            int baseWidth, baseHeight;
            m_renderer->GetBestFitSize(pageIndex, m_viewportWidth, m_viewportHeight, baseWidth, baseHeight);
            
            int textureWidth = static_cast<int>(baseWidth * effectiveZoom);
            int textureHeight = static_cast<int>(baseHeight * effectiveZoom);
            
            // Cap texture size
            const int MAX_TEXTURE_SIZE = 4096;
            if (textureWidth > MAX_TEXTURE_SIZE || textureHeight > MAX_TEXTURE_SIZE) {
                float scaleFactor = std::min(
                    static_cast<float>(MAX_TEXTURE_SIZE) / textureWidth,
                    static_cast<float>(MAX_TEXTURE_SIZE) / textureHeight
                );
                textureWidth = static_cast<int>(textureWidth * scaleFactor);
                textureHeight = static_cast<int>(textureHeight * scaleFactor);
            }
            
            // Clean up old texture
            if (pageIndex < static_cast<int>(m_pageTextures.size()) && m_pageTextures[pageIndex] != 0) {
                glDeleteTextures(1, &m_pageTextures[pageIndex]);
            }
            
            // Render new texture
            FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(pageIndex, textureWidth, textureHeight);
            if (bitmap) {
                m_pageTextures[pageIndex] = CreateTextureFromPDFBitmap(bitmap, textureWidth, textureHeight);
                FPDFBitmap_Destroy(bitmap);
            }
            
            doneCurrent();
        }
        
        // Trigger update for immediate feedback
        update();
    }
    
    // Continue processing if more pages remain
    if (m_pendingTextureUpdates.empty()) {
        cancelProgressiveRender();
        m_lastRenderedZoom = m_zoomLevel; // Update tracking zoom level
    }
}

void PDFViewerWidget::cancelProgressiveRender()
{
    if (m_progressiveRenderActive) {
        m_progressiveRenderTimer->stop();
        m_progressiveRenderActive = false;
        m_pendingTextureUpdates.clear();
    }
}

void PDFViewerWidget::processThrottledPanEvent()
{
    if (!m_isPDFLoaded || (m_pendingPanDelta.x() == 0 && m_pendingPanDelta.y() == 0)) {
        m_panThrottleActive = false;
        return;
    }
    
    // Process the accumulated pan delta
    handlePanEventBatch(m_pendingPanDelta);
    
    // Reset state
    m_pendingPanDelta = QPoint(0, 0);
    m_panEventCount = 0;
    m_panThrottleActive = false;
}

void PDFViewerWidget::handlePanEventBatch(const QPoint& totalDelta)
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Apply the accumulated panning delta
    float newScrollX = m_scrollOffsetX - totalDelta.x();
    float newScrollY = m_scrollOffsetY - totalDelta.y();
    
    m_scrollOffsetX = std::clamp(newScrollX, m_minScrollX, m_maxScrollX);
    m_scrollOffsetY = std::clamp(newScrollY, 0.0f, m_maxScrollY);
    
    // Update scroll state
    updateScrollState();
    
    // Update scroll bar position
    updateScrollBar();
    
    // At high zoom, use progressive rendering for texture updates
    if (m_zoomLevel > 2.5f) {
        if (!m_progressiveRenderActive) {
            startProgressiveRender();
        }
    } else {
        // For low zoom, trigger immediate visible texture updates
        if (context() && context()->isValid()) {
            makeCurrent();
            updateVisibleTextures();
            doneCurrent();
        }
        update();
    }
}

void PDFViewerWidget::resetPanThrottling()
{
    // Reset panning throttling state
    m_panEventCount = 0;
    
    // If throttle is still active, process remaining events
    if (m_panThrottleActive) {
        processThrottledPanEvent();
    }
}

// Text Extraction and Selection Methods
void PDFViewerWidget::extractTextFromAllPages() {
    if (!m_textExtractor || !m_renderer || !m_isPDFLoaded) {
        return;
    }

    qDebug() << "Starting text extraction from" << m_pageCount << "pages";
    
    // Clear any existing text data
    m_pageTexts.clear();
    m_pageTexts.resize(m_pageCount);
    
    // Extract text from each page
    for (int i = 0; i < m_pageCount; ++i) {
        qDebug() << "Extracting text from page" << (i + 1);
        
        // Get PDFium document from renderer
        void* document = m_renderer->GetDocument();
        if (document) {
            m_pageTexts[i] = m_textExtractor->extractPageText(document, i);
            qDebug() << "Page" << (i + 1) << "text extraction complete. Lines:" << m_pageTexts[i].lines.size();
        }
    }
    
    m_textExtractionComplete = true;
    qDebug() << "Text extraction from all pages complete";
}

void PDFViewerWidget::startTextSelection(const QPointF& startPoint)
{
    if (!m_isPDFLoaded || !m_textSelection) {
        return;
    }
    
    // Convert screen coordinates to PDF coordinates
    QPointF pdfPoint = screenToPDFCoordinates(startPoint);
    
    // Determine which page the point is on
    int pageIndex = getPageAtPoint(pdfPoint);
    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        return;
    }
    
    // Convert to page-relative coordinates
    QPointF pagePoint = pdfToPageCoordinates(pdfPoint, pageIndex);
    
    m_textSelection->startSelection(pageIndex, pagePoint);
    m_isTextSelecting = true;
    
    qDebug() << "Started text selection at page" << pageIndex << "point" << pagePoint;
}

void PDFViewerWidget::updateTextSelection(const QPointF& currentPoint)
{
    if (!m_isPDFLoaded || !m_textSelection || !m_isTextSelecting) {
        return;
    }
    
    // Convert screen coordinates to PDF coordinates
    QPointF pdfPoint = screenToPDFCoordinates(currentPoint);
    
    // Determine which page the point is on
    int pageIndex = getPageAtPoint(pdfPoint);
    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        return;
    }
    
    // Convert to page-relative coordinates
    QPointF pagePoint = pdfToPageCoordinates(pdfPoint, pageIndex);
    
    m_textSelection->updateSelection(pageIndex, pagePoint);
    
    // Trigger repaint to show selection
    update();
}

void PDFViewerWidget::endTextSelection()
{
    if (!m_isTextSelecting) {
        return;
    }
    
    m_isTextSelecting = false;
    
    if (m_textSelection && m_textSelection->hasSelection()) {
        m_textSelection->endSelection();
        
        // Get selected text
        QString selectedText = getSelectedText();
        if (!selectedText.isEmpty()) {
            qDebug() << "Text selection completed:" << selectedText;
            emit textSelectionChanged(selectedText);
        }
    }
}

void PDFViewerWidget::clearTextSelection()
{
    if (m_textSelection) {
        m_textSelection->clearSelection();
        m_isTextSelecting = false;
        update(); // Trigger repaint to clear selection highlight
        emit textSelectionChanged(QString());
    }
}

QString PDFViewerWidget::getSelectedText() const
{
    if (!m_textSelection || !m_textSelection->hasSelection() || !m_textExtractionComplete) {
        return QString();
    }
    
    // Get selection bounds
    int startPage = m_textSelection->getStartPage();
    int endPage = m_textSelection->getEndPage();
    QPointF startPoint = m_textSelection->getStartPoint();
    QPointF endPoint = m_textSelection->getEndPoint();
    
    QString selectedText;
    
    // Handle single page selection
    if (startPage == endPage) {
        if (startPage < 0 || startPage >= static_cast<int>(m_pageTexts.size())) {
            return QString();
        }
        
        const PageTextContent& pageText = m_pageTexts[startPage];
        selectedText = extractTextFromRegion(pageText, startPoint, endPoint);
    } else {
        // Handle multi-page selection
        for (int pageIndex = startPage; pageIndex <= endPage; ++pageIndex) {
            if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageTexts.size())) {
                continue;
            }
            
            const PageTextContent& pageText = m_pageTexts[pageIndex];
            
            if (pageIndex == startPage) {
                // First page: from start point to end of page
                QPointF pageEndPoint(pageText.pageWidth, pageText.pageHeight);
                selectedText += extractTextFromRegion(pageText, startPoint, pageEndPoint);
            } else if (pageIndex == endPage) {
                // Last page: from beginning to end point
                QPointF pageStartPoint(0, 0);
                selectedText += extractTextFromRegion(pageText, pageStartPoint, endPoint);
            } else {
                // Middle pages: entire page
                selectedText += pageText.fullText;
            }
            
            if (pageIndex < endPage) {
                selectedText += "\n\n"; // Add page break
            }
        }
    }
    
    return selectedText.trimmed();
}

bool PDFViewerWidget::hasTextSelection() const
{
    return m_textSelection && m_textSelection->hasSelection();
}

QString PDFViewerWidget::extractTextFromRegion(const PageTextContent& pageText, 
                                               const QPointF& startPoint, 
                                               const QPointF& endPoint) const
{
    if (pageText.characters.empty()) {
        return QString();
    }
    
    QString result;
    
    // Find start and end character indices based on proximity to selection points
    int startCharIndex = -1;
    int endCharIndex = -1;
    double minStartDist = std::numeric_limits<double>::max();
    double minEndDist = std::numeric_limits<double>::max();
    
    for (size_t i = 0; i < pageText.characters.size(); ++i) {
        const TextChar& ch = pageText.characters[i];
        QPointF charCenter = ch.bounds.center();
        
        // Find closest character to start point
        double startDist = QLineF(startPoint, charCenter).length();
        if (startDist < minStartDist) {
            minStartDist = startDist;
            startCharIndex = static_cast<int>(i);
        }
        
        // Find closest character to end point
        double endDist = QLineF(endPoint, charCenter).length();
        if (endDist < minEndDist) {
            minEndDist = endDist;
            endCharIndex = static_cast<int>(i);
        }
    }
    
    // If we found valid start and end characters
    if (startCharIndex >= 0 && endCharIndex >= 0) {
        // Ensure start comes before end in text order
        if (startCharIndex > endCharIndex) {
            std::swap(startCharIndex, endCharIndex);
        }
        
        // Extract text from selected characters
        for (int i = startCharIndex; i <= endCharIndex; ++i) {
            result += pageText.characters[i].character;
        }
    }
    
    return result;
}

QPointF PDFViewerWidget::screenToPDF(const QPoint &screenPos) {
    if (!m_isPDFLoaded || m_pageCount == 0) {
        return QPointF();
    }
    
    // Convert screen coordinates to PDF document coordinates
    // Account for scroll position and zoom level
    float x = (screenPos.x() + m_scrollOffsetX) / m_zoomLevel;
    float y = (screenPos.y() + m_scrollOffsetY) / m_zoomLevel;
    return QPointF(x, y);
}

void PDFViewerWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF pdfCoord = screenToPDF(event->pos());
        selectWordAtPosition(pdfCoord);
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void PDFViewerWidget::selectWordAtPosition(const QPointF &position) {
    if (!m_textExtractor || m_pageTexts.empty()) {
        return;
    }

    // Convert position to PDF coordinates
    QPointF pdfCoord = screenToPDFCoordinates(position);
    int page = getPageAtPoint(pdfCoord);
    
    if (page < 0 || page >= static_cast<int>(m_pageTexts.size())) {
        return;
    }

    // Convert to page coordinates
    QPointF pageCoord = pdfToPageCoordinates(pdfCoord, page);

    // Find word at position
    const PageTextContent& pageText = m_pageTexts[page];
    for (const TextLine& line : pageText.lines) {
        for (const TextWord& word : line.words) {
            if (word.bounds.contains(pageCoord)) {
                // Select this word by setting start and end to word bounds
                m_textSelection->startSelection(page, word.bounds.topLeft());
                m_textSelection->updateSelection(page, word.bounds.bottomRight());
                m_textSelection->endSelection();
                update();
                return;
            }
        }
    }
}

void PDFViewerWidget::renderTextSelection()
{
    // Only render if we have an active text selection
    if (!m_textSelection || !m_textSelection->hasSelection()) {
        return;
    }

    // Disable texturing for selection overlay
    glDisable(GL_TEXTURE_2D);
    
    // Set selection highlight color (semi-transparent blue)
    glColor4f(0.2f, 0.4f, 0.8f, 0.3f);
    
    // Get selection bounds
    std::vector<int> selectedPages = m_textSelection->getSelectedPages();
    QPointF startPoint = m_textSelection->getStartPoint();
    QPointF endPoint = m_textSelection->getEndPoint();
    
    // Render selection highlights for each selected page
    for (int pageIndex : selectedPages) {
        if (pageIndex < 0 || pageIndex >= m_pageCount) {
            continue;
        }
        
        // Calculate page position on screen
        float pageWidth = m_pageWidths[pageIndex] * m_zoomLevel;
        float pageHeight = m_pageHeights[pageIndex] * m_zoomLevel;
        
        // Calculate page Y position (sum of heights of previous pages)
        float pageY = -m_scrollOffsetY;
        for (int i = 0; i < pageIndex; ++i) {
            pageY += (m_pageHeights[i] * m_zoomLevel) + PAGE_MARGIN;
        }
        
        float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
        
        // Skip pages not visible in viewport
        if (pageY + pageHeight < 0 || pageY > m_viewportHeight) {
            continue;
        }
        
        // For single page selection, render character-based selection
        if (m_textSelection->getStartPage() == m_textSelection->getEndPage()) {
            // Selection points are already in page coordinates
            renderTextBasedSelection(pageIndex, startPoint, endPoint, pageX, pageY, pageWidth, pageHeight);
        } else {
            // For multi-page selection, render different areas
            if (pageIndex == m_textSelection->getStartPage()) {
                // First page: from start point to end of page
                QPointF pageEndPoint(m_pageWidths[pageIndex], m_pageHeights[pageIndex]);
                renderTextBasedSelection(pageIndex, startPoint, pageEndPoint, pageX, pageY, pageWidth, pageHeight);
            } else if (pageIndex == m_textSelection->getEndPage()) {
                // Last page: from beginning to end point
                QPointF pageStartPoint(0, 0);
                renderTextBasedSelection(pageIndex, pageStartPoint, endPoint, pageX, pageY, pageWidth, pageHeight);
            } else {
                // Middle pages: entire page
                QPointF pageStartPoint(0, 0);
                QPointF pageEndPoint(m_pageWidths[pageIndex], m_pageHeights[pageIndex]);
                renderTextBasedSelection(pageIndex, pageStartPoint, pageEndPoint, pageX, pageY, pageWidth, pageHeight);
            }
        }
    }
    
    // Reset color
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void PDFViewerWidget::renderTextBasedSelection(int pageIndex, const QPointF& startPoint, const QPointF& endPoint, 
                                              float pageX, float pageY, float pageWidth, float pageHeight)
{
    // Check if we have text content for this page
    if (pageIndex >= static_cast<int>(m_pageTexts.size())) {
        return;
    }
    
    const PageTextContent& pageText = m_pageTexts[pageIndex];
    if (pageText.lines.empty()) {
        return;
    }
    
    // Create selection rectangle in page coordinates
    QRectF selectionRect(
        qMin(startPoint.x(), endPoint.x()),
        qMin(startPoint.y(), endPoint.y()),
        qAbs(endPoint.x() - startPoint.x()),
        qAbs(endPoint.y() - startPoint.y())
    );
    
    // Set a nice selection color (semi-transparent blue)
    glColor4f(0.2f, 0.5f, 0.9f, 0.4f);
    
    // For each line, if any part intersects with selection, highlight the intersected portion
    for (const TextLine& line : pageText.lines) {
        if (selectionRect.intersects(line.bounds)) {
            // Calculate the intersection of selection rect with this line
            QRectF intersection = selectionRect.intersected(line.bounds);
            
            // Convert PDF coordinates to screen coordinates
            float screenLeft = pageX + (intersection.left() / pageText.pageWidth) * pageWidth;
            float screenTop = pageY + (intersection.top() / pageText.pageHeight) * pageHeight;
            float screenRight = pageX + (intersection.right() / pageText.pageWidth) * pageWidth;
            float screenBottom = pageY + (intersection.bottom() / pageText.pageHeight) * pageHeight;
            
            // Add small padding to make selection more visible
            screenLeft -= 1.0f;
            screenTop -= 1.0f;
            screenRight += 1.0f;
            screenBottom += 1.0f;
            
            // Render clean rectangular highlight
            glBegin(GL_QUADS);
            glVertex2f(screenLeft, screenTop);
            glVertex2f(screenRight, screenTop);
            glVertex2f(screenRight, screenBottom);
            glVertex2f(screenLeft, screenBottom);
            glEnd();
            
            // Add a subtle border for better visibility
            glColor4f(0.1f, 0.3f, 0.7f, 0.6f);
            glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(screenLeft, screenTop);
            glVertex2f(screenRight, screenTop);
            glVertex2f(screenRight, screenBottom);
            glVertex2f(screenLeft, screenBottom);
            glEnd();
            
            // Reset to fill color for next rectangle
            glColor4f(0.2f, 0.5f, 0.9f, 0.4f);
        }
    }
}

std::vector<QRectF> PDFViewerWidget::mergeAdjacentRects(const std::vector<QRectF>& rects)
{
    if (rects.empty()) {
        return {};
    }
    
    std::vector<QRectF> merged = rects;
    bool changed = true;
    
    // Simple merge algorithm - could be optimized further
    while (changed) {
        changed = false;
        for (size_t i = 0; i < merged.size() && !changed; ++i) {
            for (size_t j = i + 1; j < merged.size(); ++j) {
                const QRectF& rect1 = merged[i];
                const QRectF& rect2 = merged[j];
                
                // Check if rectangles can be merged (adjacent or overlapping on same line)
                bool sameHeight = qAbs(rect1.height() - rect2.height()) < 2.0;
                bool sameBaseline = qAbs(rect1.bottom() - rect2.bottom()) < 2.0;
                bool adjacent = (rect1.right() >= rect2.left() - 5.0) && (rect2.right() >= rect1.left() - 5.0);
                
                if (sameHeight && sameBaseline && adjacent) {
                    // Merge rectangles
                    QRectF mergedRect = rect1.united(rect2);
                    merged[i] = mergedRect;
                    merged.erase(merged.begin() + j);
                    changed = true;
                    break;
                }
            }
        }
    }
    
    return merged;
}

void PDFViewerWidget::renderDebugTextHighlights()
{
    // Only render debug highlights if we have extracted text
    if (m_pageTexts.empty()) {
        return;
    }

    // Disable texturing for debug overlay
    glDisable(GL_TEXTURE_2D);
    
    // Calculate visible page range for performance
    int firstVisible = 0, lastVisible = m_pageCount - 1;
    float currentY = -m_scrollOffsetY;
    
    for (int pageIndex = firstVisible; pageIndex <= lastVisible && pageIndex < m_pageCount; ++pageIndex) {
        // Check if we have text data for this page
        if (pageIndex >= static_cast<int>(m_pageTexts.size())) {
            continue;
        }
        
        const PageTextContent& pageText = m_pageTexts[pageIndex];
        if (pageText.isEmpty()) {
            // Skip to next page Y position
            if (pageIndex < static_cast<int>(m_pageHeights.size())) {
                currentY += (m_pageHeights[pageIndex] * m_zoomLevel) + PAGE_MARGIN;
            }
            continue;
        }
        
        // Calculate page position on screen
        float pageWidth = m_pageWidths[pageIndex] * m_zoomLevel;
        float pageHeight = m_pageHeights[pageIndex] * m_zoomLevel;
        float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
        float pageY = currentY;
        
        // Skip pages not visible in viewport
        if (pageY + pageHeight < 0 || pageY > m_viewportHeight) {
            currentY += pageHeight + PAGE_MARGIN;
            continue;
        }
        
        qDebug() << "Rendering debug text highlights for page" << pageIndex 
                 << "with" << pageText.getCharacterCount() << "characters"
                 << "," << pageText.getWordCount() << "words"
                 << "," << pageText.getLineCount() << "lines"
                 << "at position" << pageX << "," << pageY
                 << "page size" << pageWidth << "x" << pageHeight
                 << "PDF size" << pageText.pageWidth << "x" << pageText.pageHeight;
        
        // Render character highlights (red rectangles)
        glColor4f(1.0f, 0.0f, 0.0f, 0.5f); // Semi-transparent red
        renderTextElements(pageText.characters, pageIndex, pageX, pageY, pageWidth, pageHeight, pageText.pageWidth, pageText.pageHeight);
        
        // Render word highlights (green rectangles)
        glColor4f(0.0f, 1.0f, 0.0f, 0.4f); // Semi-transparent green
        renderTextElements(pageText.words, pageIndex, pageX, pageY, pageWidth, pageHeight, pageText.pageWidth, pageText.pageHeight);
        
        // Render line highlights (blue rectangles)
        glColor4f(0.0f, 0.0f, 1.0f, 0.3f); // Semi-transparent blue
        renderTextElements(pageText.lines, pageIndex, pageX, pageY, pageWidth, pageHeight, pageText.pageWidth, pageText.pageHeight);
        
        // Test: Render a fixed rectangle to verify rendering is working
        glColor4f(1.0f, 1.0f, 0.0f, 0.8f); // Bright yellow
        glBegin(GL_QUADS);
        glVertex2f(pageX + 50, pageY + 50);
        glVertex2f(pageX + 150, pageY + 50);
        glVertex2f(pageX + 150, pageY + 100);
        glVertex2f(pageX + 50, pageY + 100);
        glEnd();
        
        currentY += pageHeight + PAGE_MARGIN;
    }
    
    // Reset color
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

template<typename T>
void PDFViewerWidget::renderTextElements(const std::vector<T>& elements, int pageIndex, 
                                        float pageX, float pageY, float pageWidth, float pageHeight,
                                        float pdfPageWidth, float pdfPageHeight)
{
    qDebug() << "Rendering" << elements.size() << "text elements for page" << pageIndex;
    
    for (const auto& element : elements) {
        const QRectF& bounds = element.bounds;
        
        // Debug: Print element bounds
        qDebug() << "Element bounds:" << bounds << "PDF page size:" << pdfPageWidth << "x" << pdfPageHeight;
        
        // PDFium uses bottom-left origin, Qt/OpenGL uses top-left origin
        // Convert PDF coordinates to screen coordinates with Y-axis flip
        float normalizedLeft = bounds.left() / pdfPageWidth;
        float normalizedTop = bounds.top() / pdfPageHeight;
        float normalizedRight = bounds.right() / pdfPageWidth;
        float normalizedBottom = bounds.bottom() / pdfPageHeight;
        
        // Apply Y-axis flip for PDFium's bottom-left origin
        float screenLeft = pageX + normalizedLeft * pageWidth;
        float screenRight = pageX + normalizedRight * pageWidth;
        float screenTop = pageY + (1.0f - normalizedBottom) * pageHeight;    // Flip Y
        float screenBottom = pageY + (1.0f - normalizedTop) * pageHeight;    // Flip Y
        
        // Ensure proper ordering after Y-flip
        if (screenTop > screenBottom) {
            std::swap(screenTop, screenBottom);
        }
        
        // Ensure the rectangle has some minimum size for visibility
        if (screenRight - screenLeft < 3.0f) {
            screenRight = screenLeft + 3.0f;
        }
        if (screenBottom - screenTop < 3.0f) {
            screenBottom = screenTop + 3.0f;
        }
        
        // Debug: Print screen coordinates
        qDebug() << "Screen coords:" << screenLeft << "," << screenTop << "to" << screenRight << "," << screenBottom;
        
        // Render highlight rectangle
        glBegin(GL_QUADS);
        glVertex2f(screenLeft, screenTop);
        glVertex2f(screenRight, screenTop);
        glVertex2f(screenRight, screenBottom);
        glVertex2f(screenLeft, screenBottom);
        glEnd();
    }
}