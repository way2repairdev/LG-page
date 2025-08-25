//==============================================================================
// ImHex Pattern for Decrypted BRD Files (Plain Text Format)
// Analyzes plain text BRD files after decryption
// Based on the BRD file structure from BRDFile.cpp
//==============================================================================

#pragma author OpenAI Assistant
#pragma description Decrypted BRD file analyzer for plain text format
#pragma MIME text/plain

#include <std/io.pat>
#include <std/mem.pat>
#include <std/string.pat>

// Color definitions for visualization
#define COLOR_HEADER     0xFF4444
#define COLOR_SECTION    0x44FF44
#define COLOR_DATA       0x4444FF
#define COLOR_COORDS     0xFF8844
#define COLOR_NUMBERS    0xFFFF44
#define COLOR_STRINGS    0xFF44FF

//==============================================================================
// File Structure Analysis
//==============================================================================

struct BRDTextFile {
    char content[std::mem::size()] [[color("CCCCCC")]];
} [[format("format_brd_text")]];

fn format_brd_text(BRDTextFile file) {
    return "Plain Text BRD File";
};

//==============================================================================
// Content Analysis Functions
//==============================================================================

fn analyze_brd_content() {
    // Read file content as string for analysis
    char file_content[std::mem::size()] @ 0x00;
    str content = file_content;
    
    std::print("=== DECRYPTED BRD FILE ANALYSIS ===");
    std::print("");
    
    // File size and basic info
    std::print("📄 FILE INFORMATION:");
    std::print(std::format("   Size: {} bytes", std::mem::size()));
    
    // Count lines - simplified approach
    std::print("");
    std::print("🔍 BRD FORMAT DETECTION:");
    
    bool has_var_data = std::string::contains(content, "var_data:");
    bool has_format = std::string::contains(content, "Format:") || std::string::contains(content, "format:");
    bool has_parts = std::string::contains(content, "Parts:") || std::string::contains(content, "Pins1:");
    bool has_pins = std::string::contains(content, "Pins:") || std::string::contains(content, "Pins2:");
    bool has_nails = std::string::contains(content, "Nails:");
    bool has_outline = std::string::contains(content, "OUTLINE:");
    bool has_nets = std::string::contains(content, "NETS:");
    bool has_brdout = std::string::contains(content, "BRDOUT:");
    bool has_str_length = std::string::contains(content, "str_length:");
    
    u32 section_count = 0;
    
    if (has_var_data) {
        std::print("   ✓ var_data section found");
        section_count = section_count + 1;
    }
    if (has_format) {
        std::print("   ✓ Format section found");
        section_count = section_count + 1;
    }
    if (has_parts) {
        std::print("   ✓ Parts section found");
        section_count = section_count + 1;
    }
    if (has_pins) {
        std::print("   ✓ Pins section found");
        section_count = section_count + 1;
    }
    if (has_nails) {
        std::print("   ✓ Nails section found");
        section_count = section_count + 1;
    }
    if (has_outline) {
        std::print("   ✓ OUTLINE section found");
        section_count = section_count + 1;
    }
    if (has_nets) {
        std::print("   ✓ NETS section found");
        section_count = section_count + 1;
    }
    if (has_brdout) {
        std::print("   ✓ BRDOUT header found");
        section_count = section_count + 1;
    }
    if (has_str_length) {
        std::print("   ✓ str_length section found");
        section_count = section_count + 1;
    }
    
    if (section_count == 0) {
        std::print("   ❌ No BRD sections detected");
        std::print("   💡 File may not be BRD format or decryption failed");
    } else {
        std::print(std::format("   📊 Total BRD sections: {}", section_count));
    }
    
    // Estimate format confidence
    float confidence = 0.0;
    if (has_var_data) confidence = confidence + 20.0;
    if (has_format || has_outline) confidence = confidence + 15.0;
    if (has_parts) confidence = confidence + 15.0;
    if (has_pins) confidence = confidence + 15.0;
    if (has_nails) confidence = confidence + 10.0;
    if (has_nets) confidence = confidence + 10.0;
    if (has_brdout) confidence = confidence + 15.0;
    
    std::print("");
    std::print("📈 FORMAT CONFIDENCE:");
    std::print(std::format("   Score: {:.0f}%", confidence));
    
    if (confidence >= 80.0) {
        std::print("   Status: HIGH - Valid BRD file");
    } else if (confidence >= 50.0) {
        std::print("   Status: MEDIUM - Likely BRD file");
    } else if (confidence >= 20.0) {
        std::print("   Status: LOW - Possible BRD file");
    } else {
        std::print("   Status: VERY LOW - Unlikely to be BRD file");
    }
    
    // Character analysis - simplified
    std::print("");
    std::print("📊 CHARACTER ANALYSIS:");
    std::print("   File analyzed for BRD format markers");
    std::print("   Use text editor for detailed character inspection");
    
    // Data extraction hints
    std::print("");
    std::print("🔧 DATA EXTRACTION HINTS:");
    
    if (has_var_data) {
        std::print("   • Look for 'var_data:' followed by 4 numbers");
        std::print("     Format: <format_count> <parts_count> <pins_count> <nails_count>");
    }
    
    if (has_format) {
        std::print("   • 'Format:' section contains board outline coordinates");
        std::print("     Format: <x> <y> pairs defining board boundary");
    }
    
    if (has_parts) {
        std::print("   • 'Parts:' section contains component definitions");
        std::print("     Format: <name> <type_layer> <end_of_pins>");
    }
    
    if (has_pins) {
        std::print("   • 'Pins:' section contains pin locations and nets");
        std::print("     Format: <x> <y> <probe> <part> <net_name>");
    }
    
    if (has_nails) {
        std::print("   • 'Nails:' section contains test point definitions");
        std::print("     Format: <probe> <x> <y> <side> <net_name>");
    }
    
    // Coordinate system info
    std::print("");
    std::print("📐 COORDINATE SYSTEM:");
    std::print("   • Units: Typically mils (1/1000 inch)");
    std::print("   • Origin: Usually bottom-left corner");
    std::print("   • Y-axis: May be mirrored for bottom side components");
    std::print("   • Values: Integer coordinates are common");
    
    // Common part types
    std::print("");
    std::print("🔢 PART TYPE ENCODING:");
    std::print("   • type & 0xC != 0: SMD component");
    std::print("   • type & 0xC == 0: Through-hole component");
    std::print("   • type == 1 or (4 <= type < 8): Top side");
    std::print("   • type == 2 or (type >= 8): Bottom side");
    
    // File format notes
    std::print("");
    std::print("📝 FORMAT NOTES:");
    std::print("   • Sections are delimited by headers ending with ':'");
    std::print("   • Data is space or tab separated");
    std::print("   • Comments start with '#'");
    std::print("   • Empty lines are ignored");
    std::print("   • Case sensitivity varies by section");
    
    std::print("");
    std::print("🛠️ RECOMMENDED TOOLS:");
    std::print("   • Text editor for manual inspection");
    std::print("   • CSV tools for tabular data sections");
    std::print("   • CAD viewers for coordinate visualization");
    std::print("   • Custom parsers for automated processing");
};

