#include "BRDFile.h"
#include "Utils.h"
#include <cctype>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <algorithm>

// Header for recognizing a BRD file
decltype(BRDFile::signature) constexpr BRDFile::signature;

// Helper macros for parsing
#define READ_INT() strtol(p, &p, 10)
#define READ_UINT() [&]() { \
    int value = strtol(p, &p, 10); \
    if (value < 0) { \
        error_msg = "Negative value where unsigned expected"; \
        return 0u; \
    } \
    return static_cast<unsigned int>(value); \
}()
#define READ_DOUBLE() strtod(p, &p)
#define READ_STR() [&]() { \
    while ((*p) && (isspace((uint8_t)*p))) ++p; \
    s = p; \
    while ((*p) && (!isspace((uint8_t)*p))) ++p; \
    *p = 0; \
    p++; \
    return std::string(s); \
}()

#define ENSURE_OR_FAIL(condition, error_msg_str, action) \
    if (!(condition)) { \
        this->error_msg = std::string(error_msg_str); \
        action; \
    }

// Helper function to split buffer into lines
void stringfile(char *buffer, std::vector<char*> &lines) {
    char *s;
    size_t count, i;

    // Two passes through: first time count lines, second time set them
    for (i = 0; i < 2; ++i) {
        s = buffer;
        if (i == 1) lines[0] = s;
        count = 1; // was '1', but C arrays are 0-indexed
        while (*s) {
            if (*s == '\n' || *s == '\r') {
                // If this is the 2nd pass, then terminate the line at the first line break char
                if (i == 1) *s = 0;
                s++; // next char
                // if the termination is a CRLF combo, then jump to the next char
                if ((*s == '\r') || (*s == '\n')) s++;
                // if the char is valid (first after line break), set up the next item in the line array
                if (*s) { // it's not over yet
                    if (i == 1) {
                        lines[count] = s;
                    }
                    ++count;
                }
            }
            s++;
        } // while s

        // Generate the required array to hold all the line starting points
        if (i == 0) {
            lines.resize(count);
        }
    }
}

// Helper function to find string in buffer
bool find_str_in_buf(const std::string& needle, const std::vector<char>& buf) {
    if (needle.length() > buf.size()) return false;
    auto it = std::search(buf.begin(), buf.end(), needle.begin(), needle.end());
    return it != buf.end();
}

std::unique_ptr<BRDFile> BRDFile::LoadFromFile(const std::string& filepath) {
    std::cout << "LoadFromFile: Opening BRD file " << filepath << std::endl;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open BRD file " << filepath << std::endl;
        return nullptr;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    std::cout << "LoadFromFile: Creating BRDFile object" << std::endl;
    auto brdFile = std::make_unique<BRDFile>();
    std::cout << "LoadFromFile: Calling Load() method" << std::endl;
    if (brdFile->Load(buffer, filepath)) {
        std::cout << "LoadFromFile: Load() succeeded, returning brdFile" << std::endl;
        return brdFile;
    }
    std::cout << "LoadFromFile: Load() failed, returning nullptr" << std::endl;
    return nullptr;
}

bool BRDFile::VerifyFormat(const std::vector<char>& buffer) {
    if (buffer.size() < signature.size()) return false;
    if (std::equal(signature.begin(), signature.end(), buffer.begin(), 
                   [](const uint8_t &i, const char &j) {
                       return i == reinterpret_cast<const uint8_t &>(j);
                   }))
        return true;
    return find_str_in_buf("str_length:", buffer) && find_str_in_buf("var_data:", buffer);
}

