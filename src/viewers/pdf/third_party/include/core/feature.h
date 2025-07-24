#pragma once

#include <vector>
#include <string>
#include <fstream>
#include "rendering/pdf-render.h"
#include "fpdf_text.h"

// Forward declarations
struct GLFWwindow;

// Text page information structure
struct TextPageData {
    FPDF_TEXTPAGE textPage = nullptr;
    int charCount = 0;
    bool isLoaded = false;
};

// Text selection information structure
struct TextSelection {
    bool isActive = false;
    bool isDragging = false;
    int startPageIndex = -1;
    int endPageIndex = -1;
    int startCharIndex = -1;
    int endCharIndex = -1;
    double startX = 0.0;
    double startY = 0.0;
    double endX = 0.0;
    double endY = 0.0;
    
    // Initial screen coordinates for drag distance calculation
    double initialScreenX = 0.0;
    double initialScreenY = 0.0;
    
    // Track zoom/pan state when selection was made for coordinate updates
    float selectionZoomScale = 1.0f;
    float selectionScrollOffset = 0.0f;
    float selectionHorizontalOffset = 0.0f;
    bool needsCoordinateUpdate = false;
    
    // Double-click detection
    double lastClickTime = 0.0;
    double lastClickX = 0.0;
    double lastClickY = 0.0;
    bool isDoubleClick = false;
};

// Text search result structure
struct SearchResult {
    int pageIndex = -1;
    int charIndex = -1;
    int charCount = 0;
    bool isValid = false;
};

// Text search state structure
struct TextSearch {
    bool isActive = true;                     // Whether search mode is active (always true now)
    bool isSearchBoxVisible = true;           // Whether search box is visible (always true now)
    std::string searchTerm;                   // Current search term
    std::vector<SearchResult> results;        // All search results
    int currentResultIndex = -1;             // Current highlighted result
    bool needsUpdate = false;                 // Flag to indicate search needs to be updated
    bool searchChanged = false;               // Flag to indicate search term has changed
    
    // Search options
    bool matchCase = false;                   // Case-sensitive search
    bool matchWholeWord = false;              // Match whole words only      // UI state (mostly unused now - kept for compatibility)
    bool searchBoxFocused = false;            // Whether search box has focus (Win32 handles this)
    double lastInputTime = 0.0;               // Last time user typed in search box
    float searchBoxAlpha = 1.0f;              // Alpha for fade in/out animation (not used)
    bool showMenuBar = false;                 // Disable OpenGL menu bar (using Win32 instead)
    bool showSearchBox = false;               // Disable OpenGL search box (using Win32 instead)
    bool useWin32UI = true;                   // Flag to use Win32 UI instead of OpenGL
    bool autoPopulateFromSelection = true;    // Auto-populate search from text selection
    
    // Enhanced UI state (mostly unused now - kept for compatibility)
    std::string selectedText;                 // Currently selected text to display in search box
    bool showNoMatchMessage = false;          // Whether to show "No match found" message (not used)
    double noMatchMessageTime = 0.0;          // Time when no match message was shown (not used)
    bool isTyping = false;                    // Whether user is currently typing (not used)
    float cursorBlinkTime = 0.0f;             // For cursor animation in search box (not used)
    
    // Search handles for each page (PDFium search contexts)
    std::vector<FPDF_SCHHANDLE> searchHandles;
};

struct PDFScrollState {
    float scrollOffset = 0.0f; // In window coordinates (0 = top)
    float maxOffset = 0.0f;    // Maximum vertical scroll offset
    float pageHeightSum = 0.0f; // Sum of all page heights (in window units)
    float viewportHeight = 1.0f; // Window height (in window units)
    float barWidth = 0.025f;   // Scroll bar width (normalized)
    float barMargin = 0.01f;   // Margin from window edge
    float barColor[4] = {0.7f, 0.7f, 0.7f, 0.8f}; // RGBA
    float barThumbColor[4] = {0.3f, 0.3f, 0.3f, 0.9f}; // RGBA
    float zoomScale = 1.0f;    // Current zoom scale
      // Horizontal scrolling support for cursor-based zoom
    float horizontalOffset = 0.0f; // Horizontal scroll offset (in window coordinates)  
    float maxHorizontalOffset = 0.0f; // Maximum horizontal scroll offset
    float pageWidthMax = 0.0f;  // Maximum page width (in window units)
    float lastCursorX = 0.0f;  // Last cursor X position for zooming
    float lastCursorY = 0.0f;  // Last cursor Y position for zooming
    bool zoomChanged = false;  // Flag to indicate zoom level has changed
    
    // Panning support
    bool isPanning = false;    // Flag to indicate if user is currently panning
    double panStartX = 0.0;    // Mouse X position when panning started
    double panStartY = 0.0;    // Mouse Y position when panning started
    float panStartScrollOffset = 0.0f;      // Scroll offset when panning started (vertical)
    float panStartHorizontalOffset = 0.0f;  // Horizontal offset when panning started
    float lastRenderedZoom = 1.0f; // Track the zoom level at which textures were last rendered
    std::vector<int>* pageHeights = nullptr; // Pointer to page heights for zoom callback
    std::vector<int>* pageWidths = nullptr;  // Pointer to page widths for coordinate calculations
    std::vector<double>* originalPageWidths = nullptr;  // Pointer to original PDF page widths
    std::vector<double>* originalPageHeights = nullptr; // Pointer to original PDF page heights
      // Scroll bar dragging support
    bool isScrollBarDragging = false;    // Flag to indicate if user is dragging scroll bar
    double scrollBarDragStartY = 0.0;    // Mouse Y position when scroll bar drag started
    float scrollBarDragStartOffset = 0.0f; // Scroll offset when drag started
      // Performance optimization flags
    bool immediateRenderRequired = false; // Flag for immediate rendering of visible pages only
    int firstVisiblePage = -1; // Cache for visible page range
    int lastVisiblePage = -1;  // Cache for visible page range
      // Navigation control flag
    bool preventScrollOffsetOverride = false; // Prevent UpdateScrollState from overriding navigation scroll offset
    bool forceRedraw = false; // Force immediate redraw after navigation
    
