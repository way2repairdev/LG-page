#include "XZZPCBFile.h"
#include "des.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>

/*
 * Credit to @huertas for DES functions
 * Also credit to @inflex and @MuertoGB for help with cracking the encryption + decoding the format
 */

static unsigned char hexconv[256] = {0}; // Initialize all to 0
// Set up the hex conversion lookup table
void init_hexconv() {
    static bool initialized = false;
    if (!initialized) {
        for (int i = '0'; i <= '9'; i++) hexconv[i] = i - '0';
        for (int i = 'A'; i <= 'F'; i++) hexconv[i] = i - 'A' + 10;
        for (int i = 'a'; i <= 'f'; i++) hexconv[i] = i - 'a' + 10;
        initialized = true;
    }
}

std::unique_ptr<XZZPCBFile> XZZPCBFile::LoadFromFile(const std::string& filepath) {
    std::cout << "LoadFromFile: Opening " << filepath << std::endl;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filepath << std::endl;
        return nullptr;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    std::cout << "LoadFromFile: Creating XZZPCBFile object" << std::endl;
    auto pcbFile = std::make_unique<XZZPCBFile>();
    std::cout << "LoadFromFile: Calling Load() method" << std::endl;
    if (pcbFile->Load(buffer, filepath)) {
        std::cout << "LoadFromFile: Load() succeeded, returning pcbFile" << std::endl;
        return pcbFile;
    }
    std::cout << "LoadFromFile: Load() failed, returning nullptr" << std::endl;
    return nullptr;
}

bool XZZPCBFile::Load(const std::vector<char>& buffer, const std::string& filepath) {
    init_hexconv(); // Initialize hex conversion table
    
    if (!VerifyFormat(buffer)) {
        std::cerr << "Error: Invalid XZZPCB format" << std::endl;
        return false;
    }

    std::cout << "Loading XZZPCB file: " << filepath << " (size: " << buffer.size() << ")" << std::endl;

    // Create a mutable copy for parsing
    std::vector<char> buf(buffer);
    
    return ParseXZZPCBOriginal(buf);
}

bool XZZPCBFile::VerifyFormat(const std::vector<char>& buffer) {
    if (buffer.size() < 6) return false;
    
    bool raw = std::string(buffer.begin(), buffer.begin() + 6) == "XZZPCB";
    if (raw) {
        return true;
    }

    if (buffer.size() > 0x10 && buffer[0x10] != 0x00) {
        uint8_t xor_key = buffer[0x10];
        std::vector<char> xor_buf(buffer.begin(), buffer.begin() + 6);
        for (int i = 0; i < 6; ++i) {
            xor_buf[i] ^= xor_key;
        }
        return std::string(xor_buf.begin(), xor_buf.end()) == "XZZPCB";
    }

    return false;
}

