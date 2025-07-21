#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "rendering/pdf-render.h"
#include "core/feature.h"
#include "utils/stb_easy_font.h"
#include "ui/menu-integration.h"
#include "ui/tab-manager.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

// Global variables for menu integration
PDFScrollState* g_scrollState = nullptr;
PDFRenderer* g_renderer = nullptr;
std::vector<int>* g_pageHeights = nullptr;
std::vector<int>* g_pageWidths = nullptr;
MenuIntegration* g_menuIntegration = nullptr;

// Helper to convert PDFium bitmap to OpenGL texture
GLuint CreateTextureFromPDFBitmap(FPDF_BITMAP bitmap, int width, int height) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    void* buffer = FPDFBitmap_GetBuffer(bitmap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}

int APIENTRY WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR     lpCmdLine,
    int       nCmdShow
) {
    // Allocate console for debug output
    AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
    
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1024, 768, "PDF Viewer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    // Get window dimensions for initialization
    int winWidth, winHeight;
    glfwGetFramebufferSize(window, &winWidth, &winHeight);

    // Initialize global pointers (will be set by tab manager when tabs are created)
    g_scrollState = nullptr;
    g_renderer = nullptr;
    g_pageHeights = nullptr;
    g_pageWidths = nullptr;

    // Set up window user pointer for callbacks (will be updated when tabs are active)
    glfwSetWindowUserPointer(window, nullptr);

    // Initialize menu integration with tab support
    g_menuIntegration = new MenuIntegration();
    if (!g_menuIntegration->Initialize(window)) {
        std::cerr << "Failed to initialize menu integration" << std::endl;
        delete g_menuIntegration;
        g_menuIntegration = nullptr;
    } else {
        // Create tabs and search toolbar
        g_menuIntegration->CreateTabsAndSearchToolbar();
        
        // Load demo.pdf in the first tab if it exists
        if (g_tabManager) {
            int demoTabIndex = g_tabManager->CreateNewTab("demo.pdf");
            if (demoTabIndex < 0) {
                std::cout << "Demo PDF not found, starting with empty tab manager" << std::endl;            } else {
                // Update window user pointer to the active tab's scroll state
                glfwSetWindowUserPointer(window, g_scrollState);
            }
        }
    }
    
    // Add window resize callback to handle tab and search toolbar resizing
    glfwSetWindowSizeCallback(window, [](GLFWwindow* win, int width, int height) {
        if (g_menuIntegration) {
            g_menuIntegration->ResizeTabsAndSearchToolbar(width, height);
        }
    });
      // Add cursor position callback for panning, scroll bar dragging, text selection, and cursor tracking
    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
        auto* state = static_cast<PDFScrollState*>(glfwGetWindowUserPointer(win));
        if (!state) return; // No active tab
        
        state->lastCursorX = (float)xpos;
        state->lastCursorY = (float)ypos;
        
        // Get window dimensions for coordinate calculations
        int winWidth, winHeight;
        glfwGetFramebufferSize(win, &winWidth, &winHeight);
          
        // Update cursor appearance for text selection (only when not dragging/panning)
        if (state->pageHeights && state->pageWidths) {
            UpdateCursorForTextSelection(*state, win, xpos, ypos, (float)winWidth, (float)winHeight, *state->pageHeights, *state->pageWidths);
        }
        
        // Handle text selection dragging if currently active
        if (state->textSelection.isDragging && state->pageHeights && state->pageWidths) {
            UpdateTextSelection(*state, xpos, ypos, (float)winWidth, (float)winHeight, *state->pageHeights, *state->pageWidths);
        }
        
        // Handle panning if currently active
        if (state->isPanning) {
            UpdatePanning(*state, xpos, ypos, (float)winWidth, (float)winHeight);
        }
        
        // Handle scroll bar dragging if currently active
        if (state->isScrollBarDragging && state->pageHeights) {
            UpdateScrollBarDragging(*state, ypos, (float)winHeight);
            
            // Update visible page range for progressive rendering during drag
            int newFirst, newLast;
            GetVisiblePageRange(*state, *state->pageHeights, newFirst, newLast);
            if (newFirst != state->firstVisiblePage || newLast != state->lastVisiblePage) {
                state->firstVisiblePage = newFirst;
                state->lastVisiblePage = newLast;
                state->immediateRenderRequired = true;
                state->zoomChanged = true;
            }        }
    });
    
    // Add mouse button callback for panning, scroll bar support, and text selection
    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        auto* state = static_cast<PDFScrollState*>(glfwGetWindowUserPointer(win));
        if (!state) return; // No active tab
        
        double mouseX, mouseY;
        glfwGetCursorPos(win, &mouseX, &mouseY);
        int winWidth = 0, winHeight = 0;
        glfwGetFramebufferSize(win, &winWidth, &winHeight);
        
        // Calculate scroll bar position
        float barMargin = 0.01f * winWidth;
        float barWidth = 0.025f * winWidth;
        float barX = winWidth - barMargin - barWidth;
        bool isOverScrollBar = (mouseX >= barX && mouseX <= winWidth - barMargin);
          if (button == GLFW_MOUSE_BUTTON_LEFT && isOverScrollBar) {
            if (action == GLFW_PRESS) {
                // Start scroll bar dragging
                if (state->pageHeightSum > state->viewportHeight) {
                    StartScrollBarDragging(*state, mouseY);
                    
                    // Also handle immediate click-to-scroll for areas outside the thumb
                    float barY = barMargin;
                    float barH = winHeight - 2 * barMargin;
                    float thumbH = (barH * (state->viewportHeight / state->pageHeightSum) > barMargin * 2) ? 
                                  barH * (state->viewportHeight / state->pageHeightSum) : barMargin * 2;
                    float thumbY = barY + (barH - thumbH) * (state->scrollOffset / state->maxOffset);
                    
                    // If click is outside the thumb, do immediate jump
                    if (mouseY < thumbY || mouseY > thumbY + thumbH) {
                        float relativeY = ((float)mouseY - barY) / barH;
                        float newScrollOffset = relativeY * state->maxOffset;
                        
                        // Clamp to valid range
                        if (newScrollOffset < 0.0f) newScrollOffset = 0.0f;
                        if (newScrollOffset > state->maxOffset) newScrollOffset = state->maxOffset;
                        
                        state->scrollOffset = newScrollOffset;
                        
                        // Update the drag start position for potential dragging
                        state->scrollBarDragStartOffset = state->scrollOffset;
                    }
                      // Update visible page range for progressive rendering
                    if (state->pageHeights) {
                        int newFirst, newLast;
                        GetVisiblePageRange(*state, *state->pageHeights, newFirst, newLast);
                        if (newFirst != state->firstVisiblePage || newLast != state->lastVisiblePage) {
                            state->firstVisiblePage = newFirst;
                            state->lastVisiblePage = newLast;
                            state->immediateRenderRequired = true;
                            state->zoomChanged = true;
                        }
                    }
                }
            } else if (action == GLFW_RELEASE) {
                // Stop scroll bar dragging
                StopScrollBarDragging(*state);
            }
        } else if (button == GLFW_MOUSE_BUTTON_LEFT && !isOverScrollBar) {            // Handle text selection when left clicking outside scroll bar
            if (action == GLFW_PRESS) {
                // Get current time for double-click detection
                double currentTime = glfwGetTime();
                  // Check for double-click
                if (DetectDoubleClick(*state, mouseX, mouseY, currentTime)) {
                    // Double-click detected - select word at position
                    if (state->pageHeights && state->pageWidths) {
                        SelectWordAtPosition(*state, mouseX, mouseY, (float)winWidth, (float)winHeight, *state->pageHeights, *state->pageWidths);
                    }
                } else {
                    // Single click - start normal text selection (but don't populate search yet)
                    if (state->pageHeights && state->pageWidths) {
                        StartTextSelection(*state, mouseX, mouseY, (float)winWidth, (float)winHeight, *state->pageHeights, *state->pageWidths);
                    }
                }
            } else if (action == GLFW_RELEASE) {
                // End text selection (only if it was a drag selection, not double-click)
                if (!state->textSelection.isDoubleClick) {
                    EndTextSelection(*state);
                    
                    // If we have selected text and search box is not visible, 
                    // optionally populate search box with selected text
                    if (state->textSelection.isActive && !state->textSearch.isSearchBoxVisible) {
                        // This is optional - you could enable this for automatic search population
                        // PopulateSearchFromSelection(*state);
                    }
                } else {
                    // For double-click selections, manually trigger the search population
                    EndTextSelection(*state);
                }
                // Reset double-click flag
                state->textSelection.isDoubleClick = false;
            }
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                // Start panning when right mouse button is pressed
                StartPanning(*state, mouseX, mouseY);
                
                // Set cursor to hand symbol for panning
                glfwSetCursor(win, glfwCreateStandardCursor(GLFW_HAND_CURSOR));
            } else if (action == GLFW_RELEASE) {
                // Stop panning when right mouse button is released
                StopPanning(*state);
                
                // Restore default cursor                glfwSetCursor(win, nullptr);
            }
        }
    });
      // Add keyboard callback for text operations and search
    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        auto* state = static_cast<PDFScrollState*>(glfwGetWindowUserPointer(win));
        if (!state) return; // No active tab
        
        if (action == GLFW_PRESS) {
            // Search is always active now, so handle search input for typing
            if (key >= 32 && key <= 126) { // Printable ASCII characters
                HandleSearchInput(*state, key, mods);
                return;
            }
            // Handle backspace for search
            else if (key == GLFW_KEY_BACKSPACE) {
                HandleSearchInput(*state, key, mods);
                return;  
            }// Handle F3 for search navigation (next result)
            else if (key == GLFW_KEY_F3) {
                if (mods & GLFW_MOD_SHIFT) {
                    if (state->pageHeights) {
                        NavigateToPreviousSearchResult(*state, *state->pageHeights);
                    }
                } else {
                    if (state->pageHeights) {
                        NavigateToNextSearchResult(*state, *state->pageHeights);
                    }
                }
                return;
            }
            // Handle Ctrl+C for copying selected text
            else if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
                std::string selectedText = GetSelectedText(*state);
                if (!selectedText.empty()) {
                    // Copy to clipboard (simplified - you may want to use a proper clipboard library)
                    glfwSetClipboardString(win, selectedText.c_str());
                }
                return;
            }
            // Handle Ctrl+Shift+F to populate search with selected text
            else if (key == GLFW_KEY_F && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT)) {
                PopulateSearchFromSelection(*state);
                return;
            }
            // Handle Escape to clear selection (search is always visible now)
            else if (key == GLFW_KEY_ESCAPE) {
                ClearTextSelection(*state);
                return;
            }
            // Handle F1 to toggle text coordinate debug mode
            else if (key == GLFW_KEY_F1) {
                state->debugTextCoordinates = !state->debugTextCoordinates;
                return;
            }
        }
    });
      // Add scroll callback - Mouse wheel now always zooms (no Ctrl required)
    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        auto* state = static_cast<PDFScrollState*>(glfwGetWindowUserPointer(win));
        if (!state) return; // No active tab
        
        // Check if cursor is over the scroll bar area (right side of window)
        double cursorX, cursorY;
        glfwGetCursorPos(win, &cursorX, &cursorY);
        int winWidth = 0, winHeight = 0;
        glfwGetFramebufferSize(win, &winWidth, &winHeight);
        
        // Calculate scroll bar position (same as in DrawScrollBar function)
        float barMargin = 0.01f * winWidth;  // Convert normalized margin to pixels
        float barWidth = 0.025f * winWidth;  // Convert normalized width to pixels
        float barX = winWidth - barMargin - barWidth;
        
        // If cursor is over scroll bar area, disable mouse wheel input
        bool isOverScrollBar = (cursorX >= barX && cursorX <= winWidth - barMargin);
        
        if (isOverScrollBar) {
            // When over scroll bar, disable mouse wheel input - use scroll bar instead
            return;
        }
          // Mouse wheel input handling (when NOT over scroll bar)
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
            // Horizontal scrolling with Shift + Mouse Wheel (still available)
            float horizontalSpeed = state->viewportHeight * 0.1f;
            state->horizontalOffset += (float)yoffset * horizontalSpeed;
            
            // Clamp horizontal offset to valid range
            if (state->horizontalOffset < 0.0f) state->horizontalOffset = 0.0f;
            if (state->horizontalOffset > state->maxHorizontalOffset) state->horizontalOffset = state->maxHorizontalOffset;        } else {
            // DEFAULT: Mouse wheel always zooms (no Ctrl key required)
            // Get real-time cursor position at the moment of zoom
            if (state->pageHeights && state->pageWidths) {
                HandleZoom(*state, yoffset > 0 ? 1.1f : 1.0f / 1.1f, 
                          (float)cursorX, (float)cursorY, 
                          (float)winWidth, (float)winHeight, 
                          *state->pageHeights, *state->pageWidths);
            }
        }
    });
      // Main loop
    int lastWinWidth = winWidth, lastWinHeight = winHeight;    std::cout << "=== Starting main loop ===" << std::endl;
      while (!glfwWindowShouldClose(window)) {
        // Check if window should close and log why
        if (glfwWindowShouldClose(window)) {
            std::cout << "=== WINDOW SHOULD CLOSE - exiting main loop ===" << std::endl;
            break;
        }
        glfwGetFramebufferSize(window, &winWidth, &winHeight);
        
        // Get the active tab and its data
        PDFTab* activeTab = g_tabManager ? g_tabManager->GetActiveTab() : nullptr;if (!activeTab || !activeTab->isLoaded) {
            // No active tab or tab not loaded, show "No PDF opened" message
            glClear(GL_COLOR_BUFFER_BIT);
            
            // Render "No PDF opened" text in the center of the window
            if (g_tabManager && g_tabManager->GetTabCount() == 0) {
                const char* message = "No PDF opened yet. Use File -> Open to open a PDF.";
                
                // Set up OpenGL for text rendering
                glMatrixMode(GL_PROJECTION);
                glPushMatrix();
                glLoadIdentity();
                glOrtho(0, winWidth, winHeight, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();
                
                // Calculate text position (center of screen)
                float textX = winWidth / 2.0f - 200.0f; // Approximate centering
                float textY = winHeight / 2.0f;
                  // Render text using stb_easy_font
                static char stbFontBuffer[9999];
                int quads = stb_easy_font_print(textX, textY, (char*)message, NULL, stbFontBuffer, sizeof(stbFontBuffer));
                
                glColor3f(0.5f, 0.5f, 0.5f); // Gray color
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(2, GL_FLOAT, 16, stbFontBuffer);
                glDrawArrays(GL_QUADS, 0, quads * 4);
                glDisableClientState(GL_VERTEX_ARRAY);
                
                // Restore matrices
                glPopMatrix();
                glMatrixMode(GL_PROJECTION);
                glPopMatrix();
                glMatrixMode(GL_MODELVIEW);
            }
            
            glfwSwapBuffers(window);
            glfwPollEvents();
            continue;
        }
        
        // Update global pointers to active tab data
        g_scrollState = &activeTab->scrollState;
        g_renderer = activeTab->renderer.get();
        g_pageHeights = &activeTab->pageHeights;
        g_pageWidths = &activeTab->pageWidths;
        glfwSetWindowUserPointer(window, g_scrollState);
        
        PDFScrollState& scrollState = activeTab->scrollState;
        std::vector<GLuint>& textures = activeTab->textures;
        std::vector<int>& pageHeights = activeTab->pageHeights;
        std::vector<int>& pageWidths = activeTab->pageWidths;
        PDFRenderer& renderer = *activeTab->renderer;
        int pageCount = static_cast<int>(textures.size());
        
        // Check if navigation occurred and force immediate redraw
        if (scrollState.forceRedraw) {
            scrollState.forceRedraw = false;
            // Force immediate buffer swap to show navigation
            glfwPollEvents(); // Process any pending events
        }
        
        // Check if we need to regenerate textures due to window resize or zoom change
        bool needsFullRegeneration = (winWidth != lastWinWidth || winHeight != lastWinHeight);
        bool needsVisibleRegeneration = false;
        
        if (scrollState.zoomChanged) {
            float zoomDifference = std::abs(scrollState.zoomScale - scrollState.lastRenderedZoom) / scrollState.lastRenderedZoom;
            
            // For immediate responsive zoom, regenerate visible pages with lower threshold (1%)
            if (scrollState.immediateRenderRequired && zoomDifference > 0.01f) {
                needsVisibleRegeneration = true;
                scrollState.immediateRenderRequired = false;
            }
            // For full regeneration, use higher threshold (3%) to avoid too frequent full regens
            else if (zoomDifference > 0.03f) {
                needsFullRegeneration = true;
                scrollState.lastRenderedZoom = scrollState.zoomScale;
            }
            scrollState.zoomChanged = false; // Reset the flag
        }
        
        // Handle visible page regeneration for responsive zoom
        if (needsVisibleRegeneration && !needsFullRegeneration) {
            // Get current visible page range
            int firstVisible, lastVisible;
            GetVisiblePageRange(scrollState, pageHeights, firstVisible, lastVisible);
            
            // Only regenerate textures for visible pages
            for (int i = firstVisible; i <= lastVisible && i < pageCount; ++i) {
                if (textures[i]) glDeleteTextures(1, &textures[i]);
                
                int pageW = 0, pageH = 0;
                float effectiveZoom = (scrollState.zoomScale > 0.5f) ? scrollState.zoomScale : 0.5f;
                renderer.GetBestFitSize(i, static_cast<int>(winWidth * effectiveZoom), 
                                       static_cast<int>(winHeight * effectiveZoom), pageW, pageH);
                FPDF_BITMAP bmp = renderer.RenderPageToBitmap(i, pageW, pageH);
                textures[i] = CreateTextureFromPDFBitmap(bmp, pageW, pageH);
                FPDFBitmap_Destroy(bmp);
            }
        }
        
        // Handle full regeneration for window resize or major zoom changes
        if (needsFullRegeneration) {
            lastWinWidth = winWidth;
            lastWinHeight = winHeight;
            
            // Recompute and re-render all pages at current zoom level
            for (GLuint tex : textures) if (tex) glDeleteTextures(1, &tex);
            textures.clear(); pageWidths.clear(); pageHeights.clear();
            textures.resize(pageCount); pageWidths.resize(pageCount); pageHeights.resize(pageCount);
            for (int i = 0; i < pageCount; ++i) {
                int pageW = 0, pageH = 0;
                // Apply zoom scale to get higher resolution textures
                float effectiveZoom = (scrollState.zoomScale > 0.5f) ? scrollState.zoomScale : 0.5f;
                renderer.GetBestFitSize(i, static_cast<int>(winWidth * effectiveZoom), 
                                       static_cast<int>(winHeight * effectiveZoom), pageW, pageH);
                FPDF_BITMAP bmp = renderer.RenderPageToBitmap(i, pageW, pageH);
                textures[i] = CreateTextureFromPDFBitmap(bmp, pageW, pageH);
                // Store the base page dimensions (without zoom) for layout calculations
                int baseW = 0, baseH = 0;
                renderer.GetBestFitSize(i, winWidth, winHeight, baseW, baseH);
                pageWidths[i] = baseW;
                pageHeights[i] = baseH;
                FPDFBitmap_Destroy(bmp);
            }            UpdateScrollState(scrollState, (float)winHeight, pageHeights);
            scrollState.lastRenderedZoom = scrollState.zoomScale;
        }
          // Update text selection coordinates if zoom/pan changed
        UpdateTextSelectionCoordinates(scrollState, pageHeights, pageWidths);
        
        glViewport(0, 0, winWidth, winHeight);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);// Draw all pages stacked vertically, top-aligned, with scroll and zoom
        float yOffset = -scrollState.scrollOffset;
        for (int i = 0; i < pageCount; ++i) {
            float pageW = (float)pageWidths[i] * scrollState.zoomScale;
            float pageH = (float)pageHeights[i] * scrollState.zoomScale;
            float xScale = pageW / winWidth;
            float yScale = pageH / winHeight;
            float yCenter = yOffset + pageH / 2.0f;            // APPLY HORIZONTAL OFFSET for cursor-based zoom support
            // horizontalOffset represents how much the document is scrolled to the left
            // When horizontalOffset > 0, we want to shift the page left to show more right content
            // In window coordinates: larger horizontalOffset = page shifts left = smaller xCenter
            float xCenter = (winWidth / 2.0f) - scrollState.horizontalOffset;
            float xNDC = (xCenter / winWidth) * 2.0f - 1.0f;
            
            float yNDC = 1.0f - (yCenter / winHeight) * 2.0f;
            float halfX = xScale;
            float halfY = yScale;
            
            // Apply horizontal offset to vertex positions
            float leftX = xNDC - halfX;
            float rightX = xNDC + halfX;
            
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(leftX, yNDC - halfY);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(rightX, yNDC - halfY);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(rightX, yNDC + halfY);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(leftX, yNDC + halfY);
            glEnd();
            yOffset += pageH;
        }        glBindTexture(GL_TEXTURE_2D, 0);        // Draw text selection highlighting (blue for normal text selection)
        DrawTextSelection(scrollState, pageHeights, pageWidths, (float)winWidth, (float)winHeight);
        
        // Draw search results highlighting (yellow for all results, orange for current result)
        DrawSearchResultsHighlighting(scrollState, pageHeights, pageWidths, (float)winWidth, (float)winHeight);
        
        // Note: Search UI is handled by Win32 toolbar, search highlighting is handled by OpenGL
          // Draw text coordinate debug visualization (if enabled)
        DrawTextCoordinateDebug(scrollState, pageHeights, pageWidths, (float)winWidth, (float)winHeight);
          // Draw scroll bar overlay 
        DrawScrollBar(scrollState);
        
        // Draw floating text with total rendered pages, zoom scale, and selected text content on the left side
        char pageCountText[256];
        if (scrollState.textSelection.isActive) {
            // Get the selected text content
            std::string selectedText = GetSelectedText(scrollState);
            
            if (!selectedText.empty()) {
                // Truncate text if too long for display (limit to ~60 characters)
                std::string displayText = selectedText;
                if (displayText.length() > 60) {
                    displayText = displayText.substr(0, 57) + "...";
                }
                
                // Replace newlines and multiple spaces with single spaces for display
                for (size_t i = 0; i < displayText.length(); i++) {
                    if (displayText[i] == '\n' || displayText[i] == '\r' || displayText[i] == '\t') {
                        displayText[i] = ' ';
                    }
                }
                
                // Show selected text content
                snprintf(pageCountText, sizeof(pageCountText), "Pages: %d  Zoom: %.2fx | Selected: \"%s\"", 
                        pageCount, scrollState.zoomScale, displayText.c_str());
            } else {
                // Selection active but no text extracted yet
                snprintf(pageCountText, sizeof(pageCountText), "Pages: %d  Zoom: %.2fx | Selecting...", 
                        pageCount, scrollState.zoomScale);
            }
        } else {
            // No selection active - show standard display
            snprintf(pageCountText, sizeof(pageCountText), "Pages: %d  Zoom: %.2fx", pageCount, scrollState.zoomScale);
        }
        float x = -1.0f + 0.03f; // left side, slight margin
        float y = 0.95f; // near top
        glColor4f(0.1f, 0.1f, 0.1f, 0.85f); // dark text, semi-opaque
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPushMatrix();
        glLoadIdentity();
        glTranslatef(x, y, 0);
        glScalef(0.0018f, -0.0025f, 1.0f); // scale for NDC
        static char stbFontBuffer[9999]; // Declare buffer for stb_easy_font
        int quads = stb_easy_font_print(0, 0, pageCountText, NULL, stbFontBuffer, sizeof(stbFontBuffer));
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 16, stbFontBuffer);
        glDrawArrays(GL_QUADS, 0, (GLsizei)(4 * quads));
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopMatrix();        glDisable(GL_BLEND);
        glColor4f(1,1,1,1);
        
        // Update search functionality
        double currentTime = glfwGetTime();
        UpdateSearchBoxAnimation(scrollState, currentTime);
          // Perform search if needed (after user stops typing)
        if (scrollState.textSearch.needsUpdate && 
            scrollState.textSearch.lastInputTime > 0.0 &&
            (currentTime - scrollState.textSearch.lastInputTime) > 0.3) { // 300ms delay
            std::cout << "Main loop: Performing search for '" << scrollState.textSearch.searchTerm << "'" << std::endl;
            PerformTextSearch(scrollState, pageHeights, pageWidths);
            scrollState.textSearch.needsUpdate = false;
            std::cout << "Main loop: Search completed, found " << scrollState.textSearch.results.size() << " results" << std::endl;
            
            // Update Win32 toolbar after search
            if (g_menuIntegration) {
                std::cout << "Main loop: Updating Win32 toolbar after search" << std::endl;
                g_menuIntegration->UpdateSearchToolbar();
            }
        }
        
        glfwSwapBuffers(window);
        glfwPollEvents();
        
        // Progressive background rendering for better performance
        // Render non-visible pages at lower resolution when idle
        static int backgroundRenderIndex = 0;
        static int frameCounter = 0;
        frameCounter++;
        
        // Every 5 frames, render one non-visible page at lower resolution
        if (frameCounter % 5 == 0 && !needsFullRegeneration && !needsVisibleRegeneration) {
            int firstVisible, lastVisible;
            GetVisiblePageRange(scrollState, pageHeights, firstVisible, lastVisible);
            
            // Find next non-visible page to render
            for (int attempts = 0; attempts < pageCount; attempts++) {
                backgroundRenderIndex = (backgroundRenderIndex + 1) % pageCount;
                
                // Skip if this page is currently visible
                if (backgroundRenderIndex >= firstVisible && backgroundRenderIndex <= lastVisible) continue;
                
                // Render at lower resolution for better performance
                if (textures[backgroundRenderIndex]) glDeleteTextures(1, &textures[backgroundRenderIndex]);
                
                int pageW = 0, pageH = 0;
                float backgroundZoom = scrollState.zoomScale * 0.7f; // Lower res for background
                backgroundZoom = (backgroundZoom > 0.3f) ? backgroundZoom : 0.3f;
                renderer.GetBestFitSize(backgroundRenderIndex, static_cast<int>(winWidth * backgroundZoom), 
                                       static_cast<int>(winHeight * backgroundZoom), pageW, pageH);
                FPDF_BITMAP bmp = renderer.RenderPageToBitmap(backgroundRenderIndex, pageW, pageH);
                textures[backgroundRenderIndex] = CreateTextureFromPDFBitmap(bmp, pageW, pageH);
                FPDFBitmap_Destroy(bmp);
                break; // Only render one page per frame for smooth performance
            }        }
    }
    // Cleanup tab manager
    if (g_tabManager) {
        delete g_tabManager;
        g_tabManager = nullptr;
    }
    
    // Cleanup menu integration
    if (g_menuIntegration) {
        delete g_menuIntegration;
        g_menuIntegration = nullptr;
    }
    
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
