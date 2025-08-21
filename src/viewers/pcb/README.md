# PCB Viewer Module

This module will contain the PCB (Printed Circuit Board) viewer implementation.

## Planned Structure

```
pcb/
├── core/               # Core PCB viewing logic
│   ├── PCBViewerEmbedder.h/.cpp      # Main PCB viewer embedder
│   ├── PCBDocument.h/.cpp            # PCB document representation
│   └── PCBParser.h/.cpp              # Parser for various PCB formats
├── ui/                 # UI components for PCB viewing
│   ├── PCBViewerWidget.h/.cpp        # Qt widget wrapper
│   ├── PCBLayerManager.h/.cpp        # Layer visibility management
│   └── PCBToolbar.h/.cpp             # PCB-specific toolbar
├── rendering/          # Rendering pipeline for PCB
│   ├── PCBRenderer.h/.cpp            # PCB-specific renderer
│   ├── LayerRenderer.h/.cpp          # Individual layer rendering
│   └── ComponentRenderer.h/.cpp      # Component visualization
└── third_party/       # PCB-specific libraries
    ├── kicad_parser/   # KiCad file format support
    ├── eagle_parser/   # Eagle file format support
    └── gerber_parser/  # Gerber file format support
```

## Supported File Formats

### Currently Implemented ✅
- **XZZPCB**: `.xzz`, `.xzzpcb`, `.pcb` - Original format with encryption support
- **BRD**: `.brd` - OpenBoardView BRD format with encoded/unencoded support  
- **BRD2**: `.brd2` - OpenBoardView BRD2 format with named sections

### Planned for Future
- **KiCad**: `.kicad_pcb`, `.kicad_sch`
- **Eagle**: `.brd`, `.sch`
- **Gerber**: `.gbr`, `.gbl`, `.gtl`, etc.
- **Excellon**: `.drl`, `.exc` (drill files)
- **Pick and Place**: `.pos`, `.csv`

## Key Features (Planned)

1. **Multi-layer Visualization**: Show/hide individual PCB layers
2. **Component Information**: Click on components to view details
3. **Measurement Tools**: Measure distances and angles
4. **3D Visualization**: 3D view of the PCB
5. **DRC Visualization**: Design Rule Check results display
6. **Net Highlighting**: Highlight electrical nets

## Integration with Main Application

The PCB viewer will follow the same pattern as the PDF viewer:
- Embedded within Qt tabs
- Reuses the same OpenGL pipeline infrastructure
- Consistent UI/UX with other viewers

## Implementation Status

� **Fully Implemented** - Multi-format PCB viewer with OpenGL rendering

### Completed Features
- ✅ Multiple file format support (XZZPCB, BRD, BRD2)
- ✅ OpenGL-based rendering with ImGui
- ✅ Interactive navigation (pan, zoom, rotate)
- ✅ Component and pin visualization
- ✅ Net highlighting and selection
- ✅ Board outline rendering
- ✅ Test point visualization
- ✅ Format auto-detection
- ✅ Qt integration via embedder

### Recent Updates (BRD Format Support)
- ✅ Added BRD format parser based on OpenBoardView
- ✅ Added BRD2 format parser based on OpenBoardView  
- ✅ Updated file dialogs to include new formats
- ✅ Enhanced PCBViewerEmbedder for multi-format loading

## Dependencies

- OpenGL (shared with PDF viewer)
- Qt (shared with main application)
- PCB format parsing libraries (to be determined)
- 3D rendering libraries (for 3D view)
