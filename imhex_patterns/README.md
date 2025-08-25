# BRD File Format - ImHex Pattern Documentation

## Overview

This directory contains ImHex pattern files for analyzing BRD (Board) files used in PCB (Printed Circuit Board) manufacturing and testing workflows.

## Pattern Files

### 1. `brd_format.hexpat`
**Primary binary-focused pattern for BRD files**

- Handles both encoded and plain text BRD formats
- Provides structured parsing for binary/encoded variants
- Includes data structure definitions for parts, pins, nails, and format points
- Best for: Binary encoded BRD files with the signature `0x23 0xE2 0x63 0x28`

### 2. `brd_text_analyzer.hexpat` 
**Text-focused analyzer and format detector**

- Optimized for plain text BRD file analysis
- Automatically detects encoding vs plain text format
- Provides section detection and format validation
- Includes comprehensive format documentation
- Best for: Understanding BRD file structure and plain text variants

## BRD File Format Specification

### File Types

1. **Plain Text BRD**: Human-readable text format with section headers
2. **Encoded BRD**: XOR-encoded binary format with signature `23 E2 63 28`

### Encoding Algorithm

For encoded files, each byte (except `\r`, `\n`, and null) is decoded using:
```cpp
decoded_byte = ~(((encoded_byte >> 6) & 3) | (encoded_byte << 2))
```

### File Structure

BRD files are organized into sections:

#### 1. Variable Data Section (`var_data:`)
Contains counts for each data type:
```
var_data:
<num_format> <num_parts> <num_pins> <num_nails>
```

#### 2. Format Section (`Format:` or `OUTLINE:`)
Board outline coordinates:
```
Format:
<x1> <y1>
<x2> <y2>
...
```

#### 3. Parts Section (`Parts:`)
Component definitions:
```
Parts:
<name> <type_and_layer> <end_of_pins>
```

#### 4. Pins Section (`Pins:`)
Pin/pad locations and net assignments:
```
Pins:
<x> <y> <probe> <part_index> <net_name>
```

#### 5. Nails Section (`Nails:`)
Test point definitions:
```
Nails:
<probe> <x> <y> <side> <net_name>
```

### Data Types

#### Coordinates
- **Units**: Mils (thousandths of an inch)
- **Type**: 32-bit signed integers
- **Origin**: Typically bottom-left corner

#### Parts
- **Type encoding**: Bit field indicating SMD vs Through-hole and mounting side
  - `(type & 0xC) != 0`: SMD component
  - `type == 1` or `4 <= type < 8`: Top side
  - `type == 2` or `type >= 8`: Bottom side

#### Pins
- **Probe**: Test probe number (can be negative, -99 for no probe)
- **Part**: 1-based index into parts array
- **Net**: String identifier for electrical net

#### Nails (Test Points)
- **Probe**: Test probe number
- **Side**: 1 = Top, 2 = Bottom
- **Net**: Associated electrical net name

## Usage Instructions

### Using with ImHex

1. **Open your BRD file** in ImHex
2. **Load the appropriate pattern**:
   - For binary analysis: Use `brd_format.hexpat`
   - For format detection/text analysis: Use `brd_text_analyzer.hexpat`
3. **Apply the pattern** from the Pattern menu
4. **View the analysis** in the console output and pattern view

### Pattern Selection Guide

| File Type | Recommended Pattern | Purpose |
|-----------|-------------------|---------|
| Encoded BRD | `brd_format.hexpat` | Binary structure analysis |
| Plain Text BRD | `brd_text_analyzer.hexpat` | Format validation & documentation |
| Unknown BRD | `brd_text_analyzer.hexpat` | Format detection first |

## Example Files

The repository includes example BRD files for testing:

- `test.brd`: Simple plain text format example
- Various encoded files in the test suite

## Integration with Source Code

These patterns are designed to complement the BRD file parser implementation in:

- `src/viewers/pcb/format/BRDFile.cpp`: Main parser implementation
- `src/viewers/pcb/format/BRDFile.h`: Class definition
- `src/viewers/pcb/core/BRDTypes.h`: Data structure definitions

## Limitations

1. **Text Parsing**: ImHex patterns are optimized for binary data; complex text parsing is limited
2. **Encoded Files**: Require external decoding before full analysis
3. **Variable Sections**: Some BRD variants may have different section ordering

## Contributing

When modifying these patterns:

1. Test with both encoded and plain text BRD files
2. Ensure compatibility with the source code implementation
3. Update documentation for any new sections or data types
4. Validate coordinate system assumptions

## References

- BRD format implementation in `BRDFile.cpp`
- PCB testing industry standards
- ImHex pattern language documentation
- CAD export format specifications
