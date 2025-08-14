#include "PCBRenderer.h"
#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <set>
#include <imgui.h>
#include <cctype>

// Simple vertex shader - kept for reference but not used in ImGui rendering
const char* vertex_shader_source = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 projection;
out vec3 vertexColor;

void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    vertexColor = aColor;
}
)";

// Simple fragment shader - kept for reference but not used in ImGui rendering
const char* fragment_shader_source = R"(
#version 330 core
in vec3 vertexColor;
out vec4 FragColor;

uniform float alpha;

void main() {
    FragColor = vec4(vertexColor, alpha);
}
)";

PCBRenderer::PCBRenderer() {
}

PCBRenderer::~PCBRenderer() {
    Cleanup();
}

bool PCBRenderer::Initialize() {
    // Create minimal OpenGL resources for compatibility
    // Note: These resources aren't actively used for rendering anymore (ImGui handles the drawing)
    // but are kept for compatibility with the OpenGL context
    
    if (!CreateShaderProgram()) {
        LOG_ERROR("Failed to create shader program");
        return false;
    }

    // Create VAO and VBO
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    LOG_INFO("PCB Renderer initialized successfully");
    return true;
}

void PCBRenderer::Cleanup() {
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (shader_program) {
        glDeleteProgram(shader_program);
        shader_program = 0;
    }
}

void PCBRenderer::SetPCBData(std::shared_ptr<BRDFileBase> data) {
    pcb_data = data;
    
    if (pcb_data && pcb_data->IsValid()) {
        LOG_INFO("PCB data set: " + std::to_string(pcb_data->parts.size()) + 
                " parts, " + std::to_string(pcb_data->pins.size()) + " pins");
        
        // Build performance optimization cache
        BuildPinGeometryCache();

    // Cache board center for rotation pivot
    BRDPoint min_point, max_point;
    pcb_data->GetBoundingBox(min_point, max_point);
    board_cx = static_cast<float>((min_point.x + max_point.x) * 0.5);
    board_cy = static_cast<float>((min_point.y + max_point.y) * 0.5);
    }
}

void PCBRenderer::Render(int window_width, int window_height) {
    if (!pcb_data || !pcb_data->IsValid()) {
        LOG_INFO("No PCB data to render");
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // Clear screen with PCB green background
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Initialize camera if needed (first time rendering)
    static bool camera_initialized = false;
    if (!camera_initialized) {
        ZoomToFit(window_width, window_height);
        camera_initialized = true;
    }
    
    // Calculate screen transform from camera
    float zoom = camera.zoom;
    float offset_x = window_width * 0.5f - camera.x * zoom;
    float offset_y = window_height * 0.5f + camera.y * zoom;  // Mirror Y-axis
    
    // Apply camera transformation

    // Create a fullscreen ImGui window for PCB rendering
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
    
    bool window_open = ImGui::Begin("PCB View", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBackground);

    if (!window_open) {
        LOG_ERROR("Failed to create ImGui window");
        ImGui::End();
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (!draw_list) {
        LOG_ERROR("Failed to get ImGui draw list");
        ImGui::End();
        return;
    }    // Use structured ImGui rendering methods (like original OpenBoardView)
    RenderOutlineImGui(draw_list, zoom, offset_x, offset_y);
    if (settings.show_part_outlines) {
        RenderPartOutlineImGui(draw_list, zoom, offset_x, offset_y);
    }
    RenderCirclePinsImGui(draw_list, zoom, offset_x, offset_y, window_width, window_height);
    RenderRectanglePinsImGui(draw_list, zoom, offset_x, offset_y, window_width, window_height);
    RenderOvalPinsImGui(draw_list, zoom, offset_x, offset_y, window_width, window_height);

    // Collect part names for rendering on top
    CollectPartNamesForRendering(zoom, offset_x, offset_y);

    // Render part names on top of all other graphics
    RenderPartNamesOnTop(draw_list);

    // Render pin numbers as text overlays
    RenderPinNumbersAsText(draw_list, zoom, offset_x, offset_y, window_width, window_height);
    
    // Render part highlighting on top of everything
    RenderPartHighlighting(draw_list, zoom, offset_x, offset_y);

    ImGui::End();
}

void PCBRenderer::SetCamera(float x, float y, float zoom) {
    camera.x = x;
    camera.y = y;
    camera.zoom = zoom;
}

void PCBRenderer::Pan(float dx, float dy) {
    camera.x += dx / camera.zoom;
    camera.y += dy / camera.zoom;
}

void PCBRenderer::Zoom(float factor, float center_x, float center_y) {
    float old_zoom = camera.zoom;
    
    // Apply zoom factor
    camera.zoom *= factor;
    
    // Limit zoom range
    if (camera.zoom < 0.01f) camera.zoom = 0.01f;
    if (camera.zoom > 100.0f) camera.zoom = 100.0f;
    
    // If a center point is specified, adjust camera so that world point stays in same screen position
    if (center_x != 0.0f || center_y != 0.0f) {
        // The idea: after zooming, we want center_x,center_y to appear at the same screen position
        // Before zoom: screen_pos = (world_point - camera_old) * zoom_old
        // After zoom: screen_pos = (world_point - camera_new) * zoom_new
        // Since screen_pos should be the same: (center - old_camera) * old_zoom = (center - new_camera) * new_zoom
        // Solving for new_camera: new_camera = center - (center - old_camera) * old_zoom / new_zoom
        
        float old_x = camera.x;
        float old_y = camera.y;
        
        camera.x = center_x - (center_x - old_x) * old_zoom / camera.zoom;
        camera.y = center_y - (center_y - old_y) * old_zoom / camera.zoom;
    }
}

void PCBRenderer::RotateLeft() {
    // Counter-clockwise rotation = 270 CW: decrement steps
    if (!pcb_data) return;
    camera.rotation_steps = (camera.rotation_steps + 3) % 4; // -1 mod 4
    // Re-fit to ensure full visibility due to aspect swap
    ZoomToFit(ImGui::GetIO().DisplaySize.x > 0 ? (int)ImGui::GetIO().DisplaySize.x : 800,
              ImGui::GetIO().DisplaySize.y > 0 ? (int)ImGui::GetIO().DisplaySize.y : 600);
}

void PCBRenderer::RotateRight() {
    if (!pcb_data) return;
    camera.rotation_steps = (camera.rotation_steps + 1) % 4;
    ZoomToFit(ImGui::GetIO().DisplaySize.x > 0 ? (int)ImGui::GetIO().DisplaySize.x : 800,
              ImGui::GetIO().DisplaySize.y > 0 ? (int)ImGui::GetIO().DisplaySize.y : 600);
}

void PCBRenderer::ToggleFlipHorizontal() {
    if (!pcb_data) return;
    camera.flip_horizontal = !camera.flip_horizontal;
}

void PCBRenderer::ToggleFlipVertical() {
    if (!pcb_data) return;
    camera.flip_vertical = !camera.flip_vertical;
}

void PCBRenderer::ZoomToFit(int window_width, int window_height) {
    if (!pcb_data) return;
    
    BRDPoint min_point, max_point;
    pcb_data->GetBoundingBox(min_point, max_point);
    
    float pcb_width = static_cast<float>(max_point.x - min_point.x);
    float pcb_height = static_cast<float>(max_point.y - min_point.y);
    
    if (pcb_width <= 0 || pcb_height <= 0) return;
    
    // Calculate zoom to fit with margin
    // Swap width/height when rotated by 90 or 270 degrees
    bool swap_wh = (camera.rotation_steps % 2) == 1;
    float effective_width  = swap_wh ? pcb_height : pcb_width;
    float effective_height = swap_wh ? pcb_width  : pcb_height;
    float zoom_x = window_width / (effective_width * 1.2f);
    float zoom_y = window_height / (effective_height * 1.2f);
    camera.zoom = std::min(zoom_x, zoom_y);
    
    // Center the view
    camera.x = (min_point.x + max_point.x) * 0.5f;
    camera.y = (min_point.y + max_point.y) * 0.5f;
}

// Legacy OpenGL shader functions - kept for reference but not needed for ImGui rendering
bool PCBRenderer::CreateShaderProgram() {
    // Creating a minimal shader program to satisfy OpenGL initialization
    GLuint vertex_shader = CompileShader(vertex_shader_source, GL_VERTEX_SHADER);
    if (!vertex_shader) return false;
    
    GLuint fragment_shader = CompileShader(fragment_shader_source, GL_FRAGMENT_SHADER);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return false;
    }

    // Create program
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    // Check for linking errors
    GLint success;
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(shader_program, 512, nullptr, info_log);
        LOG_ERROR("Shader program linking failed: " + std::string(info_log));
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }

    // Delete shaders (they're linked into the program now)
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return true;
}

GLuint PCBRenderer::CompileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    // Check for compilation errors
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, nullptr, info_log);
        LOG_ERROR("Shader compilation failed: " + std::string(info_log));
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// void PCBRenderer::SetProjectionMatrix(int window_width, int window_height) {
//     camera.aspect_ratio = static_cast<float>(window_width) / window_height;
    
//     // Create orthographic projection matrix
//     float left = camera.x - window_width / (2.0f * camera.zoom);
//     float right = camera.x + window_width / (2.0f * camera.zoom);
//     float bottom = camera.y - window_height / (2.0f * camera.zoom);
//     float top = camera.y + window_height / (2.0f * camera.zoom);

