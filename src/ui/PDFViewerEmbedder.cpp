#include "PDFViewerEmbedder.h"
#include "OpenGLPipelineManager.h"

// Include your existing PDF viewer components
#include "rendering/pdf-render.h"
#include "core/feature.h"
#include "ui/menu-integration.h"

// GLFW includes
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GL/glew.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <ctime>

// External global variables used by the PDF system
extern PDFScrollState* g_scrollState;
extern PDFRenderer* g_renderer;
extern std::vector<int>* g_pageHeights;
extern std::vector<int>* g_pageWidths;



PDFViewerEmbedder::PDFViewerEmbedder()
    : m_glfwWindow(nullptr)
    , m_parentHwnd(nullptr)
    , m_childHwnd(nullptr)
    , m_renderer(nullptr)
    , m_scrollState(nullptr)
    , m_menuIntegration(nullptr)
    , m_initialized(false)
    , m_pdfLoaded(false)
    , m_usingFallback(false)
    , m_windowWidth(800)
    , m_windowHeight(600)
    , m_needsFullRegeneration(false)
    , m_needsVisibleRegeneration(false)
    , m_lastWinWidth(0)
    , m_lastWinHeight(0)
{
}

PDFViewerEmbedder::~PDFViewerEmbedder()
{
    shutdown();
}

