#pragma once

#include "BRDFileBase.h"
#include <GL/glew.h>
#include <memory>
#include <imgui.h>
// Forward-declare to avoid heavy include in header
struct PCBThemeSpec;

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
    float aspect_ratio = 1.0f;
    // View rotation in 90-degree steps (0..3) clockwise. 0 = normal, 1 = 90CW, 2 = 180, 3 = 270CW
    int rotation_steps = 0;
    // Optional mirroring (flip) flags. Applied after rotation around board center.
    bool flip_horizontal = false; // left-right flip
    bool flip_vertical   = false; // up-down flip
};

struct RenderSettings {
    bool show_parts = true;
    bool show_pins = true;
    bool show_outline = true;
    bool show_part_outlines = true;
    bool show_nets = false;
    bool show_diode_readings = true; // control displaying diode readings in pin text overlay
    bool show_ratsnet = false; // control displaying ratsnet/airwires
    // When true, renderer ignores per-geometry pin colors and uses pin_color from theme
    bool override_pin_colors = false;
    
    float part_alpha = 1.0f;
    float pin_alpha = 1.0f;
    float outline_alpha = 1.0f;
    float part_outline_alpha = 1.0f;
    
    struct {
        float r = 0.2f, g = 0.8f, b = 0.2f;  // Green
    } part_color;
    
    struct {
    float r = 1.0f, g = 1.0f, b = 0.0f;  // Default accent for pins when override is enabled
    } pin_color;
    
    struct {
        float r = 1.0f, g = 1.0f, b = 1.0f;  // White
    } outline_color;

    struct {
        float r = 1.0f, g = 1.0f, b = 1.0f;  // White
    } part_outline_color;
    // Pin override colors for special cases and net highlighting
    struct {
        float r = 1.0f, g = 1.0f, b = 0.0f; // Yellow for same-net highlight
    } pin_same_net_color;

    struct {
        float r = 0.0f, g = 0.3f, b = 0.3f; // Teal for NC pins
    } pin_nc_color;

    struct {
        float r = 0.376f, g = 0.376f, b = 0.376f; // Gray for GND-family pins
    } pin_ground_color;
    
    struct {
        float r = 0.0f, g = 1.0f, b = 1.0f;  // Cyan
    } ratsnet_color;
    
    struct {
    float r = 0.0f, g = 0.0f, b = 0.0f;  // Default black background (legacy behavior)
    } background_color;

    // Part highlight colors
    struct {
        float r = 1.0f, g = 1.0f, b = 0.0f; // Yellow border
    } part_highlight_border_color;

    struct {
        float r = 1.0f, g = 1.0f, b = 0.0f; // Yellow fill (alpha controlled separately)
    } part_highlight_fill_color;
};

// Predefined color themes
enum class ColorTheme {
    Default = 0,
    Light   = 1,
    HighContrast = 2,
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
    void RenderCirclePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height);
    void RenderRectanglePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height);
    void RenderOvalPinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height);
    void RenderPartNamesOnTop(ImDrawList* draw_list);  // Render collected part names on top
    void RenderPinNumbersAsText(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height); // Render pin numbers as text overlays
    void CollectPartNamesForRendering(float zoom, float offset_x, float offset_y); // Collect part names for rendering
    void RenderPartHighlighting(ImDrawList* draw_list, float zoom, float offset_x, float offset_y); // Render part highlighting on top
    void RenderRatsnetImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height); // Render ratsnet/airwires
    
    // Camera controls
    void SetCamera(float x, float y, float zoom);
    void ZoomToFit(int window_width, int window_height);
    void Pan(float dx, float dy);
    void Zoom(float factor, float center_x = 0.0f, float center_y = 0.0f);
    // Compute the minimum zoom that keeps the whole board visible in the current rotation
    float ComputeFitZoom(int window_width, int window_height) const;
    // Rotation (90-degree increments). Automatically re-fit view to keep entire board visible.
    void RotateLeft();   // 90 degrees counter-clockwise
    void RotateRight();  // 90 degrees clockwise
    int  GetRotationSteps() const { return camera.rotation_steps; }
    // Flips (mirror). Each toggles the state.
    void ToggleFlipHorizontal();
    void ToggleFlipVertical();
    bool IsFlipHorizontal() const { return camera.flip_horizontal; }
    bool IsFlipVertical() const { return camera.flip_vertical; }
    // Diode readings visibility
    void ToggleDiodeReadings() { settings.show_diode_readings = !settings.show_diode_readings; }
    void SetDiodeReadingsEnabled(bool enabled) { settings.show_diode_readings = enabled; }
    bool IsDiodeReadingsEnabled() const { return settings.show_diode_readings; }
    
    // Ratsnet/airwires visibility
    void ToggleRatsnet() { settings.show_ratsnet = !settings.show_ratsnet; }
    void SetRatsnetEnabled(bool enabled) { settings.show_ratsnet = enabled; }
    bool IsRatsnetEnabled() const { return settings.show_ratsnet; }

    // External net highlighting (independent from selected pin net)
    void SetHighlightedNet(const std::string &net) { highlighted_net = net; }
    void ClearHighlightedNet() { highlighted_net.clear(); }
    const std::string &GetHighlightedNet() const { return highlighted_net; }
    void SetHighlightedPart(int partIndex) { highlighted_part_index = partIndex; }
    void ClearHighlightedPart() { highlighted_part_index = -1; }
    int GetHighlightedPart() const { return highlighted_part_index; }
    
    // Pin selection functionality
    bool HandleMouseClick(float screen_x, float screen_y, int window_width, int window_height);
    void ClearSelection();
    int GetSelectedPinIndex() const { return selected_pin_index; }
    bool HasSelectedPin() const { return selected_pin_index >= 0; }
    
    // Hover functionality
    int GetHoveredPin(float screen_x, float screen_y, int window_width, int window_height);
    void SetHoveredPin(int pin_index) { hovered_pin_index = pin_index; }
    // Non-destructive hit-test for parts: returns part index or -1
    int HitTestPart(float screen_x, float screen_y, int window_width, int window_height) const;
    
    // Settings
    RenderSettings& GetSettings() { return settings; }
    const Camera& GetCamera() const { return camera; }

    // Themes
    void SetColorTheme(ColorTheme theme);
    ColorTheme GetColorTheme() const { return current_theme; }
    // Apply a theme spec loaded at runtime (e.g., from JSON). Optionally set current_theme to a base.
    void ApplyTheme(const PCBThemeSpec& spec, bool setBaseFromSpec = true);

