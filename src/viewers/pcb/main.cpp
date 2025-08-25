#include "Window.h"
#include "PCBRenderer.h"
#include "XZZPCBFile.h"
#include "BRDFile.h"
#include "BRD2File.h"
#include "Utils.h"
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

class PCBViewerApp {
public:
    PCBViewerApp() : window(1200, 800, "W2R Schematic Viewer") {
    }
    
    // Static callback for scroll events
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        PCBViewerApp* app = static_cast<PCBViewerApp*>(glfwGetWindowUserPointer(window));
        if (app) {
            app->HandleScroll(xoffset, yoffset);
        }
    }
    
    bool Initialize() {
        // Initialize window
        if (!window.Initialize()) {
            LOG_ERROR("Failed to initialize window");
            return false;
        }

        // Initialize renderer
        if (!renderer.Initialize()) {
            LOG_ERROR("Failed to initialize renderer");
            return false;
        }

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForOpenGL(window.GetHandle(), true)) {
            LOG_ERROR("Failed to initialize ImGui GLFW backend");
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init("#version 330")) {
            LOG_ERROR("Failed to initialize ImGui OpenGL3 backend");
            return false;
        }

        // Set the user pointer for the window
        glfwSetWindowUserPointer(window.GetHandle(), this);

        // Set the scroll callback
        glfwSetScrollCallback(window.GetHandle(), ScrollCallback);

        LOG_INFO("PCB Viewer application initialized successfully");
        return true;
    }

    void Run(const std::string& pcb_file_path = "") {
        if (!pcb_file_path.empty()) {
            LoadPCBFile(pcb_file_path);
        } else {
            // Load a sample/demo PCB if no file specified
            CreateSamplePCB();
        }        // Main rendering loop
        LOG_INFO("Starting main render loop");
        int frame_count = 0;
        
        // Frame rate limiting variables
        const double target_fps = 60.0;
        const double frame_time = 1.0 / target_fps;
        auto last_time = std::chrono::high_resolution_clock::now();
        
        while (!window.ShouldClose()) {
            if (frame_count == 0) {
                LOG_INFO("First frame rendering");
            }
            frame_count++;
            
            // Calculate delta time for frame rate limiting
            auto current_time = std::chrono::high_resolution_clock::now();
            double delta_time = std::chrono::duration<double>(current_time - last_time).count();
              window.PollEvents();
            
            // Update window size for responsiveness
            window.UpdateSize();
            
            HandleInput();
            
            // Start ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            
            // Render
            renderer.Render(window.GetWidth(), window.GetHeight());
            
            // Display hover information if a pin is hovered
            DisplayPinHoverInfo();
            
            // Render ImGui
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            
            window.SwapBuffers();
            
            // Frame rate limiting - only sleep if we're rendering too fast
            double frame_end_time = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - current_time).count();
            
            if (frame_end_time < frame_time) {
                double sleep_time = frame_time - frame_end_time;
                std::this_thread::sleep_for(std::chrono::microseconds(
                    static_cast<int>(sleep_time * 1000000)));
            }
            
            last_time = current_time;
        }
    }

    void Cleanup() {
        // Cleanup ImGui
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        
        renderer.Cleanup();
        window.Cleanup();
    }

private:
    Window window;
    PCBRenderer renderer;
    std::shared_ptr<BRDFileBase> pcb_data;
      // Input state
    bool mouse_dragging = false;
    double last_mouse_x = 0.0;
    double last_mouse_y = 0.0;

    // File dialog functions
    std::string OpenFileDialog() {
#ifdef _WIN32
        OPENFILENAMEW ofn;
        wchar_t szFile[260] = {0};
        
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = glfwGetWin32Window(window.GetHandle());
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);        ofn.lpstrFilter = L"PCB Files\0*.xzzpcb;*.pcb;*.xzz\0XZZPCB Files\0*.xzzpcb;*.xzz\0PCB Files\0*.pcb\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.lpstrTitle = L"Open PCB File";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        
        if (GetOpenFileNameW(&ofn)) {
            // Convert wide string to UTF-8 std::string
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, NULL, 0, NULL, NULL);
            std::string result(size_needed - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, szFile, -1, &result[0], size_needed, NULL, NULL);
            return result;
        }
