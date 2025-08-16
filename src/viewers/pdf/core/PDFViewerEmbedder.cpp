#include "viewers/pdf/PDFViewerEmbedder.h"
#include "viewers/pdf/OpenGLPipelineManager.h"

// Include your existing PDF viewer components
#include "../third_party/include/rendering/pdf-render.h"
#include "../third_party/include/core/feature.h"
#include "../third_party/include/ui/menu-integration.h"
#include "../third_party/include/utils/Resource.h"

// PDFium includes for rotation functions
#include "fpdf_edit.h"

// GLFW includes
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GL/glew.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <ctime>
#include <limits>

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


// Transient zoom gesture state (file-scope to avoid header changes)
static double s_lastWheelZoomTime = 0.0;        // Last time we received a wheel-zoom event
static bool   s_pendingSettledRegen = false;    // Whether a crisp regen should run after zoom settles


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
    static std::atomic<long long> s_counter{0};
    m_viewerId = ++s_counter;
    std::cout << "PDFViewerEmbedder["<<m_viewerId<<"] ctor" << std::endl;
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

    // FAST PATH: If this exact file is already loaded in this embedder, skip all work.
    // This must happen BEFORE attempting to open the file or re-run PDFium load.
    auto normalizePathForCompare = [](std::string p){
        std::replace(p.begin(), p.end(), '\\', '/');
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return p;
    };
    if (m_pdfLoaded) {
        std::string currentNorm = normalizePathForCompare(m_currentFilePath);
        std::string incomingNorm = normalizePathForCompare(filePath);
        if (currentNorm == incomingNorm) {
            if (m_scrollState) {
                m_scrollState->forceRedraw = true; // light redraw only
                m_scrollState->zoomChanged = false;
            }
            return true; // Already loaded, no reload
        }
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
    m_textureWidths.assign(pageCount, 0);
    m_textureHeights.assign(pageCount, 0);
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
        // CONSISTENT PDF OPENING: Set initial zoom to fit first page to viewer
        // This ensures all PDFs open with the same consistent view of the first page
        std::cout << "=== CONSISTENT PDF ZOOM INITIALIZATION ===" << std::endl;
        std::cout << "Window dimensions: " << m_windowWidth << " x " << m_windowHeight << std::endl;
        
        if (!m_pageWidths.empty() && !m_pageHeights.empty() && m_windowWidth > 0 && m_windowHeight > 0) {
            // Calculate zoom to fit first page within viewer with some padding
            float pageWidth = static_cast<float>(m_pageWidths[0]);
            float pageHeight = static_cast<float>(m_pageHeights[0]);
            
            // Add 5% padding on each side (so use 90% of available space)
            float availableWidth = m_windowWidth * 0.90f;
            float availableHeight = m_windowHeight * 0.90f;
            
            // Calculate zoom scales for width and height fitting
            float zoomForWidth = availableWidth / pageWidth;
            float zoomForHeight = availableHeight / pageHeight;
            
            // Use the smaller zoom to ensure the page fits completely
            float fitZoom = std::min(zoomForWidth, zoomForHeight);
            
            // Apply reasonable zoom bounds (don't go too small or too large)
            fitZoom = std::clamp(fitZoom, 0.35f, 15.0f);
            
            // Set the initial zoom to fit the first page
            m_scrollState->zoomScale = fitZoom;
            
            std::cout << "Page 0 original size: " << pageWidth << " x " << pageHeight << " pixels" << std::endl;
            std::cout << "Available display area: " << availableWidth << " x " << availableHeight << " pixels" << std::endl;
            std::cout << "Calculated fit zoom: " << fitZoom << std::endl;
            std::cout << "Page 0 will display at: " << (pageWidth * fitZoom) << " x " << (pageHeight * fitZoom) << " pixels" << std::endl;
        } else {
            std::cout << "Using default zoom scale: " << m_scrollState->zoomScale << std::endl;
        }
        std::cout << "================================================" << std::endl;
        
        UpdateScrollState(*m_scrollState, (float)m_windowHeight, m_pageHeights);
    } catch (const std::exception& e) {
        return false;
    }
    
    // Create async render queue now that a document is loaded
    m_asyncQueue = std::make_unique<AsyncRenderQueue>(m_renderer.get());

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
    if (!m_initialized || !m_pdfLoaded) return;

    // If globals point elsewhere (another active tab), skip heavy work to avoid race
    if (!(g_scrollState == m_scrollState.get() && g_renderer == m_renderer.get())) {
        // Try to detect if this viewer should become the active one. Conditions:
        // 1. No active global viewer (globals null)
        // 2. Our window currently has focus
        // 3. Our parent HWND is foreground window (tab switched at higher UI layer)
        bool claimGlobals = false;
        if (!g_scrollState && !g_renderer) {
            claimGlobals = true;
        }
#ifdef _WIN32
        else if (m_childHwnd && GetFocus() == m_childHwnd) {
            claimGlobals = true;
        } else if (m_parentHwnd && GetForegroundWindow() == m_parentHwnd) {
            claimGlobals = true;
        }
#endif
        if (claimGlobals) {
            g_scrollState = m_scrollState.get();
            g_renderer = m_renderer.get();
            g_pageHeights = &m_pageHeights;
            g_pageWidths = &m_pageWidths;
            // Decide what level of regeneration is needed
            bool anyTexture = false;
            for (auto tex : m_textures) { if (tex != 0) { anyTexture = true; break; } }
            if (!anyTexture) {
                // No textures yet: do a full regen pass
                m_needsFullRegeneration = true;
            } else {
                // Textures exist but may be stale vs current zoom: regen visible only
                m_needsVisibleRegeneration = true;
            }
            std::cout << "PDFViewerEmbedder["<<m_viewerId<<"] claimed global active viewer context" << std::endl;
        } else {
            // Not active: still allow lightweight rendering of existing textures so previously
            // generated content remains visible instead of a blank window. Skip regeneration.
            glfwMakeContextCurrent(m_glfwWindow);
            renderFrame();
            glfwSwapBuffers(m_glfwWindow);
            glfwPollEvents();
            return;
        }
    }

    // Make OpenGL context current
    glfwMakeContextCurrent(m_glfwWindow);

    // Update window dimensions
    int currentWidth, currentHeight;
    glfwGetFramebufferSize(m_glfwWindow, &currentWidth, &currentHeight);

    // Apply any pending horizontal centering request here, where window width is known
    if (m_scrollState && m_scrollState->pendingHorizCenter &&
        m_scrollState->pageWidths && m_scrollState->pendingHorizPage >= 0 &&
        m_scrollState->pendingHorizPage < (int)m_scrollState->pageWidths->size()) {
        int pageIndex = m_scrollState->pendingHorizPage;
        float pageWidthPx = (*m_scrollState->pageWidths)[pageIndex] * m_scrollState->zoomScale;
        float relX = std::clamp(m_scrollState->pendingHorizRelX, 0.0f, 1.0f);
        float selectionCenterXOnPage = relX * pageWidthPx;
        // Center selection in the viewport horizontally:
        // xCenter = (windowWidth/2) - horizontalOffset
        // pageLeftX = xCenter - pageWidth/2
        // We want: pageLeftX + selectionCenterXOnPage == windowWidth/2
        // Solve -> horizontalOffset = selectionCenterXOnPage - (pageWidth/2)
        float desired = selectionCenterXOnPage - (pageWidthPx * 0.5f);

        // Clamp using current bounds; recompute bounds for this window width
        float maxHoriz = 0.0f;
        float zoomedPageWidthMax = 0.0f;
        for (int w : *m_scrollState->pageWidths) {
            float pw = w * m_scrollState->zoomScale;
            if (pw > zoomedPageWidthMax) zoomedPageWidthMax = pw;
        }
        if (zoomedPageWidthMax > (float)currentWidth) {
            maxHoriz = (zoomedPageWidthMax - (float)currentWidth) * 0.5f;
        }
        if (maxHoriz > 0.0f) desired = std::clamp(desired, -maxHoriz, maxHoriz);
        m_scrollState->horizontalOffset = desired;

        // Clear pending flag
        m_scrollState->pendingHorizCenter = false;
        m_scrollState->pendingHorizPage = -1;
    }

    if (currentWidth != m_lastWinWidth || currentHeight != m_lastWinHeight) {
        m_needsFullRegeneration = true;
        m_windowWidth = currentWidth;
        m_windowHeight = currentHeight;
    }

    // SAME ZOOM HANDLING LOGIC AS STANDALONE VIEWER'S MAIN LOOP
    // Check if we need to regenerate textures due to window resize or zoom change
    bool needsFullRegeneration = (currentWidth != m_lastWinWidth || currentHeight != m_lastWinHeight) || m_needsFullRegeneration;
    bool needsVisibleRegeneration = m_needsVisibleRegeneration;

    if (m_scrollState->zoomChanged) {
        float zoomDifference = std::abs(m_scrollState->zoomScale - m_scrollState->lastRenderedZoom) / m_scrollState->lastRenderedZoom;
        
        // For immediate responsive zoom, regenerate visible pages with lower threshold (1%)
        if (m_scrollState->immediateRenderRequired && zoomDifference > 0.01f) {
            needsVisibleRegeneration = true;
            m_scrollState->immediateRenderRequired = false;
        }
        // For full regeneration, use higher threshold (3%) to avoid too frequent full regens
        else if (zoomDifference > 0.03f) {
            // Avoid heavy full regen while a zoom gesture is active; defer to after it settles
            double now = glfwGetTime();
            bool zoomGestureActive = (now - s_lastWheelZoomTime) < 0.20; // 200 ms grace window
            if (!zoomGestureActive) {
                needsFullRegeneration = true;
                m_scrollState->lastRenderedZoom = m_scrollState->zoomScale;
            } else {
                // Ensure we schedule a crisp visible regen once settled
                s_pendingSettledRegen = true;
            }
        }
        m_scrollState->zoomChanged = false; // Reset the flag
    }
    
    // Handle texture regeneration using the same logic as standalone viewer
    if (needsFullRegeneration) {
        regenerateTextures();
        m_lastWinWidth = currentWidth;
        m_lastWinHeight = currentHeight;
        m_needsFullRegeneration = false; // Reset the flag after regeneration
        // After full regen, also schedule async visible refresh for crispness
        scheduleVisibleRegeneration(true);
    } else if (needsVisibleRegeneration) {
        // Use async visible regeneration instead of blocking sync path
        scheduleVisibleRegeneration(false);
        m_needsVisibleRegeneration = false; // Reset the flag after scheduling
    }

    // If a zoom gesture just stopped, do one crisp settled regen for visible pages
    double now = glfwGetTime();
    if (s_pendingSettledRegen && (now - s_lastWheelZoomTime) > 0.12) {
        scheduleVisibleRegeneration(true);
        s_pendingSettledRegen = false;
    }

    // If navigation or other actions requested an immediate redraw, schedule a visible regeneration
    if (m_scrollState->forceRedraw) {
        bool highQuality = m_scrollState->requestHighQualityVisibleRegen;
        m_scrollState->forceRedraw = false;
        if (highQuality) {
            scheduleVisibleRegeneration(true); // settled quality
        } else {
            scheduleVisibleRegeneration(false);
        }
        m_scrollState->requestHighQualityVisibleRegen = false; // reset
    }

    // IMPORTANT: Update search state and trigger search if needed
    // This ensures that search highlighting works properly
    if (m_scrollState->textSearch.needsUpdate && !m_scrollState->textSearch.searchTerm.empty()) {
        PerformTextSearch(*m_scrollState, m_pageHeights, m_pageWidths);
    }

    // Drain async results and update textures before drawing
    processAsyncResults();

    // Render the frame
    renderFrame();
    
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
    if (m_scrollState && m_pdfLoaded) {
        UpdateScrollState(*m_scrollState, (float)height, m_pageHeights);

        // Re-center/clamp horizontal offset after width changes to avoid left/right gaps
        // Compute the maximum zoomed page width
        float zoom = m_scrollState->zoomScale;
        float zoomedPageWidthMax = 0.0f;
        for (size_t i = 0; i < m_pageWidths.size(); ++i) {
            float w = static_cast<float>(m_pageWidths[i]) * zoom;
            if (w > zoomedPageWidthMax) zoomedPageWidthMax = w;
        }

        if (zoomedPageWidthMax <= static_cast<float>(width)) {
            // Content fits in the viewport, keep perfectly centered
            m_scrollState->horizontalOffset = 0.0f;
        } else {
            // Content is wider than viewport; clamp offset within valid range
            float minHorizontalOffset = (static_cast<float>(width) - zoomedPageWidthMax) / 2.0f;
            float maxHorizontalOffset = (zoomedPageWidthMax - static_cast<float>(width)) / 2.0f;
            if (m_scrollState->horizontalOffset < minHorizontalOffset) {
                m_scrollState->horizontalOffset = minHorizontalOffset;
            } else if (m_scrollState->horizontalOffset > maxHorizontalOffset) {
                m_scrollState->horizontalOffset = maxHorizontalOffset;
            }
        }
    }

    // Force texture regeneration for the new size
    m_needsFullRegeneration = true;
    // Cancel pending renders and reschedule visible
    if (m_asyncQueue) {
        m_asyncQueue->cancelAll();
        scheduleVisibleRegeneration(false);
    }
    
    std::cout << "PDFViewerEmbedder: Resized to " << width << "x" << height << std::endl;
}

