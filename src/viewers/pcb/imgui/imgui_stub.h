#pragma once

// ImGui stub header - provides basic ImGui functionality compatible with MinGW
// This is a minimal implementation to resolve MSVC/MinGW compatibility issues

#include <string>

// Basic ImGui types and constants
struct ImVec2 {
    float x, y;
    ImVec2() { x = y = 0.0f; }
    ImVec2(float _x, float _y) { x = _x; y = _y; }
};

struct ImGuiIO {
    int ConfigFlags;
    // Add more fields as needed
};

enum ImGuiConfigFlags_ {
    ImGuiConfigFlags_NavEnableKeyboard = 1 << 0
};

// ImGui stub functions
namespace ImGui {
    // Context management
    bool CreateContext();
    void DestroyContext();
    ImGuiIO& GetIO();
    
    // Style
    void StyleColorsDark();
    
    // Frame management
    void NewFrame();
    void Render();
    
    // Window management
    bool Begin(const char* name, bool* p_open = nullptr, int flags = 0);
    void End();
    
    // Text
    void Text(const char* fmt, ...);
    
    // Layout
    void SameLine();
    void Separator();
}

// Platform backend stubs
struct GLFWwindow;
bool ImGui_ImplGlfw_InitForOpenGL(GLFWwindow* window, bool install_callbacks);
void ImGui_ImplGlfw_Shutdown();
void ImGui_ImplGlfw_NewFrame();

bool ImGui_ImplOpenGL3_Init(const char* glsl_version);
void ImGui_ImplOpenGL3_Shutdown();
void ImGui_ImplOpenGL3_NewFrame();
void ImGui_ImplOpenGL3_RenderDrawData(void* draw_data);

struct ImDrawData;
ImDrawData* ImGui_GetDrawData();

// Version check stub
#define IMGUI_CHECKVERSION() do {} while(0)
