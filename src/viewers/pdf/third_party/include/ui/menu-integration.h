#pragma once
#include <windows.h>
#include <string>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "utils/resource.h"

// Forward declaration
class TabManager;

// Menu integration class for GLFW + Win32 hybrid approach
class MenuIntegration {
private:
    HWND hwnd;
    HMENU hMenu;
    bool m_embeddedMode;  // Flag to disable internal tabs when embedded in Qt
    
public:
    GLFWwindow* glfwWindow; // Make this public for access
    WNDPROC originalWndProc;  // Store original window procedure
    WNDPROC originalEditProc; // Store original edit control procedure
    HWND searchToolbar; // Handle to search toolbar
    HWND searchEdit;    // Handle to search edit box
    HWND searchResults; // Handle to results text
    HWND prevButton;    // Previous button
    HWND nextButton;    // Next button
    HWND caseCheck;     // Match case checkbox
    HWND wholeCheck;    // Whole word checkbox
    
    MenuIntegration();
    ~MenuIntegration();
    
    // Initialize menu with GLFW window
    bool Initialize(GLFWwindow* window, bool embeddedMode = true);
    
    // Set embedded mode (disables internal tab system)
    void setEmbeddedMode(bool embedded) { m_embeddedMode = embedded; }
    
    // Handle menu commands
    bool HandleMenuCommand(WPARAM wParam);
    
    // Menu state management
    void EnableMenuItem(UINT menuID, bool enabled);
    void CheckMenuItem(UINT menuID, bool checked);    void UpdateMenuState();
      // Search toolbar management
    void CreateSearchToolbar();
    void UpdateSearchToolbar();
    void ResizeSearchToolbar(int width, int height);
    void UpdateSearchEditText(const std::string& text);
    
    // Tab integration
    void CreateTabsAndSearchToolbar();
    void ResizeTabsAndSearchToolbar(int width, int height);
    
    // Get native window handle
    HWND GetWindowHandle() const { return hwnd; }
    HWND GetSearchToolbar() const { return searchToolbar; }
    
private:
    // Menu command handlers
    void OnFileOpen();
    void OnFileClose();
    void OnFilePrint();
    void OnFileExit();
    
    void OnViewZoomIn();
    void OnViewZoomOut();
    void OnViewZoomFit();
    void OnViewZoomWidth();
    void OnViewActualSize();
    void OnViewFullScreen();
    void OnViewRotateLeft();
    void OnViewRotateRight();
    
    void OnNavFirst();
    void OnNavPrevious();
    void OnNavNext();
    void OnNavLast();    void OnNavGoto();
    
    void OnToolsSelect();
    void OnToolsHand();
    void OnToolsCopy();
    
    void OnHelpAbout();
};

// Global menu integration instance
extern MenuIntegration* g_menuIntegration;

// Window procedure for handling menu messages
LRESULT CALLBACK MenuWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Search edit control procedure
LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