void PDFViewerEmbedder::shutdown()
{
    if (!m_initialized) {
        return;
    }

    std::cout << "PDFViewerEmbedder: Starting shutdown..." << std::endl;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Clean up textures first
    cleanupTextures();

    // CRITICAL: Stop async rendering thread BEFORE destroying renderer or other state.
    // Previously we destroyed the PDFRenderer while the AsyncRenderQueue worker thread
    // could still be rendering a page (it holds a raw PDFRenderer*). That creates a
    // dangling pointer use-after-free when many tabs are opened/closed or switched rapidly.
    // Resetting here joins the worker thread safely (AsyncRenderQueue dtor joins).
    if (m_asyncQueue) {
        std::cout << "PDFViewerEmbedder: Stopping async render queue..." << std::endl;
        m_asyncQueue.reset();
    }
    
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

    // Schedule progressive visible regeneration
    scheduleVisibleRegeneration(false);
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

    // Schedule progressive visible regeneration
    scheduleVisibleRegeneration(false);
}

void PDFViewerEmbedder::setZoom(float zoomLevel)
{
    if (!m_scrollState) return;
    
    // Apply SAME zoom limits as HandleZoom function (0.35f to 15.0f)
    if (zoomLevel < 0.35f) zoomLevel = 0.35f;
    if (zoomLevel > 15.0f) zoomLevel = 15.0f;
    
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

    // Schedule progressive visible regeneration
    scheduleVisibleRegeneration(true);
}