//     // Simple orthographic projection matrix
//     float projection[16] = {
//         2.0f / (right - left), 0.0f, 0.0f, -(right + left) / (right - left),
//         0.0f, 2.0f / (top - bottom), 0.0f, -(top + bottom) / (top - bottom),
//         0.0f, 0.0f, -1.0f, 0.0f,
//         0.0f, 0.0f, 0.0f, 1.0f
//     };

//     GLint projection_loc = glGetUniformLocation(shader_program, "projection");
//     glUniformMatrix4fv(projection_loc, 1, GL_FALSE, projection);
// }

// void PCBRenderer::RenderOutline() {
//     if (pcb_data->outline_segments.empty()) {
//         return;
//     }

//     GLint alpha_loc = glGetUniformLocation(shader_program, "alpha");
//     glUniform1f(alpha_loc, settings.outline_alpha);

    // Render outline segments
//     for (const auto& segment : pcb_data->outline_segments) {
//         DrawLine(static_cast<float>(segment.first.x), static_cast<float>(segment.first.y),
//                 static_cast<float>(segment.second.x), static_cast<float>(segment.second.y),
//                 settings.outline_color.r, settings.outline_color.g, settings.outline_color.b);
//     }
// }

// void PCBRenderer::RenderParts() {
//     GLint alpha_loc = glGetUniformLocation(shader_program, "alpha");
//     glUniform1f(alpha_loc, settings.part_alpha);

//     // Enhanced part rendering with proper outline detection
//     for (size_t i = 0; i < pcb_data->parts.size(); ++i) {
//         const auto& part = pcb_data->parts[i];
        
//         // Get pins for this part
//         std::vector<BRDPin> part_pins;
//         for (const auto& pin : pcb_data->pins) {
//             if (pin.part == i + 1) { // Parts are 1-indexed
//                 part_pins.push_back(pin);
//             }
//         }
        
//         if (part_pins.empty()) {
//             // Draw a simple rectangle for parts with no pins
//             float x = static_cast<float>(part.p1.x);
//             float y = static_cast<float>(part.p1.y);
//             float width = static_cast<float>(part.p2.x - part.p1.x);
//             float height = static_cast<float>(part.p2.y - part.p1.y);
            
//             if (width > 0 && height > 0) {
//                 DrawRect(x, y, width, height, 
//                         settings.part_color.r, settings.part_color.g, settings.part_color.b);
//             }
//             continue;
//         }
        
//         // Calculate part outline based on pins - enhanced logic
//         RenderPartOutline(part, part_pins);
//     }
// }

// Legacy OpenGL part outline function - not used with ImGui rendering
// void PCBRenderer::RenderPartOutline(const BRDPart& part, const std::vector<BRDPin>& part_pins) {
//     if (part_pins.empty()) return;
    
//     // Calculate bounding box
//     float min_x = part_pins[0].pos.x, max_x = part_pins[0].pos.x;
//     float min_y = part_pins[0].pos.y, max_y = part_pins[0].pos.y;
    
//     for (const auto& pin : part_pins) {
//         min_x = std::min(min_x, static_cast<float>(pin.pos.x));
//         max_x = std::max(max_x, static_cast<float>(pin.pos.x));
//         min_y = std::min(min_y, static_cast<float>(pin.pos.y));
//         max_y = std::max(max_y, static_cast<float>(pin.pos.y));
//     }
    
//     // Calculate distance and pin spacing for component type detection
//     float distance = std::sqrt((max_x - min_x) * (max_x - min_x) + (max_y - min_y) * (max_y - min_y));
//     int pin_count = part_pins.size();
    
//     // Component type detection and outline generation
//     float outline_margin = DeterminePinMargin(part, part_pins, distance);
// }

float PCBRenderer::DeterminePinMargin(const BRDPart& part, const std::vector<BRDPin>& part_pins, float distance) {
    int pin_count = part_pins.size();
      // Enhanced component type detection based on OpenBoardView logic - REDUCED MARGINS
    if (pin_count < 4 && !part.name.empty() && part.name[0] != 'U' && part.name[0] != 'Q') {
        // 2-3 pin components - likely passives (reduced margins by ~30-40%)
        if (distance > 52 && distance < 57) {
            return 5.0f; // 0603 - reduced from 8.0f
        } else if (distance > 247 && distance < 253) {
            return 15.0f; // SMC diode - reduced from 25.0f
        } else if (distance > 195 && distance < 199) {
            return 15.0f; // Inductor - reduced from 25.0f
        } else if (distance > 165 && distance < 169) {
            return 12.0f; // SMB diode - reduced from 18.0f
        } else if (distance > 101 && distance < 109) {
            return 10.0f; // SMA diode / tant cap - reduced from 15.0f
        } else if (distance > 108 && distance < 112) {
            return 10.0f; // 1206 - reduced from 15.0f
        } else if (distance > 64 && distance < 68) {
            return 8.0f; // 0805 - reduced from 13.0f
        } else if (distance > 18 && distance < 22) {
            return 2.0f; // 0201 - reduced from 3.0f
        } else if (distance > 28 && distance < 32) {
            return 3.0f; // 0402 - reduced from 5.0f
        }
    }
    
    // Default margins for other components (reduced by ~30-40%)
    if (pin_count <= 4) return 6.0f;   // reduced from 10.0f
    if (pin_count <= 16) return 9.0f;  // reduced from 15.0f
    if (pin_count <= 32) return 12.0f; // reduced from 20.0f
    return 15.0f; // Large ICs - reduced from 25.0f
}

// void PCBRenderer::RenderGenericComponentOutline(float min_x, float min_y, float max_x, float max_y, float margin) {
//     float x = min_x - margin;
//     float y = min_y - margin;
//     float w = (max_x - min_x) + 2 * margin;
//     float h = (max_y - min_y) + 2 * margin;
    
//     // Draw filled rectangle with transparency
//     std::vector<float> vertices = {
//         x, y,     settings.part_color.r, settings.part_color.g, settings.part_color.b,
//         x+w, y,   settings.part_color.r, settings.part_color.g, settings.part_color.b,
//         x+w, y+h, settings.part_color.r, settings.part_color.g, settings.part_color.b,
//         x, y,     settings.part_color.r, settings.part_color.g, settings.part_color.b,
//         x+w, y+h, settings.part_color.r, settings.part_color.g, settings.part_color.b,
//         x, y+h,   settings.part_color.r, settings.part_color.g, settings.part_color.b
//     };
    
//     glBindBuffer(GL_ARRAY_BUFFER, vbo);
//     glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
    
//     glBindVertexArray(vao);
//     glDrawArrays(GL_TRIANGLES, 0, 6);
    
//     // Draw outline with lines
//     DrawLine(x, y, x + w, y, settings.part_color.r, settings.part_color.g, settings.part_color.b);
//     DrawLine(x + w, y, x + w, y + h, settings.part_color.r, settings.part_color.g, settings.part_color.b);
//     DrawLine(x + w, y + h, x, y + h, settings.part_color.r, settings.part_color.g, settings.part_color.b);
//     DrawLine(x, y + h, x, y, settings.part_color.r, settings.part_color.g, settings.part_color.b);
// }

// void PCBRenderer::RenderPins() {
//     GLint alpha_loc = glGetUniformLocation(shader_program, "alpha");
//     glUniform1f(alpha_loc, settings.pin_alpha);

//     for (const auto& pin : pcb_data->pins) {
//         float x = static_cast<float>(pin.pos.x);
//         float y = static_cast<float>(pin.pos.y);
        
//         // Enhanced pin sizing based on component type
//         float radius = static_cast<float>(pin.radius);
//         if (radius < 1.0f) {
//             // Determine pin size based on part type
//             if (pin.part > 0 && pin.part <= pcb_data->parts.size()) {
//                 const auto& part = pcb_data->parts[pin.part - 1];
                
//                 // Get all pins for this part to determine spacing
//                 std::vector<BRDPin> part_pins;
//                 for (const auto& p : pcb_data->pins) {
//                     if (p.part == pin.part) {
//                         part_pins.push_back(p);
//                     }
//                 }
                
//                 radius = DeterminePinSize(part, part_pins);
//             } else {
//                 radius = 7.0f; // Default size
//             }
//         }
        
//         // Draw pin with appropriate color
//         float pin_r = settings.pin_color.r;
//         float pin_g = settings.pin_color.g;
//         float pin_b = settings.pin_color.b;
//           // Color coding based on net connectivity
//         if (!pin.net.empty() && pin.net != "UNCONNECTED") {
//             // Connected pins get full color
//             DrawCircle(x, y, radius, pin_r, pin_g, pin_b);
//         } else {
//             // Unconnected pins get dimmed color
//             DrawCircle(x, y, radius, pin_r * 0.5f, pin_g * 0.5f, pin_b * 0.5f);
//         }
        
//         // Draw pin name if available and zoom level is high enough
//         if (!pin.name.empty() && camera.zoom > 0.5f) {
//             // Draw a small dot to indicate named pin
//             DrawCircle(x, y, radius * 0.3f, 1.0f, 1.0f, 1.0f); // White center
//         }
//     }
// }

