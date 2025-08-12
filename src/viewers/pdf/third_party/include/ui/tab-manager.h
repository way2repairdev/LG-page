#pragma once

#define NOMINMAX
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <memory>
#include <windows.h>
#include "rendering/pdf-render.h"
#include "core/feature.h"

// Forward declarations
struct GLFWwindow;

// Tab data structure
struct PDFTab {
    std::string filename;           // Full path to the PDF file
    std::string displayName;        // Display name for the tab (just filename)
    std::string normalizedPath;     // Normalized (lowercase, unified slashes) full path for duplicate detection
    std::unique_ptr<PDFRenderer> renderer;  // PDF renderer for this tab
    PDFScrollState scrollState;     // Scroll state for this tab
    std::vector<GLuint> textures;   // OpenGL textures for pages
    std::vector<int> pageWidths;    // Page widths
    std::vector<int> pageHeights;   // Page heights
    std::vector<double> originalPageWidths;  // Original PDF page widths
    std::vector<double> originalPageHeights; // Original PDF page heights
    bool isLoaded;                  // Whether the PDF is fully loaded
    bool needsReload;               // Whether textures need to be reloaded
    int tabIndex;                   // Index of this tab
    
    PDFTab() : isLoaded(false), needsReload(false), tabIndex(-1) {}
    
    // Clean up resources
    ~PDFTab() {
        CleanupTextures();
    }
    
    void CleanupTextures() {
        if (!textures.empty()) {
            glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
            textures.clear();
        }
    }
};

// Tab manager class
class TabManager {
private:
    std::vector<std::unique_ptr<PDFTab>> tabs;
    int activeTabIndex;
    HWND tabToolbar;        // Handle to tab toolbar
    GLFWwindow* glfwWindow;
    
    // Tab control constants
    static constexpr int TAB_HEIGHT = 30;
    static constexpr int TAB_MIN_WIDTH = 120;
    static constexpr int TAB_MAX_WIDTH = 200;
    static constexpr int TAB_CLOSE_BUTTON_SIZE = 16;
    static constexpr int TAB_MARGIN = 2;
    
public:
    TabManager();
    ~TabManager();
    
    // Initialize tab manager
    bool Initialize(GLFWwindow* window, HWND parentWindow);
    
    // Tab management
    int CreateNewTab(const std::string& filename);
    // Open file: if already open just activate, else create new tab. Returns tab index or -1 on failure
    int OpenOrActivateFile(const std::string& filename);
    bool LoadPDFInTab(int tabIndex, const std::string& filename);
    bool CloseTab(int tabIndex);
    bool SwitchToTab(int tabIndex);
    void CloseAllTabs();
    
    // Tab UI management
    void CreateTabToolbar(HWND parentWindow);
    void UpdateTabToolbar();
    void ResizeTabToolbar(int width, int height);
    void RedrawTabs();
    
    // Getters
    int GetActiveTabIndex() const { return activeTabIndex; }
    int GetTabCount() const { return static_cast<int>(tabs.size()); }
    PDFTab* GetActiveTab() const;
    PDFTab* GetTab(int index) const;
    std::string GetTabDisplayName(int index) const;
    
    // Tab toolbar event handling
    bool HandleTabClick(int x, int y);
    bool HandleTabClose(int tabIndex);
      // Utility functions
    void UpdateWindowTitle();
    std::string ExtractFilename(const std::string& fullPath);
    
    // Tab rendering helpers (public for toolbar WndProc)
    void DrawTab(HDC hdc, int tabIndex, const RECT& tabRect, bool isActive);
    RECT GetTabRect(int tabIndex, int toolbarWidth);
    
private:
    int GetTabIndexFromPoint(int x, int y);
    bool IsPointInCloseButton(int x, int y, int tabIndex);
    
    // Tab layout calculation
    int CalculateTabWidth(int totalWidth, int tabCount);
    void LayoutTabs(int toolbarWidth);
};

// Global tab manager instance
extern TabManager* g_tabManager;

// Tab toolbar window procedure
LRESULT CALLBACK TabToolbarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
