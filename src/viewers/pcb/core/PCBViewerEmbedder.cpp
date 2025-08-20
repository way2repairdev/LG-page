#include "viewers/pcb/PCBViewerEmbedder.h"

// Include PCB viewer components - using relative paths like the PCB main.cpp
#include "../rendering/PCBRenderer.h"
#include "../format/BRDFileBase.h"
#include "../format/XZZPCBFile.h"
#include "../core/BRDTypes.h"
#include "../core/Utils.h"

// GLFW includes
#include <GLFW/glfw3.h>
#include <GL/glew.h>

#ifdef _WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

// ImGui includes for UI overlay (if needed)
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <algorithm>
#include <cmath>
#include <mutex>

PCBViewerEmbedder::PCBViewerEmbedder()
    : m_glfwWindow(nullptr)
    , m_parentHwnd(nullptr)
    , m_childHwnd(nullptr)
    , m_imguiContext(nullptr)
    , m_imguiUIEnabled(false)
    , m_renderer(std::make_unique<PCBRenderer>())
    , m_pcbData(nullptr)
    , m_initialized(false)
    , m_pdfLoaded(false) // Keep same name for compatibility
    , m_usingFallback(false)
    , m_visible(false)
    , m_currentFilePath("")
    , m_windowWidth(800)
    , m_windowHeight(600)
    , m_lastMouseX(0.0)
    , m_lastMouseY(0.0)
    , m_mouseDragging(false)
    , m_errorCallback(nullptr)
    , m_statusCallback(nullptr)
    , m_pinSelectedCallback(nullptr)
    , m_zoomCallback(nullptr)
{
}

PCBViewerEmbedder::~PCBViewerEmbedder()
{
    cleanup();
}

bool PCBViewerEmbedder::initialize(void* parentWindowHandle, int width, int height)
{
    if (m_initialized) {
        handleStatus("PCB viewer already initialized");
        return true;
    }

    m_parentHwnd = parentWindowHandle;
    m_windowWidth = width;
    m_windowHeight = height;

    handleStatus("Initializing PCB viewer embedder...");

    // Try to initialize GLFW first
    if (!initializeGLFW(parentWindowHandle, width, height)) {
        handleError("Failed to initialize GLFW, falling back to Qt-only mode");
        enableFallbackMode();
        return true; // Still successful, just in fallback mode
    }

    // Initialize renderer
    if (!initializeRenderer()) {
        handleError("Failed to initialize PCB renderer, falling back to Qt-only mode");
        enableFallbackMode();
        return true; // Still successful, just in fallback mode
    }

    // Set up GLFW callbacks
    setupCallbacks();

    
    m_initialized = true;
    handleStatus("PCB viewer embedder initialized successfully");
    return true;
}

void PCBViewerEmbedder::cleanup()
{
    if (!m_initialized) {
        return;
    }

    handleStatus("Cleaning up PCB viewer embedder...");

    // Cleanup ImGui with proper context isolation
    if (m_glfwWindow && m_imguiContext) {
        glfwMakeContextCurrent(m_glfwWindow);
        
        // Set context before cleanup
        ImGui::SetCurrentContext(m_imguiContext);
        
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        
        // Destroy our specific context
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext = nullptr;
    }

    // Cleanup renderer
    if (m_renderer) {
        m_renderer->Cleanup();
    }

    // Cleanup GLFW window (don't terminate GLFW as other tabs may be using it)
    if (m_glfwWindow) {
        glfwDestroyWindow(m_glfwWindow);
        m_glfwWindow = nullptr;
    }

    // Manage GLFW termination with static counter
    static int s_instanceCount = 0;
    static std::mutex s_glfwMutex;
    
    {
        std::lock_guard<std::mutex> lock(s_glfwMutex);
        s_instanceCount--;
        
        // Only terminate GLFW when last instance is cleaned up
        if (s_instanceCount <= 0) {
            glfwTerminate();
            handleStatus("GLFW terminated (last PCB instance)");
        } else {
            handleStatus("GLFW kept alive (other PCB instances active)");
        }
    }

    m_initialized = false;
    m_pdfLoaded = false;
    m_pcbData.reset();
    
    handleStatus("PCB viewer embedder cleaned up");
}

bool PCBViewerEmbedder::loadPCB(const std::string& filePath)
{
    handleStatus("Loading PCB file: " + filePath);

    if (m_usingFallback) {
        handleError("PCB loading not supported in fallback mode");
        return false;
    }

    try {
        // Load PCB file using XZZPCBFile
        auto pcbFile = XZZPCBFile::LoadFromFile(filePath);
        if (!pcbFile) {
            handleError("Failed to load PCB file: " + filePath);
            return false;
        }

        // Convert to shared_ptr<BRDFileBase>
        m_pcbData = std::shared_ptr<BRDFileBase>(pcbFile.release());
        
        if (m_renderer) {
            m_renderer->SetPCBData(m_pcbData);
            m_renderer->ZoomToFit(m_windowWidth, m_windowHeight);
        }

        m_currentFilePath = filePath;
        m_pdfLoaded = true;

        handleStatus("PCB file loaded successfully: " + filePath);
        return true;
    }
    catch (const std::exception& e) {
        handleError("Exception while loading PCB: " + std::string(e.what()));
        return false;
    }
}