float PCBRenderer::DeterminePinSize(const BRDPart& part, const std::vector<BRDPin>& part_pins) {
    if (part_pins.empty()) return 7.0f;
    
    int pin_count = part_pins.size();
    
    // Calculate pin spacing for size determination
    if (pin_count >= 2) {
        float min_x = part_pins[0].pos.x, max_x = part_pins[0].pos.x;
        float min_y = part_pins[0].pos.y, max_y = part_pins[0].pos.y;
        
        for (const auto& pin : part_pins) {
            min_x = std::min(min_x, static_cast<float>(pin.pos.x));
            max_x = std::max(max_x, static_cast<float>(pin.pos.x));
            min_y = std::min(min_y, static_cast<float>(pin.pos.y));
            max_y = std::max(max_y, static_cast<float>(pin.pos.y));
        }
        
        float distance = std::sqrt((max_x - min_x) * (max_x - min_x) + (max_y - min_y) * (max_y - min_y));
          // Pin size determination based on OpenBoardView logic
        if (pin_count < 4 && !part.name.empty() && part.name[0] != 'U' && part.name[0] != 'Q') {
            if (distance > 52 && distance < 57) return 15.0f;  // 0603
            if (distance > 247 && distance < 253) return 50.0f; // SMC diode
            if (distance > 195 && distance < 199) return 50.0f; // Inductor
            if (distance > 165 && distance < 169) return 35.0f; // SMB diode
            if (distance > 101 && distance < 109) return 30.0f; // SMA diode
            if (distance > 108 && distance < 112) return 30.0f; // 1206
            if (distance > 64 && distance < 68) return 25.0f;   // 0805
            if (distance > 18 && distance < 22) return 5.0f;    // 0201
            if (distance > 28 && distance < 32) return 10.0f;   // 0402
        }
    }
    
    // Default sizes based on pin count
    if (pin_count <= 2) return 12.0f;
    if (pin_count <= 4) return 10.0f;
    if (pin_count <= 16) return 8.0f;
    if (pin_count <= 32) return 6.0f;
    return 4.0f; // High pin count ICs
}

// void PCBRenderer::DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a) {
//     // Create line vertices (position + color)
//     float vertices[] = {
//         x1, y1, r, g, b,
//         x2, y2, r, g, b
//     };

//     glBindVertexArray(vao);
//     glBindBuffer(GL_ARRAY_BUFFER, vbo);
//     glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

//     glDrawArrays(GL_LINES, 0, 2);
// }

// void PCBRenderer::DrawRect(float x, float y, float width, float height, float r, float g, float b, float a) {
//     // Create rectangle vertices (as two triangles)
//     float vertices[] = {
//         // Triangle 1
//         x, y, r, g, b,
//         x + width, y, r, g, b,
//         x, y + height, r, g, b,
//         // Triangle 2
//         x + width, y, r, g, b,
//         x + width, y + height, r, g, b,
//         x, y + height, r, g, b
//     };

//     glBindVertexArray(vao);
//     glBindBuffer(GL_ARRAY_BUFFER, vbo);
//     glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

//     glDrawArrays(GL_TRIANGLES, 0, 6);
// }

// void PCBRenderer::DrawCircle(float x, float y, float radius, float r, float g, float b, float a) {
//     const int segments = 16;
//     std::vector<float> vertices;
    
//     // Center vertex
//     vertices.insert(vertices.end(), {x, y, r, g, b});
    
//     // Circle vertices
//     for (int i = 0; i <= segments; ++i) {
//         float angle = 2.0f * 3.14159f * i / segments;
//         float px = x + radius * std::cos(angle);
//         float py = y + radius * std::sin(angle);
//         vertices.insert(vertices.end(), {px, py, r, g, b});
//     }

//     glBindVertexArray(vao);
//     glBindBuffer(GL_ARRAY_BUFFER, vbo);
//     glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

//     glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(vertices.size() / 5));
// }

void PCBRenderer::RenderOutlineImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    if (!pcb_data || pcb_data->outline_segments.empty()) {
        LOG_INFO("No outline segments to render");
        return;
    }    // Render board outline
    
    // Set outline color - white/gray for PCB outline
    ImU32 outline_color = IM_COL32(255, 255, 255, 255);
    
    // Adaptive line thickness based on zoom level
    float line_thickness = std::max(1.0f, std::min(4.0f, zoom * 2.0f));  // Thicker when zoomed in
    
    for (const auto& segment : pcb_data->outline_segments) {
        // Apply global rotation first
        float x1 = segment.first.x, y1 = segment.first.y;
        float x2 = segment.second.x, y2 = segment.second.y;
        ApplyRotation(x1, y1, false);
        ApplyRotation(x2, y2, false);
        ImVec2 p1(x1 * zoom + offset_x, offset_y - y1 * zoom);
        ImVec2 p2(x2 * zoom + offset_x, offset_y - y2 * zoom);
        
        // Draw outline segment
        draw_list->AddLine(p1, p2, outline_color, line_thickness);
    }
    
    // Outline rendering complete
}

void PCBRenderer::RenderPartOutlineImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    if (!pcb_data || pcb_data->part_outline_segments.empty()) {
        return;
    }

    // Render part outlines normally
    
    // Use part outline color from settings with alpha
    ImU32 part_outline_color = IM_COL32(
        static_cast<int>(settings.part_outline_color.r * 255),
        static_cast<int>(settings.part_outline_color.g * 255),
        static_cast<int>(settings.part_outline_color.b * 255),
        static_cast<int>(settings.part_outline_alpha * 255)
    );
    
    // Adaptive line thickness based on zoom level (slightly thinner than board outline)
    float line_thickness = std::max(0.5f, std::min(2.0f, zoom * 1.5f));
    
    for (const auto& segment : pcb_data->part_outline_segments) {
        float x1 = segment.first.x, y1 = segment.first.y;
        float x2 = segment.second.x, y2 = segment.second.y;
        ApplyRotation(x1, y1, false);
        ApplyRotation(x2, y2, false);
        ImVec2 p1(x1 * zoom + offset_x, offset_y - y1 * zoom);
        ImVec2 p2(x2 * zoom + offset_x, offset_y - y2 * zoom);
        
        // Draw part outline segment
        draw_list->AddLine(p1, p2, part_outline_color, line_thickness);
    }
    
    // Part outline rendering complete
}