void PDFViewerEmbedder::goToPage(int pageNumber)
{
    if (!m_initialized || !m_pdfLoaded || !m_scrollState || !m_renderer) {
        std::cout << "PDFViewerEmbedder::goToPage() - Not initialized or PDF not loaded" << std::endl;
        return;
    }
    
    int pageCount = m_renderer->GetPageCount();
    if (pageNumber < 1 || pageNumber > pageCount) {
        std::cout << "PDFViewerEmbedder::goToPage() - Invalid page number: " << pageNumber << " (valid range: 1-" << pageCount << ")" << std::endl;
        return;
    }
    
    // Convert to 0-based index
    int pageIndex = pageNumber - 1;
    
    // Calculate the Y offset to scroll to the target page
    float targetOffset = 0.0f;
    
    // Sum up heights of all pages before the target page
    for (int i = 0; i < pageIndex && i < (int)m_pageHeights.size(); ++i) {
        // Add scaled page height (considering current zoom)
        targetOffset += m_pageHeights[i] * m_scrollState->zoomScale;
    }
    
    std::cout << "PDFViewerEmbedder::goToPage() - Navigating to page " << pageNumber 
              << " (index " << pageIndex << "), target offset: " << targetOffset << std::endl;
    
    // Set the scroll offset to show the target page at the top
    m_scrollState->scrollOffset = targetOffset;
    
    // Update the scroll state first to recalculate max offset
    UpdateScrollState(*m_scrollState, (float)m_windowHeight, m_pageHeights);
    
    // Ensure we don't scroll beyond the valid range (after scroll state update)
    if (m_scrollState->scrollOffset > m_scrollState->maxOffset) {
        m_scrollState->scrollOffset = m_scrollState->maxOffset;
    }
    if (m_scrollState->scrollOffset < 0.0f) {
        m_scrollState->scrollOffset = 0.0f;
    }
    
    // Force regeneration of visible textures for the new page range
    m_needsVisibleRegeneration = true;
    
    std::cout << "PDFViewerEmbedder::goToPage() - Successfully navigated to page " << pageNumber 
              << ", final scroll offset: " << m_scrollState->scrollOffset 
              << ", max offset: " << m_scrollState->maxOffset << std::endl;
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

// Rotation methods
void PDFViewerEmbedder::rotateLeft()
{
    if (!m_initialized || !m_pdfLoaded || !m_renderer || !m_scrollState) return;
    
    std::cout << "Embedded viewer: Rotating all pages left (counterclockwise)" << std::endl;
    
    // Work directly with our local renderer instead of using global MenuIntegration
    FPDF_DOCUMENT doc = m_renderer->GetDocument();
    if (!doc) {
        std::cout << "Error: No document loaded for rotation" << std::endl;
        return;
    }
    
    int pageCount = m_renderer->GetPageCount();
    std::cout << "Rotating " << pageCount << " pages left (counterclockwise)" << std::endl;
    
    // Rotate all pages 90 degrees counterclockwise
    for (int i = 0; i < pageCount; i++) {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        if (page) {
            // Get current rotation
            int currentRotation = FPDFPage_GetRotation(page);
            
            // Calculate new rotation (subtract 90 degrees, wrap around)
            int newRotation = (currentRotation - 1 + 4) % 4;  // 0=0°, 1=90°, 2=180°, 3=270°
            
            // Set new rotation
            FPDFPage_SetRotation(page, newRotation);
            
            FPDF_ClosePage(page);
        }
    }
    
    // Force regeneration of all textures after rotation
    m_needsFullRegeneration = true;
    
    // Force immediate update to show rotation effect
    update();
    
    std::cout << "Embedded viewer: Left rotation completed, textures regenerated immediately" << std::endl;
}

void PDFViewerEmbedder::rotateRight()
{
    if (!m_initialized || !m_pdfLoaded || !m_renderer || !m_scrollState) return;
    
    std::cout << "Embedded viewer: Rotating all pages right (clockwise)" << std::endl;
    
    // Work directly with our local renderer instead of using global MenuIntegration
    FPDF_DOCUMENT doc = m_renderer->GetDocument();
    if (!doc) {
        std::cout << "Error: No document loaded for rotation" << std::endl;
        return;
    }
    
    int pageCount = m_renderer->GetPageCount();
    std::cout << "Rotating " << pageCount << " pages right (clockwise)" << std::endl;
    
    // Rotate all pages 90 degrees clockwise
    for (int i = 0; i < pageCount; i++) {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        if (page) {
            // Get current rotation
            int currentRotation = FPDFPage_GetRotation(page);
            
            // Calculate new rotation (add 90 degrees, wrap around)
            int newRotation = (currentRotation + 1) % 4;  // 0=0°, 1=90°, 2=180°, 3=270°
            
            // Set new rotation
            FPDFPage_SetRotation(page, newRotation);
            
            FPDF_ClosePage(page);
        }
    }
    
    // Force regeneration of all textures after rotation
    m_needsFullRegeneration = true;
    
    // Force immediate update to show rotation effect
    update();
    
    std::cout << "Embedded viewer: Right rotation completed, textures regenerated immediately" << std::endl;
}

int PDFViewerEmbedder::getPageCount() const
{
    if (!m_initialized || !m_pdfLoaded || !m_renderer) return 0;
    return m_renderer->GetPageCount();
}

float PDFViewerEmbedder::getCurrentZoom() const
{
    if (!m_initialized || !m_pdfLoaded || !m_scrollState) return 1.0f;
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
        
        // Add spacing between pages (same as in goToPage)
        if (i > 0) {
            accumulatedHeight += 10.0f; // Page spacing
        }
        
        // Check if current offset is within this page
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

    // Cache GL max texture size for runtime clamping
    m_glMaxTextureSize = caps.maxTextureSize;
    if (m_glMaxTextureSize <= 0) {
        GLint q = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &q);
        m_glMaxTextureSize = (int)q;
    }
    if (m_glMaxTextureSize <= 0) {
        // Fallback to a safe default if query failed
        m_glMaxTextureSize = 8192;
    }
    std::cout << "GL_MAX_TEXTURE_SIZE cached: " << m_glMaxTextureSize << std::endl;
    
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
    // Use a very light neutral tone instead of pure white to reduce contrast + hide tearing
    glClearColor(0.965f, 0.965f, 0.97f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (!m_pdfLoaded) {
        return;
    }
    
    // Enable blending early (used for gradient / placeholders)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw a subtle vertical gradient background (light gray to slightly lighter gray)
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_windowWidth, m_windowHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_QUADS);
        glColor4f(0.958f, 0.958f, 0.965f, 1.0f); glVertex2f(0, 0);
        glColor4f(0.958f, 0.958f, 0.965f, 1.0f); glVertex2f((float)m_windowWidth, 0);
        glColor4f(0.975f, 0.975f, 0.98f, 1.0f); glVertex2f((float)m_windowWidth, (float)m_windowHeight);
        glColor4f(0.975f, 0.975f, 0.98f, 1.0f); glVertex2f(0, (float)m_windowHeight);
    glEnd();
    glColor4f(1,1,1,1);
    glEnable(GL_TEXTURE_2D);
    
    int pageCount = m_renderer->GetPageCount();
    float yOffset = -m_scrollState->scrollOffset;
    
    // Use pipeline-specific rendering
    auto selectedPipeline = m_pipelineManager->getSelectedPipeline();
    
    // NOTE: Page spacing was previously 12px but that introduced a mismatch with
    // scrollOffset math (which doesn't account for spacing) causing cursor-centered
    // zoom to drift. Setting to 0 restores original zoom behavior. If spacing is
    // reintroduced later, UpdateScrollState and related computations must include it.
    const float PAGE_SPACING = 0.0f; // keep 0 to preserve correct zoom focal mapping
    if (selectedPipeline == RenderingPipeline::LEGACY_IMMEDIATE) {
        // Legacy OpenGL 2.1 - Use fixed function pipeline with immediate mode
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, m_windowWidth, m_windowHeight, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        // Draw pages using immediate mode (supported in legacy pipeline)
    for (int i = 0; i < pageCount; ++i) {
            float pageW = (float)m_pageWidths[i] * m_scrollState->zoomScale;
            float pageH = (float)m_pageHeights[i] * m_scrollState->zoomScale;
            
            float xCenter = (m_windowWidth / 2.0f) - m_scrollState->horizontalOffset;
            float yCenter = yOffset + pageH / 2.0f;
            float x = xCenter - pageW / 2.0f;
            float y = yCenter - pageH / 2.0f;

            if (m_textures[i] != 0) {
                glBindTexture(GL_TEXTURE_2D, m_textures[i]);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(x + pageW, y);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(x + pageW, y + pageH);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + pageH);
                glEnd();
            } else {
                // Improved placeholder: soft neutral card with subtle shimmer (no black flash)
                glBindTexture(GL_TEXTURE_2D, 0);
                double t = glfwGetTime();
                float shimmer = 0.15f + 0.10f * (float)sin(t * 3.5 + i * 0.7);
                float topL = 0.94f + shimmer * 0.05f;
                float botL = 0.935f + shimmer * 0.04f;
                // Card background
                glBegin(GL_QUADS);
                    glColor4f(topL, topL, topL, 1.0f); glVertex2f(x, y);
                    glColor4f(topL, topL, topL, 1.0f); glVertex2f(x + pageW, y);
                    glColor4f(botL, botL, botL, 1.0f); glVertex2f(x + pageW, y + pageH);
                    glColor4f(botL, botL, botL, 1.0f); glVertex2f(x, y + pageH);
                glEnd();
                // Soft inner border (shadow) to suggest depth
                glColor4f(0.80f, 0.80f, 0.82f, 0.55f);
                glBegin(GL_LINE_LOOP);
                    glVertex2f(x+0.5f, y+0.5f);
                    glVertex2f(x + pageW-0.5f, y+0.5f);
                    glVertex2f(x + pageW-0.5f, y + pageH-0.5f);
                    glVertex2f(x+0.5f, y + pageH-0.5f);
                glEnd();
            }
            yOffset += pageH + PAGE_SPACING;
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
            float pageW = (float)m_pageWidths[i] * m_scrollState->zoomScale;
            float pageH = (float)m_pageHeights[i] * m_scrollState->zoomScale;

            float xCenter = (m_windowWidth / 2.0f) - m_scrollState->horizontalOffset;
            float yCenter = yOffset + pageH / 2.0f;
            float x = xCenter - pageW / 2.0f;
            float y = yCenter - pageH / 2.0f;

            if (m_textures[i] != 0) {
                glBindTexture(GL_TEXTURE_2D, m_textures[i]);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(x + pageW, y);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(x + pageW, y + pageH);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + pageH);
                glEnd();
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
                double t = glfwGetTime();
                float shimmer = 0.15f + 0.10f * (float)sin(t * 3.5 + i * 0.7);
                float topL = 0.94f + shimmer * 0.05f;
                float botL = 0.935f + shimmer * 0.04f;
                glBegin(GL_QUADS);
                    glColor4f(topL, topL, topL, 1.0f); glVertex2f(x, y);
                    glColor4f(topL, topL, topL, 1.0f); glVertex2f(x + pageW, y);
                    glColor4f(botL, botL, botL, 1.0f); glVertex2f(x + pageW, y + pageH);
                    glColor4f(botL, botL, botL, 1.0f); glVertex2f(x, y + pageH);
                glEnd();
                glColor4f(0.80f, 0.80f, 0.82f, 0.55f);
                glBegin(GL_LINE_LOOP);
                    glVertex2f(x+0.5f, y+0.5f);
                    glVertex2f(x + pageW-0.5f, y+0.5f);
                    glVertex2f(x + pageW-0.5f, y + pageH-0.5f);
                    glVertex2f(x+0.5f, y + pageH-0.5f);
                glEnd();
            }
            yOffset += pageH + PAGE_SPACING;
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
    m_textureByteSizes.assign(pageCount, 0);
    m_currentTextureBytes = 0;
    m_budgetDownscaleApplied = false;
    
    // Determine visible range to restrict regeneration to only needed pages + margin
    int firstVisible=-1, lastVisible=-1;
    if (m_scrollState) {
        GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    }
    if (firstVisible < 0 || lastVisible < 0) { firstVisible = 0; lastVisible = std::min(pageCount-1, 7); }
    int regenStart = std::max(0, firstVisible - m_preloadPageMargin);
    int regenEnd   = std::min(pageCount-1, lastVisible + m_preloadPageMargin);

    // Regenerate only visible + margin textures using ACTUAL page dimensions (not window-fitted)
    for (int i = regenStart; i <= regenEnd; ++i) {
        // Get the ACTUAL page dimensions from PDF (not window-fitted)
        double originalPageWidth, originalPageHeight;
        m_renderer->GetOriginalPageSize(i, originalPageWidth, originalPageHeight);
        
        // Calculate texture size based on zoom level applied to ACTUAL page dimensions
        float effectiveZoom = m_scrollState->zoomScale;
        // Adaptive zoom reduce if we are over budget (pre-flight)
        int textureWidth = std::max(8, (int)(originalPageWidth * effectiveZoom));
        int textureHeight = std::max(8, (int)(originalPageHeight * effectiveZoom));
        size_t projectedBytes = (size_t)textureWidth * (size_t)textureHeight * 4ull;
        float adjustedZoom = computeAdaptiveZoomForBudget(originalPageWidth, originalPageHeight, effectiveZoom, projectedBytes);
        if (adjustedZoom != effectiveZoom) {
            effectiveZoom = adjustedZoom;
            m_budgetDownscaleApplied = true;
        }
        // HARD FLOOR: don't allow effective texture zoom to drop below % of requested (prevents 8x8 block pages)
        const float MIN_EFFECTIVE_RATIO = 0.55f; // keep at least 55% resolution to avoid ugly block artifact
        if (effectiveZoom < m_scrollState->zoomScale * MIN_EFFECTIVE_RATIO) {
            effectiveZoom = m_scrollState->zoomScale * MIN_EFFECTIVE_RATIO;
        }
        textureWidth = std::max(8, (int)(originalPageWidth * effectiveZoom));
        textureHeight = std::max(8, (int)(originalPageHeight * effectiveZoom));

        // Clamp to runtime GL max texture size conservatively
        const int MAX_TEXTURE_DIM = (m_glMaxTextureSize > 0 ? m_glMaxTextureSize - 64 : 8192);
        if (textureWidth > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureWidth;
            textureWidth = MAX_TEXTURE_DIM;
            textureHeight = std::max(1, (int)(textureHeight * scale));
        }
        if (textureHeight > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureHeight;
            textureHeight = MAX_TEXTURE_DIM;
            textureWidth = std::max(1, (int)(textureWidth * scale));
        }
        
        // Ensure minimum texture size for readability
    // Allow very small textures at low zoom to avoid upscaling blur
        
        // Debug output for high zoom levels
    if (m_scrollState->zoomScale > 6.0f) {
            std::cout << "HIGH ZOOM DEBUG (Full): ZoomScale=" << m_scrollState->zoomScale 
                      << ", EffectiveZoom=" << effectiveZoom 
                      << ", TextureSize=" << textureWidth << "x" << textureHeight 
                      << ", OriginalPage=" << originalPageWidth << "x" << originalPageHeight << std::endl;
        }
        
        // Render at calculated size using actual page dimensions (no window fitting)
        FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(i, textureWidth, textureHeight);
        if (!bmp) {
            // Fallback: no bitmap (PDFium failure) – leave texture 0 so placeholder renders (avoid black).
            m_textures[i] = 0;
        } else {
            size_t oldBytes = (m_textureByteSizes.size()> (size_t)i)? m_textureByteSizes[i] : 0;
            if (m_textureByteSizes.size() <= (size_t)i) m_textureByteSizes.resize(i+1,0);
            m_textures[i] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
            size_t newBytes = (size_t)textureWidth * (size_t)textureHeight * 4ull;
            m_textureByteSizes[i] = newBytes;
            trackTextureAllocation(oldBytes, newBytes, i);
            FPDFBitmap_Destroy(bmp);
        }
        // Store BASE dimensions (ACTUAL page dimensions, not window-fitted)
        m_pageWidths[i] = static_cast<int>(originalPageWidth);
        m_pageHeights[i] = static_cast<int>(originalPageHeight);
    }
    // Pages outside (regenStart, regenEnd) remain as placeholders until scrolled into view
    
    // Update scroll state
    UpdateScrollState(*m_scrollState, (float)m_windowHeight, m_pageHeights);
    m_scrollState->lastRenderedZoom = m_scrollState->zoomScale;
    
    m_needsFullRegeneration = false;
    enforceMemoryBudget();
}

