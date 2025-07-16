# Qt PDF Viewer Integration Guide

This guide shows how to integrate your high-performance standalone PDF viewer into Qt applications using the provided `PDFViewerEmbedder` and `PDFViewerWidget` classes.

## Overview

The integration preserves all features of your standalone PDF viewer:
- **High-performance rendering** with OpenGL and PDFium
- **Smooth zooming and panning** with cursor-based zoom
- **Text selection and search** with highlighting
- **Background rendering** for optimal performance
- **Native scrollbars** and navigation
- **All keyboard/mouse interactions**

## Files Created

### Core Integration Files
- `src/ui/PDFViewerEmbedder.h/.cpp` - Wraps your standalone viewer for embedding
- `src/ui/PDFViewerWidget.h/.cpp` - Qt widget interface with toolbar controls
- `include/ui/pdfviewerwidget.h` - Header for the main PDFViewerWidget class

### Alternative Implementation
- `src/ui/QtPDFViewerWidget.h/.cpp` - Enhanced version with more UI controls

## Integration Steps

### 1. Basic Integration in Existing Qt Tab

```cpp
#include "ui/pdfviewerwidget.h"

void MainApplication::openPDFInTab(const QString &filePath)
{
    // Create PDF viewer widget
    PDFViewerWidget *pdfViewer = new PDFViewerWidget();
    
    // Connect signals
    connect(pdfViewer, &PDFViewerWidget::pdfLoaded, this, [this](const QString &path) {
        statusBar()->showMessage("PDF loaded: " + QFileInfo(path).fileName());
    });
    
    connect(pdfViewer, &PDFViewerWidget::errorOccurred, this, [this](const QString &error) {
        QMessageBox::warning(this, "PDF Error", error);
    });
    
    connect(pdfViewer, &PDFViewerWidget::pageChanged, this, [this](int page, int total) {
        statusBar()->showMessage(QString("Page %1 of %2").arg(page).arg(total));
    });
    
    // Add to tab widget
    QString tabName = QFileInfo(filePath).fileName();
    int tabIndex = m_tabWidget->addTab(pdfViewer, tabName);
    m_tabWidget->setCurrentIndex(tabIndex);
    
    // Load PDF (with delay to ensure widget is ready)
    QTimer::singleShot(100, [pdfViewer, filePath]() {
        pdfViewer->loadPDF(filePath);
    });
}
```

### 2. Standalone PDF Viewer Window

```cpp
#include "ui/pdfviewerwidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    PDFViewerWidget viewer;
    viewer.resize(1200, 800);
    viewer.show();
    
    if (argc > 1) {
        viewer.loadPDF(QString(argv[1]));
    }
    
    return app.exec();
}
```

### 3. Custom Integration

```cpp
// Create viewer without toolbar for custom UI
PDFViewerWidget *viewer = new PDFViewerWidget();
viewer->toggleControls(false); // Hide built-in toolbar

// Create custom controls
QPushButton *zoomInBtn = new QPushButton("Zoom In");
connect(zoomInBtn, &QPushButton::clicked, viewer, &PDFViewerWidget::zoomIn);

QPushButton *zoomOutBtn = new QPushButton("Zoom Out");
connect(zoomOutBtn, &QPushButton::clicked, viewer, &PDFViewerWidget::zoomOut);

// Layout with custom controls
QVBoxLayout *layout = new QVBoxLayout();
QHBoxLayout *controlsLayout = new QHBoxLayout();
controlsLayout->addWidget(zoomInBtn);
controlsLayout->addWidget(zoomOutBtn);

layout->addLayout(controlsLayout);
layout->addWidget(viewer);
```

## Key Features

### High-Performance Rendering
- Embeds your native OpenGL viewer as a child window
- Maintains ~60 FPS rendering with QTimer updates
- Background rendering for smooth scrolling
- Hardware-accelerated PDFium rendering

### Full Feature Preservation
- All zoom and pan functionality
- Text selection with drag and double-click
- Search with F3/Shift+F3 navigation
- Scroll bar support with dragging
- Keyboard shortcuts (Ctrl+C, Escape, etc.)

### Qt Integration Benefits
- Native Qt signals/slots for event handling
- Proper focus and resize behavior
- Integration with Qt layouts and styles
- Tab-based interface support

## Build Requirements

### Dependencies
- **Qt 5.15+** with Widgets module
- **OpenGL** (already required for your viewer)
- **GLFW** (your existing dependency)
- **GLEW** (your existing dependency)
- **PDFium** (your existing dependency)

### CMake Integration
Add to your CMakeLists.txt:

```cmake
# Qt components
find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

# PDF Viewer Integration files
set(PDF_INTEGRATION_SOURCES
    src/ui/PDFViewerEmbedder.cpp
    src/ui/pdfviewerwidget.cpp
)

set(PDF_INTEGRATION_HEADERS
    src/ui/PDFViewerEmbedder.h
    include/ui/pdfviewerwidget.h
)

# Add to your executable
target_sources(your_target PRIVATE
    ${PDF_INTEGRATION_SOURCES}
    ${PDF_INTEGRATION_HEADERS}
)

# Link Qt
target_link_libraries(your_target PRIVATE
    Qt5::Core
    Qt5::Widgets
    # Your existing libraries (GLFW, GLEW, PDFium, etc.)
)
```

## Usage in Your Current Application

Your `MainApplication::openPDFInTab()` method is already set up correctly. The integration:

1. **Creates PDFViewerWidget** instead of a basic text viewer
2. **Connects all necessary signals** for status updates
3. **Handles loading with proper timing** (100ms delay for initialization)
4. **Provides full PDF viewing capabilities** within the tab

## Troubleshooting

### Common Issues

1. **OpenGL Context Errors**
   - Ensure Qt application has OpenGL support
   - Delay PDF loading until widget is shown

2. **Window Embedding Issues**
   - Widget must be shown before initializing embedded viewer
   - Parent window handle must be valid

3. **Performance Issues**
   - Update timer runs at 60 FPS (16ms intervals)
   - Background rendering handles non-visible pages

### Debug Output
Enable debug output to monitor initialization:
```cpp
QLoggingCategory::setFilterRules("*.debug=true");
```

The integration provides seamless embedding of your high-performance PDF viewer while maintaining all advanced features and adding Qt-native UI controls.