void PCBRenderer::RenderPartHighlighting(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    // Render part highlighting on top of everything
    if (selected_pin_index >= 0 && selected_pin_index < (int)pcb_data->pins.size()) {
        std::string selected_net = pcb_data->pins[selected_pin_index].net;
        
        if (!selected_net.empty() && selected_net != "UNCONNECTED") {
            // Find all parts that have pins on the selected net
            std::set<unsigned int> parts_to_highlight;
            for (const auto& pin : pcb_data->pins) {
                if (pin.net == selected_net && pin.part > 0) {
                    parts_to_highlight.insert(pin.part);
                }
            }
            
            // Highlight each part that has pins on the selected net
            for (unsigned int highlighted_part : parts_to_highlight) {
                // Collect all pins belonging to this part with their geometry info
                struct PinGeometryInfo {
                    BRDPoint pos;
                    float extent_left, extent_right, extent_bottom, extent_top;
                };
                std::vector<PinGeometryInfo> part_pins_info;
                
                for (const auto& pin : pcb_data->pins) {
                    if (pin.part == highlighted_part) {
                        PinGeometryInfo info;
                        info.pos = pin.pos;
                        
                        // Default extents (symmetric for circles)
                        float default_extent = 5.0f;
                        info.extent_left = info.extent_right = info.extent_bottom = info.extent_top = default_extent;
                        
                        // Check if this pin has a circle
                        bool found_geometry = false;
                        for (const auto& circle : pcb_data->circles) {
                            if (circle.center.x == pin.pos.x && circle.center.y == pin.pos.y) {
                                float radius = std::max(default_extent, circle.radius);
                                info.extent_left = info.extent_right = info.extent_bottom = info.extent_top = radius;
                                found_geometry = true;
                                break;
                            }
                        }
                        
                        // Check if this pin has a rectangle
                        if (!found_geometry) {
                            for (const auto& rectangle : pcb_data->rectangles) {
                                if (rectangle.center.x == pin.pos.x && rectangle.center.y == pin.pos.y) {
                                    // For rectangles, calculate extents considering rotation
                                    float half_width = rectangle.width / 2.0f;
                                    float half_height = rectangle.height / 2.0f;
                                    
                                    if (rectangle.rotation == 0.0f) {
                                        // No rotation - simple case
                                        info.extent_left = info.extent_right = half_width;
                                        info.extent_bottom = info.extent_top = half_height;
                                    } else {
                                        // With rotation, calculate the maximum extent in each direction
                                        float rot_rad = rectangle.rotation * 3.14159265f / 180.0f;
                                        float cos_rot = std::abs(std::cos(rot_rad));
                                        float sin_rot = std::abs(std::sin(rot_rad));
                                        
                                        float extent_x = half_width * cos_rot + half_height * sin_rot;
                                        float extent_y = half_width * sin_rot + half_height * cos_rot;
                                        
                                        info.extent_left = info.extent_right = extent_x;
                                        info.extent_bottom = info.extent_top = extent_y;
                                    }
                                    found_geometry = true;
                                    break;
                                }
                            }
                        }
                        
                        // Check if this pin has an oval
                        if (!found_geometry) {
                            for (const auto& oval : pcb_data->ovals) {
                                if (oval.center.x == pin.pos.x && oval.center.y == pin.pos.y) {
                                    // For ovals, calculate extents considering rotation
                                    float half_width = oval.width / 2.0f;
                                    float half_height = oval.height / 2.0f;
                                    
                                    if (oval.rotation == 0.0f) {
                                        // No rotation - simple case
                                        info.extent_left = info.extent_right = half_width;
                                        info.extent_bottom = info.extent_top = half_height;
                                    } else {
                                        // With rotation, calculate the maximum extent in each direction
                                        float rot_rad = oval.rotation * 3.14159265f / 180.0f;
                                        float cos_rot = std::abs(std::cos(rot_rad));
                                        float sin_rot = std::abs(std::sin(rot_rad));
                                        
                                        float extent_x = half_width * cos_rot + half_height * sin_rot;
                                        float extent_y = half_width * sin_rot + half_height * cos_rot;
                                        
                                        info.extent_left = info.extent_right = extent_x;
                                        info.extent_bottom = info.extent_top = extent_y;
                                    }
                                    found_geometry = true;
                                    break;
                                }
                            }
                        }
                        
                        part_pins_info.push_back(info);
                    }
                }
                
                if (part_pins_info.size() >= 1) {
                    // Calculate initial bounding box of the part's pins
                    float min_x = part_pins_info[0].pos.x, max_x = part_pins_info[0].pos.x;
                    float min_y = part_pins_info[0].pos.y, max_y = part_pins_info[0].pos.y;
                    
                    for (const auto& info : part_pins_info) {
                        min_x = std::min(min_x, static_cast<float>(info.pos.x));
                        max_x = std::max(max_x, static_cast<float>(info.pos.x));
                        min_y = std::min(min_y, static_cast<float>(info.pos.y));
                        max_y = std::max(max_y, static_cast<float>(info.pos.y));
                    }
                    
                    // Calculate directional margins based on boundary pins with proper geometry
                    float left_margin = 0.0f, right_margin = 0.0f;
                    float bottom_margin = 0.0f, top_margin = 0.0f;
                    
                    for (const auto& info : part_pins_info) {
                        // Check if this pin affects the left boundary
                        if (info.pos.x == min_x) {
                            left_margin = std::max(left_margin, info.extent_left);
                        }
                        // Check if this pin affects the right boundary
                        if (info.pos.x == max_x) {
                            right_margin = std::max(right_margin, info.extent_right);
                        }
                        // Check if this pin affects the bottom boundary
                        if (info.pos.y == min_y) {
                            bottom_margin = std::max(bottom_margin, info.extent_bottom);
                        }
                        // Check if this pin affects the top boundary
                        if (info.pos.y == max_y) {
                            top_margin = std::max(top_margin, info.extent_top);
                        }
                    }
                    
                    // Apply directional margins
                    min_x -= left_margin;
                    max_x += right_margin;
                    min_y -= bottom_margin;
                    max_y += top_margin;
                    
                    // Transform to screen coordinates with rotation
                    float tlx = min_x, tly = max_y;
                    float brx = max_x, bry = min_y;
                    ApplyRotation(tlx, tly, false);
                    ApplyRotation(brx, bry, false);
                    ImVec2 top_left(tlx * zoom + offset_x, offset_y - tly * zoom);
                    ImVec2 bottom_right(brx * zoom + offset_x, offset_y - bry * zoom);
                    
                    // Draw highlighted bounding box around the part
                    ImU32 highlight_color = IM_COL32(255, 255, 0, 125); // Semi-transparent yellow
                    ImU32 highlight_border = IM_COL32(255, 255, 0, 0);  // More opaque yellow border
                    
                    // Draw filled rectangle
                    draw_list->AddRectFilled(top_left, bottom_right, highlight_color);
                    // Draw border
                    draw_list->AddRect(top_left, bottom_right, highlight_border, 0.0f, 0, 2.0f);
                }
            }
        }
    }
}

void PCBRenderer::RenderCirclePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height) {
    if (!pcb_data || pcb_data->circles.empty() || pin_geometry_cache.empty()) {
        return;
    }
    
    // Pre-calculate selected net for highlighting (avoid string operations in loop)
    std::string selected_net;
    if (selected_pin_index >= 0 && selected_pin_index < (int)pcb_data->pins.size()) {
        selected_net = pcb_data->pins[selected_pin_index].net;
    }
    
    // Render all circles with optimized visibility culling
    for (size_t circle_idx = 0; circle_idx < pcb_data->circles.size(); ++circle_idx) {
        const auto& circle = pcb_data->circles[circle_idx];
        
        // Early visibility culling - skip if circle is outside visible area
        if (!IsElementVisible(circle.center.x, circle.center.y, circle.radius, zoom, offset_x, offset_y, window_width, window_height)) {
            continue;
        }
        
    // Apply global rotation to circle center
    float wx = circle.center.x, wy = circle.center.y;
    ApplyRotation(wx, wy, false);
    float x = wx * zoom + offset_x;
    float y = offset_y - wy * zoom;  // Mirror Y-axis
        
        // Scale radius by zoom factor
        float radius = circle.radius * zoom;
        
        // Ensure minimum visibility
        if (radius < 1.0f) radius = 1.0f;
        
        // Check if this circle corresponds to a pin with cached data for color override
        float r = circle.r, g = circle.g, b = circle.b, a = circle.a;
        
        // Find pin that matches this circle using cache (much faster than linear search)
        for (size_t pin_idx = 0; pin_idx < pcb_data->pins.size() && pin_idx < pin_geometry_cache.size(); ++pin_idx) {
            const auto& pin = pcb_data->pins[pin_idx];
            const auto& cache = pin_geometry_cache[pin_idx];
            
            // Quick check using cached geometry index
            if (cache.circle_index == circle_idx) {
                // Use cached pin type checks
                if (!selected_net.empty() && pin.net == selected_net) {
                    // Highlight all pins on the same net
                    r = 1.0f; g = 1.0f; b = 0.0f; a = 1.0f;
                } else if (cache.is_nc) {
                    // Use blue color for NC pins
                    r = 0.0f; g = 0.3f; b = 0.3f; a = 1.0f;
                } else if (cache.is_ground) {
                    // Use grey color for ground pins
                    r = 0.376f; g = 0.376f; b = 0.376f; a = 1.0f;
                }
                break;
            }
        }
        
        // Convert color components to ImU32 format (0-255 range)
        ImU32 fill_color = IM_COL32(
            (int)(r * 255), 
            (int)(g * 255), 
            (int)(b * 255), 
            (int)(a * 255)
        );
        
        // Draw filled circle
        draw_list->AddCircleFilled(ImVec2(x, y), radius, fill_color);
        
        // Optional: Add a darker outline for better visibility
        ImU32 outline_color = IM_COL32(
            (int)(r * 180), 
            (int)(g * 180), 
            (int)(b * 180), 
            255
        );
        draw_list->AddCircle(ImVec2(x, y), radius, outline_color, 0, 1.0f);
    }
}