void PDFViewerEmbedder::regenerateVisibleTextures()
{
    if (!m_pdfLoaded) return;
    
    int pageCount = m_renderer->GetPageCount();
    int firstVisible, lastVisible;
    GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    int regenStart = std::max(0, firstVisible - m_preloadPageMargin);
    int regenEnd   = std::min(pageCount-1, lastVisible + m_preloadPageMargin);
    
    // Only regenerate textures for visible+margin pages using ACTUAL page dimensions
    for (int i = regenStart; i <= regenEnd && i < pageCount; ++i) {
        if (m_textures[i]) {
            glDeleteTextures(1, &m_textures[i]);
        }
        
        // Get the ACTUAL page dimensions from PDF (not window-fitted)
        double originalPageWidth, originalPageHeight;
        m_renderer->GetOriginalPageSize(i, originalPageWidth, originalPageHeight);
        
        // Calculate texture size based on zoom level applied to ACTUAL page dimensions
        float effectiveZoom = m_scrollState->zoomScale;
        int textureWidth = std::max(8, (int)(originalPageWidth * effectiveZoom));
        int textureHeight = std::max(8, (int)(originalPageHeight * effectiveZoom));
        size_t projectedBytes = (size_t)textureWidth * (size_t)textureHeight * 4ull;
        float adjustedZoom = computeAdaptiveZoomForBudget(originalPageWidth, originalPageHeight, effectiveZoom, projectedBytes);
        if (adjustedZoom != effectiveZoom) {
            effectiveZoom = adjustedZoom;
            m_budgetDownscaleApplied = true;
        }
        const float MIN_EFFECTIVE_RATIO = 0.55f;
        if (effectiveZoom < m_scrollState->zoomScale * MIN_EFFECTIVE_RATIO) {
            effectiveZoom = m_scrollState->zoomScale * MIN_EFFECTIVE_RATIO;
        }
        textureWidth = std::max(8, (int)(originalPageWidth * effectiveZoom));
        textureHeight = std::max(8, (int)(originalPageHeight * effectiveZoom));

        // Clamp to runtime GL max texture size conservatively
        const int MAX_TEXTURE_DIM = (m_glMaxTextureSize > 0 ? m_glMaxTextureSize - 64 : 8192);
        if (textureWidth > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureWidth;
            textureWidth = MAX_TEXTURE_DIM;
            textureHeight = std::max(1, (int)(textureHeight * scale));
        }
        if (textureHeight > MAX_TEXTURE_DIM) {
            float scale = (float)MAX_TEXTURE_DIM / textureHeight;
            textureHeight = MAX_TEXTURE_DIM;
            textureWidth = std::max(1, (int)(textureWidth * scale));
        }
        
    // Allow very small textures at low zoom to avoid upscaling blur
        
        // Debug output for high zoom levels
    if (m_scrollState->zoomScale > 6.0f) {
            std::cout << "HIGH ZOOM DEBUG (Visible): ZoomScale=" << m_scrollState->zoomScale 
                      << ", EffectiveZoom=" << effectiveZoom 
                      << ", TextureSize=" << textureWidth << "x" << textureHeight 
                      << ", OriginalPage=" << originalPageWidth << "x" << originalPageHeight << std::endl;
        }
    // Allow very small textures at low zoom to avoid upscaling blur
        
        // Render at calculated size using actual page dimensions (no window fitting)
    FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(i, textureWidth, textureHeight);
        if (!bmp) {
            m_textures[i] = 0; // placeholder will show
        } else {
            size_t oldBytes = (m_textureByteSizes.size()> (size_t)i)? m_textureByteSizes[i] : 0;
            if (m_textureByteSizes.size() <= (size_t)i) m_textureByteSizes.resize(i+1,0);
            m_textures[i] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
            size_t newBytes = (size_t)textureWidth * (size_t)textureHeight * 4ull;
            m_textureByteSizes[i] = newBytes;
            trackTextureAllocation(oldBytes, newBytes, i);
            FPDFBitmap_Destroy(bmp);
        }
        // Store BASE dimensions (ACTUAL page dimensions, not window-fitted)
        m_pageWidths[i] = static_cast<int>(originalPageWidth);
        m_pageHeights[i] = static_cast<int>(originalPageHeight);
    }
    
    m_needsVisibleRegeneration = false;
    enforceMemoryBudget();
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
    float effectiveZoom = m_scrollState->zoomScale;

    int textureWidth = std::max(8, (int)(originalPageWidth * effectiveZoom));
    int textureHeight = std::max(8, (int)(originalPageHeight * effectiveZoom));
    size_t projectedBytes = (size_t)textureWidth * (size_t)textureHeight * 4ull;
    float adjustedZoom = computeAdaptiveZoomForBudget(originalPageWidth, originalPageHeight, effectiveZoom, projectedBytes);
    if (adjustedZoom != effectiveZoom) {
        effectiveZoom = adjustedZoom;
        m_budgetDownscaleApplied = true;
    }
    const float MIN_EFFECTIVE_RATIO = 0.55f;
    if (effectiveZoom < m_scrollState->zoomScale * MIN_EFFECTIVE_RATIO) {
        effectiveZoom = m_scrollState->zoomScale * MIN_EFFECTIVE_RATIO;
    }
    textureWidth = std::max(8, (int)(originalPageWidth * effectiveZoom));
    textureHeight = std::max(8, (int)(originalPageHeight * effectiveZoom));

    // Clamp to runtime GL max texture size conservatively
    const int MAX_TEXTURE_DIM = (m_glMaxTextureSize > 0 ? m_glMaxTextureSize - 64 : 8192);
    if (textureWidth > MAX_TEXTURE_DIM) {
        float scale = (float)MAX_TEXTURE_DIM / textureWidth;
        textureWidth = MAX_TEXTURE_DIM;
        textureHeight = std::max(1, (int)(textureHeight * scale));
    }
    if (textureHeight > MAX_TEXTURE_DIM) {
        float scale = (float)MAX_TEXTURE_DIM / textureHeight;
        textureHeight = MAX_TEXTURE_DIM;
        textureWidth = std::max(1, (int)(textureWidth * scale));
    }
    
    // Allow very small textures at low zoom to avoid upscaling blur
    
    // Debug output for high zoom levels
    if (m_scrollState->zoomScale > 6.0f) {
        std::cout << "HIGH ZOOM DEBUG (Single): Page=" << pageIndex 
                  << ", ZoomScale=" << m_scrollState->zoomScale 
                  << ", EffectiveZoom=" << effectiveZoom 
                  << ", TextureSize=" << textureWidth << "x" << textureHeight 
                  << ", OriginalPage=" << originalPageWidth << "x" << originalPageHeight << std::endl;
    }
    
    // Render at calculated size using actual page dimensions (no window fitting)
    FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(pageIndex, textureWidth, textureHeight);
    if (!bmp) {
        std::cerr << "PDFViewerEmbedder["<<m_viewerId<<"] regeneratePageTexture: NULL bitmap page="<<pageIndex
                  <<" size="<<textureWidth<<"x"<<textureHeight<<" (placeholder kept)" << std::endl;
    } else {
        size_t oldBytes = (m_textureByteSizes.size()> (size_t)pageIndex)? m_textureByteSizes[pageIndex] : 0;
        if (m_textureByteSizes.size() <= (size_t)pageIndex) m_textureByteSizes.resize(pageIndex+1,0);
        m_textures[pageIndex] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
        size_t newBytes = (size_t)textureWidth * (size_t)textureHeight * 4ull;
        m_textureByteSizes[pageIndex] = newBytes;
        trackTextureAllocation(oldBytes, newBytes, pageIndex);
        FPDFBitmap_Destroy(bmp);
    }
    // Store BASE dimensions
    m_pageWidths[pageIndex] = static_cast<int>(originalPageWidth);
    m_pageHeights[pageIndex] = static_cast<int>(originalPageHeight);
    enforceMemoryBudget();
}

