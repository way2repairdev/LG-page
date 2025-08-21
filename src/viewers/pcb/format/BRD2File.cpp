#include "BRD2File.h"
#include "Utils.h"
#include <cctype>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <algorithm>

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
        this->error_msg = error_msg_str; \
        action; \
    }

// Helper function to split buffer into lines
void stringfile_brd2(char *buffer, std::vector<char*> &lines) {
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
bool find_str_in_buf_brd2(const std::string& needle, const std::vector<char>& buf) {
    if (needle.length() > buf.size()) return false;
    auto it = std::search(buf.begin(), buf.end(), needle.begin(), needle.end());
    return it != buf.end();
}

std::unique_ptr<BRD2File> BRD2File::LoadFromFile(const std::string& filepath) {
    std::cout << "LoadFromFile: Opening BRD2 file " << filepath << std::endl;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open BRD2 file " << filepath << std::endl;
        return nullptr;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    std::cout << "LoadFromFile: Creating BRD2File object" << std::endl;
    auto brd2File = std::make_unique<BRD2File>();
    std::cout << "LoadFromFile: Calling Load() method" << std::endl;
    if (brd2File->Load(buffer, filepath)) {
        std::cout << "LoadFromFile: Load() succeeded, returning brd2File" << std::endl;
        return brd2File;
    }
    std::cout << "LoadFromFile: Load() failed, returning nullptr" << std::endl;
    return nullptr;
}

bool BRD2File::VerifyFormat(const std::vector<char>& buffer) {
    return find_str_in_buf_brd2("BRDOUT:", buffer) && find_str_in_buf_brd2("NETS:", buffer);
}

bool BRD2File::Load(const std::vector<char>& buf, const std::string& /*filepath*/) {
    auto buffer_size = buf.size();
    std::unordered_map<int, std::string> nets; // Map between net id and net name
    unsigned int num_nets = 0;
    BRDPoint max{0, 0}; // Top-right board boundary

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

    int current_block = 0;

    std::vector<char*> lines;
    stringfile_brd2(file_buf, lines);

    for (char *line : lines) {
        while (isspace((uint8_t)*line)) line++;
        if (!line[0]) continue;

        char *p = line;
        char *s;

        if (strstr(line, "BRDOUT:") == line) {
            current_block = 1;
            p += 7; // Skip "BRDOUT:"
            num_format = READ_UINT();
            max.x = READ_INT();
            max.y = READ_INT();
            continue;
        }
        if (strstr(line, "NETS:") == line) {
            current_block = 2;
            p += 5; // Skip "NETS:"
            num_nets = READ_UINT();
            continue;
        }
        if (strstr(line, "PARTS:") == line) {
            current_block = 3;
            p += 6; // Skip "PARTS:"
            num_parts = READ_UINT();
            continue;
        }
        if (strstr(line, "PINS:") == line) {
            current_block = 4;
            p += 5; // Skip "PINS:"
            num_pins = READ_UINT();
            continue;
        }
        if (strstr(line, "NAILS:") == line) {
            current_block = 5;
            p += 6; // Skip "NAILS:"
            num_nails = READ_UINT();
            continue;
        }

        switch (current_block) {
            case 1: { // Format
                if (format.size() >= num_format) break;
                BRDPoint point;
                point.x = READ_INT();
                point.y = READ_INT();
                if (point.x > max.x || point.y > max.y) {
                    error_msg = "Format point exceeds board boundary";
                    return false;
                }
                format.push_back(point);
            } break;

            case 2: { // Nets
                if (nets.size() >= num_nets) break;
                int id = READ_UINT();
                nets[id] = READ_STR();
            } break;

            case 3: { // PARTS
                if (parts.size() >= num_parts) break;
                BRDPart part;

                part.name = READ_STR();
                part.p1.x = READ_INT();
                part.p1.y = READ_INT();
                part.p2.x = READ_INT();
                part.p2.y = READ_INT();
                part.end_of_pins = READ_UINT(); // Warning: not end but beginning in this format
                part.part_type = BRDPartType::SMD;
                int side = READ_UINT();
                if (side == 1)
                    part.mounting_side = BRDPartMountingSide::Top; // SMD part on top
                else if (side == 2)
                    part.mounting_side = BRDPartMountingSide::Bottom; // SMD part on bottom
                else //0
                    part.mounting_side = BRDPartMountingSide::Both;

                parts.push_back(part);
            } break;

            case 4: { // PINS
                if (pins.size() >= num_pins) break;
                BRDPin pin;

                pin.pos.x = READ_INT();
                pin.pos.y = READ_INT();
                int netid = READ_UINT();
                unsigned int side = READ_UINT();
                if (side == 1)
                    pin.side = BRDPinSide::Top;
                else if (side == 2)
                    pin.side = BRDPinSide::Bottom;
                else //0
                    pin.side = BRDPinSide::Both;

                auto it = nets.find(netid);
                if (it != nets.end()) {
                    pin.net = it->second;
                } else {
                    pin.net = "UNCONNECTED";
                }

                pin.probe = 1;
                pin.part = 0;
                pins.push_back(pin);
            } break;

            case 5: { // NAILS
                if (nails.size() >= num_nails) break;
                BRDNail nail;

                nail.probe = READ_UINT();
                nail.pos.x = READ_INT();
                nail.pos.y = READ_INT();
                int netid = READ_UINT();

                auto inet = nets.find(netid);
                if (inet != nets.end())
                    nail.net = inet->second;
                else {
                    nail.net = "UNCONNECTED";
                    std::cerr << "Missing net id: " << netid << std::endl;
                }

                bool nail_is_top = READ_UINT() == 1;
                if (nail_is_top) {
                    nail.side = BRDPartMountingSide::Top;
                } else {
                    nail.side = BRDPartMountingSide::Bottom;
                    nail.pos.y = max.y - nail.pos.y;
                }
                nails.push_back(nail);

            } break;
            default: continue;
        }
    }

    if (num_format != format.size()) {
        error_msg = "Format count mismatch";
        return false;
    }
    if (num_nets != nets.size()) {
        error_msg = "Nets count mismatch";
        return false;
    }
    if (num_parts != parts.size()) {
        error_msg = "Parts count mismatch";
        return false;
    }
    if (num_pins != pins.size()) {
        error_msg = "Pins count mismatch";
        return false;
    }
    if (num_nails != nails.size()) {
        error_msg = "Nails count mismatch";
        return false;
    }

    /*
     * Postprocess the data. Specifically we need to allocate
     * pins to parts. So with this it's easiest to just iterate
     * through the parts and pick up the pins required
     */
    {
        int pei;     // pin end index (part[i+1].pin# -1
        int cpi = 0; // current pin index
        for (decltype(parts)::size_type i = 0; i < parts.size(); i++) {
            bool isDIP = true;

            if (parts[i].mounting_side == BRDPartMountingSide::Bottom) { // Part on bottom
                parts[i].p1.y = max.y - parts[i].p1.y;
                parts[i].p2.y = max.y - parts[i].p2.y;
            }

            if (i == parts.size() - 1) {
                pei = pins.size();
            } else {
                pei = parts[i + 1].end_of_pins; // Again, not end of pins but beginning
            }

            while (cpi < pei) {
                pins[cpi].part = i + 1;
                if (pins[cpi].side != BRDPinSide::Top) pins[cpi].pos.y = max.y - pins[cpi].pos.y;
                if ((pins[cpi].side == BRDPinSide::Top && parts[i].mounting_side == BRDPartMountingSide::Top) ||
                    (pins[cpi].side == BRDPinSide::Bottom && parts[i].mounting_side == BRDPartMountingSide::Bottom)) {
                    // Pins on the same side as the part
                    isDIP = false;
                }
                cpi++;
            }

            if (isDIP) {
                // All pins are through hole so part is DIP
                parts[i].part_type = BRDPartType::ThroughHole;
                parts[i].mounting_side = BRDPartMountingSide::Both;
            } else {
                parts[i].part_type = BRDPartType::SMD;
            }
        }
    }

    for (auto i = 1; i <= 2; i++) { // Add dummy parts for probe points on both sides
        BRDPart part;
        part.name = "...";
        part.mounting_side =
            (i == 1 ? BRDPartMountingSide::Bottom : BRDPartMountingSide::Top); // First part is bottom, last is top.
        part.end_of_pins = 0; // Unused
        parts.push_back(part);
    }

    // Add nails as pins
    for (const auto& nail : nails) {
        BRDPin pin;
        pin.pos = nail.pos;
        pin.probe = nail.probe;
        pin.part = (nail.side == BRDPartMountingSide::Top) ? parts.size() : parts.size() - 1; // Use dummy parts
        pin.side = (nail.side == BRDPartMountingSide::Top) ? BRDPinSide::Top : BRDPinSide::Bottom;
        pin.net = nail.net;
        pins.push_back(pin);
    }

    valid = current_block != 0;
    
    // Generate rendering geometry for pins
    if (valid) {
        GenerateRenderingGeometry();
    }
    
    std::cout << "BRD2 file parsed successfully:" << std::endl;
    std::cout << "  Parts: " << parts.size() << std::endl;
    std::cout << "  Pins: " << pins.size() << std::endl;
    std::cout << "  Nails: " << nails.size() << std::endl;
    std::cout << "  Format points: " << format.size() << std::endl;
    std::cout << "  Circles: " << circles.size() << std::endl;
    
    return valid;
}

void BRD2File::GenerateRenderingGeometry() {
    // Clear existing geometry
    circles.clear();
    rectangles.clear();
    ovals.clear();
    
    // Generate circles for pins
    for (const auto& pin : pins) {
        // Use pin radius if available, otherwise use a default radius
        float radius = static_cast<float>(pin.radius);
        if (radius <= 0.0f) {
            radius = 6.5f; // Default radius similar to XZZPCBFile
        }
        
        // Create circle for pin with red color (similar to XZZPCBFile)
        BRDCircle circle(pin.pos, radius, 0.7f, 0.0f, 0.0f, 1.0f); // Red color
        circles.push_back(circle);
    }
    
    // Generate circles for nails (test points) with different color
    for (const auto& nail : nails) {
        float radius = 4.0f; // Fixed radius for test points
        
        // Create circle for nail with green color
        BRDCircle circle(nail.pos, radius, 0.0f, 0.7f, 0.0f, 1.0f); // Green color
        circles.push_back(circle);
    }
}

BRD2File::~BRD2File() {
    if (file_buf) {
        free(file_buf);
        file_buf = nullptr;
    }
}
