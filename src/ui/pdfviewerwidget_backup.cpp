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
#include "ui/tab-manager.h"

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

void PDFViewerWidget::setupUI()
{
    // Create main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Setup toolbar
    setupToolbar();
    
    // Setup search bar (initially hidden)
    setupSearchBar();
    
    // Add stretch to push OpenGL widget to fill remaining space
    mainLayout->addStretch();
    
    setLayout(mainLayout);
}

void PDFViewerWidget::setupToolbar()
{
    m_toolbarWidget = new QWidget(this);
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
    QPushButton *firstPageBtn = new QPushButton("⏮", m_toolbarWidget);
    firstPageBtn->setToolTip("First Page");
    connect(firstPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToFirstPage);
    
    QPushButton *prevPageBtn = new QPushButton("◀", m_toolbarWidget);
    prevPageBtn->setToolTip("Previous Page");
    connect(prevPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::previousPage);
    
    m_pageInput = new QLineEdit(m_toolbarWidget);
    m_pageInput->setFixedWidth(50);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setToolTip("Current Page");
    connect(m_pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    
    m_pageCountLabel = new QLabel("/ 0", m_toolbarWidget);
    
    QPushButton *nextPageBtn = new QPushButton("▶", m_toolbarWidget);
    nextPageBtn->setToolTip("Next Page");
    connect(nextPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::nextPage);
    
    QPushButton *lastPageBtn = new QPushButton("⏭", m_toolbarWidget);
    lastPageBtn->setToolTip("Last Page");
    connect(lastPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToLastPage);
    
    // Separator
    QFrame *separator1 = new QFrame(m_toolbarWidget);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setFrameShadow(QFrame::Sunken);
    
    // Zoom controls
    QPushButton *zoomOutBtn = new QPushButton("-", m_toolbarWidget);
    zoomOutBtn->setToolTip("Zoom Out");
    connect(zoomOutBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomOut);
    
    m_zoomSlider = new QSlider(Qt::Horizontal, m_toolbarWidget);
    m_zoomSlider->setMinimum(static_cast<int>(MIN_ZOOM * 100));
    m_zoomSlider->setMaximum(static_cast<int>(MAX_ZOOM * 100));
    m_zoomSlider->setValue(static_cast<int>(DEFAULT_ZOOM * 100));
    m_zoomSlider->setFixedWidth(100);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &PDFViewerWidget::onZoomSliderChanged);
    
    QPushButton *zoomInBtn = new QPushButton("+", m_toolbarWidget);
    zoomInBtn->setToolTip("Zoom In");
    connect(zoomInBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomIn);
    
    m_zoomLabel = new QLabel("100%", m_toolbarWidget);
    m_zoomLabel->setFixedWidth(40);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    
    QPushButton *fitToWidthBtn = new QPushButton("Fit Width", m_toolbarWidget);
    connect(fitToWidthBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToWidth);
    
    QPushButton *fitToPageBtn = new QPushButton("Fit Page", m_toolbarWidget);
    connect(fitToPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToFit);
    
    // Separator
    QFrame *separator2 = new QFrame(m_toolbarWidget);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setFrameShadow(QFrame::Sunken);
    
    // Search button
    QPushButton *searchBtn = new QPushButton("🔍 Search", m_toolbarWidget);
    searchBtn->setToolTip("Search in Document");
    connect(searchBtn, &QPushButton::clicked, this, &PDFViewerWidget::startSearch);
    
    // Add widgets to toolbar layout
    toolbarLayout->addWidget(firstPageBtn);
    toolbarLayout->addWidget(prevPageBtn);
    toolbarLayout->addWidget(m_pageInput);
    toolbarLayout->addWidget(m_pageCountLabel);
    toolbarLayout->addWidget(nextPageBtn);
    toolbarLayout->addWidget(lastPageBtn);
    toolbarLayout->addWidget(separator1);
    toolbarLayout->addWidget(zoomOutBtn);
    toolbarLayout->addWidget(m_zoomSlider);
    toolbarLayout->addWidget(zoomInBtn);
    toolbarLayout->addWidget(m_zoomLabel);
    toolbarLayout->addWidget(fitToWidthBtn);
    toolbarLayout->addWidget(fitToPageBtn);
    toolbarLayout->addWidget(separator2);
    toolbarLayout->addWidget(searchBtn);
    toolbarLayout->addStretch();
    
    // Add toolbar to main layout
    layout()->addWidget(m_toolbarWidget);
}

