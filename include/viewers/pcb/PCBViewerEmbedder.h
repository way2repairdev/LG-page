#pragma once

#include <memory>
#include <string>
#include <functional>

// Forward declarations
struct GLFWwindow;
class PCBRenderer;
class BRDFileBase;
struct BRDPin;

/**
 * PCBViewerEmbedder - Embeds the standalone PCB viewer into Qt widgets
 * 
 * This class acts as a bridge between the standalone GLFW-based PCB viewer
 * and the Qt widget system, similar to how PDFViewerEmbedder works.
 */
class PCBViewerEmbedder {
public:
    // Callback types for Qt widget communication
    using ErrorCallback = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(const std::string&)>;
    using PinSelectedCallback = std::function<void(const std::string&, const std::string&)>; // pin name, net name
    using ZoomCallback = std::function<void(double)>; // zoom level

    PCBViewerEmbedder();
    ~PCBViewerEmbedder();

    // Core initialization and cleanup
    bool initialize(void* parentWindowHandle, int width, int height);
    void cleanup();
    bool isInitialized() const { return m_initialized; }

    // File operations
    bool loadPCB(const std::string& filePath);
    void closePCB();
    bool isPCBLoaded() const { return m_pdfLoaded; }
    std::string getCurrentFilePath() const { return m_currentFilePath; }

    // Viewer operations
    void render();
    void resize(int width, int height);
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void resetView();
    void pan(float deltaX, float deltaY);
    void zoom(float factor, float centerX = -1, float centerY = -1);

    // Mouse and keyboard input
    void handleMouseMove(int x, int y);
    void handleMouseClick(int x, int y, int button);
    void handleMouseRelease(int x, int y, int button);
    void handleMouseScroll(double xOffset, double yOffset);
    void handleKeyPress(int key, int scancode, int action, int mods);

    // Selection and interaction
    void clearSelection();
    bool hasSelection() const;
    std::string getSelectedPinInfo() const;
    void highlightNet(const std::string& netName);
    void clearHighlights();

    // Layer management
    void showLayer(const std::string& layerName, bool visible);
    void showAllLayers();
    void hideAllLayers();
    std::vector<std::string> getLayerNames() const;

    // Component operations
    void highlightComponent(const std::string& reference);
    std::vector<std::string> getComponentList() const;

    // View state
    double getZoomLevel() const;
    void setZoomLevel(double zoom);
    void getCameraPosition(float& x, float& y) const;
    void setCameraPosition(float x, float y);

    // Callbacks for Qt widget integration
    void setErrorCallback(ErrorCallback callback) { m_errorCallback = callback; }
    void setStatusCallback(StatusCallback callback) { m_statusCallback = callback; }
    void setPinSelectedCallback(PinSelectedCallback callback) { m_pinSelectedCallback = callback; }
    void setZoomCallback(ZoomCallback callback) { m_zoomCallback = callback; }

    // Window management
    void show();
    void hide();
    void setVisible(bool visible);
    bool isVisible() const { return m_visible; }

    // Fallback mode (Qt-only rendering when GLFW fails)
    bool isUsingFallback() const { return m_usingFallback; }
    void enableFallbackMode();

private:
    // GLFW window management
    GLFWwindow* m_glfwWindow;
    void* m_parentHwnd;
    void* m_childHwnd;

    // Core PCB viewer components
    std::unique_ptr<PCBRenderer> m_renderer;
    std::shared_ptr<BRDFileBase> m_pcbData;

    // State
    bool m_initialized;
    bool m_pdfLoaded; // Keep same name for compatibility
    bool m_usingFallback;
    bool m_visible;
    std::string m_currentFilePath;

    // Window dimensions
    int m_windowWidth;
    int m_windowHeight;

    // Mouse state for interaction
    double m_lastMouseX;
    double m_lastMouseY;
    bool m_mouseDragging;

    // Callbacks
    ErrorCallback m_errorCallback;
    StatusCallback m_statusCallback;
    PinSelectedCallback m_pinSelectedCallback;
    ZoomCallback m_zoomCallback;

    // Internal methods
    bool initializeGLFW(void* parentHandle, int width, int height);
    bool initializeRenderer();
    void setupCallbacks();
    bool createSamplePCB(); // For testing when no file is loaded
    void displayPinHoverInfo(); // Display pin hover and selection info - matching main.cpp
    void handleError(const std::string& error);
    void handleStatus(const std::string& status);
    void onPinSelected(int pinIndex);
    void onZoomChanged();

    // GLFW callback wrappers
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
};