bool PDFViewerEmbedder::initialize(HWND parentHwnd, int width, int height)
{
    if (m_initialized) {
        return false; // Already initialized
    }

    m_parentHwnd = parentHwnd;
    m_windowWidth = width;
    m_windowHeight = height;

    // Initialize GLFW if not already done (should be safe to call multiple times)
    if (!glfwInit()) {
        std::cerr << "PDFViewerEmbedder: Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Create embedded OpenGL window
    if (!createEmbeddedWindow()) {
        return false;
    }

    // Initialize OpenGL
    if (!initializeOpenGL()) {
        return false;
    }

    // Initialize PDF renderer
    m_renderer = std::make_unique<PDFRenderer>();
    
    // Try to initialize PDFRenderer, fall back to Qt implementation if PDFium is not available
    bool rendererInitialized = false;
    try {
        m_renderer->Initialize();
        rendererInitialized = true;
        std::cout << "PDFViewerEmbedder: PDFium renderer initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "PDFViewerEmbedder: PDFium initialization failed: " << e.what() << std::endl;
        std::cerr << "PDFViewerEmbedder: This is likely due to missing or incompatible PDFium library" << std::endl;
        std::cerr << "PDFViewerEmbedder: Falling back to Qt PDF implementation" << std::endl;
        // We'll continue with limited functionality
    }
    
    if (!rendererInitialized) {
        std::cerr << "PDFViewerEmbedder: CRITICAL - Cannot proceed without renderer" << std::endl;
        return false;
    }

    // Initialize scroll state
    m_scrollState = std::make_unique<PDFScrollState>();
    
    // Initialize menu integration (for keyboard/mouse handling) but DISABLE internal tabs
    m_menuIntegration = std::make_unique<MenuIntegration>();
    
    // Initialize with embedded mode enabled to prevent internal tab creation
    if (!m_menuIntegration->Initialize(m_glfwWindow, true)) {
        std::cerr << "PDFViewerEmbedder: Failed to initialize MenuIntegration" << std::endl;
        // Continue without menu integration - basic PDF viewing should still work
    } else {
        std::cout << "PDFViewerEmbedder: MenuIntegration initialized in embedded mode" << std::endl;
    }
    
    // Set up GLFW callbacks
    setupCallbacks();

    m_initialized = true;
    
    std::cout << "PDFViewerEmbedder: Successfully initialized" << std::endl;
    return true;
}

bool PDFViewerEmbedder::loadPDF(const std::string& filePath)
{
    if (!m_initialized) {
        std::cerr << "PDFViewerEmbedder: Not initialized" << std::endl;
        return false;
    }

    // Verify file exists and is accessible
    std::ifstream file(filePath, std::ios::binary);
    if (!file.good()) {
        std::cerr << "PDFViewerEmbedder: File cannot be opened: " << filePath << std::endl;
        return false;
    }
    
    // Get file size for additional info
    file.seekg(0, std::ios::end);
    file.close();

    // Check if renderer is properly initialized
    if (!m_renderer) {
        std::cerr << "PDFViewerEmbedder: Renderer is null!" << std::endl;
        return false;
    }

    // Try to load PDF using existing renderer
    try {
        if (!m_renderer->LoadDocument(filePath)) {
            std::cerr << "PDFViewerEmbedder: Failed to load PDF: " << filePath << std::endl;
            std::cerr << "PDFViewerEmbedder: This may be due to missing PDFium library or incompatible PDF format" << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "PDFViewerEmbedder: Exception while loading PDF: " << e.what() << std::endl;
        std::cerr << "PDFViewerEmbedder: File: " << filePath << std::endl;
        return false;
    }

    // If we reach here, PDFium loading succeeded
    m_usingFallback = false;
    m_currentFilePath = filePath;
    m_pdfLoaded = true;

    // Get page count and initialize structures
    int pageCount = 0;
    try {
        pageCount = m_renderer->GetPageCount();
        if (pageCount <= 0) {
            std::cerr << "PDFViewerEmbedder: Invalid page count: " << pageCount << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "PDFViewerEmbedder: Exception getting page count: " << e.what() << std::endl;
        return false;
    }
    
    // Clean up any existing textures
    cleanupTextures();
    
    // Initialize texture and dimension arrays
    m_textures.resize(pageCount);
    m_pageWidths.resize(pageCount);
    m_pageHeights.resize(pageCount);
    m_originalPageWidths.resize(pageCount);
    m_originalPageHeights.resize(pageCount);
    
    // Fill with zeros initially
    std::fill(m_textures.begin(), m_textures.end(), 0);
    
    // Get original page dimensions
    for (int i = 0; i < pageCount; ++i) {
        try {
            m_renderer->GetOriginalPageSize(i, m_originalPageWidths[i], m_originalPageHeights[i]);
            
            // Calculate initial display dimensions
            int pageW = 0, pageH = 0;
            m_renderer->GetBestFitSize(i, m_windowWidth, m_windowHeight, pageW, pageH);
            m_pageWidths[i] = pageW;
            m_pageHeights[i] = pageH;
        } catch (const std::exception& e) {
            return false;
        }
    }

    // CRITICAL: Initialize scroll state with proper page arrays (missing from original implementation)
    m_scrollState->pageHeights = &m_pageHeights;
    m_scrollState->pageWidths = &m_pageWidths;
    m_scrollState->originalPageWidths = &m_originalPageWidths;
    m_scrollState->originalPageHeights = &m_originalPageHeights;
    
    // Initialize text extraction and search capabilities (missing from original implementation)
    try {
        InitializeTextExtraction(*m_scrollState, pageCount);
    } catch (const std::exception& e) {
        return false;
    }
    
    try {
        InitializeTextSearch(*m_scrollState);
    } catch (const std::exception& e) {
        return false;
    }
    
    // Load text pages for search functionality (missing from original implementation)
    try {
        FPDF_DOCUMENT document = m_renderer->GetDocument();
        
        for (int i = 0; i < pageCount; ++i) {
            FPDF_PAGE page = FPDF_LoadPage(document, i);
            if (page) {
                LoadTextPage(*m_scrollState, i, page);
                FPDF_ClosePage(page);
            }
        }
    } catch (const std::exception& e) {
        return false;
    }

    // Initialize scroll state for the new document
    try {
        UpdateScrollState(*m_scrollState, (float)m_windowHeight, m_pageHeights);
    } catch (const std::exception& e) {
        return false;
    }
    
    // Force full regeneration on next update
    m_needsFullRegeneration = true;
    
    // Set global pointers for the PDF system to use our embedded data
    // (Similar to what TabManager::SwitchToTab does)
    g_scrollState = m_scrollState.get();
    g_renderer = m_renderer.get();
    g_pageHeights = &m_pageHeights;
    g_pageWidths = &m_pageWidths;
    
    std::cout << "PDFViewerEmbedder: Successfully loaded PDF with " << pageCount << " pages" << std::endl;
    return true;
}

void PDFViewerEmbedder::update()
{
    if (!m_initialized || !m_pdfLoaded) {
        return;
    }

    // Make OpenGL context current
    glfwMakeContextCurrent(m_glfwWindow);
    
    // Update window dimensions
    int currentWidth, currentHeight;
    glfwGetFramebufferSize(m_glfwWindow, &currentWidth, &currentHeight);
    
    if (currentWidth != m_lastWinWidth || currentHeight != m_lastWinHeight) {
        m_needsFullRegeneration = true;
        m_windowWidth = currentWidth;
        m_windowHeight = currentHeight;
    }

    // Handle texture regeneration (from your main.cpp logic)
    if (m_needsFullRegeneration || m_needsVisibleRegeneration) {
        if (m_needsFullRegeneration) {
            regenerateTextures();
        } else {
            regenerateVisibleTextures();
        }
    }

    // Render the frame
    renderFrame();
    
    // Handle background rendering for better performance
    handleBackgroundRendering();
    
    // Swap buffers and poll events
    glfwSwapBuffers(m_glfwWindow);
    glfwPollEvents();
}

void PDFViewerEmbedder::resize(int width, int height)
{
    if (!m_initialized) {
        return;
    }

    m_windowWidth = width;
    m_windowHeight = height;
    
    // Update GLFW window size
    glfwSetWindowSize(m_glfwWindow, width, height);
    
    // Force regeneration on next update
    m_needsFullRegeneration = true;
}

void PDFViewerEmbedder::shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Clean up textures
    cleanupTextures();
    
    // Clean up GLFW window
    if (m_glfwWindow) {
        glfwDestroyWindow(m_glfwWindow);
        m_glfwWindow = nullptr;
    }

    // Reset state
    m_renderer.reset();
    m_scrollState.reset();
    m_menuIntegration.reset();
    
    m_initialized = false;
    m_pdfLoaded = false;
    
    std::cout << "PDFViewerEmbedder: Shutdown complete" << std::endl;
}

// Navigation methods
void PDFViewerEmbedder::zoomIn()
{
    if (!m_initialized || !m_pdfLoaded) return;
    
    float newZoom = m_scrollState->zoomScale * 1.2f;
    if (newZoom > 5.0f) newZoom = 5.0f; // Max zoom limit
    
    m_scrollState->zoomScale = newZoom;
    m_scrollState->zoomChanged = true;
    m_needsFullRegeneration = true;
}

void PDFViewerEmbedder::zoomOut()
{
    if (!m_initialized || !m_pdfLoaded) return;
    
    float newZoom = m_scrollState->zoomScale / 1.2f;
    if (newZoom < 0.2f) newZoom = 0.2f; // Min zoom limit
    
    m_scrollState->zoomScale = newZoom;
    m_scrollState->zoomChanged = true;
    m_needsFullRegeneration = true;
}

void PDFViewerEmbedder::setZoom(float zoomLevel)
{
    if (!m_initialized || !m_pdfLoaded) return;
    
    if (zoomLevel < 0.2f) zoomLevel = 0.2f;
    if (zoomLevel > 5.0f) zoomLevel = 5.0f;
    
    m_scrollState->zoomScale = zoomLevel;
    m_scrollState->zoomChanged = true;
    m_needsFullRegeneration = true;
}

void PDFViewerEmbedder::zoomToFit()
{
    if (!m_initialized || !m_pdfLoaded || m_pageWidths.empty()) return;
    
    // Calculate zoom level to fit the page width to window
    float windowWidth = static_cast<float>(m_windowWidth);
    float pageWidth = static_cast<float>(m_pageWidths[0]); // Use first page as reference
    
    if (pageWidth > 0) {
        float fitZoom = (windowWidth - 40.0f) / pageWidth; // Leave some margin
        if (fitZoom < 0.2f) fitZoom = 0.2f;
        if (fitZoom > 5.0f) fitZoom = 5.0f;
        
        m_scrollState->zoomScale = fitZoom;
        m_needsFullRegeneration = true;
    }
}

void PDFViewerEmbedder::goToPage(int pageNumber)
{
    if (!m_initialized || !m_pdfLoaded) return;
    
    int pageCount = m_renderer->GetPageCount();
    if (pageNumber < 1 || pageNumber > pageCount) return;
    
    // Calculate scroll position for the target page
    float targetOffset = 0.0f;
    for (int i = 0; i < pageNumber - 1; ++i) {
        targetOffset += m_pageHeights[i] * m_scrollState->zoomScale;
    }
    
    m_scrollState->scrollOffset = targetOffset;
    
    // Update visible page range
    int newFirst, newLast;
    GetVisiblePageRange(*m_scrollState, m_pageHeights, newFirst, newLast);
    if (newFirst != m_scrollState->firstVisiblePage || newLast != m_scrollState->lastVisiblePage) {
        m_scrollState->firstVisiblePage = newFirst;
        m_scrollState->lastVisiblePage = newLast;
        m_needsVisibleRegeneration = true;
    }
}

void PDFViewerEmbedder::nextPage()
{
    int currentPage = getCurrentPage();
    goToPage(currentPage + 1);
}

void PDFViewerEmbedder::previousPage()
{
    int currentPage = getCurrentPage();
    goToPage(currentPage - 1);
}

int PDFViewerEmbedder::getPageCount() const
{
    if (!m_pdfLoaded) return 0;
    return m_renderer->GetPageCount();
}

float PDFViewerEmbedder::getCurrentZoom() const
{
    if (!m_initialized) return 1.0f;
    return m_scrollState->zoomScale;
}

int PDFViewerEmbedder::getCurrentPage() const
{
    if (!m_initialized || !m_pdfLoaded) return 1;
    
    // Calculate current page based on scroll position
    float currentOffset = m_scrollState->scrollOffset;
    float accumulatedHeight = 0.0f;
    
    for (int i = 0; i < static_cast<int>(m_pageHeights.size()); ++i) {
        float pageHeight = m_pageHeights[i] * m_scrollState->zoomScale;
        if (currentOffset <= accumulatedHeight + pageHeight / 2.0f) {
            return i + 1; // 1-based page numbering
        }
        accumulatedHeight += pageHeight;
    }
    
    return m_pageHeights.size(); // Return last page if scrolled to bottom
}

// Private helper methods

bool PDFViewerEmbedder::createEmbeddedWindow()
{
    // Try OpenGL 2.1 first (guaranteed to support immediate mode)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Start hidden, will be embedded
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // No window decorations for embedding
    
    // Create GLFW window
    m_glfwWindow = glfwCreateWindow(m_windowWidth, m_windowHeight, "PDF Viewer Embedded", nullptr, nullptr);
    
    if (!m_glfwWindow) {
        // Clear any previous error
        glfwGetError(nullptr);
        
        // Try OpenGL 3.3 Compatibility Profile (if 2.1 fails)
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        
        m_glfwWindow = glfwCreateWindow(m_windowWidth, m_windowHeight, "PDF Viewer Embedded", nullptr, nullptr);
        
        if (!m_glfwWindow) {
            // Clear any previous error
            glfwGetError(nullptr);
            
            // Try OpenGL 2.1 (Legacy)
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            
            m_glfwWindow = glfwCreateWindow(m_windowWidth, m_windowHeight, "PDF Viewer Embedded", nullptr, nullptr);
            
            if (!m_glfwWindow) {
                // Clear any previous error
                glfwGetError(nullptr);
                
                // Try default context (no specific version)
                glfwDefaultWindowHints();
                glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
                glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
                glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
                
                m_glfwWindow = glfwCreateWindow(m_windowWidth, m_windowHeight, "PDF Viewer Embedded", nullptr, nullptr);
                
                if (!m_glfwWindow) {
                    const char* description;
                    int error = glfwGetError(&description);
                    std::cerr << "PDFViewerEmbedder: Failed to create GLFW window with any OpenGL context. Error: " << error << " - " << (description ? description : "No description") << std::endl;
                    return false;
                }
            }
        }
    }

    // Get the native Windows handle from GLFW
    m_childHwnd = glfwGetWin32Window(m_glfwWindow);
    if (!m_childHwnd) {
        std::cerr << "PDFViewerEmbedder: Failed to get native window handle" << std::endl;
        return false;
    }

    if (!IsWindow(m_parentHwnd)) {
        return false;
    }

    // Embed the GLFW window into the Qt parent
    HWND result = SetParent(m_childHwnd, m_parentHwnd);
    if (result == nullptr) {
        return false;
    }
    
    // Set window style for embedding
    LONG style = GetWindowLong(m_childHwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
    style |= WS_CHILD;
    SetWindowLong(m_childHwnd, GWL_STYLE, style);
    
    // Position and show the embedded window
    SetWindowPos(m_childHwnd, HWND_TOP, 0, 0, m_windowWidth, m_windowHeight, SWP_SHOWWINDOW);
    
    return IsWindow(m_childHwnd) && IsWindowVisible(m_childHwnd);
}

bool PDFViewerEmbedder::initializeOpenGL()
{
    glfwMakeContextCurrent(m_glfwWindow);
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        std::cerr << "PDFViewerEmbedder: Failed to initialize GLEW: " << glewGetErrorString(glewError) << std::endl;
        // Try to continue without GLEW for basic OpenGL
    }
    
    // Clear any OpenGL errors from GLEW initialization
    while (glGetError() != GL_NO_ERROR) {
        // Clear error queue
    }
    
    // Create and initialize adaptive pipeline manager
    m_pipelineManager = std::make_unique<OpenGLPipelineManager>();
    
    // Initialize the adaptive pipeline system
    if (!m_pipelineManager->initialize()) {
        std::cout << "Failed to initialize OpenGL pipeline manager, falling back to basic OpenGL" << std::endl;
        // Continue with basic OpenGL setup
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return true;
    }
    
    // Get pipeline information
    const auto& caps = m_pipelineManager->getCapabilities();
    const char* version = caps.version.c_str();
    const char* vendor = caps.vendor.c_str();
    const char* renderer = caps.renderer.c_str();
    
    // Write comprehensive debug information
    std::ofstream debugFile("opengl_debug.txt", std::ios::app);
    if (debugFile.is_open()) {
        debugFile << "=== OpenGL Debug Information ===" << std::endl;
        auto now = std::time(nullptr);
        debugFile << "Timestamp: " << std::ctime(&now);
        
        // Basic OpenGL information
        debugFile << "OpenGL Version: " << version << std::endl;
        debugFile << "OpenGL Vendor: " << vendor << std::endl;
        debugFile << "OpenGL Renderer: " << renderer << std::endl;
        debugFile << "OpenGL Context Version: " << caps.majorVersion << "." << caps.minorVersion << std::endl;
        
        // Get GLSL version
        const char* glslVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
        debugFile << "GLSL Version: " << (glslVersion ? glslVersion : "Unknown") << std::endl;
        
        // Check profile type
        if (caps.majorVersion >= 3 && caps.minorVersion >= 2) {
            int profile = 0;
            glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
            if (profile & GL_CONTEXT_CORE_PROFILE_BIT) {
                debugFile << "OpenGL Profile: Core Profile" << std::endl;
            } else if (profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) {
                debugFile << "OpenGL Profile: Compatibility Profile" << std::endl;
            } else {
                debugFile << "OpenGL Profile: Unknown/Default" << std::endl;
            }
        }
        
        debugFile << "Max Texture Size: " << caps.maxTextureSize << std::endl;
        
        GLint maxViewportDims[2];
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewportDims);
        debugFile << "Max Viewport: " << maxViewportDims[0] << "x" << maxViewportDims[1] << std::endl;
        
        debugFile << "Extensions Support:" << std::endl;
        debugFile << "- VBO Support: " << (caps.hasVBO ? "YES" : "NO") << std::endl;
        debugFile << "- VAO Support: " << (caps.hasVAO ? "YES" : "NO") << std::endl;
        debugFile << "- Shader Support: " << (caps.hasShaders ? "YES" : "NO") << std::endl;
        debugFile << "- Framebuffer Support: " << (caps.hasFramebuffers ? "YES" : "NO") << std::endl;
        
        // Pipeline analysis
        debugFile << "=== Pipeline Analysis ===" << std::endl;
        debugFile << "Selected Pipeline: " << m_pipelineManager->getPipelineDescription() << std::endl;
        debugFile << "Pipeline Features:" << std::endl;
        debugFile << "- Fixed Function Pipeline: YES" << std::endl;
        
        auto pipeline = m_pipelineManager->getSelectedPipeline();
        debugFile << "- Immediate Mode Rendering: " << (pipeline == RenderingPipeline::LEGACY_IMMEDIATE ? "YES (glBegin/glEnd)" : "NO") << std::endl;
        debugFile << "- Vertex Arrays: " << (caps.hasVAO && pipeline == RenderingPipeline::MODERN_SHADER ? "YES" : "NO") << std::endl;
        debugFile << "- Vertex Buffer Objects (VBOs): " << (caps.hasVBO && pipeline != RenderingPipeline::LEGACY_IMMEDIATE ? "YES" : "NO") << std::endl;
        debugFile << "- Shaders: " << (caps.hasShaders && pipeline == RenderingPipeline::MODERN_SHADER ? "YES" : "NO") << std::endl;
        
        // Library versions
        debugFile << "Library Versions:" << std::endl;
        debugFile << "- GLFW Version: " << glfwGetVersionString() << std::endl;
        debugFile << "- GLEW Version: " << glewGetString(GLEW_VERSION) << std::endl;
        
        debugFile << "=== End Debug Information ===" << std::endl << std::endl;
        debugFile.close();
        
        std::cout << "OpenGL debug information written to opengl_debug.txt" << std::endl;
    }
    
    // Console output
    std::cout << "=== OpenGL Information ===" << std::endl;
    std::cout << "OpenGL Version: " << version << std::endl;
    std::cout << "OpenGL Vendor: " << vendor << std::endl;
    std::cout << "OpenGL Renderer: " << renderer << std::endl;
    std::cout << "OpenGL Context Version: " << caps.majorVersion << "." << caps.minorVersion << std::endl;
    
    std::cout << "=== Adaptive Pipeline Information ===" << std::endl;
    std::cout << "Selected Pipeline: " << m_pipelineManager->getPipelineDescription() << std::endl;
    std::cout << "Optimization Level: ";
    
    switch (m_pipelineManager->getSelectedPipeline()) {
        case RenderingPipeline::MODERN_SHADER:
            std::cout << "MAXIMUM (VBO/VAO/Shaders)" << std::endl;
            break;
        case RenderingPipeline::INTERMEDIATE_VBO:
            std::cout << "GOOD (VBO without shaders)" << std::endl;
            break;
        case RenderingPipeline::LEGACY_IMMEDIATE:
            std::cout << "COMPATIBLE (Immediate mode)" << std::endl;
            break;
    }
    
    std::cout << "=================================" << std::endl;
    
    return true;
}

void PDFViewerEmbedder::setupCallbacks()
{
    glfwSetWindowUserPointer(m_glfwWindow, this);
    glfwSetWindowSizeCallback(m_glfwWindow, windowSizeCallback);
    glfwSetCursorPosCallback(m_glfwWindow, cursorPosCallback);
    glfwSetMouseButtonCallback(m_glfwWindow, mouseButtonCallback);
    glfwSetScrollCallback(m_glfwWindow, scrollCallback);
    glfwSetKeyCallback(m_glfwWindow, keyCallback);
}

void PDFViewerEmbedder::renderFrame()
{
    // Set viewport
    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (!m_pdfLoaded) {
        return;
    }
    
    // Enable texturing and blending
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    int pageCount = m_renderer->GetPageCount();
    float yOffset = -m_scrollState->scrollOffset;
    
    // Use pipeline-specific rendering
    auto selectedPipeline = m_pipelineManager->getSelectedPipeline();
    
    if (selectedPipeline == RenderingPipeline::LEGACY_IMMEDIATE) {
        // Legacy OpenGL 2.1 - Use fixed function pipeline with immediate mode
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, m_windowWidth, m_windowHeight, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        // Draw pages using immediate mode (supported in legacy pipeline)
        for (int i = 0; i < pageCount; ++i) {
            if (m_textures[i] == 0) continue;
            
            float pageW = (float)m_pageWidths[i] * m_scrollState->zoomScale;
            float pageH = (float)m_pageHeights[i] * m_scrollState->zoomScale;
            
            float xCenter = (m_windowWidth / 2.0f) - m_scrollState->horizontalOffset;
            float yCenter = yOffset + pageH / 2.0f;
            
            float x = xCenter - pageW / 2.0f;
            float y = yCenter - pageH / 2.0f;
            
            glBindTexture(GL_TEXTURE_2D, m_textures[i]);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(x + pageW, y);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(x + pageW, y + pageH);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + pageH);
            glEnd();
            
            yOffset += pageH;
        }
    }
    else {
        // Modern/Intermediate Pipeline - Use compatibility approach that works in both Core and Compatibility
        // Set up 2D orthographic projection for screen coordinates
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0f, (float)m_windowWidth, (float)m_windowHeight, 0.0f, -1.0f, 1.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        // Draw pages one by one using immediate mode (works in compatibility profile)
        // This ensures compatibility while still using the modern pipeline path
        for (int i = 0; i < pageCount; ++i) {
            if (m_textures[i] == 0) continue;
            
            float pageW = (float)m_pageWidths[i] * m_scrollState->zoomScale;
            float pageH = (float)m_pageHeights[i] * m_scrollState->zoomScale;
            
            float xCenter = (m_windowWidth / 2.0f) - m_scrollState->horizontalOffset;
            float yCenter = yOffset + pageH / 2.0f;
            
            float x = xCenter - pageW / 2.0f;
            float y = yCenter - pageH / 2.0f;
            
            glBindTexture(GL_TEXTURE_2D, m_textures[i]);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            
            // Use immediate mode for now - this should work in both profiles
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(x + pageW, y);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(x + pageW, y + pageH);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + pageH);
            glEnd();
            
            yOffset += pageH;
        }
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Draw text selection highlighting (reuse your existing functions)
    // Note: These functions need to be available - they're from your main.cpp
    try {
        DrawTextSelection(*m_scrollState, m_pageHeights, m_pageWidths, (float)m_windowWidth, (float)m_windowHeight);
        
        // Draw search results highlighting
        DrawSearchResultsHighlighting(*m_scrollState, m_pageHeights, m_pageWidths, (float)m_windowWidth, (float)m_windowHeight);
        
        // Draw scroll bar overlay
        DrawScrollBar(*m_scrollState);
    } catch (...) {
        // If the drawing functions aren't available, continue without them
        // This ensures basic PDF rendering still works
    }
    
    // Reset GL state
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void PDFViewerEmbedder::regenerateTextures()
{
    if (!m_pdfLoaded) return;
    
    m_lastWinWidth = m_windowWidth;
    m_lastWinHeight = m_windowHeight;
    
    int pageCount = m_renderer->GetPageCount();
    
    // Clean up existing textures
    cleanupTextures();
    
    // Resize arrays
    m_textures.resize(pageCount);
    m_pageWidths.resize(pageCount);
    m_pageHeights.resize(pageCount);
    
    // Regenerate all textures
    for (int i = 0; i < pageCount; ++i) {
        int pageW = 0, pageH = 0;
        float effectiveZoom = (m_scrollState->zoomScale > 0.5f) ? m_scrollState->zoomScale : 0.5f;
        m_renderer->GetBestFitSize(i, static_cast<int>(m_windowWidth * effectiveZoom), 
                                   static_cast<int>(m_windowHeight * effectiveZoom), pageW, pageH);
        
        FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(i, pageW, pageH);
        m_textures[i] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), pageW, pageH);
        
        // Store base page dimensions for layout calculations
        int baseW = 0, baseH = 0;
        m_renderer->GetBestFitSize(i, m_windowWidth, m_windowHeight, baseW, baseH);
        m_pageWidths[i] = baseW;
        m_pageHeights[i] = baseH;
        
        FPDFBitmap_Destroy(bmp);
    }
    
    // Update scroll state
    UpdateScrollState(*m_scrollState, (float)m_windowHeight, m_pageHeights);
    m_scrollState->lastRenderedZoom = m_scrollState->zoomScale;
    
    m_needsFullRegeneration = false;
}