void PDFViewerWidget::setupSearchBar()
{
    m_searchWidget = new QWidget(this);
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
    
    QLabel *searchLabel = new QLabel("Search:", m_searchWidget);
    
    m_searchInput = new QLineEdit(m_searchWidget);
    m_searchInput->setPlaceholderText("Enter search term...");
    connect(m_searchInput, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchTextChanged);
    connect(m_searchInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onSearchNext);
    
    m_searchPrevButton = new QPushButton("◀", m_searchWidget);
    m_searchPrevButton->setToolTip("Previous Result");
    connect(m_searchPrevButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchPrevious);
    
    m_searchNextButton = new QPushButton("▶", m_searchWidget);
    m_searchNextButton->setToolTip("Next Result");
    connect(m_searchNextButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchNext);
    
    m_searchResultsLabel = new QLabel("", m_searchWidget);
    m_searchResultsLabel->setMinimumWidth(80);
    
    m_caseSensitiveCheck = new QCheckBox("Case", m_searchWidget);
    m_caseSensitiveCheck->setToolTip("Case Sensitive");
    connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleCaseSensitive);
    
    m_wholeWordsCheck = new QCheckBox("Whole", m_searchWidget);
    m_wholeWordsCheck->setToolTip("Whole Words");
    connect(m_wholeWordsCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleWholeWords);
    
    m_closeSearchButton = new QPushButton("✕", m_searchWidget);
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
    
    // Add search bar to main layout (after toolbar)
    layout()->addWidget(m_searchWidget);
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
        
        // Update UI
        m_pageInput->setText("1");
        m_pageCountLabel->setText(QString("/ %1").arg(m_pageCount));
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
        
        // Initialize scroll state and text search if needed
        if (!m_scrollState) {
            m_scrollState = std::make_unique<PDFScrollState>();
        }
        
        if (!m_textSearch) {
            m_textSearch = std::make_unique<TextSearch>();
        }
        
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

void PDFViewerWidget::closePDF()
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Clean up OpenGL textures
    makeCurrent();
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    doneCurrent();
    
    // Reset state
    m_isPDFLoaded = false;
    m_filePath.clear();
    m_currentPage = 0;
    m_pageCount = 0;
    m_pageWidths.clear();
    m_pageHeights.clear();
    m_scrollOffsetX = 0.0f;
    m_scrollOffsetY = 0.0f;
    
    // Update UI
    m_pageInput->setText("0");
    m_pageCountLabel->setText("/ 0");
    
    // Hide search if visible
    if (m_searchWidget->isVisible()) {
        clearSearch();
    }
    
    update();
    emit pdfClosed();
}

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
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Create shader program for rendering PDF pages
    m_shaderProgram = new QOpenGLShaderProgram(this);
    
    // Vertex shader
    const char *vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;
        
        out vec2 TexCoord;
        
        uniform mat4 projection;
        uniform mat4 model;
        
        void main()
        {
            gl_Position = projection * model * vec4(aPos, 1.0);
            TexCoord = aTexCoord;
        }
    )";
    
    // Fragment shader
    const char *fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        
        in vec2 TexCoord;
        
        uniform sampler2D pageTexture;
        
        void main()
        {
            FragColor = texture(pageTexture, TexCoord);
        }
    )";
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qDebug() << "Failed to compile vertex shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        qDebug() << "Failed to compile fragment shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->link()) {
        qDebug() << "Failed to link shader program:" << m_shaderProgram->log();
    }
    
    // Create vertex array object and buffers
    createQuadGeometry();
}

void PDFViewerWidget::resizeGL(int w, int h)
{
    m_viewportWidth = w;
    m_viewportHeight = h - TOOLBAR_HEIGHT - (m_searchWidget->isVisible() ? SEARCH_BAR_HEIGHT : 0);
    
    glViewport(0, 0, w, h);
    updateViewport();
    
    if (m_isPDFLoaded) {
        calculatePageLayout();
        update();
    }
}

void PDFViewerWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (!m_isPDFLoaded || !m_shaderProgram) {
        return;
    }
    
    renderPDF();
}

void PDFViewerWidget::renderPDF()
{
    if (!m_shaderProgram || m_pageTextures.empty()) {
        return;
    }
    
    m_shaderProgram->bind();
    m_vao.bind();
    
    // Set up projection matrix (orthographic)
    QMatrix4x4 projection;
    projection.ortho(0.0f, static_cast<float>(m_viewportWidth), 
                     static_cast<float>(m_viewportHeight), 0.0f, -1.0f, 1.0f);
    m_shaderProgram->setUniformValue("projection", projection);
    
    // Render visible pages
    float currentY = -m_scrollOffsetY;
    
    for (int i = 0; i < m_pageCount && i < static_cast<int>(m_pageTextures.size()); ++i) {
        if (i >= static_cast<int>(m_pageWidths.size()) || i >= static_cast<int>(m_pageHeights.size())) {
            continue;
        }
        
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        
        // Skip pages that are not visible
        if (currentY + pageHeight < 0) {
            currentY += pageHeight + PAGE_MARGIN;
            continue;
        }
        
        if (currentY > m_viewportHeight) {
            break;
        }
        
        // Calculate page position
        float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
        float pageY = currentY;
        
        // Set up model matrix
        QMatrix4x4 model;
        model.translate(pageX, pageY, 0.0f);
        model.scale(pageWidth, pageHeight, 1.0f);
        m_shaderProgram->setUniformValue("model", model);
        
        // Bind page texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_pageTextures[i]);
        m_shaderProgram->setUniformValue("pageTexture", 0);
        
        // Draw quad
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        
        currentY += pageHeight + PAGE_MARGIN;
    }
    
    m_vao.release();
    m_shaderProgram->release();
}