bool XZZPCBFile::ParseXZZPCBOriginal(std::vector<char>& buf) {
    auto v6v6555v6v6 = std::vector<uint8_t>{0x76, 0x36, 0x76, 0x36, 0x35, 0x35, 0x35, 0x76, 0x36, 0x76, 0x36};
    auto v6v6555v6v6_found = std::search(buf.begin(), buf.end(), v6v6555v6v6.begin(), v6v6555v6v6.end());

    if (v6v6555v6v6_found != buf.end()) {
        if (buf[0x10] != 0x00) {
            uint8_t xor_key = buf[0x10];
            for (int i = 0; i < v6v6555v6v6_found - buf.begin(); ++i) {
                buf[i] ^= xor_key; // XOR the buffer with xor_key until v6v6555v6v6 is reached
            }
        }
        ParsePostV6(v6v6555v6v6_found, buf);
    } else {
        if (buf[0x10] != 0) {
            uint8_t xor_key = buf[0x10];
            for (int i = 0; i < buf.end() - buf.begin(); ++i) {
                buf[i] ^= xor_key; // XOR the buffer with xor_key until the end of the buffer as no v6v6555v6v6
            }
        }
    }

    if (buf.size() < 0x30) {
        std::cerr << "Error: Buffer too small for XZZPCB format" << std::endl;
        return false;
    }

    uint32_t main_data_offset = *reinterpret_cast<uint32_t*>(&buf[0x20]);
    uint32_t net_data_offset = *reinterpret_cast<uint32_t*>(&buf[0x28]);

    uint32_t main_data_start = main_data_offset + 0x20;
    uint32_t net_data_start = net_data_offset + 0x20;

    if (main_data_start >= buf.size() || net_data_start >= buf.size()) {
        std::cerr << "Error: Invalid offsets in XZZPCB file" << std::endl;
        return false;
    }

    uint32_t main_data_blocks_size = *reinterpret_cast<uint32_t*>(&buf[main_data_start]);
    uint32_t net_block_size = *reinterpret_cast<uint32_t*>(&buf[net_data_start]);

    if (net_data_start + net_block_size + 4 > buf.size()) {
        std::cerr << "Error: Net block extends beyond buffer" << std::endl;
        return false;
    }

    std::vector<char> net_block_buf(buf.begin() + net_data_start + 4, buf.begin() + net_data_start + net_block_size + 4);
    ParseNetBlockOriginal(net_block_buf);

    uint32_t current_pointer = main_data_start + 4;
    while (current_pointer < main_data_start + 4 + main_data_blocks_size) {
        if (current_pointer >= buf.size()) break;
        
        uint8_t block_type = buf[current_pointer];
        current_pointer += 1;
        
        if (current_pointer + 4 > buf.size()) break;
        uint32_t block_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
        current_pointer += 4;
        
        if (current_pointer + block_size > buf.size()) break;
        std::vector<char> block_buf(buf.begin() + current_pointer, buf.begin() + current_pointer + block_size);
        ProcessBlockOriginal(block_type, block_buf);
        current_pointer += block_size;
    }
    
    FindXYTranslation();
    TranslateSegments();
    TranslatePartOutlineSegments();
    TranslatePins();
    TranslateCircles();
    TranslateRectangles();
    TranslateOvals();

    // Update counts
    num_parts = parts.size();
    num_pins = pins.size();
      std::cout << "XZZPCB parsing completed:" << std::endl;
    std::cout << "  Parts: " << num_parts << std::endl;
    std::cout << "  Pins: " << num_pins << std::endl;
    std::cout << "  Outline segments: " << outline_segments.size() << std::endl;
    std::cout << "  Part outline segments: " << part_outline_segments.size() << std::endl;
    std::cout << "  Circles: " << circles.size() << std::endl;
    std::cout << "  Rectangles: " << rectangles.size() << std::endl;
    std::cout << "  Ovals: " << ovals.size() << std::endl;
    
    // Debug: Print first few pin coordinates
    if (!pins.empty()) {
        std::cout << "First few pin coordinates:" << std::endl;
        for (size_t i = 0; i < std::min((size_t)5, pins.size()); i++) {
            std::cout << "  Pin " << i << ": (" << pins[i].pos.x << ", " << pins[i].pos.y << ") " << pins[i].name << std::endl;
        }
    }
      // Debug: Print first few outline segments
    if (!outline_segments.empty()) {
        std::cout << "First few outline segments:" << std::endl;
        for (size_t i = 0; i < std::min((size_t)3, outline_segments.size()); i++) {
            std::cout << "  Segment " << i << ": (" << outline_segments[i].first.x << ", " << outline_segments[i].first.y << ") to (" 
                     << outline_segments[i].second.x << ", " << outline_segments[i].second.y << ")" << std::endl;
        }
    }

    // Debug: Print first few part outline segments
    if (!part_outline_segments.empty()) {
        std::cout << "First few part outline segments:" << std::endl;
        for (size_t i = 0; i < std::min((size_t)3, part_outline_segments.size()); i++) {
            std::cout << "  Part segment " << i << ": (" << part_outline_segments[i].first.x << ", " << part_outline_segments[i].first.y << ") to (" 
                     << part_outline_segments[i].second.x << ", " << part_outline_segments[i].second.y << ")" << std::endl;
        }
    }

    // Set valid flag to indicate successful parsing
    valid = true;
    std::cout << "XZZPCB file parsed successfully - setting valid flag to true" << std::endl;

    return true;
}

void XZZPCBFile::ProcessBlockOriginal(uint8_t block_type, std::vector<char>& block_buf) {
    switch (block_type) {
        case 0x01: { // ARC
            std::vector<uint32_t> arc_data((uint32_t*)(block_buf.data()), (uint32_t*)(block_buf.data() + block_buf.size()));
            ParseArcBlockOriginal(arc_data);
            break;
        }
        case 0x02: { // VIA
            // Not currently relevant
            break;
        }
        case 0x05: { // LINE SEGMENT
            std::vector<uint32_t> line_segment_data((uint32_t*)(block_buf.data()),
                                                   (uint32_t*)(block_buf.data() + block_buf.size()));
            ParseLineSegmentBlockOriginal(line_segment_data);
            break;
        }
        case 0x06: { // TEXT
            // Not currently relevant
            break;
        }
        case 0x07: { // PART/PIN
            std::vector<char> part_data(block_buf.begin(), block_buf.end());
            ParsePartBlockOriginal(part_data);
            break;
        }
        case 0x09: { // TEST PADS/DRILL HOLES
            std::vector<uint8_t> test_pad_data((uint8_t*)(block_buf.data()), (uint8_t*)(block_buf.data() + block_buf.size()));
            ParseTestPadBlockOriginal(test_pad_data);
            break;
        }
        default:
            break;
    }
}