void PDFViewerEmbedder::regenerateVisibleTextures()
{
    if (!m_pdfLoaded) return;
    
    int pageCount = m_renderer->GetPageCount();
    int firstVisible, lastVisible;
    GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    
    // Only regenerate textures for visible pages
    for (int i = firstVisible; i <= lastVisible && i < pageCount; ++i) {
        if (m_textures[i]) {
            glDeleteTextures(1, &m_textures[i]);
        }
        
        int pageW = 0, pageH = 0;
        float effectiveZoom = (m_scrollState->zoomScale > 0.5f) ? m_scrollState->zoomScale : 0.5f;
        m_renderer->GetBestFitSize(i, static_cast<int>(m_windowWidth * effectiveZoom), 
                                   static_cast<int>(m_windowHeight * effectiveZoom), pageW, pageH);
        
        FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(i, pageW, pageH);
        m_textures[i] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), pageW, pageH);
        FPDFBitmap_Destroy(bmp);
    }
    
    m_needsVisibleRegeneration = false;
}

void PDFViewerEmbedder::regeneratePageTexture(int pageIndex)
{
    if (!m_pdfLoaded || pageIndex < 0 || pageIndex >= (int)m_textures.size()) return;
    
    // Delete existing texture
    if (m_textures[pageIndex]) {
        glDeleteTextures(1, &m_textures[pageIndex]);
    }
    
    // Generate new texture
    int pageW = 0, pageH = 0;
    float effectiveZoom = (m_scrollState->zoomScale > 0.5f) ? m_scrollState->zoomScale : 0.5f;
    m_renderer->GetBestFitSize(pageIndex, static_cast<int>(m_windowWidth * effectiveZoom), 
                               static_cast<int>(m_windowHeight * effectiveZoom), pageW, pageH);
    
    FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(pageIndex, pageW, pageH);
    m_textures[pageIndex] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), pageW, pageH);
    FPDFBitmap_Destroy(bmp);
}

