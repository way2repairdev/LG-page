# Viewers Module Structure

This folder contains all viewer implementations for different file types. The structure is organized to support multiple viewer types while maintaining clean separation.

## Structure Overview

```
src/viewers/
├── pdf/                    # PDF Viewer Implementation
│   ├── core/               # Core PDF viewer logic
│   │   └── PDFViewerEmbedder.cpp
│   ├── ui/                 # UI components for PDF viewing
│   │   ├── pdfviewerwidget.cpp
│   │   └── QtPDFViewerWidget.cpp
│   ├── rendering/          # Rendering pipeline components
│   │   └── OpenGLPipelineManager.cpp
│   ├── third_party/       # Third-party libraries (PDFium, GLEW, GLFW)
│   └── examples/           # Example implementations
├── pcb/                    # PCB Viewer Implementation (Future)
│   ├── core/               # Core PCB viewer logic
│   ├── ui/                 # UI components for PCB viewing
│   ├── rendering/          # Rendering pipeline for PCB
│   └── third_party/       # PCB-specific libraries
└── README.md              # This file

include/viewers/
├── pdf/                    # PDF viewer headers
│   ├── PDFViewerEmbedder.h
│   ├── pdfviewerwidget.h
│   ├── QtPDFViewerWidget.h
│   └── OpenGLPipelineManager.h
└── pcb/                    # PCB viewer headers (Future)
    └── [PCB viewer headers]
```

## PDF Viewer Components

### Core Components
- **PDFViewerEmbedder**: Main PDF viewer implementation that embeds OpenGL/PDFium rendering
- **OpenGLPipelineManager**: Manages different OpenGL rendering pipelines for compatibility

### UI Components
- **PDFViewerWidget**: Qt widget wrapper for the PDF viewer
- **QtPDFViewerWidget**: Alternative Qt-based PDF viewer using Qt's PDF module

### Third-Party Libraries
- **PDFium**: Google's PDF rendering library
- **GLEW**: OpenGL Extension Wrangler Library
- **GLFW**: OpenGL window and input management library

## Benefits of This Structure

1. **Modularity**: Each viewer type is self-contained
2. **Extensibility**: Easy to add new viewer types (PCB, image, etc.)
3. **Clean Separation**: Clear boundaries between different concerns
4. **Maintainability**: Easier to maintain and debug specific viewer types
5. **Reusability**: Components can be reused across different viewers

## Adding a New Viewer

To add a new viewer type (e.g., PCB viewer):

1. Create the folder structure under `src/viewers/[type]/`
2. Create corresponding header folder under `include/viewers/[type]/`
3. Update CMakeLists.txt to include the new sources
4. Follow the same pattern as the PDF viewer for consistency

## Migration Notes

Files have been moved from their original locations:
- `src/ui/PDFViewerEmbedder.*` → `src/viewers/pdf/core/` and `include/viewers/pdf/`
- `src/ui/pdfviewerwidget.*` → `src/viewers/pdf/ui/` and `include/viewers/pdf/`
- `src/ui/OpenGLPipelineManager.*` → `src/viewers/pdf/rendering/` and `include/viewers/pdf/`
- `src/pdf/` → `src/viewers/pdf/third_party/`

All include paths and CMakeLists.txt have been updated accordingly.