void XZZPCBFile::des_decrypt(std::vector<char>& buf) {
    std::vector<uint16_t> byteList = {0xE0, 0xCF, 0x2E, 0x9F, 0x3C, 0x33, 0x3C, 0x33};

    std::ostringstream a;
    for (size_t i = 0; i < byteList.size(); i += 2) {
        uint16_t value = (byteList[i] << 8) | byteList[i + 1];
        value ^= 0x3C33; // <3
        a << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << value;
    }
    std::string b = a.str();
    char* c = new char[b.length() + 1];
    std::copy(b.begin(), b.end(), c);
    c[b.length()] = '\0';

    std::vector<uint8_t> buf_uint8(buf.begin(), buf.end());
    uint8_t* p = buf_uint8.data();
    uint8_t* ep = p + buf_uint8.size();

    uint64_t k = 0x0000000000000000;
    const char* kp = c;
    for (int i = 0; i < 8; i++) {
        uint64_t v = hexconv[(int)*kp] * 16 + hexconv[(int)*(kp + 1)];
        k |= (v << ((7 - i) * 8));
        kp += 2;
    }

    std::vector<uint8_t> decrypted_buf;
    while (p < ep) {
        unsigned char e[8];
        unsigned char d[8];
        uint64_t d64;
        uint64_t e64;

        // Build encrypted block
        for (int i = 0; i < 8; i++) {
            e[7 - i] = *p;
            p++;
        }
        memcpy(&e64, e, 8);

        // Decode/decrypt
        d64 = des(e64, k, 'd');

        // Reverse d64 and append to decrypted_buf
        for (int i = 0; i < 8; i++) {
            d[i] = (d64 >> (i * 8)) & 0xff; // Extract each byte
        }
        for (int i = 7; i >= 0; i--) {
            decrypted_buf.push_back(d[i]); // Append in reverse order
        }
    }
    buf = std::vector<char>(decrypted_buf.begin(), decrypted_buf.end());
    delete[] c;
}

std::vector<std::pair<BRDPoint, BRDPoint>> XZZPCBFile::xzz_arc_to_segments(int startAngle, int endAngle, int r, BRDPoint pc) {
    const int numPoints = 10;
    std::vector<std::pair<BRDPoint, BRDPoint>> arc_segments{};

    if (startAngle > endAngle) {
        std::swap(startAngle, endAngle);
    }

    if (endAngle - startAngle > 180) {
        startAngle += 360;
    }

    double startAngleD = static_cast<double>(startAngle);
    double endAngleD = static_cast<double>(endAngle);
    double rD = static_cast<double>(r);
    double pc_xD = static_cast<double>(pc.x);
    double pc_yD = static_cast<double>(pc.y);

    const double degToRad = 3.14159265358979323846 / 180.0;
    startAngleD *= degToRad;
    endAngleD *= degToRad;

    double angleStep = (endAngleD - startAngleD) / (numPoints - 1);

    BRDPoint pold = {static_cast<int>(static_cast<float>(pc_xD + rD * std::cos(startAngleD))),
                     static_cast<int>(static_cast<float>(pc_yD + rD * std::sin(startAngleD)))};
    for (int i = 1; i < numPoints; ++i) {
        double angle = startAngleD + i * angleStep;
        BRDPoint p = {static_cast<int>(pc_xD + rD * std::cos(angle)), static_cast<int>(pc_yD + rD * std::sin(angle))};
        arc_segments.push_back({pold, p});
        pold = p;
    }

    return arc_segments;
}

void XZZPCBFile::ParseArcBlockOriginal(std::vector<uint32_t>& buf) {
    uint32_t layer = buf[0];
    uint32_t x = buf[1];
    uint32_t y = buf[2];
    int32_t r = buf[3];
    int32_t angle_start = buf[4];
    int32_t angle_end = buf[5];
    int32_t scale = buf[6];
    // int32_t unknown_arc = buf[7];
    scale = 10000;
    if (layer != 28 && layer != 17) {
        return;
    }

    int point_x = static_cast<int>(x / scale);
    int point_y = static_cast<int>(y / scale);
    r = r / scale;
    angle_start = angle_start / scale;
    angle_end = angle_end / scale;
    BRDPoint centre = {point_x, point_y};

    std::vector<std::pair<BRDPoint, BRDPoint>> segments = xzz_arc_to_segments(angle_start, angle_end, r, centre);
    std::move(segments.begin(), segments.end(), std::back_inserter(outline_segments));
}

