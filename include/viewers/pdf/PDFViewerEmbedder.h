#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include <vector>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <atomic>

#include "viewers/pdf/AsyncRender.h"
#include <functional>

// Forward declarations for your existing PDF viewer components
class PDFRenderer;
struct PDFScrollState;
class MenuIntegration;
class OpenGLPipelineManager;

/**
 * PDFViewerEmbedder - Wraps the existing standalone PDF viewer for embedding in Qt applications
 * 
 * This class encapsulates all the OpenGL rendering, GLFW window management, and PDF viewing
 * functionality into a reusable component that can be embedded within a Qt widget's native window.
 * 
 * Key features preserved from standalone viewer:
 * - High-performance PDFium rendering with OpenGL
 * - Zooming, panning, scrolling
 * - Text selection and search
 * - Background rendering for smooth performance
 * - All existing keyboard/mouse interactions
 */
class PDFViewerEmbedder {
public:
    PDFViewerEmbedder();
    ~PDFViewerEmbedder();

    /**
     * Initialize the PDF viewer within a parent window
     * @param parentHwnd Native Windows handle from Qt (e.g., reinterpret_cast<HWND>(widget->winId()))
     * @param width Initial width of the viewer area
     * @param height Initial height of the viewer area
     * @return true if initialization succeeded
     */
    bool initialize(HWND parentHwnd, int width, int height);

    /**
     * Load a PDF file into the viewer
     * @param filePath Full path to the PDF file
     * @return true if the PDF was loaded successfully
     */
    bool loadPDF(const std::string& filePath);

    /**
     * Load a PDF from memory buffer (for secure AWS files)
     * @param data PDF file contents as byte buffer
     * @param size Size of the buffer
     * @param displayName Optional display name for the PDF (used in UI)
     * @return true if the PDF was loaded successfully
     */
    bool loadPDFFromMemory(const char* data, size_t size, const std::string& displayName = "");

    /**
     * Update the viewer - call this from Qt's timer or paint event
     * This replaces the main rendering loop from the standalone application
     */
    void update();

    /**
     * Handle window resize events
     * @param width New width
     * @param height New height
     */
    void resize(int width, int height);

    /**
     * Clean shutdown of the viewer
     */
    void shutdown();

    /**
     * Check if a PDF is currently loaded
     */
    bool isPDFLoaded() const { return m_pdfLoaded; }

    /**
     * Get the current page count
     */
    int getPageCount() const;

    /**
     * Navigation methods
     */
    void zoomIn();
    void zoomOut();
    void setZoom(float zoomLevel);
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();
    
    /**
     * Rotation methods - rotate all pages
     */
    void rotateLeft();
    void rotateRight();

    /**
     * Get current viewer state
     */
    float getCurrentZoom() const;
    int getCurrentPage() const;

    // Lightweight snapshot of current viewport state (for tab switch resume)
    struct ViewState {
        float zoom {1.0f};
        float scrollOffset {0.0f};
        float horizontalOffset {0.0f};
        int   page {1};
        bool  valid {false};
    };
    ViewState captureViewState() const;
    void restoreViewState(const ViewState &state);

    /**
     * Text operations
     */
    std::string getSelectedText() const;
    void clearSelection();
    bool findText(const std::string& searchTerm);
    void findNext();
    void findPrevious();
    
    /**
     * Text search information
     */
    int countTextOccurrences(const std::string& searchTerm) const;
    int getCurrentSearchResultIndex() const;

    // --- Fresh search helpers for cross-viewer integration ---
    // Clears old highlights/state and performs a brand new search for term,
    // immediately focusing the first match (if any). Returns true if matches found.
    bool findTextFreshAndFocusFirst(const std::string& term);
    // Optimized version for cross-search: defers expensive regeneration to prevent cursor lag
    bool findTextFreshAndFocusFirstOptimized(const std::string& term);
    // Clears all search highlights/handles/state and triggers a light repaint.
    void clearSearchHighlights();

    // Search options
    void setWholeWordSearch(bool enabled);
    bool wholeWordSearch() const { return m_wholeWordSearch; }

    /**
     * Force this viewer to become the active global PDF context and schedule a
     * visible regeneration. Use this when switching from external views (e.g.,
     * PCB -> PDF cross-search) to ensure crisp content immediately.
     * @param highQuality If true, schedule a settled high-quality visible regen.
     */
    void activateForCrossSearchAndRefresh(bool highQuality = true);

    /**
     * Set focus to the viewer (for keyboard input)
     */
    void setFocus();

    // --- Performance / memory tuning APIs ---
    // Enable/disable mipmap generation for full-quality page textures (saves ~33% memory when off).
    void setTextureMipmapsEnabled(bool enabled) { m_enableMipmaps = enabled; }
    bool mipmapsEnabled() const { return m_enableMipmaps; }
    // Set how many pages beyond the visible range are eagerly rendered (on each side).
    // 0 = only visible pages. Higher numbers improve scroll smoothness at cost of memory.
    void setPreloadPageMargin(int margin) { m_preloadPageMargin = std::max(0, margin); }
    int preloadPageMargin() const { return m_preloadPageMargin; }
    // Adjust memory budget (in megabytes) for all page textures in this viewer.
    void setMemoryBudgetMB(size_t mb) { m_memoryBudgetBytes = mb * 1024ull * 1024ull; }
    size_t memoryBudgetMB() const { return m_memoryBudgetBytes / 1024ull / 1024ull; }

private:
    // Core components from your existing viewer
    GLFWwindow* m_glfwWindow;
    HWND m_parentHwnd;
    HWND m_childHwnd;
    