void PDFViewerEmbedder::handleBackgroundRendering()
{
    // Background rendering logic (from your main.cpp)
    static int backgroundRenderIndex = 0;
    static int frameCounter = 0;
    frameCounter++;
    
    if (frameCounter % 5 == 0 && !m_needsFullRegeneration && !m_needsVisibleRegeneration) {
    // If we're already at/over 90% budget, skip background refinement to avoid pressure
    if (m_memoryBudgetBytes>0 && m_currentTextureBytes > (size_t)(m_memoryBudgetBytes * 0.9)) return;
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
            // Ensure background still has a sane floor to avoid 8x8 blocks appearing dark
            if (backgroundZoom < m_scrollState->zoomScale * 0.45f) {
                backgroundZoom = m_scrollState->zoomScale * 0.45f;
            }
            
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
            size_t oldBytes = m_textureByteSizes.size() > (size_t)backgroundRenderIndex ? m_textureByteSizes[backgroundRenderIndex] : 0;
            if (m_textureByteSizes.size() <= (size_t)backgroundRenderIndex) {
                m_textureByteSizes.resize(backgroundRenderIndex + 1, 0);
            }
            m_textures[backgroundRenderIndex] = createTextureFromPDFBitmap(FPDFBitmap_GetBuffer(bmp), textureWidth, textureHeight);
            size_t newBytes = (size_t)textureWidth * (size_t)textureHeight * 4ull;
            m_textureByteSizes[backgroundRenderIndex] = newBytes;
            trackTextureAllocation(oldBytes, newBytes, backgroundRenderIndex);
            FPDFBitmap_Destroy(bmp);
            enforceMemoryBudget();
            break;
        }
    }
}