void PDFViewerWidget::createQuadGeometry()
{
    // Quad vertices (position + texture coordinates)
    float vertices[] = {
        // positions   // texture coords
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f,   // top left
        1.0f, 1.0f, 0.0f,  1.0f, 1.0f,   // top right
        1.0f, 0.0f, 0.0f,  1.0f, 0.0f,   // bottom right
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f    // bottom left
    };
    
    unsigned int indices[] = {
        0, 1, 2,   // first triangle
        2, 3, 0    // second triangle
    };
    
    m_vao.create();
    m_vao.bind();
    
    m_vertexBuffer.create();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices, sizeof(vertices));
    
    // Create element buffer
    QOpenGLBuffer elementBuffer(QOpenGLBuffer::IndexBuffer);
    elementBuffer.create();
    elementBuffer.bind();
    elementBuffer.allocate(indices, sizeof(indices));
    
    // Set vertex attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    m_vao.release();
}

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
    
    // Make sure OpenGL functions are initialized (no need to check, they are initialized in initializeGL)
    
    // Clean up existing textures
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    
    m_pageWidths.clear();
    m_pageHeights.clear();
    
    // Create textures for all pages
    m_pageTextures.resize(m_pageCount);
    m_pageWidths.resize(m_pageCount);
    m_pageHeights.resize(m_pageCount);
    
    glGenTextures(m_pageCount, m_pageTextures.data());
    
    // Get device pixel ratio for high-DPI displays
    qreal devicePixelRatio = this->devicePixelRatio();
    
    for (int i = 0; i < m_pageCount; ++i) {
        int width, height;
        
        // Calculate high-resolution rendering dimensions
        // Use device pixel ratio and zoom level to determine quality
        double renderScale = std::max(1.0, devicePixelRatio * m_zoomLevel);
        bool useHighRes = renderScale > 1.0;
        
        FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(i, width, height, useHighRes);
        
        if (bitmap) {
            m_pageWidths[i] = width;
            m_pageHeights[i] = height;
            
            glBindTexture(GL_TEXTURE_2D, m_pageTextures[i]);
            
            // Use better texture filtering for high-quality rendering
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            void* buffer = FPDFBitmap_GetBuffer(bitmap);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
            
            // Generate mipmaps for better quality at different zoom levels
            glGenerateMipmap(GL_TEXTURE_2D);
            
            FPDFBitmap_Destroy(bitmap);
        } else {
            m_pageWidths[i] = 0;
            m_pageHeights[i] = 0;
        }
    }
    
    calculatePageLayout();
}

void PDFViewerWidget::calculatePageLayout()
{
    if (!m_isPDFLoaded || m_pageHeights.empty()) {
        return;
    }
    
    // Calculate total document height and max scroll
    float totalHeight = 0.0f;
    float maxWidth = 0.0f;
    
    for (int i = 0; i < m_pageCount; ++i) {
        if (i < static_cast<int>(m_pageHeights.size()) && i < static_cast<int>(m_pageWidths.size())) {
            totalHeight += m_pageHeights[i] * static_cast<float>(m_zoomLevel) + PAGE_MARGIN;
            maxWidth = std::max(maxWidth, m_pageWidths[i] * static_cast<float>(m_zoomLevel));
        }
    }
    
    m_maxScrollY = std::max(0.0f, totalHeight - m_viewportHeight);
    m_maxScrollX = std::max(0.0f, maxWidth - m_viewportWidth);
    
    // Clamp current scroll position
    m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0.0f, m_maxScrollY);
    m_scrollOffsetX = std::clamp(m_scrollOffsetX, 0.0f, m_maxScrollX);
}

// Navigation methods
void PDFViewerWidget::goToPage(int pageNumber)
{
    if (!m_isPDFLoaded || pageNumber < 1 || pageNumber > m_pageCount) {
        return;
    }
    
    int targetPage = pageNumber - 1; // Convert to 0-based index
    if (targetPage == m_currentPage) {
        return;
    }
    
    m_currentPage = targetPage;
    
    // Update UI
    m_pageInput->setText(QString::number(pageNumber));
    
    // Calculate scroll position to show the target page
    float targetY = 0.0f;
    for (int i = 0; i < m_currentPage; ++i) {
        if (i < static_cast<int>(m_pageHeights.size())) {
            targetY += m_pageHeights[i] * static_cast<float>(m_zoomLevel) + PAGE_MARGIN;
        }
    }
    
    m_scrollOffsetY = std::clamp(targetY, 0.0f, m_maxScrollY);
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
    setZoomLevel(m_zoomLevel + ZOOM_STEP);
}

void PDFViewerWidget::zoomOut()
{
    setZoomLevel(m_zoomLevel - ZOOM_STEP);
}

