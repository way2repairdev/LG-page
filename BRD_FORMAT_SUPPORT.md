# BRD File Format Support

This update adds support for two additional PCB file formats from the OpenBoardView project:

## Supported Formats

### 1. BRD Format (.brd)
- Based on OpenBoardView's BRDFile.cpp implementation
- Supports encoded and unencoded BRD files
- Format structure:
  - str_length section
  - var_data section (contains counts)
  - Format section (board outline points)
  - Parts section
  - Pins section  
  - Nails section (test points)

### 2. BRD2 Format (.brd2)
- Based on OpenBoardView's BRD2File.cpp implementation
- Simpler format with named sections
- Format structure:
  - BRDOUT: section (board outline and dimensions)
  - NETS: section (net definitions)
  - PARTS: section (component definitions)
  - PINS: section (pin definitions)
  - NAILS: section (test point definitions)

### 3. XZZPCB Format (.xzz, .pcb, .xzzpcb)
- Original format support maintained
- Supports encrypted and compressed files

## Usage

### File Dialog
The file dialog now includes filters for all supported formats:
- PCB Files (*.xzzpcb;*.pcb;*.xzz;*.brd;*.brd2)
- Individual format filters for each type

### Programmatic Loading
```cpp
// Auto-detect format and load
std::string filepath = "board.brd";
std::string ext = Utils::ToLower(Utils::GetFileExtension(filepath));

if (ext == "brd") {
    auto brd = BRDFile::LoadFromFile(filepath);
    pcb_data = std::shared_ptr<BRDFileBase>(brd.release());
} else if (ext == "brd2") {
    auto brd2 = BRD2File::LoadFromFile(filepath);
    pcb_data = std::shared_ptr<BRDFileBase>(brd2.release());
}
```

## Implementation Details

### File Structure
```
src/viewers/pcb/format/
├── BRDFileBase.h/.cpp    - Base class for all formats
├── XZZPCBFile.h/.cpp     - Original XZZPCB format
├── BRDFile.h/.cpp        - BRD format parser (NEW)
├── BRD2File.h/.cpp       - BRD2 format parser (NEW)
└── des.h/.cpp            - DES encryption support
```

### Key Features
- Polymorphic design using BRDFileBase inheritance
- Format auto-detection through VerifyFormat() methods
- Consistent data model across all formats
- Memory management with proper cleanup
- Error handling and validation

### Data Mapping
All formats are converted to the common BRDFileBase data model:
- **Parts**: Component definitions with position and type
- **Pins**: Pin/pad definitions with position, net, and part association
- **Nails**: Test point definitions
- **Format**: Board outline points
- **Outline segments**: Rendered board outline

### Format Detection
Each format implements a `VerifyFormat()` method:
- **BRD**: Looks for signature bytes or "str_length:" and "var_data:" strings
- **BRD2**: Looks for "BRDOUT:" and "NETS:" section headers
- **XZZPCB**: Checks for XZZ signature or specific byte patterns

## Building

The new formats are automatically included in the CMake build:
```cmake
set(PCB_SOURCES
    # ... existing sources ...
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRDFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRD2File.cpp
)
```

## Testing

The PCB viewer now displays format information in the console:
```
PCB Viewer - Multi-Format Support (XZZPCB, BRD, BRD2)
```

A test program is available at `test_brd_formats.cpp` to verify format detection.

## Credits

This implementation is based on the open-source OpenBoardView project:
- Repository: https://github.com/OpenBoardView/OpenBoardView
- Original authors: The OpenBoardView team
- License: MIT License

The parsers have been adapted to work with the existing PCB viewer architecture while maintaining compatibility with the original file formats.
