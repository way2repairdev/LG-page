#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "ui/menu-integration.h"
#include "rendering/pdf-render.h"
#include "core/feature.h"
#include "ui/tab-manager.h"
#include "fpdf_edit.h"
#include <iostream>
#include <fstream>
#include <commdlg.h>
#include <shellapi.h>

// Debug logging function
void WriteDebugLog(const std::string& message) {
    std::ofstream logFile("x64/Debug/debug.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << message << std::endl;
        logFile.close();
    }
    std::cout << message << std::endl; // Also output to console
}

// External references to main application state
extern PDFScrollState* g_scrollState;
extern PDFRenderer* g_renderer;
extern GLFWwindow* g_mainWindow;
extern std::vector<GLuint>* g_textures;
extern std::vector<int>* g_pageWidths;
extern std::vector<int>* g_pageHeights;

MenuIntegration::MenuIntegration() : hwnd(nullptr), hMenu(nullptr), glfwWindow(nullptr), 
    searchToolbar(nullptr), searchEdit(nullptr), searchResults(nullptr),
    prevButton(nullptr), nextButton(nullptr), caseCheck(nullptr), wholeCheck(nullptr),
    m_embeddedMode(true) {  // Default to embedded mode for Qt integration
}

MenuIntegration::~MenuIntegration() {
    if (hMenu) {
        DestroyMenu(hMenu);
    }
}

bool MenuIntegration::Initialize(GLFWwindow* window, bool embeddedMode) {
    glfwWindow = window;
    hwnd = glfwGetWin32Window(window);
    m_embeddedMode = embeddedMode;
    
    if (!hwnd) {
        std::cerr << "Failed to get Win32 window handle from GLFW" << std::endl;
        return false;
    }
    
    // Load menu from resources
    hMenu = LoadMenu(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDC_VIEWERNEW));
    if (!hMenu) {
        std::cerr << "Failed to load menu resource" << std::endl;
        return false;
    }
    
    // Set the menu to the window
    if (!SetMenu(hwnd, hMenu)) {
        std::cerr << "Failed to set menu to window" << std::endl;
        return false;
    }
      // Subclass the window to handle menu messages
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    
    // Set our custom window procedure to handle menu and toolbar messages
    originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)MenuWindowProc);
      // Update the window to show the menu
    DrawMenuBar(hwnd);
    
    // Create search toolbar
    CreateSearchToolbar();
    
    return true;
}

bool MenuIntegration::HandleMenuCommand(WPARAM wParam) {
    UINT menuID = LOWORD(wParam);
    
    switch (menuID) {
        case IDM_FILE_OPEN:
            OnFileOpen();
            return true;
        case IDM_FILE_CLOSE:
            OnFileClose();
            return true;
        case IDM_FILE_PRINT:
            OnFilePrint();
            return true;
        case IDM_EXIT:
            OnFileExit();
            return true;
            
        case IDM_VIEW_ZOOMIN:
            OnViewZoomIn();
            return true;
        case IDM_VIEW_ZOOMOUT:
            OnViewZoomOut();
            return true;
        case IDM_VIEW_ZOOMFIT:
            OnViewZoomFit();
            return true;
        case IDM_VIEW_ZOOMWIDTH:
            OnViewZoomWidth();
            return true;
        case IDM_VIEW_ACTUAL:
            OnViewActualSize();
            return true;
        case IDM_VIEW_FULLSCREEN:
            OnViewFullScreen();
            return true;
        case IDM_VIEW_ROTATE_LEFT:
            OnViewRotateLeft();
            return true;
        case IDM_VIEW_ROTATE_RIGHT:
            OnViewRotateRight();
            return true;
            
        case IDM_NAV_FIRST:
            OnNavFirst();
            return true;
        case IDM_NAV_PREV:
            OnNavPrevious();
            return true;
        case IDM_NAV_NEXT:
            OnNavNext();
            return true;
        case IDM_NAV_LAST:
            OnNavLast();
            return true;        case IDM_NAV_GOTO:
            OnNavGoto();
            return true;
            
        case IDM_TOOLS_SELECT:
            OnToolsSelect();
            return true;
        case IDM_TOOLS_HAND:
            OnToolsHand();
            return true;
        case IDM_TOOLS_COPY:
            OnToolsCopy();
            return true;
            
        case IDM_ABOUT:
            OnHelpAbout();
            return true;
    }
    
    return false;
}

void MenuIntegration::EnableMenuItem(UINT menuID, bool enabled) {
    if (hMenu) {
        ::EnableMenuItem(hMenu, menuID, enabled ? MF_ENABLED : MF_GRAYED);
    }
}

void MenuIntegration::CheckMenuItem(UINT menuID, bool checked) {
    if (hMenu) {
        ::CheckMenuItem(hMenu, menuID, checked ? MF_CHECKED : MF_UNCHECKED);
    }
}