void PDFViewerEmbedder::cleanupTextures()
{
    if (!m_textures.empty()) {
        for (size_t i = 0; i < m_textures.size(); ++i) {
            GLuint texture = m_textures[i];
            if (texture) {
                glDeleteTextures(1, &texture);
            }
        }
        m_textures.clear();
    }
    m_textureByteSizes.clear();
    m_currentTextureBytes = 0;
}

unsigned int PDFViewerEmbedder::createTextureFromPDFBitmap(void* buffer, int width, int height)
{
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    if (m_enableMipmaps) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
    if (m_enableMipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}

// Track allocation delta and update total
void PDFViewerEmbedder::trackTextureAllocation(size_t oldBytes, size_t newBytes, int index) {
    if (newBytes == oldBytes) return;
    if (newBytes > oldBytes) {
        m_currentTextureBytes += (newBytes - oldBytes);
    } else if (oldBytes > newBytes) {
        m_currentTextureBytes -= (oldBytes - newBytes);
    }
    // Optional: log when large (>8MB) allocations happen
    if (newBytes > 8ull * 1024ull * 1024ull) {
        std::cout << "Texture " << index << " size=" << newBytes/1024/1024 << "MB totalUsed=" << m_currentTextureBytes/1024/1024 << "MB" << std::endl;
    }
}

// Compute adaptive zoom to keep projected allocation within remaining budget
float PDFViewerEmbedder::computeAdaptiveZoomForBudget(double originalW, double originalH, float requestedZoom, size_t projectedBytes) const {
    if (m_memoryBudgetBytes == 0) return requestedZoom; // disabled
    size_t remaining = (m_currentTextureBytes < m_memoryBudgetBytes) ? (m_memoryBudgetBytes - m_currentTextureBytes) : 0;
    if (projectedBytes <= remaining) return requestedZoom; // fine
    if (remaining == 0) {
        // Hard pressure: shrink requested zoom proportionally
        double area = originalW * originalH;
        if (area <= 0) return requestedZoom;
        double maxPixels = (double)remaining / 4.0; // RGBA
        if (maxPixels <= 0) maxPixels = 1.0;
        double scaleFactor = std::sqrt(maxPixels / area);
        return (float)std::max(8.0 / std::max(originalW, originalH), scaleFactor); // ensure min size
    }
    double over = (double)projectedBytes / (double)remaining; // >1 means overflow factor
    if (over <= 1.0) return requestedZoom;
    double reduction = 1.0 / std::sqrt(over); // scale linear dims by sqrt to reduce area
    float adjusted = (float)(requestedZoom * reduction);
    // Clamp not below minimal meaningful sampling (approx 0.15 of requested)
    if (adjusted < requestedZoom * 0.15f) adjusted = requestedZoom * 0.15f;
    return adjusted;
}

// Enforce global budget after allocations (evict/downscale non-visible textures if needed)
void PDFViewerEmbedder::enforceMemoryBudget() {
    if (m_memoryBudgetBytes == 0) return; // disabled
    if (m_currentTextureBytes <= m_memoryBudgetBytes) return;

    // Strategy: first delete non-visible high-cost textures until under budget.
    int firstVisible=-1, lastVisible=-1;
    if (m_scrollState) {
        GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    }
    // Build list of candidate indices (outside visible range) sorted by size desc
    struct Item { int idx; size_t bytes; };
    std::vector<Item> candidates;
    for (int i = 0; i < (int)m_textures.size(); ++i) {
        bool visible = (i >= firstVisible && i <= lastVisible);
        if (!visible && m_textures[i] && i < (int)m_textureByteSizes.size()) {
            candidates.push_back({i, m_textureByteSizes[i]});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Item&a,const Item&b){return a.bytes>b.bytes;});
    for (auto &c : candidates) {
        if (m_currentTextureBytes <= m_memoryBudgetBytes) break;
        if (m_textures[c.idx]) {
            glDeleteTextures(1, &m_textures[c.idx]);
            m_textures[c.idx] = 0;
            m_currentTextureBytes -= c.bytes;
            m_textureByteSizes[c.idx] = 0;
        }
    }
    // If still over budget, downscale visible pages progressively (regenVisibleTextures will rebuild)
    if (m_currentTextureBytes > m_memoryBudgetBytes * 1.05) { // allow small hysteresis
        // Request a visible regeneration at reduced zoom (soft approach: pretend lastRenderedZoom smaller)
        if (m_scrollState) {
            m_scrollState->lastRenderedZoom = m_scrollState->zoomScale * 0.7f; // force regen path
        }
        m_needsVisibleRegeneration = true;
        m_budgetDownscaleApplied = true;
    }
    if (m_budgetDownscaleApplied) {
        std::cout << "Memory budget enforcement: total=" << m_currentTextureBytes/1024/1024
                  << "MB budget=" << m_memoryBudgetBytes/1024/1024 << "MB" << std::endl;
    }
}

// Texture optimization method for smooth zoom transitions
float PDFViewerEmbedder::getOptimalTextureZoom(float currentZoom) const
{
    // Keep as-is; clamping occurs in regeneration code using GL_MAX_TEXTURE_SIZE
    // Avoid artificially reducing zoom which would cause blur
    return currentZoom;
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
        if (embedder->m_rightPressTime > 0.0 && !embedder->m_rightMoved) {
            double dx = xpos - embedder->m_rightPressX;
            double dy = ypos - embedder->m_rightPressY;
            if ((dx*dx + dy*dy) > (6.0 * 6.0)) {
                embedder->m_rightMoved = true;
            }
        }
        embedder->onCursorPos(xpos, ypos);
    }
}

void PDFViewerEmbedder::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    PDFViewerEmbedder* embedder = static_cast<PDFViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                embedder->m_rightPressTime = glfwGetTime();
                embedder->m_rightMoved = false;
                embedder->m_rightPressX = embedder->m_scrollState ? embedder->m_scrollState->lastCursorX : 0.0;
                embedder->m_rightPressY = embedder->m_scrollState ? embedder->m_scrollState->lastCursorY : 0.0;
            } else if (action == GLFW_RELEASE) {
                double dt = glfwGetTime() - embedder->m_rightPressTime;
                if (embedder->m_rightPressTime > 0.0 && !embedder->m_rightMoved && dt < 0.35) {
                    std::string sel = embedder->getSelectedText();
                    if (!sel.empty() && embedder->m_quickRightClickCallback) {
                        embedder->m_quickRightClickCallback(sel);
                    }
                }
                embedder->m_rightPressTime = 0.0;
            }
        }
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
        // While panning, schedule lightweight async visible regeneration at a limited rate
        double now = glfwGetTime();
        if (now - m_lastPanRegenTime > 0.050) { // 20 FPS cap for regen submits
            scheduleVisibleRegeneration(false);
            m_lastPanRegenTime = now;
        }
    }
    
    if (m_scrollState->isScrollBarDragging) {
        UpdateScrollBarDragging(*m_scrollState, ypos, (float)m_windowHeight);
        // Throttled regeneration while dragging the scrollbar
        double now = glfwGetTime();
        if (now - m_lastScrollRegenTime > 0.050) { // ~20 FPS
            scheduleVisibleRegeneration(false);
            m_lastScrollRegenTime = now;
        }
    }
}

