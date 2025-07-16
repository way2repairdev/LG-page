#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include <vector>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// Forward declarations for your existing PDF viewer components
class PDFRenderer;
struct PDFScrollState;
class MenuIntegration;

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
    void zoomToFit();
    void setZoom(float zoomLevel);
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();

    /**
     * Get current viewer state
     */
    float getCurrentZoom() const;
    int getCurrentPage() const;

    /**
     * Text operations
     */
    std::string getSelectedText() const;
    void clearSelection();
    bool findText(const std::string& searchTerm);
    void findNext();
    void findPrevious();

    /**
     * Set focus to the viewer (for keyboard input)
     */
    void setFocus();

private:
    // Core components from your existing viewer
    GLFWwindow* m_glfwWindow;
    HWND m_parentHwnd;
    HWND m_childHwnd;
    
    // PDF rendering components (reusing your existing classes)
    std::unique_ptr<PDFRenderer> m_renderer;
    std::unique_ptr<PDFScrollState> m_scrollState;
    std::unique_ptr<MenuIntegration> m_menuIntegration;
    
    // OpenGL state
    std::vector<unsigned int> m_textures;
    std::vector<int> m_pageWidths;
    std::vector<int> m_pageHeights;
    std::vector<double> m_originalPageWidths;
    std::vector<double> m_originalPageHeights;
    
    // Viewer state
    bool m_initialized;
    bool m_pdfLoaded;
    bool m_usingFallback;  // True when using Qt fallback instead of PDFium
    int m_windowWidth;
    int m_windowHeight;
    std::string m_currentFilePath;
    
    // Rendering state management
    bool m_needsFullRegeneration;
    bool m_needsVisibleRegeneration;
    int m_lastWinWidth;
    int m_lastWinHeight;
    
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
};