void PCBRenderer::RenderRectanglePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height) {
    if (!pcb_data || pcb_data->rectangles.empty() || pin_geometry_cache.empty()) {
        return;
    }
    
    // Pre-calculate selected net for highlighting (avoid string operations in loop)
    std::string selected_net;
    if (selected_pin_index >= 0 && selected_pin_index < (int)pcb_data->pins.size()) {
        selected_net = pcb_data->pins[selected_pin_index].net;
    }
    
    // Render all rectangles with optimized visibility culling
    for (size_t rect_idx = 0; rect_idx < pcb_data->rectangles.size(); ++rect_idx) {
        const auto& rectangle = pcb_data->rectangles[rect_idx];
        
        // Early visibility culling using approximate radius
        float approx_radius = std::max(rectangle.width, rectangle.height) * 0.5f;
        if (!IsElementVisible(rectangle.center.x, rectangle.center.y, approx_radius, zoom, offset_x, offset_y, window_width, window_height)) {
            continue;
        }
        
    // Scale dimensions by zoom factor (store original half sizes in world units first)
    float half_width_world = rectangle.width * 0.5f;
    float half_height_world = rectangle.height * 0.5f;
        
    // Minimum visibility handled after projection; keep world sizes intact here
        
        // Check if this rectangle corresponds to a pin with cached data for color override
        float r = rectangle.r, g = rectangle.g, b = rectangle.b, a = rectangle.a;
        
        // Find pin that matches this rectangle using cache (much faster than linear search)
        for (size_t pin_idx = 0; pin_idx < pcb_data->pins.size() && pin_idx < pin_geometry_cache.size(); ++pin_idx) {
            const auto& pin = pcb_data->pins[pin_idx];
            const auto& cache = pin_geometry_cache[pin_idx];
            
            // Quick check using cached geometry index
            if (cache.rectangle_index == rect_idx) {
                // Use cached pin type checks
                if (!selected_net.empty() && pin.net == selected_net) {
                    // Highlight all pins on the same net
                    r = 1.0f; g = 1.0f; b = 0.0f; a = 1.0f;
                } else if (cache.is_nc) {
                    // Use blue color for NC pins
                    r = 0.0f; g = 0.3f; b = 0.3f; a = 1.0f;
                } else if (cache.is_ground) {
                    // Use grey color for ground pins
                    r = 0.376f; g = 0.376f; b = 0.376f; a = 1.0f;
                }
                break;
            }
        }
        
        // Convert color components to ImU32 format (0-255 range)
        ImU32 fill_color = IM_COL32(
            (int)(r * 255), 
            (int)(g * 255), 
            (int)(b * 255), 
            (int)(a * 255)
        );
        
        // Compute rectangle corner world coordinates first (applying its own rotation), then apply global board rotation per corner
        float rot_rad = rectangle.rotation * 3.14159265f / 180.0f;
        float cos_rot = std::cos(rot_rad);
        float sin_rot = std::sin(rot_rad);

        struct Corner { float x; float y; } worldCorners[4];
        // Local (pre-rectangle-rotation) offsets
        float dx[4] = {-half_width_world,  half_width_world,  half_width_world, -half_width_world};
        float dy[4] = {-half_height_world, -half_height_world, half_height_world,  half_height_world};

        for (int c = 0; c < 4; ++c) {
            // Apply the rectangle's own rotation about its center
            float lx = dx[c];
            float ly = dy[c];
            float rx = lx * cos_rot - ly * sin_rot;
            float ry = lx * sin_rot + ly * cos_rot;
            // World coordinates before board rotation
            worldCorners[c].x = rectangle.center.x + rx;
            worldCorners[c].y = rectangle.center.y + ry;
            // Apply board/global rotation in-place
            ApplyRotation(worldCorners[c].x, worldCorners[c].y, false);
        }

        // Project to screen
        ImVec2 corners[4];
        for (int c = 0; c < 4; ++c) {
            float sx = worldCorners[c].x * zoom + offset_x;
            float sy = offset_y - worldCorners[c].y * zoom;
            corners[c] = ImVec2(sx, sy);
        }

        // Optionally enforce minimum pixel size by early exit if collapsed
        // (skip if projected area extremely small)
        float bbox_min_x = corners[0].x, bbox_max_x = corners[0].x;
        float bbox_min_y = corners[0].y, bbox_max_y = corners[0].y;
        for (int c = 1; c < 4; ++c) {
            bbox_min_x = std::min(bbox_min_x, corners[c].x);
            bbox_max_x = std::max(bbox_max_x, corners[c].x);
            bbox_min_y = std::min(bbox_min_y, corners[c].y);
            bbox_max_y = std::max(bbox_max_y, corners[c].y);
        }
        if ((bbox_max_x - bbox_min_x) < 1.0f || (bbox_max_y - bbox_min_y) < 1.0f) {
            continue; // too small to render meaningfully
        }

        // Draw filled quad
        draw_list->AddQuadFilled(corners[0], corners[1], corners[2], corners[3], fill_color);

        // Outline
        ImU32 outline_color = IM_COL32(
            (int)(r * 180),
            (int)(g * 180),
            (int)(b * 180),
            255
        );
        draw_list->AddQuad(corners[0], corners[1], corners[2], corners[3], outline_color, 1.0f);
    }
}

void PCBRenderer::RenderOvalPinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height) {
    if (!pcb_data || pcb_data->ovals.empty() || pin_geometry_cache.empty()) {
        return;
    }
    
    // Pre-calculate selected net for highlighting (avoid string operations in loop)
    std::string selected_net;
    if (selected_pin_index >= 0 && selected_pin_index < (int)pcb_data->pins.size()) {
        selected_net = pcb_data->pins[selected_pin_index].net;
    }
    
    // Render all ovals as stadium shapes (rounded rectangles) with optimized visibility culling
    for (size_t oval_idx = 0; oval_idx < pcb_data->ovals.size(); ++oval_idx) {
        const auto& oval = pcb_data->ovals[oval_idx];
        
        // Early visibility culling using approximate radius
        float approx_radius = std::max(oval.width, oval.height) * 0.5f;
        if (!IsElementVisible(oval.center.x, oval.center.y, approx_radius, zoom, offset_x, offset_y, window_width, window_height)) {
            continue;
        }
        
    // Precompute world half dimensions for local shape generation
    float half_w_world = oval.width * 0.5f;
    float half_h_world = oval.height * 0.5f;
        
        // Check if this oval corresponds to a pin with cached data for color override
        float r = oval.r, g = oval.g, b = oval.b, a = oval.a;
        
        // Find pin that matches this oval using cache (much faster than linear search)
        for (size_t pin_idx = 0; pin_idx < pcb_data->pins.size() && pin_idx < pin_geometry_cache.size(); ++pin_idx) {
            const auto& pin = pcb_data->pins[pin_idx];
            const auto& cache = pin_geometry_cache[pin_idx];
            
            // Quick check using cached geometry index
            if (cache.oval_index == oval_idx) {
                // Use cached pin type checks
                if (!selected_net.empty() && pin.net == selected_net) {
                    // Highlight all pins on the same net
                    r = 1.0f; g = 1.0f; b = 0.0f; a = 1.0f;
                } else if (cache.is_nc) {
                    // Use blue color for NC pins
                    r = 0.0f; g = 0.3f; b = 0.3f; a = 1.0f;
                } else if (cache.is_ground) {
                    // Use grey color for ground pins
                    r = 0.376f; g = 0.376f; b = 0.376f; a = 1.0f;
                }
                break;
            }
        }
        
        // Pin local rotation
        float rot_pin = oval.rotation * 3.14159265f / 180.0f;
        float cos_pin = std::cos(rot_pin);
        float sin_pin = std::sin(rot_pin);

        // Determine orientation (horizontal vs vertical capsule)
        bool horizontal = half_w_world >= half_h_world;
        float radius = std::min(half_w_world, half_h_world);
        float body_half_len = (horizontal ? half_w_world : half_h_world) - radius;
        const int arc_segments = 12;

        std::vector<ImVec2> pts;

        auto emitPointLocal = [&](float lx, float ly) {
            // Apply pin-local rotation
            float rx = lx * cos_pin - ly * sin_pin;
            float ry = lx * sin_pin + ly * cos_pin;
            // Add center
            float wx = oval.center.x + rx;
            float wy = oval.center.y + ry;
            // Apply global board rotation
            ApplyRotation(wx, wy, false);
            // Project to screen
            float sx = wx * zoom + offset_x;
            float sy = offset_y - wy * zoom;
            pts.emplace_back(sx, sy);
        };

        if (horizontal) {
            // Left arc (top to bottom)
            for (int i = 0; i <= arc_segments; ++i) {
                float ang = 3.14159265f * 0.5f + 3.14159265f * (float)i / arc_segments; // 90 -> 270
                float lx = -body_half_len + radius * std::cos(ang);
                float ly = radius * std::sin(ang);
                emitPointLocal(lx, ly);
            }
            // Right arc (bottom to top)
            for (int i = 0; i <= arc_segments; ++i) {
                float ang = 3.14159265f * 1.5f + 3.14159265f * (float)i / arc_segments; // 270 -> 450
                float lx = body_half_len + radius * std::cos(ang);
                float ly = radius * std::sin(ang);
                emitPointLocal(lx, ly);
            }
        } else {
            // Bottom arc (left to right)
            for (int i = 0; i <= arc_segments; ++i) {
                float ang = 3.14159265f + 3.14159265f * (float)i / arc_segments; // 180 -> 360
                float lx = radius * std::cos(ang);
                float ly = -body_half_len + radius * std::sin(ang);
                emitPointLocal(lx, ly);
            }
            // Top arc (right to left)
            for (int i = 0; i <= arc_segments; ++i) {
                float ang = 2.0f * 3.14159265f + 3.14159265f * (float)i / arc_segments; // 360 -> 540
                float lx = radius * std::cos(ang);
                float ly = body_half_len + radius * std::sin(ang);
                emitPointLocal(lx, ly);
            }
        }

        if (pts.size() >= 3) {
            ImU32 fill_color = IM_COL32((int)(r * 255),(int)(g * 255),(int)(b * 255),(int)(a * 255));
            draw_list->AddConvexPolyFilled(pts.data(), (int)pts.size(), fill_color);
            ImU32 outline_color = IM_COL32((int)(r * 180),(int)(g * 180),(int)(b * 180),255);
            draw_list->AddPolyline(pts.data(), (int)pts.size(), outline_color, ImDrawFlags_Closed, 1.0f);
        }
    }
}

bool PCBRenderer::IsGroundPin(const BRDPin& pin) {
    // Check if pin is a ground pin based on net name
    if (pin.net.empty()) return false;
    
    std::string net_upper = pin.net;
    // Convert to uppercase for case-insensitive comparison
    std::transform(net_upper.begin(), net_upper.end(), net_upper.begin(), ::toupper);
    
    // Common ground net names
    return (net_upper == "GND" || 
            net_upper == "GROUND" || 
            net_upper == "VSS" || 
            net_upper == "AGND" || 
            net_upper == "DGND" || 
            net_upper == "PGND" || 
            net_upper == "SGND" || 
            net_upper.find("GND") == 0 ||  // Starts with GND (GND1, GND2, etc.)
            net_upper.find("GROUND") == 0); // Starts with GROUND
}

bool PCBRenderer::IsNCPin(const BRDPin& pin) {
    // Check if pin is a No Connect (NC) pin based on net name
    if (pin.net.empty()) return false;
    
    std::string net_upper = pin.net;
    // Convert to uppercase for case-insensitive comparison
    std::transform(net_upper.begin(), net_upper.end(), net_upper.begin(), ::toupper);
    
    // Check for NC (No Connect) net names
    return (net_upper == "NC" || 
            net_upper == "NO_CONNECT" || 
            net_upper == "NOCONNECT" ||
            net_upper == "N/C" ||
            net_upper == "N.C." ||
            net_upper.find("NC") == 0);  // Starts with NC (NC1, NC2, etc.)
}

