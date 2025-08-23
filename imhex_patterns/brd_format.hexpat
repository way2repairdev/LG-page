//==============================================================================
// ImHex Pattern for BRD File Format
// Board File Format used for PCB (Printed Circuit Board) data
// Based on the BRDFile.cpp implementation
//==============================================================================

#pragma author OpenAI Assistant
#pragma description BRD file format pattern for PCB board files
#pragma MIME text/plain

#include <std/io.pat>
#include <std/mem.pat>
#include <std/string.pat>

// Color definitions for better visualization
#define COLOR_HEADER    0xFF4444
#define COLOR_SECTION   0x44FF44  
#define COLOR_DATA      0x4444FF
#define COLOR_COORDS    0xFF8844
#define COLOR_STRING    0xFFFF44

//==============================================================================
// Basic Types
//==============================================================================

struct BRDPoint {
    s32 x [[color("FF8844")]];  // X coordinate (in mils/thou)
    s32 y [[color("FF8844")]];  // Y coordinate (in mils/thou)
} [[format("format_point")]];

fn format_point(BRDPoint point) {
    return std::format("({}, {})", point.x, point.y);
};

//==============================================================================
// Encoded Header Detection
//==============================================================================

struct EncodedHeader {
    u8 magic[4] [[color("FF4444")]];  // Encoded signature: 0x23, 0xE2, 0x63, 0x28
    
    if (magic[0] == 0x23 && magic[1] == 0xE2 && magic[2] == 0x63 && magic[3] == 0x28) {
        std::print("File appears to be encoded - decoding required");
    }
} [[format("format_encoded_header")]];

fn format_encoded_header(EncodedHeader header) {
    if (header.magic[0] == 0x23 && header.magic[1] == 0xE2 && 
        header.magic[2] == 0x63 && header.magic[3] == 0x28) {
        return "ENCODED BRD";
    }
    return "PLAIN TEXT BRD";
};

//==============================================================================
// Text-based BRD Structure (most common format)
//==============================================================================

struct TextBRDHeader {
    char signature[8] [[color("FF4444")]];  // Usually "BRDOUT: " or similar
} [[format("format_text_header")]];

fn format_text_header(TextBRDHeader header) {
    return std::string::substr(header.signature, 0, 7);
};

//==============================================================================
// Section Markers (for text format)
//==============================================================================

enum SectionType : u8 {
    UNKNOWN = 0,
    STR_LENGTH = 1,
    VAR_DATA = 2, 
    FORMAT = 3,
    PARTS = 4,
    PINS = 5,
    NAILS = 6,
    OUTLINE = 7,
    NETS = 8
};

//==============================================================================
// Variable Data Section (contains counts)
//==============================================================================

struct VarDataSection {
    u32 num_format [[color("44FF44")]];   // Number of format/outline points
    u32 num_parts [[color("44FF44")]];    // Number of parts/components
    u32 num_pins [[color("44FF44")]];     // Number of pins/pads
    u32 num_nails [[color("44FF44")]];    // Number of test nails/points
} [[format("format_var_data")]];

fn format_var_data(VarDataSection data) {
    return std::format("Format:{} Parts:{} Pins:{} Nails:{}", 
                      data.num_format, data.num_parts, data.num_pins, data.num_nails);
};

//==============================================================================
// Part Information
//==============================================================================

enum PartType : u8 {
    THROUGH_HOLE = 0,
    SMD = 1
};

enum MountingSide : u8 {
    TOP = 1,
    BOTTOM = 2,
    BOTH = 3
};

struct BRDPart {
    char name[32] [[color("FFFF44")]];          // Part name/reference designator
    u32 type_and_layer [[color("44FF44")]];    // Encoded type and mounting side
    u32 end_of_pins [[color("4444FF")]];       // Index of last pin for this part
    BRDPoint p1 [[color("FF8844")]];           // First corner point
    BRDPoint p2 [[color("FF8844")]];           // Second corner point
} [[format("format_part")]];

fn format_part(BRDPart part) {
    str part_name = std::string::trim_null(part.name);
    bool is_smd = (part.type_and_layer & 0xC) != 0;
    str type = is_smd ? "SMD" : "TH";
    
    str side = "Top";
    if (part.type_and_layer == 2 || part.type_and_layer >= 8) {
        side = "Bottom";
    } else if (part.type_and_layer == 1 || (part.type_and_layer >= 4 && part.type_and_layer < 8)) {
        side = "Top";
    }
    
    return std::format("{} [{}] {} pins:{}", part_name, type, side, part.end_of_pins);
};

//==============================================================================
// Pin Information  
//==============================================================================

enum PinSide : u8 {
    PIN_TOP = 1,
    PIN_BOTTOM = 2,
    PIN_BOTH = 3
};

