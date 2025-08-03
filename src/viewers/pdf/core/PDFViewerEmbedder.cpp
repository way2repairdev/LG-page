#include "viewers/pdf/PDFViewerEmbedder.h"
#include "viewers/pdf/OpenGLPipelineManager.h"

// Include your existing PDF viewer components
#include "../third_party/include/rendering/pdf-render.h"
#include "../third_party/include/core/feature.h"
#include "../third_party/include/ui/menu-integration.h"
#include "../third_party/include/utils/Resource.h"

// GLFW includes
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GL/glew.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <ctime>

// Prevent Windows min/max macros from conflicting with std::min/max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

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
    std::cout << "PDFViewerEmbedder::initialize() called - parent: " << parentHwnd << ", size: " << width << "x" << height << std::endl;
    
    if (m_initialized) {
        std::cout << "PDFViewerEmbedder: Already initialized, returning true" << std::endl;
        return true; // Already initialized - return success, not failure
    }

    if (!parentHwnd || !IsWindow(parentHwnd)) {
        std::cerr << "PDFViewerEmbedder: Invalid parent window handle!" << std::endl;
        return false;
    }

    m_parentHwnd = parentHwnd;
    m_windowWidth = width;
    m_windowHeight = height;

    std::cout << "PDFViewerEmbedder: Checking GLFW initialization..." << std::endl;
    // Initialize GLFW if not already done - use static counter to track initialization
    static int glfwInitCount = 0;
    static bool glfwInitialized = false;
    
    if (!glfwInitialized) {
        if (!glfwInit()) {
            const char* description;
            int error = glfwGetError(&description);
            std::cerr << "PDFViewerEmbedder: Failed to initialize GLFW. Error: " << error << " - " << (description ? description : "No description") << std::endl;
            return false;
        }
        glfwInitialized = true;
        std::cout << "PDFViewerEmbedder: GLFW initialized for first time" << std::endl;
    } else {
        std::cout << "PDFViewerEmbedder: GLFW already initialized, reusing" << std::endl;
    }
    glfwInitCount++;
    std::cout << "PDFViewerEmbedder: GLFW instance count: " << glfwInitCount << std::endl;

    std::cout << "PDFViewerEmbedder: Creating embedded window..." << std::endl;
    // Create embedded OpenGL window
    if (!createEmbeddedWindow()) {
        std::cerr << "PDFViewerEmbedder: Failed to create embedded window" << std::endl;
        return false;
    }
    std::cout << "PDFViewerEmbedder: Embedded window created successfully" << std::endl;

    std::cout << "PDFViewerEmbedder: Initializing OpenGL..." << std::endl;
    // Initialize OpenGL
    if (!initializeOpenGL()) {
        std::cerr << "PDFViewerEmbedder: Failed to initialize OpenGL" << std::endl;
        return false;
    }
    std::cout << "PDFViewerEmbedder: OpenGL initialized successfully" << std::endl;

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
            
            // DEBUG: Print actual PDF page dimensions to debug file (in build folder)
            std::ofstream debugFile("build/pdf_embedder_debug.txt", std::ios::app);
            if (debugFile.is_open()) {
                debugFile << "DEBUG: Page " << i << " original dimensions: " 
                          << m_originalPageWidths[i] << " x " << m_originalPageHeights[i] << " points" << std::endl;
                
                // Calculate ratio to window size
                float widthRatio = m_originalPageWidths[i] / (float)m_windowWidth;
                float heightRatio = m_originalPageHeights[i] / (float)m_windowHeight;
                debugFile << "Page width ratio to window: " << widthRatio << std::endl;
                debugFile << "Page height ratio to window: " << heightRatio << std::endl;
                
                // Check if page appears to be auto-fitted
                if (std::abs(widthRatio - 1.0f) < 0.1f || std::abs(heightRatio - 1.0f) < 0.1f) {
                    debugFile << "WARNING: Page appears to be auto-fitted to window!" << std::endl;
                }
                
                debugFile.close();
            }
            
            // Also print to console
            std::cout << "DEBUG: Page " << i << " original dimensions: " 
                      << m_originalPageWidths[i] << " x " << m_originalPageHeights[i] << " points" << std::endl;
            
            // Use ACTUAL page dimensions (not window-fitted) for initial display
            m_pageWidths[i] = static_cast<int>(m_originalPageWidths[i]);
            m_pageHeights[i] = static_cast<int>(m_originalPageHeights[i]);
            
            // DEBUG: Print what we're storing as base dimensions
            std::cout << "DEBUG: Storing base dimensions: " 
                      << m_pageWidths[i] << " x " << m_pageHeights[i] << " pixels" << std::endl;
            
        } catch (const std::exception& e) {
            return false;
        }
    }

    // CRITICAL: Initialize scroll state with proper page arrays (missing from original implementation)
    m_scrollState->pageHeights = &m_pageHeights;
    m_scrollState->pageWidths = &m_pageWidths;
    m_scrollState->originalPageWidths = &m_originalPageWidths;
    m_scrollState->originalPageHeights = &m_originalPageHeights;
    
    std::cout << "PDFViewerEmbedder: Initializing with " << pageCount << " pages" << std::endl;
    std::cout << "PDFViewerEmbedder: Original page dimensions: " << m_originalPageWidths[0] << "x" << m_originalPageHeights[0] << std::endl;
    
    // Initialize text extraction and search capabilities (missing from original implementation)
    try {
        InitializeTextExtraction(*m_scrollState, pageCount);
        std::cout << "PDFViewerEmbedder: Text extraction initialized" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "PDFViewerEmbedder: Failed to initialize text extraction: " << e.what() << std::endl;
        return false;
    }
    
    // Initialize text search capabilities
    try {
        InitializeTextSearch(*m_scrollState);
        std::cout << "PDFViewerEmbedder: Text search initialized" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "PDFViewerEmbedder: Failed to initialize text search: " << e.what() << std::endl;
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
                std::cout << "PDFViewerEmbedder: Loaded text page " << i << std::endl;
            } else {
                std::cerr << "PDFViewerEmbedder: Failed to load page " << i << " for text extraction" << std::endl;
            }
        }
        std::cout << "PDFViewerEmbedder: Text pages loaded for " << pageCount << " pages" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "PDFViewerEmbedder: Failed to load text pages: " << e.what() << std::endl;
        return false;
    }

    // Initialize scroll state for the new document
    try {
        // CRITICAL DEBUGGING: Check initial zoom and page dimensions
        std::cout << "=== CRITICAL DEBUG: ZOOM INITIALIZATION ===" << std::endl;
        std::cout << "Initial zoom scale: " << m_scrollState->zoomScale << std::endl;
        std::cout << "Window dimensions: " << m_windowWidth << " x " << m_windowHeight << std::endl;
        
        // Calculate what the page will look like at current zoom
        if (!m_pageWidths.empty() && !m_pageHeights.empty()) {
            float pageDisplayWidth = m_pageWidths[0] * m_scrollState->zoomScale;
            float pageDisplayHeight = m_pageHeights[0] * m_scrollState->zoomScale;
            std::cout << "Page 0 will display at: " << pageDisplayWidth << " x " << pageDisplayHeight << " pixels" << std::endl;
            std::cout << "Page width ratio to window: " << (pageDisplayWidth / m_windowWidth) << std::endl;
            std::cout << "Page height ratio to window: " << (pageDisplayHeight / m_windowHeight) << std::endl;
            
            // Check if the page appears to be auto-fitted
            float widthRatio = pageDisplayWidth / m_windowWidth;
            float heightRatio = pageDisplayHeight / m_windowHeight;
            if (widthRatio > 0.8f && widthRatio < 1.2f && heightRatio > 0.8f && heightRatio < 1.2f) {
                std::cout << "WARNING: Page appears to be auto-fitted to window!" << std::endl;
            }
        }
        std::cout << "================================================" << std::endl;
        
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

    // SAME ZOOM HANDLING LOGIC AS STANDALONE VIEWER'S MAIN LOOP
    // Check if we need to regenerate textures due to window resize or zoom change
    bool needsFullRegeneration = (currentWidth != m_lastWinWidth || currentHeight != m_lastWinHeight);
    bool needsVisibleRegeneration = false;
    
    if (m_scrollState->zoomChanged) {
        float zoomDifference = std::abs(m_scrollState->zoomScale - m_scrollState->lastRenderedZoom) / m_scrollState->lastRenderedZoom;
        
        // For immediate responsive zoom, regenerate visible pages with lower threshold (1%)
        if (m_scrollState->immediateRenderRequired && zoomDifference > 0.01f) {
            needsVisibleRegeneration = true;
            m_scrollState->immediateRenderRequired = false;
        }
        // For full regeneration, use higher threshold (3%) to avoid too frequent full regens
        else if (zoomDifference > 0.03f) {
            needsFullRegeneration = true;
            m_scrollState->lastRenderedZoom = m_scrollState->zoomScale;
        }
        m_scrollState->zoomChanged = false; // Reset the flag
    }
    
    // Handle texture regeneration using the same logic as standalone viewer
    if (needsFullRegeneration) {
        regenerateTextures();
        m_lastWinWidth = currentWidth;
        m_lastWinHeight = currentHeight;
    } else if (needsVisibleRegeneration) {
        regenerateVisibleTextures();
    }

    // IMPORTANT: Update search state and trigger search if needed
    // This ensures that search highlighting works properly
    if (m_scrollState->textSearch.needsUpdate && !m_scrollState->textSearch.searchTerm.empty()) {
        PerformTextSearch(*m_scrollState, m_pageHeights, m_pageWidths);
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
    m_windowWidth = width;
    m_windowHeight = height;

    if (m_glfwWindow) {
        // Resize the GLFW window
        glfwSetWindowSize(m_glfwWindow, width, height);
        
        // Update viewport
        glfwMakeContextCurrent(m_glfwWindow);
        glViewport(0, 0, width, height);
        
#ifdef _WIN32
        // Update Windows position for embedded window
        if (m_childHwnd && m_parentHwnd) {
            SetWindowPos(static_cast<HWND>(m_childHwnd), nullptr, 
                        0, 0, width, height, 
                        SWP_NOZORDER | SWP_NOACTIVATE);
        }
#endif
    }

    // Update scroll state with new viewport dimensions
    if (m_scrollState && m_pdfLoaded) {
        UpdateScrollState(*m_scrollState, (float)height, m_pageHeights);
    }

    // Force texture regeneration for the new size
    m_needsFullRegeneration = true;
    
    std::cout << "PDFViewerEmbedder: Resized to " << width << "x" << height << std::endl;
}

void PDFViewerEmbedder::shutdown()
{
    if (!m_initialized) {
        return;
    }

    std::cout << "PDFViewerEmbedder: Starting shutdown..." << std::endl;

    // Clean up textures first
    cleanupTextures();
    
    // Clean up GLFW window
    if (m_glfwWindow) {
        std::cout << "PDFViewerEmbedder: Destroying GLFW window..." << std::endl;
        glfwDestroyWindow(m_glfwWindow);
        m_glfwWindow = nullptr;
        m_childHwnd = nullptr;
    }

    // Reset state
    if (m_renderer) {
        std::cout << "PDFViewerEmbedder: Cleaning up renderer..." << std::endl;
        m_renderer.reset();
    }
    
    if (m_scrollState) {
        std::cout << "PDFViewerEmbedder: Cleaning up scroll state..." << std::endl;
        m_scrollState.reset();
    }
    
    if (m_menuIntegration) {
        std::cout << "PDFViewerEmbedder: Cleaning up menu integration..." << std::endl;
        m_menuIntegration.reset();
    }
    
    if (m_pipelineManager) {
        std::cout << "PDFViewerEmbedder: Cleaning up pipeline manager..." << std::endl;
        m_pipelineManager.reset();
    }
    
    m_initialized = false;
    m_pdfLoaded = false;
    
    // Update static counter for GLFW cleanup tracking
    static int glfwInitCount = 0;
    if (glfwInitCount > 0) {
        glfwInitCount--;
        std::cout << "PDFViewerEmbedder: GLFW instance count after cleanup: " << glfwInitCount << std::endl;
    }
    
    std::cout << "PDFViewerEmbedder: Shutdown complete" << std::endl;
}

// Navigation methods
void PDFViewerEmbedder::zoomIn()
{
    if (!m_scrollState) return;
    
    float oldZoom = m_scrollState->zoomScale;
    
    // Write zoom debug to file
    std::ofstream debugFile("pdf_embedder_debug.txt", std::ios::app);
    if (debugFile.is_open()) {
        debugFile << "ZOOM DEBUG: ZoomIn called - Current zoom: " << oldZoom << std::endl;
        debugFile.close();
    }
    
    std::cout << "DEBUG: ZoomIn called - Current zoom: " << oldZoom << std::endl;
    
    // Use the SAME cursor-based zoom as standalone viewer's HandleZoom function
    // Get center of viewport as zoom focal point
    float centerX = m_windowWidth / 2.0f;
    float centerY = m_windowHeight / 2.0f;
    
    // Call HandleZoom with 1.2f zoom delta (same as MenuIntegration)
    HandleZoom(*m_scrollState, 1.2f, centerX, centerY, 
               (float)m_windowWidth, (float)m_windowHeight, 
               m_pageHeights, m_pageWidths);
    
    float newZoom = m_scrollState->zoomScale;
    
    // Write zoom result to debug file
    std::ofstream debugFile2("build/pdf_embedder_debug.txt", std::ios::app);
    if (debugFile2.is_open()) {
        debugFile2 << "ZOOM DEBUG: ZoomIn completed - New zoom: " << newZoom << " (delta: " << (newZoom/oldZoom) << ")" << std::endl;
        debugFile2 << "Page 0 pixel dimensions after zoom: " << (m_pageWidths[0] * newZoom) << " x " << (m_pageHeights[0] * newZoom) << " pixels" << std::endl;
        debugFile2.close();
    }
    
    std::cout << "DEBUG: ZoomIn completed - New zoom: " << newZoom << " (delta: " << (newZoom/oldZoom) << ")" << std::endl;
    std::cout << "DEBUG: Page 0 will render at: " << (m_pageWidths[0] * newZoom) << " x " << (m_pageHeights[0] * newZoom) << " pixels" << std::endl;
    std::cout << "Embedded viewer: HandleZoom zoom in to " << m_scrollState->zoomScale << std::endl;
}

void PDFViewerEmbedder::zoomOut()
{
    if (!m_scrollState) return;
    
    float oldZoom = m_scrollState->zoomScale;
    
    // Write zoom debug to file
    std::ofstream debugFile("pdf_embedder_debug.txt", std::ios::app);
    if (debugFile.is_open()) {
        debugFile << "ZOOM DEBUG: ZoomOut called - Current zoom: " << oldZoom << std::endl;
        debugFile.close();
    }
    
    std::cout << "DEBUG: ZoomOut called - Current zoom: " << oldZoom << std::endl;
    
    // Use the SAME cursor-based zoom as standalone viewer's HandleZoom function
    // Get center of viewport as zoom focal point
    float centerX = m_windowWidth / 2.0f;
    float centerY = m_windowHeight / 2.0f;
    
    // Call HandleZoom with 1/1.2f zoom delta (same as MenuIntegration)
    HandleZoom(*m_scrollState, 1.0f/1.2f, centerX, centerY, 
               (float)m_windowWidth, (float)m_windowHeight, 
               m_pageHeights, m_pageWidths);
    
    float newZoom = m_scrollState->zoomScale;
    
    // Write zoom result to debug file
    std::ofstream debugFile2("pdf_embedder_debug.txt", std::ios::app);
    if (debugFile2.is_open()) {
        debugFile2 << "ZOOM DEBUG: ZoomOut completed - New zoom: " << newZoom << " (delta: " << (newZoom/oldZoom) << ")" << std::endl;
        debugFile2 << "Page 0 pixel dimensions after zoom: " << (m_pageWidths[0] * newZoom) << " x " << (m_pageHeights[0] * newZoom) << " pixels" << std::endl;
        debugFile2.close();
    }
    
    std::cout << "DEBUG: ZoomOut completed - New zoom: " << newZoom << " (delta: " << (newZoom/oldZoom) << ")" << std::endl;
    std::cout << "DEBUG: Page 0 will render at: " << (m_pageWidths[0] * newZoom) << " x " << (m_pageHeights[0] * newZoom) << " pixels" << std::endl;
    std::cout << "Embedded viewer: HandleZoom zoom out to " << m_scrollState->zoomScale << std::endl;
}

void PDFViewerEmbedder::setZoom(float zoomLevel)
{
    if (!m_scrollState) return;
    
    // Apply SAME zoom limits as HandleZoom function (0.35f to 5.0f)
    if (zoomLevel < 0.35f) zoomLevel = 0.35f;
    if (zoomLevel > 5.0f) zoomLevel = 5.0f;
    
    // Calculate zoom delta to reach target zoom level
    float currentZoom = m_scrollState->zoomScale;
    if (std::abs(currentZoom - zoomLevel) < 0.001f) return; // Already at target zoom
    
    float zoomDelta = zoomLevel / currentZoom;
    
    // Use center-based zoom like HandleZoom function
    float centerX = m_windowWidth / 2.0f;
    float centerY = m_windowHeight / 2.0f;
    
    // Call HandleZoom with calculated delta
    HandleZoom(*m_scrollState, zoomDelta, centerX, centerY, 
               (float)m_windowWidth, (float)m_windowHeight, 
               m_pageHeights, m_pageWidths);
    
    std::cout << "Embedded viewer: Set zoom to " << m_scrollState->zoomScale << std::endl;
}

void PDFViewerEmbedder::goToPage(int pageNumber)
{
    
    
    
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
 
}

float PDFViewerEmbedder::getCurrentZoom() const
{

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
        // Save current OpenGL state
        glPushMatrix();
        
        // Switch to normalized coordinates (-1 to 1) for text selection drawing
        // This matches what the standalone viewer uses
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        // Debug: Check if we have an active text selection
        if (m_scrollState->textSelection.isActive) {
            std::cout << "PDFViewerEmbedder: Drawing text selection - startChar=" << m_scrollState->textSelection.startCharIndex 
                      << ", endChar=" << m_scrollState->textSelection.endCharIndex << std::endl;
        }
        
        DrawTextSelection(*m_scrollState, m_pageHeights, m_pageWidths, (float)m_windowWidth, (float)m_windowHeight);
        
        // Draw search results highlighting
        DrawSearchResultsHighlighting(*m_scrollState, m_pageHeights, m_pageWidths, (float)m_windowWidth, (float)m_windowHeight);
        
        // Draw scroll bar overlay
        DrawScrollBar(*m_scrollState);
        
        // Restore OpenGL state
        glPopMatrix();
        
        // Restore the screen coordinate system for future rendering
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0f, (float)m_windowWidth, (float)m_windowHeight, 0.0f, -1.0f, 1.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
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
    
    // Regenerate all textures using ACTUAL page dimensions (not window-fitted)
    for (int i = 0; i < pageCount; ++i) {
        // Get the ACTUAL page dimensions from PDF (not window-fitted)
        double originalPageWidth, originalPageHeight;
        m_renderer->GetOriginalPageSize(i, originalPageWidth, originalPageHeight);
        
        // Calculate texture size based on zoom level applied to ACTUAL page dimensions
        // FIXED: Apply texture size limits to prevent blank screen at high zoom levels
        float effectiveZoom = (m_scrollState->zoomScale > 0.5f) ? m_scrollState->zoomScale : 0.5f;
        
        // Apply maximum texture zoom to prevent exceeding OpenGL limits
        // From pipeline debug, max texture size is 16384 - stay well below this
        effectiveZoom = std::min(effectiveZoom, 3.0f); // Limit texture resolution
        
        int textureWidth = static_cast<int>(originalPageWidth * effectiveZoom);
        int textureHeight = static_cast<int>(originalPageHeight * effectiveZoom);
        
        // Additional safety check: ensure we don't exceed reasonable texture sizes
        const int MAX_TEXTURE_DIM = 8192; // Conservative limit for older GPUs
        if (textureWidth > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureWidth;
            textureWidth = MAX_TEXTURE_DIM;
            textureHeight = static_cast<int>(textureHeight * scale);
        }
        if (textureHeight > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureHeight;
            textureHeight = MAX_TEXTURE_DIM;
            textureWidth = static_cast<int>(textureWidth * scale);
        }
        
        // Ensure minimum texture size for readability
        if (textureWidth < 200) textureWidth = 200;
        if (textureHeight < 200) textureHeight = 200;
        
        // Debug output for high zoom levels
        if (m_scrollState->zoomScale > 3.0f) {
            std::cout << "HIGH ZOOM DEBUG (Full): ZoomScale=" << m_scrollState->zoomScale 
                      << ", EffectiveZoom=" << effectiveZoom 
                      << ", TextureSize=" << textureWidth << "x" << textureHeight 
                      << ", OriginalPage=" << originalPageWidth << "x" << originalPageHeight << std::endl;
        }
        
        // Render at calculated size using actual page dimensions (no window fitting)
        FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(i, textureWidth, textureHeight);
        m_textures[i] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
        
        // Store BASE dimensions (ACTUAL page dimensions, not window-fitted)
        // These will be scaled by zoomScale during rendering
        m_pageWidths[i] = static_cast<int>(originalPageWidth);
        m_pageHeights[i] = static_cast<int>(originalPageHeight);
        
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
    
    // Only regenerate textures for visible pages using ACTUAL page dimensions
    for (int i = firstVisible; i <= lastVisible && i < pageCount; ++i) {
        if (m_textures[i]) {
            glDeleteTextures(1, &m_textures[i]);
        }
        
        // Get the ACTUAL page dimensions from PDF (not window-fitted)
        double originalPageWidth, originalPageHeight;
        m_renderer->GetOriginalPageSize(i, originalPageWidth, originalPageHeight);
        
        // Calculate texture size based on zoom level applied to ACTUAL page dimensions
        // FIXED: Apply texture size limits to prevent blank screen at high zoom levels
        float effectiveZoom = (m_scrollState->zoomScale > 0.5f) ? m_scrollState->zoomScale : 0.5f;
        
        // Apply maximum texture zoom to prevent exceeding OpenGL limits
        // From pipeline debug, max texture size is 16384 - stay well below this
        effectiveZoom = std::min(effectiveZoom, 3.0f); // Limit texture resolution
        
        int textureWidth = static_cast<int>(originalPageWidth * effectiveZoom);
        int textureHeight = static_cast<int>(originalPageHeight * effectiveZoom);
        
        // Additional safety check: ensure we don't exceed reasonable texture sizes
        const int MAX_TEXTURE_DIM = 8192; // Conservative limit for older GPUs
        if (textureWidth > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureWidth;
            textureWidth = MAX_TEXTURE_DIM;
            textureHeight = static_cast<int>(textureHeight * scale);
        }
        if (textureHeight > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureHeight;
            textureHeight = MAX_TEXTURE_DIM;
            textureWidth = static_cast<int>(textureWidth * scale);
        }
        
        // Ensure minimum texture size for readability
        if (textureWidth < 200) textureWidth = 200;
        if (textureHeight < 200) textureHeight = 200;
        
        // Debug output for high zoom levels
        if (m_scrollState->zoomScale > 3.0f) {
            std::cout << "HIGH ZOOM DEBUG (Visible): ZoomScale=" << m_scrollState->zoomScale 
                      << ", EffectiveZoom=" << effectiveZoom 
                      << ", TextureSize=" << textureWidth << "x" << textureHeight 
                      << ", OriginalPage=" << originalPageWidth << "x" << originalPageHeight << std::endl;
        }
        if (textureWidth < 200) textureWidth = 200;
        if (textureHeight < 200) textureHeight = 200;
        
        // Render at calculated size using actual page dimensions (no window fitting)
        FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(i, textureWidth, textureHeight);
        m_textures[i] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
        
        // Store BASE dimensions (ACTUAL page dimensions, not window-fitted)
        // These will be scaled by zoomScale during rendering
        m_pageWidths[i] = static_cast<int>(originalPageWidth);
        m_pageHeights[i] = static_cast<int>(originalPageHeight);
        
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
    
    // Generate new texture using ACTUAL page dimensions (not window-fitted)
    
    // Get the ACTUAL page dimensions from PDF (not window-fitted)
    double originalPageWidth, originalPageHeight;
    m_renderer->GetOriginalPageSize(pageIndex, originalPageWidth, originalPageHeight);
    
    // Calculate texture size based on zoom level applied to ACTUAL page dimensions
    // FIXED: Apply texture size limits to prevent blank screen at high zoom levels
    float effectiveZoom = (m_scrollState->zoomScale > 0.5f) ? m_scrollState->zoomScale : 0.5f;
    
    // Apply maximum texture zoom to prevent exceeding OpenGL limits
    // From pipeline debug, max texture size is 16384 - stay well below this
    effectiveZoom = std::min(effectiveZoom, 3.0f); // Limit texture resolution
    
    int textureWidth = static_cast<int>(originalPageWidth * effectiveZoom);
    int textureHeight = static_cast<int>(originalPageHeight * effectiveZoom);
    
    // Additional safety check: ensure we don't exceed reasonable texture sizes
    const int MAX_TEXTURE_DIM = 8192; // Conservative limit for older GPUs
    if (textureWidth > MAX_TEXTURE_DIM) {
        float scale = (float)MAX_TEXTURE_DIM / textureWidth;
        textureWidth = MAX_TEXTURE_DIM;
        textureHeight = static_cast<int>(textureHeight * scale);
    }
    if (textureHeight > MAX_TEXTURE_DIM) {
        float scale = (float)MAX_TEXTURE_DIM / textureHeight;
        textureHeight = MAX_TEXTURE_DIM;
        textureWidth = static_cast<int>(textureWidth * scale);
    }
    
    // Ensure minimum texture size for readability
    if (textureWidth < 200) textureWidth = 200;
    if (textureHeight < 200) textureHeight = 200;
    
    // Debug output for high zoom levels
    if (m_scrollState->zoomScale > 3.0f) {
        std::cout << "HIGH ZOOM DEBUG (Single): Page=" << pageIndex 
                  << ", ZoomScale=" << m_scrollState->zoomScale 
                  << ", EffectiveZoom=" << effectiveZoom 
                  << ", TextureSize=" << textureWidth << "x" << textureHeight 
                  << ", OriginalPage=" << originalPageWidth << "x" << originalPageHeight << std::endl;
    }
    
    // Render at calculated size using actual page dimensions (no window fitting)
    FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(pageIndex, textureWidth, textureHeight);
    m_textures[pageIndex] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
    
    // Store BASE dimensions (ACTUAL page dimensions, not window-fitted)
    // These will be scaled by zoomScale during rendering
    m_pageWidths[pageIndex] = static_cast<int>(originalPageWidth);
    m_pageHeights[pageIndex] = static_cast<int>(originalPageHeight);
    
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
            
            // Get ACTUAL page dimensions for background rendering (not window-fitted)
            double originalPageWidth, originalPageHeight;
            m_renderer->GetOriginalPageSize(backgroundRenderIndex, originalPageWidth, originalPageHeight);
            
            // Calculate background texture size using actual page dimensions with zoom scaling
            // FIXED: Apply texture size limits to prevent issues at high zoom levels
            float backgroundZoom = m_scrollState->zoomScale * 0.7f;
            backgroundZoom = (backgroundZoom > 0.3f) ? backgroundZoom : 0.3f;
            // Apply same texture limit for background rendering
            backgroundZoom = std::min(backgroundZoom, 2.0f); // Even more conservative for background
            
            int textureWidth = static_cast<int>(originalPageWidth * backgroundZoom);
            int textureHeight = static_cast<int>(originalPageHeight * backgroundZoom);
            
            // Apply texture size limits for background rendering too
            const int MAX_BACKGROUND_DIM = 4096; // Even more conservative for background
            if (textureWidth > MAX_BACKGROUND_DIM) {
                float scale = (float)MAX_BACKGROUND_DIM / textureWidth;
                textureWidth = MAX_BACKGROUND_DIM;
                textureHeight = static_cast<int>(textureHeight * scale);
            }
            if (textureHeight > MAX_BACKGROUND_DIM) {
                float scale = (float)MAX_BACKGROUND_DIM / textureHeight;
                textureHeight = MAX_BACKGROUND_DIM;
                textureWidth = static_cast<int>(textureWidth * scale);
            }
            
            FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(backgroundRenderIndex, textureWidth, textureHeight);
            m_textures[backgroundRenderIndex] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
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

// Texture optimization method for smooth zoom transitions
float PDFViewerEmbedder::getOptimalTextureZoom(float currentZoom) const
{
    // FIXED: Apply texture zoom limits that match our texture generation
    // This prevents coordinate misplacement and texture size issues
    return std::clamp(currentZoom, 0.2f, 3.0f); // Match the 3.0f limit from texture generation
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
        std::cout << "PDFViewerEmbedder: Updating text selection at (" << xpos << ", " << ypos << ")" << std::endl;
        UpdateTextSelection(*m_scrollState, xpos, ypos, (float)m_windowWidth, (float)m_windowHeight, 
                            m_pageHeights, m_pageWidths);
    }
    
    if (m_scrollState->isPanning) {
        UpdatePanning(*m_scrollState, xpos, ypos, (float)m_windowWidth, (float)m_windowHeight, m_pageHeights);
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
                    std::cout << "PDFViewerEmbedder: Starting text selection at (" << mouseX << ", " << mouseY << ")" << std::endl;
                    StartTextSelection(*m_scrollState, mouseX, mouseY, (float)m_windowWidth, (float)m_windowHeight, 
                                       m_pageHeights, m_pageWidths);
                }
            }
        } else if (action == GLFW_RELEASE) {
            // Stop scroll bar dragging
            StopScrollBarDragging(*m_scrollState);
            
            // End text selection (but not for double-click word selection)
            if (!m_scrollState->textSelection.isDoubleClick) {
                std::cout << "PDFViewerEmbedder: Ending text selection" << std::endl;
                EndTextSelection(*m_scrollState);
                
                // IMPORTANT: Trigger the search immediately after text selection
                // This ensures that the selected text gets highlighted in yellow
                if (m_scrollState->textSearch.needsUpdate) {
                    std::cout << "PDFViewerEmbedder: Triggering search for selected text: '" << m_scrollState->textSearch.searchTerm << "'" << std::endl;
                    PerformTextSearch(*m_scrollState, m_pageHeights, m_pageWidths);
                }
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
    
    // Get cursor position
    double cursorX = m_scrollState->lastCursorX;
    double cursorY = m_scrollState->lastCursorY;
    
    // Check if cursor is over the scroll bar area (right side of window)
    float barMargin = 0.01f * m_windowWidth;
    float barWidth = 0.025f * m_windowWidth;
    float barX = m_windowWidth - barMargin - barWidth;
    
    // If cursor is over scroll bar area, handle scrolling
    if (cursorX >= barX) {
        // Handle scroll bar scrolling using existing logic
        HandleScroll(*m_scrollState, (float)yoffset);
        return;
    }
    
    // Otherwise, handle cursor-based zooming with mouse wheel
    if (std::abs(yoffset) > 0.01) {
        float zoomDelta = (yoffset > 0) ? 1.1f : 1.0f / 1.1f;
        
        // CRITICAL DEBUG: Log detailed state before HandleZoom
        std::ofstream debugFile("build/zoom_debug.txt", std::ios::app);
        int firstVisible = -1, lastVisible = -1;
        if (debugFile.is_open()) {
            debugFile << "=== ZOOM DEBUG - BEFORE HandleZoom ===" << std::endl;
            debugFile << "Cursor position: (" << cursorX << ", " << cursorY << ")" << std::endl;
            debugFile << "Window size: " << m_windowWidth << "x" << m_windowHeight << std::endl;
            debugFile << "Current zoom: " << m_scrollState->zoomScale << std::endl;
            debugFile << "Current scrollOffset: " << m_scrollState->scrollOffset << std::endl;
            debugFile << "Current horizontalOffset: " << m_scrollState->horizontalOffset << std::endl;
            debugFile << "Zoom delta: " << zoomDelta << std::endl;
            debugFile << "Page count: " << (m_pageHeights.size()) << std::endl;
            
            // Log current visible page range
            GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
            debugFile << "Visible pages: " << firstVisible << " to " << lastVisible << std::endl;
            
            // SPECIAL DEBUG for last page issue
            bool isNearLastPage = (lastVisible >= (int)m_pageHeights.size() - 3);
            debugFile << "NEAR LAST PAGE: " << (isNearLastPage ? "YES" : "NO") << std::endl;
            
            // Log page heights for context (only log relevant pages to reduce spam)
            int startPage = std::max(0, firstVisible - 1);
            int endPage = std::min((int)m_pageHeights.size() - 1, lastVisible + 1);
            for (int i = startPage; i <= endPage; ++i) {
                debugFile << "Page " << i << " height: " << m_pageHeights[i] << " (scaled: " 
                          << (m_pageHeights[i] * m_scrollState->zoomScale) << ")" << std::endl;
            }
            debugFile << "===========================================" << std::endl;
        }
        
        // Use current cursor position as zoom focal point
        HandleZoom(*m_scrollState, zoomDelta, (float)cursorX, (float)cursorY, 
                   (float)m_windowWidth, (float)m_windowHeight, 
                   m_pageHeights, m_pageWidths);
        
        // CRITICAL DEBUG: Log detailed state after HandleZoom
        if (debugFile.is_open()) {
            debugFile << "=== ZOOM DEBUG - AFTER HandleZoom ===" << std::endl;
            debugFile << "New zoom: " << m_scrollState->zoomScale << std::endl;
            debugFile << "New scrollOffset: " << m_scrollState->scrollOffset << std::endl;
            debugFile << "New horizontalOffset: " << m_scrollState->horizontalOffset << std::endl;
            
            // Log new visible page range
            int newFirstVisible, newLastVisible;
            GetVisiblePageRange(*m_scrollState, m_pageHeights, newFirstVisible, newLastVisible);
            debugFile << "New visible pages: " << newFirstVisible << " to " << newLastVisible << std::endl;
            debugFile << "ZOOM JUMP DETECTED: " << ((newFirstVisible < firstVisible) ? "YES" : "NO") << std::endl;
            
            // Calculate how much scroll offset changed
            float scrollOffsetDelta = m_scrollState->scrollOffset - firstVisible;  // This should be the old scrollOffset
            debugFile << "Scroll offset change: " << scrollOffsetDelta << std::endl;
            
            // CRITICAL: Log if we jumped from near last page to earlier page
            bool wasNearLastPage = (lastVisible >= (int)m_pageHeights.size() - 3);
            bool isNowNearLastPage = (newLastVisible >= (int)m_pageHeights.size() - 3);
            if (wasNearLastPage && !isNowNearLastPage) {
                debugFile << "!!! PROBLEMATIC JUMP: Was on last pages, now on earlier pages !!!" << std::endl;
            }
            
            debugFile << "===========================================" << std::endl;
            debugFile.close();
        }
        
        std::cout << "PDFViewerEmbedder: Mouse wheel zoom at cursor (" << cursorX << ", " << cursorY 
                  << ") with delta " << zoomDelta << " to zoom " << m_scrollState->zoomScale << std::endl;
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

int PDFViewerEmbedder::countTextOccurrences(const std::string& searchTerm) const
{
    if (!m_scrollState || searchTerm.empty()) {
        return 0;
    }
    
    // Return the total number of search results found
    return static_cast<int>(m_scrollState->textSearch.results.size());
}

int PDFViewerEmbedder::getCurrentSearchResultIndex() const
{
    if (!m_scrollState || m_scrollState->textSearch.results.empty()) {
        return -1;
    }
    
    // Return the current search result index (0-based)
    return m_scrollState->textSearch.currentResultIndex;
}
