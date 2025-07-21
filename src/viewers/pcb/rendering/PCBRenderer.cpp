#include "PCBRenderer.h"
#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <vector>
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
    RenderCirclePinsImGui(draw_list, zoom, offset_x, offset_y);
    RenderRectanglePinsImGui(draw_list, zoom, offset_x, offset_y);
    RenderOvalPinsImGui(draw_list, zoom, offset_x, offset_y);

    // Collect part names for rendering on top
    CollectPartNamesForRendering(zoom, offset_x, offset_y);

    // Render part names on top of all other graphics
    RenderPartNamesOnTop(draw_list);

    // Render pin numbers as text overlays
    RenderPinNumbersAsText(draw_list, zoom, offset_x, offset_y);

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

void PCBRenderer::ZoomToFit(int window_width, int window_height) {
    if (!pcb_data) return;
    
    BRDPoint min_point, max_point;
    pcb_data->GetBoundingBox(min_point, max_point);
    
    float pcb_width = static_cast<float>(max_point.x - min_point.x);
    float pcb_height = static_cast<float>(max_point.y - min_point.y);
    
    if (pcb_width <= 0 || pcb_height <= 0) return;
    
    // Calculate zoom to fit with margin
    float zoom_x = window_width / (pcb_width * 1.2f);
    float zoom_y = window_height / (pcb_height * 1.2f);
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
        // Transform coordinates from PCB space to screen space with Y-axis mirroring
        ImVec2 p1(segment.first.x * zoom + offset_x, offset_y - segment.first.y * zoom);
        ImVec2 p2(segment.second.x * zoom + offset_x, offset_y - segment.second.y * zoom);
        
        // Draw outline segment
        draw_list->AddLine(p1, p2, outline_color, line_thickness);
    }
    
    // Outline rendering complete
}

