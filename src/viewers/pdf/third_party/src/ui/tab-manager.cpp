#define NOMINMAX
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "ui/tab-manager.h"
#include "rendering/pdf-render.h"
#include "core/feature.h"
#include <algorithm>
#include <iostream>

// External references
extern TabManager* g_tabManager;
extern PDFScrollState* g_scrollState;
extern PDFRenderer* g_renderer;
extern std::vector<int>* g_pageHeights;
extern std::vector<int>* g_pageWidths;

TabManager::TabManager() : activeTabIndex(-1), tabToolbar(nullptr), glfwWindow(nullptr) {
}

TabManager::~TabManager() {
    CloseAllTabs();
    if (tabToolbar) {
        DestroyWindow(tabToolbar);
    }
}

bool TabManager::Initialize(GLFWwindow* window, HWND parentWindow) {
    glfwWindow = window;
    CreateTabToolbar(parentWindow);
    return true;
}

void TabManager::CreateTabToolbar(HWND parentWindow) {
    // Create tab toolbar as a child window
    tabToolbar = CreateWindowEx(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 800, TAB_HEIGHT,
        parentWindow,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    if (tabToolbar) {
        // Set window procedure to handle tab clicks
        SetWindowLongPtr(tabToolbar, GWLP_WNDPROC, (LONG_PTR)TabToolbarProc);
        SetWindowLongPtr(tabToolbar, GWLP_USERDATA, (LONG_PTR)this);
        
        std::cout << "Tab toolbar created successfully" << std::endl;
    } else {
        std::cout << "Failed to create tab toolbar" << std::endl;
    }
}

int TabManager::CreateNewTab(const std::string& filename) {
    auto newTab = std::make_unique<PDFTab>();
    newTab->filename = filename;
    newTab->displayName = ExtractFilename(filename);
    // Build normalized path (lowercase + forward slashes) for duplicate detection
    newTab->normalizedPath = filename;
    std::replace(newTab->normalizedPath.begin(), newTab->normalizedPath.end(), '\\', '/');
    std::transform(newTab->normalizedPath.begin(), newTab->normalizedPath.end(), newTab->normalizedPath.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    newTab->tabIndex = static_cast<int>(tabs.size());
    newTab->renderer = std::make_unique<PDFRenderer>();
    
    // Initialize the PDF renderer
    newTab->renderer->Initialize();
    
    tabs.push_back(std::move(newTab));
    int newTabIndex = static_cast<int>(tabs.size()) - 1;
    
    // Load the PDF in the new tab
    if (LoadPDFInTab(newTabIndex, filename)) {
        // Switch to the new tab
        SwitchToTab(newTabIndex);
        UpdateTabToolbar();
        return newTabIndex;
    } else {
        // Failed to load PDF, remove the tab
        tabs.pop_back();
        return -1;
    }
}

// Helper to normalize a path for comparisons
static std::string NormalizePathForCompare(const std::string& path) {
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    std::transform(norm.begin(), norm.end(), norm.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return norm;
}

int TabManager::OpenOrActivateFile(const std::string& filename) {
    if (filename.empty()) return -1;

    std::string target = NormalizePathForCompare(filename);

    // First scan existing tabs for a match
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i] && tabs[i]->normalizedPath == target) {
            // Already open: just switch; DO NOT reload to avoid flicker/re-render
            SwitchToTab((int)i);
            std::cout << "TabManager: Activated existing tab for file: " << filename << std::endl;
            return (int)i;
        }
    }

    // Not open yet: create
    int idx = CreateNewTab(filename);
    if (idx >= 0) {
        std::cout << "TabManager: Opened new tab for file: " << filename << std::endl;
    }
    return idx;
}