void PCBViewerEmbedder::closePCB()
{
    if (!m_pdfLoaded) {
        return;
    }

    handleStatus("Closing PCB file");
    
    m_pcbData.reset();
    m_pdfLoaded = false;
    m_currentFilePath.clear();
    
        
    handleStatus("PCB file closed");
}

void PCBViewerEmbedder::render()
{
    if (!m_initialized || m_usingFallback || !m_glfwWindow || !m_imguiContext) {
        return;
    }

    if (!m_visible) {
        return;
    }

    // Poll GLFW events first - CRITICAL: This was missing!
    glfwPollEvents();

    // Make the context current - essential for OpenGL rendering
    glfwMakeContextCurrent(m_glfwWindow);
    
    // Set ImGui context for this tab (prevents cross-tab conflicts)
    ImGui::SetCurrentContext(m_imguiContext);

    // Clear the framebuffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Start ImGui frame - matching main.cpp sequence
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Render the PCB using PCBRenderer - matching main.cpp
    if (m_renderer) {
        m_renderer->Render(m_windowWidth, m_windowHeight);
    }

    // Display hover information like in main.cpp - this was missing!
    displayPinHoverInfo();

    // Render ImGui - matching main.cpp sequence
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Swap buffers - matching main.cpp
    glfwSwapBuffers(m_glfwWindow);
}

void PCBViewerEmbedder::resize(int width, int height)
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

    // After viewport size changes, adjust camera horizontally to avoid gaps
    if (m_renderer && m_pdfLoaded && m_pcbData) {
        const auto& cam = m_renderer->GetCamera();
        float zoom = cam.zoom;
        if (zoom < 0.01f) zoom = 0.01f;

        BRDPoint min_pt{}, max_pt{};
        m_pcbData->GetBoundingBox(min_pt, max_pt);
        float pcb_min_x = static_cast<float>(min_pt.x);
        float pcb_max_x = static_cast<float>(max_pt.x);
        float pcb_width = pcb_max_x - pcb_min_x;

        if (pcb_width > 0.0f) {
            float half_view_world = static_cast<float>(width) * 0.5f / zoom;
            float cam_x = cam.x;
            float cam_y = cam.y;
            float cam_zoom = cam.zoom;

            if (pcb_width <= 2.0f * half_view_world) {
                // Content narrower than viewport: center exactly
                float center_x = (pcb_min_x + pcb_max_x) * 0.5f;
                m_renderer->SetCamera(center_x, cam_y, cam_zoom);
            } else {
                // Wider than viewport: clamp camera.x within valid horizontal bounds
                float min_cam_x = pcb_min_x + half_view_world;
                float max_cam_x = pcb_max_x - half_view_world;
                if (cam_x < min_cam_x) cam_x = min_cam_x;
                if (cam_x > max_cam_x) cam_x = max_cam_x;
                m_renderer->SetCamera(cam_x, cam_y, cam_zoom);
            }
        }
    }

    handleStatus("PCB viewer resized to " + std::to_string(width) + "x" + std::to_string(height));
}

void PCBViewerEmbedder::zoomIn()
{
    if (m_renderer) {
        // Get current camera center for zoom
        const auto& camera = m_renderer->GetCamera();
        m_renderer->Zoom(1.2f, camera.x, camera.y);
        onZoomChanged();
    }
}

void PCBViewerEmbedder::zoomOut()
{
    if (m_renderer) {
        const auto& camera = m_renderer->GetCamera();
        m_renderer->Zoom(0.8f, camera.x, camera.y);
        onZoomChanged();
    }
}

void PCBViewerEmbedder::zoomToFit()
{
    if (m_renderer) {
        m_renderer->ZoomToFit(m_windowWidth, m_windowHeight);
        onZoomChanged();
    }
}

void PCBViewerEmbedder::resetView()
{
    zoomToFit();
}

void PCBViewerEmbedder::pan(float deltaX, float deltaY)
{
    if (m_renderer) {
        m_renderer->Pan(deltaX, deltaY);
    }
}

void PCBViewerEmbedder::zoom(float factor, float centerX, float centerY)
{
    if (m_renderer) {
        if (centerX < 0 || centerY < 0) {
            // Use current camera center
            const auto& camera = m_renderer->GetCamera();
            m_renderer->Zoom(factor, camera.x, camera.y);
        } else {
            m_renderer->Zoom(factor, centerX, centerY);
        }
        onZoomChanged();
    }
}

void PCBViewerEmbedder::rotateLeft()
{
    if (m_renderer) {
        m_renderer->RotateLeft();
        onZoomChanged(); // zoom may have been refitted
    }
}

void PCBViewerEmbedder::rotateRight()
{
    if (m_renderer) {
        m_renderer->RotateRight();
        onZoomChanged();
    }
}

int PCBViewerEmbedder::getRotationSteps() const
{
    return m_renderer ? m_renderer->GetRotationSteps() : 0;
}