struct BRDPin {
    BRDPoint pos [[color("FF8844")]];          // Pin position
    s32 probe [[color("4444FF")]];             // Probe number (can be negative)
    u32 part_index [[color("44FF44")]];       // Index of parent part
    char net_name[64] [[color("FFFF44")]];    // Net name
    float radius [[color("FF4444")]];         // Pin radius
} [[format("format_pin")]];

fn format_pin(BRDPin pin) {
    str net = std::string::trim_null(pin.net_name);
    return std::format("Part:{} Net:{} Probe:{} R:{:.1f}", 
                      pin.part_index, net, pin.probe, pin.radius);
};

//==============================================================================
// Test Nail/Point Information
//==============================================================================

struct BRDNail {
    u32 probe [[color("4444FF")]];            // Probe number
    BRDPoint pos [[color("FF8844")]];         // Nail position  
    u32 side [[color("44FF44")]];             // Mounting side (1=top, 2=bottom)
    char net_name[64] [[color("FFFF44")]];   // Net name
} [[format("format_nail")]];

fn format_nail(BRDNail nail) {
    str net = std::string::trim_null(nail.net_name);
    str side = nail.side == 1 ? "Top" : "Bottom";
    return std::format("Probe:{} Net:{} Side:{}", nail.probe, net, side);
};

//==============================================================================
// Main BRD File Structure
//==============================================================================

struct BRDFile {
    // Check if file is encoded or plain text
    EncodedHeader header_check @ 0x00;
    
    // If it's a plain text file, it might start with text
    if (header_check.magic[0] != 0x23 || header_check.magic[1] != 0xE2 || 
        header_check.magic[2] != 0x63 || header_check.magic[3] != 0x28) {
        
        // Plain text format - show as text header
        TextBRDHeader text_header @ 0x00;
        
        // The rest would be parsed as text sections, but ImHex is better 
        // suited for binary formats. For text analysis, use text editor.
        char remaining_data[std::mem::size() - 8] @ 0x08 [[color("CCCCCC")]];
        
    } else {
        // Encoded binary format
        std::print("Detected encoded BRD format");
        
        // After decoding, the structure would typically contain:
        // 1. Variable data section with counts
        // 2. Format points (board outline)
        // 3. Parts data
        // 4. Pins data  
        // 5. Nails data
        
        // For demonstration, assuming decoded structure:
        VarDataSection var_data @ 0x04;
        
        // Format points (board outline)
        if (var_data.num_format > 0 && var_data.num_format < 1000) {
            BRDPoint format_points[var_data.num_format] @ $ [[color("FF8844")]];
        }
        
        // Parts data
        if (var_data.num_parts > 0 && var_data.num_parts < 10000) {
            BRDPart parts[var_data.num_parts] @ $ [[color("44FF44")]];
        }
        
        // Pins data
        if (var_data.num_pins > 0 && var_data.num_pins < 100000) {
            BRDPin pins[var_data.num_pins] @ $ [[color("4444FF")]];
        }
        
        // Nails data
        if (var_data.num_nails > 0 && var_data.num_nails < 10000) {
            BRDNail nails[var_data.num_nails] @ $ [[color("FFFF44")]];
        }
    }
} [[format("format_brd_file")]];

fn format_brd_file(BRDFile file) {
    return "BRD PCB Board File";
};

//==============================================================================
// Pattern Entry Point
//==============================================================================

BRDFile brd_file @ 0x00;

//==============================================================================
// Additional Information Display
//==============================================================================

std::print("=== BRD File Analysis ===");
std::print("File Format: PCB Board File");
std::print("Used by: PCB testing and manufacturing systems");
std::print("");
std::print("Format Detection:");

// Check if encoded
u8 first_bytes[4] @ 0x00;
if (first_bytes[0] == 0x23 && first_bytes[1] == 0xE2 && 
    first_bytes[2] == 0x63 && first_bytes[3] == 0x28) {
    std::print("✓ Encoded binary format detected");
    std::print("  Signature: 0x23 0xE2 0x63 0x28");
    std::print("  Note: File needs decoding before text parsing");
} else {
    std::print("✓ Plain text format detected");
    std::print("  Sections may include:");
    std::print("  - OUTLINE: Board outline coordinates");
    std::print("  - PARTS: Component definitions");
    std::print("  - PINS: Pin/pad locations");
    std::print("  - NETS: Net connectivity");
    std::print("  - NAILS: Test point locations");
}

std::print("");
std::print("=== Format Specifications ===");
std::print("Coordinates: Typically in mils (thousandths of an inch)");
std::print("Text Format: Space or tab delimited");
std::print("Comments: Lines starting with '#'");
std::print("Encoding: Optional XOR encoding with signature 0x23E26328");
std::print("");
std::print("=== Common Section Headers ===");
std::print("str_length: - Length information");
std::print("var_data: - Variable counts (format, parts, pins, nails)");
std::print("Format: - Board outline points");  
std::print("Parts: - Component information");
std::print("Pins: - Pin/pad locations and net assignments");
std::print("Nails: - Test point locations");