bool TabManager::LoadPDFInTab(int tabIndex, const std::string& filename) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) {
        return false;
    }
    
    PDFTab* tab = tabs[tabIndex].get();
    if (!tab->renderer->LoadDocument(filename)) {
        std::cout << "Failed to load PDF: " << filename << std::endl;
        return false;
    }
    
    // Get window dimensions for texture rendering
    int winWidth, winHeight;
    glfwGetFramebufferSize(glfwWindow, &winWidth, &winHeight);
    
    // Render all pages to textures
    int pageCount = tab->renderer->GetPageCount();
    tab->textures.resize(pageCount);
    tab->pageWidths.resize(pageCount);
    tab->pageHeights.resize(pageCount);
    tab->originalPageWidths.resize(pageCount);
    tab->originalPageHeights.resize(pageCount);
    
    for (int i = 0; i < pageCount; ++i) {
        // Get rendered page dimensions
        int pageW = 0, pageH = 0;
        tab->renderer->GetBestFitSize(i, winWidth, winHeight, pageW, pageH);
        FPDF_BITMAP bmp = tab->renderer->RenderPageToBitmap(i, pageW, pageH);
        
        // Create OpenGL texture
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        void* buffer = FPDFBitmap_GetBuffer(bmp);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pageW, pageH, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
        glBindTexture(GL_TEXTURE_2D, 0);
        
        tab->textures[i] = textureID;
        tab->pageWidths[i] = pageW;
        tab->pageHeights[i] = pageH;
        FPDFBitmap_Destroy(bmp);
        
        // Get original PDF page dimensions
        tab->renderer->GetOriginalPageSize(i, tab->originalPageWidths[i], tab->originalPageHeights[i]);
    }
    
    // Initialize scroll state and text extraction
    tab->scrollState.pageHeights = &tab->pageHeights;
    tab->scrollState.pageWidths = &tab->pageWidths;
    tab->scrollState.originalPageWidths = &tab->originalPageWidths;
    tab->scrollState.originalPageHeights = &tab->originalPageHeights;
    
    // Initialize text extraction
    InitializeTextExtraction(tab->scrollState, pageCount);
    InitializeTextSearch(tab->scrollState);
    
    // Load text pages
    FPDF_DOCUMENT document = tab->renderer->GetDocument();
    for (int i = 0; i < pageCount; ++i) {
        FPDF_PAGE page = FPDF_LoadPage(document, i);
        if (page) {
            LoadTextPage(tab->scrollState, i, page);
            FPDF_ClosePage(page);
        }
    }
    
    // Update scroll state
    UpdateScrollState(tab->scrollState, static_cast<float>(winHeight), tab->pageHeights);
    
    tab->isLoaded = true;
    std::cout << "Successfully loaded PDF with " << pageCount << " pages: " << filename << std::endl;
    
    return true;
}

bool TabManager::SwitchToTab(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) {
        return false;
    }
    
    if (tabIndex == activeTabIndex) {
        return true; // Already active
    }
    
    activeTabIndex = tabIndex;
    PDFTab* activeTab = GetActiveTab();
    
    if (activeTab && activeTab->isLoaded) {
        // Update global pointers to point to the active tab's data
        g_scrollState = &activeTab->scrollState;
        g_renderer = activeTab->renderer.get();
        g_pageHeights = &activeTab->pageHeights;
        g_pageWidths = &activeTab->pageWidths;
        
        // Update window title
        UpdateWindowTitle();
        
        // Redraw tabs to show the active tab
        RedrawTabs();
        
        std::cout << "Switched to tab " << tabIndex << ": " << activeTab->displayName << std::endl;
        return true;
    }
    
    return false;
}

bool TabManager::CloseTab(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) {
        std::cout << "CloseTab: Invalid tab index " << tabIndex << std::endl;
        return false;
    }
    
    std::cout << "CloseTab: Closing tab " << tabIndex << " (" << tabs[tabIndex]->displayName << ")" << std::endl;
    std::cout << "CloseTab: Before - activeTabIndex=" << activeTabIndex << ", tabs.size()=" << tabs.size() << std::endl;
      // If closing the active tab, switch to another tab first
    if (tabIndex == activeTabIndex) {
        if (tabs.size() > 1) {
            // Switch to the next tab, or previous if this is the last tab
            int newActiveTab;
            if (tabIndex < static_cast<int>(tabs.size()) - 1) {
                // Not the last tab, switch to the next tab
                newActiveTab = tabIndex + 1;
            } else {
                // This is the last tab, switch to the previous tab
                newActiveTab = tabIndex - 1;
            }
            std::cout << "CloseTab: Active tab being closed, switching to tab " << newActiveTab << std::endl;
            if (newActiveTab >= 0 && newActiveTab < static_cast<int>(tabs.size()) && newActiveTab != tabIndex) {
                SwitchToTab(newActiveTab);
            }
        } else {
            // This is the last tab, clear global pointers
            std::cout << "CloseTab: Closing last tab, clearing global pointers" << std::endl;
            g_scrollState = nullptr;
            g_renderer = nullptr;
            g_pageHeights = nullptr;
            g_pageWidths = nullptr;
            activeTabIndex = -1;
        }
    }
      // Remove the tab
    tabs.erase(tabs.begin() + tabIndex);
    std::cout << "CloseTab: Tab removed, new size=" << tabs.size() << std::endl;
    
    // Update tab indices for all remaining tabs
    for (int i = tabIndex; i < static_cast<int>(tabs.size()); ++i) {
        tabs[i]->tabIndex = i;
    }
    
    // Adjust active tab index if necessary
    if (activeTabIndex > tabIndex) {
        // Active tab was after the closed tab, shift index down
        activeTabIndex--;
        std::cout << "CloseTab: Adjusted activeTabIndex to " << activeTabIndex << std::endl;
    } else if (activeTabIndex == tabIndex) {
        // We closed the active tab
        if (tabs.empty()) {
            // No tabs left
            activeTabIndex = -1;
            std::cout << "CloseTab: No tabs remaining, activeTabIndex = -1" << std::endl;
        } else {
            // We should have already switched to another tab above
            // Make sure activeTabIndex is valid
            if (activeTabIndex >= static_cast<int>(tabs.size())) {
                activeTabIndex = static_cast<int>(tabs.size()) - 1;
            }
            std::cout << "CloseTab: Active tab was closed, activeTabIndex now = " << activeTabIndex << std::endl;
        }
    }
    
    std::cout << "CloseTab: Final - activeTabIndex=" << activeTabIndex << ", tabs.size()=" << tabs.size() << std::endl;
    
    UpdateTabToolbar();
    UpdateWindowTitle();
    
    return true;
}

