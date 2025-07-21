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

## Supported File Formats (Planned)

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

🔴 **Not Started** - This is a placeholder for future development

## Next Steps

1. Research PCB file format specifications
2. Evaluate existing open-source PCB libraries
3. Design the PCB document data model
4. Implement basic file parsing
5. Create rendering pipeline
6. Build UI components

## Dependencies

- OpenGL (shared with PDF viewer)
- Qt (shared with main application)
- PCB format parsing libraries (to be determined)
- 3D rendering libraries (for 3D view)