void MenuIntegration::UpdateMenuState() {
    if (!g_scrollState || !g_renderer) return;
    
    // Enable/disable menu items based on current state
    bool hasDocument = g_renderer->GetDocument() != nullptr;
    bool hasSelection = g_scrollState->textSelection.isActive;
    
    EnableMenuItem(IDM_FILE_CLOSE, hasDocument);
    EnableMenuItem(IDM_FILE_PRINT, hasDocument);
    EnableMenuItem(IDM_VIEW_ZOOMIN, hasDocument);
    EnableMenuItem(IDM_VIEW_ZOOMOUT, hasDocument);
    EnableMenuItem(IDM_VIEW_ZOOMFIT, hasDocument);
    EnableMenuItem(IDM_VIEW_ZOOMWIDTH, hasDocument);
    EnableMenuItem(IDM_VIEW_ACTUAL, hasDocument);
    EnableMenuItem(IDM_VIEW_FULLSCREEN, hasDocument);
    EnableMenuItem(IDM_VIEW_ROTATE_LEFT, hasDocument);
    EnableMenuItem(IDM_VIEW_ROTATE_RIGHT, hasDocument);
    
    EnableMenuItem(IDM_NAV_FIRST, hasDocument);
    EnableMenuItem(IDM_NAV_PREV, hasDocument);
    EnableMenuItem(IDM_NAV_NEXT, hasDocument);
    EnableMenuItem(IDM_NAV_LAST, hasDocument);    EnableMenuItem(IDM_NAV_GOTO, hasDocument);
      EnableMenuItem(IDM_TOOLS_COPY, hasSelection);
}