void TabManager::CloseAllTabs() {
    tabs.clear();
    activeTabIndex = -1;
    g_scrollState = nullptr;
    g_renderer = nullptr;
    g_pageHeights = nullptr;
    g_pageWidths = nullptr;
    UpdateTabToolbar();
    UpdateWindowTitle();
}

PDFTab* TabManager::GetActiveTab() const {
    if (activeTabIndex >= 0 && activeTabIndex < static_cast<int>(tabs.size())) {
        return tabs[activeTabIndex].get();
    }
    return nullptr;
}

PDFTab* TabManager::GetTab(int index) const {
    if (index >= 0 && index < static_cast<int>(tabs.size())) {
        return tabs[index].get();
    }
    return nullptr;
}

std::string TabManager::GetTabDisplayName(int index) const {
    PDFTab* tab = GetTab(index);
    return tab ? tab->displayName : "";
}

void TabManager::UpdateTabToolbar() {
    if (tabToolbar) {
        InvalidateRect(tabToolbar, nullptr, TRUE);
    }
}

void TabManager::ResizeTabToolbar(int width, int height) {
    if (tabToolbar) {
        SetWindowPos(tabToolbar, nullptr, 0, 0, width, TAB_HEIGHT, SWP_NOZORDER);
        UpdateTabToolbar();
    }
}

void TabManager::RedrawTabs() {
    UpdateTabToolbar();
}

void TabManager::UpdateWindowTitle() {
    if (glfwWindow) {
        PDFTab* activeTab = GetActiveTab();
        if (activeTab) {
            std::string title = "PDF Viewer - " + activeTab->displayName;
            glfwSetWindowTitle(glfwWindow, title.c_str());
        } else {
            glfwSetWindowTitle(glfwWindow, "PDF Viewer");
        }
    }
}

std::string TabManager::ExtractFilename(const std::string& fullPath) {
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(lastSlash + 1);
    }
    return fullPath;
}

bool TabManager::HandleTabClick(int x, int y) {
    if (tabs.empty()) return false;
    
    RECT clientRect;
    GetClientRect(tabToolbar, &clientRect);
    int toolbarWidth = clientRect.right - clientRect.left;
    
    // Check if click is on a close button first
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        if (IsPointInCloseButton(x, y, i)) {
            CloseTab(i);
            return true;
        }
    }
    
    // Check if click is on a tab
    int clickedTab = GetTabIndexFromPoint(x, y);
    if (clickedTab >= 0) {
        SwitchToTab(clickedTab);
        return true;
    }
    
    return false;
}

int TabManager::GetTabIndexFromPoint(int x, int y) {
    if (tabs.empty()) return -1;
    
    RECT clientRect;
    GetClientRect(tabToolbar, &clientRect);
    int toolbarWidth = clientRect.right - clientRect.left;
    
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        RECT tabRect = GetTabRect(i, toolbarWidth);
        if (x >= tabRect.left && x <= tabRect.right && y >= tabRect.top && y <= tabRect.bottom) {
            return i;
        }
    }
    
    return -1;
}

bool TabManager::IsPointInCloseButton(int x, int y, int tabIndex) {
    RECT clientRect;
    GetClientRect(tabToolbar, &clientRect);
    int toolbarWidth = clientRect.right - clientRect.left;
    
    RECT tabRect = GetTabRect(tabIndex, toolbarWidth);
    
    // Close button is on the right side of the tab
    RECT closeRect;
    closeRect.right = tabRect.right - TAB_MARGIN;
    closeRect.left = closeRect.right - TAB_CLOSE_BUTTON_SIZE;
    closeRect.top = tabRect.top + (TAB_HEIGHT - TAB_CLOSE_BUTTON_SIZE) / 2;
    closeRect.bottom = closeRect.top + TAB_CLOSE_BUTTON_SIZE;
    
    return (x >= closeRect.left && x <= closeRect.right && y >= closeRect.top && y <= closeRect.bottom);
}