void PCBRenderer::RenderCirclePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    if (!pcb_data || pcb_data->circles.empty()) {
        return;
    }
    
    // Render all circles with red fill
    for (const auto& circle : pcb_data->circles) {
        // Transform circle center coordinates to screen space with Y-axis mirroring
        float x = circle.center.x * zoom + offset_x;
        float y = offset_y - circle.center.y * zoom;  // Mirror Y-axis
        
        // Scale radius by zoom factor
        float radius = circle.radius * zoom;
        
        // Ensure minimum visibility
        if (radius < 1.0f) radius = 1.0f;
        
        // Check if this circle corresponds to a ground pin or selected pin and override color
        float r = circle.r, g = circle.g, b = circle.b, a = circle.a;
        std::string selected_net;
        if (selected_pin_index >= 0 && selected_pin_index < (int)pcb_data->pins.size()) {
            selected_net = pcb_data->pins[selected_pin_index].net;
        }
        for (size_t pin_idx = 0; pin_idx < pcb_data->pins.size(); ++pin_idx) {
            const auto& pin = pcb_data->pins[pin_idx];
            if (pin.pos.x == circle.center.x && pin.pos.y == circle.center.y) {
                if (!selected_net.empty() && pin.net == selected_net) {
                    // Highlight all pins on the same net
                    r = 1.0f; g = 1.0f; b = 0.7f; a = 1.0f;
                } else if (IsNCPin(pin)) {
                    // Use blue color for NC pins
                    r = 0.0f; g = 0.3f; b = 0.3f; a = 1.0f;
                } else if (IsGroundPin(pin)) {
                    // Use grey color for ground pins
                    r = 0.5f; g = 0.5f; b = 0.5f; a = 1.0f;
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

void PCBRenderer::RenderRectanglePinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    if (!pcb_data || pcb_data->rectangles.empty()) {
        return;
    }
    
    // Render all rectangles with red fill
    for (const auto& rectangle : pcb_data->rectangles) {
        // Transform rectangle center coordinates to screen space with Y-axis mirroring
        float center_x = rectangle.center.x * zoom + offset_x;
        float center_y = offset_y - rectangle.center.y * zoom;  // Mirror Y-axis
        
        // Scale dimensions by zoom factor
        float width = rectangle.width * zoom;
        float height = rectangle.height * zoom;
        
        // Ensure minimum visibility
        if (width < 2.0f) width = 2.0f;
        if (height < 2.0f) height = 2.0f;
        
        // Check if this rectangle corresponds to a ground pin or selected pin and override color
        float r = rectangle.r, g = rectangle.g, b = rectangle.b, a = rectangle.a;
        std::string selected_net;
        if (selected_pin_index >= 0 && selected_pin_index < (int)pcb_data->pins.size()) {
            selected_net = pcb_data->pins[selected_pin_index].net;
        }
        for (size_t pin_idx = 0; pin_idx < pcb_data->pins.size(); ++pin_idx) {
            const auto& pin = pcb_data->pins[pin_idx];
            if (pin.pos.x == rectangle.center.x && pin.pos.y == rectangle.center.y) {
                if (!selected_net.empty() && pin.net == selected_net) {
                    // Highlight all pins on the same net
                    r = 1.0f; g = 1.0f; b = 0.7f; a = 1.0f;
                } else if (IsNCPin(pin)) {
                    // Use blue color for NC pins
                    r = 0.0f; g = 0.3f; b = 0.3f; a = 1.0f;
                } else if (IsGroundPin(pin)) {
                    // Use grey color for ground pins
                    r = 0.5f; g = 0.5f; b = 0.5f; a = 1.0f;
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
        
        if (rectangle.rotation == 0.0f) {
            // No rotation - simple axis-aligned rectangle
            float half_width = width / 2.0f;
            float half_height = height / 2.0f;
            
            ImVec2 top_left(center_x - half_width, center_y - half_height);
            ImVec2 bottom_right(center_x + half_width, center_y + half_height);
            
            draw_list->AddRectFilled(top_left, bottom_right, fill_color);
            
            // Optional: Add outline for better visibility
            ImU32 outline_color = IM_COL32(
                (int)(r * 180), 
                (int)(g * 180), 
                (int)(b * 180), 
                255
            );
            draw_list->AddRect(top_left, bottom_right, outline_color, 0.0f, 0, 1.0f);
        } else {
            // Rotated rectangle - draw as quad with rotation
            float half_width = width / 2.0f;
            float half_height = height / 2.0f;
            
            // Convert rotation to radians
            float rot_rad = rectangle.rotation * 3.14159265f / 180.0f;
            float cos_rot = std::cos(rot_rad);
            float sin_rot = std::sin(rot_rad);
            
            // Calculate the four corners of the rotated rectangle
            ImVec2 corners[4];
            
            // Corner offsets before rotation
            float dx1 = -half_width, dy1 = -half_height; // Top-left
            float dx2 = half_width,  dy2 = -half_height; // Top-right
            float dx3 = half_width,  dy3 = half_height;  // Bottom-right
            float dx4 = -half_width, dy4 = half_height;  // Bottom-left
            
            // Apply rotation and translation
            corners[0] = ImVec2(center_x + dx1 * cos_rot - dy1 * sin_rot, center_y + dx1 * sin_rot + dy1 * cos_rot);
            corners[1] = ImVec2(center_x + dx2 * cos_rot - dy2 * sin_rot, center_y + dx2 * sin_rot + dy2 * cos_rot);
            corners[2] = ImVec2(center_x + dx3 * cos_rot - dy3 * sin_rot, center_y + dx3 * sin_rot + dy3 * cos_rot);
            corners[3] = ImVec2(center_x + dx4 * cos_rot - dy4 * sin_rot, center_y + dx4 * sin_rot + dy4 * cos_rot);
            
            // Draw the quad as two triangles
            draw_list->AddQuadFilled(corners[0], corners[1], corners[2], corners[3], fill_color);
            
            // Optional: Add outline for better visibility
            ImU32 outline_color = IM_COL32(
                (int)(r * 180), 
                (int)(g * 180), 
                (int)(b * 180), 
                255
            );
            draw_list->AddQuad(corners[0], corners[1], corners[2], corners[3], outline_color, 1.0f);
        }
    }
}

void PCBRenderer::RenderOvalPinsImGui(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    if (!pcb_data || pcb_data->ovals.empty()) {
        return;
    }
    
    // Render all ovals as stadium shapes (rounded rectangles)
    for (const auto& oval : pcb_data->ovals) {
        // Transform oval center coordinates to screen space with Y-axis mirroring
        float center_x = oval.center.x * zoom + offset_x;
        float center_y = offset_y - oval.center.y * zoom;  // Mirror Y-axis
        
        // Scale dimensions by zoom factor
        float width = oval.width * zoom;
        float height = oval.height * zoom;
        
        // Ensure minimum visibility
        if (width < 2.0f) width = 2.0f;
        if (height < 2.0f) height = 2.0f;
        
        // Check if this oval corresponds to a ground pin or selected pin and override color
        float r = oval.r, g = oval.g, b = oval.b, a = oval.a;
        std::string selected_net;
        if (selected_pin_index >= 0 && selected_pin_index < (int)pcb_data->pins.size()) {
            selected_net = pcb_data->pins[selected_pin_index].net;
        }
        for (size_t pin_idx = 0; pin_idx < pcb_data->pins.size(); ++pin_idx) {
            const auto& pin = pcb_data->pins[pin_idx];
            if (pin.pos.x == oval.center.x && pin.pos.y == oval.center.y) {
                if (!selected_net.empty() && pin.net == selected_net) {
                    // Highlight all pins on the same net
                    r = 1.0f; g = 1.0f; b = 0.7f; a = 1.0f;
                } else if (IsNCPin(pin)) {
                    // Use blue color for NC pins
                    r = 0.0f; g = 0.3f; b = 0.3f; a = 1.0f;
                } else if (IsGroundPin(pin)) {
                    // Use grey color for ground pins
                    r = 0.5f; g = 0.5f; b = 0.5f; a = 1.0f;
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
        
        // Stadium shape: rectangle with semicircular ends
        // The radius of the semicircles is half the smaller dimension
        float radius = std::min(width, height) / 2.0f;
        
        // Convert rotation to radians
        float rot_rad = oval.rotation * 3.14159265f / 180.0f;
        float cos_rot = std::cos(rot_rad);
        float sin_rot = std::sin(rot_rad);
        
        // Create stadium shape points
        std::vector<ImVec2> stadium_points;
        const int semicircle_segments = 12; // Segments per semicircle
        
        if (width > height) {
            // Horizontal stadium: longer in width
            float rect_width = width - 2.0f * radius;  // Width of central rectangle
            float rect_height = height;
            
            // Left semicircle (from bottom to top)
            for (int i = 0; i <= semicircle_segments; ++i) {
                float angle = 3.14159265f * 0.5f + 3.14159265f * i / semicircle_segments; // π/2 to 3π/2
                float x_local = -rect_width / 2.0f + radius * std::cos(angle);
                float y_local = radius * std::sin(angle);
                
                // Apply rotation
                float x_rotated = x_local * cos_rot - y_local * sin_rot;
                float y_rotated = x_local * sin_rot + y_local * cos_rot;
                
                stadium_points.push_back(ImVec2(center_x + x_rotated, center_y + y_rotated));
            }
            
            // Right semicircle (from top to bottom)
            for (int i = 0; i <= semicircle_segments; ++i) {
                float angle = 3.14159265f * 1.5f + 3.14159265f * i / semicircle_segments; // 3π/2 to 5π/2
                float x_local = rect_width / 2.0f + radius * std::cos(angle);
                float y_local = radius * std::sin(angle);
                
                // Apply rotation
                float x_rotated = x_local * cos_rot - y_local * sin_rot;
                float y_rotated = x_local * sin_rot + y_local * cos_rot;
                
                stadium_points.push_back(ImVec2(center_x + x_rotated, center_y + y_rotated));
            }
        } else {
            // Vertical stadium: longer in height
            float rect_width = width;
            float rect_height = height - 2.0f * radius;  // Height of central rectangle
            
            // Bottom semicircle (from left to right)
            for (int i = 0; i <= semicircle_segments; ++i) {
                float angle = 3.14159265f + 3.14159265f * i / semicircle_segments; // π to 2π
                float x_local = radius * std::cos(angle);
                float y_local = -rect_height / 2.0f + radius * std::sin(angle);
                
                // Apply rotation
                float x_rotated = x_local * cos_rot - y_local * sin_rot;
                float y_rotated = x_local * sin_rot + y_local * cos_rot;
                
                stadium_points.push_back(ImVec2(center_x + x_rotated, center_y + y_rotated));
            }
            
            // Top semicircle (from right to left)
            for (int i = 0; i <= semicircle_segments; ++i) {
                float angle = 2.0f * 3.14159265f + 3.14159265f * i / semicircle_segments; // 2π to 3π
                float x_local = radius * std::cos(angle);
                float y_local = rect_height / 2.0f + radius * std::sin(angle);
                
                // Apply rotation
                float x_rotated = x_local * cos_rot - y_local * sin_rot;
                float y_rotated = x_local * sin_rot + y_local * cos_rot;
                
                stadium_points.push_back(ImVec2(center_x + x_rotated, center_y + y_rotated));
            }
        }
        
        // Draw filled stadium shape using convex polygon
        if (stadium_points.size() >= 3) {
            draw_list->AddConvexPolyFilled(stadium_points.data(), stadium_points.size(), fill_color);
            
            // Optional: Add outline for better visibility
            ImU32 outline_color = IM_COL32(
                (int)(r * 180), 
                (int)(g * 180), 
                (int)(b * 180), 
                255
            );
            
            // Draw outline by connecting consecutive points
            for (size_t i = 0; i < stadium_points.size(); ++i) {
                size_t next_i = (i + 1) % stadium_points.size();
                draw_list->AddLine(stadium_points[i], stadium_points[next_i], outline_color, 1.0f);
            }
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


// Pin selection functionality
bool PCBRenderer::HandleMouseClick(float screen_x, float screen_y, int window_width, int window_height) {
    if (!pcb_data || pcb_data->pins.empty()) {
        return false;
    }
    
    // Convert screen coordinates to world coordinates
    float world_x, world_y;
    ScreenToWorld(screen_x, screen_y, world_x, world_y, window_width, window_height);
      // Check if click is near any pin
    // Remove extra click radius: selection is now only within the real pin geometry
    float click_radius = 0.0f;
    
    for (size_t i = 0; i < pcb_data->pins.size(); ++i) {
        const auto& pin = pcb_data->pins[i];
        // Skip ground pins and NC pins - they are not selectable
        if (IsGroundPin(pin) || IsNCPin(pin)) {
            continue;
        }

        // Check if this pin is a rectangle (square/rectangular pin)
        bool is_rect = false;
        float rect_width = 0.0f, rect_height = 0.0f, rect_rotation = 0.0f;
        // Try to find a rectangle at this pin position
        for (const auto& rect : pcb_data->rectangles) {
            if (rect.center.x == pin.pos.x && rect.center.y == pin.pos.y) {
                is_rect = true;
                rect_width = rect.width;
                rect_height = rect.height;
                rect_rotation = rect.rotation;
                break;
            }
        }

        if (is_rect) {
            // Rectangle pin: check if click is inside the rectangle (with rotation)
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            // Undo rotation
            float angle_rad = -rect_rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float half_w = rect_width / 2.0f;
            float half_h = rect_height / 2.0f;
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

        // Check if this pin is an oval (stadium shape)
        bool is_oval = false;
        float oval_width = 0.0f, oval_height = 0.0f, oval_rotation = 0.0f;
        for (const auto& oval : pcb_data->ovals) {
            if (oval.center.x == pin.pos.x && oval.center.y == pin.pos.y) {
                is_oval = true;
                oval_width = oval.width;
                oval_height = oval.height;
                oval_rotation = oval.rotation;
                break;
            }
        }
        if (is_oval) {
            // Oval pin: check if click is inside the rotated ellipse (approximate)
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            float angle_rad = -oval_rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float rx = oval_width / 2.0f;
            float ry = oval_height / 2.0f;
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

        // Otherwise, treat as circle (default)
        float dx = world_x - pin.pos.x;
        float dy = world_y - pin.pos.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        // Find the corresponding circle data for this pin position
        float circle_radius = static_cast<float>(pin.radius); // fallback to pin radius
        for (const auto& circle : pcb_data->circles) {
            if (circle.center.x == pin.pos.x && circle.center.y == pin.pos.y) {
                circle_radius = circle.radius;
                break;
            }
        }
        
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
    if (!pcb_data || pcb_data->pins.empty()) {
        return -1;
    }
    
    // Convert screen coordinates to world coordinates
    float world_x, world_y;
    ScreenToWorld(screen_x, screen_y, world_x, world_y, window_width, window_height);
    
    for (size_t i = 0; i < pcb_data->pins.size(); ++i) {
        const auto& pin = pcb_data->pins[i];
        
        // Skip ground pins and NC pins - they are not hoverable
        if (IsGroundPin(pin) || IsNCPin(pin)) {
            continue;
        }

        // Check if this pin is a rectangle (square/rectangular pin)
        bool is_rect = false;
        float rect_width = 0.0f, rect_height = 0.0f, rect_rotation = 0.0f;
        // Try to find a rectangle at this pin position
        for (const auto& rect : pcb_data->rectangles) {
            if (rect.center.x == pin.pos.x && rect.center.y == pin.pos.y) {
                is_rect = true;
                rect_width = rect.width;
                rect_height = rect.height;
                rect_rotation = rect.rotation;
                break;
            }
        }

        if (is_rect) {
            // Rectangle pin: check if mouse is inside the rectangle (with rotation)
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            // Undo rotation
            float angle_rad = -rect_rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float half_w = rect_width / 2.0f;
            float half_h = rect_height / 2.0f;
            if (std::abs(local_x) <= half_w && std::abs(local_y) <= half_h) {
                return static_cast<int>(i);
            }
            continue;
        }

        // Check if this pin is an oval (stadium shape)
        bool is_oval = false;
        float oval_width = 0.0f, oval_height = 0.0f, oval_rotation = 0.0f;
        for (const auto& oval : pcb_data->ovals) {
            if (oval.center.x == pin.pos.x && oval.center.y == pin.pos.y) {
                is_oval = true;
                oval_width = oval.width;
                oval_height = oval.height;
                oval_rotation = oval.rotation;
                break;
            }
        }
        if (is_oval) {
            // Oval pin: check if mouse is inside the rotated ellipse (approximate)
            float dx = world_x - pin.pos.x;
            float dy = world_y - pin.pos.y;
            float angle_rad = -oval_rotation * 3.14159265f / 180.0f;
            float cos_a = std::cos(angle_rad);
            float sin_a = std::sin(angle_rad);
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;
            float rx = oval_width / 2.0f;
            float ry = oval_height / 2.0f;
            if ((local_x * local_x) / (rx * rx) + (local_y * local_y) / (ry * ry) <= 1.0f) {
                return static_cast<int>(i);
            }
            continue;
        }

        // Otherwise, treat as circle (default)
        float dx = world_x - pin.pos.x;
        float dy = world_y - pin.pos.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        // Find the corresponding circle data for this pin position
        float circle_radius = static_cast<float>(pin.radius); // fallback to pin radius
        for (const auto& circle : pcb_data->circles) {
            if (circle.center.x == pin.pos.x && circle.center.y == pin.pos.y) {
                circle_radius = circle.radius;
                break;
            }
        }
        
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
    // Convert screen coordinates to world coordinates using camera transform
    world_x = camera.x + (screen_x - window_width * 0.5f) / camera.zoom;
    world_y = camera.y + (window_height * 0.5f - screen_y) / camera.zoom;
}

void PCBRenderer::WorldToScreen(float world_x, float world_y, float& screen_x, float& screen_y,
                               int window_width, int window_height) {
    // Convert world coordinates to screen coordinates using camera transform
    screen_x = (world_x - camera.x) * camera.zoom + window_width * 0.5f;
    screen_y = window_height * 0.5f - (world_y - camera.y) * camera.zoom;
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
            float min_x = part.p1.x * zoom + offset_x;
            float max_x = part.p2.x * zoom + offset_x;
            float min_y = offset_y - part.p2.y * zoom;
            float max_y = offset_y - part.p1.y * zoom;
            
            info.clip_min = ImVec2(min_x, min_y);
            info.clip_max = ImVec2(max_x, max_y);
            
            part_names_to_render.push_back(info);
            continue;
        }

        // Calculate part bounds from pins
        float min_x = part_pins[0].pos.x, max_x = part_pins[0].pos.x;
        float min_y = part_pins[0].pos.y, max_y = part_pins[0].pos.y;
        
        for (const auto& pin : part_pins) {
            min_x = std::min(min_x, static_cast<float>(pin.pos.x));
            max_x = std::max(max_x, static_cast<float>(pin.pos.x));
            min_y = std::min(min_y, static_cast<float>(pin.pos.y));
            max_y = std::max(max_y, static_cast<float>(pin.pos.y));
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

void PCBRenderer::RenderPinNumbersAsText(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    if (!pcb_data || pcb_data->pins.empty()) {
        return;
    }

    // Only show pin numbers when zoomed in enough for readability
    if (zoom < 2.0f) {
        return;
    }

    for (size_t pin_index = 0; pin_index < pcb_data->pins.size(); ++pin_index) {
        const auto& pin = pcb_data->pins[pin_index];
        
        // Transform pin coordinates to screen space with Y-axis mirroring
        float x = pin.pos.x * zoom + offset_x;
        float y = offset_y - pin.pos.y * zoom;
        
        // Calculate pin dimensions using the same logic as HandleMouseClick
        float pin_width = 0.0f, pin_height = 0.0f;
        bool is_rect = false, is_oval = false;
        
        // Check if this pin is a rectangle (square/rectangular pin)
        for (const auto& rect : pcb_data->rectangles) {
            if (rect.center.x == pin.pos.x && rect.center.y == pin.pos.y) {
                is_rect = true;
                pin_width = rect.width * zoom;
                pin_height = rect.height * zoom;
                break;
            }
        }
        
        // Check if this pin is an oval (stadium shape)
        if (!is_rect) {
            for (const auto& oval : pcb_data->ovals) {
                if (oval.center.x == pin.pos.x && oval.center.y == pin.pos.y) {
                    is_oval = true;
                    pin_width = oval.width * zoom;
                    pin_height = oval.height * zoom;
                    break;
                }
            }
        }
        
        // Otherwise, treat as circle (default)
        float pin_radius = 0.0f;
        if (!is_rect && !is_oval) {
            // Find the corresponding circle data for this pin position
            float circle_radius = static_cast<float>(pin.radius); // fallback to pin radius
            for (const auto& circle : pcb_data->circles) {
                if (circle.center.x == pin.pos.x && circle.center.y == pin.pos.y) {
                    circle_radius = circle.radius;
                    break;
                }
            }
            
            if (circle_radius < 1.0f) {
                circle_radius = 6.5f; // Default fallback
            }
            pin_radius = circle_radius * zoom;
            pin_width = pin_radius * 2.0f;
            pin_height = pin_radius * 2.0f;
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
        
        // Calculate base text sizes
        ImVec2 pin_text_size = ImGui::CalcTextSize(pin_number.c_str());
        ImVec2 net_text_size = net_name.empty() ? ImVec2(0,0) : ImGui::CalcTextSize(net_name.c_str());
        
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
        
        // Calculate total heights for multiline text
        float pin_text_height = pin_lines.empty() ? 0.0f : pin_lines.size() * ImGui::GetTextLineHeight();
        float net_text_height = net_lines.empty() ? 0.0f : net_lines.size() * ImGui::GetTextLineHeight();
        
        // Check if texts fit within the pin
        bool show_pin_text = !pin_lines.empty() && pin_text_height <= max_text_height;
        bool show_net_text = !net_lines.empty() && net_text_height <= max_text_height;
        
        // If we have both texts, check if they fit stacked vertically
        if (show_pin_text && show_net_text) {
            float text_spacing = 2.0f;
            float total_text_height = pin_text_height + net_text_height + text_spacing;
            if (total_text_height > max_text_height) {
                show_net_text = false; // Disable net text if both don't fit
            }
        }
        
        // Skip if no text will be shown
        if (!show_pin_text && !show_net_text) {
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
        
        if (show_pin_text && show_net_text) {
            // Both texts - stack them vertically
            float text_spacing = 2.0f;
            float total_text_height = pin_text_height + net_text_height + text_spacing;
            
            // Position pin number lines at TOP of circle (WHITE text for better contrast)
            float current_y = y - total_text_height * 0.5f;
            for (const auto& line : pin_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 pin_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(pin_text_pos, IM_COL32(255, 255, 255, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
            
            current_y += text_spacing;
            
            // Position net name lines at BOTTOM of circle (YELLOW text for visibility)
            for (const auto& line : net_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 net_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(net_text_pos, IM_COL32(255, 255, 0, 255), line.c_str());
                current_y += ImGui::GetTextLineHeight();
            }
        }
        else if (show_pin_text) {
            // Only pin number - center it
            float current_y = y - pin_text_height * 0.5f;
            for (const auto& line : pin_lines) {
                ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
                ImVec2 pin_text_pos(x - line_size.x * 0.5f, current_y);
                draw_list->AddText(pin_text_pos, IM_COL32(255, 255, 255, 255), line.c_str());
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