void PCBViewerEmbedder::flipHorizontal()
{
    if (m_renderer) {
        m_renderer->ToggleFlipHorizontal();
        onZoomChanged();
    }
}

void PCBViewerEmbedder::flipVertical()
{
    if (m_renderer) {
        m_renderer->ToggleFlipVertical();
        onZoomChanged();
    }
}

bool PCBViewerEmbedder::isFlipHorizontal() const { return m_renderer ? m_renderer->IsFlipHorizontal() : false; }
bool PCBViewerEmbedder::isFlipVertical() const { return m_renderer ? m_renderer->IsFlipVertical() : false; }

void PCBViewerEmbedder::toggleDiodeReadings()
{
    if (m_renderer) {
        m_renderer->ToggleDiodeReadings();
    }
}

void PCBViewerEmbedder::setDiodeReadingsEnabled(bool enabled)
{
    if (m_renderer) {
        m_renderer->SetDiodeReadingsEnabled(enabled);
    }
}

bool PCBViewerEmbedder::isDiodeReadingsEnabled() const
{
    return m_renderer ? m_renderer->IsDiodeReadingsEnabled() : false;
}

void PCBViewerEmbedder::handleMouseMove(int x, int y)
{
    if (m_renderer) {
        // Update hover state - use the callback parameters for accuracy
        int hoveredPin = m_renderer->GetHoveredPin(static_cast<float>(x), static_cast<float>(y), 
                                                  m_windowWidth, m_windowHeight);
        m_renderer->SetHoveredPin(hoveredPin);
    }

    // Handle panning if dragging - use the callback parameters for consistent delta calculation
    if (m_mouseDragging) {
        double dx = x - m_lastMouseX;
        double dy = y - m_lastMouseY;
        pan(static_cast<float>(-dx), static_cast<float>(dy));
    }

    // Update last mouse position AFTER panning calculation
    m_lastMouseX = x;
    m_lastMouseY = y;
}

void PCBViewerEmbedder::handleMouseClick(int x, int y, int button)
{
    if (m_renderer && (button == 0 || button == 1)) { // Left click or Right click
        // Handle pin/part selection for both left and right clicks
        m_renderer->HandleMouseClick(static_cast<float>(x), static_cast<float>(y),
                                   m_windowWidth, m_windowHeight);
        
        // Check if a pin was selected
        if (m_renderer->HasSelectedPin()) {
            int selectedPin = m_renderer->GetSelectedPinIndex();
            onPinSelected(selectedPin);
        }
        // Check if a part was highlighted (selected)
        else if (m_renderer->GetHighlightedPart() >= 0) {
            int highlightedPart = m_renderer->GetHighlightedPart();
            onPartSelected(highlightedPart);
        }
    }
    
    if (button == 1) { // Right click - start panning
        m_mouseDragging = true;
        m_lastMouseX = x;
        m_lastMouseY = y;
    }
}

void PCBViewerEmbedder::handleMouseRelease(int x, int y, int button)
{
    (void)x; (void)y; // suppress unused warnings
    if (button == 1) { // Right click release - stop panning
        m_mouseDragging = false;
    }
}

void PCBViewerEmbedder::handleMouseScroll(double xOffset, double yOffset)
{
    (void)xOffset; // horizontal unused
    if (m_renderer) {
        // Get real-time mouse position for zoom center - this is important for zoom-to-cursor functionality
        double mouseX, mouseY;
        if (m_glfwWindow) {
            glfwGetCursorPos(m_glfwWindow, &mouseX, &mouseY);
        } else {
            mouseX = m_lastMouseX;
            mouseY = m_lastMouseY;
        }
        
        // Get mouse world position for zoom center
        const auto& camera = m_renderer->GetCamera();
        float mouseWorldX = camera.x + (static_cast<float>(mouseX) - m_windowWidth * 0.5f) / camera.zoom;
        float mouseWorldY = camera.y + (m_windowHeight * 0.5f - static_cast<float>(mouseY)) / camera.zoom;
        
        float zoomFactor = 1.0f + static_cast<float>(yOffset) * 0.1f;
        m_renderer->Zoom(zoomFactor, mouseWorldX, mouseWorldY);
        onZoomChanged();
    }
}

void PCBViewerEmbedder::handleKeyPress(int key, int scancode, int action, int mods)
{
    (void)scancode; (void)mods;
    // Handle keyboard shortcuts
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
            case GLFW_KEY_R:
                resetView();
                break;
            case GLFW_KEY_EQUAL:
            case GLFW_KEY_KP_ADD:
                zoomIn();
                break;
            case GLFW_KEY_MINUS:
            case GLFW_KEY_KP_SUBTRACT:
                zoomOut();
                break;
        }
    }
}

void PCBViewerEmbedder::clearSelection()
{
    if (m_renderer) {
        m_renderer->ClearSelection();
    }
}

std::string PCBViewerEmbedder::getSelectedPinNet() const {
    if (!m_renderer || !m_pcbData) return {};
    if (!m_renderer->HasSelectedPin()) return {};
    int idx = m_renderer->GetSelectedPinIndex();
    if (idx < 0 || idx >= (int)m_pcbData->pins.size()) return {};
    return m_pcbData->pins[idx].net;
}