bool BRDFile::Load(const std::vector<char>& buf, const std::string& /*filepath*/) {
    auto buffer_size = buf.size();
    ENSURE_OR_FAIL(buffer_size > 4, "Buffer too small", return false);
    
    size_t file_buf_size = 3 * (1 + buffer_size);
    file_buf = (char *)calloc(1, file_buf_size);
    ENSURE_OR_FAIL(file_buf != nullptr, "Memory allocation failed", return false);

    std::copy(buf.begin(), buf.end(), file_buf);
    file_buf[buffer_size] = 0;
    // This is for fixing degenerate utf8
    char *arena = &file_buf[buffer_size + 1];
    char *arena_end = file_buf + file_buf_size - 1;
    *arena_end = 0;
    
    // Suppress unused variable warning
    (void)arena;

    // Decode the file if it appears to be encoded:
    static const uint8_t encoded_header[] = {0x23, 0xe2, 0x63, 0x28};
    if (!memcmp(file_buf, encoded_header, 4)) {
        for (size_t i = 0; i < buffer_size; i++) {
            char x = file_buf[i];
            if (!(x == '\r' || x == '\n' || !x)) {
                int c = x;
                x = ~(((c >> 6) & 3) | (c << 2));
            }
            file_buf[i] = x;
        }
    }

    int current_block = 0;
    std::vector<char*> lines;
    stringfile(file_buf, lines);

    for (char *line : lines) {
        while (isspace((uint8_t)*line)) line++;
        if (!line[0]) continue;
        if (!strcmp(line, "str_length:")) {
            current_block = 1;
            continue;
        }
        if (!strcmp(line, "var_data:")) {
            current_block = 2;
            continue;
        }
        if (!strcmp(line, "Format:") || !strcmp(line, "format:")) {
            current_block = 3;
            continue;
        }
        if (!strcmp(line, "Parts:") || !strcmp(line, "Pins1:")) {
            current_block = 4;
            continue;
        }
        if (!strcmp(line, "Pins:") || !strcmp(line, "Pins2:")) {
            current_block = 5;
            continue;
        }
        if (!strcmp(line, "Nails:")) {
            current_block = 6;
            continue;
        }

        char *p = line;
        char *s;
        unsigned int tmp = 0;

        switch (current_block) {
            case 2: { // var_data
                num_format = READ_UINT();
                num_parts = READ_UINT();
                num_pins = READ_UINT();
                num_nails = READ_UINT();
            } break;
            case 3: { // Format
                if (format.size() >= num_format) break;
                BRDPoint fmt;
                fmt.x = strtol(p, &p, 10);
                fmt.y = strtol(p, &p, 10);
                format.push_back(fmt);
            } break;
            case 4: { // Parts
                if (parts.size() >= num_parts) break;
                BRDPart part;
                part.name = READ_STR();
                tmp = READ_UINT(); // Type and layer, actually.
                part.part_type = (tmp & 0xc) ? BRDPartType::SMD : BRDPartType::ThroughHole;
                if (tmp == 1 || (4 <= tmp && tmp < 8)) part.mounting_side = BRDPartMountingSide::Top;
                if (tmp == 2 || (8 <= tmp)) part.mounting_side = BRDPartMountingSide::Bottom;
                part.end_of_pins = READ_UINT();
                if (part.end_of_pins > num_pins) {
                    error_msg = "Part end_of_pins exceeds num_pins";
                    return false;
                }
                parts.push_back(part);
            } break;
            case 5: { // Pins
                if (pins.size() >= num_pins) break;
                BRDPin pin;
                pin.pos.x = READ_INT();
                pin.pos.y = READ_INT();
                pin.probe = READ_INT(); // Can be negative (-99)
                pin.part = READ_UINT();
                if (pin.part > num_parts) {
                    error_msg = "Pin part exceeds num_parts";
                    return false;
                }
                pin.net = READ_STR();
                pins.push_back(pin);
            } break;
            case 6: { // Nails
                if (nails.size() >= num_nails) break;
                BRDNail nail;
                nail.probe = READ_UINT();
                nail.pos.x = READ_INT();
                nail.pos.y = READ_INT();
                nail.side = READ_UINT() == 1 ? BRDPartMountingSide::Top : BRDPartMountingSide::Bottom;
                nail.net = READ_STR();
                nails.push_back(nail);
            } break;
        }
    }

    // Lenovo brd variant, find net from nail
    std::unordered_map<int, std::string> nailsToNets; // Map between net id and net name
    for (auto &nail : nails) {
        nailsToNets[nail.probe] = nail.net;
    }

    for (auto &pin : pins) {
        if (pin.net.empty()) {
            auto it = nailsToNets.find(pin.probe);
            if (it != nailsToNets.end()) {
                pin.net = it->second;
            } else {
                pin.net = "UNCONNECTED";
            }
        }
        if (pin.part > 0 && pin.part <= parts.size()) {
            switch (parts[pin.part - 1].mounting_side) {
                case BRDPartMountingSide::Top:    pin.side = BRDPinSide::Top;    break;
                case BRDPartMountingSide::Bottom: pin.side = BRDPinSide::Bottom; break;
                case BRDPartMountingSide::Both:   pin.side = BRDPinSide::Both;   break;
            }
        }
    }

    valid = current_block != 0;
    
    // Generate rendering geometry for pins
    if (valid) {
        GenerateRenderingGeometry();
    }
    
    std::cout << "BRD file parsed successfully:" << std::endl;
    std::cout << "  Parts: " << parts.size() << std::endl;
    std::cout << "  Pins: " << pins.size() << std::endl;
    std::cout << "  Nails: " << nails.size() << std::endl;
    std::cout << "  Format points: " << format.size() << std::endl;
    std::cout << "  Circles: " << circles.size() << std::endl;
    std::cout << "  Outline segments: " << outline_segments.size() << std::endl;
    std::cout << "  Part outline segments: " << part_outline_segments.size() << std::endl;
    
    return valid;
}