    // PDF rendering components (reusing your existing classes)
    std::unique_ptr<PDFRenderer> m_renderer;
    std::unique_ptr<PDFScrollState> m_scrollState;
    std::unique_ptr<MenuIntegration> m_menuIntegration;
    std::unique_ptr<OpenGLPipelineManager> m_pipelineManager;
    
    // OpenGL state
    std::vector<unsigned int> m_textures;
    // Track current GL texture pixel dimensions per page for skip logic
    std::vector<int> m_textureWidths;  // 0 if not created
    std::vector<int> m_textureHeights; // 0 if not created
    std::vector<int> m_pageWidths;
    std::vector<int> m_pageHeights;
    std::vector<double> m_originalPageWidths;
    std::vector<double> m_originalPageHeights;
    // Per-texture byte sizes (RGBA) to track memory usage
    std::vector<size_t> m_textureByteSizes;

    // Memory budgeting (helps prevent OOM / driver resets when multiple tabs active)
    size_t m_memoryBudgetBytes = 256ull * 1024ull * 1024ull; // 256 MB default budget for all page textures in this viewer
    size_t m_currentTextureBytes = 0;                         // Tracked allocated texture bytes
    bool   m_budgetDownscaleApplied = false;                  // Flag to indicate textures were downscaled due to budget
    bool   m_enableMipmaps = false;                           // Disable by default for memory savings
    int    m_preloadPageMargin = 1;                           // Eagerly render 1 page before/after visible by default
    
    // Viewer state
    bool m_initialized;
    bool m_pdfLoaded;
    bool m_usingFallback;  // True when using Qt fallback instead of PDFium
    int m_windowWidth;
    int m_windowHeight;
    std::string m_currentFilePath;

    // Diagnostics
    long long m_viewerId = 0;                 // Unique id per viewer instance
    static std::atomic<long long> s_nextViewerId; // Generator
    bool isActiveGlobal() const;              // Whether global pointers map to this viewer
    void logContextMismatch(const char* where) const; // Helper
    
    // Rendering state management
    bool m_needsFullRegeneration;
    bool m_needsVisibleRegeneration;
    int m_lastWinWidth;
    int m_lastWinHeight;

    // GL capabilities
    int m_glMaxTextureSize = 0;

    // Throttling for regen while interacting
    double m_lastPanRegenTime = 0.0;
    double m_lastScrollRegenTime = 0.0;
    double m_lastPreviewRegenTime = 0.0; // debounce rapid progressive zoom regenerations
    double m_lastHighQualityNavigationTime = 0.0; // avoid immediate double high-quality passes

    // Async rendering state
    std::unique_ptr<AsyncRenderQueue> m_asyncQueue;
    std::atomic<int> m_generation{0};

    // Helpers for async visible regeneration
    void scheduleVisibleRegeneration(bool settled);
    void processAsyncResults();
    
    // Private helper methods
    bool createEmbeddedWindow();
    bool initializeOpenGL();
    void setupCallbacks();
    void renderFrame();
    void updateScrollState();
    void regenerateTextures();
    void regenerateVisibleTextures();
    void regeneratePageTexture(int pageIndex);
    void handleBackgroundRendering();
    void cleanupTextures();
    unsigned int createTextureFromPDFBitmap(void* bitmap, int width, int height);
    void trackTextureAllocation(size_t oldBytes, size_t newBytes, int index);
    void enforceMemoryBudget();
    float computeAdaptiveZoomForBudget(double originalW, double originalH, float requestedZoom, size_t pendingBytes) const;
    
    // Texture optimization helpers
    float getOptimalTextureZoom(float currentZoom) const;
    
    // Callback wrapper methods (static functions that call instance methods)
    static void windowSizeCallback(GLFWwindow* window, int width, int height);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    
    // Instance callback handlers
    void onWindowSize(int width, int height);
    void onCursorPos(double xpos, double ypos);
    void onMouseButton(int button, int action, int mods);
    void onScroll(double xoffset, double yoffset);
    void onKey(int key, int scancode, int action, int mods);

    // --- Quick right-click hook for Qt context menu integration ---
public:
    void setQuickRightClickCallback(const std::function<void(const std::string &selectedText)> &cb) { m_quickRightClickCallback = cb; }
private:
    // Pending GL uploads queue to cap per-frame texture upload work
    std::vector<PageRenderResult> m_pendingGLUploads;
    double m_rightPressTime {0.0};
    double m_rightPressX {0.0};
    double m_rightPressY {0.0};
    bool   m_rightMoved {false};
    std::function<void(const std::string&)> m_quickRightClickCallback;

    // Ensure global viewer pointers point to this instance before executing actions
    void ensureActiveGlobals();

    // Search options
    bool m_wholeWordSearch { false }; // default: substring search until user toggles
};