void PDFViewerWidget::zoomToFit()
{
    if (!m_isPDFLoaded || m_pageWidths.empty() || m_pageHeights.empty()) {
        return;
    }
    
    double fitZoom = calculateZoomToFit();
    setZoomLevel(fitZoom);
}

void PDFViewerWidget::zoomToWidth()
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return;
    }
    
    double widthZoom = calculateZoomToWidth();
    setZoomLevel(widthZoom);
}

void PDFViewerWidget::resetZoom()
{
    setZoomLevel(DEFAULT_ZOOM);
}

void PDFViewerWidget::setupUI()
{
    // Create main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Setup toolbar
    setupToolbar();
    
    // Setup search bar (initially hidden)
    setupSearchBar();
    
    // Add stretch to push OpenGL widget to fill remaining space
    mainLayout->addStretch();
    
    setLayout(mainLayout);
}

void PDFViewerWidget::setupToolbar()
{
    m_toolbarWidget = new QWidget(this);
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
    QPushButton *firstPageBtn = new QPushButton("⏮", m_toolbarWidget);
    firstPageBtn->setToolTip("First Page");
    connect(firstPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToFirstPage);
    
    QPushButton *prevPageBtn = new QPushButton("◀", m_toolbarWidget);
    prevPageBtn->setToolTip("Previous Page");
    connect(prevPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::previousPage);
    
    m_pageInput = new QLineEdit(m_toolbarWidget);
    m_pageInput->setFixedWidth(50);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setToolTip("Current Page");
    connect(m_pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    
    m_pageCountLabel = new QLabel("/ 0", m_toolbarWidget);
    
    QPushButton *nextPageBtn = new QPushButton("▶", m_toolbarWidget);
    nextPageBtn->setToolTip("Next Page");
    connect(nextPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::nextPage);
    
    QPushButton *lastPageBtn = new QPushButton("⏭", m_toolbarWidget);
    lastPageBtn->setToolTip("Last Page");
    connect(lastPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToLastPage);
    
    // Separator
    QFrame *separator1 = new QFrame(m_toolbarWidget);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setFrameShadow(QFrame::Sunken);
    
    // Zoom controls
    QPushButton *zoomOutBtn = new QPushButton("-", m_toolbarWidget);
    zoomOutBtn->setToolTip("Zoom Out");
    connect(zoomOutBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomOut);
    
    m_zoomSlider = new QSlider(Qt::Horizontal, m_toolbarWidget);
    m_zoomSlider->setMinimum(static_cast<int>(MIN_ZOOM * 100));
    m_zoomSlider->setMaximum(static_cast<int>(MAX_ZOOM * 100));
    m_zoomSlider->setValue(static_cast<int>(DEFAULT_ZOOM * 100));
    m_zoomSlider->setFixedWidth(100);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &PDFViewerWidget::onZoomSliderChanged);
    
    QPushButton *zoomInBtn = new QPushButton("+", m_toolbarWidget);
    zoomInBtn->setToolTip("Zoom In");
    connect(zoomInBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomIn);
    
    m_zoomLabel = new QLabel("100%", m_toolbarWidget);
    m_zoomLabel->setFixedWidth(40);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    
    QPushButton *fitToWidthBtn = new QPushButton("Fit Width", m_toolbarWidget);
    connect(fitToWidthBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToWidth);
    
    QPushButton *fitToPageBtn = new QPushButton("Fit Page", m_toolbarWidget);
    connect(fitToPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToFit);
    
    // Separator
    QFrame *separator2 = new QFrame(m_toolbarWidget);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setFrameShadow(QFrame::Sunken);
    
    // Search button
    QPushButton *searchBtn = new QPushButton("🔍 Search", m_toolbarWidget);
    searchBtn->setToolTip("Search in Document");
    connect(searchBtn, &QPushButton::clicked, this, &PDFViewerWidget::startSearch);
    
    // Add widgets to toolbar layout
    toolbarLayout->addWidget(firstPageBtn);
    toolbarLayout->addWidget(prevPageBtn);
    toolbarLayout->addWidget(m_pageInput);
    toolbarLayout->addWidget(m_pageCountLabel);
    toolbarLayout->addWidget(nextPageBtn);
    toolbarLayout->addWidget(lastPageBtn);
    toolbarLayout->addWidget(separator1);
    toolbarLayout->addWidget(zoomOutBtn);
    toolbarLayout->addWidget(m_zoomSlider);
    toolbarLayout->addWidget(zoomInBtn);
    toolbarLayout->addWidget(m_zoomLabel);
    toolbarLayout->addWidget(fitToWidthBtn);
    toolbarLayout->addWidget(fitToPageBtn);
    toolbarLayout->addWidget(separator2);
    toolbarLayout->addWidget(searchBtn);
    toolbarLayout->addStretch();
    
    // Add toolbar to main layout
    layout()->addWidget(m_toolbarWidget);
}