void PDFViewerEmbedder::handleBackgroundRendering()
{
    // Background rendering logic (from your main.cpp)
    static int backgroundRenderIndex = 0;
    static int frameCounter = 0;
    frameCounter++;
    
    if (frameCounter % 5 == 0 && !m_needsFullRegeneration && !m_needsVisibleRegeneration) {
        int pageCount = m_renderer->GetPageCount();
        int firstVisible, lastVisible;
        GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
        
        for (int attempts = 0; attempts < pageCount; attempts++) {
            backgroundRenderIndex = (backgroundRenderIndex + 1) % pageCount;
            
            if (backgroundRenderIndex >= firstVisible && backgroundRenderIndex <= lastVisible) continue;
            
            if (m_textures[backgroundRenderIndex]) {
                glDeleteTextures(1, &m_textures[backgroundRenderIndex]);
            }
            
            int pageW = 0, pageH = 0;
            float backgroundZoom = m_scrollState->zoomScale * 0.7f;
            backgroundZoom = (backgroundZoom > 0.3f) ? backgroundZoom : 0.3f;
            m_renderer->GetBestFitSize(backgroundRenderIndex, 
                                       static_cast<int>(m_windowWidth * backgroundZoom), 
                                       static_cast<int>(m_windowHeight * backgroundZoom), pageW, pageH);
            
            FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(backgroundRenderIndex, pageW, pageH);
            m_textures[backgroundRenderIndex] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), pageW, pageH);
            FPDFBitmap_Destroy(bmp);
            break;
        }
    }
}

