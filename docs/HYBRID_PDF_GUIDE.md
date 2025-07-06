# Hybrid PDF Viewer Configuration

## 🎯 Overview
This configuration enables the use of both Qt's native PDF viewer and your custom OpenGL-based PDF viewer in a single application.

## 🔧 Build Requirements

### Required Qt Modules
- **Qt::Core** - Core Qt functionality
- **Qt::Widgets** - UI widgets
- **Qt::OpenGL** - OpenGL integration
- **Qt::OpenGLWidgets** - OpenGL widget support
- **Qt::Pdf** - Qt's PDF document support
- **Qt::PdfWidgets** - Qt's PDF viewer widgets

### External Dependencies
- **PDFium** - Google's PDF rendering engine
- **GLEW** - OpenGL Extension Wrangler
- **OpenGL** - Hardware-accelerated graphics

## 🚀 Features Comparison

### Qt Native PDF Viewer
```cpp
// Simple Qt PDF usage
QPdfDocument *document = new QPdfDocument();
QPdfView *view = new QPdfView();
view->setDocument(document);
document->load("document.pdf");
```

**Pros:**
- ✅ Easy to implement
- ✅ Native Qt integration
- ✅ Automatic theming
- ✅ Standard Qt patterns

**Cons:**
- ❌ Limited customization
- ❌ Basic zoom/pan
- ❌ Standard performance
- ❌ Limited search features

### Custom OpenGL PDF Viewer
```cpp
// Advanced OpenGL PDF usage
PDFViewerWidget *viewer = new PDFViewerWidget();
viewer->loadPDF("document.pdf");
viewer->setZoomLevel(1.5);
viewer->performCursorBasedZoom(cursorPos, true);
```

**Pros:**
- ✅ Hardware acceleration
- ✅ Advanced UI features
- ✅ Cursor-based zooming
- ✅ Background rendering
- ✅ Professional search
- ✅ Smooth animations

**Cons:**
- ❌ Complex implementation
- ❌ Requires OpenGL
- ❌ Custom maintenance
- ❌ Higher memory usage

## 🔄 Hybrid Approach Benefits

### Best of Both Worlds
```cpp
HybridPDFViewer *hybridViewer = new HybridPDFViewer();
hybridViewer->loadPDF("document.pdf");

// Switch between viewers dynamically
hybridViewer->setViewerMode(HybridPDFViewer::QtNativeViewer);      // Simple
hybridViewer->setViewerMode(HybridPDFViewer::CustomOpenGLViewer);  // Advanced
```

### Use Cases

#### Qt Native Viewer - Best For:
- 📄 Simple document viewing
- 🎨 Consistent UI theming
- 🏃 Quick prototyping
- 💻 Low-resource devices
- 👥 Standard user expectations

#### Custom OpenGL Viewer - Best For:
- 🎮 High-performance applications
- 🔍 Advanced search features
- 🎯 Professional document workflows
- 📊 Large document handling
- 🎨 Custom UI requirements

## 🛠️ Implementation Guide

### 1. Basic Setup
```cpp
// In your main window
HybridPDFViewer *pdfViewer = new HybridPDFViewer(this);
setCentralWidget(pdfViewer);

// Connect signals
connect(pdfViewer, &HybridPDFViewer::pdfLoaded, this, &MainWindow::onPdfLoaded);
connect(pdfViewer, &HybridPDFViewer::viewerModeChanged, this, &MainWindow::onModeChanged);
```

### 2. Loading Documents
```cpp
// Load PDF in both viewers
if (pdfViewer->loadPDF(filePath)) {
    // Both Qt and OpenGL viewers are ready
    statusBar()->showMessage("PDF loaded successfully");
}
```

### 3. Mode Switching
```cpp
// Switch between viewers
if (needHighPerformance) {
    pdfViewer->setViewerMode(HybridPDFViewer::CustomOpenGLViewer);
} else {
    pdfViewer->setViewerMode(HybridPDFViewer::QtNativeViewer);
}
```

### 4. Advanced Features
```cpp
// Use advanced features (OpenGL mode only)
pdfViewer->setViewerMode(HybridPDFViewer::CustomOpenGLViewer);
pdfViewer->startSearch("search term");
pdfViewer->performCursorBasedZoom(mousePos, true);
```

## 📊 Performance Comparison

| Feature | Qt Native | Custom OpenGL | Hybrid |
|---------|-----------|---------------|--------|
| **Rendering Speed** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Memory Usage** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ |
| **Feature Set** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Ease of Use** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ |
| **Customization** | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

## 🔧 Build Configuration

### CMakeLists.txt Updates
```cmake
# Find Qt packages with PDF support
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets Pdf PdfWidgets)

# Link libraries
target_link_libraries(YourApp PRIVATE 
    Qt6::Core Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Pdf Qt6::PdfWidgets
    pdfium glew32
)
```

### Include Paths
```cmake
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/pdfium/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glew/include
)
```

## 🎯 Recommended Usage

### For Document Viewers
```cpp
// Start with Qt native for simplicity
hybridViewer->setViewerMode(HybridPDFViewer::QtNativeViewer);

// Switch to OpenGL for power users
if (userPreferences.highPerformance) {
    hybridViewer->setViewerMode(HybridPDFViewer::CustomOpenGLViewer);
}
```

### For Professional Applications
```cpp
// Use OpenGL by default for professional features
hybridViewer->setViewerMode(HybridPDFViewer::CustomOpenGLViewer);

// Fallback to Qt native if OpenGL not available
if (!hybridViewer->isOpenGLAvailable()) {
    hybridViewer->setViewerMode(HybridPDFViewer::QtNativeViewer);
}
```

## 🔍 Testing Strategy

### Unit Tests
- Test both viewer modes independently
- Verify mode switching functionality
- Test state synchronization between viewers

### Performance Tests
- Measure rendering performance in both modes
- Test memory usage with large documents
- Benchmark search functionality

### User Experience Tests
- Test seamless switching between modes
- Verify feature parity where applicable
- Test on different hardware configurations

## 🎉 Conclusion

The hybrid approach gives you:
- **Flexibility** - Choose the right viewer for each use case
- **Performance** - OpenGL acceleration when needed
- **Compatibility** - Qt native fallback always available
- **Future-proof** - Easy to add new features to either viewer
- **User Choice** - Let users decide their preferred viewing mode