void XZZPCBFile::ParseLineSegmentBlockOriginal(std::vector<uint32_t>& buf) {
    int32_t layer = buf[0];
    int32_t x1 = buf[1];
    int32_t y1 = buf[2];
    int32_t x2 = buf[3];
    int32_t y2 = buf[4];
    int32_t scale = buf[5];
    scale = 10000;
    // int32_t trace_net_index = buf[6];	/* unused */
    if (layer != 28 && layer != 17) {
        return;
    }

    BRDPoint point;
    point.x = static_cast<int>(static_cast<double>(x1) / static_cast<double>(scale));
    point.y = static_cast<int>(static_cast<double>(y1) / static_cast<double>(scale));
    BRDPoint point2;
    point2.x = static_cast<int>(static_cast<double>(x2) / static_cast<double>(scale));
    point2.y = static_cast<int>(static_cast<double>(y2) / static_cast<double>(scale));
    outline_segments.push_back({point, point2});
}

void XZZPCBFile::ParsePartBlockOriginal(std::vector<char>& buf) {
    BRDPart blank_part;
    BRDPin blank_pin;
    BRDPart part;
    BRDPin pin;

    des_decrypt(buf);

    uint32_t current_pointer = 0;
    if (buf.size() < 4) return;
    uint32_t part_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    current_pointer += 18;
    
    if (current_pointer + 4 > buf.size()) return;
    uint32_t part_group_name_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    current_pointer += part_group_name_size;

    // So far 0x06 sub blocks have been first always
    // Also contains part name so needed before pins
    if (current_pointer >= buf.size() || buf[current_pointer] != 0x06) {
        return;
    }

    current_pointer += 31;
    if (current_pointer + 4 > buf.size()) return;
    uint32_t part_name_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    if (current_pointer + part_name_size > buf.size()) return;
    std::string part_name(reinterpret_cast<char*>(&buf[current_pointer]), part_name_size);
    current_pointer += part_name_size;

    part.name = part_name;
    part.mounting_side = BRDPartMountingSide::Top;
    part.part_type = BRDPartType::SMD;

    // uint32_t pin_count = 0; /* currently unused */
    while (current_pointer <= part_size && current_pointer < buf.size()) {
        uint8_t sub_type_identifier = buf[current_pointer];
        current_pointer += 1;

        switch (sub_type_identifier) {
            case 0x01: {
                // Currently unsure what this is
                if (current_pointer + 4 > buf.size()) return;
                current_pointer += *reinterpret_cast<uint32_t*>(&buf[current_pointer]) + 4; // Skip the block
                break;
            }
            case 0x05: { // Line Segment - Part outline
                if (current_pointer + 4 > buf.size()) return;
                uint32_t line_block_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
                current_pointer += 4;
                
                // Process line segment data (expected format: similar to main line segments)
                if (line_block_size >= 24 && current_pointer + line_block_size <= buf.size()) { // 6 uint32_t values = 24 bytes minimum
                    uint32_t* line_data = reinterpret_cast<uint32_t*>(&buf[current_pointer]);
                    
                    // Extract line segment data (assuming similar format to ParseLineSegmentBlockOriginal)
                    // int32_t layer = line_data[0];  // Layer - may not be relevant for part outlines
                    int32_t x1 = line_data[1];
                    int32_t y1 = line_data[2]; 
                    int32_t x2 = line_data[3];
                    int32_t y2 = line_data[4];
                    int32_t scale = line_data[5];
                    
                    // Use consistent scaling
                    scale = 10000;
                    
                    // Create line segment points
                    BRDPoint point1;
                    point1.x = static_cast<int>(static_cast<double>(x1) / static_cast<double>(scale));
                    point1.y = static_cast<int>(static_cast<double>(y1) / static_cast<double>(scale));
                    BRDPoint point2;
                    point2.x = static_cast<int>(static_cast<double>(x2) / static_cast<double>(scale));
                    point2.y = static_cast<int>(static_cast<double>(y2) / static_cast<double>(scale));
                    
                    // Add to part outline segments for rendering (these are part outlines, not board outlines)
                    part_outline_segments.push_back({point1, point2});
                    
                    //std::cout << "DEBUG: Added part outline segment from (" << point1.x << ", " << point1.y 
                             //<< ") to (" << point2.x << ", " << point2.y << ") for part: " << part_name << std::endl;
                }
                
                current_pointer += line_block_size;
                break;
            }
            case 0x06: { // Labels/Part Names
                // Not currently relevant for BRDPin
                if (current_pointer + 4 > buf.size()) return;
                current_pointer += *reinterpret_cast<uint32_t*>(&buf[current_pointer]) + 4; // Skip the block
                break;
            }
            case 0x09: { // Pins
                // pin_count += 1;
                pin.side = BRDPinSide::Top;

                // Block size
                if (current_pointer + 4 > buf.size()) return;
                uint32_t pin_block_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
                uint32_t pin_block_end = current_pointer + pin_block_size + 4;
                current_pointer += 4;
                current_pointer += 4; // currently unknown

                if (current_pointer + 16 > buf.size()) return;
                pin.pos.x = *reinterpret_cast<uint32_t*>(&buf[current_pointer]) / 10000;
                current_pointer += 4;
                pin.pos.y = *reinterpret_cast<uint32_t*>(&buf[current_pointer]) / 10000;
                current_pointer += 4;
                
                current_pointer += 4; // currently unknown
                uint32_t pin_rotation = *reinterpret_cast<uint32_t*>(&buf[current_pointer]) / 10000; // Rotation in degrees
                //pin_rotation = pin_rotation + 90; // Adjust to match BRD coordinate system (0 degrees is right, 90 degrees is up)
                current_pointer += 4;

                if (current_pointer + 4 > buf.size()) return;
                uint32_t pin_name_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
                current_pointer += 4;
                if (current_pointer + pin_name_size > buf.size()) return;
                std::string pin_name(reinterpret_cast<char*>(&buf[current_pointer]), pin_name_size);
                pin.name = pin_name;
                pin.snum = pin_name;
                
                // Debug: Log pin data loading
                //std::cout << "DEBUG: Loaded pin - name: '" << pin_name << "', setting snum to: '" << pin.snum << "'" << std::endl;
                //std::cout << "Pin rotation: '" << pin_rotation << "'" << std::endl;


                current_pointer += pin_name_size;
                uint32_t height_radius_raw = *reinterpret_cast<uint32_t*>(&buf[current_pointer]); // Height for rectangular pins, radius for circular pins
                current_pointer += 4;
                uint32_t width_raw = *reinterpret_cast<uint32_t*>(&buf[current_pointer]); // Width for rectangular pins
                current_pointer += 22;
                uint8_t pin_shape = *reinterpret_cast<uint8_t*>(&buf[current_pointer]); // Shape for pins



                
                // Extract shape data based on pin_rotation
                if (current_pointer + 4 <= buf.size()) {
                    //uint32_t height_radius_raw = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
                    //current_pointer += 4;
                    
                    if (pin_shape == 1) {
                        // Check if it's circular (height == width) or oval (height != width)
                        if (height_radius_raw == width_raw) {
                            // Circular pin - height and width are equal
                            float diameter = static_cast<float>(height_radius_raw) / 10000.0f; // Apply same scaling as coordinates
                            float radius = diameter / 2.0f;
                            
                            // Create circle with red fill color at pin position
                            BRDCircle circle(pin.pos, radius, 0.7f, 0.0f, 0.0f, 1.0f); // Red color (R=1.0, G=0.0, B=0.0, A=1.0)
                            circles.push_back(circle);
                            
                            //std::cout << "DEBUG: Added circle for pin '" << pin_name << "' at (" << pin.pos.x << ", " << pin.pos.y 
                                     //<< ") with diameter " << diameter << " (radius " << radius << ")" << std::endl;
                        } else {

                            // If pin_rotation is 900000, treat as 0 (no rotation), else keep as is
                        if (pin_rotation == 0 || pin_rotation == 90 || pin_rotation == 180 || pin_rotation == 270 || pin_rotation == 360) {
                            pin_rotation += 90;
                        }
                        
                            // Oval pin - height and width are different
                            float height = static_cast<float>(height_radius_raw) / 10000.0f; // Apply same scaling as coordinates
                            float width = static_cast<float>(width_raw) / 10000.0f;
                            
                            // Create oval with red fill color at pin position
                            BRDOval oval(pin.pos, width, height, static_cast<float>(pin_rotation), 0.7f, 0.0f, 0.0f, 1.0f); // Red color
                            ovals.push_back(oval);
                            
                            //std::cout << "DEBUG: Added oval for pin '" << pin_name << "' at (" << pin.pos.x << ", " << pin.pos.y 
                                     //<< ") with width " << width << ", height " << height << std::endl;
                        }
                    } else {
                        // If pin_rotation is 900000, treat as 0 (no rotation), else keep as is
                        if (pin_rotation == 0 || pin_rotation == 90 || pin_rotation == 180 || pin_rotation == 270 || pin_rotation == 360) {
                            pin_rotation += 90;
                        }
                       
                        
                        // Rectangular pin - height_radius_raw is height, need to get width next
                        float height = static_cast<float>(height_radius_raw) / 10000.0f; // Apply same scaling as coordinates
                        
                        // Get width (next 4 bytes)
                        if (current_pointer + 4 <= buf.size()) {
                            //uint32_t width_raw = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
                            float width = static_cast<float>(width_raw) / 10000.0f;
                            
                            // Create rectangle with red fill color at pin position
                            BRDRectangle rectangle(pin.pos, width, height, static_cast<float>(pin_rotation), 0.7f, 0.0f, 0.0f, 1.0f); // Red color
                            rectangles.push_back(rectangle);
                            
                            //std::cout << "DEBUG: Added rectangle for pin '" << pin_name << "' at (" << pin.pos.x << ", " << pin.pos.y 
                                     //<< ") with width " << width << ", height " << height << ", rotation " << pin_rotation << "°" << "shape: " << pin_shape <<std::endl;
                        }
                    }
                }
                
                current_pointer += 6;

                if (current_pointer + 4 > buf.size()) return;
                uint32_t net_index = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
                current_pointer = pin_block_end;

                std::string diode_reading;
                std::string pin_net = net_dict[net_index];

                if (pin_net == "NC") {
                    pin.net = "NC";
                } else {
                    pin.net = pin_net;
                }                pin.part = parts.size() + 1;

                if (!diode_reading.empty()) {
                    pin.comment = diode_reading;
                } else if (diode_readings_type == 1) {
                    if (diode_dict.find(part.name) != diode_dict.end() &&
                        diode_dict[part.name].find(pin.name) != diode_dict[part.name].end()) {
                        pin.comment = diode_dict[part.name][pin.name];
                    }
                } else if (diode_readings_type == 2) {
                    if (diode_dict.find(pin.net) != diode_dict.end()) {
                        pin.comment = diode_dict[pin.net]["0"];
                    }
                }

                pins.push_back(pin);
                pin = blank_pin;
                break;
            }
            default:
                if (sub_type_identifier != 0x00) {
                    printf("Unknown sub block type: 0x%02X at %d in %s\n", sub_type_identifier, current_pointer, part_name.c_str());
                }
                break;
        }
    }

    part.end_of_pins = pins.size();
    parts.push_back(part);
    part = blank_part;
}