void BRDFile::GenerateRenderingGeometry() {
    // Clear existing geometry
    circles.clear();
    rectangles.clear();
    ovals.clear();
    outline_segments.clear();
    part_outline_segments.clear();
    
    // Calculate board dimensions for layer separation
    BRDPoint min_point, max_point;
    GetBoundingBox(min_point, max_point);
    float board_width = max_point.x - min_point.x;
    float board_height = max_point.y - min_point.y;
    
    // No offset - both sides overlap in the same position
    float bottom_side_offset_x = 0.0f;
    float bottom_side_offset_y = 0.0f;
    
    // Generate board outline segments from format points
    if (format.size() >= 2) {
        // Top side outline (original position)
        for (size_t i = 0; i < format.size(); ++i) {
            size_t next_i = (i + 1) % format.size();
            outline_segments.push_back({format[i], format[next_i]});
        }
        
        // Bottom side outline (offset position and mirrored vertically)
        for (size_t i = 0; i < format.size(); ++i) {
            size_t next_i = (i + 1) % format.size();
            BRDPoint p1_offset = {format[i].x + bottom_side_offset_x, -format[i].y + bottom_side_offset_y};
            BRDPoint p2_offset = {format[next_i].x + bottom_side_offset_x, -format[next_i].y + bottom_side_offset_y};
            outline_segments.push_back({p1_offset, p2_offset});
        }
    }
    
    // Generate part outline segments from parts
    for (const auto& part : parts) {
        bool is_bottom_side = (part.mounting_side == BRDPartMountingSide::Bottom);
        float offset_x = is_bottom_side ? bottom_side_offset_x : 0.0f;
        float offset_y = is_bottom_side ? bottom_side_offset_y : 0.0f;
        
        // Use p1 and p2 points to create part outline
        if (part.p1 != part.p2) {
            // Create rectangle from p1 and p2, mirror Y coordinates if bottom side
            float p1_y = is_bottom_side ? -part.p1.y : part.p1.y;
            float p2_y = is_bottom_side ? -part.p2.y : part.p2.y;
            
            BRDPoint top_left = {std::min(part.p1.x, part.p2.x) + offset_x, std::max(p1_y, p2_y) + offset_y};
            BRDPoint top_right = {std::max(part.p1.x, part.p2.x) + offset_x, std::max(p1_y, p2_y) + offset_y};
            BRDPoint bottom_right = {std::max(part.p1.x, part.p2.x) + offset_x, std::min(p1_y, p2_y) + offset_y};
            BRDPoint bottom_left = {std::min(part.p1.x, part.p2.x) + offset_x, std::min(p1_y, p2_y) + offset_y};
            
            part_outline_segments.push_back({top_left, top_right});
            part_outline_segments.push_back({top_right, bottom_right});
            part_outline_segments.push_back({bottom_right, bottom_left});
            part_outline_segments.push_back({bottom_left, top_left});
        } else {
            // Create a small square around the point, mirror Y coordinate if bottom side
            float part_size = 10.0f;
            float center_y = is_bottom_side ? -part.p1.y : part.p1.y;
            BRDPoint center = {part.p1.x + offset_x, center_y + offset_y};
            BRDPoint top_left = {center.x - part_size/2, center.y + part_size/2};
            BRDPoint top_right = {center.x + part_size/2, center.y + part_size/2};
            BRDPoint bottom_right = {center.x + part_size/2, center.y - part_size/2};
            BRDPoint bottom_left = {center.x - part_size/2, center.y - part_size/2};
            
            part_outline_segments.push_back({top_left, top_right});
            part_outline_segments.push_back({top_right, bottom_right});
            part_outline_segments.push_back({bottom_right, bottom_left});
            part_outline_segments.push_back({bottom_left, top_left});
        }
    }
    
    // Generate circles for pins
    for (const auto& pin : pins) {
        bool is_bottom_side = (pin.side == BRDPinSide::Bottom);
        float offset_x = is_bottom_side ? bottom_side_offset_x : 0.0f;
        float offset_y = is_bottom_side ? bottom_side_offset_y : 0.0f;
        
        // Use pin radius if available, otherwise use a default radius
        float radius = static_cast<float>(pin.radius);
        if (radius <= 0.0f) {
            radius = 6.5f; // Default radius similar to XZZPCBFile
        }
        
        // Apply offset to pin position, mirror Y coordinate if bottom side
        float pin_y = is_bottom_side ? -pin.pos.y : pin.pos.y;
        BRDPoint pin_pos = {pin.pos.x + offset_x, pin_y + offset_y};
        
        // Create circle for pin with red color (top) or blue color (bottom)
        float r = is_bottom_side ? 0.0f : 0.7f; // Blue for bottom, red for top
        float g = 0.0f;
        float b = is_bottom_side ? 0.7f : 0.0f;
        BRDCircle circle(pin_pos, radius, r, g, b, 1.0f);
        circles.push_back(circle);
    }
    
    // Generate circles for nails (test points) with different color
    for (const auto& nail : nails) {
        bool is_bottom_side = (nail.side == BRDPartMountingSide::Bottom);
        float offset_x = is_bottom_side ? bottom_side_offset_x : 0.0f;
        float offset_y = is_bottom_side ? bottom_side_offset_y : 0.0f;
        
        float radius = 4.0f; // Fixed radius for test points
        
        // Apply offset to nail position, mirror Y coordinate if bottom side
        float nail_y = is_bottom_side ? -nail.pos.y : nail.pos.y;
        BRDPoint nail_pos = {nail.pos.x + offset_x, nail_y + offset_y};
        
        // Create circle for nail with green color (top) or cyan color (bottom)
        float r = 0.0f;
        float g = 0.7f;
        float b = is_bottom_side ? 0.7f : 0.0f;
        BRDCircle circle(nail_pos, radius, r, g, b, 1.0f);
        circles.push_back(circle);
    }
}

BRDFile::~BRDFile() {
    if (file_buf) {
        free(file_buf);
        file_buf = nullptr;
    }
}