    // Text selection support
    TextSelection textSelection;              // Current text selection state
    std::vector<TextPageData> textPages;      // Text data for each page
      // Debug mode for text coordinate visualization
    bool debugTextCoordinates = false;       // Enable/disable text coordinate debugging
    
    // Cursor state tracking
    bool isOverText = false;                 // Whether cursor is over text
    bool cursorChanged = false;              // Whether cursor was changed by text selection
    
    // Text search support
    TextSearch textSearch;                    // Text search state
};

void UpdateScrollState(PDFScrollState& state, float winHeight, const std::vector<int>& pageHeights);
void HandleScroll(PDFScrollState& state, float yoffset);
void HandleHorizontalScroll(PDFScrollState& state, float xoffset, float winWidth);
void DrawScrollBar(const PDFScrollState& state);
int GetCurrentPageIndex(const PDFScrollState& state, const std::vector<int>& pageHeights);
void HandleZoom(PDFScrollState& state, float zoomDelta, float cursorX, float cursorY, float winWidth, float winHeight, std::vector<int>& pageHeights, const std::vector<int>& pageWidths);

// Panning functions
void StartPanning(PDFScrollState& state, double mouseX, double mouseY);
void UpdatePanning(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight);
void StopPanning(PDFScrollState& state);

// Scroll bar dragging functions
void StartScrollBarDragging(PDFScrollState& state, double mouseY);
void UpdateScrollBarDragging(PDFScrollState& state, double mouseY, float winHeight);
void StopScrollBarDragging(PDFScrollState& state);

// New helper functions for performance optimization
void GetVisiblePageRange(const PDFScrollState& state, const std::vector<int>& pageHeights, 
                        int& firstVisible, int& lastVisible);
bool IsPageVisible(const PDFScrollState& state, const std::vector<int>& pageHeights, 
                  int pageIndex, float pageTopY, float pageBottomY);

// Text extraction and selection functions
void InitializeTextExtraction(PDFScrollState& state, int pageCount);
void LoadTextPage(PDFScrollState& state, int pageIndex, FPDF_PAGE page);
void UnloadTextPage(PDFScrollState& state, int pageIndex);
void CleanupTextExtraction(PDFScrollState& state);

// Text selection functions
void StartTextSelection(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);
void UpdateTextSelection(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);
void EndTextSelection(PDFScrollState& state);
void ClearTextSelection(PDFScrollState& state);
std::string GetSelectedText(const PDFScrollState& state);

// Double-click text selection functions
bool DetectDoubleClick(PDFScrollState& state, double mouseX, double mouseY, double currentTime);
void SelectWordAtPosition(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);
void FindWordBoundaries(FPDF_TEXTPAGE textPage, int charIndex, int& startChar, int& endChar);

// Helper functions for coordinate transformation
void ScreenToPDFCoordinates(double screenX, double screenY, double& pdfX, double& pdfY, 
                           int pageIndex, float winWidth, float winHeight, 
                           const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);
int GetPageAtScreenPosition(double screenY, const PDFScrollState& state, const std::vector<int>& pageHeights);

// Text highlighting rendering
void DrawTextSelection(const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths, float winWidth, float winHeight);

// Debug text coordinate visualization
void DrawTextCoordinateDebug(const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths, float winWidth, float winHeight);

// Text selection coordinate updates and cursor management
void UpdateTextSelectionCoordinates(PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);
bool CheckMouseOverText(const PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);
void UpdateCursorForTextSelection(PDFScrollState& state, GLFWwindow* window, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);

// Text search functions
void InitializeTextSearch(PDFScrollState& state);
void CleanupTextSearch(PDFScrollState& state);
void ToggleSearchBox(PDFScrollState& state);
void UpdateSearchTerm(PDFScrollState& state, const std::string& term);
void PerformTextSearch(PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths);
void NavigateToNextSearchResult(PDFScrollState& state, const std::vector<int>& pageHeights);
void NavigateToPreviousSearchResult(PDFScrollState& state, const std::vector<int>& pageHeights);
void NavigateToSearchResultPrecise(PDFScrollState& state, const std::vector<int>& pageHeights, int resultIndex);
void ClearSearchResults(PDFScrollState& state);
void DrawSearchResultsHighlighting(const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths, float winWidth, float winHeight);
void HandleSearchInput(PDFScrollState& state, int key, int mods);
void UpdateSearchBoxAnimation(PDFScrollState& state, double currentTime);

// Enhanced search UI functions
void PopulateSearchFromSelection(PDFScrollState& state);

// Deprecated OpenGL UI functions (kept for compatibility, do nothing)
void DrawSearchBox(const PDFScrollState& state, float winWidth, float winHeight);
void DrawSearchMenuBar(const PDFScrollState& state, float winWidth, float winHeight);
bool HandleSearchButtonClick(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight);

extern PDFRenderer pdfRenderer;