std::string PCBViewerEmbedder::getSelectedPinPart() const {
    if (!m_renderer || !m_pcbData) return {};
    if (!m_renderer->HasSelectedPin()) return {};
    int idx = m_renderer->GetSelectedPinIndex();
    if (idx < 0 || idx >= (int)m_pcbData->pins.size()) return {};
    int partIndex = (int)m_pcbData->pins[idx].part - 1; // parts 1-indexed
    if (partIndex < 0 || partIndex >= (int)m_pcbData->parts.size()) return {};
    return m_pcbData->parts[partIndex].name;
}

std::string PCBViewerEmbedder::getHighlightedPartName() const {
    if (!m_renderer || !m_pcbData) return {};
    int partIndex = m_renderer->GetHighlightedPart();
    if (partIndex < 0 || partIndex >= (int)m_pcbData->parts.size()) return {};
    return m_pcbData->parts[partIndex].name;
}

bool PCBViewerEmbedder::hasSelection() const
{
    return m_renderer ? m_renderer->HasSelectedPin() : false;
}

std::string PCBViewerEmbedder::getSelectedPinInfo() const
{
    if (!m_renderer || !m_renderer->HasSelectedPin() || !m_pcbData) {
        return "";
    }

    int selectedPin = m_renderer->GetSelectedPinIndex();
    if (selectedPin >= 0 && selectedPin < static_cast<int>(m_pcbData->pins.size())) {
        const auto& pin = m_pcbData->pins[selectedPin];
        return "Pin: " + pin.name + " Net: " + pin.net;
    }

    return "";
}

void PCBViewerEmbedder::show()
{
    m_visible = true;
    if (m_glfwWindow) {
        glfwShowWindow(m_glfwWindow);
    }
}

void PCBViewerEmbedder::hide()
{
    m_visible = false;
    if (m_glfwWindow) {
        glfwHideWindow(m_glfwWindow);
    }
}

void PCBViewerEmbedder::setVisible(bool visible)
{
    if (visible) {
        show();
    } else {
        hide();
    }
}

void PCBViewerEmbedder::enableFallbackMode()
{
    m_usingFallback = true;
    handleStatus("PCB viewer running in fallback mode (Qt-only rendering)");
}

double PCBViewerEmbedder::getZoomLevel() const
{
    if (m_renderer) {
        const auto& camera = m_renderer->GetCamera();
        return static_cast<double>(camera.zoom);
    }
    return 1.0;
}

void PCBViewerEmbedder::setZoomLevel(double zoom)
{
    if (m_renderer) {
        const auto& camera = m_renderer->GetCamera();
        m_renderer->SetCamera(camera.x, camera.y, static_cast<float>(zoom));
        onZoomChanged();
    }
}

void PCBViewerEmbedder::getCameraPosition(float& x, float& y) const
{
    if (m_renderer) {
        const auto& camera = m_renderer->GetCamera();
        x = camera.x;
        y = camera.y;
    } else {
        x = 0.0f;
        y = 0.0f;
    }
}

void PCBViewerEmbedder::setCameraPosition(float x, float y)
{
    if (m_renderer) {
        const auto& camera = m_renderer->GetCamera();
        m_renderer->SetCamera(x, y, camera.zoom);
    }
}

// Private methods