RECT TabManager::GetTabRect(int tabIndex, int toolbarWidth) {
    RECT rect = {0};
    
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) {
        return rect;
    }
    
    int tabWidth = CalculateTabWidth(toolbarWidth, static_cast<int>(tabs.size()));
    
    rect.left = tabIndex * tabWidth;
    rect.right = rect.left + tabWidth;
    rect.top = 0;
    rect.bottom = TAB_HEIGHT;
    
    return rect;
}

int TabManager::CalculateTabWidth(int totalWidth, int tabCount) {
    if (tabCount == 0) return 0;
    
    int availableWidth = totalWidth - (TAB_MARGIN * 2);
    int idealWidth = availableWidth / tabCount;
    
    // Clamp to min/max width
    idealWidth = std::max(TAB_MIN_WIDTH, std::min(TAB_MAX_WIDTH, idealWidth));
    
    return idealWidth;
}

void TabManager::DrawTab(HDC hdc, int tabIndex, const RECT& tabRect, bool isActive) {
    // Draw tab background
    HBRUSH bgBrush = CreateSolidBrush(isActive ? RGB(255, 255, 255) : RGB(240, 240, 240));
    FillRect(hdc, &tabRect, bgBrush);
    DeleteObject(bgBrush);
    
    // Draw tab border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    
    MoveToEx(hdc, tabRect.left, tabRect.bottom - 1, nullptr);
    LineTo(hdc, tabRect.left, tabRect.top);
    LineTo(hdc, tabRect.right - 1, tabRect.top);
    LineTo(hdc, tabRect.right - 1, tabRect.bottom - 1);
    
    if (!isActive) {
        LineTo(hdc, tabRect.left, tabRect.bottom - 1);
    }
    
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
    
    // Draw tab text
    PDFTab* tab = GetTab(tabIndex);
    if (tab) {
        RECT textRect = tabRect;
        textRect.left += TAB_MARGIN;
        textRect.right -= TAB_CLOSE_BUTTON_SIZE + TAB_MARGIN * 2;
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        
        // Convert string to wide string for drawing
        std::wstring wideText(tab->displayName.begin(), tab->displayName.end());
        DrawTextW(hdc, wideText.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    
    // Draw close button (X)
    RECT closeRect;
    closeRect.right = tabRect.right - TAB_MARGIN;
    closeRect.left = closeRect.right - TAB_CLOSE_BUTTON_SIZE;
    closeRect.top = tabRect.top + (TAB_HEIGHT - TAB_CLOSE_BUTTON_SIZE) / 2;
    closeRect.bottom = closeRect.top + TAB_CLOSE_BUTTON_SIZE;
    
    // Draw close button background
    HBRUSH closeBrush = CreateSolidBrush(RGB(220, 220, 220));
    FillRect(hdc, &closeRect, closeBrush);
    DeleteObject(closeBrush);
    
    // Draw X
    HPEN closePen = CreatePen(PS_SOLID, 2, RGB(100, 100, 100));
    SelectObject(hdc, closePen);
    
    int padding = 4;
    MoveToEx(hdc, closeRect.left + padding, closeRect.top + padding, nullptr);
    LineTo(hdc, closeRect.right - padding, closeRect.bottom - padding);
    MoveToEx(hdc, closeRect.right - padding, closeRect.top + padding, nullptr);
    LineTo(hdc, closeRect.left + padding, closeRect.bottom - padding);
    
    SelectObject(hdc, oldPen);
    DeleteObject(closePen);
}

// Tab toolbar window procedure
LRESULT CALLBACK TabToolbarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    TabManager* tabManager = (TabManager*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (tabManager) {
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                int toolbarWidth = clientRect.right - clientRect.left;
                
                // Clear background
                HBRUSH bgBrush = CreateSolidBrush(RGB(250, 250, 250));
                FillRect(hdc, &clientRect, bgBrush);
                DeleteObject(bgBrush);
                
                // Draw tabs
                for (int i = 0; i < tabManager->GetTabCount(); ++i) {
                    RECT tabRect = tabManager->GetTabRect(i, toolbarWidth);
                    bool isActive = (i == tabManager->GetActiveTabIndex());
                    tabManager->DrawTab(hdc, i, tabRect, isActive);
                }
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            
            if (tabManager) {
                tabManager->HandleTabClick(x, y);
            }
            return 0;
        }
        
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}
