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
#include <algorithm>
#include <cmath>

PCBViewerEmbedder::PCBViewerEmbedder()
    : m_glfwWindow(nullptr)
    , m_parentHwnd(nullptr)
    , m_childHwnd(nullptr)
    , m_renderer(std::make_unique<PCBRenderer>())
    , m_pcbData(nullptr)
    , m_initialized(false)
    , m_pdfLoaded(false) // Keep same name for compatibility
    , m_usingFallback(false)
    , m_visible(false)
    , m_imguiUIEnabled(false) // Disable ImGui UI by default - use Qt toolbar only
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

    // Create sample PCB data for testing
    createSamplePCB();

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

    try {
        // Clear PCB data first to free memory
        if (m_pcbData) {
            handleStatus("Clearing PCB data...");
            m_pcbData.reset();
        }

        // Cleanup renderer before ImGui
        if (m_renderer) {
            handleStatus("Cleaning up PCB renderer...");
            m_renderer->SetPCBData(nullptr); // Clear data reference
            m_renderer->Cleanup();
        }

        // Cleanup ImGui - always initialized for GLFW compatibility
        if (m_glfwWindow) {
            handleStatus("Making GLFW context current for cleanup...");
            glfwMakeContextCurrent(m_glfwWindow);
            
            handleStatus("Shutting down ImGui...");
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }

        // Cleanup GLFW window
        if (m_glfwWindow) {
            handleStatus("Destroying GLFW window...");
            glfwDestroyWindow(m_glfwWindow);
            m_glfwWindow = nullptr;
        }

        // Reset all state
        m_initialized = false;
        m_pdfLoaded = false;
        m_currentFilePath.clear();
        m_parentHwnd = nullptr;
        m_childHwnd = nullptr;
        
        handleStatus("PCB viewer embedder cleaned up successfully");
    }
    catch (const std::exception& e) {
        handleError("Exception during cleanup: " + std::string(e.what()));
        // Force reset even if cleanup fails
        m_initialized = false;
        m_pdfLoaded = false;
        m_pcbData.reset();
        m_glfwWindow = nullptr;
    }
    catch (...) {
        handleError("Unknown exception during cleanup");
        // Force reset even if cleanup fails
        m_initialized = false;
        m_pdfLoaded = false;
        m_pcbData.reset();
        m_glfwWindow = nullptr;
    }
}