// Search toolbar management
void MenuIntegration::CreateSearchToolbar() {
    if (!hwnd) return;
    
    // Get client area
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    
    // Get menu bar height
    RECT windowRect, clientAreaRect;
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientAreaRect);
    int menuHeight = GetSystemMetrics(SM_CYMENU);
    
    // Create main toolbar window - position below menu bar
    searchToolbar = CreateWindowEx(
        0,
        L"STATIC",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER,
        0, menuHeight,  // Position below menu bar
        clientRect.right, 40,  // Full width, 40px height
        hwnd,
        (HMENU)IDC_TOOLBAR,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    if (!searchToolbar) return;
    
    // Set background color to light gray
    SetClassLongPtr(searchToolbar, GCLP_HBRBACKGROUND, (LONG_PTR)GetStockObject(LTGRAY_BRUSH));
    
    // Create search label
    CreateWindowEx(
        0, L"STATIC", L"🔍 Search:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        10, 8, 70, 24,
        searchToolbar, nullptr, GetModuleHandle(nullptr), nullptr
    );
    
    // Create search edit box
    searchEdit = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        85, 8, 200, 24,
        searchToolbar,
        (HMENU)IDC_SEARCH_EDIT_TOOLBAR,
        GetModuleHandle(nullptr),
        nullptr    );
    
    // Subclass the search edit control to handle EN_CHANGE messages
    if (searchEdit) {
        SetWindowLongPtr(searchEdit, GWLP_USERDATA, (LONG_PTR)this);
        originalEditProc = (WNDPROC)SetWindowLongPtr(searchEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
    }
      // Create Previous button (parent: main window for direct message handling)
    prevButton = CreateWindowEx(
        0, L"BUTTON", L"◀",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        295, menuHeight + 8, 30, 24,  // Adjust Y position for menu height
        hwnd,  // Parent is main window, not searchToolbar
        (HMENU)IDC_SEARCH_PREV_BTN,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    // Create Next button (parent: main window for direct message handling)
    nextButton = CreateWindowEx(
        0, L"BUTTON", L"▶",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        330, menuHeight + 8, 30, 24,  // Adjust Y position for menu height
        hwnd,  // Parent is main window, not searchToolbar
        (HMENU)IDC_SEARCH_NEXT_BTN,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    // Create Match Case checkbox
    caseCheck = CreateWindowEx(
        0, L"BUTTON", L"Match Case",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        375, 10, 80, 20,
        searchToolbar,
        (HMENU)IDC_SEARCH_CASE_CHECK,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    // Create Whole Words checkbox
    wholeCheck = CreateWindowEx(
        0, L"BUTTON", L"Whole Words",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        465, 10, 90, 20,
        searchToolbar,
        (HMENU)IDC_SEARCH_WHOLE_CHECK,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    // Create results text
    searchResults = CreateWindowEx(
        0, L"STATIC", L"Ready",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        570, 8, 150, 24,
        searchToolbar,
        (HMENU)IDC_SEARCH_RESULTS_TEXT,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    // Create Clear button
    CreateWindowEx(
        0, L"BUTTON", L"✕",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        730, 8, 24, 24,
        searchToolbar,
        (HMENU)IDC_SEARCH_CLEAR_BTN,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    // Set font for all controls
    HFONT hFont = CreateFont(
        14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    
    if (hFont) {
        EnumChildWindows(searchToolbar, [](HWND hwnd, LPARAM lParam) -> BOOL {
            SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
        }, (LPARAM)hFont);
    }
    
    UpdateSearchToolbar();
}

void MenuIntegration::UpdateSearchToolbar() {
    WriteDebugLog("=== UpdateSearchToolbar CALLED ===");
    
    if (!searchResults || !g_scrollState) {
        WriteDebugLog("UpdateSearchToolbar: Early return - searchResults=" + std::string(searchResults ? "valid" : "null") + 
                      ", g_scrollState=" + std::string(g_scrollState ? "valid" : "null"));
        return;
    }
    
    // Debug current search state with more details
    WriteDebugLog("UpdateSearchToolbar: searchTerm='" + g_scrollState->textSearch.searchTerm + 
                  "', results.size()=" + std::to_string(g_scrollState->textSearch.results.size()) +
                  ", currentResultIndex=" + std::to_string(g_scrollState->textSearch.currentResultIndex) +
                  ", needsUpdate=" + std::string(g_scrollState->textSearch.needsUpdate ? "true" : "false") +
                  ", searchChanged=" + std::string(g_scrollState->textSearch.searchChanged ? "true" : "false"));
    
    // Check button validity before proceeding
    if (!nextButton || !prevButton) {
        WriteDebugLog("UpdateSearchToolbar: Button controls are null! nextButton=" + std::to_string((uintptr_t)nextButton) + 
                      ", prevButton=" + std::to_string((uintptr_t)prevButton));
        return;
    }
    
    // Check current button states BEFORE making changes
    BOOL nextEnabledBefore = IsWindowEnabled(nextButton);
    BOOL prevEnabledBefore = IsWindowEnabled(prevButton);
    WriteDebugLog("UpdateSearchToolbar: BEFORE changes - nextButton=" + std::string(nextEnabledBefore ? "enabled" : "disabled") +
                  ", prevButton=" + std::string(prevEnabledBefore ? "enabled" : "disabled"));
    
    wchar_t statusText[256];
    if (g_scrollState->textSearch.searchTerm.empty()) {
        wcscpy_s(statusText, L"Ready");
        WriteDebugLog("UpdateSearchToolbar: Status = Ready (empty search term)");
    } else if (g_scrollState->textSearch.results.empty()) {
        swprintf_s(statusText, L"No matches found");
        WriteDebugLog("UpdateSearchToolbar: Status = No matches found");
    } else {
        swprintf_s(statusText, L"%d of %d matches", 
                 g_scrollState->textSearch.currentResultIndex + 1,
                 (int)g_scrollState->textSearch.results.size());
        WriteDebugLog("UpdateSearchToolbar: Status = " + std::to_string(g_scrollState->textSearch.currentResultIndex + 1) + 
                      " of " + std::to_string(g_scrollState->textSearch.results.size()) + " matches");
    }
      SetWindowText(searchResults, statusText);
    
    // Enable/disable navigation buttons (use wraparound navigation)
    bool hasResults = !g_scrollState->textSearch.results.empty();
    
    WriteDebugLog("UpdateSearchToolbar: hasResults=" + std::string(hasResults ? "true" : "false") + 
                  ", results.size()=" + std::to_string(g_scrollState->textSearch.results.size()) +
                  ", enabling buttons=" + std::string(hasResults ? "YES" : "NO"));
      // With wraparound navigation, buttons are always enabled when results exist
    BOOL prevStateNext = EnableWindow(nextButton, hasResults);
    BOOL prevStatePrev = EnableWindow(prevButton, hasResults);
    
    WriteDebugLog("UpdateSearchToolbar: EnableWindow return values - nextButton previous state=" + std::string(prevStateNext ? "was_enabled" : "was_disabled") +
                  ", prevButton previous state=" + std::string(prevStatePrev ? "was_enabled" : "was_disabled"));
    
    // Debug button states after enabling/disabling
    BOOL nextEnabledAfter = IsWindowEnabled(nextButton);
    BOOL prevEnabledAfter = IsWindowEnabled(prevButton);
    WriteDebugLog("UpdateSearchToolbar: AFTER EnableWindow - nextButton=" + std::string(nextEnabledAfter ? "enabled" : "disabled") +
                  ", prevButton=" + std::string(prevEnabledAfter ? "enabled" : "disabled"));
    
    // If buttons didn't change as expected, investigate
    if (hasResults && (!nextEnabledAfter || !prevEnabledAfter)) {
        WriteDebugLog("UpdateSearchToolbar: WARNING! Expected buttons to be enabled but they are not!");
        WriteDebugLog("  hasResults=" + std::string(hasResults ? "true" : "false") + 
                      ", nextEnabled=" + std::string(nextEnabledAfter ? "true" : "false") + 
                      ", prevEnabled=" + std::string(prevEnabledAfter ? "true" : "false"));
    }
    
    // Update checkboxes
    if (caseCheck) {
        SendMessage(caseCheck, BM_SETCHECK, g_scrollState->textSearch.matchCase ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (wholeCheck) {
        SendMessage(wholeCheck, BM_SETCHECK, g_scrollState->textSearch.matchWholeWord ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void MenuIntegration::UpdateSearchEditText(const std::string& text) {
    if (searchEdit) {
        // Convert UTF-8 string to wide string for Win32
        int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring wtext(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], size);
        wtext.resize(size - 1); // Remove null terminator
        
        // Temporarily disable change notifications to prevent recursive updates
        static bool updating = false;
        if (!updating) {
            updating = true;
            SetWindowText(searchEdit, wtext.c_str());
            updating = false;
        }
    }
}

void MenuIntegration::ResizeSearchToolbar(int width, int height) {
    if (searchToolbar) {
        // Get menu bar height
        int menuHeight = GetSystemMetrics(SM_CYMENU);
        SetWindowPos(searchToolbar, nullptr, 0, menuHeight, width, 40, SWP_NOZORDER);
    }
}

void MenuIntegration::CreateTabsAndSearchToolbar() {
    // Skip tab creation when in embedded mode (Qt handles tabs)
    if (m_embeddedMode) {
        std::cout << "MenuIntegration: Skipping internal tab creation (embedded in Qt)" << std::endl;
        // Still create search toolbar for PDF search functionality
        CreateSearchToolbar();
        return;
    }
    
    // Initialize tab manager (only for standalone mode)
    if (!g_tabManager) {
        g_tabManager = new TabManager();
        g_tabManager->Initialize(glfwWindow, hwnd);
    }
    
    // Create the search toolbar below the tabs
    CreateSearchToolbar();
    
    // Resize both toolbars to fit properly
    int winWidth, winHeight;
    glfwGetFramebufferSize(glfwWindow, &winWidth, &winHeight);
    ResizeTabsAndSearchToolbar(winWidth, winHeight);
}

void MenuIntegration::ResizeTabsAndSearchToolbar(int width, int height) {
    if (g_tabManager) {
        // Tab toolbar at the top
        g_tabManager->ResizeTabToolbar(width, 30);
    }
    
    // Search toolbar below the tabs
    if (searchToolbar) {
        int tabHeight = g_tabManager ? 30 : 0;
        SetWindowPos(searchToolbar, nullptr, 0, tabHeight, width, 40, SWP_NOZORDER);
        
        // Update child controls within the search toolbar
        UpdateSearchToolbar();
    }
}

// Menu command implementations
void MenuIntegration::OnFileOpen() {
    OPENFILENAME ofn;
    wchar_t szFile[260] = {0};
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileName(&ofn)) {
        // Convert wide string to narrow string
        char narrowPath[260];
        WideCharToMultiByte(CP_UTF8, 0, szFile, -1, narrowPath, sizeof(narrowPath), nullptr, nullptr);
        
        // Open or activate existing tab with the PDF document (prevents duplicate tabs + unnecessary re-render)
        if (g_tabManager) {
            int tabIndex = g_tabManager->OpenOrActivateFile(narrowPath);
            if (tabIndex >= 0) {
                // If it was newly created we already switched; if existing we just activated.
                std::cout << "PDF open/activate request handled (tab index=" << tabIndex << ") path: " << narrowPath << std::endl;
                UpdateSearchToolbar();
            } else {
                MessageBox(hwnd, L"Failed to load or activate PDF document", L"Error", MB_OK | MB_ICONERROR);
            }
        } else {
            // Fallback to legacy behavior if tab manager is not available
            if (g_renderer && g_renderer->LoadDocument(narrowPath)) {
                std::string title = "PDF Viewer - " + std::string(narrowPath);
                glfwSetWindowTitle(glfwWindow, title.c_str());
                std::cout << "Loaded PDF: " << narrowPath << std::endl;
            } else {
                MessageBox(hwnd, L"Failed to load PDF document", L"Error", MB_OK | MB_ICONERROR);
            }
        }
    }
}

void MenuIntegration::OnFileClose() {
    if (g_renderer) {
        // Close current document
        // (You'll need to implement document closing logic)
        glfwSetWindowTitle(glfwWindow, "PDF Viewer");
        std::cout << "Document closed" << std::endl;
    }
}

void MenuIntegration::OnFilePrint() {
    // Implement printing functionality
    MessageBox(hwnd, L"Print functionality not yet implemented", L"Info", MB_OK | MB_ICONINFORMATION);
}

void MenuIntegration::OnFileExit() {
    glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
}

void MenuIntegration::OnViewZoomIn() {
    if (g_scrollState) {
        g_scrollState->zoomScale *= 1.2f;
        // Use SAME limits as HandleZoom function in feature.cpp for consistency
        if (g_scrollState->zoomScale > 5.0f) g_scrollState->zoomScale = 5.0f;
        g_scrollState->zoomChanged = true;
        std::cout << "Zoom In: " << g_scrollState->zoomScale << std::endl;
    }
}

void MenuIntegration::OnViewZoomOut() {
    if (g_scrollState) {
        g_scrollState->zoomScale /= 1.2f;
        // Use SAME limits as HandleZoom function in feature.cpp for consistency
        if (g_scrollState->zoomScale < 0.35f) g_scrollState->zoomScale = 0.35f;
        g_scrollState->zoomChanged = true;
        std::cout << "Zoom Out: " << g_scrollState->zoomScale << std::endl;
    }
}

void MenuIntegration::OnViewZoomFit() {
    if (g_scrollState) {
        // Implement fit to window logic
        g_scrollState->zoomScale = 1.0f; // Placeholder
        g_scrollState->zoomChanged = true;
        std::cout << "Zoom Fit" << std::endl;
    }
}

void MenuIntegration::OnViewZoomWidth() {
    if (g_scrollState) {
        // Implement fit to width logic
        g_scrollState->zoomScale = 1.0f; // Placeholder
        g_scrollState->zoomChanged = true;
        std::cout << "Zoom Width" << std::endl;
    }
}

void MenuIntegration::OnViewActualSize() {
    if (g_scrollState) {
        g_scrollState->zoomScale = 1.0f;
        g_scrollState->zoomChanged = true;
        std::cout << "Actual Size" << std::endl;
    }
}

void MenuIntegration::OnViewFullScreen() {
    // Toggle fullscreen mode
    static bool isFullscreen = false;
    if (isFullscreen) {
        // Exit fullscreen
        glfwSetWindowMonitor(glfwWindow, nullptr, 100, 100, 1024, 768, 0);
        isFullscreen = false;
    } else {
        // Enter fullscreen
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(glfwWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        isFullscreen = true;
    }
}

void MenuIntegration::OnViewRotateLeft() {
    std::cout << "Rotate Left" << std::endl;
    
    // Check if we have a valid document and renderer
    if (!g_renderer || !g_scrollState) {
        std::cout << "Error: No renderer or scroll state available for rotation" << std::endl;
        return;
    }
    
    FPDF_DOCUMENT doc = g_renderer->GetDocument();
    if (!doc) {
        std::cout << "Error: No document loaded for rotation" << std::endl;
        return;
    }
    
    int pageCount = g_renderer->GetPageCount();
    std::cout << "Rotating " << pageCount << " pages left (counterclockwise)" << std::endl;
    
    // Rotate all pages 90 degrees counterclockwise
    for (int i = 0; i < pageCount; i++) {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        if (page) {
            // Get current rotation
            int currentRotation = FPDFPage_GetRotation(page);
            
            // Calculate new rotation (subtract 90 degrees, wrap around)
            int newRotation = (currentRotation - 1 + 4) % 4;  // 0=0°, 1=90°, 2=180°, 3=270°
            
            // Set new rotation
            FPDFPage_SetRotation(page, newRotation);
            
            FPDF_ClosePage(page);
        }
    }
    
    // CRITICAL: Reload all text pages to get updated coordinates for the rotated content
    if (g_scrollState && pageCount > 0) {
        std::cout << "Reloading text pages after left rotation..." << std::endl;
        
        // Clear existing text selection as coordinates are now invalid
        ClearTextSelection(*g_scrollState);
        
        // Reload text pages with updated rotation
        for (int i = 0; i < pageCount; ++i) {
            // Unload existing text page
            UnloadTextPage(*g_scrollState, i);
            
            // Reload with new rotation
            FPDF_PAGE page = FPDF_LoadPage(doc, i);
            if (page) {
                LoadTextPage(*g_scrollState, i, page);
                FPDF_ClosePage(page);
            }
        }
        
        std::cout << "Text pages reloaded with rotated coordinates" << std::endl;
    }
    
    std::cout << "Left rotation completed for all pages" << std::endl;
}

void MenuIntegration::OnViewRotateRight() {
    std::cout << "Rotate Right" << std::endl;
    
    // Check if we have a valid document and renderer
    if (!g_renderer || !g_scrollState) {
        std::cout << "Error: No renderer or scroll state available for rotation" << std::endl;
        return;
    }
    
    FPDF_DOCUMENT doc = g_renderer->GetDocument();
    if (!doc) {
        std::cout << "Error: No document loaded for rotation" << std::endl;
        return;
    }
    
    int pageCount = g_renderer->GetPageCount();
    std::cout << "Rotating " << pageCount << " pages right (clockwise)" << std::endl;
    
    // Rotate all pages 90 degrees clockwise
    for (int i = 0; i < pageCount; i++) {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        if (page) {
            // Get current rotation
            int currentRotation = FPDFPage_GetRotation(page);
            
            // Calculate new rotation (add 90 degrees, wrap around)
            int newRotation = (currentRotation + 1) % 4;  // 0=0°, 1=90°, 2=180°, 3=270°
            
            // Set new rotation
            FPDFPage_SetRotation(page, newRotation);
            
            FPDF_ClosePage(page);
        }
    }
    
    // CRITICAL: Reload all text pages to get updated coordinates for the rotated content
    if (g_scrollState && pageCount > 0) {
        std::cout << "Reloading text pages after right rotation..." << std::endl;
        
        // Clear existing text selection as coordinates are now invalid
        ClearTextSelection(*g_scrollState);
        
        // Reload text pages with updated rotation
        for (int i = 0; i < pageCount; ++i) {
            // Unload existing text page
            UnloadTextPage(*g_scrollState, i);
            
            // Reload with new rotation
            FPDF_PAGE page = FPDF_LoadPage(doc, i);
            if (page) {
                LoadTextPage(*g_scrollState, i, page);
                FPDF_ClosePage(page);
            }
        }
        
        std::cout << "Text pages reloaded with rotated coordinates" << std::endl;
    }
    
    std::cout << "Right rotation completed for all pages" << std::endl;
}

void MenuIntegration::OnNavFirst() {
    if (g_scrollState) {
        g_scrollState->scrollOffset = 0.0f;
        std::cout << "Navigate to First Page" << std::endl;
    }
}

void MenuIntegration::OnNavPrevious() {
    if (g_scrollState) {
        // Implement previous page logic
        std::cout << "Navigate to Previous Page" << std::endl;
    }
}

void MenuIntegration::OnNavNext() {
    if (g_scrollState) {
        // Implement next page logic
        std::cout << "Navigate to Next Page" << std::endl;
    }
}

void MenuIntegration::OnNavLast() {
    if (g_scrollState) {
        g_scrollState->scrollOffset = g_scrollState->maxOffset;
        std::cout << "Navigate to Last Page" << std::endl;
    }
}

void MenuIntegration::OnNavGoto() {
    // Show go to page dialog
    MessageBox(hwnd, L"Go to page dialog not yet implemented", L"Info", MB_OK | MB_ICONINFORMATION);
}

void MenuIntegration::OnToolsSelect() {
    std::cout << "Select Tool" << std::endl;
    // Implement select tool logic
}

void MenuIntegration::OnToolsHand() {
    std::cout << "Hand Tool" << std::endl;
    // Implement hand tool logic
}

void MenuIntegration::OnToolsCopy() {
    if (g_scrollState) {
        std::string selectedText = GetSelectedText(*g_scrollState);
        if (!selectedText.empty()) {
            glfwSetClipboardString(glfwWindow, selectedText.c_str());
            std::cout << "Copied selected text to clipboard" << std::endl;
        }
    }
}

void MenuIntegration::OnHelpAbout() {
    MessageBox(hwnd, 
               L"PDF Viewer v1.0\n\nA modern PDF viewer with advanced features.\n\nCopyright (c) 2025", 
               L"About PDF Viewer", 
               MB_OK | MB_ICONINFORMATION);
}

// Search edit control procedure for handling text changes
LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MenuIntegration* menuIntegration = (MenuIntegration*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    switch (uMsg) {
        case WM_CHAR:
        case WM_KEYUP:
        case WM_PASTE:
            // Post a custom message to trigger search after text change
            PostMessage(GetParent(GetParent(hwnd)), WM_COMMAND, MAKEWPARAM(IDC_SEARCH_EDIT_TOOLBAR, EN_CHANGE), (LPARAM)hwnd);
            break;
    }
    
    // Call original edit control procedure
    if (menuIntegration && menuIntegration->originalEditProc) {
        return CallWindowProc(menuIntegration->originalEditProc, hwnd, uMsg, wParam, lParam);
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Window procedure for handling menu messages
LRESULT CALLBACK MenuWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MenuIntegration* menuIntegration = (MenuIntegration*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    switch (uMsg) {        case WM_COMMAND:
            // Debug: Log all WM_COMMAND messages
            {
                WORD commandId = LOWORD(wParam);
                WORD notificationCode = HIWORD(wParam);
                std::cout << "WM_COMMAND: commandId=" << commandId << ", notificationCode=" << notificationCode << std::endl;
                
                // Check specifically for our button IDs
                if (commandId == IDC_SEARCH_NEXT_BTN) {
                    std::cout << "WM_COMMAND: NEXT BUTTON COMMAND RECEIVED!" << std::endl;
                } else if (commandId == IDC_SEARCH_PREV_BTN) {
                    std::cout << "WM_COMMAND: PREV BUTTON COMMAND RECEIVED!" << std::endl;
                }
            }
            
            if (menuIntegration) {
                // Handle toolbar commands
                switch (LOWORD(wParam)) {case IDC_SEARCH_EDIT_TOOLBAR:                        if (HIWORD(wParam) == EN_CHANGE) {
                            WriteDebugLog("=== EN_CHANGE: Search text changed ===");
                            std::cout << "=== EN_CHANGE: Search text changed ===" << std::endl;
                            
                            // Search text changed
                            char searchText[256];
                            GetWindowTextA((HWND)lParam, searchText, sizeof(searchText));                            WriteDebugLog("EN_CHANGE: New search text: '" + std::string(searchText) + "'");
                            std::cout << "EN_CHANGE: New search text: '" << searchText << "' (length: " << strlen(searchText) << ")" << std::endl;
                            
                            if (g_scrollState) {
                                std::cout << "EN_CHANGE: Setting search state..." << std::endl;
                                g_scrollState->textSearch.searchTerm = searchText;
                                g_scrollState->textSearch.needsUpdate = true;
                                g_scrollState->textSearch.searchChanged = true;
                                g_scrollState->textSearch.lastInputTime = glfwGetTime(); // Set timestamp for main loop search
                                
                                std::cout << "EN_CHANGE: Search flags set, starting timer..." << std::endl;
                                
                                // Check current button state before setting timer
                                if (menuIntegration->nextButton && menuIntegration->prevButton) {
                                    BOOL nextEnabled = IsWindowEnabled(menuIntegration->nextButton);
                                    BOOL prevEnabled = IsWindowEnabled(menuIntegration->prevButton);
                                    std::cout << "EN_CHANGE: Button state BEFORE timer - next=" << (nextEnabled ? "enabled" : "disabled")
                                              << ", prev=" << (prevEnabled ? "enabled" : "disabled") << std::endl;
                                }
                                
                                // Schedule toolbar update
                                SetTimer(hwnd, 1, 300, nullptr);
                                
                                std::cout << "EN_CHANGE: Timer set for 300ms" << std::endl;
                            } else {
                                std::cout << "EN_CHANGE: ERROR - g_scrollState is null!" << std::endl;
                            }
                            std::cout << "=== EN_CHANGE END ===" << std::endl;
                        }
                        break;case IDC_SEARCH_PREV_BTN:
                        if (HIWORD(wParam) == BN_CLICKED) {
                            std::cout << "=== PREVIOUS BUTTON CLICKED ===" << std::endl;
                            
                            // Debug button state at click time
                            BOOL isEnabled = IsWindowEnabled(menuIntegration->prevButton);
                            std::cout << "Previous button enabled state at click: " << (isEnabled ? "enabled" : "disabled") << std::endl;
                            
                            if (g_scrollState) {
                                std::cout << "Previous button: g_scrollState is valid" << std::endl;
                                std::cout << "Previous button: searchTerm='" << g_scrollState->textSearch.searchTerm << "'" << std::endl; 
                                std::cout << "Previous button: results.size()=" << g_scrollState->textSearch.results.size() << std::endl;
                                std::cout << "Previous button: currentResultIndex=" << g_scrollState->textSearch.currentResultIndex << std::endl;
                                  if (!g_scrollState->textSearch.results.empty()) {
                                    std::cout << "Previous button: Calling NavigateToPreviousSearchResult" << std::endl;
                                    NavigateToPreviousSearchResult(*g_scrollState, *g_pageHeights);
                                    std::cout << "Previous button: Navigation completed, updating toolbar" << std::endl;
                                    menuIntegration->UpdateSearchToolbar();
                                    
                                    // Force GLFW window to redraw immediately
                                    if (menuIntegration->glfwWindow) {
                                        glfwPostEmptyEvent(); // Wake up the GLFW event loop
                                    }
                                } else {
                                    std::cout << "Previous button: NO RESULTS - results are empty!" << std::endl;
                                }
                            } else {
                                std::cout << "Previous button: ERROR - g_scrollState is null!" << std::endl;
                            }
                            std::cout << "=== PREVIOUS BUTTON CLICK END ===" << std::endl;
                        }
                        break;                    case IDC_SEARCH_NEXT_BTN:
                        if (HIWORD(wParam) == BN_CLICKED) {
                            WriteDebugLog("=== NEXT BUTTON CLICKED ===");
                            
                            // Debug button state at click time
                            BOOL isEnabled = IsWindowEnabled(menuIntegration->nextButton);
                            WriteDebugLog("Next button enabled state at click: " + std::string(isEnabled ? "enabled" : "disabled"));
                            
                            if (g_scrollState) {
                                WriteDebugLog("Next button: g_scrollState is valid");
                                WriteDebugLog("Next button: searchTerm='" + g_scrollState->textSearch.searchTerm + "'");
                                WriteDebugLog("Next button: results.size()=" + std::to_string(g_scrollState->textSearch.results.size()));
                                WriteDebugLog("Next button: currentResultIndex=" + std::to_string(g_scrollState->textSearch.currentResultIndex));
                                  if (!g_scrollState->textSearch.results.empty()) {
                                    WriteDebugLog("Next button: Calling NavigateToNextSearchResult");
                                    NavigateToNextSearchResult(*g_scrollState, *g_pageHeights);
                                    WriteDebugLog("Next button: Navigation completed, updating toolbar");
                                    menuIntegration->UpdateSearchToolbar();
                                    
                                    // Force GLFW window to redraw immediately
                                    if (menuIntegration->glfwWindow) {
                                        glfwPostEmptyEvent(); // Wake up the GLFW event loop
                                    }
                                } else {
                                    WriteDebugLog("Next button: NO RESULTS - results are empty!");
                                }
                            } else {
                                WriteDebugLog("Next button: ERROR - g_scrollState is null!");
                            }
                            WriteDebugLog("=== NEXT BUTTON CLICK END ===");
                        }
                        break;
                        
                    case IDC_SEARCH_CASE_CHECK:
                        if (g_scrollState) {
                            g_scrollState->textSearch.matchCase = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                            g_scrollState->textSearch.needsUpdate = true;
                            g_scrollState->textSearch.searchChanged = true;
                            SetTimer(hwnd, 1, 100, nullptr);
                        }
                        break;
                        
                    case IDC_SEARCH_WHOLE_CHECK:
                        if (g_scrollState) {
                            g_scrollState->textSearch.matchWholeWord = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                            g_scrollState->textSearch.needsUpdate = true;
                            g_scrollState->textSearch.searchChanged = true;
                            SetTimer(hwnd, 1, 100, nullptr);
                        }
                        break;
                        
                    case IDC_SEARCH_CLEAR_BTN:
                        if (g_scrollState && menuIntegration->searchEdit) {
                            SetWindowText(menuIntegration->searchEdit, L"");
                            g_scrollState->textSearch.searchTerm.clear();
                            g_scrollState->textSearch.results.clear();
                            g_scrollState->textSearch.currentResultIndex = -1;
                            menuIntegration->UpdateSearchToolbar();
                        }
                        break;
                        
                    default:
                        // Handle regular menu commands
                        if (menuIntegration->HandleMenuCommand(wParam)) {
                            menuIntegration->UpdateMenuState();
                            return 0;
                        }
                        break;
                }
            }
            break;        case WM_TIMER:
            if (wParam == 1 && menuIntegration) {
                std::cout << "=== WM_TIMER: Timer triggered ===" << std::endl;
                KillTimer(hwnd, 1);
                
                // Check button state before search
                if (menuIntegration->nextButton && menuIntegration->prevButton) {
                    BOOL nextEnabled = IsWindowEnabled(menuIntegration->nextButton);
                    BOOL prevEnabled = IsWindowEnabled(menuIntegration->prevButton);
                    std::cout << "WM_TIMER: Button state BEFORE search - next=" << (nextEnabled ? "enabled" : "disabled")
                              << ", prev=" << (prevEnabled ? "enabled" : "disabled") << std::endl;
                }
                
                // Perform the actual search when timer expires
                if (g_scrollState && g_pageHeights && g_pageWidths && g_scrollState->textSearch.searchChanged) {
                    std::cout << "WM_TIMER: Calling PerformTextSearch for '" << g_scrollState->textSearch.searchTerm << "'" << std::endl;
                    
                    size_t resultsBefore = g_scrollState->textSearch.results.size();
                    PerformTextSearch(*g_scrollState, *g_pageHeights, *g_pageWidths);
                    size_t resultsAfter = g_scrollState->textSearch.results.size();
                    
                    g_scrollState->textSearch.searchChanged = false;
                    
                    std::cout << "WM_TIMER: Search completed - results before=" << resultsBefore << ", after=" << resultsAfter << std::endl;
                } else {
                    std::cout << "WM_TIMER: Search conditions not met" << std::endl;
                    if (!g_scrollState) std::cout << "  - g_scrollState is null" << std::endl;
                    if (!g_pageHeights) std::cout << "  - g_pageHeights is null" << std::endl;
                    if (!g_pageWidths) std::cout << "  - g_pageWidths is null" << std::endl;
                    if (g_scrollState && !g_scrollState->textSearch.searchChanged) std::cout << "  - searchChanged is false" << std::endl;
                }
                
                std::cout << "WM_TIMER: Updating toolbar..." << std::endl;
                menuIntegration->UpdateSearchToolbar();
                
                // Check button state after search and toolbar update
                if (menuIntegration->nextButton && menuIntegration->prevButton) {
                    BOOL nextEnabled = IsWindowEnabled(menuIntegration->nextButton);
                    BOOL prevEnabled = IsWindowEnabled(menuIntegration->prevButton);
                    std::cout << "WM_TIMER: Button state AFTER search+update - next=" << (nextEnabled ? "enabled" : "disabled")
                              << ", prev=" << (prevEnabled ? "enabled" : "disabled") << std::endl;
                }
                
                std::cout << "=== WM_TIMER END ===" << std::endl;
            }
            break;
            
        case WM_SIZE:
            if (menuIntegration) {
                menuIntegration->ResizeSearchToolbar(LOWORD(lParam), HIWORD(lParam));
            }
            break;    }
    
    // Call the original window procedure for unhandled messages
    if (menuIntegration && menuIntegration->originalWndProc) {
        return CallWindowProc(menuIntegration->originalWndProc, hwnd, uMsg, wParam, lParam);
    }
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
}