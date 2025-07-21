#pragma once

#include "BRDFileBase.h"
#include <GL/glew.h>
#include <memory>
#include <imgui.h>

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
    float aspect_ratio = 1.0f;
};

struct RenderSettings {
    bool show_parts = true;
    bool show_pins = true;
    bool show_outline = true;
    bool show_part_outlines = true;
    bool show_nets = false;
    bool enable_imgui_overlay = false;  // Added ImGui overlay control
    
    float part_alpha = 1.0f;
    float pin_alpha = 1.0f;
    float outline_alpha = 1.0f;
    float part_outline_alpha = 1.0f;
    
    struct {
        float r = 0.2f, g = 0.8f, b = 0.2f;  // Green
    } part_color;
    
    struct {
        float r = 1.0f, g = 1.0f, b = 0.0f;  // Yellow
    } pin_color;
    
    struct {
        float r = 1.0f, g = 1.0f, b = 1.0f;  // White
    } outline_color;

    struct {
        float r = 1.0f, g = 1.0f, b = 1.0f;  // White
    } part_outline_color;
      struct {
        float r = 0.0f, g = 0.3f, b = 0.0f;  // Dark green PCB background
    } background_color;
};

// Structure to hold part name rendering information
struct PartNameInfo {
    ImVec2 position;
    ImVec2 size;
    std::string text;
    ImU32 color;
    ImVec2 clip_min;
    ImVec2 clip_max;
    ImU32 background_color;
};

// Structure to hold pin number rendering information
struct PinNumberInfo {
    ImVec2 position;
    ImVec2 size;
    std::string pin_number;
    std::string net_name;
    ImU32 pin_color;
    ImU32 net_color;
    ImU32 background_color;
    float pin_radius;
    bool show_background;
};

class PCBRenderer {
public:
    PCBRenderer();
    ~PCBRenderer();

    bool Initialize();
    void Cleanup();
    
    void SetPCBData(std::shared_ptr<BRDFileBase> pcb_data);
    void Render(int window_width, int window_height);
    
    // ImGui-based rendering methods (like original OpenBoardView)
    void RenderOutlineImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y);
    void RenderPartOutlineImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y);
    void RenderCirclePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y);
    void RenderRectanglePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y);
    void RenderOvalPinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y);
    void RenderPartNamesOnTop(ImDrawList* draw_list);  // Render collected part names on top
    void RenderPinNumbersAsText(ImDrawList* draw_list, float zoom, float offset_x, float offset_y); // Render pin numbers as text overlays
    void CollectPartNamesForRendering(float zoom, float offset_x, float offset_y); // Collect part names for rendering
    void RenderPartHighlighting(ImDrawList* draw_list, float zoom, float offset_x, float offset_y); // Render part highlighting on top
    
    // Camera controls
    void SetCamera(float x, float y, float zoom);
    void ZoomToFit(int window_width, int window_height);
    void Pan(float dx, float dy);
    void Zoom(float factor, float center_x = 0.0f, float center_y = 0.0f);
    
    // Pin selection functionality
    bool HandleMouseClick(float screen_x, float screen_y, int window_width, int window_height);
    void ClearSelection();
    int GetSelectedPinIndex() const { return selected_pin_index; }
    bool HasSelectedPin() const { return selected_pin_index >= 0; }
    
    // Hover functionality
    int GetHoveredPin(float screen_x, float screen_y, int window_width, int window_height);
    void SetHoveredPin(int pin_index) { hovered_pin_index = pin_index; }
    
    // Settings
    RenderSettings& GetSettings() { return settings; }
    const Camera& GetCamera() const { return camera; }

private:
    // OpenGL objects
    GLuint shader_program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    
    // Data
    std::shared_ptr<BRDFileBase> pcb_data;
    Camera camera;
    RenderSettings settings;
    
    // Selection state
    int selected_pin_index = -1;  // -1 means no selection
    int hovered_pin_index = -1;   // -1 means no hover
    
    // Part name rendering (collected during rendering, drawn on top)
    std::vector<PartNameInfo> part_names_to_render;
    
    // Pin number rendering (collected during rendering, drawn on top)
    std::vector<PinNumberInfo> pin_numbers_to_render;

    // Shader compilation
    bool CreateShaderProgram();
    GLuint CompileShader(const char* source, GLenum type);
    
    // Rendering methods
    void RenderBackground();
    void RenderOutline();
    void RenderParts();
    void RenderPins();
      // Enhanced rendering methods
    void RenderPartOutline(const BRDPart& part, const std::vector<BRDPin>& part_pins);
    float DeterminePinMargin(const BRDPart& part, const std::vector<BRDPin>& part_pins, float distance);
    float DeterminePinSize(const BRDPart& part, const std::vector<BRDPin>& part_pins);
    void RenderGenericComponentOutline(float min_x, float min_y, float max_x, float max_y, float margin);
    void RenderConnectorComponentImGui(ImDrawList* draw_list, const BRDPart& part, const std::vector<BRDPin>& part_pins, float zoom, float offset_x, float offset_y);    // Pin utilities
    bool IsGroundPin(const BRDPin& pin);
    bool IsNCPin(const BRDPin& pin);
    bool IsUnconnectedPin(const BRDPin& pin);
    bool IsConnectorComponent(const BRDPart& part);
    
    // Coordinate conversion
    void WorldToScreen(float world_x, float world_y, float& screen_x, float& screen_y, 
                      int window_width, int window_height);
    void ScreenToWorld(float screen_x, float screen_y, float& world_x, float& world_y,
                      int window_width, int window_height);
    
    // Utility
    void SetProjectionMatrix(int window_width, int window_height);
    void DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a = 1.0f);
    void DrawRect(float x, float y, float width, float height, float r, float g, float b, float a = 1.0f);
    void DrawCircle(float x, float y, float radius, float r, float g, float b, float a = 1.0f);
};