void PDFViewerWidget::setupSearchBar()
{
    m_searchWidget = new QWidget(this);
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
    
    QLabel *searchLabel = new QLabel("Search:", m_searchWidget);
    
    m_searchInput = new QLineEdit(m_searchWidget);
    m_searchInput->setPlaceholderText("Enter search term...");
    connect(m_searchInput, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchTextChanged);
    connect(m_searchInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onSearchNext);
    
    m_searchPrevButton = new QPushButton("◀", m_searchWidget);
    m_searchPrevButton->setToolTip("Previous Result");
    connect(m_searchPrevButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchPrevious);
    
    m_searchNextButton = new QPushButton("▶", m_searchWidget);
    m_searchNextButton->setToolTip("Next Result");
    connect(m_searchNextButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchNext);
    
    m_searchResultsLabel = new QLabel("", m_searchWidget);
    m_searchResultsLabel->setMinimumWidth(80);
    
    m_caseSensitiveCheck = new QCheckBox("Case", m_searchWidget);
    m_caseSensitiveCheck->setToolTip("Case Sensitive");
    connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleCaseSensitive);
    
    m_wholeWordsCheck = new QCheckBox("Whole", m_searchWidget);
    m_wholeWordsCheck->setToolTip("Whole Words");
    connect(m_wholeWordsCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleWholeWords);
    
    m_closeSearchButton = new QPushButton("✕", m_searchWidget);
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
    
    // Add search bar to main layout (after toolbar)
    layout()->addWidget(m_searchWidget);
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
        
        // Update UI
        m_pageInput->setText("1");
        m_pageCountLabel->setText(QString("/ %1").arg(m_pageCount));
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
        
        // Initialize scroll state and text search if needed
        if (!m_scrollState) {
            m_scrollState = std::make_unique<PDFScrollState>();
        }
        
        if (!m_textSearch) {
            m_textSearch = std::make_unique<TextSearch>();
        }
        
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

void PDFViewerWidget::closePDF()
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Clean up OpenGL textures
    makeCurrent();
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    doneCurrent();
    
    // Reset state
    m_isPDFLoaded = false;
    m_filePath.clear();
    m_currentPage = 0;
    m_pageCount = 0;
    m_pageWidths.clear();
    m_pageHeights.clear();
    m_scrollOffsetX = 0.0f;
    m_scrollOffsetY = 0.0f;
    
    // Update UI
    m_pageInput->setText("0");
    m_pageCountLabel->setText("/ 0");
    
    // Hide search if visible
    if (m_searchWidget->isVisible()) {
        clearSearch();
    }
    
    update();
    emit pdfClosed();
}

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
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Create shader program for rendering PDF pages
    m_shaderProgram = new QOpenGLShaderProgram(this);
    
    // Vertex shader
    const char *vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;
        
        out vec2 TexCoord;
        
        uniform mat4 projection;
        uniform mat4 model;
        
        void main()
        {
            gl_Position = projection * model * vec4(aPos, 1.0);
            TexCoord = aTexCoord;
        }
    )";
    
    // Fragment shader
    const char *fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        
        in vec2 TexCoord;
        
        uniform sampler2D pageTexture;
        
        void main()
        {
            FragColor = texture(pageTexture, TexCoord);
        }
    )";
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qDebug() << "Failed to compile vertex shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        qDebug() << "Failed to compile fragment shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->link()) {
        qDebug() << "Failed to link shader program:" << m_shaderProgram->log();
    }
    
    // Create vertex array object and buffers
    createQuadGeometry();
}

void PDFViewerWidget::resizeGL(int w, int h)
{
    m_viewportWidth = w;
    m_viewportHeight = h - TOOLBAR_HEIGHT - (m_searchWidget->isVisible() ? SEARCH_BAR_HEIGHT : 0);
    
    glViewport(0, 0, w, h);
    updateViewport();
    
    if (m_isPDFLoaded) {
        calculatePageLayout();
        update();
    }
}

void PDFViewerWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (!m_isPDFLoaded || !m_shaderProgram) {
        return;
    }
    
    renderPDF();
}

