// Global variable stubs for PDF viewer integration
// These are referenced by the existing PDF source files but need to be defined

#include "rendering/pdf-render.h"
#include "core/feature.h"
#include "ui/menu-integration.h"

// Forward declarations
class TabManager;

// Global variables that are referenced by the PDF source files
PDFScrollState* g_scrollState = nullptr;
PDFRenderer* g_renderer = nullptr;
GLFWwindow* g_mainWindow = nullptr;
std::vector<GLuint>* g_textures = nullptr;
std::vector<int>* g_pageWidths = nullptr;
std::vector<int>* g_pageHeights = nullptr;
MenuIntegration* g_menuIntegration = nullptr;
TabManager* g_tabManager = nullptr;