void XZZPCBFile::ParseTestPadBlockOriginal(std::vector<uint8_t>& buf) {
    BRDPart blank_part;
    BRDPin blank_pin;
    BRDPart part;
    BRDPin pin;

    uint32_t current_pointer = 0;
    if (buf.size() < 20) return;
    // uint32_t pad_number = *reinterpret_cast<uint32_t*>(&buf[current_pointer]); /* unused */
    current_pointer += 4;
    uint32_t x_origin = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    uint32_t y_origin = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    current_pointer += 4; // inner_diameter

    uint32_t pin_rotation = *reinterpret_cast<uint32_t*>(&buf[current_pointer]) / 10000;

    current_pointer += 4; // rotation
    uint32_t name_length = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    if (current_pointer + name_length > buf.size()) return;
    std::string name(reinterpret_cast<char*>(&buf[current_pointer]), name_length);
    current_pointer += name_length;
    if (current_pointer + 8 > buf.size()) return;
    uint32_t width_raw = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    uint32_t height_raw = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    current_pointer += 4;
    uint8_t pin_shape = *reinterpret_cast<uint8_t*>(&buf[current_pointer]);

    // Optionally, store or use width_raw and height_raw for rendering test pad shapes
    current_pointer = buf.size() - 12;
    std::cout << "Buffer Size '" << buf.size() << "'" << std::endl;
    std::cout << "Current Pointer After '" << current_pointer << "'" << std::endl;
    if (current_pointer >= buf.size()) return;
    uint32_t net_index = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
    std::cout << "Net index '" << net_index << "'" << std::endl;

    // Create test pad shapes based on width and height
    float width = static_cast<float>(width_raw) / 10000.0f;
    float height = static_cast<float>(height_raw) / 10000.0f;

    

    BRDPoint test_pad_pos;
    test_pad_pos.x = static_cast<int>(static_cast<double>(x_origin / 10000.0));
    test_pad_pos.y = static_cast<int>(static_cast<double>(y_origin / 10000.0));
    
    if (pin_shape == 1) {
        // Create circle for test pad when width equals height
        float radius = width / 2.0f;
        BRDCircle circle(test_pad_pos, radius, 0.7f, 0.0f, 0.0f, 1.0f); // Green color for test pads
        circles.push_back(circle);
        
        //std::cout << "DEBUG: Added circle test pad '" << name << "' at (" << test_pad_pos.x << ", " << test_pad_pos.y 
                 //<< ") with radius " << radius << std::endl;
    } else {

        
        // Create rectangle for test pad when width differs from height
        BRDRectangle rectangle(test_pad_pos, width, height, static_cast<float>(pin_rotation), 0.7f, 0.0f, 0.0f, 1.0f); // Green color for test pads
        rectangles.push_back(rectangle);
        
        //std::cout << "DEBUG: Added rectangle test pad '" << name << "' at (" << test_pad_pos.x << ", " << test_pad_pos.y 
                 //<< ") with width " << width << ", height " << height << std::endl;
    }    part.name = "..." + name; // To make it get the kPinTypeTestPad type
    part.mounting_side = BRDPartMountingSide::Top;
    part.part_type = BRDPartType::SMD;

    pin.snum = name;
    
    // Debug: Log pin data loading for test pad
    //std::cout << "DEBUG: Loaded test pad pin - name: '" << name << "', setting snum to: '" << pin.snum << "'" << std::endl;

    pin.side = BRDPinSide::Top;
    pin.pos.x = static_cast<int>(static_cast<double>(x_origin / 10000.0));
    pin.pos.y = static_cast<int>(static_cast<double>(y_origin / 10000.0));
    if (net_dict.find(net_index) != net_dict.end()) {
        if (net_dict[net_index] == "UNCONNECTED" || net_dict[net_index] == "NC") {
            pin.net = ""; // As the part already gets the kPinTypeTestPad type if "UNCONNECTED" is used type will be changed
                          // to kPinTypeNotConnected
        } else {
            pin.net = net_dict[net_index];
        }
    } else {
        pin.net = ""; // As the part already gets the kPinTypeTestPad type if "UNCONNECTED" is used type will be changed to
                      // kPinTypeNotConnected
    }
    pin.part = parts.size() + 1;
    pins.push_back(pin);
    pin = blank_pin;
    part.end_of_pins = pins.size();
    parts.push_back(part);
    part = blank_part;
}