bool PCBViewerEmbedder::initializeGLFW(void* parentHandle, int width, int height)
{
    // Use static initialization counter to ensure GLFW is only initialized once globally
    // This prevents crashes when opening multiple PCB tabs
    static bool s_glfwInitialized = false;
    static int s_instanceCount = 0;
    static std::mutex s_glfwMutex;
    
    {
        std::lock_guard<std::mutex> lock(s_glfwMutex);
        
        // Only initialize GLFW on first instance
        if (!s_glfwInitialized) {
            if (!glfwInit()) {
                handleError("Failed to initialize GLFW globally");
                return false;
            }
            s_glfwInitialized = true;
            handleStatus("GLFW initialized globally (first PCB instance)");
        } else {
            handleStatus("Using existing GLFW initialization (instance " + std::to_string(s_instanceCount + 1) + ")");
        }
        s_instanceCount++;
    }

    // Set GLFW window hints - matching Window.cpp configuration
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Start hidden
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // No window decorations

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

#ifdef _WIN32
    // Create window as child of Qt widget
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
#endif

    // Create GLFW window - similar to Window.cpp but for embedding
    m_glfwWindow = glfwCreateWindow(width, height, "PCB Viewer Embedded", nullptr, nullptr);
    if (!m_glfwWindow) {
        handleError("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    // Make the window's context current - essential step from Window.cpp
    glfwMakeContextCurrent(m_glfwWindow);
    
    // Enable V-sync for better performance (from Window.cpp)
    glfwSwapInterval(1);

    // Initialize GLEW - exactly like Window.cpp
    if (glewInit() != GLEW_OK) {
        handleError("Failed to initialize GLEW");
        return false;
    }

    // Set viewport - like Window.cpp
    glViewport(0, 0, width, height);

    // Enable depth testing and blending - matching Window.cpp OpenGL setup
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize ImGui with proper context isolation to prevent tab conflicts
    // Each PCB tab gets its own ImGui context to prevent crashes
    IMGUI_CHECKVERSION();
    m_imguiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiContext);
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(m_glfwWindow, true)) {
        handleError("Failed to initialize ImGui GLFW backend");
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        handleError("Failed to initialize ImGui OpenGL3 backend");
        return false;
    }

#ifdef _WIN32
    // Embed in parent window if provided
    if (parentHandle) {
        HWND parentHwnd = static_cast<HWND>(parentHandle);
        HWND childHwnd = glfwGetWin32Window(m_glfwWindow);
        
        if (childHwnd && parentHwnd) {
            SetParent(childHwnd, parentHwnd);
            SetWindowLong(childHwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE);
            SetWindowPos(childHwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
            m_childHwnd = childHwnd;
            m_parentHwnd = parentHwnd;
            
            // Show the embedded window
            ShowWindow(childHwnd, SW_SHOW);
            UpdateWindow(childHwnd);
        }
    } else {
        // If no parent, just show the window normally
        glfwShowWindow(m_glfwWindow);
    }
#else
    // Non-Windows platforms
    glfwShowWindow(m_glfwWindow);
#endif

    return true;
}

bool PCBViewerEmbedder::initializeRenderer()
{
    if (!m_renderer) {
        handleError("PCB renderer not available");
        return false;
    }

    // Initialize renderer - matching PCBRenderer::Initialize() from the standalone version
    if (!m_renderer->Initialize()) {
        handleError("Failed to initialize PCB renderer");
        return false;
    }

    // Apply ImGui UI setting and memory optimizations to renderer after initialization
    auto& settings = m_renderer->GetSettings();
    
    // Memory optimization - reduce alpha levels to save on blending operations
    settings.part_alpha = 0.8f;   // Slightly more transparent
    settings.pin_alpha = 0.9f;    // Reduce transparency computations
    settings.outline_alpha = 0.9f;
    
    handleStatus(std::string("PCB renderer initialized successfully with ImGui overlay ") + 
                (m_imguiUIEnabled ? "enabled" : "disabled"));
    
    return true;
}

void PCBViewerEmbedder::setupCallbacks()
{
    if (!m_glfwWindow) {
        return;
    }

    // Set user pointer for callbacks
    glfwSetWindowUserPointer(m_glfwWindow, this);

    // Set callbacks
    glfwSetMouseButtonCallback(m_glfwWindow, mouseButtonCallback);
    glfwSetCursorPosCallback(m_glfwWindow, cursorPosCallback);
    glfwSetScrollCallback(m_glfwWindow, scrollCallback);
    glfwSetKeyCallback(m_glfwWindow, keyCallback);
    glfwSetFramebufferSizeCallback(m_glfwWindow, framebufferSizeCallback);
}



void PCBViewerEmbedder::handleError(const std::string& error)
{
    if (m_errorCallback) {
        m_errorCallback(error);
    }
    std::cerr << "[PCB Embedder Error] " << error << std::endl;
}

void PCBViewerEmbedder::handleStatus(const std::string& status)
{
    if (m_statusCallback) {
        m_statusCallback(status);
    }
    std::cout << "[PCB Embedder] " << status << std::endl;
}

void PCBViewerEmbedder::onPinSelected(int pinIndex)
{
    if (m_pinSelectedCallback && m_pcbData && pinIndex >= 0 && pinIndex < static_cast<int>(m_pcbData->pins.size())) {
        const auto& pin = m_pcbData->pins[pinIndex];
        m_pinSelectedCallback(pin.name, pin.net);
    }
}

void PCBViewerEmbedder::onPartSelected(int partIndex)
{
    if (m_partSelectedCallback && m_pcbData && partIndex >= 0 && partIndex < static_cast<int>(m_pcbData->parts.size())) {
        const auto& part = m_pcbData->parts[partIndex];
        m_partSelectedCallback(part.name);
    }
}

void PCBViewerEmbedder::onZoomChanged()
{
    if (m_zoomCallback) {
        m_zoomCallback(getZoomLevel());
    }
}

// GLFW callbacks

void PCBViewerEmbedder::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    (void)mods;
    PCBViewerEmbedder* embedder = static_cast<PCBViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        // Quick right-click tracking
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                embedder->m_rcPressTime = glfwGetTime();
                embedder->m_rcPressX = xpos;
                embedder->m_rcPressY = ypos;
                embedder->m_rcMoved = false;
                // Do not mutate selection/highlight on press; wait for release to decide (quick menu vs drag)
                // Start panning on press (right-drag)
                embedder->m_mouseDragging = true;
                embedder->m_lastMouseX = xpos;
                embedder->m_lastMouseY = ypos;
            } else if (action == GLFW_RELEASE) {
                double dt = glfwGetTime() - embedder->m_rcPressTime;
                if (embedder->m_rcPressTime > 0.0 && !embedder->m_rcMoved && dt < 0.35) {
                    // Determine if the cursor is on an actionable target (pin or part)
                    int hoveredPin = -1;
                    int partIdx    = -1;
                    if (embedder->m_renderer) {
                        hoveredPin = embedder->m_renderer->GetHoveredPin(
                            static_cast<float>(xpos), static_cast<float>(ypos),
                            embedder->m_windowWidth, embedder->m_windowHeight);
                        if (hoveredPin < 0) {
                            partIdx = embedder->m_renderer->HitTestPart(
                                static_cast<float>(xpos), static_cast<float>(ypos),
                                embedder->m_windowWidth, embedder->m_windowHeight);
                        }
                    }

                    const bool actionable = (hoveredPin >= 0) || (partIdx >= 0);

                    if (actionable) {
                        // On quick right-click over a pin/part, update state minimally to reflect target
                        if (embedder->m_renderer) {
                            if (hoveredPin >= 0) {
                                const bool hasSel = embedder->m_renderer->HasSelectedPin();
                                const int  selIdx = hasSel ? embedder->m_renderer->GetSelectedPinIndex() : -1;
                                // Only trigger a select if hovered differs from current selection; avoids toggling off
                                if (!hasSel || selIdx != hoveredPin) {
                                    embedder->handleMouseClick(static_cast<int>(xpos), static_cast<int>(ypos), GLFW_MOUSE_BUTTON_LEFT);
                                }
                                // Do NOT force component-only highlight here; let selected pin's net drive multi-component highlight
                                // by ensuring no explicit part highlight is set.
                                if (embedder->m_renderer->GetHighlightedPart() >= 0) {
                                    embedder->m_renderer->ClearHighlightedPart();
                                }
                            } else if (partIdx >= 0) {
                                bool keepPin = false;
                                if (embedder->m_renderer->HasSelectedPin() && embedder->m_pcbData) {
                                    int selIdx = embedder->m_renderer->GetSelectedPinIndex();
                                    if (selIdx >= 0 && selIdx < static_cast<int>(embedder->m_pcbData->pins.size())) {
                                        unsigned int selPartOneBased = embedder->m_pcbData->pins[selIdx].part;
                                        if (selPartOneBased > 0 && static_cast<int>(selPartOneBased - 1) == partIdx) {
                                            keepPin = true;
                                        }
                                    }
                                }
                                if (!keepPin) {
                                    // Highlight part without affecting pin selection
                                    embedder->m_renderer->ClearHighlightedNet();
                                    embedder->m_renderer->SetHighlightedPart(partIdx);
                                }
                            }
                        }

                        // Gather part/net based on the current (possibly updated) selection/highlight
                        std::string part;
                        std::string net;
                        
                        // Prefer selected pin info (more specific)
                        if (embedder->m_renderer && embedder->m_renderer->HasSelectedPin()) {
                            part = embedder->getSelectedPinPart();
                            net = embedder->getSelectedPinNet();
                        } else if (embedder->m_renderer && embedder->m_renderer->GetHighlightedPart() >= 0) {
                            part = embedder->getHighlightedPartName();
                        }

                        // Ensure on-screen highlight matches the menu candidate, but keep pin selection if present
                        if (!part.empty() && embedder->m_renderer && embedder->m_pcbData) {
                            int partIndex = -1; size_t idx = 0;
                            for (const auto &p : embedder->m_pcbData->parts) { if (p.name == part) { partIndex = (int)idx; break; } ++idx; }
                            if (partIndex >= 0) {
                                const bool hadSelectedPin = embedder->m_renderer->HasSelectedPin();
                                if (!hadSelectedPin) {
                                    embedder->m_renderer->ClearSelection();
                                    embedder->m_renderer->ClearHighlightedNet();
                                    embedder->m_renderer->SetHighlightedPart(partIndex);
                                } else {
                                    // When a pin is selected, prefer net-driven highlighting; clear any explicit part highlight.
                                    embedder->m_renderer->ClearHighlightedPart();
                                }
                            }
                        }

                        // Render once so visual state matches the menu
                        embedder->render();

                        if ((!part.empty() || !net.empty()) && embedder->m_quickRightClickCallback) {
                            embedder->m_quickRightClickCallback(part, net);
                        }
                    }
                    // else: blank space quick right-click -> no context menu (allows simple panning behavior)
                }
                embedder->m_rcPressTime = 0.0;
                // Stop panning
                embedder->m_mouseDragging = false;
            }
        }

        if (action == GLFW_PRESS && button != GLFW_MOUSE_BUTTON_RIGHT) {
            // Handle left clicks and other buttons (right-click is handled above)
            embedder->handleMouseClick(static_cast<int>(xpos), static_cast<int>(ypos), button);
        } else if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_RIGHT) {
            embedder->m_mouseDragging = false;
        }
    }
}

