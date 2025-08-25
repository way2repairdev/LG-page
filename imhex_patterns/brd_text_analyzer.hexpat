//==============================================================================
// ImHex Pattern for BRD File Format - Text Parser Version
// Board File Format used for PCB (Printed Circuit Board) data
// Optimized for text-based BRD files
//==============================================================================

#pragma author OpenAI Assistant  
#pragma description BRD file format pattern - optimized for text parsing
#pragma MIME text/plain

#include <std/io.pat>
#include <std/mem.pat>
#include <std/string.pat>

//==============================================================================
// Header Detection and File Type Analysis
//==============================================================================

struct BRDFileHeader {
    u8 first_four_bytes[4] [[color("FF4444")]];
    
    // Check for encoded format signature
    bool is_encoded = (first_four_bytes[0] == 0x23 && first_four_bytes[1] == 0xE2 && 
                      first_four_bytes[2] == 0x63 && first_four_bytes[3] == 0x28);
    
    if (is_encoded) {
        std::print("🔒 ENCODED BRD FILE DETECTED");
        std::print("Signature: 0x23 0xE2 0x63 0x28");
        std::print("This file requires decoding with XOR algorithm:");
        std::print("decoded_byte = ~(((byte >> 6) & 3) | (byte << 2))");
        std::print("(excluding \\r, \\n, and null bytes)");
    } else {
        std::print("📄 PLAIN TEXT BRD FILE DETECTED");
    }
} [[format("format_header")]];

fn format_header(BRDFileHeader header) {
    if (header.is_encoded) {
        return "ENCODED BRD FILE";
    } else {
        return "PLAIN TEXT BRD FILE";
    }
};

//==============================================================================
// Text Content Analysis
//==============================================================================

struct TextContent {
    char content[std::mem::size()] [[color("CCCCCC")]];
} [[format("format_text_content")]];

fn format_text_content(TextContent text) {
    return "Text Content - Use text editor for detailed analysis";
};

//==============================================================================
// Main File Structure
//==============================================================================

struct BRDFile {
    BRDFileHeader header @ 0x00;
    
    if (!header.is_encoded) {
        // For plain text files, display the content as text
        TextContent text_data @ 0x00;
    } else {
        // For encoded files, show the raw encoded data
        u8 encoded_data[std::mem::size()] @ 0x00 [[color("FF8888")]];
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
// File Analysis and Information
//==============================================================================

std::print("=== BRD FILE ANALYZER ===");
std::print("");

// Analyze file header
u8 header_bytes[4] @ 0x00;
bool is_encoded = (header_bytes[0] == 0x23 && header_bytes[1] == 0xE2 && 
                  header_bytes[2] == 0x63 && header_bytes[3] == 0x28);

if (is_encoded) {
    std::print("🔒 FILE TYPE: Encoded Binary BRD");
    std::print("📋 ENCODING: XOR with bit manipulation");
    std::print("🔧 DECODER: ~(((byte >> 6) & 3) | (byte << 2))");
    std::print("⚠️  NOTE: Carriage returns, line feeds, and nulls are preserved");
    std::print("");
    std::print("💡 TO VIEW DECODED CONTENT:");
    std::print("   1. Use the decoder algorithm above");
    std::print("   2. Or use the BRDFile::Load() function from the source code");
    std::print("   3. After decoding, file becomes plain text format");
} else {
    std::print("📄 FILE TYPE: Plain Text BRD");
    std::print("📋 FORMAT: Line-based with section headers");
    std::print("🔧 PARSING: Direct text processing");
    std::print("");
    std::print("📝 EXPECTED SECTIONS:");
    std::print("   • str_length: - String length information");
    std::print("   • var_data: - Counts (format parts pins nails)");
    std::print("   • Format: - Board outline coordinates");
    std::print("   • Parts: - Component definitions");
    std::print("   • Pins: - Pin locations and assignments");
    std::print("   • Nails: - Test point definitions");
    std::print("");
    
    // Try to detect common section headers in the file
    char file_content[std::mem::size()] @ 0x00;
    str content_str = file_content;
    
    std::print("🔍 SECTION DETECTION:");
    
    // Check for common sections
    if (std::string::contains(content_str, "str_length:")) {
        std::print("   ✓ str_length section found");
    }
    if (std::string::contains(content_str, "var_data:")) {
        std::print("   ✓ var_data section found");
    }
    if (std::string::contains(content_str, "Format:") || std::string::contains(content_str, "format:")) {
        std::print("   ✓ Format section found");
    }
    if (std::string::contains(content_str, "OUTLINE:")) {
        std::print("   ✓ OUTLINE section found");
    }
    if (std::string::contains(content_str, "Parts:") || std::string::contains(content_str, "Pins1:")) {
        std::print("   ✓ Parts section found");
    }
    if (std::string::contains(content_str, "Pins:") || std::string::contains(content_str, "Pins2:")) {
        std::print("   ✓ Pins section found");
    }
    if (std::string::contains(content_str, "Nails:")) {
        std::print("   ✓ Nails section found");
    }
    if (std::string::contains(content_str, "NETS:")) {
        std::print("   ✓ NETS section found");
    }
    if (std::string::contains(content_str, "BRDOUT:")) {
        std::print("   ✓ BRDOUT header found");
    }
}

std::print("");
std::print("=== BRD FORMAT SPECIFICATIONS ===");
std::print("📏 COORDINATES: Usually in mils (1/1000 inch)");
std::print("📝 DELIMITERS: Spaces, tabs, or specific characters");
std::print("💬 COMMENTS: Lines starting with '#'");
std::print("🏷️  STRUCTURE: Section-based with headers");
std::print("");
std::print("=== EXAMPLE SECTIONS ===");
std::print("");
std::print("var_data:");
std::print("4 2 8 2");
std::print("  ↑ ↑ ↑ ↑");
std::print("  │ │ │ └─ Number of nails/test points");
std::print("  │ │ └─── Number of pins");
std::print("  │ └───── Number of parts");
std::print("  └─────── Number of format points");
std::print("");
std::print("Format: (Board Outline)");
std::print("0 0");
std::print("1000 0"); 
std::print("1000 800");
std::print("0 800");
std::print("");
std::print("Parts:");
std::print("U1 1 8    ← name type end_of_pins");
std::print("R1 1 10");
std::print("");
std::print("Pins:");
std::print("500 400 1 1 VCC    ← x y probe part net");
std::print("600 400 2 1 GND");
std::print("");
std::print("Nails:");
std::print("1 100 100 1 VCC    ← probe x y side net");
std::print("2 900 100 1 GND");

std::print("");
std::print("=== COORDINATE SYSTEM ===");
std::print("📐 ORIGIN: Typically bottom-left corner");
std::print("📏 UNITS: Mils (thousandths of an inch)");
std::print("🔄 Y-AXIS: May be mirrored for bottom side");
std::print("🎯 PRECISION: Integer coordinates common");

std::print("");
std::print("=== USAGE CONTEXT ===");
std::print("🏭 MANUFACTURING: PCB test fixture generation");
std::print("🔍 TESTING: In-circuit test (ICT) programming");
std::print("📊 ANALYSIS: Board layout verification");
std::print("🔧 TOOLS: CAD export, test equipment import");