void XZZPCBFile::ParseNetBlockOriginal(std::vector<char>& buf) {
    uint32_t current_pointer = 0;
    while (current_pointer < buf.size()) {
        if (current_pointer + 8 > buf.size()) break;
        uint32_t net_size = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
        current_pointer += 4;
        uint32_t net_index = *reinterpret_cast<uint32_t*>(&buf[current_pointer]);
        current_pointer += 4;
        if (current_pointer + net_size - 8 > buf.size()) break;
        std::string net_name(&buf[current_pointer], net_size - 8);
        current_pointer += net_size - 8;

        net_dict[net_index] = net_name;
    }
}

char XZZPCBFile::read_utf8_char(char c) const {
    if (static_cast<unsigned char>(c) < 128) {
        return c; // Print ASCII character as is
    } else {
        return '?'; // Replace non-ASCII character with '?'
    }
}

std::string XZZPCBFile::read_cb2312_string(const std::string& str) {
    std::string result;
    bool last_was_cb2312 = false; // As CB2312 encoded characters are 2 bytes each so single '?' can represent one CB2312 character
    for (char c : str) {
        if (static_cast<unsigned char>(c) < 128) {
            result += c;
            last_was_cb2312 = false;
        } else {
            if (last_was_cb2312) {
                result += '?';
                last_was_cb2312 = false;
            } else {
                last_was_cb2312 = true;
            }
        }
    }
    return result;
}