void PDFViewerEmbedder::onMouseButton(int button, int action, int /*mods*/)
{
    // Quick right-click press bookkeeping BEFORE existing logic
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            m_rightPressTime = glfwGetTime();
            m_rightMoved = false;
            m_rightPressX = m_scrollState ? m_scrollState->lastCursorX : 0.0;
            m_rightPressY = m_scrollState ? m_scrollState->lastCursorY : 0.0;
        } else if (action == GLFW_RELEASE) {
            double dt = glfwGetTime() - m_rightPressTime;
            if (m_rightPressTime > 0.0 && !m_rightMoved && dt < 0.35) {
                std::string sel = getSelectedText();
                if (!sel.empty() && m_quickRightClickCallback) {
                    m_quickRightClickCallback(sel);
                }
            }
            m_rightPressTime = 0.0; // reset regardless
        }
    }

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
            // Fire a settled crisp regeneration after scrollbar drag ends
            scheduleVisibleRegeneration(true);
            
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
            // Fire a settled crisp regeneration
            scheduleVisibleRegeneration(true);
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            // Middle click for panning (alternative to right click)
            StartPanning(*m_scrollState, mouseX, mouseY);
            glfwSetCursor(m_glfwWindow, glfwCreateStandardCursor(GLFW_HAND_CURSOR));
        } else if (action == GLFW_RELEASE) {
            StopPanning(*m_scrollState);
            glfwSetCursor(m_glfwWindow, nullptr);
            scheduleVisibleRegeneration(true);
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
        // Throttled regen while scrollbar scrolling
        double now = glfwGetTime();
        if (now - m_lastScrollRegenTime > 0.050) {
            scheduleVisibleRegeneration(false);
            m_lastScrollRegenTime = now;
        }
        return;
    }
    
    // Otherwise, handle cursor-based zooming with mouse wheel
    if (std::abs(yoffset) > 0.01) {
        // Use a stronger but bounded delta so high zoom stays responsive
        float stepUp = 1.2f;
        float rawDelta = (yoffset > 0) ? stepUp : 1.0f / stepUp;
        float zoomDelta = std::clamp(rawDelta, 0.85f, 1.25f);

        // (Disabled heavy per-tick file logging – it was causing I/O stalls during fast wheel zoom)
        // If needed for deep debugging, wrap the logging block in an #ifdef and re-enable.

        HandleZoom(*m_scrollState, zoomDelta, (float)cursorX, (float)cursorY,
                   (float)m_windowWidth, (float)m_windowHeight,
                   m_pageHeights, m_pageWidths);

        // Detect whether this wheel event starts a new gesture (long gap since last one)
        double now = glfwGetTime();
        double previousWheelTime = s_lastWheelZoomTime;
        s_lastWheelZoomTime = now;
        bool newGesture = (now - previousWheelTime) > 0.25; // 250 ms gap ⇒ new gesture

        s_pendingSettledRegen = true; // request crisp pass after idle

        bool triggeredPreview = false;
        if (m_scrollState->lastRenderedZoom > 0.0f) {
            float ratio = m_scrollState->zoomScale / m_scrollState->lastRenderedZoom;
            bool zoomingIn = ratio > 1.0f;
            float upThreshold = 1.5f;
            float downThreshold = 0.55f;
            if (((zoomingIn && ratio > upThreshold) || (!zoomingIn && ratio < downThreshold)) &&
                (now - m_lastScrollRegenTime) > 0.095) {
                scheduleVisibleRegeneration(false); // quick preview regen
                m_lastScrollRegenTime = now;
                triggeredPreview = true;
            }
        }

        // Guarantee at least one preview at the start of a gesture even if thresholds not crossed
        if (!triggeredPreview && newGesture) {
            scheduleVisibleRegeneration(false);
            triggeredPreview = true;
        }

        // If visible pages currently have no textures (fresh load / eviction), force a preview regen
        if (!triggeredPreview) {
            int firstVisible=-1, lastVisible=-1;
            GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
            bool missingTexture = false;
            if (firstVisible >= 0 && lastVisible >= firstVisible) {
                for (int i = firstVisible; i <= lastVisible; ++i) {
                    if (i >= 0 && i < (int)m_textures.size() && m_textures[i] == 0) { missingTexture = true; break; }
                }
            }
            if (missingTexture && (now - m_lastScrollRegenTime) > 0.03) { // faster path when blank
                scheduleVisibleRegeneration(false);
                m_lastScrollRegenTime = now;
                triggeredPreview = true;
            }
        }

        if (!triggeredPreview) {
            // Just force redraw with scaled (existing) textures for ultra-low-latency feedback
            m_scrollState->forceRedraw = true;
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
                // Ensure the viewport updates promptly after navigation
                scheduleVisibleRegeneration(false);
            } else {
                NavigateToNextSearchResult(*m_scrollState, m_pageHeights);
                // Ensure the viewport updates promptly after navigation
                scheduleVisibleRegeneration(false);
            }
        } else if (key == GLFW_KEY_F3) {
            // F3 for search navigation
            if (mods & GLFW_MOD_SHIFT) {
                NavigateToPreviousSearchResult(*m_scrollState, m_pageHeights);
                scheduleVisibleRegeneration(false);
            } else {
                NavigateToNextSearchResult(*m_scrollState, m_pageHeights);
                scheduleVisibleRegeneration(false);
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
                // Ensure crisp visible pages after keyboard scroll
                scheduleVisibleRegeneration(false);
            }
        } else if (key == GLFW_KEY_END) {
            // End key navigation
            if (mods & GLFW_MOD_CONTROL) {
                // Ctrl+End: Go to last page
                goToPage(getPageCount());
                // goToPage sets a visible regen flag; no extra call needed
            } else {
                // End: Go to bottom of document
                m_scrollState->scrollOffset = m_scrollState->maxOffset;
                m_scrollState->forceRedraw = true;
                scheduleVisibleRegeneration(false);
            }
        } else if (key == GLFW_KEY_PAGE_UP) {
            // Page Up: Scroll up by page height
            float pageHeight = m_windowHeight * 0.9f; // 90% of window height
            m_scrollState->scrollOffset = std::max(0.0f, m_scrollState->scrollOffset - pageHeight);
            m_scrollState->forceRedraw = true;
            scheduleVisibleRegeneration(false);
        } else if (key == GLFW_KEY_PAGE_DOWN) {
            // Page Down: Scroll down by page height
            float pageHeight = m_windowHeight * 0.9f; // 90% of window height
            m_scrollState->scrollOffset = std::min(m_scrollState->maxOffset, 
                                                    m_scrollState->scrollOffset + pageHeight);
            m_scrollState->forceRedraw = true;
            scheduleVisibleRegeneration(false);
        } else if (key == GLFW_KEY_UP) {
            // Arrow up: Fine scroll up
            float scrollAmount = 50.0f; // pixels
            m_scrollState->scrollOffset = std::max(0.0f, m_scrollState->scrollOffset - scrollAmount);
            m_scrollState->forceRedraw = true;
            scheduleVisibleRegeneration(false);
        } else if (key == GLFW_KEY_DOWN) {
            // Arrow down: Fine scroll down
            float scrollAmount = 50.0f; // pixels
            m_scrollState->scrollOffset = std::min(m_scrollState->maxOffset, 
                                                    m_scrollState->scrollOffset + scrollAmount);
            m_scrollState->forceRedraw = true;
            scheduleVisibleRegeneration(false);
        } else if (key == GLFW_KEY_LEFT) {
            // Arrow left: Horizontal scroll or previous page
            if (mods & GLFW_MOD_CONTROL) {
                previousPage();
            } else {
                HandleHorizontalScroll(*m_scrollState, -1.0f, (float)m_windowWidth);
                scheduleVisibleRegeneration(false);
            }
        } else if (key == GLFW_KEY_RIGHT) {
            // Arrow right: Horizontal scroll or next page
            if (mods & GLFW_MOD_CONTROL) {
                nextPage();
            } else {
                HandleHorizontalScroll(*m_scrollState, 1.0f, (float)m_windowWidth);
                scheduleVisibleRegeneration(false);
            }
        } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 && (mods & GLFW_MOD_CONTROL)) {
            // Ctrl+1-9: Quick zoom levels
            float zoomLevels[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 6.0f};
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
    // Refresh visible pages to reflect new selection location
    scheduleVisibleRegeneration(false);
    }
}