void PDFViewerWidget::renderPDF()
{
    if (!m_shaderProgram || m_pageTextures.empty()) {
        return;
    }
    
    m_shaderProgram->bind();
    m_vao.bind();
    
    // Set up projection matrix (orthographic)
    QMatrix4x4 projection;
    projection.ortho(0.0f, static_cast<float>(m_viewportWidth), 
                     static_cast<float>(m_viewportHeight), 0.0f, -1.0f, 1.0f);
    m_shaderProgram->setUniformValue("projection", projection);
    
    // Render visible pages
    float currentY = -m_scrollOffsetY;
    
    for (int i = 0; i < m_pageCount && i < static_cast<int>(m_pageTextures.size()); ++i) {
        if (i >= static_cast<int>(m_pageWidths.size()) || i >= static_cast<int>(m_pageHeights.size())) {
            continue;
        }
        
        float pageWidth = m_pageWidths[i] * m_zoomLevel;
        float pageHeight = m_pageHeights[i] * m_zoomLevel;
        
        // Skip pages that are not visible
        if (currentY + pageHeight < 0) {
            currentY += pageHeight + PAGE_MARGIN;
            continue;
        }
        
        if (currentY > m_viewportHeight) {
            break;
        }
        
        // Calculate page position
        float pageX = (m_viewportWidth - pageWidth) / 2.0f - m_scrollOffsetX;
        float pageY = currentY;
        
        // Set up model matrix
        QMatrix4x4 model;
        model.translate(pageX, pageY, 0.0f);
        model.scale(pageWidth, pageHeight, 1.0f);
        m_shaderProgram->setUniformValue("model", model);
        
        // Bind page texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_pageTextures[i]);
        m_shaderProgram->setUniformValue("pageTexture", 0);
        
        // Draw quad
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        
        currentY += pageHeight + PAGE_MARGIN;
    }
    
    m_vao.release();
    m_shaderProgram->release();
}

void PDFViewerWidget::createQuadGeometry()
{
    // Quad vertices (position + texture coordinates)
    float vertices[] = {
        // positions   // texture coords
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f,   // top left
        1.0f, 1.0f, 0.0f,  1.0f, 1.0f,   // top right
        1.0f, 0.0f, 0.0f,  1.0f, 0.0f,   // bottom right
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f    // bottom left
    };
    
    unsigned int indices[] = {
        0, 1, 2,   // first triangle
        2, 3, 0    // second triangle
    };
    
    m_vao.create();
    m_vao.bind();
    
    m_vertexBuffer.create();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices, sizeof(vertices));
    
    // Create element buffer
    QOpenGLBuffer elementBuffer(QOpenGLBuffer::IndexBuffer);
    elementBuffer.create();
    elementBuffer.bind();
    elementBuffer.allocate(indices, sizeof(indices));
    
    // Set vertex attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    m_vao.release();
}

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
    
    // Make sure OpenGL functions are initialized (no need to check, they are initialized in initializeGL)
    
    // Clean up existing textures
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    
    m_pageWidths.clear();
    m_pageHeights.clear();
    
    // Create textures for all pages
    m_pageTextures.resize(m_pageCount);
    m_pageWidths.resize(m_pageCount);
    m_pageHeights.resize(m_pageCount);
    
    glGenTextures(m_pageCount, m_pageTextures.data());
    
    // Get device pixel ratio for high-DPI displays
    qreal devicePixelRatio = this->devicePixelRatio();
    
    for (int i = 0; i < m_pageCount; ++i) {
        int width, height;
        
        // Calculate high-resolution rendering dimensions
        // Use device pixel ratio and zoom level to determine quality
        double renderScale = std::max(1.0, devicePixelRatio * m_zoomLevel);
        bool useHighRes = renderScale > 1.0;
        
        FPDF_BITMAP bitmap = m_renderer->RenderPageToBitmap(i, width, height, useHighRes);
        
        if (bitmap) {
            m_pageWidths[i] = width;
            m_pageHeights[i] = height;
            
            glBindTexture(GL_TEXTURE_2D, m_pageTextures[i]);
            
            // Use better texture filtering for high-quality rendering
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            void* buffer = FPDFBitmap_GetBuffer(bitmap);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
            
            // Generate mipmaps for better quality at different zoom levels
            glGenerateMipmap(GL_TEXTURE_2D);
            
            FPDFBitmap_Destroy(bitmap);
        } else {
            m_pageWidths[i] = 0;
            m_pageHeights[i] = 0;
        }
    }
    
    calculatePageLayout();
}

void PDFViewerWidget::calculatePageLayout()
{
    if (!m_isPDFLoaded || m_pageHeights.empty()) {
        return;
    }
    
    // Calculate total document height and max scroll
    float totalHeight = 0.0f;
    float maxWidth = 0.0f;
    
    for (int i = 0; i < m_pageCount; ++i) {
        if (i < static_cast<int>(m_pageHeights.size()) && i < static_cast<int>(m_pageWidths.size())) {
            totalHeight += m_pageHeights[i] * static_cast<float>(m_zoomLevel) + PAGE_MARGIN;
            maxWidth = std::max(maxWidth, m_pageWidths[i] * static_cast<float>(m_zoomLevel));
        }
    }
    
    m_maxScrollY = std::max(0.0f, totalHeight - m_viewportHeight);
    m_maxScrollX = std::max(0.0f, maxWidth - m_viewportWidth);
    
    // Clamp current scroll position
    m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0.0f, m_maxScrollY);
    m_scrollOffsetX = std::clamp(m_scrollOffsetX, 0.0f, m_maxScrollX);
}