// Performance optimization methods
void PCBRenderer::BuildPinGeometryCache() {
    if (!pcb_data) return;
    
    pin_geometry_cache.clear();
    pin_geometry_cache.resize(pcb_data->pins.size());
    
    LOG_INFO("Building pin geometry cache for " + std::to_string(pcb_data->pins.size()) + " pins");
    
    for (size_t pin_idx = 0; pin_idx < pcb_data->pins.size(); ++pin_idx) {
        const auto& pin = pcb_data->pins[pin_idx];
        auto& cache = pin_geometry_cache[pin_idx];
        
        // Pre-compute pin type checks
        cache.is_ground = IsGroundPin(pin);
        cache.is_nc = IsNCPin(pin);
        
        // Find geometry for this pin
        bool found_geometry = false;
        
        // Check circles
        for (size_t circle_idx = 0; circle_idx < pcb_data->circles.size(); ++circle_idx) {
            const auto& circle = pcb_data->circles[circle_idx];
            if (circle.center.x == pin.pos.x && circle.center.y == pin.pos.y) {
                cache.circle_index = circle_idx;
                cache.radius = circle.radius;
                found_geometry = true;
                break;
            }
        }
        
        // Check rectangles if no circle found
        if (!found_geometry) {
            for (size_t rect_idx = 0; rect_idx < pcb_data->rectangles.size(); ++rect_idx) {
                const auto& rect = pcb_data->rectangles[rect_idx];
                if (rect.center.x == pin.pos.x && rect.center.y == pin.pos.y) {
                    cache.rectangle_index = rect_idx;
                    found_geometry = true;
                    break;
                }
            }
        }
        
        // Check ovals if no other geometry found
        if (!found_geometry) {
            for (size_t oval_idx = 0; oval_idx < pcb_data->ovals.size(); ++oval_idx) {
                const auto& oval = pcb_data->ovals[oval_idx];
                if (oval.center.x == pin.pos.x && oval.center.y == pin.pos.y) {
                    cache.oval_index = oval_idx;
                    found_geometry = true;
                    break;
                }
            }
        }
        
        // Fallback radius if no geometry found
        if (!found_geometry) {
            cache.radius = static_cast<float>(pin.radius);
            if (cache.radius < 1.0f) {
                cache.radius = 6.5f;
            }
        }
    }
    
    LOG_INFO("Pin geometry cache built successfully");
}

bool PCBRenderer::IsElementVisible(float x, float y, float radius, float zoom, float offset_x, float offset_y, int window_width, int window_height) {
    // Apply global rotation to element position before projecting
    float rx = x, ry = y;
    ApplyRotation(rx, ry, false);
    float screen_x = rx * zoom + offset_x;
    float screen_y = offset_y - ry * zoom;
    float screen_radius = radius * zoom;
    
    // Add some margin for smooth culling
    float margin = 10.0f;
    
    // Check if element is within screen bounds
    return (screen_x + screen_radius + margin >= 0 &&
            screen_x - screen_radius - margin <= window_width &&
            screen_y + screen_radius + margin >= 0 &&
            screen_y - screen_radius - margin <= window_height);
}


// Pin selection functionality
bool PCBRenderer::HandleMouseClick(float screen_x, float screen_y, int window_width, int window_height) {
    if (!pcb_data || pcb_data->pins.empty() || pin_geometry_cache.empty()) {
        return false;
    }
    
    // Convert screen coordinates to world coordinates
    float world_x, world_y;
    ScreenToWorld(screen_x, screen_y, world_x, world_y, window_width, window_height);
    
    // Check if click is near any pin using cached geometry data
    for (size_t i = 0; i < pcb_data->pins.size() && i < pin_geometry_cache.size(); ++i) {
        const auto& pin = pcb_data->pins[i];
        const auto& cache = pin_geometry_cache[i];
        
        // Skip ground pins and NC pins using cached data - they are not selectable
        if (cache.is_ground || cache.is_nc) {
            continue;
        }

        // Use cached geometry data for hit testing
        if (cache.rectangle_index != SIZE_MAX) {
            // Rectangle pin: check if click is inside the rectangle (with rotation)
            const auto& rect = pcb_data->rectangles[cache.rectangle_index];
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            // Undo rotation
            float angle_rad = -rect.rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float half_w = rect.width / 2.0f;
            float half_h = rect.height / 2.0f;
            if (std::abs(local_x) <= half_w && std::abs(local_y) <= half_h) {
                if (selected_pin_index == static_cast<int>(i)) {
                    selected_pin_index = -1;
                } else {
                    selected_pin_index = static_cast<int>(i);
                }
                return true;
            }
            continue;
        }

        if (cache.oval_index != SIZE_MAX) {
            // Oval pin: check if click is inside the rotated ellipse (approximate)
            const auto& oval = pcb_data->ovals[cache.oval_index];
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            float angle_rad = -oval.rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float rx = oval.width / 2.0f;
            float ry = oval.height / 2.0f;
            if ((local_x * local_x) / (rx * rx) + (local_y * local_y) / (ry * ry) <= 1.0f) {
                if (selected_pin_index == static_cast<int>(i)) {
                    selected_pin_index = -1;
                } else {
                    selected_pin_index = static_cast<int>(i);
                }
                return true;
            }
            continue;
        }

        // Otherwise, treat as circle (default) using cached radius
        float dx = world_x - pin.pos.x;
        float dy = world_y - pin.pos.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        float circle_radius = cache.radius;
        if (circle_radius < 1.0f) {
            circle_radius = 5.0f;
        }
        
        if (distance <= circle_radius) {
            if (selected_pin_index == static_cast<int>(i)) {
                selected_pin_index = -1;
            } else {
                selected_pin_index = static_cast<int>(i);
            }
            return true;
        }
    }
    
    // Click on empty area - deselect
    selected_pin_index = -1;
    return false; // Click not consumed
}

void PCBRenderer::ClearSelection() {
    selected_pin_index = -1;
}

int PCBRenderer::GetHoveredPin(float screen_x, float screen_y, int window_width, int window_height) {
    if (!pcb_data || pcb_data->pins.empty() || pin_geometry_cache.empty()) {
        return -1;
    }
    
    // Convert screen coordinates to world coordinates
    float world_x, world_y;
    ScreenToWorld(screen_x, screen_y, world_x, world_y, window_width, window_height);
    
    for (size_t i = 0; i < pcb_data->pins.size() && i < pin_geometry_cache.size(); ++i) {
        const auto& pin = pcb_data->pins[i];
        const auto& cache = pin_geometry_cache[i];
        
        // Skip ground pins and NC pins using cached data - they are not hoverable
        if (cache.is_ground || cache.is_nc) {
            continue;
        }

        // Use cached geometry data for hit testing
        if (cache.rectangle_index != SIZE_MAX) {
            // Rectangle pin: check if mouse is inside the rectangle (with rotation)
            const auto& rect = pcb_data->rectangles[cache.rectangle_index];
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            // Undo rotation
            float angle_rad = -rect.rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float half_w = rect.width / 2.0f;
            float half_h = rect.height / 2.0f;
            if (std::abs(local_x) <= half_w && std::abs(local_y) <= half_h) {
                return static_cast<int>(i);
            }
            continue;
        }

        if (cache.oval_index != SIZE_MAX) {
            // Oval pin: check if mouse is inside the rotated ellipse (approximate)
            const auto& oval = pcb_data->ovals[cache.oval_index];
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            float angle_rad = -oval.rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float rx = oval.width / 2.0f;
            float ry = oval.height / 2.0f;
            if ((local_x * local_x) / (rx * rx) + (local_y * local_y) / (ry * ry) <= 1.0f) {
                return static_cast<int>(i);
            }
            continue;
        }

        // Otherwise, treat as circle (default) using cached radius
        float dx = world_x - pin.pos.x;
        float dy = world_y - pin.pos.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        float circle_radius = cache.radius;
        if (circle_radius < 1.0f) {
            circle_radius = 5.0f; // Default fallback for very small pins
        }
        
        if (distance <= circle_radius) {
            return static_cast<int>(i);
        }
    }
    
    return -1; // No pin hovered
}

// Coordinate conversion methods
void PCBRenderer::ScreenToWorld(float screen_x, float screen_y, float& world_x, float& world_y,
                               int window_width, int window_height) {
    // Convert to unrotated world first
    float xr = camera.x + (screen_x - window_width * 0.5f) / camera.zoom;
    float yr = camera.y + (window_height * 0.5f - screen_y) / camera.zoom;
    // Inverse rotation around board center
    world_x = xr; world_y = yr;
    ApplyRotation(world_x, world_y, true);
}

void PCBRenderer::WorldToScreen(float world_x, float world_y, float& screen_x, float& screen_y,
                               int window_width, int window_height) {
    float xr = world_x, yr = world_y;
    ApplyRotation(xr, yr, false);
    screen_x = (xr - camera.x) * camera.zoom + window_width * 0.5f;
    screen_y = window_height * 0.5f - (yr - camera.y) * camera.zoom;
}