private:
    // OpenGL objects
    GLuint shader_program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    
    // Data
    std::shared_ptr<BRDFileBase> pcb_data;
    Camera camera;
    RenderSettings settings;
    ColorTheme current_theme = ColorTheme::Default;
    
    // Selection state
    int selected_pin_index = -1;  // -1 means no selection
    int hovered_pin_index = -1;   // -1 means no hover
    
    // Performance optimization caches
    struct PinGeometryCache {
        size_t circle_index = SIZE_MAX;
        size_t rectangle_index = SIZE_MAX;
        size_t oval_index = SIZE_MAX;
        float radius = 0.0f;
        bool is_ground = false;
        bool is_nc = false;
    };
    std::vector<PinGeometryCache> pin_geometry_cache;
    
    // Part name rendering (collected during rendering, drawn on top)
    std::vector<PartNameInfo> part_names_to_render;
    
    // Pin number rendering (collected during rendering, drawn on top)
    std::vector<PinNumberInfo> pin_numbers_to_render;

    // Shader compilation
    bool CreateShaderProgram();
    GLuint CompileShader(const char* source, GLenum type);
    
    // Utility helpers
    bool IsGroundNet(const std::string& net) const;
    
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
    void RenderConnectorComponentImGui(ImDrawList* draw_list, const BRDPart& part, const std::vector<BRDPin>& part_pins, float zoom, float offset_x, float offset_y);
    
    // Performance optimization methods
    void BuildPinGeometryCache();
    bool IsElementVisible(float x, float y, float radius, float zoom, float offset_x, float offset_y, int window_width, int window_height);
    
    // Pin utilities
    bool IsGroundPin(const BRDPin& pin);
    bool IsNCPin(const BRDPin& pin);
    bool IsUnconnectedPin(const BRDPin& pin);
    bool IsConnectorComponent(const BRDPart& part);
    
    // Coordinate conversion
    void WorldToScreen(float world_x, float world_y, float& screen_x, float& screen_y, 
                      int window_width, int window_height);
    void ScreenToWorld(float screen_x, float screen_y, float& world_x, float& world_y,
                      int window_width, int window_height);
    void ApplyRotation(float& x, float& y, bool inverse) const; // forward (world->rotated) or inverse
    
    // Utility
    void SetProjectionMatrix(int window_width, int window_height);
    void DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a = 1.0f);
    void DrawRect(float x, float y, float width, float height, float r, float g, float b, float a = 1.0f);
    void DrawCircle(float x, float y, float radius, float r, float g, float b, float a = 1.0f);
    // Cached board center for rotation pivot
    float board_cx = 0.0f;
    float board_cy = 0.0f;

    // Currently externally highlighted net (via dropdown search)
    std::string highlighted_net;
    int highlighted_part_index = -1;
};