// Navigation methods
void PDFViewerWidget::goToPage(int pageNumber)
{
    if (!m_isPDFLoaded || pageNumber < 1 || pageNumber > m_pageCount) {
        return;
    }
    
    int targetPage = pageNumber - 1; // Convert to 0-based index
    if (targetPage == m_currentPage) {
        return;
    }
    
    m_currentPage = targetPage;
    
    // Update UI
    m_pageInput->setText(QString::number(pageNumber));
    
    // Calculate scroll position to show the target page
    float targetY = 0.0f;
    for (int i = 0; i < m_currentPage; ++i) {
        if (i < static_cast<int>(m_pageHeights.size())) {
            targetY += m_pageHeights[i] * static_cast<float>(m_zoomLevel) + PAGE_MARGIN;
        }
    }
    
    m_scrollOffsetY = std::clamp(targetY, 0.0f, m_maxScrollY);
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
    setZoomLevel(m_zoomLevel + ZOOM_STEP);
}

void PDFViewerWidget::zoomOut()
{
    setZoomLevel(m_zoomLevel - ZOOM_STEP);
}

void PDFViewerWidget::zoomToFit()
{
    if (!m_isPDFLoaded || m_pageWidths.empty() || m_pageHeights.empty()) {
        return;
    }
    
    double fitZoom = calculateZoomToFit();
    setZoomLevel(fitZoom);
}

void PDFViewerWidget::zoomToWidth()
{
    if (!m_isPDFLoaded || m_pageWidths.empty()) {
        return;
    }
    
    double widthZoom = calculateZoomToWidth();
    setZoomLevel(widthZoom);
}

void PDFViewerWidget::resetZoom()
{
    setZoomLevel(DEFAULT_ZOOM);
}

void PDFViewerWidget::setupUI()
{
    // Create main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Setup toolbar
    setupToolbar();
    
    // Setup search bar (initially hidden)
    setupSearchBar();
    
    // Add stretch to push OpenGL widget to fill remaining space
    mainLayout->addStretch();
    
    setLayout(mainLayout);
}

void PDFViewerWidget::setupToolbar()
{
    m_toolbarWidget = new QWidget(this);
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
    QPushButton *firstPageBtn = new QPushButton("⏮", m_toolbarWidget);
    firstPageBtn->setToolTip("First Page");
    connect(firstPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToFirstPage);
    
    QPushButton *prevPageBtn = new QPushButton("◀", m_toolbarWidget);
    prevPageBtn->setToolTip("Previous Page");
    connect(prevPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::previousPage);
    
    m_pageInput = new QLineEdit(m_toolbarWidget);
    m_pageInput->setFixedWidth(50);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setToolTip("Current Page");
    connect(m_pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    
    m_pageCountLabel = new QLabel("/ 0", m_toolbarWidget);
    
    QPushButton *nextPageBtn = new QPushButton("▶", m_toolbarWidget);
    nextPageBtn->setToolTip("Next Page");
    connect(nextPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::nextPage);
    
    QPushButton *lastPageBtn = new QPushButton("⏭", m_toolbarWidget);
    lastPageBtn->setToolTip("Last Page");
    connect(lastPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::goToLastPage);
    
    // Separator
    QFrame *separator1 = new QFrame(m_toolbarWidget);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setFrameShadow(QFrame::Sunken);
    
    // Zoom controls
    QPushButton *zoomOutBtn = new QPushButton("-", m_toolbarWidget);
    zoomOutBtn->setToolTip("Zoom Out");
    connect(zoomOutBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomOut);
    
    m_zoomSlider = new QSlider(Qt::Horizontal, m_toolbarWidget);
    m_zoomSlider->setMinimum(static_cast<int>(MIN_ZOOM * 100));
    m_zoomSlider->setMaximum(static_cast<int>(MAX_ZOOM * 100));
    m_zoomSlider->setValue(static_cast<int>(DEFAULT_ZOOM * 100));
    m_zoomSlider->setFixedWidth(100);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &PDFViewerWidget::onZoomSliderChanged);
    
    QPushButton *zoomInBtn = new QPushButton("+", m_toolbarWidget);
    zoomInBtn->setToolTip("Zoom In");
    connect(zoomInBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomIn);
    
    m_zoomLabel = new QLabel("100%", m_toolbarWidget);
    m_zoomLabel->setFixedWidth(40);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    
    QPushButton *fitToWidthBtn = new QPushButton("Fit Width", m_toolbarWidget);
    connect(fitToWidthBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToWidth);
    
    QPushButton *fitToPageBtn = new QPushButton("Fit Page", m_toolbarWidget);
    connect(fitToPageBtn, &QPushButton::clicked, this, &PDFViewerWidget::zoomToFit);
    
    // Separator
    QFrame *separator2 = new QFrame(m_toolbarWidget);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setFrameShadow(QFrame::Sunken);
    
    // Search button
    QPushButton *searchBtn = new QPushButton("🔍 Search", m_toolbarWidget);
    searchBtn->setToolTip("Search in Document");
    connect(searchBtn, &QPushButton::clicked, this, &PDFViewerWidget::startSearch);
    
    // Add widgets to toolbar layout
    toolbarLayout->addWidget(firstPageBtn);
    toolbarLayout->addWidget(prevPageBtn);
    toolbarLayout->addWidget(m_pageInput);
    toolbarLayout->addWidget(m_pageCountLabel);
    toolbarLayout->addWidget(nextPageBtn);
    toolbarLayout->addWidget(lastPageBtn);
    toolbarLayout->addWidget(separator1);
    toolbarLayout->addWidget(zoomOutBtn);
    toolbarLayout->addWidget(m_zoomSlider);
    toolbarLayout->addWidget(zoomInBtn);
    toolbarLayout->addWidget(m_zoomLabel);
    toolbarLayout->addWidget(fitToWidthBtn);
    toolbarLayout->addWidget(fitToPageBtn);
    toolbarLayout->addWidget(separator2);
    toolbarLayout->addWidget(searchBtn);
    toolbarLayout->addStretch();
    
    // Add toolbar to main layout
    layout()->addWidget(m_toolbarWidget);
}