// atm some diode readings aren't processed properly
void XZZPCBFile::ParsePostV6(std::vector<char>::iterator v6_pos, std::vector<char>& buf) {
    unsigned int current_pointer = v6_pos - buf.begin() + 11;
    current_pointer += 7; // While post v6 isnt handled properly
    if (current_pointer >= buf.size()) return;
    
    // Check for Type 1 variants
    bool is_type1_0x0A = (buf[current_pointer] == 0x0A);
    bool is_type1_0x0D0A = (current_pointer + 1 < buf.size() && 
                            buf[current_pointer] == 0x0D && 
                            buf[current_pointer + 1] == 0x0A);
    
    if (is_type1_0x0A || is_type1_0x0D0A) {
        // Type 1 - Two variants:
        // Type 1A: 0x0A '=480=N65594(1)'
        // Type 1B: 0x0D 0x0A '=480=N65594(1)'
        diode_readings_type = 1;
        
        // Skip the initial delimiter(s)
        if (is_type1_0x0D0A) {
            current_pointer += 2; // Skip 0x0D 0x0A
        } else {
            current_pointer += 1; // Skip 0x0A
        }
        
        while (current_pointer < buf.size()) {
            // Look for the start of a diode reading entry
            // For Type 1B, entries might be separated by 0x0D 0x0A
            if (is_type1_0x0D0A && current_pointer + 1 < buf.size() && 
                buf[current_pointer] == 0x0D && buf[current_pointer + 1] == 0x0A) {
                current_pointer += 2; // Skip 0x0D 0x0A separator
                if (current_pointer >= buf.size()) return;
            }
            
            // Check if we found the start of a reading (should start with '=')
            if (current_pointer >= buf.size() || buf[current_pointer] != 0x3D) {
                current_pointer += 1;
                continue;
            }
            current_pointer += 1; // Skip '='
            std::string volt_reading = "";
            while (current_pointer < buf.size() && buf[current_pointer] != 0x3D) {
                volt_reading += buf[current_pointer];
                current_pointer += 1;
            }
            volt_reading = read_cb2312_string(volt_reading);
            current_pointer += 1;
            std::string net = "";
            while (current_pointer < buf.size() && buf[current_pointer] != 0x28) {
                net += buf[current_pointer];
                current_pointer += 1;
            }
            net = read_cb2312_string(net);
            current_pointer += 1;
            std::string pin_name = "";
            while (current_pointer < buf.size() && buf[current_pointer] != 0x29) {
                pin_name += buf[current_pointer];
                current_pointer += 1;
            }
            pin_name = read_cb2312_string(pin_name);
            current_pointer += 1;

            diode_dict[net][pin_name] = volt_reading;
        }
    } else {
        if (buf[current_pointer] != 0x0D) {
            // Type 2
            // 0xCD 0xBC 0x0D 0x0A 'Net242=0'
            // printf("Type 2 Diode Reading\n");
            current_pointer += 2; // Currently unknown what these two bytes are
        } else {
            // Type 3
            // 0x0D 0x0A 'SMBUS_SMC_5_G3_SCL=0.5'
            // printf("Type 3 Diode Reading\n");
        }
        diode_readings_type = 2;

        while (current_pointer < buf.size()) {
            current_pointer += 2;
            if (current_pointer >= buf.size()) {
                return;
            }
            if (buf[current_pointer] == 0x0D) {
                break; // Has done 0x0D 0x0A 0x0D 0x0A so end of block
            }
            std::string net = "";
            while (current_pointer < buf.size() && buf[current_pointer] != 0x3D) {
                net += buf[current_pointer];
                current_pointer += 1;
            }
            net = read_cb2312_string(net);
            current_pointer += 1;
            std::string comment = "";
            while (current_pointer < buf.size() && buf[current_pointer] != 0x0D) {
                comment += buf[current_pointer];
                current_pointer += 1;
            }
            comment = read_cb2312_string(comment);
            diode_dict[net]["0"] = comment; // Seemingly is there is a second reading the first is replaced
        }
    }
}