//==============================================================================
// Pattern Entry Point
//==============================================================================

BRDTextFile brd_text @ 0x00;

// Perform content analysis
analyze_brd_content();

//==============================================================================
// Usage Instructions
//==============================================================================

std::print("");
std::print("=== USAGE INSTRUCTIONS ===");
std::print("");
std::print("This pattern analyzes decrypted BRD files in plain text format.");
std::print("Use this AFTER decrypting an encoded BRD file.");
std::print("");
std::print("DECRYPTION WORKFLOW:");
std::print("1. Use brd_format.hexpat to identify encoded files");
std::print("2. Decrypt using Python scripts or ImHex data processor");
std::print("3. Use this pattern to analyze the decrypted content");
std::print("");
std::print("MANUAL INSPECTION:");
std::print("• Open the file in a text editor to see the raw content");
std::print("• Look for section headers like 'var_data:', 'Format:', etc.");
std::print("• Coordinate data follows each section header");
std::print("• Numbers are typically space-separated");
std::print("");
std::print("PARSING TIPS:");
std::print("• Split file into sections by headers");
std::print("• Parse each section according to its format");
std::print("• Handle both space and tab delimiters");
std::print("• Skip empty lines and comments (#)");
std::print("• Validate coordinate ranges and counts");

std::print("");
std::print("=== SECTION FORMAT REFERENCE ===");
std::print("");
std::print("var_data:");
std::print("<format_points> <parts_count> <pins_count> <nails_count>");
std::print("");
std::print("Format: (or OUTLINE:)");
std::print("<x1> <y1>");
std::print("<x2> <y2>");
std::print("...");
std::print("");
std::print("Parts:");
std::print("<name> <type_and_layer> <end_of_pins>");
std::print("...");
std::print("");
std::print("Pins:");
std::print("<x> <y> <probe> <part_index> <net_name>");
std::print("...");
std::print("");
std::print("Nails:");
std::print("<probe> <x> <y> <side> <net_name>");
std::print("...");