void PDFViewerWidget::setupSearchBar()
{
    m_searchWidget = new QWidget(this);
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
    
    QLabel *searchLabel = new QLabel("Search:", m_searchWidget);
    
    m_searchInput = new QLineEdit(m_searchWidget);
    m_searchInput->setPlaceholderText("Enter search term...");
    connect(m_searchInput, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchTextChanged);
    connect(m_searchInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onSearchNext);
    
    m_searchPrevButton = new QPushButton("◀", m_searchWidget);
    m_searchPrevButton->setToolTip("Previous Result");
    connect(m_searchPrevButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchPrevious);
    
    m_searchNextButton = new QPushButton("▶", m_searchWidget);
    m_searchNextButton->setToolTip("Next Result");
    connect(m_searchNextButton, &QPushButton::clicked, this, &PDFViewerWidget::onSearchNext);
    
    m_searchResultsLabel = new QLabel("", m_searchWidget);
    m_searchResultsLabel->setMinimumWidth(80);
    
    m_caseSensitiveCheck = new QCheckBox("Case", m_searchWidget);
    m_caseSensitiveCheck->setToolTip("Case Sensitive");
    connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleCaseSensitive);
    
    m_wholeWordsCheck = new QCheckBox("Whole", m_searchWidget);
    m_wholeWordsCheck->setToolTip("Whole Words");
    connect(m_wholeWordsCheck, &QCheckBox::toggled, this, &PDFViewerWidget::onToggleWholeWords);
    
    m_closeSearchButton = new QPushButton("✕", m_searchWidget);
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
    
    // Add search bar to main layout (after toolbar)
    layout()->addWidget(m_searchWidget);
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
        
        // Update UI
        m_pageInput->setText("1");
        m_pageCountLabel->setText(QString("/ %1").arg(m_pageCount));
        m_zoomSlider->setValue(static_cast<int>(m_zoomLevel * 100));
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_zoomLevel * 100)));
        
        // Initialize scroll state and text search if needed
        if (!m_scrollState) {
            m_scrollState = std::make_unique<PDFScrollState>();
        }
        
        if (!m_textSearch) {
            m_textSearch = std::make_unique<TextSearch>();
        }
        
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

void PDFViewerWidget::closePDF()
{
    if (!m_isPDFLoaded) {
        return;
    }
    
    // Clean up OpenGL textures
    makeCurrent();
    if (!m_pageTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pageTextures.size()), m_pageTextures.data());
        m_pageTextures.clear();
    }
    doneCurrent();
    
    // Reset state
    m_isPDFLoaded = false;
    m_filePath.clear();
    m_currentPage = 0;
    m_pageCount = 0;
    m_pageWidths.clear();
    m_pageHeights.clear();
    m_scrollOffsetX = 0.0f;
    m_scrollOffsetY = 0.0f;
    
    // Update UI
    m_pageInput->setText("0");
    m_pageCountLabel->setText("/ 0");
    
    // Hide search if visible
    if (m_searchWidget->isVisible()) {
        clearSearch();
    }
    
    update();
    emit pdfClosed();
}

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
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Create shader program for rendering PDF pages
    m_shaderProgram = new QOpenGLShaderProgram(this);
    
    // Vertex shader
    const char *vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;
        
        out vec2 TexCoord;
        
        uniform mat4 projection;
        uniform mat4 model;
        
        void main()
        {
            gl_Position = projection * model * vec4(aPos, 1.0);
            TexCoord = aTexCoord;
        }
    )";
    
    // Fragment shader
    const char *fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        
        in vec2 TexCoord;
        
        uniform sampler2D pageTexture;
        
        void main()
        {
            FragColor = texture(pageTexture, TexCoord);
        }
    )";
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qDebug() << "Failed to compile vertex shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        qDebug() << "Failed to compile fragment shader:" << m_shaderProgram->log();
    }
    
    if (!m_shaderProgram->link()) {
        qDebug() << "Failed to link shader program:" << m_shaderProgram->log();
    }
    
    // Create vertex array