void PCBViewerEmbedder::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    PCBViewerEmbedder* embedder = static_cast<PCBViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        // Movement threshold for quick right-click detection
        if (embedder->m_rcPressTime > 0.0 && !embedder->m_rcMoved) {
            double dx = xpos - embedder->m_rcPressX;
            double dy = ypos - embedder->m_rcPressY;
            if ((dx*dx + dy*dy) > (6.0 * 6.0)) embedder->m_rcMoved = true;
        }
        embedder->handleMouseMove(static_cast<int>(xpos), static_cast<int>(ypos));
    }
}

void PCBViewerEmbedder::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    PCBViewerEmbedder* embedder = static_cast<PCBViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->handleMouseScroll(xoffset, yoffset);
    }
}

void PCBViewerEmbedder::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    PCBViewerEmbedder* embedder = static_cast<PCBViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->handleKeyPress(key, scancode, action, mods);
    }
}

void PCBViewerEmbedder::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    PCBViewerEmbedder* embedder = static_cast<PCBViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        embedder->resize(width, height);
    }
}

// Placeholder implementations for features not yet needed

void PCBViewerEmbedder::highlightNet(const std::string& netName)
{
    if (!m_renderer || !m_pcbData) return;
    if (netName.empty()) { clearHighlights(); return; }
    m_renderer->SetHighlightedNet(netName);
    handleStatus("Highlight net: " + netName);
}