#endif
        return "";
    }

    bool LoadPCBFile(const std::string& filepath) {
        LOG_INFO("Loading PCB file: " + filepath);
          // Check file extension
        std::string ext = Utils::ToLower(Utils::GetFileExtension(filepath));
        LOG_INFO("Detected file extension: '" + ext + "'");
        if (ext != "xzz" && ext != "pcb" && ext != "xzzpcb") {
            LOG_ERROR("Unsupported file format: '" + ext + "' - Expected: xzz, pcb, or xzzpcb (.brd and .brd2 support disabled)");
            return false;
        }
        
        std::shared_ptr<BRDFileBase> loaded_pcb = nullptr;
        
        // BRD and BRD2 support disabled - parsing code kept for reference
        /*
        if (ext == "brd") {
            // Load BRD file
            auto brd = BRDFile::LoadFromFile(filepath);
            if (!brd) {
                LOG_ERROR("Failed to load BRD file: " + filepath);
                return false;
            }
            loaded_pcb = std::shared_ptr<BRDFileBase>(brd.release());
        } else if (ext == "brd2") {
            // Load BRD2 file
            auto brd2 = BRD2File::LoadFromFile(filepath);
            if (!brd2) {
                LOG_ERROR("Failed to load BRD2 file: " + filepath);
                return false;
            }
            loaded_pcb = std::shared_ptr<BRDFileBase>(brd2.release());
        } else {
        */
            // Load XZZPCB file (for .xzz, .pcb, .xzzpcb)
            auto xzzpcb = XZZPCBFile::LoadFromFile(filepath);
            if (!xzzpcb) {
                LOG_ERROR("Failed to load XZZPCB file: " + filepath);
                return false;
            }
            loaded_pcb = std::shared_ptr<BRDFileBase>(xzzpcb.release());
        /*
        }
        */
        
        if (!loaded_pcb) {
            LOG_ERROR("Failed to load PCB file: " + filepath);
            return false;
        }
          pcb_data = loaded_pcb;
        renderer.SetPCBData(pcb_data);
        
        // Force camera to a reasonable position based on the coordinates we saw
        renderer.SetCamera(1500, 900, 0.5f); // Center around middle of PCB coordinates
        
        // Also try Zoom to fit
        renderer.ZoomToFit(window.GetWidth(), window.GetHeight());
        
        LOG_INFO("PCB file loaded successfully");
        return true;
    }

    void CreateSamplePCB() {
        LOG_INFO("Creating sample PCB data");
        
        // Create a sample PCB file for demonstration
        auto sample_pcb = std::make_shared<XZZPCBFile>();
        
        // Create sample data manually
        // Board outline (rectangle)
        sample_pcb->format = {
            {0, 0}, {10000, 0}, {10000, 7000}, {0, 7000}
        };
        
        // Outline segments
        for (size_t i = 0; i < sample_pcb->format.size(); ++i) {
            size_t next = (i + 1) % sample_pcb->format.size();
            sample_pcb->outline_segments.push_back({sample_pcb->format[i], sample_pcb->format[next]});
        }
        
        // Sample parts
        BRDPart part1;
        part1.name = "U1";
        part1.mounting_side = BRDPartMountingSide::Top;
        part1.part_type = BRDPartType::SMD;
        part1.p1 = {2000, 2000};
        part1.p2 = {4000, 3000};
        sample_pcb->parts.push_back(part1);
        
        BRDPart part2;
        part2.name = "U2";
        part2.mounting_side = BRDPartMountingSide::Top;
        part2.part_type = BRDPartType::SMD;
        part2.p1 = {6000, 4000};
        part2.p2 = {8000, 5000};
        sample_pcb->parts.push_back(part2);
          // Sample pins with meaningful net names
        std::vector<std::string> net_names = {"VCC", "GND", "LCD_VSN", "NET1816", "VPH_PWR", "SPMI_CLK", "SPMI_DATA", "UNCONNECTED"};
        std::vector<std::string> net_names2 = {"NET1807", "NET1789", "VREG_L5_1P8", "GND", "LCD_VSN", "VPH_PWR"};
        
        // Debug: Log sample pin data
        LOG_INFO("Creating sample pins with net names and pin numbers:");
        
        for (int i = 0; i < 8; ++i) {
            BRDPin pin;
            pin.pos = {2000 + i * 250, 2000};
            pin.part = 0;
            pin.name = std::to_string(i + 1);  // Pin number
            pin.net = (i < net_names.size()) ? net_names[i] : "NET_" + std::to_string(i);
            pin.snum = std::to_string(i + 1);
            pin.radius = 50;
            sample_pcb->pins.push_back(pin);
            
            // Debug log each pin
            LOG_INFO("Pin " + std::to_string(i+1) + ": name='" + pin.name + "', net='" + pin.net + "', snum='" + pin.snum + "'");
        }
          for (int i = 0; i < 6; ++i) {
            BRDPin pin;
            pin.pos = {6000 + i * 300, 4000};
            pin.part = 1;
            pin.name = std::to_string(i + 1);  // Pin number
            pin.net = (i < net_names2.size()) ? net_names2[i] : "NET_" + std::to_string(i + 8);
            pin.snum = std::to_string(i + 1);
            pin.radius = 60;
            sample_pcb->pins.push_back(pin);
            
            // Debug log each pin
            LOG_INFO("Pin " + std::to_string(i+9) + ": name='" + pin.name + "', net='" + pin.net + "', snum='" + pin.snum + "'");
        }// Validate and set data
        sample_pcb->SetValid(true);  // For demo data, we know it's valid
        
        pcb_data = std::static_pointer_cast<BRDFileBase>(sample_pcb);
        renderer.SetPCBData(pcb_data);
        
        // Zoom to fit
        renderer.ZoomToFit(window.GetWidth(), window.GetHeight());
        
        LOG_INFO("Sample PCB created with " + std::to_string(sample_pcb->parts.size()) + 
                " parts and " + std::to_string(sample_pcb->pins.size()) + " pins");
    }    void HandleInput() {
        GLFWwindow* glfw_window = window.GetHandle();
        
        // Handle mouse input for selection and hover
        double mouse_x, mouse_y;
        glfwGetCursorPos(glfw_window, &mouse_x, &mouse_y);
          // Update hover state
        int hovered_pin = renderer.GetHoveredPin(static_cast<float>(mouse_x), static_cast<float>(mouse_y), 
                                                window.GetWidth(), window.GetHeight());
        renderer.SetHoveredPin(hovered_pin);
          // Handle left mouse button for pin selection (single-click)
        static bool left_mouse_pressed = false;
        if (glfwGetMouseButton(glfw_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            if (!left_mouse_pressed) {
                left_mouse_pressed = true;
                
                // Single-click - handle pin selection immediately
                renderer.HandleMouseClick(static_cast<float>(mouse_x), static_cast<float>(mouse_y),
                                        window.GetWidth(), window.GetHeight());
            }
        } else {
            left_mouse_pressed = false;
        }
          // Handle keyboard input
        if (glfwGetKey(glfw_window, GLFW_KEY_R) == GLFW_PRESS) {
            // Reset view
            renderer.ZoomToFit(window.GetWidth(), window.GetHeight());
        }
        
        // Ctrl+O to open file
        static bool ctrl_o_pressed = false;
        if (glfwGetKey(glfw_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || 
            glfwGetKey(glfw_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
            if (glfwGetKey(glfw_window, GLFW_KEY_O) == GLFW_PRESS && !ctrl_o_pressed) {
                ctrl_o_pressed = true;
                OpenFile();
            }
        } else {
            ctrl_o_pressed = false;
        }
        
        // Mouse dragging for panning
        if (glfwGetMouseButton(glfw_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            if (!mouse_dragging) {
                mouse_dragging = true;
                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
            } else {
                double dx = mouse_x - last_mouse_x;
                double dy = mouse_y - last_mouse_y;
                
                // Pan the view (invert Y because screen coordinates are inverted)
                renderer.Pan(static_cast<float>(-dx), static_cast<float>(dy));
                
                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
            }
        } else {
            mouse_dragging = false;
        }
        
        // Handle keyboard input
        static bool r_key_pressed = false;
        if (glfwGetKey(glfw_window, GLFW_KEY_R) == GLFW_PRESS) {
            if (!r_key_pressed) {
                r_key_pressed = true;
                // Reset view to fit PCB
                int width, height;
                glfwGetFramebufferSize(glfw_window, &width, &height);
                renderer.ZoomToFit(width, height);
            }
        } else {
            r_key_pressed = false;
        }
    }void OpenFile() {
        std::string filepath = OpenFileDialog();
        if (!filepath.empty()) {
            LOG_INFO("Opening file: " + filepath);            bool success = LoadPCBFile(filepath);
            if (!success) {
                LOG_INFO("File could not be loaded - continuing with current PCB data");
                // The current PCB data remains displayed
            }
        }
    }    void HandleScroll(double xoffset, double yoffset) {
        // Get mouse position for zoom center
        double mouse_x, mouse_y;
        glfwGetCursorPos(window.GetHandle(), &mouse_x, &mouse_y);
        
        // Get window size
        int width, height;
        glfwGetFramebufferSize(window.GetHandle(), &width, &height);
        
        // Get current camera state
        const auto& camera = renderer.GetCamera();
        
        // Calculate the world position under the mouse cursor BEFORE zooming
        // This is the point that should remain stationary
        float mouse_world_x = camera.x + (static_cast<float>(mouse_x) - width * 0.5f) / camera.zoom;
        float mouse_world_y = camera.y + (height * 0.5f - static_cast<float>(mouse_y)) / camera.zoom;
        
        // Apply zoom
    // Use exponential scaling for smooth & responsive zoom steps (1 notch ~ x1.2)
    float zoom_factor = std::pow(1.2f, static_cast<float>(yoffset));
        
        // Call zoom function with the world point that should stay under the cursor
        renderer.Zoom(zoom_factor, mouse_world_x, mouse_world_y);
    }
    
    void DisplayPinHoverInfo() {
        // Get current mouse position
        double mouse_x, mouse_y;
        glfwGetCursorPos(window.GetHandle(), &mouse_x, &mouse_y);
        
        // Check if any pin is hovered
        int hovered_pin = renderer.GetHoveredPin(static_cast<float>(mouse_x), static_cast<float>(mouse_y),
                                                window.GetWidth(), window.GetHeight());
        
        if (hovered_pin >= 0 && pcb_data && hovered_pin < static_cast<int>(pcb_data->pins.size())) {
            const auto& pin = pcb_data->pins[hovered_pin];
            
            // Create hover tooltip
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(mouse_x) + 15, static_cast<float>(mouse_y) + 15));
            ImGui::SetNextWindowBgAlpha(0.9f); // Semi-transparent background
            
            if (ImGui::Begin("Pin Info", nullptr, 
                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
                
                // Display pin information
                ImGui::Text("Pin Information:");
                ImGui::Separator();
                
                if (!pin.snum.empty()) {
                    ImGui::Text("Pin Number: %s", pin.snum.c_str());
                }                if (!pin.name.empty() && pin.name != pin.snum) {
                    ImGui::Text("Pin Name: %s", pin.name.c_str());
                }
                if (!pin.net.empty()) {
                    ImGui::Text("Net: %s", pin.net.c_str());
                    
                    // Count connected pins in the same net
                    if (pin.net != "UNCONNECTED" && pin.net != "") {
                        int connected_pins = 0;
                        for (const auto& other_pin : pcb_data->pins) {
                            if (other_pin.net == pin.net) {
                                connected_pins++;
                            }
                        }
                        ImGui::Text("Connected Pins: %d", connected_pins);                        if (connected_pins > 1) {
                            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                                             "Click to highlight net");
                        }
                    }
                }
                
                ImGui::Text("Position: (%.1f, %.1f)", pin.pos.x, pin.pos.y);
                ImGui::Text("Part: %d", pin.part);
                
                // Show selection status
                if (renderer.GetSelectedPinIndex() == hovered_pin) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "SELECTED");                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click to select");
                }
                
                ImGui::End();
            }
        }
        
        // Display selected pin information in a separate window
        if (renderer.HasSelectedPin() && pcb_data) {
            int selected_pin = renderer.GetSelectedPinIndex();
            if (selected_pin >= 0 && selected_pin < static_cast<int>(pcb_data->pins.size())) {
                const auto& pin = pcb_data->pins[selected_pin];
                
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Selected Pin Details", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Selected Pin:");
                    ImGui::Separator();
                    
                    if (!pin.snum.empty()) {
                        ImGui::Text("Pin Number: %s", pin.snum.c_str());
                    }
                    if (!pin.name.empty() && pin.name != pin.snum) {
                        ImGui::Text("Pin Name: %s", pin.name.c_str());
                    }                    if (!pin.net.empty()) {
                        ImGui::Text("Net: %s", pin.net.c_str());
                        
                        // Show connected pins count for selected pin
                        if (pin.net != "UNCONNECTED" && pin.net != "") {
                            int connected_pins = 0;
                            for (const auto& other_pin : pcb_data->pins) {
                                if (other_pin.net == pin.net) {
                                    connected_pins++;
                                }
                            }
                            ImGui::Text("Total pins in net: %d", connected_pins);                            if (connected_pins > 1) {
                                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                                                 "%d pins highlighted", connected_pins);
                            }
                        }
                    }
                    
                    ImGui::Text("Position: (%.1f, %.1f)", pin.pos.x, pin.pos.y);
                    ImGui::Text("Part: %d", pin.part);
                    
                    if (ImGui::Button("Clear Selection")) {
                        renderer.ClearSelection();
                    }
                    
                    ImGui::End();
                }
            }
        }
    }
};

int main(int argc, char* argv[]) {    std::cout << "PCB Viewer - Multi-Format Support (XZZPCB) - BRD/BRD2 support disabled" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Right Mouse Button + Drag: Pan view" << std::endl;
    std::cout << "  Mouse Wheel: Zoom in/out" << std::endl;
    std::cout << "  R Key: Reset view to fit PCB" << std::endl;
    std::cout << "  Ctrl+O: Open PCB file" << std::endl;
    std::cout << "  ESC Key: Exit application" << std::endl;
    std::cout << std::endl;

    PCBViewerApp app;
    
    if (!app.Initialize()) {
        LOG_ERROR("Failed to initialize application");
        return -1;
    }

    // Check if a file path was provided
    std::string pcb_file_path;
    if (argc > 1) {
        pcb_file_path = argv[1];
        if (!Utils::FileExists(pcb_file_path)) {
            LOG_ERROR("File does not exist: " + pcb_file_path);
            pcb_file_path.clear();
        }
    }

    try {
        app.Run(pcb_file_path);
    } catch (const std::exception& e) {
        LOG_ERROR("Application error: " + std::string(e.what()));
        app.Cleanup();
        return -1;
    }

    app.Cleanup();
    LOG_INFO("Application finished successfully");
    return 0;
}