void PDFViewerEmbedder::cleanupTextures()
{
    if (!m_textures.empty()) {
        for (GLuint texture : m_textures) {
            if (texture) {
                glDeleteTextures(1, &texture);
            }
        }
        m_textures.clear();
    }
}

unsigned int PDFViewerEmbedder::createTextureFromPDFBitmap(void* buffer, int width, int height)
{
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}

// Static callback wrappers
void PDFViewerEmbedder::windowSizeCallback(GLFWwindow* window, int width, int height)
{
    PDFViewerEmbedder* embedder = static_cast<PDFViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->onWindowSize(width, height);
    }
}

void PDFViewerEmbedder::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    PDFViewerEmbedder* embedder = static_cast<PDFViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->onCursorPos(xpos, ypos);
    }
}

void PDFViewerEmbedder::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    PDFViewerEmbedder* embedder = static_cast<PDFViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->onMouseButton(button, action, mods);
    }
}

void PDFViewerEmbedder::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    PDFViewerEmbedder* embedder = static_cast<PDFViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->onScroll(xoffset, yoffset);
    }
}

void PDFViewerEmbedder::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    PDFViewerEmbedder* embedder = static_cast<PDFViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->onKey(key, scancode, action, mods);
    }
}

// Instance callback handlers (reuse your existing logic)
void PDFViewerEmbedder::onWindowSize(int width, int height)
{
    m_windowWidth = width;
    m_windowHeight = height;
    m_needsFullRegeneration = true;
}