void PCBViewerEmbedder::clearHighlights()
{
    if (m_renderer) {
        m_renderer->ClearHighlightedNet();
    m_renderer->ClearHighlightedPart();
        handleStatus("Clear net highlight");
    }
}

void PCBViewerEmbedder::showLayer(const std::string& layerName, bool visible)
{
    (void)visible;
    handleStatus("Layer control not yet implemented: " + layerName);
}

void PCBViewerEmbedder::showAllLayers()
{
    handleStatus("Show all layers not yet implemented");
}

void PCBViewerEmbedder::hideAllLayers()
{
    handleStatus("Hide all layers not yet implemented");
}

std::vector<std::string> PCBViewerEmbedder::getNetNames() const {
    std::vector<std::string> nets;
    if (!m_pcbData) return nets;
    nets.reserve(m_pcbData->pins.size());
    std::unordered_set<std::string> uniq;
    for (const auto &p: m_pcbData->pins) {
        if (p.net.empty() || p.net=="UNCONNECTED") continue;
        if (uniq.insert(p.net).second) nets.push_back(p.net);
    }
    std::sort(nets.begin(), nets.end());
    return nets;
}

void PCBViewerEmbedder::zoomToNet(const std::string& netName) {
    if (!m_renderer || !m_pcbData || netName.empty()) return;
    float minx=0, miny=0, maxx=0, maxy=0; bool first=true;
    for (const auto &pin: m_pcbData->pins) {
        if (pin.net==netName) {
            float x=pin.pos.x, y=pin.pos.y; // rotation applied later by renderer zoom-to-fit if needed
            if (first) {
                minx = maxx = x; miny = maxy = y; first = false;
            } else {
                if (x < minx) { minx = x; }
                if (x > maxx) { maxx = x; }
                if (y < miny) { miny = y; }
                if (y > maxy) { maxy = y; }
            }
        }
    }
    if (first) return; // not found
    // Center camera roughly: compute midpoint and set zoom to show bbox
    float cx=(minx+maxx)*0.5f;
    float cy=(miny+maxy)*0.5f;
    // Use existing camera and window size
    float w = (float)m_windowWidth;
    float h = (float)m_windowHeight;
    if (w<=0||h<=0) return;
    // Estimate needed zoom: project bbox size to screen: zoom = min(w/width, h/height)*0.8
    float bw = (maxx-minx);
    float bh = (maxy-miny);
    float zoomTarget = 1.0f;
    if (bw>0 && bh>0) {
        float zx = w/(bw*1.2f);
        float zy = h/(bh*1.2f);
        zoomTarget = (zx < zy) ? zx : zy;
    }
    m_renderer->SetCamera(cx, cy, zoomTarget);
}

std::vector<std::string> PCBViewerEmbedder::getComponentNames() const {
    std::vector<std::string> refs;
    if (!m_pcbData) return refs;
    refs.reserve(m_pcbData->parts.size());
    for (const auto &part : m_pcbData->parts) {
        if (!part.name.empty()) refs.push_back(part.name);
    }
    std::sort(refs.begin(), refs.end());
    return refs;
}

void PCBViewerEmbedder::zoomToComponent(const std::string &ref) {
    if (!m_renderer || !m_pcbData) return;
    // find part by name
    int partIndex = -1; size_t idx=0;
    for (const auto &part : m_pcbData->parts) { if (part.name == ref) { partIndex = (int)idx; break;} ++idx; }
    if (partIndex < 0) return;
    // compute bbox from pins belonging to part
    float minx=0,miny=0,maxx=0,maxy=0; bool first=true;
    for (const auto &pin: m_pcbData->pins) {
        if ((int)pin.part == partIndex + 1) { // parts 1-indexed
            float x=pin.pos.x, y=pin.pos.y;
            if (first) { minx=maxx=x; miny=maxy=y; first=false; }
            else {
                if (x < minx) { minx = x; }
                if (x > maxx) { maxx = x; }
                if (y < miny) { miny = y; }
                if (y > maxy) { maxy = y; }
            }
        }
    }
    if (first) return; // no pins
    // camera only; do not implicitly change highlight state here so caller can control clearing
    float cx=(minx+maxx)*0.5f;
    float cy=(miny+maxy)*0.5f;
    float w=(float)m_windowWidth, h=(float)m_windowHeight;
    float bw = (maxx-minx); float bh=(maxy-miny);
    float zoomTarget = 1.0f; if (bw>0 && bh>0) { float zx=w/(bw*1.5f); float zy=h/(bh*1.5f); zoomTarget = (zx<zy)?zx:zy; }
    m_renderer->SetCamera(cx, cy, zoomTarget);
}