// Translation and mirroring functions
void XZZPCBFile::FindXYTranslation() {
    // Assuming line segments encompass all parts
    // Find the min and max x and y values

    if (outline_segments.empty()) {
        xy_translation.x = 0;
        xy_translation.y = 0;
        return;
    }
    xy_translation.x = outline_segments[0].first.x;
    xy_translation.y = outline_segments[0].first.y;
    for (auto& segment : outline_segments) {
        xy_translation.x = std::min({xy_translation.x, segment.first.x, segment.second.x});
        xy_translation.y = std::min({xy_translation.y, segment.first.y, segment.second.y});
    }
}

void XZZPCBFile::TranslatePoints(BRDPoint& point) const {
    point.x -= xy_translation.x;
    point.y -= xy_translation.y;
}

void XZZPCBFile::TranslateSegments() {
    for (auto& segment : outline_segments) {
        TranslatePoints(segment.first);
        TranslatePoints(segment.second);
    }
}

void XZZPCBFile::TranslatePartOutlineSegments() {
    for (auto& segment : part_outline_segments) {
        TranslatePoints(segment.first);
        TranslatePoints(segment.second);
    }
}

void XZZPCBFile::TranslatePins() {
    for (auto& pin : pins) {
        TranslatePoints(pin.pos);
    }
}

void XZZPCBFile::TranslateCircles() {
    for (auto& circle : circles) {
        TranslatePoints(circle.center);
    }
}

void XZZPCBFile::TranslateRectangles() {
    for (auto& rectangle : rectangles) {
        TranslatePoints(rectangle.center);
    }
}

void XZZPCBFile::TranslateOvals() {
    for (auto& oval : ovals) {
        TranslatePoints(oval.center);
    }
}

// Legacy compatibility methods
void XZZPCBFile::CreateEnhancedSampleData() {
    // This method is kept for compatibility but not used in the full implementation
}