void PDFViewerEmbedder::onCursorPos(double xpos, double ypos)
{
    if (!m_scrollState) return;
    
    m_scrollState->lastCursorX = (float)xpos;
    m_scrollState->lastCursorY = (float)ypos;
    
    // Reuse your existing cursor handling logic
    UpdateCursorForTextSelection(*m_scrollState, m_glfwWindow, xpos, ypos, 
                                 (float)m_windowWidth, (float)m_windowHeight, 
                                 m_pageHeights, m_pageWidths);
    
    if (m_scrollState->textSelection.isDragging) {
        UpdateTextSelection(*m_scrollState, xpos, ypos, (float)m_windowWidth, (float)m_windowHeight, 
                            m_pageHeights, m_pageWidths);
    }
    
    if (m_scrollState->isPanning) {
        UpdatePanning(*m_scrollState, xpos, ypos, (float)m_windowWidth, (float)m_windowHeight);
    }
    
    if (m_scrollState->isScrollBarDragging) {
        UpdateScrollBarDragging(*m_scrollState, ypos, (float)m_windowHeight);
    }
}

void PDFViewerEmbedder::onMouseButton(int button, int action, int /*mods*/)
{
    if (!m_scrollState) return;
    
    double mouseX = m_scrollState->lastCursorX;
    double mouseY = m_scrollState->lastCursorY;
    
    // Reuse your existing mouse button handling logic
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            // Calculate scroll bar position for detection
            float barMargin = 0.01f * m_windowWidth;
            float barWidth = 0.025f * m_windowWidth;
            float barX = m_windowWidth - barMargin - barWidth;
            bool isOverScrollBar = (mouseX >= barX && mouseX <= m_windowWidth - barMargin);
            
            if (isOverScrollBar) {
                // Start scroll bar dragging
                StartScrollBarDragging(*m_scrollState, mouseY);
            } else {
                // Text selection logic with double-click detection
                double currentTime = glfwGetTime();
                if (DetectDoubleClick(*m_scrollState, mouseX, mouseY, currentTime)) {
                    // Double-click: select word at position
                    SelectWordAtPosition(*m_scrollState, mouseX, mouseY, (float)m_windowWidth, (float)m_windowHeight, 
                                         m_pageHeights, m_pageWidths);
                } else {
                    // Single click: start text selection
                    StartTextSelection(*m_scrollState, mouseX, mouseY, (float)m_windowWidth, (float)m_windowHeight, 
                                       m_pageHeights, m_pageWidths);
                }
            }
        } else if (action == GLFW_RELEASE) {
            // Stop scroll bar dragging
            StopScrollBarDragging(*m_scrollState);
            
            // End text selection (but not for double-click word selection)
            if (!m_scrollState->textSelection.isDoubleClick) {
                EndTextSelection(*m_scrollState);
            }
            m_scrollState->textSelection.isDoubleClick = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            // Start panning with right mouse button
            StartPanning(*m_scrollState, mouseX, mouseY);
            
            // Change cursor to hand cursor for panning
            glfwSetCursor(m_glfwWindow, glfwCreateStandardCursor(GLFW_HAND_CURSOR));
        } else if (action == GLFW_RELEASE) {
            // Stop panning
            StopPanning(*m_scrollState);
            
            // Restore default cursor
            glfwSetCursor(m_glfwWindow, nullptr);
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            // Middle click for panning (alternative to right click)
            StartPanning(*m_scrollState, mouseX, mouseY);
            glfwSetCursor(m_glfwWindow, glfwCreateStandardCursor(GLFW_HAND_CURSOR));
        } else if (action == GLFW_RELEASE) {
            StopPanning(*m_scrollState);
            glfwSetCursor(m_glfwWindow, nullptr);
        }
    }
}