std::vector<std::string> PCBViewerEmbedder::getLayerNames() const { return {"Top Layer", "Bottom Layer", "Outline"}; }
std::vector<std::string> PCBViewerEmbedder::getComponentList() const { std::vector<std::string> components; if (m_pcbData) { for (const auto& part : m_pcbData->parts) components.push_back(part.name);} return components; }

void PCBViewerEmbedder::highlightComponent(const std::string& reference)
{
    if (!m_renderer || !m_pcbData || reference.empty()) return;
    int partIndex=-1; size_t idx=0; for (const auto &part: m_pcbData->parts){ if(part.name==reference){partIndex=(int)idx;break;} ++idx; }
    if (partIndex<0) return;
    m_renderer->ClearHighlightedNet();
    m_renderer->SetHighlightedPart(partIndex);
    handleStatus("Highlight component: "+reference);
}

void PCBViewerEmbedder::displayPinHoverInfo()
{
    // Get current mouse position - matching main.cpp exactly
    double mouseX, mouseY;
    if (m_glfwWindow) {
        glfwGetCursorPos(m_glfwWindow, &mouseX, &mouseY);
    } else {
        mouseX = m_lastMouseX;
        mouseY = m_lastMouseY;
    }
    
    // Check if any pin is hovered - matching main.cpp DisplayPinHoverInfo()
    int hoveredPin = -1;
    if (m_renderer) {
        hoveredPin = m_renderer->GetHoveredPin(static_cast<float>(mouseX), static_cast<float>(mouseY),
                                              m_windowWidth, m_windowHeight);
    }
    
    if (hoveredPin >= 0 && m_pcbData && hoveredPin < static_cast<int>(m_pcbData->pins.size())) {
        const auto& pin = m_pcbData->pins[hoveredPin];
        
        // Create hover tooltip with exact mouse cursor positioning - matching main.cpp
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(mouseX) + 15, static_cast<float>(mouseY) + 15));
        ImGui::SetNextWindowBgAlpha(0.9f); // Semi-transparent background
        
        if (ImGui::Begin("Pin Info", nullptr, 
                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
            
            // Display pin information - matching main.cpp format
            ImGui::Text("Pin Information:");
            ImGui::Separator();
            
            if (!pin.snum.empty()) {
                ImGui::Text("Pin Number: %s", pin.snum.c_str());
            }
            if (!pin.name.empty() && pin.name != pin.snum) {
                ImGui::Text("Pin Name: %s", pin.name.c_str());
            }
            if (!pin.net.empty()) {
                ImGui::Text("Net: %s", pin.net.c_str());
                
                // Count connected pins in the same net
                if (pin.net != "UNCONNECTED" && pin.net != "") {
                    int connectedPins = 0;
                    for (const auto& otherPin : m_pcbData->pins) {
                        if (otherPin.net == pin.net) {
                            connectedPins++;
                        }
                    }
                    ImGui::Text("Connected Pins: %d", connectedPins);
                    
                    if (connectedPins > 1) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                                         "Click to highlight net");
                    }
                }
            }
            
            ImGui::Text("Position: (%d, %d)", pin.pos.x, pin.pos.y);
            ImGui::Text("Part: %d", pin.part);
            
            // Show selection status
            if (m_renderer && m_renderer->GetSelectedPinIndex() == hoveredPin) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "SELECTED");
            } else {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Click to select");
            }
            
            ImGui::End();
        }
    }
    
    // Display selected pin information in a separate window - matching main.cpp
    if (m_renderer && m_renderer->HasSelectedPin() && m_pcbData) {
        int selectedPin = m_renderer->GetSelectedPinIndex();
        if (selectedPin >= 0 && selectedPin < static_cast<int>(m_pcbData->pins.size())) {
            const auto& pin = m_pcbData->pins[selectedPin];
            
            // Create selection info window
            ImGui::Begin("Pin Information", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Selected Pin Details:");
            ImGui::Separator();
            ImGui::Text("Pin Number: %s", pin.name.c_str());
            ImGui::Text("Net Name: %s", pin.net.empty() ? "UNCONNECTED" : pin.net.c_str());
            ImGui::Text("Serial Number: %s", pin.snum.c_str());
            ImGui::Text("Position: (%d, %d)", pin.pos.x, pin.pos.y);
            ImGui::Text("Radius: %.1f", pin.radius);
            
            if ((int)pin.part < (int)m_pcbData->parts.size()) {
                ImGui::Text("Part: %s", m_pcbData->parts[(int)pin.part].name.c_str());
            }
            
            if (ImGui::Button("Clear Selection")) {
                clearSelection();
            }
            
            ImGui::End();
        }
    }
}