void PDFViewerEmbedder::findPrevious()
{
    if (m_scrollState) {
    NavigateToPreviousSearchResult(*m_scrollState, m_pageHeights);
    // Refresh visible pages to reflect new selection location
    scheduleVisibleRegeneration(false);
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

// Fresh search that resets any previous state and focuses first result
bool PDFViewerEmbedder::findTextFreshAndFocusFirst(const std::string& term)
{
    if (!m_scrollState || !m_pdfLoaded) return false;

    // 1) Clear any existing selection/search highlights and cached matches
    clearSearchHighlights();

    // 2) Set new term and request a fresh search
    m_scrollState->textSearch.searchTerm = term;
    m_scrollState->textSearch.needsUpdate = true;
    m_scrollState->textSearch.searchChanged = true;

    // Perform immediately to have results ready this frame
    PerformTextSearch(*m_scrollState, m_pageHeights, m_pageWidths);

    // 3) Navigate to first occurrence (if any)
    if (!m_scrollState->textSearch.results.empty()) {
        // Reset index then focus first result precisely
        m_scrollState->textSearch.currentResultIndex = 0;
        NavigateToSearchResultPrecise(*m_scrollState, m_pageHeights, 0);
        // Ensure visible textures refresh promptly
        scheduleVisibleRegeneration(false);
        return true;
    } else {
        // Force redraw so old highlights are not shown
        m_scrollState->forceRedraw = true;
        scheduleVisibleRegeneration(false);
        return false;
    }
}

void PDFViewerEmbedder::clearSearchHighlights()
{
    if (!m_scrollState) return;
    // Clear selection box too (avoid lingering selection tint)
    ClearTextSelection(*m_scrollState);
    // Reset search state including cached results and any PDFium handle
    ClearSearchResults(*m_scrollState);
    m_scrollState->textSearch.searchTerm.clear();
    m_scrollState->textSearch.currentResultIndex = 0;
    m_scrollState->textSearch.needsUpdate = false;
    m_scrollState->textSearch.searchChanged = false;

    // Targeted repaint: only visible pages need redraw to remove overlays
    int firstVisible=-1, lastVisible=-1;
    GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    if (firstVisible >= 0 && lastVisible >= firstVisible) {
        // No need to regenerate textures; overlays are drawn on top. Just force redraw.
        m_scrollState->forceRedraw = true;
    } else {
        m_scrollState->forceRedraw = true;
    }
}

// --- Async visible regeneration helpers ---
void PDFViewerEmbedder::scheduleVisibleRegeneration(bool settled) {
    if (!m_pdfLoaded || !m_asyncQueue) return;

    // Debounce progressive (non-settled) regeneration to at most ~1 every 55 ms
    double now = glfwGetTime();
    if (!settled) {
        const double DEBOUNCE_INTERVAL = 0.055; // ~18 fps ceiling while interacting
        if (now - m_lastPreviewRegenTime < DEBOUNCE_INTERVAL) {
            return; // Skip scheduling too soon
        }
        m_lastPreviewRegenTime = now;
    } else {
        // Record time of high quality regen to optionally gate follow-ups
        m_lastHighQualityNavigationTime = now;
    }

    // Compute visible range
    int firstVisible = -1, lastVisible = -1;
    GetVisiblePageRange(*m_scrollState, m_pageHeights, firstVisible, lastVisible);
    if (firstVisible < 0 || lastVisible < firstVisible) return;

    // Bump generation for each scheduling to cancel stale results
    int gen = ++m_generation;

    // Build tasks for visible pages only
    std::vector<PageRenderTask> tasks;
    int priority = 0;
    // When not settled, limit progressive pass to center-most 2 pages to reduce GPU churn
    int progressiveFirst = firstVisible;
    int progressiveLast = lastVisible;
    if (!settled) {
        int mid = (firstVisible + lastVisible) / 2;
        progressiveFirst = std::max(firstVisible, mid - 1);
        progressiveLast = std::min(lastVisible, mid + 1);
    }

    for (int i = firstVisible; i <= lastVisible; ++i) {
        if (!settled && (i < progressiveFirst || i > progressiveLast)) {
            continue; // Skip less important pages in preview passes
        }
        // Choose target pixel size based on current zoom and page original size
        double pw = m_originalPageWidths[i];
        double ph = m_originalPageHeights[i];
        float zoom = m_scrollState->zoomScale;

        // Progressive quality: while not settled (gesture ongoing), cap to a preview size
        float quality = 1.0f;
        if (!settled) {
            // Cap max dimension relative to window to keep wheel zoom snappy (reduced to ~window size)
            const int windowMax = std::max(m_windowWidth, m_windowHeight);
            const int PREVIEW_MAX = std::max(256, std::min(windowMax, // <= window dimension
                                                           (m_glMaxTextureSize > 0 ? m_glMaxTextureSize - 64 : 4096)));
            const double desiredMax = std::max(pw * zoom, ph * zoom);
            if (desiredMax > 0.0) {
                quality = std::min(1.0, (double)PREVIEW_MAX / desiredMax);
                // Avoid too blurry preview
                quality = std::max(quality, 0.3f);
            }
        }

        int w = std::max(8, (int)std::lround(pw * zoom * quality));
        int h = std::max(8, (int)std::lround(ph * zoom * quality));

        // Near-size skip: if existing texture within 8% of desired in both dimensions, skip unless settled high quality
        if (i >= 0 && i < (int)m_textureWidths.size()) {
            int existingW = m_textureWidths[i];
            int existingH = m_textureHeights[i];
            if (existingW > 0 && existingH > 0) {
                float dw = std::abs(existingW - w) / (float)std::max(w, 1);
                float dh = std::abs(existingH - h) / (float)std::max(h, 1);
                if (!settled && dw < 0.08f && dh < 0.08f) {
                    continue; // Texture close enough during interaction
                }
            }
        }

        // Clamp texture size to avoid oversize allocations based on runtime GL limit
        const int MAX_DIM = (m_glMaxTextureSize > 0 ? m_glMaxTextureSize - 64 : 8192);
        if (w > MAX_DIM) { float s = (float)MAX_DIM / w; w = MAX_DIM; h = std::max(1, (int)(h * s)); }
        if (h > MAX_DIM) { float s = (float)MAX_DIM / h; h = MAX_DIM; w = std::max(1, (int)(w * s)); }

    tasks.push_back(PageRenderTask{ i, w, h, gen, priority++, !settled });
    }

    m_asyncQueue->submit(std::move(tasks), gen);
}

void PDFViewerEmbedder::processAsyncResults() {
    if (!m_asyncQueue) return;
    auto results = m_asyncQueue->drainResults();
    if (results.empty()) return;

    // Upload results to GL textures
    glfwMakeContextCurrent(m_glfwWindow);
    for (auto& r : results) {
        // Drop stale generation results (e.g., from an older zoom / resize / tab activation)
        // to avoid overwriting with wrong-sized textures or resurrecting freed pages.
        if (r.generation != m_generation.load()) {
            continue;
        }
        // Recreate page texture at new size for simplicity (could be sub-image into atlas)
        if (r.pageIndex < 0 || r.pageIndex >= (int)m_textures.size()) continue;
        if (m_textures[r.pageIndex]) glDeleteTextures(1, &m_textures[r.pageIndex]);

        GLuint tex=0; glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        if (r.preview) {
            // Cheaper filtering & no mipmaps for preview to speed interaction
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r.width, r.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, r.bgra.data());
        if (!r.preview) glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_textures[r.pageIndex] = tex;

        // Record the uploaded texture size for skip / reuse heuristics
        if (r.pageIndex >= 0 && r.pageIndex < (int)m_textureWidths.size()) {
            m_textureWidths[r.pageIndex] = r.width;
        }
        if (r.pageIndex >= 0 && r.pageIndex < (int)m_textureHeights.size()) {
            m_textureHeights[r.pageIndex] = r.height;
        }

    // Keep base page size unchanged; zoom applied in draw. Original page
    // size tracked separately; here we only update current texture dims.
    }
}