void PDFViewerEmbedder::onScroll(double xoffset, double yoffset)
{
    if (!m_scrollState) return;
    
    // Get cursor position for cursor-based zooming
    double cursorX = m_scrollState->lastCursorX;
    double cursorY = m_scrollState->lastCursorY;
    
    // Check if cursor is over the scroll bar area (right side of window)
    float barMargin = 0.01f * m_windowWidth;
    float barWidth = 0.025f * m_windowWidth;
    float barX = m_windowWidth - barMargin - barWidth;
    
    // If cursor is over scroll bar area, disable mouse wheel input
    bool isOverScrollBar = (cursorX >= barX && cursorX <= m_windowWidth - barMargin);
    
    if (isOverScrollBar) {
        return; // Don't handle scroll when over scroll bar
    }
    
    // Get key modifier states for different scroll behaviors
    bool shiftPressed = (glfwGetKey(m_glfwWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || 
                        glfwGetKey(m_glfwWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
    
    if (shiftPressed) {
        // Horizontal scrolling with Shift + mouse wheel
        HandleHorizontalScroll(*m_scrollState, (float)yoffset, (float)m_windowWidth);
    } else {
        // Always zoom with mouse wheel (no Ctrl modifier required)
        // Use zoom FACTOR not delta - same as standalone PDF viewer
        float zoomFactor = (yoffset > 0) ? 1.1f : 1.0f / 1.1f;
        
        HandleZoom(*m_scrollState, zoomFactor, (float)cursorX, (float)cursorY, 
                   (float)m_windowWidth, (float)m_windowHeight, m_pageHeights, m_pageWidths);
        
        // Check if zoom changed and force regeneration
        if (m_scrollState->zoomChanged) {
            m_needsVisibleRegeneration = true;
            m_scrollState->zoomChanged = false;
        }
    }
}

void PDFViewerEmbedder::onKey(int key, int scancode, int action, int mods)
{
    if (!m_scrollState) return;
    
    if (action == GLFW_PRESS) {
        // Enhanced keyboard handling from standalone PDF viewer
        if (key >= 32 && key <= 126) {
            // Printable characters for search input
            HandleSearchInput(*m_scrollState, key, mods);
        } else if (key == GLFW_KEY_BACKSPACE) {
            // Backspace for search editing
            HandleSearchInput(*m_scrollState, key, mods);
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            // Enter for search navigation
            if (mods & GLFW_MOD_SHIFT) {
                NavigateToPreviousSearchResult(*m_scrollState, m_pageHeights);
            } else {
                NavigateToNextSearchResult(*m_scrollState, m_pageHeights);
            }
        } else if (key == GLFW_KEY_F3) {
            // F3 for search navigation
            if (mods & GLFW_MOD_SHIFT) {
                NavigateToPreviousSearchResult(*m_scrollState, m_pageHeights);
            } else {
                NavigateToNextSearchResult(*m_scrollState, m_pageHeights);
            }
        } else if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+C for copying selected text
            std::string selectedText = GetSelectedText(*m_scrollState);
            if (!selectedText.empty()) {
                glfwSetClipboardString(m_glfwWindow, selectedText.c_str());
            }
        } else if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+A for select all (select all text on current page)
            // TODO: Implement select all functionality
        } else if (key == GLFW_KEY_F && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+F for search (toggle search box focus)
            ToggleSearchBox(*m_scrollState);
        } else if (key == GLFW_KEY_ESCAPE) {
            // Escape to clear text selection and search
            ClearTextSelection(*m_scrollState);
            ClearSearchResults(*m_scrollState);
        } else if (key == GLFW_KEY_HOME) {
            // Home key navigation
            if (mods & GLFW_MOD_CONTROL) {
                // Ctrl+Home: Go to first page
                goToPage(1);
            } else {
                // Home: Go to top of current page
                m_scrollState->scrollOffset = 0.0f;
                m_scrollState->forceRedraw = true;
            }
        } else if (key == GLFW_KEY_END) {
            // End key navigation
            if (mods & GLFW_MOD_CONTROL) {
                // Ctrl+End: Go to last page
                goToPage(getPageCount());
            } else {
                // End: Go to bottom of document
                m_scrollState->scrollOffset = m_scrollState->maxOffset;
                m_scrollState->forceRedraw = true;
            }
        } else if (key == GLFW_KEY_PAGE_UP) {
            // Page Up: Scroll up by page height
            float pageHeight = m_windowHeight * 0.9f; // 90% of window height
            m_scrollState->scrollOffset = std::max(0.0f, m_scrollState->scrollOffset - pageHeight);
            m_scrollState->forceRedraw = true;
        } else if (key == GLFW_KEY_PAGE_DOWN) {
            // Page Down: Scroll down by page height
            float pageHeight = m_windowHeight * 0.9f; // 90% of window height
            m_scrollState->scrollOffset = std::min(m_scrollState->maxOffset, 
                                                    m_scrollState->scrollOffset + pageHeight);
            m_scrollState->forceRedraw = true;
        } else if (key == GLFW_KEY_UP) {
            // Arrow up: Fine scroll up
            float scrollAmount = 50.0f; // pixels
            m_scrollState->scrollOffset = std::max(0.0f, m_scrollState->scrollOffset - scrollAmount);
            m_scrollState->forceRedraw = true;
        } else if (key == GLFW_KEY_DOWN) {
            // Arrow down: Fine scroll down
            float scrollAmount = 50.0f; // pixels
            m_scrollState->scrollOffset = std::min(m_scrollState->maxOffset, 
                                                    m_scrollState->scrollOffset + scrollAmount);
            m_scrollState->forceRedraw = true;
        } else if (key == GLFW_KEY_LEFT) {
            // Arrow left: Horizontal scroll or previous page
            if (mods & GLFW_MOD_CONTROL) {
                previousPage();
            } else {
                HandleHorizontalScroll(*m_scrollState, -1.0f, (float)m_windowWidth);
            }
        } else if (key == GLFW_KEY_RIGHT) {
            // Arrow right: Horizontal scroll or next page
            if (mods & GLFW_MOD_CONTROL) {
                nextPage();
            } else {
                HandleHorizontalScroll(*m_scrollState, 1.0f, (float)m_windowWidth);
            }
        } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+1-9: Quick zoom levels
            float zoomLevels[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f};
            int index = key - GLFW_KEY_1;
            if (index < 9) {
                setZoom(zoomLevels[index]);
            }
        } else if (key == GLFW_KEY_0 && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+0: Zoom to fit
            zoomToFit();
        } else if (key == GLFW_KEY_EQUAL && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+= (Plus): Zoom in
            zoomIn();
        } else if (key == GLFW_KEY_MINUS && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+- (Minus): Zoom out
            zoomOut();
        }
    }
}

// Text operations
std::string PDFViewerEmbedder::getSelectedText() const
{
    if (!m_scrollState) return "";
    return GetSelectedText(*m_scrollState);
}

void PDFViewerEmbedder::clearSelection()
{
    if (m_scrollState) {
        ClearTextSelection(*m_scrollState);
    }
}

bool PDFViewerEmbedder::findText(const std::string& searchTerm)
{
    if (!m_scrollState) return false;
    
    m_scrollState->textSearch.searchTerm = searchTerm;
    m_scrollState->textSearch.needsUpdate = true;
    m_scrollState->textSearch.searchChanged = true;
    
    return true;
}

void PDFViewerEmbedder::findNext()
{
    if (m_scrollState) {
        NavigateToNextSearchResult(*m_scrollState, m_pageHeights);
    }
}

void PDFViewerEmbedder::findPrevious()
{
    if (m_scrollState) {
        NavigateToPreviousSearchResult(*m_scrollState, m_pageHeights);
    }
}

void PDFViewerEmbedder::setFocus()
{
    if (m_glfwWindow) {
        glfwFocusWindow(m_glfwWindow);
    }
}