void PCBRenderer::ApplyRotation(float& x, float& y, bool inverse) const {
    // Apply rotation first
    if (camera.rotation_steps != 0) {
        int steps = camera.rotation_steps;
        if (inverse) steps = (4 - steps) % 4; // inverse rotation
        float dx = x - board_cx;
        float dy = y - board_cy;
        float rx = x, ry = y;
        switch(steps) {
            case 1: // 90 CW
                rx = board_cx + dy;
                ry = board_cy - dx;
                break;
            case 2: // 180
                rx = board_cx - dx;
                ry = board_cy - dy;
                break;
            case 3: // 270 CW
                rx = board_cx - dy;
                ry = board_cy + dx;
                break;
            default: break;
        }
        x = rx; y = ry;
    }
    // Apply flips (mirror about board center). Same operation for forward/inverse because mirroring is its own inverse.
    if (camera.flip_horizontal) {
        x = board_cx * 2.0f - x;
    }
    if (camera.flip_vertical) {
        y = board_cy * 2.0f - y;
    }
}

void PCBRenderer::RenderPartNamesOnTop(ImDrawList* draw_list) {
    // Render all collected part names on top of all other graphics
    for (const auto& part_name_info : part_names_to_render) {
        // Clip text rendering to component boundaries
        draw_list->PushClipRect(part_name_info.clip_min, part_name_info.clip_max, true);
        
        // Add text background only if it's not transparent
        if ((part_name_info.background_color & 0xFF) > 0) {  // Check alpha channel
            ImVec2 bg_min = ImVec2(part_name_info.position.x - 1, part_name_info.position.y);
            ImVec2 bg_max = ImVec2(part_name_info.position.x + part_name_info.size.x + 1, part_name_info.position.y + part_name_info.size.y);
            draw_list->AddRectFilled(bg_min, bg_max, part_name_info.background_color);
        }
        
        // Render the part name text (no scaling - text already fits within bounds)
        draw_list->AddText(part_name_info.position, part_name_info.color, part_name_info.text.c_str());
        
        // Restore clipping
        draw_list->PopClipRect();
    }
    
    // Clear the collection for the next frame
    part_names_to_render.clear();
}

void PCBRenderer::CollectPartNamesForRendering(float zoom, float offset_x, float offset_y) {
    if (!pcb_data || pcb_data->parts.empty()) {
        return;
    }

    // Only show part names when zoomed in enough for readability
    if (zoom < 0.3f) {
        return;
    }

    // Clear any existing part names from previous frame
    part_names_to_render.clear();

    for (size_t part_index = 0; part_index < pcb_data->parts.size(); ++part_index) {
        const auto& part = pcb_data->parts[part_index];
        
        // Skip parts without names
        if (part.name.empty()) {
            continue;
        }

        // Get pins for this part to calculate bounds
        std::vector<BRDPin> part_pins;
        for (const auto& pin : pcb_data->pins) {
            if (pin.part == part_index + 1) { // Parts are 1-indexed
                part_pins.push_back(pin);
            }
        }

        // If part has only one pin, do not show the part name
        if (part_pins.size() == 1) {
            continue;
        }

        if (part_pins.empty()) {
            // Use part bounds if no pins
            float center_x = (part.p1.x + part.p2.x) * 0.5f;
            float center_y = (part.p1.y + part.p2.y) * 0.5f;
            ApplyRotation(center_x, center_y, false);
            
            // Transform to screen coordinates
            float screen_x = center_x * zoom + offset_x;
            float screen_y = offset_y - center_y * zoom;
            
            // Calculate text size
            ImVec2 text_size = ImGui::CalcTextSize(part.name.c_str());
            
            // Check if text fits within part bounds
            float part_width = std::abs(part.p2.x - part.p1.x) * zoom;
            float part_height = std::abs(part.p2.y - part.p1.y) * zoom;
            
            // Only show if text fits completely within the part boundaries
            if (text_size.x > part_width || text_size.y > part_height) {
                continue; // Skip if text doesn't fit
            }
            
            PartNameInfo info;
            info.text = part.name;
            info.position = ImVec2(screen_x - text_size.x * 0.5f, screen_y - text_size.y * 0.5f);
            info.size = text_size;
            info.color = IM_COL32(255, 255, 255, 255); // White text
            info.background_color = IM_COL32(0, 0, 0, 128); // Semi-transparent black background
            
            // Set clipping bounds (use part bounds)
            float bbminx = part.p1.x, bbminy = part.p1.y;
            float bbmaxx = part.p2.x, bbmaxy = part.p2.y;
            ApplyRotation(bbminx, bbminy, false);
            ApplyRotation(bbmaxx, bbmaxy, false);
            float min_x = bbminx * zoom + offset_x;
            float max_x = bbmaxx * zoom + offset_x;
            float min_y = offset_y - bbmaxy * zoom;
            float max_y = offset_y - bbminy * zoom;
            
            info.clip_min = ImVec2(min_x, min_y);
            info.clip_max = ImVec2(max_x, max_y);
            
            part_names_to_render.push_back(info);
            continue;
        }

        // Calculate part bounds from pins
        float min_x = part_pins[0].pos.x, max_x = part_pins[0].pos.x;
        float min_y = part_pins[0].pos.y, max_y = part_pins[0].pos.y;
        ApplyRotation(min_x, min_y, false); // rotate initial
        ApplyRotation(max_x, max_y, false);
        if (min_x > max_x) std::swap(min_x, max_x);
        if (min_y > max_y) std::swap(min_y, max_y);
        
        for (const auto& pin : part_pins) {
            float px = pin.pos.x, py = pin.pos.y;
            ApplyRotation(px, py, false);
            min_x = std::min(min_x, px);
            max_x = std::max(max_x, px);
            min_y = std::min(min_y, py);
            max_y = std::max(max_y, py);
        }

        // Add some margin around the pins
        float margin = DeterminePinMargin(part, part_pins, 
                                        std::sqrt((max_x - min_x) * (max_x - min_x) + (max_y - min_y) * (max_y - min_y)));
        
        min_x -= margin;
        max_x += margin;
        min_y -= margin;
        max_y += margin;

        // Calculate center position in world coordinates
        float center_x = (min_x + max_x) * 0.5f;
        float center_y = (min_y + max_y) * 0.5f;
        
        // Transform to screen coordinates
        float screen_center_x = center_x * zoom + offset_x;
        float screen_center_y = offset_y - center_y * zoom;
        
        // Calculate text size
        ImVec2 text_size = ImGui::CalcTextSize(part.name.c_str());
        
        // Check if text fits within component bounds (screen coordinates)
        float component_width = (max_x - min_x) * zoom;
        float component_height = (max_y - min_y) * zoom;
        
        // Only show if text fits completely within the component boundaries
        if (text_size.x > component_width || text_size.y > component_height) {
            // Text too large to fit inside component, skip
            continue;
        }
        
        // Use original text size (no artificial scaling)
        ImVec2 scaled_text_size = text_size;

        PartNameInfo info;
        info.text = part.name;
        info.position = ImVec2(screen_center_x - scaled_text_size.x * 0.5f, screen_center_y - scaled_text_size.y * 0.5f);
        info.size = scaled_text_size;
        info.color = IM_COL32(255, 255, 255, 255);
        // Semi-transparent black background for better visibility
        info.background_color = IM_COL32(0, 0, 0, 128); // Semi-transparent black background
        
        // Set clipping bounds to component area
        float screen_min_x = min_x * zoom + offset_x;
        float screen_max_x = max_x * zoom + offset_x;
        float screen_min_y = offset_y - max_y * zoom;
        float screen_max_y = offset_y - min_y * zoom;
        
        info.clip_min = ImVec2(screen_min_x, screen_min_y);
        info.clip_max = ImVec2(screen_max_x, screen_max_y);
        
        part_names_to_render.push_back(info);
    }
}