bool PCBViewerEmbedder::loadPCB(const std::string& filePath)
{
    handleStatus("Loading PCB file: " + filePath);

    if (m_usingFallback) {
        handleError("PCB loading not supported in fallback mode");
        return false;
    }

    try {
        // Clear any existing PCB data to free memory before loading new file
        if (m_pcbData) {
            handleStatus("Clearing existing PCB data before loading new file");
            m_pcbData.reset();
            
            // Force garbage collection
            if (m_renderer) {
                m_renderer->SetPCBData(nullptr);
            }
        }

        // Check file extension - matching standalone version exactly
        std::string ext = Utils::ToLower(Utils::GetFileExtension(filePath));
        if (ext != "xzz" && ext != "pcb" && ext != "xzzpcb") {
            handleError("Unsupported file format: " + ext);
            return false;
        }

        handleStatus("Loading XZZPCB file format: " + ext);
        
        // Load PCB file using XZZPCBFile - matching standalone version
        auto pcbFile = XZZPCBFile::LoadFromFile(filePath);
        if (!pcbFile) {
            handleError("Failed to load XZZPCB file: " + filePath);
            return false;
        }

        // Convert to shared_ptr<BRDFileBase> - matching standalone version
        m_pcbData = std::shared_ptr<BRDFileBase>(pcbFile.release());
        
        // Log data size for debugging large files
        if (m_pcbData && m_pcbData->IsValid()) {
            handleStatus("PCB data loaded: " + std::to_string(m_pcbData->parts.size()) + 
                        " parts, " + std::to_string(m_pcbData->pins.size()) + " pins");
            
            // Check for very large PCB files that might cause memory issues
            if (m_pcbData->pins.size() > 50000 || m_pcbData->parts.size() > 10000) {
                handleStatus("Warning: Large PCB file detected - using memory optimization");
            }
        }
        
        if (m_renderer) {
            m_renderer->SetPCBData(m_pcbData);
            
            // Set initial camera position - matching standalone version exactly
            m_renderer->SetCamera(1500, 900, 0.5f); // Center around middle of PCB coordinates
            
            // Then zoom to fit
            m_renderer->ZoomToFit(m_windowWidth, m_windowHeight);
        }

        m_currentFilePath = filePath;
        m_pdfLoaded = true;

        handleStatus("PCB file loaded successfully: " + filePath);
        return true;
    }
    catch (const std::bad_alloc& e) {
        handleError("Memory allocation failed while loading PCB (file too large): " + std::string(e.what()));
        // Clean up on memory failure
        m_pcbData.reset();
        if (m_renderer) {
            m_renderer->SetPCBData(nullptr);
        }
        return false;
    }
    catch (const std::exception& e) {
        handleError("Exception while loading PCB: " + std::string(e.what()));
        // Clean up on any failure
        m_pcbData.reset();
        if (m_renderer) {
            m_renderer->SetPCBData(nullptr);
        }
        return false;
    }
    catch (...) {
        handleError("Unknown exception while loading PCB file: " + filePath);
        // Clean up on unknown failure
        m_pcbData.reset();
        if (m_renderer) {
            m_renderer->SetPCBData(nullptr);
        }
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
    
    // Create sample data for display
    createSamplePCB();
    
    handleStatus("PCB file closed");
}

void PCBViewerEmbedder::render()
{
    if (!m_initialized || m_usingFallback || !m_glfwWindow) {
        return;
    }

    if (!m_visible) {
        return;
    }

    try {
        // Make the context current first - essential for OpenGL rendering
        glfwMakeContextCurrent(m_glfwWindow);

        // Check for OpenGL errors before rendering
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            handleError("OpenGL error detected before rendering: " + std::to_string(error));
            return;
        }

        // Poll GLFW events - but limit frequency for large datasets
        static int eventPollCounter = 0;
        eventPollCounter++;
        if (eventPollCounter % 3 == 0) { // Poll events every 3rd frame for better performance with large PCBs
            glfwPollEvents();
        }

        // Clear the framebuffer
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Always start ImGui frame for GLFW compatibility
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render the PCB using PCBRenderer - matching main.cpp with error handling
        if (m_renderer) {
            try {
                m_renderer->Render(m_windowWidth, m_windowHeight);
            }
            catch (const std::bad_alloc& e) {
                handleError("Memory allocation failed during rendering (PCB too large): " + std::string(e.what()));
                // Try to recover by clearing the PCB data
                m_pcbData.reset();
                m_renderer->SetPCBData(nullptr);
                enableFallbackMode();
                return;
            }
            catch (const std::exception& e) {
                handleError("Exception during PCB rendering: " + std::string(e.what()));
                return;
            }
        }

        // Display hover information only if ImGui UI is enabled
        if (m_imguiUIEnabled) {
            try {
                displayPinHoverInfo();
            }
            catch (const std::exception& e) {
                handleError("Exception during pin hover info display: " + std::string(e.what()));
                // Continue rendering even if hover info fails
            }
        }

        // Always render ImGui (even if no UI elements) for GLFW compatibility
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Swap buffers - matching main.cpp
        glfwSwapBuffers(m_glfwWindow);

        // Check for OpenGL errors after rendering
        error = glGetError();
        if (error != GL_NO_ERROR) {
            handleError("OpenGL error detected after rendering: " + std::to_string(error));
        }
    }
    catch (const std::exception& e) {
        handleError("Critical exception in render loop: " + std::string(e.what()));
        enableFallbackMode();
    }
    catch (...) {
        handleError("Unknown critical exception in render loop");
        enableFallbackMode();
    }
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
    if (m_renderer && button == 0) { // Left click
        // Handle pin selection
        m_renderer->HandleMouseClick(static_cast<float>(x), static_cast<float>(y),
                                   m_windowWidth, m_windowHeight);
        
        // Check if a pin was selected
        if (m_renderer->HasSelectedPin()) {
            int selectedPin = m_renderer->GetSelectedPinIndex();
            onPinSelected(selectedPin);
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
    (void)x; (void)y; // Suppress unused parameter warnings
    if (button == 1) { // Right click release - stop panning
        m_mouseDragging = false;
    }
}

void PCBViewerEmbedder::handleMouseScroll(double xOffset, double yOffset)
{
    (void)xOffset; // Suppress unused parameter warning
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
    (void)scancode; (void)mods; // Suppress unused parameter warnings
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
    try {
        // Initialize GLFW - following the pattern from Window.cpp
        if (!glfwInit()) {
            handleError("Failed to initialize GLFW");
            return false;
        }

        // Set GLFW window hints - matching Window.cpp configuration with optimizations for large PCBs
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Start hidden
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // No window decorations
        
        // Memory optimization hints for large PCB files
        glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 0); // Disable multisampling to save memory

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
        
        // Enable V-sync for better performance (from Window.cpp) - but disable for large files
        glfwSwapInterval(1);

        // Initialize GLEW - exactly like Window.cpp
        if (glewInit() != GLEW_OK) {
            handleError("Failed to initialize GLEW");
            glfwDestroyWindow(m_glfwWindow);
            glfwTerminate();
            return false;
        }

        // Set viewport - like Window.cpp
        glViewport(0, 0, width, height);

        // Enable depth testing and blending - matching Window.cpp OpenGL setup
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Memory optimization for large PCBs
        glHint(GL_GENERATE_MIPMAP_HINT, GL_FASTEST);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
        
        // Check OpenGL limits for debugging large file issues
        GLint maxTextureSize, maxVertexAttribs, maxUniformComponents;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &maxUniformComponents);
        
        handleStatus("OpenGL limits - Max texture: " + std::to_string(maxTextureSize) + 
                    ", Max vertex attribs: " + std::to_string(maxVertexAttribs) +
                    ", Max uniform components: " + std::to_string(maxUniformComponents));

        // Always initialize ImGui for GLFW event handling, but control UI rendering separately
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForOpenGL(m_glfwWindow, true)) {
            handleError("Failed to initialize ImGui GLFW backend");
            glfwDestroyWindow(m_glfwWindow);
            glfwTerminate();
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init("#version 330")) {
            handleError("Failed to initialize ImGui OpenGL3 backend");
            glfwDestroyWindow(m_glfwWindow);
            glfwTerminate();
            return false;
        }
        
        if (m_imguiUIEnabled) {
            handleStatus("ImGui UI enabled (will show pin hover/selection overlays)");
        } else {
            handleStatus("ImGui UI disabled (using Qt toolbar only) - ImGui initialized for GLFW compatibility");
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
    catch (const std::exception& e) {
        handleError("Exception during GLFW initialization: " + std::string(e.what()));
        if (m_glfwWindow) {
            glfwDestroyWindow(m_glfwWindow);
            m_glfwWindow = nullptr;
        }
        glfwTerminate();
        return false;
    }
    catch (...) {
        handleError("Unknown exception during GLFW initialization");
        if (m_glfwWindow) {
            glfwDestroyWindow(m_glfwWindow);
            m_glfwWindow = nullptr;
        }
        glfwTerminate();
        return false;
    }
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

    // Apply ImGui UI setting to renderer after initialization
    m_renderer->GetSettings().enable_imgui_overlay = m_imguiUIEnabled;
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

bool PCBViewerEmbedder::createSamplePCB()
{
    handleStatus("Creating sample PCB data for testing");

    try {
        // Create sample PCB file for demonstration - matching main.cpp CreateSamplePCB()
        auto samplePcb = std::make_shared<XZZPCBFile>();
        
        // Board outline (rectangle) - exact copy from main.cpp
        samplePcb->format = {
            {0, 0}, {10000, 0}, {10000, 7000}, {0, 7000}
        };
        
        // Outline segments - exact copy from main.cpp
        for (size_t i = 0; i < samplePcb->format.size(); ++i) {
            size_t next = (i + 1) % samplePcb->format.size();
            samplePcb->outline_segments.push_back({samplePcb->format[i], samplePcb->format[next]});
        }
        
        // Sample parts - exact copy from main.cpp
        BRDPart part1;
        part1.name = "U1";
        part1.mounting_side = BRDPartMountingSide::Top;
        part1.part_type = BRDPartType::SMD;
        part1.p1 = {2000, 2000};
        part1.p2 = {4000, 3000};
        samplePcb->parts.push_back(part1);
        
        BRDPart part2;
        part2.name = "U2";
        part2.mounting_side = BRDPartMountingSide::Top;
        part2.part_type = BRDPartType::SMD;
        part2.p1 = {6000, 4000};
        part2.p2 = {8000, 5000};
        samplePcb->parts.push_back(part2);
        
        // Sample pins with meaningful net names - exact copy from main.cpp
        std::vector<std::string> netNames = {"VCC", "GND", "LCD_VSN", "NET1816", "VPH_PWR", "SPMI_CLK", "SPMI_DATA", "UNCONNECTED"};
        std::vector<std::string> netNames2 = {"NET1807", "NET1789", "VREG_L5_1P8", "GND", "LCD_VSN", "VPH_PWR"};
        
        for (int i = 0; i < 8; ++i) {
            BRDPin pin;
            pin.pos = {2000 + i * 250, 2000};
            pin.part = 0;
            pin.name = std::to_string(i + 1);  // Pin number
            pin.net = (static_cast<size_t>(i) < netNames.size()) ? netNames[i] : "NET_" + std::to_string(i);
            pin.snum = std::to_string(i + 1);
            pin.radius = 50;
            samplePcb->pins.push_back(pin);
        }
        
        for (int i = 0; i < 6; ++i) {
            BRDPin pin;
            pin.pos = {6000 + i * 300, 4000};
            pin.part = 1;
            pin.name = std::to_string(i + 1);  // Pin number
            pin.net = (static_cast<size_t>(i) < netNames2.size()) ? netNames2[i] : "NET_" + std::to_string(i + 8);
            pin.snum = std::to_string(i + 1);
            pin.radius = 60;
            samplePcb->pins.push_back(pin);
        }
        
        // Validate and set data - matching main.cpp
        samplePcb->SetValid(true);  // For demo data, we know it's valid
        
        m_pcbData = std::static_pointer_cast<BRDFileBase>(samplePcb);
        if (m_renderer) {
            m_renderer->SetPCBData(m_pcbData);
            m_renderer->ZoomToFit(m_windowWidth, m_windowHeight);
        }
        
        handleStatus("Sample PCB data created successfully with " + 
                    std::to_string(samplePcb->parts.size()) + " parts and " + 
                    std::to_string(samplePcb->pins.size()) + " pins");
        return true;
    }
    catch (const std::exception& e) {
        handleError("Failed to create sample PCB: " + std::string(e.what()));
        return false;
    }
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

void PCBViewerEmbedder::onZoomChanged()
{
    if (m_zoomCallback) {
        m_zoomCallback(getZoomLevel());
    }
}

// GLFW callbacks

void PCBViewerEmbedder::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    (void)mods; // Suppress unused parameter warning
    PCBViewerEmbedder* embedder = static_cast<PCBViewerEmbedder*>(glfwGetWindowUserPointer(window));
    if (embedder) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        
        if (action == GLFW_PRESS) {
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
    handleStatus("Net highlighting not yet implemented: " + netName);
}

void PCBViewerEmbedder::clearHighlights()
{
    handleStatus("Clear highlights not yet implemented");
}

void PCBViewerEmbedder::showLayer(const std::string& layerName, bool visible)
{
    (void)visible; // Suppress unused parameter warning
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

std::vector<std::string> PCBViewerEmbedder::getLayerNames() const
{
    return {"Top Layer", "Bottom Layer", "Outline"};
}

void PCBViewerEmbedder::highlightComponent(const std::string& reference)
{
    handleStatus("Component highlighting not yet implemented: " + reference);
}

std::vector<std::string> PCBViewerEmbedder::getComponentList() const
{
    std::vector<std::string> components;
    if (m_pcbData) {
        for (const auto& part : m_pcbData->parts) {
            components.push_back(part.name);
        }
    }
    return components;
}

void PCBViewerEmbedder::setImGuiUIEnabled(bool enabled)
{
    m_imguiUIEnabled = enabled;
    handleStatus("setImGuiUIEnabled called with: " + std::string(enabled ? "true" : "false"));
    
    // Also control PCBRenderer's ImGui overlay
    if (m_renderer) {
        m_renderer->GetSettings().enable_imgui_overlay = enabled;
        handleStatus("PCBRenderer overlay setting updated to: " + std::string(enabled ? "true" : "false"));
    } else {
        handleStatus("PCBRenderer not available yet - setting will be applied after initialization");
    }
    
    LOG_INFO("ImGui UI enabled set to: " + std::string(enabled ? "true" : "false"));
    handleStatus("ImGui UI " + std::string(enabled ? "enabled" : "disabled"));
}

void PCBViewerEmbedder::displayPinHoverInfo()
{
    // Skip ImGui UI rendering if disabled (using Qt toolbar only)
    if (!m_imguiUIEnabled) {
        // Debug: This should be printed when ImGui UI is disabled
        static bool debugOnce = false;
        if (!debugOnce) {
            LOG_INFO("ImGui UI is disabled - skipping pin hover info display");
            debugOnce = true;
        }
        return;
    }

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
            
            if (static_cast<size_t>(pin.part) < m_pcbData->parts.size()) {
                ImGui::Text("Part: %s", m_pcbData->parts[pin.part].name.c_str());
            }
            
            if (ImGui::Button("Clear Selection")) {
                clearSelection();
            }
            
            ImGui::End();
        }
    }
}