void PCBRenderer::RenderPinNumbersAsText(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height) {
    if (!pcb_data || pcb_data->pins.empty() || pin_geometry_cache.empty()) {
        return;
    }

    // Only show pin numbers when zoomed in enough for readability
    if (zoom < 2.0f) {
        return;
    }

    for (size_t pin_index = 0; pin_index < pcb_data->pins.size() && pin_index < pin_geometry_cache.size(); ++pin_index) {
        const auto& pin = pcb_data->pins[pin_index];
        const auto& cache = pin_geometry_cache[pin_index];
        
        // Early visibility culling for pins
        float approx_radius = cache.radius > 0 ? cache.radius : 10.0f;
        if (!IsElementVisible(pin.pos.x, pin.pos.y, approx_radius, zoom, offset_x, offset_y, window_width, window_height)) {
            continue;
        }
        
    // Transform pin coordinates applying global rotation, then to screen space (Y-axis mirrored)
    float px = pin.pos.x;
    float py = pin.pos.y;
    ApplyRotation(px, py, false);
    float x = px * zoom + offset_x;
    float y = offset_y - py * zoom;
        
        // Calculate pin dimensions using cached geometry data
        float pin_width = 0.0f, pin_height = 0.0f;
        
        if (cache.rectangle_index != SIZE_MAX) {
            // Rectangle pin
            const auto& rect = pcb_data->rectangles[cache.rectangle_index];
            pin_width = rect.width * zoom;
            pin_height = rect.height * zoom;
        } else if (cache.oval_index != SIZE_MAX) {
            // Oval pin
            const auto& oval = pcb_data->ovals[cache.oval_index];
            pin_width = oval.width * zoom;
            pin_height = oval.height * zoom;
        } else if (cache.circle_index != SIZE_MAX) {
            // Circle pin
            const auto& circle = pcb_data->circles[cache.circle_index];
            float radius = circle.radius * zoom;
            pin_width = radius * 2.0f;
            pin_height = radius * 2.0f;
        } else {
            // Fallback using cached radius
            float radius = cache.radius * zoom;
            pin_width = radius * 2.0f;
            pin_height = radius * 2.0f;
        }
        
        // Ensure minimum visibility for all pin types
        if (pin_width < 2.0f) pin_width = 2.0f;
        if (pin_height < 2.0f) pin_height = 2.0f;
        
        // Calculate effective area for text fitting (use smaller dimension)
        float effective_size = std::min(pin_width, pin_height);
        
        // Only show text if pin is large enough for crisp text rendering
        if (effective_size < 12.0f) {
            continue;
        }
        
        // Get pin number
        std::string pin_number = "";
        if (!pin.snum.empty()) {
            pin_number = pin.snum;
        } else if (!pin.name.empty()) {
            pin_number = pin.name;
        }
        
        // Get diode reading from pin comment
        std::string diode_reading = "";
        if (!pin.comment.empty()) {
            diode_reading = pin.comment;
        }
        
        // Get net name (prefer meaningful names)
        std::string net_name = "";
        if (!pin.net.empty() && pin.net != "UNCONNECTED" && pin.net != "") {
            if (pin.net.substr(0, 4) != "NET_") {
                net_name = pin.net;  // Meaningful names (VCC, GND, etc.)
            } else {
                net_name = pin.net;  // Show generic NET_ names too
            }
        }
        
        // Skip if no pin number available
        if (pin_number.empty()) {
            continue;
        }
        
    // (Removed unused base text size calculations; dynamic line wrapping below recalculates sizes as needed)
        
        // Calculate maximum text dimensions that fit in pin area (with margin)
        float max_text_width = pin_width * 0.95f;   // Use ~95% of pin width for text
        float max_text_height = pin_height * 0.95f; // Use ~95% of pin height for text
        
        // Helper function to break text into multiple lines if needed
        auto breakTextIntoLines = [&](const std::string& text, float max_width) -> std::vector<std::string> {
            std::vector<std::string> lines;
            if (text.empty()) return lines;
            
            ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
            if (text_size.x <= max_width) {
                lines.push_back(text);
                return lines;
            }
            
            // Text is too wide, try to break it intelligently
            std::string remaining = text;
            while (!remaining.empty()) {
                // Find the longest substring that fits
                size_t best_break = 0;
                for (size_t i = 1; i <= remaining.length(); ++i) {
                    std::string substr = remaining.substr(0, i);
                    ImVec2 substr_size = ImGui::CalcTextSize(substr.c_str());
                    if (substr_size.x <= max_width) {
                        best_break = i;
                    } else {
                        break;
                    }
                }
                
                if (best_break == 0) {
                    // Even single character doesn't fit, force break
                    best_break = 1;
                }
                
                // Try to break at a better position (space, underscore, etc.)
                if (best_break < remaining.length()) {
                    size_t last_good_break = best_break;
                    for (size_t j = best_break; j > 0; --j) {
                        char c = remaining[j-1];
                        if (c == '_' || c == '-' || c == '.' || c == ' ') {
                            last_good_break = j;
                            break;
                        }
                    }
                    best_break = last_good_break;
                }
                
                lines.push_back(remaining.substr(0, best_break));
                remaining = remaining.substr(best_break);
            }
            
            return lines;
        };
        
        // Break texts into lines if needed
        std::vector<std::string> pin_lines = breakTextIntoLines(pin_number, max_text_width);
        std::vector<std::string> net_lines = breakTextIntoLines(net_name, max_text_width);
        std::vector<std::string> diode_lines = breakTextIntoLines(diode_reading, max_text_width);
        
        // Calculate total heights for multiline text
        float pin_text_height = pin_lines.empty() ? 0.0f : pin_lines.size() * ImGui::GetTextLineHeight();
        float net_text_height = net_lines.empty() ? 0.0f : net_lines.size() * ImGui::GetTextLineHeight();
        float diode_text_height = diode_lines.empty() ? 0.0f : diode_lines.size() * ImGui::GetTextLineHeight();
        
        // Check if texts fit within the pin
        bool show_pin_text = !pin_lines.empty() && pin_text_height <= max_text_height;
        bool show_net_text = !net_lines.empty() && net_text_height <= max_text_height;
        bool show_diode_text = !diode_lines.empty() && diode_text_height <= max_text_height;
        
        // If we have multiple texts, check if they fit stacked vertically
        // Priority: diode reading (top), pin number (middle), net name (bottom)
        float text_spacing = 2.0f;
        if (show_diode_text && show_pin_text && show_net_text) {
            float total_text_height = diode_text_height + pin_text_height + net_text_height + 2 * text_spacing;
            if (total_text_height > max_text_height) {
                show_net_text = false; // Drop net text first
                total_text_height = diode_text_height + pin_text_height + text_spacing;
                if (total_text_height > max_text_height) {
                    show_diode_text = false; // Drop diode text if still doesn't fit
                }
            }
        } else if (show_diode_text && show_pin_text) {
            float total_text_height = diode_text_height + pin_text_height + text_spacing;
            if (total_text_height > max_text_height) {
                show_diode_text = false; // Drop diode text if doesn't fit with pin
            }
        } else if (show_pin_text && show_net_text) {
            float total_text_height = pin_text_height + net_text_height + text_spacing;
            if (total_text_height > max_text_height) {
                show_net_text = false; // Drop net text if doesn't fit with pin
            }
        }
        
        // Skip if no text will be shown
        if (!show_pin_text && !show_net_text && !show_diode_text) {
            continue;
        }
        
        // Position text centered on pin location
        ImVec2 text_pos;
        
        // Clip text rendering to pin area to ensure it stays inside
        float half_width = pin_width * 0.5f;
        float half_height = pin_height * 0.5f;
        draw_list->PushClipRect(
            ImVec2(x - half_width, y - half_height), 
            ImVec2(x + half_width, y + half_height), 
            true
        );
        
        if (show_diode_text && show_pin_text && show_net_text) {
            // All three texts - stack them vertically: diode (top), pin (middle), net (bottom)
            float text_spacing = 2.0f;
            float total_text_height = diode_text_height + pin_text_height + net_text_height + 2 * text_spacing;
            
            // Position diode reading lines at TOP (CYAN text for visibility)
            float current_y = y - total_text_height * 0.5f;
            for (const auto& line : diode_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 diode_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(diode_text_pos, IM_COL32(0, 255, 255, 255), line.c_str()); // Cyan
                current_y += ImGui::GetTextLineHeight();
            }
            
            current_y += text_spacing;
            
            // Position pin number lines in MIDDLE (original gray text)
            for (const auto& line : pin_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 pin_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(pin_text_pos, IM_COL32(128, 128, 128, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
            
            current_y += text_spacing;
            
            // Position net name lines at BOTTOM (original gray text)
            for (const auto& line : net_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 net_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(net_text_pos, IM_COL32(128, 128, 128, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
        }
        else if (show_diode_text && show_pin_text) {
            // Diode and pin texts - stack them vertically: diode (top), pin (bottom)
            float text_spacing = 2.0f;
            float total_text_height = diode_text_height + pin_text_height + text_spacing;
            
            // Position diode reading lines at TOP (CYAN text)
            float current_y = y - total_text_height * 0.5f;
            for (const auto& line : diode_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 diode_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(diode_text_pos, IM_COL32(0, 255, 255, 255), line.c_str()); // Cyan
                current_y += ImGui::GetTextLineHeight();
            }
            
            current_y += text_spacing;
            
            // Position pin number lines at BOTTOM (original gray text)
            for (const auto& line : pin_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 pin_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(pin_text_pos, IM_COL32(128, 128, 128, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
        }
        else if (show_pin_text && show_net_text) {
            // Both texts - stack them vertically
            float text_spacing = 2.0f;
            float total_text_height = pin_text_height + net_text_height + text_spacing;
            
            // Position pin number lines at TOP of circle (original gray text)
            float current_y = y - total_text_height * 0.5f;
            for (const auto& line : pin_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 pin_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(pin_text_pos, IM_COL32(128, 128, 128, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
            
            current_y += text_spacing;
            
            // Position net name lines at BOTTOM of circle (original gray text)
            for (const auto& line : net_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 net_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(net_text_pos, IM_COL32(128, 128, 128, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
        }
        else if (show_diode_text) {
            // Only diode reading - center it
            float current_y = y - diode_text_height * 0.5f;
            for (const auto& line : diode_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 diode_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(diode_text_pos, IM_COL32(0, 255, 255, 255), line.c_str()); // Cyan
                current_y += ImGui::GetTextLineHeight();
            }
        }
        else if (show_pin_text) {
            // Only pin number - center it
            float current_y = y - pin_text_height * 0.5f;
            for (const auto& line : pin_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 pin_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(pin_text_pos, IM_COL32(128, 128, 128, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
        }
        else if (show_net_text) {
            // Only net name - center it
            float current_y = y - net_text_height * 0.5f;
            for (const auto& line : net_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 net_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(net_text_pos, IM_COL32(255, 255, 0, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
        }
        
        // Restore clipping
        draw_list->PopClipRect();
    }
}
