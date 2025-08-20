#include "core/feature.h"
#include "rendering/pdf-render.h"
#include "fpdf_text.h"
#include "utils/stb_easy_font.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "ui/menu-integration.h"
#include <algorithm>
#include <cmath>
#include <string>

// Forward declarations
float GetVisiblePageMaxWidth(const PDFScrollState& state, const std::vector<int>& pageHeights);
#include <iostream> // For debug output

// Resolve Windows macro conflicts with std::min/std::max
#undef max
#undef min

// External references
extern MenuIntegration* g_menuIntegration;

void UpdateScrollState(PDFScrollState& state, float winHeight, const std::vector<int>& pageHeights) {
    state.viewportHeight = winHeight;
    state.pageHeightSum = 0.0f;
    for (int h : pageHeights) state.pageHeightSum += (float)h * state.zoomScale;
    
    // Add bottom padding to ensure last page content is fully visible
    float bottomPadding = winHeight * 0.1f; // 10% of viewport height as bottom padding
    state.maxOffset = std::max(0.0f, state.pageHeightSum - winHeight + bottomPadding);
    
    // Only clamp scroll offset if not preventing override (e.g., during navigation)
    if (!state.preventScrollOffsetOverride) {
        if (state.scrollOffset > state.maxOffset) state.scrollOffset = state.maxOffset;
        if (state.scrollOffset < 0.0f) state.scrollOffset = 0.0f;
    } else {
        // Reset the flag after respecting it once
        state.preventScrollOffsetOverride = false;
    }
}

void HandleScroll(PDFScrollState& state, float yoffset) {
    // yoffset: positive = scroll up, negative = scroll down
    
    // Only allow scrolling if content exceeds viewport height
    if (state.pageHeightSum > state.viewportHeight) {
        state.scrollOffset -= yoffset * (state.viewportHeight * 0.1f);
        if (state.scrollOffset < 0.0f) state.scrollOffset = 0.0f;
        if (state.scrollOffset > state.maxOffset) state.scrollOffset = state.maxOffset;
    }
    // If content fits in viewport, keep it centered (no scrolling)
}

void HandleHorizontalScroll(PDFScrollState& state, float xoffset, float winWidth) {
    // xoffset: positive = scroll right, negative = scroll left

    // Only allow horizontal scrolling if content exceeds viewport width
    // NOTE: state.pageWidthMax is already in screen units at the current zoom
    if (state.pageWidthMax > winWidth) {
        state.horizontalOffset += xoffset * (winWidth * 0.1f);
        if (state.horizontalOffset < 0.0f) state.horizontalOffset = 0.0f;
        if (state.horizontalOffset > state.maxHorizontalOffset) state.horizontalOffset = state.maxHorizontalOffset;
    }
    // If content fits in viewport, keep it centered (no horizontal scrolling)
}

void DrawScrollBar(const PDFScrollState& state) {
    if (state.pageHeightSum <= state.viewportHeight) return; // No need for scroll bar
    
    // Use dynamic margins based on viewport size to ensure scroll bar is always visible
    float dynamicMargin = std::min(state.barMargin, state.viewportHeight * 0.01f);
    float barX = 1.0f - dynamicMargin - state.barWidth;
    float barY = -1.0f + dynamicMargin;
    float barH = 2.0f - 2 * dynamicMargin;
    
    // Ensure minimum scroll bar height and proper thumb sizing
    float thumbH = std::max(barH * (state.viewportHeight / state.pageHeightSum), 0.05f);
    
    // Fix scroll bar position calculation to handle the new maxOffset with padding
    float scrollRatio = (state.maxOffset > 0.0f) ? (state.scrollOffset / state.maxOffset) : 0.0f;
    // FIXED: Invert the scroll ratio to fix the opposite movement direction
    // When scrollOffset = 0 (top of document), thumb should be at top of scroll bar (barY + barH - thumbH)
    // When scrollOffset = maxOffset (bottom of document), thumb should be at bottom of scroll bar (barY)
    float thumbY = barY + (barH - thumbH) * (1.0f - scrollRatio);
    
    // Draw scroll bar background
    glColor4fv(state.barColor);
    glBegin(GL_QUADS);
    glVertex2f(barX, barY);
    glVertex2f(barX + state.barWidth, barY);
    glVertex2f(barX + state.barWidth, barY + barH);
    glVertex2f(barX, barY + barH);
    glEnd();
    
    // Draw scroll bar thumb
    glColor4fv(state.barThumbColor);
    glBegin(GL_QUADS);
    glVertex2f(barX, thumbY);
    glVertex2f(barX + state.barWidth, thumbY);
    glVertex2f(barX + state.barWidth, thumbY + thumbH);
    glVertex2f(barX, thumbY + thumbH);
    glEnd();
    glColor4f(1,1,1,1);
}

int GetCurrentPageIndex(const PDFScrollState& state, const std::vector<int>& pageHeights) {
    float centerY = state.scrollOffset + state.viewportHeight / 2.0f;
    float y = 0.0f;
    for (int i = 0; i < (int)pageHeights.size(); ++i) {
        float nextY = y + pageHeights[i] * state.zoomScale;
        if (centerY >= y && centerY < nextY) return i;
        y = nextY;
    }
    return (int)pageHeights.size() - 1;
}

void GetVisiblePageRange(const PDFScrollState& state, const std::vector<int>& pageHeights, 
                        int& firstVisible, int& lastVisible) {
    firstVisible = -1;
    lastVisible = -1;
    
    float viewTop = state.scrollOffset;
    float viewBottom = state.scrollOffset + state.viewportHeight;
    
    float yOffset = 0.0f;
    for (int i = 0; i < (int)pageHeights.size(); ++i) {
        float pageHeight = pageHeights[i] * state.zoomScale;
        float pageTop = yOffset;
        float pageBottom = yOffset + pageHeight;
        
        // Check if page is visible (with small buffer for smooth scrolling)
        float buffer = pageHeight * 0.1f; // 10% buffer
        if (pageBottom + buffer >= viewTop && pageTop - buffer <= viewBottom) {
            if (firstVisible == -1) firstVisible = i;
            lastVisible = i;
        }
        
        yOffset += pageHeight;
    }
    
    // Ensure we have at least some pages to render
    if (firstVisible == -1) {
        firstVisible = 0;
        int maxPage = (int)pageHeights.size() - 1;
        lastVisible = (2 < maxPage) ? 2 : maxPage;
    }
}

// Helper function to get the maximum width of currently visible pages
float GetVisiblePageMaxWidth(const PDFScrollState& state, const std::vector<int>& pageHeights) {
    if (!state.pageWidths || state.pageWidths->empty()) {
        return state.pageWidthMax; // Fallback to existing logic
    }
    
    int firstVisible, lastVisible;
    GetVisiblePageRange(state, pageHeights, firstVisible, lastVisible);
    
    float maxVisibleWidth = 0.0f;
    for (int i = firstVisible; i <= lastVisible && i < (int)state.pageWidths->size(); ++i) {
        float pageWidth = (*state.pageWidths)[i] * state.zoomScale;
        if (pageWidth > maxVisibleWidth) {
            maxVisibleWidth = pageWidth;
        }
    }
    
    // If no visible pages found, fallback to the widest page overall
    return maxVisibleWidth > 0.0f ? maxVisibleWidth : state.pageWidthMax;
}

bool IsPageVisible(const PDFScrollState& state, const std::vector<int>& pageHeights, 
    int pageIndex, float pageTopY, float pageBottomY) {
    float viewTop = state.scrollOffset;
    float viewBottom = state.scrollOffset + state.viewportHeight;
      // Add small buffer for smooth transitions
    float buffer = pageHeights[pageIndex] * state.zoomScale * 0.05f;
    return (pageBottomY + buffer >= viewTop && pageTopY - buffer <= viewBottom);
}

void HandleZoom(PDFScrollState& state, float zoomDelta, float cursorX, float cursorY, float winWidth, float winHeight, std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    // Normalize delta so mouse wheel steps feel consistent across scales
    // Expectation: zoomDelta ~ 1.2 for wheel up, ~ 1/1.2 for wheel down
    float oldZoom = state.zoomScale;
    // Bound the instantaneous delta to avoid extreme jumps or sluggishness
    float boundedDelta = std::clamp(zoomDelta, 0.8f, 1.25f);
    state.zoomScale *= boundedDelta;
    // Apply global zoom bounds
    state.zoomScale = std::clamp(state.zoomScale, 0.35f, 15.0f);
    float zoomRatio = state.zoomScale / oldZoom;

    // Reduce threshold so tiny deltas don't trigger heavy work, but keep it responsive
    if (std::abs(zoomRatio - 1.0f) < 0.0002f) return;

    // ENHANCED CURSOR-BASED ZOOM: Uses viewport-aligned coordinate system
    // This implementation uses the exact same coordinate transformation pipeline
    // as the rendering system to ensure pixel-perfect cursor tracking
    
    // Step 1: Find which page is under the cursor using the same logic as text selection
    int cursorPageIndex = -1;
    float cursorPageY = 0.0f;
    float accumulatedY = -state.scrollOffset;
    
    for (int i = 0; i < (int)pageHeights.size(); ++i) {
        float pageH = (float)pageHeights[i] * oldZoom; // Use old zoom for current positions
        float pageTop = accumulatedY;
        float pageBottom = accumulatedY + pageH;
        
        if (cursorY >= pageTop && cursorY <= pageBottom) {
            cursorPageIndex = i;
            cursorPageY = cursorY - pageTop; // Position within the page
            break;
        }
        accumulatedY += pageH;
    }
    
    // Step 2: If cursor is over a page, calculate precise document coordinates
    float targetDocumentX = 0.0f;
    float targetDocumentY = 0.0f;
    bool hasValidTarget = false;
    
    if (cursorPageIndex >= 0 && state.pageWidths && cursorPageIndex < (int)state.pageWidths->size()) {
        // Use exact same coordinate transformation as ScreenToPDFCoordinates and rendering
        
        // Calculate page position at OLD zoom level (current state)
        float yOffset = -state.scrollOffset;
        for (int i = 0; i < cursorPageIndex; i++) {
            float pageH = (float)pageHeights[i] * oldZoom;
            yOffset += pageH;
        }
        
        // Calculate page dimensions at OLD zoom
        float pageW = (float)(*state.pageWidths)[cursorPageIndex] * oldZoom;
        float pageH = (float)pageHeights[cursorPageIndex] * oldZoom;
        float xScale = pageW / winWidth;
        float yScale = pageH / winHeight;
        
        // Calculate page center position (same as rendering loop)
        float yCenter = yOffset + pageH / 2.0f;
        float xCenter = (winWidth / 2.0f) - state.horizontalOffset;
        
        // Convert to NDC coordinates (same as rendering loop)
        float xNDC = (xCenter / winWidth) * 2.0f - 1.0f;
        float yNDC = 1.0f - (yCenter / winHeight) * 2.0f;
        float halfX = xScale;
        float halfY = yScale;
        
        // Calculate page bounds in NDC
        float leftX = xNDC - halfX;
        float rightX = xNDC + halfX;
        float topY = yNDC + halfY;
        float bottomY = yNDC - halfY;
        
        // Convert cursor coordinates to NDC
        float cursorNDC_X = (cursorX / winWidth) * 2.0f - 1.0f;
        float cursorNDC_Y = 1.0f - (cursorY / winHeight) * 2.0f;
        
        // Convert NDC to page-relative coordinates (0 to 1)
        if (rightX != leftX && topY != bottomY) { // Avoid division by zero
            float pageRelativeX = (cursorNDC_X - leftX) / (rightX - leftX);
            float pageRelativeY = (cursorNDC_Y - bottomY) / (topY - bottomY);
              // Convert to document coordinates (this is our zoom target)
            targetDocumentX = pageRelativeX * (float)(*state.pageWidths)[cursorPageIndex];
            targetDocumentY = (1.0f - pageRelativeY) * (float)pageHeights[cursorPageIndex]; // Y-flip for PDF coordinates
            hasValidTarget = true;
        }
    }
    
    // Step 3: Apply zoom and calculate new offsets to maintain cursor position
    float newHorizontalOffset = state.horizontalOffset;
    float newScrollOffset = state.scrollOffset;    
    if (hasValidTarget && cursorPageIndex >= 0) {
        // Calculate what the new page position will be after zoom
        float newYOffset = -newScrollOffset; // Will be calculated iteratively
        for (int i = 0; i < cursorPageIndex; i++) {
            float pageH = (float)pageHeights[i] * state.zoomScale;
            newYOffset += pageH;
        }
        
        // Calculate NEW page dimensions at NEW zoom
        float newPageW = (float)(*state.pageWidths)[cursorPageIndex] * state.zoomScale;
        float newPageH = (float)pageHeights[cursorPageIndex] * state.zoomScale;
        float newXScale = newPageW / winWidth;
        float newYScale = newPageH / winHeight;
        
        // We want the cursor to point to the same document coordinates
        // Work backwards from document coordinates to screen position
        
        // Convert document coordinates back to page-relative (0 to 1)
        float newPageRelativeX = targetDocumentX / (float)(*state.pageWidths)[cursorPageIndex];
        float newPageRelativeY = 1.0f - (targetDocumentY / (float)pageHeights[cursorPageIndex]); // Y-flip
        
        // Calculate where the page center should be to achieve the desired cursor position
        // We need to solve for the new offsets such that:
        // cursorNDC_X lies at newPageRelativeX within the page bounds
        // cursorNDC_Y lies at newPageRelativeY within the page bounds
        
        float cursorNDC_X = (cursorX / winWidth) * 2.0f - 1.0f;
        float cursorNDC_Y = 1.0f - (cursorY / winHeight) * 2.0f;
        
        // Calculate required page center position in NDC
        // cursorNDC_X = leftPageX + newPageRelativeX * (rightPageX - leftPageX)
        // cursorNDC_X = (xNDC - newXScale) + newPageRelativeX * (2 * newXScale)
        // Solving for xNDC:
        float requiredXNDC = cursorNDC_X - newPageRelativeX * 2.0f * newXScale + newXScale;
        
        // Similarly for Y:
        float requiredYNDC = cursorNDC_Y - newPageRelativeY * 2.0f * newYScale + newYScale;
        
        // Convert required NDC back to center coordinates
        // xNDC = (xCenter / winWidth) * 2.0f - 1.0f
        // Solving for xCenter:
        float requiredXCenter = (requiredXNDC + 1.0f) * winWidth / 2.0f;
        float requiredYCenter = (1.0f - requiredYNDC) * winHeight / 2.0f;
        
        // Calculate required horizontal offset
        // xCenter = (winWidth / 2.0f) - horizontalOffset
        // Solving for horizontalOffset:
        newHorizontalOffset = (winWidth / 2.0f) - requiredXCenter;
        
        // Calculate required scroll offset
        // yCenter = yOffset + pageH / 2.0f = -scrollOffset + (pages before) + pageH / 2.0f
        // requiredYCenter = -scrollOffset + yOffsetFromPages + newPageH / 2.0f
        // Solving for scrollOffset:
        float yOffsetFromPages = 0.0f;
        for (int i = 0; i < cursorPageIndex; i++) {
            yOffsetFromPages += (float)pageHeights[i] * state.zoomScale;
        }
        newScrollOffset = -(requiredYCenter - yOffsetFromPages - newPageH / 2.0f);
    }
    
    // Step 4: Apply calculated offsets with proper content boundary constraints
    // CRITICAL FIX: Calculate the actual content dimensions at NEW zoom level for boundary checking
    // BEFORE: Used old state.pageHeightSum which caused incorrect boundary calculations
    float zoomedPageHeightSum = 0.0f;
    for (int i = 0; i < (int)pageHeights.size(); ++i) {
        zoomedPageHeightSum += pageHeights[i] * state.zoomScale;  // Use NEW zoomScale, not old
    }
    float zoomedPageWidthMax = GetVisiblePageMaxWidth(state, pageHeights);
    
    // Calculate proper max offset with bottom padding
    float bottomPadding = winHeight * 0.1f; // 10% of viewport height as bottom padding
    float calculatedMaxOffset = std::max(0.0f, zoomedPageHeightSum - winHeight + bottomPadding);
    
    // Apply zoom-aware boundary constraints
    if (state.zoomScale <= 1.0f) {
        // ZOOM OUT MODE: Use standard boundary constraints
        // Vertical constraints
        if (zoomedPageHeightSum > winHeight) {
            float minVerticalOffset = 0.0f;
            float maxVerticalOffset = calculatedMaxOffset;
            if (newScrollOffset < minVerticalOffset) newScrollOffset = minVerticalOffset;
            if (newScrollOffset > maxVerticalOffset) newScrollOffset = maxVerticalOffset;
        } else {
            // Content fits in viewport - center it
            newScrollOffset = -(winHeight - zoomedPageHeightSum) / 2.0f;
        }
        
        // Horizontal constraints  
        if (zoomedPageWidthMax > winWidth) {
            // Content is wider than viewport - constrain within content bounds
            // IMPORTANT: horizontalOffset works in reverse - positive values move content LEFT
            // Use correct boundary calculation based on rendering coordinate system
            float minHorizontalOffset = (winWidth - zoomedPageWidthMax) / 2.0f;
            float maxHorizontalOffset = (zoomedPageWidthMax - winWidth) / 2.0f;
            if (newHorizontalOffset > maxHorizontalOffset) newHorizontalOffset = maxHorizontalOffset;
            if (newHorizontalOffset < minHorizontalOffset) newHorizontalOffset = minHorizontalOffset;
        } else {
            // Content fits in viewport - center it
            newHorizontalOffset = 0.0f;
        }
    } else {
        // ZOOM IN MODE: Constrained panning within content bounds
        // Vertical constraints - allow viewing the entire zoomed content
        float verticalContentOverflow = zoomedPageHeightSum - winHeight + bottomPadding;
        if (verticalContentOverflow > 0.0f) {
            float minVerticalOffset = 0.0f;
            float maxVerticalOffset = verticalContentOverflow;
            if (newScrollOffset < minVerticalOffset) newScrollOffset = minVerticalOffset;
            if (newScrollOffset > maxVerticalOffset) newScrollOffset = maxVerticalOffset;
        } else {
            // Content fits in viewport - center it with padding consideration
            newScrollOffset = -verticalContentOverflow / 2.0f;
        }
        
        // Horizontal constraints - allow viewing the entire zoomed content
        // IMPORTANT: horizontalOffset works in reverse - positive values move content LEFT
        float horizontalContentOverflow = zoomedPageWidthMax - winWidth;
        if (horizontalContentOverflow > 0.0f) {
            // Content is wider than viewport - constrain within content bounds
            // Use correct boundary calculation based on rendering coordinate system
            float minHorizontalOffset = (winWidth - zoomedPageWidthMax) / 2.0f;
            float maxHorizontalOffset = (zoomedPageWidthMax - winWidth) / 2.0f;
            if (newHorizontalOffset > maxHorizontalOffset) newHorizontalOffset = maxHorizontalOffset;
            if (newHorizontalOffset < minHorizontalOffset) newHorizontalOffset = minHorizontalOffset;
        } else {
            // Content fits in viewport - center it
            newHorizontalOffset = 0.0f;
        }
    }
    
    // Apply the constrained offsets
    state.scrollOffset = newScrollOffset;
    state.horizontalOffset = newHorizontalOffset;    
    // Debug output for enhanced cursor tracking (can be removed in production)
    if (hasValidTarget) {
        // Enhanced cursor-based zoom successfully calculated precise position
    } else {
        // Cursor not over page content - use fallback centering behavior
    }    // Update total content size based on new zoom
    state.pageHeightSum = 0.0f;
    state.pageWidthMax = 0.0f;
    
    for (int i = 0; i < (int)pageHeights.size(); ++i) {
        state.pageHeightSum += pageHeights[i] * state.zoomScale;
        // Use actual page width if available, otherwise fall back to aspect ratio estimate
        if (state.pageWidths && i < (int)state.pageWidths->size()) {
            float pageWidth = (*state.pageWidths)[i] * state.zoomScale;
            if (pageWidth > state.pageWidthMax) state.pageWidthMax = pageWidth;
        } else {
            // Fallback: assume standard page aspect ratio (e.g., A4 = 8.5x11 inches)
            float pageWidth = pageHeights[i] * state.zoomScale * 0.77f; // Approximate aspect ratio
            if (pageWidth > state.pageWidthMax) state.pageWidthMax = pageWidth;
        }
    }
    // Update maximum scroll offsets (for UI elements) with proper bottom padding
    state.maxOffset = std::max(0.0f, state.pageHeightSum - winHeight + bottomPadding);
    // FIXED: maxHorizontalOffset calculation for the coordinate system
    // Since horizontalOffset works in reverse (positive = move left), we need proper bounds
    state.maxHorizontalOffset = std::max(0.0f, (state.pageWidthMax - winWidth) / 2.0f);
    
    // DISABLED AUTO-CENTERING: Allow manual zoom control without auto-fitting
    // Only apply boundary constraints to prevent content from going out of bounds
    
    // Apply boundary constraints for all zoom levels
    // Vertical constraints
    if (state.scrollOffset < 0.0f) state.scrollOffset = 0.0f;
    if (state.scrollOffset > state.maxOffset) state.scrollOffset = state.maxOffset;
    
    // Horizontal constraints - only clamp if content exceeds viewport
    // NOTE: state.pageWidthMax is already computed in screen units for current zoom
    if (state.pageWidthMax > winWidth) {
        float minHorizontalOffset = (winWidth - state.pageWidthMax) / 2.0f;
        float maxHorizontalOffset = (state.pageWidthMax - winWidth) / 2.0f;
        if (state.horizontalOffset > maxHorizontalOffset) state.horizontalOffset = maxHorizontalOffset;
        if (state.horizontalOffset < minHorizontalOffset) state.horizontalOffset = minHorizontalOffset;
    }
    // FREELOOK ZOOM: For zoom > 100%, maintain current position (no auto-centering)    // Mark that zoom has changed with immediate rendering for visible pages only
    state.zoomChanged = true;
    state.immediateRenderRequired = true;
    
    // Update visible page range for optimization
    GetVisiblePageRange(state, pageHeights, state.firstVisiblePage, state.lastVisiblePage);    // FIXED: Update text selection coordinates when zoom changes
    // This ensures text selection remains accurate after zoom operations
    if (!pageWidths.empty()) {
        UpdateTextSelectionCoordinates(state, pageHeights, pageWidths);
    }

    // Note: Rendering will be handled by main loop's optimized rendering system
}

// Panning functions for drag-to-pan support
void StartPanning(PDFScrollState& state, double mouseX, double mouseY) {
    state.isPanning = true;
    state.panStartX = mouseX;
    state.panStartY = mouseY;
    state.panStartScrollOffset = state.scrollOffset;
    state.panStartHorizontalOffset = state.horizontalOffset;
}

void UpdatePanning(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights) {
    if (!state.isPanning) return;
    
    // Calculate mouse movement delta
    double deltaX = mouseX - state.panStartX;
    double deltaY = mouseY - state.panStartY;
      // ENHANCED SCALE-AWARE PANNING: Improved sensitivity curve for all zoom levels
    // When zoomed IN more, panning should be LESS sensitive (finer control)
    // When zoomed OUT, panning should be MORE sensitive (coarser movement)
    // Use a more refined sensitivity curve that works well at extreme zoom levels
      float panSensitivity = 1.0f;
    /* if (state.zoomScale <= 1.0f) {
        // Zoom out mode: Higher sensitivity for faster navigation
        panSensitivity = 1.0f / state.zoomScale;
        if (panSensitivity > 4.0f) panSensitivity = 4.0f; // Cap at 4x for very low zoom
    } else if (state.zoomScale <= 3.0f) {
        // Normal zoom in mode: Standard inverse relationship
        panSensitivity = 1.0f / state.zoomScale;
    } else {
        // High zoom mode: Improved sensitivity curve for smooth and responsive panning
        // Use a modified inverse relationship that maintains good responsiveness
        // Formula: base sensitivity * scale factor for smooth transition from 3.0x
        float baseAtThreeX = 1.0f / 3.0f; // Sensitivity at 3.0x zoom (�0.333)
        float scaleFactor = 3.0f / state.zoomScale; // Reduces as zoom increases
        panSensitivity = baseAtThreeX * scaleFactor * 1.2f; // 1.2f boost for better responsiveness
        
        // Ensure minimum responsiveness while maintaining fine control
        if (panSensitivity < 0.15f) panSensitivity = 0.15f; // Increased minimum for better feel
        if (panSensitivity > 0.4f) panSensitivity = 0.4f;   // Cap to prevent oversensitivity
    } */
    
    // Calculate potential new offsets
    float newScrollOffset = state.panStartScrollOffset - (float)deltaY * panSensitivity;
    float newHorizontalOffset = state.panStartHorizontalOffset - (float)deltaX * panSensitivity;
    
    // ADAPTIVE PANNING CONSTRAINTS: Behavior depends on zoom level
    if (state.zoomScale <= 1.0f) {
        // ZOOM OUT MODE: Center-aligned with constrained panning
        // When zoomed out, pages should stay centered and panning should be limited
        
        // Vertical panning: Only allow if content exceeds viewport height
        if (state.pageHeightSum > winHeight) {
            // Allow vertical panning but constrain to content bounds with proper maxOffset
            float bottomPadding = winHeight * 0.1f;
            float correctedMaxOffset = std::max(0.0f, state.pageHeightSum - winHeight + bottomPadding);
            if (newScrollOffset < 0.0f) newScrollOffset = 0.0f;
            if (newScrollOffset > correctedMaxOffset) newScrollOffset = correctedMaxOffset;
            state.scrollOffset = newScrollOffset;
        } else {
            // Content fits in viewport - center it vertically, no panning
            state.scrollOffset = 0.0f;
        }
        
        // Horizontal panning: Only allow if content exceeds viewport width  
        float visiblePageWidth = GetVisiblePageMaxWidth(state, pageHeights);
        if (visiblePageWidth > winWidth) {
            // Allow horizontal panning but constrain to content bounds - no gaps
            // IMPORTANT: horizontalOffset works in reverse - positive values move content LEFT
            
            // Use correct boundary calculation based on rendering coordinate system
            // Page left edge: (winWidth/2 - horizontalOffset) - pageW/2
            // Page right edge: (winWidth/2 - horizontalOffset) + pageW/2
            float minHorizontalOffset = (winWidth - visiblePageWidth) / 2.0f;
            float maxHorizontalOffset = (visiblePageWidth - winWidth) / 2.0f;
            
            if (newHorizontalOffset > maxHorizontalOffset) newHorizontalOffset = maxHorizontalOffset;
            if (newHorizontalOffset < minHorizontalOffset) newHorizontalOffset = minHorizontalOffset;
            state.horizontalOffset = newHorizontalOffset;
        } else {
            // Content fits in viewport - center it horizontally, no panning
            state.horizontalOffset = 0.0f;
        }
    } else {
        // ZOOM IN MODE: Constrained panning within content bounds
        // When zoomed in, calculate proper bounds based on actual content dimensions
        
        // Calculate the actual content dimensions at current zoom level
        float zoomedPageWidthMax = GetVisiblePageMaxWidth(state, pageHeights);
        float zoomedPageHeightSum = state.pageHeightSum;
        
        // Vertical panning constraints with proper padding
        // Allow panning only within the bounds of the zoomed content
        float bottomPadding = winHeight * 0.1f;
        float verticalContentOverflow = zoomedPageHeightSum - winHeight + bottomPadding;
        if (verticalContentOverflow > 0.0f) {
            // Content is larger than viewport - constrain within content bounds
            float minVerticalOffset = 0.0f;
            float maxVerticalOffset = verticalContentOverflow;
            
            if (newScrollOffset < minVerticalOffset) newScrollOffset = minVerticalOffset;
            if (newScrollOffset > maxVerticalOffset) newScrollOffset = maxVerticalOffset;
            state.scrollOffset = newScrollOffset;
        } else {
            // Content fits within viewport height - center it vertically
            state.scrollOffset = -verticalContentOverflow / 2.0f;
        }
        
        // Horizontal panning constraints
        // Allow panning only within the bounds of the zoomed content
        // IMPORTANT: horizontalOffset works in reverse - positive values move content LEFT
        // xCenter = (winWidth / 2.0f) - horizontalOffset
        float horizontalContentOverflow = zoomedPageWidthMax - winWidth;
        if (horizontalContentOverflow > 0.0f) {
            // Content is wider than viewport - constrain to show only content, no gaps
            // Page rendering: x = xCenter - pageW/2, where xCenter = winWidth/2 - horizontalOffset
            // Page left edge: (winWidth/2 - horizontalOffset) - pageW/2
            // Page right edge: (winWidth/2 - horizontalOffset) + pageW/2
            
            // For no right gap: page left edge should be <= 0 (content fills right side)
            // (winWidth/2 - horizontalOffset) - pageW/2 <= 0
            // winWidth/2 - pageW/2 <= horizontalOffset
            // horizontalOffset >= (winWidth - pageW)/2
            float minHorizontalOffset = (winWidth - zoomedPageWidthMax) / 2.0f;
            
            // For no left gap: page right edge should be >= winWidth (content fills left side)
            // (winWidth/2 - horizontalOffset) + pageW/2 >= winWidth
            // winWidth/2 + pageW/2 - winWidth >= horizontalOffset
            // (pageW - winWidth)/2 >= horizontalOffset
            // horizontalOffset <= (pageW - winWidth)/2
            float maxHorizontalOffset = (zoomedPageWidthMax - winWidth) / 2.0f;
            
            if (newHorizontalOffset > maxHorizontalOffset) newHorizontalOffset = maxHorizontalOffset;
            if (newHorizontalOffset < minHorizontalOffset) newHorizontalOffset = minHorizontalOffset;
            state.horizontalOffset = newHorizontalOffset;
        } else {
            // Content fits within viewport width - keep it centered
            state.horizontalOffset = 0.0f;
        }
    }
}

void StopPanning(PDFScrollState& state) {
    state.isPanning = false;
}

// Scroll bar dragging functions
void StartScrollBarDragging(PDFScrollState& state, double mouseY) {
    state.isScrollBarDragging = true;
    state.scrollBarDragStartY = mouseY;
    state.scrollBarDragStartOffset = state.scrollOffset;
}

void UpdateScrollBarDragging(PDFScrollState& state, double mouseY, float winHeight) {
    if (!state.isScrollBarDragging) return;
    
    // Calculate mouse movement delta
    double deltaY = mouseY - state.scrollBarDragStartY;
    
    // Convert mouse delta to scroll offset delta
    float barMargin = 0.01f * winHeight;
    float barH = winHeight - 2 * barMargin;
    float scrollDelta = (float)deltaY / barH * state.maxOffset;
    
    // Apply the scroll delta
    float newScrollOffset = state.scrollBarDragStartOffset + scrollDelta;
    
    // Clamp to valid range
    if (newScrollOffset < 0.0f) newScrollOffset = 0.0f;
    if (newScrollOffset > state.maxOffset) newScrollOffset = state.maxOffset;
    
    state.scrollOffset = newScrollOffset;
}

void StopScrollBarDragging(PDFScrollState& state) {
    state.isScrollBarDragging = false;
}

// =============================================================================
// TEXT EXTRACTION AND SELECTION IMPLEMENTATION
// =============================================================================

void InitializeTextExtraction(PDFScrollState& state, int pageCount) {
    state.textPages.clear();
    state.textPages.resize(pageCount);
    
    // Initialize all text page data structures
    for (int i = 0; i < pageCount; i++) {
        state.textPages[i].textPage = nullptr;
        state.textPages[i].charCount = 0;
        state.textPages[i].isLoaded = false;
    }
    
    // Clear any existing text selection
    ClearTextSelection(state);
}

void LoadTextPage(PDFScrollState& state, int pageIndex, FPDF_PAGE page) {
    if (pageIndex < 0 || pageIndex >= (int)state.textPages.size()) return;
    if (state.textPages[pageIndex].isLoaded) return; // Already loaded
    
    // Load text page using PDFium
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (textPage) {
        state.textPages[pageIndex].textPage = textPage;
        state.textPages[pageIndex].charCount = FPDFText_CountChars(textPage);
        state.textPages[pageIndex].isLoaded = true;
    }
}

void UnloadTextPage(PDFScrollState& state, int pageIndex) {
    if (pageIndex < 0 || pageIndex >= (int)state.textPages.size()) return;
    if (!state.textPages[pageIndex].isLoaded) return;
    
    // Free PDFium text page
    if (state.textPages[pageIndex].textPage) {
        FPDFText_ClosePage(state.textPages[pageIndex].textPage);
        state.textPages[pageIndex].textPage = nullptr;
    }
    
    state.textPages[pageIndex].charCount = 0;
    state.textPages[pageIndex].isLoaded = false;
}

void CleanupTextExtraction(PDFScrollState& state) {
    // Unload all text pages
    for (int i = 0; i < (int)state.textPages.size(); i++) {
        UnloadTextPage(state, i);
    }
    state.textPages.clear();
    ClearTextSelection(state);
}

// Convert screen coordinates to PDF coordinates for a specific page
void ScreenToPDFCoordinates(double screenX, double screenY, double& pdfX, double& pdfY, 
                           int pageIndex, float winWidth, float winHeight, 
                           const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    
    // Validate page index
    if (pageIndex < 0 || pageIndex >= (int)pageHeights.size() || pageIndex >= (int)pageWidths.size()) {
        pdfX = pdfY = 0.0;
        return;
    }
    
    // IMPROVED: Direct page-aligned coordinate transformation
    // Calculate exact page position and dimensions using same logic as rendering
    
    // 1. Calculate the page's top-left position in screen coordinates
    float pageTopY = -state.scrollOffset;
    for (int i = 0; i < pageIndex; i++) {
        pageTopY += (float)pageHeights[i] * state.zoomScale;
    }
    
    // 2. Calculate page dimensions in screen coordinates
    float pageWidthInScreen = (float)pageWidths[pageIndex] * state.zoomScale;
    float pageHeightInScreen = (float)pageHeights[pageIndex] * state.zoomScale;
    
    // 3. Calculate page center position considering horizontal offset
    float pageCenterX = (winWidth / 2.0f) - state.horizontalOffset;
    float pageLeftX = pageCenterX - (pageWidthInScreen / 2.0f);
    float pageRightX = pageCenterX + (pageWidthInScreen / 2.0f);
    float pageBottomY = pageTopY + pageHeightInScreen;
    
    // 4. Check if screen coordinates are within page bounds
    if (screenX < pageLeftX || screenX > pageRightX || screenY < pageTopY || screenY > pageBottomY) {
        // Outside page bounds - clamp to page boundaries for better selection behavior
        if (screenX < pageLeftX) screenX = pageLeftX;
        if (screenX > pageRightX) screenX = pageRightX;
        if (screenY < pageTopY) screenY = pageTopY;
        if (screenY > pageBottomY) screenY = pageBottomY;
    }
    
    // 5. Convert screen coordinates to page-relative coordinates (0 to 1)
    float pageRelativeX = (float)(screenX - pageLeftX) / pageWidthInScreen;
    float pageRelativeY = (float)(screenY - pageTopY) / pageHeightInScreen;
    
    // 6. Clamp to valid range [0, 1]
    if (pageRelativeX < 0.0f) pageRelativeX = 0.0f;
    if (pageRelativeX > 1.0f) pageRelativeX = 1.0f;
    if (pageRelativeY < 0.0f) pageRelativeY = 0.0f;
    if (pageRelativeY > 1.0f) pageRelativeY = 1.0f;
    
    // 7. Convert to PDF coordinates using page bounding box (CropBox ∩ MediaBox)
    // PDF origin is bottom-left; screen origin is top-left
    double bboxLeft = 0.0, bboxRight = (*state.originalPageWidths)[pageIndex];
    double bboxBottom = 0.0, bboxTop = (*state.originalPageHeights)[pageIndex];
    if (pageIndex >= 0 && pageIndex < (int)state.pageBBoxes.size()) {
        const FS_RECTF& bb = state.pageBBoxes[pageIndex];
        // FS_RECTF uses top greater than bottom; use as floats
        bboxLeft = bb.left;
        bboxRight = bb.right;
        bboxBottom = bb.bottom;
        bboxTop = bb.top;
        // Fallback sanity if invalid
        if (bboxRight <= bboxLeft) { bboxLeft = 0.0; bboxRight = (*state.originalPageWidths)[pageIndex]; }
        if (bboxTop <= bboxBottom) { bboxBottom = 0.0; bboxTop = (*state.originalPageHeights)[pageIndex]; }
    }
    const double bboxWidth = bboxRight - bboxLeft;
    const double bboxHeight = bboxTop - bboxBottom;
    pdfX = bboxLeft + pageRelativeX * bboxWidth;
    pdfY = bboxBottom + (1.0 - pageRelativeY) * bboxHeight; // Flip Y for PDF coordinate system
    
    // 8. Final validation within bbox
    if (pdfX < bboxLeft) pdfX = bboxLeft;
    if (pdfX > bboxRight) pdfX = bboxRight;
    if (pdfY < bboxBottom) pdfY = bboxBottom;
    if (pdfY > bboxTop) pdfY = bboxTop;
}

// Find which page is at a given screen Y position
int GetPageAtScreenPosition(double screenY, const PDFScrollState& state, const std::vector<int>& pageHeights) {
    if (pageHeights.empty()) return -1;
    
    double currentY = -state.scrollOffset;
    
    for (int i = 0; i < (int)pageHeights.size(); i++) {
        double pageHeight = (double)pageHeights[i] * state.zoomScale;
        
        // Check if Y position is within this page (with small tolerance for edges)
        if (screenY >= currentY - 1.0 && screenY <= currentY + pageHeight + 1.0) {
            return i;
        }
        currentY += pageHeight;
    }
    
    // If not found, check if we're before the first page or after the last page
    if (screenY < -state.scrollOffset) {
        return 0; // Before first page, use first page
    }
    
    // After last page, use last page
    return (int)pageHeights.size() - 1;
}

void StartTextSelection(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    // Clear any existing selection
    ClearTextSelection(state);
    
    // Find which page the mouse is over
    int pageIndex = GetPageAtScreenPosition(mouseY, state, pageHeights);
    if (pageIndex == -1) return;
    
    // Convert screen coordinates to PDF coordinates with improved precision
    double pdfX, pdfY;
    ScreenToPDFCoordinates(mouseX, mouseY, pdfX, pdfY, pageIndex, winWidth, winHeight, state, pageHeights, pageWidths);
    
    // Initialize text selection (but don't make it visually active yet)
    state.textSelection.isActive = false;  // Will be activated when dragging actually starts
    state.textSelection.isDragging = true;
    state.textSelection.startPageIndex = pageIndex;
    state.textSelection.endPageIndex = pageIndex;
    state.textSelection.startX = pdfX;
    state.textSelection.startY = pdfY;
    state.textSelection.endX = pdfX;
    state.textSelection.endY = pdfY;
    
    // Store current zoom/pan state for coordinate tracking
    state.textSelection.selectionZoomScale = state.zoomScale;
    state.textSelection.selectionScrollOffset = state.scrollOffset;
    state.textSelection.selectionHorizontalOffset = state.horizontalOffset;
    state.textSelection.needsCoordinateUpdate = false;
    
    // Find character index at start position with adaptive tolerance
    if (pageIndex < (int)state.textPages.size() && state.textPages[pageIndex].isLoaded) {
        // Use adaptive tolerance based on zoom level for better accuracy
        double tolerance = std::max(2.0, 8.0 / state.zoomScale);
        
        int charIndex = FPDFText_GetCharIndexAtPos(
            state.textPages[pageIndex].textPage, pdfX, pdfY, tolerance, tolerance);
        
        // If no character found with normal tolerance, try with larger tolerance
        if (charIndex == -1 && tolerance < 10.0) {
            charIndex = FPDFText_GetCharIndexAtPos(
                state.textPages[pageIndex].textPage, pdfX, pdfY, 10.0, 10.0);
        }
        
        state.textSelection.startCharIndex = charIndex;
        state.textSelection.endCharIndex = charIndex;
    }
}

void UpdateTextSelection(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    if (!state.textSelection.isDragging) return;
    
    // Activate selection immediately when dragging (simplified from old version)
    state.textSelection.isActive = true;
    
    // Find which page the mouse is over
    int pageIndex = GetPageAtScreenPosition(mouseY, state, pageHeights);
    if (pageIndex == -1) {
        // Mouse is outside any page - use the last valid page
        if (state.textSelection.endPageIndex >= 0 && state.textSelection.endPageIndex < (int)pageHeights.size()) {
            pageIndex = state.textSelection.endPageIndex;
        } else {
            return;
        }
    }
    
    // Convert screen coordinates to PDF coordinates with improved precision
    double pdfX, pdfY;
    ScreenToPDFCoordinates(mouseX, mouseY, pdfX, pdfY, pageIndex, winWidth, winHeight, state, pageHeights, pageWidths);
    
    // Update end position
    state.textSelection.endPageIndex = pageIndex;
    state.textSelection.endX = pdfX;
    state.textSelection.endY = pdfY;
    
    // Find character index at end position with consistent tolerance
    if (pageIndex < (int)state.textPages.size() && state.textPages[pageIndex].isLoaded) {
        // Use adaptive tolerance based on zoom level for better accuracy
        double tolerance = std::max(2.0, 8.0 / state.zoomScale);
        
        int charIndex = FPDFText_GetCharIndexAtPos(
            state.textPages[pageIndex].textPage, pdfX, pdfY, tolerance, tolerance);
        
        // If no character found with normal tolerance, try with larger tolerance
        if (charIndex == -1 && tolerance < 10.0) {
            charIndex = FPDFText_GetCharIndexAtPos(
                state.textPages[pageIndex].textPage, pdfX, pdfY, 10.0, 10.0);
        }
        
        state.textSelection.endCharIndex = charIndex;
        
        // Ensure we maintain selection continuity - if we lose character detection,
        // use the last valid character index for the page
        if (charIndex == -1 && pageIndex == state.textSelection.startPageIndex) {
            // Same page selection - ensure we don't lose the selection
            state.textSelection.endCharIndex = state.textSelection.startCharIndex;
        } else if (charIndex == -1 && state.textPages[pageIndex].isLoaded) {
            // Different page - use last character if we're at the end
            int totalChars = state.textPages[pageIndex].charCount;
            if (totalChars > 0) {
                state.textSelection.endCharIndex = totalChars - 1;
            }
        }
    }
}

void EndTextSelection(PDFScrollState& state) {
    state.textSelection.isDragging = false;
    
    // Ensure we have a valid selection
    if (state.textSelection.startCharIndex == -1 || state.textSelection.endCharIndex == -1) {
        ClearTextSelection(state);
        return;
    }
    
    // Normalize selection (ensure start comes before end)
    if (state.textSelection.startPageIndex > state.textSelection.endPageIndex ||
        (state.textSelection.startPageIndex == state.textSelection.endPageIndex && 
         state.textSelection.startCharIndex > state.textSelection.endCharIndex)) {
        
        // Swap start and end
        std::swap(state.textSelection.startPageIndex, state.textSelection.endPageIndex);
        std::swap(state.textSelection.startCharIndex, state.textSelection.endCharIndex);
        std::swap(state.textSelection.startX, state.textSelection.endX);
        std::swap(state.textSelection.startY, state.textSelection.endY);
    }
    
    // Get the selected text and populate both search field and selectedText display
    std::string selectedText = GetSelectedText(state);
    if (!selectedText.empty()) {
        // Store the selected text for display in search box
        state.textSearch.selectedText = selectedText;
        
        // Auto-populate search field with selected text if enabled
        if (state.textSearch.autoPopulateFromSelection) {
            // Limit search term length for performance
            if (selectedText.length() > 100) {
                selectedText = selectedText.substr(0, 100);
            }
            
            // Remove newlines and normalize whitespace for search
            for (size_t i = 0; i < selectedText.length(); i++) {
                if (selectedText[i] == '\n' || selectedText[i] == '\r' || selectedText[i] == '\t') {
                    selectedText[i] = ' ';
                }
            }
              // Update search term and trigger search
            state.textSearch.searchTerm = selectedText;
            state.textSearch.needsUpdate = true;
            state.textSearch.searchChanged = true;
            state.textSearch.searchBoxFocused = true;
            state.textSearch.lastInputTime = glfwGetTime(); // Set timestamp for search
            
            // Update the Win32 search edit box with the selected text
            if (g_menuIntegration) {
                g_menuIntegration->UpdateSearchEditText(selectedText);
            }
        }
    } else {
        // Clear selectedText if no valid selection
        state.textSearch.selectedText.clear();
    }
}

void ClearTextSelection(PDFScrollState& state) {
    state.textSelection.isActive = false;
    state.textSelection.isDragging = false;
    state.textSelection.startPageIndex = -1;
    state.textSelection.endPageIndex = -1;
    state.textSelection.startCharIndex = -1;
    state.textSelection.endCharIndex = -1;
    state.textSelection.needsCoordinateUpdate = false;
}

// =============================================================================
// NEW: TEXT SELECTION COORDINATE UPDATES AND CURSOR MANAGEMENT
// =============================================================================

void UpdateTextSelectionCoordinates(PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    if (!state.textSelection.isActive) return;
    
    // Check if zoom or pan has changed since selection was made
    bool zoomChanged = (std::abs(state.zoomScale - state.textSelection.selectionZoomScale) > 0.001f);
    bool scrollChanged = (std::abs(state.scrollOffset - state.textSelection.selectionScrollOffset) > 1.0f);
    bool horizontalChanged = (std::abs(state.horizontalOffset - state.textSelection.selectionHorizontalOffset) > 1.0f);
    
    if (zoomChanged || scrollChanged || horizontalChanged) {
        // FIXED: Actually recalculate text selection coordinates for the new zoom/pan state
        // The character indices remain the same, but we need to get their new coordinates
        
        // Recalculate start position coordinates
        if (state.textSelection.startPageIndex < (int)state.textPages.size() && 
            state.textPages[state.textSelection.startPageIndex].isLoaded) {
            
            FPDF_TEXTPAGE startPage = state.textPages[state.textSelection.startPageIndex].textPage;
            if (state.textSelection.startCharIndex >= 0) {
                double left, top, right, bottom;
                if (FPDFText_GetCharBox(startPage, state.textSelection.startCharIndex, &left, &top, &right, &bottom)) {
                    state.textSelection.startX = left;
                    state.textSelection.startY = top;
                }
            }
        }
        
        // Recalculate end position coordinates
        if (state.textSelection.endPageIndex < (int)state.textPages.size() && 
            state.textPages[state.textSelection.endPageIndex].isLoaded) {
            
            FPDF_TEXTPAGE endPage = state.textPages[state.textSelection.endPageIndex].textPage;
            if (state.textSelection.endCharIndex >= 0) {
                double left, top, right, bottom;
                if (FPDFText_GetCharBox(endPage, state.textSelection.endCharIndex, &left, &top, &right, &bottom)) {
                    state.textSelection.endX = right;
                    state.textSelection.endY = bottom;
                }
            }
        }
        
        state.textSelection.needsCoordinateUpdate = true;
        
        // Update stored state to current values
        state.textSelection.selectionZoomScale = state.zoomScale;
        state.textSelection.selectionScrollOffset = state.scrollOffset;
        state.textSelection.selectionHorizontalOffset = state.horizontalOffset;
    }
}

bool CheckMouseOverText(const PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    // Find which page the mouse is over
    int pageIndex = GetPageAtScreenPosition(mouseY, state, pageHeights);
    if (pageIndex == -1) return false;
    
    // Check if this page has text loaded
    if (pageIndex >= (int)state.textPages.size() || !state.textPages[pageIndex].isLoaded) return false;
    
    // Convert screen coordinates to PDF coordinates
    double pdfX, pdfY;
    ScreenToPDFCoordinates(mouseX, mouseY, pdfX, pdfY, pageIndex, winWidth, winHeight, state, pageHeights, pageWidths);
    
    // Check if there's a character at this position with adaptive tolerance
    double tolerance = std::max(3.0, 12.0 / state.zoomScale);
    int charIndex = FPDFText_GetCharIndexAtPos(state.textPages[pageIndex].textPage, pdfX, pdfY, tolerance, tolerance);
    return (charIndex != -1);
}

void UpdateCursorForTextSelection(PDFScrollState& state, GLFWwindow* window, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    // Don't change cursor during panning, scroll bar dragging, or text selection dragging
    if (state.isPanning || state.isScrollBarDragging || state.textSelection.isDragging) {
        return;
    }
    
    bool isOverText = CheckMouseOverText(state, mouseX, mouseY, winWidth, winHeight, pageHeights, pageWidths);
    
    // Only update cursor if state changed
    if (isOverText != state.isOverText) {
        state.isOverText = isOverText;
        
        if (isOverText) {
            // Change to text selection cursor (I-beam)
            glfwSetCursor(window, glfwCreateStandardCursor(GLFW_IBEAM_CURSOR));
            state.cursorChanged = true;
        } else if (state.cursorChanged) {
            // Restore default cursor
            glfwSetCursor(window, nullptr);
            state.cursorChanged = false;
        }
    }
}

std::string GetSelectedText(const PDFScrollState& state) {
    if (!state.textSelection.isActive || 
        state.textSelection.startCharIndex == -1 || 
        state.textSelection.endCharIndex == -1) {
        return "";
    }
    
    static int lastStartChar = -1;
    static int lastEndChar = -1;
    static std::string lastResult = "";
    
    // Avoid reprocessing the same selection repeatedly
    if (state.textSelection.startCharIndex == lastStartChar && 
        state.textSelection.endCharIndex == lastEndChar) {
        return lastResult;
    }
    
    std::string result;
    
    // Handle single page selection
    if (state.textSelection.startPageIndex == state.textSelection.endPageIndex) {
        int pageIndex = state.textSelection.startPageIndex;
        if (pageIndex < (int)state.textPages.size() && state.textPages[pageIndex].isLoaded) {
            FPDF_TEXTPAGE textPage = state.textPages[pageIndex].textPage;
            
            // Calculate the character count properly
            // For PDFium, FPDFText_GetText expects a count, not an end index
            int startChar = state.textSelection.startCharIndex;
            int endChar = state.textSelection.endCharIndex;
            int count = endChar - startChar + 1;
            
            // Debug output to help diagnose the issue (only when selection changes)
            std::cout << "GetSelectedText: startChar=" << startChar << ", endChar=" << endChar << ", count=" << count << std::endl;
            
            if (count > 0) {
                // Allocate buffer with extra space for null terminator
                std::vector<unsigned short> buffer(count + 2);
                int written = FPDFText_GetText(textPage, startChar, count, buffer.data());
                
                std::cout << "FPDFText_GetText returned: " << written << " characters" << std::endl;
                
                // Convert UTF-16 to UTF-8
                if (written > 0) {
                    // written includes the null terminator, so process written-1 characters
                    for (int i = 0; i < written - 1; i++) {
                        if (buffer[i] < 128) {
                            result += (char)buffer[i];
                        } else {
                            result += '?'; // Placeholder for non-ASCII characters
                        }
                    }
                }
                
                std::cout << "Final selected text: '" << result << "'" << std::endl;
            }
        }
    } else {
        // Handle multi-page selection (simplified - extract from start page to end page)
        for (int pageIndex = state.textSelection.startPageIndex; 
             pageIndex <= state.textSelection.endPageIndex; pageIndex++) {
            
            if (pageIndex < (int)state.textPages.size() && state.textPages[pageIndex].isLoaded) {
                FPDF_TEXTPAGE textPage = state.textPages[pageIndex].textPage;
                int startChar = (pageIndex == state.textSelection.startPageIndex) ? 
                               state.textSelection.startCharIndex : 0;
                int endChar = (pageIndex == state.textSelection.endPageIndex) ? 
                             state.textSelection.endCharIndex : state.textPages[pageIndex].charCount - 1;
                int count = endChar - startChar + 1;
                
                if (count > 0) {
                    std::vector<unsigned short> buffer(count + 1);
                    int written = FPDFText_GetText(textPage, startChar, count, buffer.data());
                    
                    // Convert UTF-16 to UTF-8
                    if (written > 0) {
                        for (int i = 0; i < written - 1; i++) {
                            if (buffer[i] < 128) {
                                result += (char)buffer[i];
                            } else {
                                result += '?'; // Placeholder for non-ASCII characters
                            }
                        }
                    }
                    
                    if (pageIndex < state.textSelection.endPageIndex) {
                        result += "\n"; // Add page break
                    }
                }
            }
        }    }
    
    // Cache the result to avoid repeated processing
    lastStartChar = state.textSelection.startCharIndex;
    lastEndChar = state.textSelection.endCharIndex;
    lastResult = result;
    
    return result;
}

void DrawTextSelection(const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths, float winWidth, float winHeight) {
    if (!state.textSelection.isActive) return;
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Set selection highlight color (blue with transparency)
    glColor4f(0.2f, 0.4f, 0.8f, 0.3f);
    
    // Draw selection rectangles for each page in the selection
    for (int pageIndex = state.textSelection.startPageIndex; 
         pageIndex <= state.textSelection.endPageIndex && pageIndex < (int)state.textPages.size(); 
         pageIndex++) {
        
        if (!state.textPages[pageIndex].isLoaded) continue;
        if (pageIndex >= (int)pageWidths.size()) continue; // Safety check
        
        FPDF_TEXTPAGE textPage = state.textPages[pageIndex].textPage;
        
        // Determine character range for this page
        int startChar = (pageIndex == state.textSelection.startPageIndex) ? 
                       state.textSelection.startCharIndex : 0;
        int endChar = (pageIndex == state.textSelection.endPageIndex) ? 
                     state.textSelection.endCharIndex : state.textPages[pageIndex].charCount - 1;
        
        if (startChar == -1 || endChar == -1 || startChar > endChar) continue;
        
        // Get selection rectangles from PDFium
        int rectCount = FPDFText_CountRects(textPage, startChar, endChar - startChar + 1);
          for (int rectIndex = 0; rectIndex < rectCount; rectIndex++) {
            double left, top, right, bottom;
            if (FPDFText_GetRect(textPage, rectIndex, &left, &top, &right, &bottom)) {
                
                // FIXED: Use exact same coordinate transformation as ScreenToPDFCoordinates (page-aligned)
                
                // 1. Calculate page position in screen coordinates
                float pageTopY = -state.scrollOffset;
                for (int i = 0; i < pageIndex; i++) {
                    pageTopY += (float)pageHeights[i] * state.zoomScale;
                }
                
                // 2. Calculate page dimensions in screen coordinates
                float pageWidthInScreen = (float)pageWidths[pageIndex] * state.zoomScale;
                float pageHeightInScreen = (float)pageHeights[pageIndex] * state.zoomScale;
                
                // 3. Calculate page center position considering horizontal offset
                float pageCenterX = (winWidth / 2.0f) - state.horizontalOffset;
                float pageLeftX = pageCenterX - (pageWidthInScreen / 2.0f);
                // 4. Convert PDF text coordinates to page-relative coordinates within bbox (0 to 1)
                double bboxLeft = 0.0, bboxRight = (*state.originalPageWidths)[pageIndex];
                double bboxBottom = 0.0, bboxTop = (*state.originalPageHeights)[pageIndex];
                if (pageIndex >= 0 && pageIndex < (int)state.pageBBoxes.size()) {
                    const FS_RECTF& bb = state.pageBBoxes[pageIndex];
                    bboxLeft = bb.left; bboxRight = bb.right; bboxBottom = bb.bottom; bboxTop = bb.top;
                    if (bboxRight <= bboxLeft) { bboxLeft = 0.0; bboxRight = (*state.originalPageWidths)[pageIndex]; }
                    if (bboxTop <= bboxBottom) { bboxBottom = 0.0; bboxTop = (*state.originalPageHeights)[pageIndex]; }
                }
                const double bboxW = bboxRight - bboxLeft;
                const double bboxH = bboxTop - bboxBottom;
                float textRelativeLeft = (float)((left - bboxLeft) / bboxW);
                float textRelativeRight = (float)((right - bboxLeft) / bboxW);
                float textRelativeTop = (float)(1.0 - (top - bboxBottom) / bboxH); // Flip Y for PDF coordinate system
                float textRelativeBottom = (float)(1.0 - (bottom - bboxBottom) / bboxH); // Flip Y for PDF coordinate system
                
                // 5. Convert page-relative coordinates to screen coordinates
                float screenLeft = pageLeftX + textRelativeLeft * pageWidthInScreen;
                float screenRight = pageLeftX + textRelativeRight * pageWidthInScreen;
                float screenTop = pageTopY + textRelativeTop * pageHeightInScreen;
                float screenBottom = pageTopY + textRelativeBottom * pageHeightInScreen;
                
                // 6. Visibility boost: enforce a minimum on-screen height for the selection rect
                {
                    float rectScreenH = screenBottom - screenTop;
                    // Minimum height: max(6px, 0.8% of viewport height)
                    float minPx = std::max(6.0f, winHeight * 0.008f);
                    if (rectScreenH < minPx) {
                        float inflate = (minPx - rectScreenH) * 0.5f;
                        screenTop -= inflate;
                        screenBottom += inflate;
                    }
                }

                // 7. Convert screen coordinates to normalized coordinates (-1 to 1)
                float normLeft = (screenLeft / winWidth) * 2.0f - 1.0f;
                float normRight = (screenRight / winWidth) * 2.0f - 1.0f;
                float normTop = 1.0f - (screenTop / winHeight) * 2.0f;
                float normBottom = 1.0f - (screenBottom / winHeight) * 2.0f;
                
                // Only draw if within viewport bounds
                if (normRight > -1.0f && normLeft < 1.0f && normBottom > -1.0f && normTop < 1.0f) {
                    // Draw character bounding rectangle
                    glBegin(GL_QUADS);
                    glVertex2f(normLeft, normTop);
                    glVertex2f(normRight, normTop);
                    glVertex2f(normRight, normBottom);
                    glVertex2f(normLeft, normBottom);
                    glEnd();
                }
            }
        }    }
    
    glDisable(GL_BLEND);
}

// =============================================================================
// DEBUG TEXT COORDINATE VISUALIZATION
// =============================================================================

void DrawTextCoordinateDebug(const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths, float winWidth, float winHeight) {
    if (!state.debugTextCoordinates) return;
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Set debug highlight color (red with transparency)
    glColor4f(1.0f, 0.0f, 0.0f, 0.2f);
    
    // Draw debug rectangles for all text on visible pages
    for (int pageIndex = 0; pageIndex < (int)state.textPages.size(); pageIndex++) {
        if (!state.textPages[pageIndex].isLoaded) continue;
        if (pageIndex >= (int)pageWidths.size()) continue; // Safety check
        
        // Check if page is visible (simple check based on scroll state)
        float currentY = -state.scrollOffset;
        for (int i = 0; i < pageIndex; i++) {
            currentY += pageHeights[i] * state.zoomScale;
        }
        float pageBottom = currentY + pageHeights[pageIndex] * state.zoomScale;
        
        // Skip if page is completely outside viewport
        if (currentY > winHeight || pageBottom < 0) continue;
        
        FPDF_TEXTPAGE textPage = state.textPages[pageIndex].textPage;
        int charCount = state.textPages[pageIndex].charCount;        // Draw bounding box for every 10th character to avoid too much visual noise
        for (int charIndex = 0; charIndex < charCount; charIndex += 10) {
            double left, top, right, bottom;
            if (FPDFText_GetCharBox(textPage, charIndex, &left, &top, &right, &bottom)) {
                
                // FIXED: Use exact same coordinate transformation as ScreenToPDFCoordinates (page-aligned)
                // 1. Calculate page position in screen coordinates
                float pageTopY = -state.scrollOffset;
                for (int i = 0; i < pageIndex; i++) {
                    pageTopY += (float)pageHeights[i] * state.zoomScale;
                }
                
                // 2. Calculate page dimensions in screen coordinates
                float pageWidthInScreen = (float)pageWidths[pageIndex] * state.zoomScale;
                float pageHeightInScreen = (float)pageHeights[pageIndex] * state.zoomScale;
                
                // 3. Calculate page center position considering horizontal offset
                float pageCenterX = (winWidth / 2.0f) - state.horizontalOffset;
                float pageLeftX = pageCenterX - (pageWidthInScreen / 2.0f);
                
                // 4. Convert PDF coordinates to page-relative coordinates within bbox (0 to 1)
                double bboxLeft = 0.0, bboxRight = (*state.originalPageWidths)[pageIndex];
                double bboxBottom = 0.0, bboxTop = (*state.originalPageHeights)[pageIndex];
                if (pageIndex >= 0 && pageIndex < (int)state.pageBBoxes.size()) {
                    const FS_RECTF& bb = state.pageBBoxes[pageIndex];
                    bboxLeft = bb.left; bboxRight = bb.right; bboxBottom = bb.bottom; bboxTop = bb.top;
                    if (bboxRight <= bboxLeft) { bboxLeft = 0.0; bboxRight = (*state.originalPageWidths)[pageIndex]; }
                    if (bboxTop <= bboxBottom) { bboxBottom = 0.0; bboxTop = (*state.originalPageHeights)[pageIndex]; }
                }
                const double bboxW = bboxRight - bboxLeft;
                const double bboxH = bboxTop - bboxBottom;
                float textRelativeLeft = (float)((left - bboxLeft) / bboxW);
                float textRelativeRight = (float)((right - bboxLeft) / bboxW);
                float textRelativeTop = (float)(1.0 - (top - bboxBottom) / bboxH);
                float textRelativeBottom = (float)(1.0 - (bottom - bboxBottom) / bboxH);
                
                // 5. Convert page-relative coordinates to screen coordinates
                float screenLeft = pageLeftX + textRelativeLeft * pageWidthInScreen;
                float screenRight = pageLeftX + textRelativeRight * pageWidthInScreen;
                float screenTop = pageTopY + textRelativeTop * pageHeightInScreen;
                float screenBottom = pageTopY + textRelativeBottom * pageHeightInScreen;
                
                // 6. Convert to normalized coordinates (-1 to 1)
                float normLeft = (screenLeft / winWidth) * 2.0f - 1.0f;
                float normRight = (screenRight / winWidth) * 2.0f - 1.0f;
                float normTop = 1.0f - (screenTop / winHeight) * 2.0f;
                float normBottom = 1.0f - (screenBottom / winHeight) * 2.0f;
                
                // Only draw if within viewport bounds
                if (normRight > -1.0f && normLeft < 1.0f && normBottom > -1.0f && normTop < 1.0f) {
                    // Draw character bounding rectangle
                    glBegin(GL_QUADS);
                    glVertex2f(normLeft, normTop);
                    glVertex2f(normRight, normTop);
                    glVertex2f(normRight, normBottom);
                    glVertex2f(normLeft, normBottom);
                    glEnd();
                }
            }
        }
          // Draw text words as green boxes for better visualization
        glColor4f(0.0f, 1.0f, 0.0f, 0.15f);
          // Get word rectangles using FPDFText_GetRects for groups of characters
        for (int startChar = 0; startChar < charCount; startChar += 20) {
            int endChar = (startChar + 19 < charCount - 1) ? startChar + 19 : charCount - 1;
            if (endChar <= startChar) continue;
            
            int rectCount = FPDFText_CountRects(textPage, startChar, endChar - startChar + 1);
            
            for (int rectIndex = 0; rectIndex < rectCount; rectIndex++) {
                double left, top, right, bottom;
                if (FPDFText_GetRect(textPage, rectIndex, &left, &top, &right, &bottom)) {
                    
                    // FIXED: Use exact same coordinate transformation as ScreenToPDFCoordinates (page-aligned)
                    // 1. Calculate page position in screen coordinates
                    float pageTopY = -state.scrollOffset;
                    for (int i = 0; i < pageIndex; i++) {
                        pageTopY += (float)pageHeights[i] * state.zoomScale;
                    }
                    
                    // 2. Calculate page dimensions in screen coordinates
                    float pageWidthInScreen = (float)pageWidths[pageIndex] * state.zoomScale;
                    float pageHeightInScreen = (float)pageHeights[pageIndex] * state.zoomScale;
                    
                    // 3. Calculate page center position considering horizontal offset
                    float pageCenterX = (winWidth / 2.0f) - state.horizontalOffset;
                    float pageLeftX = pageCenterX - (pageWidthInScreen / 2.0f);
                      // 4. Convert PDF coordinates to page-relative coordinates (0 to 1)
                    // CRITICAL FIX: Use original PDF page dimensions, not rendered bitmap dimensions
                    float textRelativeLeft = (float)(left / (*state.originalPageWidths)[pageIndex]);
                    float textRelativeRight = (float)(right / (*state.originalPageWidths)[pageIndex]);
                    float textRelativeTop = (float)(1.0 - top / (*state.originalPageHeights)[pageIndex]); // Flip Y for PDF coordinate system
                    float textRelativeBottom = (float)(1.0 - bottom / (*state.originalPageHeights)[pageIndex]); // Flip Y for PDF coordinate system
                    
                    // 5. Convert page-relative coordinates to screen coordinates
                    float screenLeft = pageLeftX + textRelativeLeft * pageWidthInScreen;
                    float screenRight = pageLeftX + textRelativeRight * pageWidthInScreen;
                    float screenTop = pageTopY + textRelativeTop * pageHeightInScreen;
                    float screenBottom = pageTopY + textRelativeBottom * pageHeightInScreen;
                    
                    // 6. Convert to normalized coordinates (-1 to 1)
                    float normLeft = (screenLeft / winWidth) * 2.0f - 1.0f;
                    float normRight = (screenRight / winWidth) * 2.0f - 1.0f;
                    float normTop = 1.0f - (screenTop / winHeight) * 2.0f;
                    float normBottom = 1.0f - (screenBottom / winHeight) * 2.0f;
                    
                    // Only draw if within viewport bounds
                    if (normRight > -1.0f && normLeft < 1.0f && normBottom > -1.0f && normTop < 1.0f) {
                        // Draw word group rectangle
                        glBegin(GL_QUADS);
                        glVertex2f(normLeft, normTop);
                        glVertex2f(normRight, normTop);
                        glVertex2f(normRight, normBottom);
                        glVertex2f(normLeft, normBottom);
                        glEnd();
                    }
                }
            }
        }
        
        // Reset color for next page
        glColor4f(1.0f, 0.0f, 0.0f, 0.2f);
    }
    
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Reset color
}

// =============================================================================
// DOUBLE-CLICK TEXT SELECTION IMPLEMENTATION
// =============================================================================

bool DetectDoubleClick(PDFScrollState& state, double mouseX, double mouseY, double currentTime) {
    const double DOUBLE_CLICK_TIME = 0.5; // Maximum time between clicks (500ms)
    const double DOUBLE_CLICK_DISTANCE = 10.0; // Maximum distance between clicks (pixels)
    
    bool isDoubleClick = false;
    
    // Check if this is a potential double-click
    if (state.textSelection.lastClickTime > 0.0) {
        double timeDiff = currentTime - state.textSelection.lastClickTime;
        double distanceX = mouseX - state.textSelection.lastClickX;
        double distanceY = mouseY - state.textSelection.lastClickY;
        double distance = sqrt(distanceX * distanceX + distanceY * distanceY);
        
        if (timeDiff <= DOUBLE_CLICK_TIME && distance <= DOUBLE_CLICK_DISTANCE) {
            isDoubleClick = true;
        }
    }
    
    // Update click tracking
    state.textSelection.lastClickTime = currentTime;
    state.textSelection.lastClickX = mouseX;
    state.textSelection.lastClickY = mouseY;
    state.textSelection.isDoubleClick = isDoubleClick;
    
    return isDoubleClick;
}

void FindWordBoundaries(FPDF_TEXTPAGE textPage, int charIndex, int& startChar, int& endChar) {
    if (!textPage || charIndex < 0) {
        startChar = endChar = -1;
        return;
    }
    
    int totalChars = FPDFText_CountChars(textPage);
    if (charIndex >= totalChars) {
        startChar = endChar = -1;
        return;
    }
    
    // Debug: Get the character at the click position
    unsigned int clickedChar = FPDFText_GetUnicode(textPage, charIndex);
    std::cout << "FindWordBoundaries: clicked on char index " << charIndex << " ('" << (char)clickedChar << "', Unicode: " << clickedChar << ")" << std::endl;
    
    // Find start of word (go backward until we hit whitespace or start of document)
    startChar = charIndex;
    while (startChar > 0) {
        unsigned int ch = FPDFText_GetUnicode(textPage, startChar - 1);
        
        // Check if character is whitespace, punctuation, or word boundary
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || 
            ch == '.' || ch == ',' || ch == ';' || ch == ':' || 
            ch == '!' || ch == '?' || ch == '(' || ch == ')' || 
            ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
            ch == '"' || ch == '\'' || ch == '-') { // NOTE: underscore '_' is treated as part of word
            break;
        }
        startChar--;
    }
    
    // Find end of word (go forward until we hit whitespace or end of document)
    endChar = charIndex;
    while (endChar < totalChars - 1) {
        unsigned int ch = FPDFText_GetUnicode(textPage, endChar + 1);
        
        // Check if character is whitespace, punctuation, or word boundary
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || 
            ch == '.' || ch == ',' || ch == ';' || ch == ':' || 
            ch == '!' || ch == '?' || ch == '(' || ch == ')' || 
            ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
            ch == '"' || ch == '\'' || ch == '-') { // NOTE: underscore '_' is treated as part of word
            break;
        }
        endChar++;
    }
    
    // Debug: Show the word boundaries found
    std::cout << "FindWordBoundaries: word spans from " << startChar << " to " << endChar << std::endl;
    
    // Debug: Extract and display the found word
    if (startChar <= endChar && startChar >= 0 && endChar < totalChars) {
        int count = endChar - startChar + 1;
        std::vector<unsigned short> buffer(count + 2);
        int written = FPDFText_GetText(textPage, startChar, count, buffer.data());
        if (written > 0) {
            std::string word;
            for (int i = 0; i < written - 1; i++) {
                if (buffer[i] < 128) {
                    word += (char)buffer[i];
                } else {
                    word += '?';
                }
            }
            std::cout << "FindWordBoundaries: found word '" << word << "'" << std::endl;
        }
    }
}

void SelectWordAtPosition(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    // Clear any existing selection
    ClearTextSelection(state);
    
    // Find which page the mouse is over
    int pageIndex = GetPageAtScreenPosition(mouseY, state, pageHeights);
    if (pageIndex == -1) return;
    
    // Check if this page has text loaded
    if (pageIndex >= (int)state.textPages.size() || !state.textPages[pageIndex].isLoaded) return;
    
    // Convert screen coordinates to PDF coordinates
    double pdfX, pdfY;
    ScreenToPDFCoordinates(mouseX, mouseY, pdfX, pdfY, pageIndex, winWidth, winHeight, state, pageHeights, pageWidths);
    
    // Find character at this position with adaptive tolerance
    FPDF_TEXTPAGE textPage = state.textPages[pageIndex].textPage;
    double tolerance = std::max(2.0, 8.0 / state.zoomScale);
    
    int charIndex = FPDFText_GetCharIndexAtPos(textPage, pdfX, pdfY, tolerance, tolerance);
    
    // If no character found with normal tolerance, try with larger tolerance
    if (charIndex == -1 && tolerance < 10.0) {
        charIndex = FPDFText_GetCharIndexAtPos(textPage, pdfX, pdfY, 10.0, 10.0);
    }
    
    if (charIndex == -1) return; // No character at this position
      // Find word boundaries
    int startChar, endChar;
    FindWordBoundaries(textPage, charIndex, startChar, endChar);
    
    // Debug output
    std::cout << "SelectWordAtPosition: charIndex=" << charIndex << ", startChar=" << startChar << ", endChar=" << endChar << std::endl;
    
    if (startChar == -1 || endChar == -1) return; // Could not find word boundaries
      // Get the coordinates of the word boundaries
    double startX, startY, endX, endY;
    double temp1, temp2; // For unused coordinate values
    
    // For PDFium, FPDFText_GetCharBox returns left, top, right, bottom
    // We need the left coordinate of start char and right coordinate of end char
    double startLeft, startTop, startRight, startBottom;
    double endLeft, endTop, endRight, endBottom;
    
    if (FPDFText_GetCharBox(textPage, startChar, &startLeft, &startTop, &startRight, &startBottom) &&
        FPDFText_GetCharBox(textPage, endChar, &endLeft, &endTop, &endRight, &endBottom)) {
        
        // Use left edge of start character and right edge of end character
        startX = startLeft;
        startY = startTop;
        endX = endRight;  // This should be the right edge of the last character
        endY = endBottom;
          // Debug output for coordinates
        std::cout << "Word coordinates: startChar=" << startChar << " (left=" << startLeft << ",top=" << startTop << ",right=" << startRight << ",bottom=" << startBottom << ")" << std::endl;
        std::cout << "                 endChar=" << endChar << " (left=" << endLeft << ",top=" << endTop << ",right=" << endRight << ",bottom=" << endBottom << ")" << std::endl;
        std::cout << "Final selection coordinates: (" << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;
        
        // Initialize text selection for the word
        state.textSelection.isActive = true;
        state.textSelection.isDragging = false;
        state.textSelection.startPageIndex = pageIndex;
        state.textSelection.endPageIndex = pageIndex;
        state.textSelection.startCharIndex = startChar;
        state.textSelection.endCharIndex = endChar;
        state.textSelection.startX = startX;
        state.textSelection.startY = startY;
        state.textSelection.endX = endX;
        state.textSelection.endY = endY;
        
        // Store current zoom/pan state for coordinate tracking
        state.textSelection.selectionZoomScale = state.zoomScale;
        state.textSelection.selectionScrollOffset = state.scrollOffset;
        state.textSelection.selectionHorizontalOffset = state.horizontalOffset;
        state.textSelection.needsCoordinateUpdate = false;
        
        // Mark this as a double-click selection to prevent EndTextSelection from clearing it immediately
        state.textSelection.isDoubleClick = true;
    }
}

// =============================================================================
// TEXT SEARCH IMPLEMENTATION
// =============================================================================

void InitializeTextSearch(PDFScrollState& state) {
    state.textSearch.isActive = true;              // Always active
    state.textSearch.isSearchBoxVisible = true;    // Always visible
    state.textSearch.searchTerm.clear();
    state.textSearch.results.clear();    state.textSearch.currentResultIndex = -1;
    state.textSearch.needsUpdate = false;
    state.textSearch.searchChanged = false;
    state.textSearch.matchCase = false;
    state.textSearch.matchWholeWord = false;
    state.textSearch.searchBoxFocused = false;
    state.textSearch.lastInputTime = 0.0;
    state.textSearch.searchBoxAlpha = 1.0f;        // Not used for Win32 UI
    state.textSearch.showMenuBar = false;          // Use Win32 toolbar instead of OpenGL
    state.textSearch.showSearchBox = false;        // Use Win32 toolbar instead of OpenGL
    state.textSearch.useWin32UI = true;            // Use Win32 toolbar only
    state.textSearch.autoPopulateFromSelection = true;
    
    // Initialize UI state variables (mostly unused for Win32 UI)
    state.textSearch.selectedText.clear();
    state.textSearch.showNoMatchMessage = false;
    state.textSearch.noMatchMessageTime = 0.0;
    state.textSearch.isTyping = false;
    state.textSearch.cursorBlinkTime = 0.0f;
    
    state.textSearch.searchHandles.clear();
}

void CleanupTextSearch(PDFScrollState& state) {
    // Close all search handles
    for (FPDF_SCHHANDLE handle : state.textSearch.searchHandles) {
        if (handle) {
            FPDFText_FindClose(handle);
        }
    }
    
    state.textSearch.searchHandles.clear();
    state.textSearch.results.clear();
    state.textSearch.currentResultIndex = -1;
    state.textSearch.isActive = false;
    state.textSearch.isSearchBoxVisible = false;
}

void ToggleSearchBox(PDFScrollState& state) {
    state.textSearch.isSearchBoxVisible = !state.textSearch.isSearchBoxVisible;
    state.textSearch.searchBoxFocused = state.textSearch.isSearchBoxVisible;
    
    if (state.textSearch.isSearchBoxVisible) {
        state.textSearch.searchBoxAlpha = 1.0f;
        state.textSearch.isActive = true;
    } else {
        // Hide search box and clear results
        state.textSearch.searchBoxAlpha = 0.0f;
        ClearSearchResults(state);
        state.textSearch.isActive = false;
    }
}

void UpdateSearchTerm(PDFScrollState& state, const std::string& term) {
    if (state.textSearch.searchTerm != term) {
        state.textSearch.searchTerm = term;
        state.textSearch.searchChanged = true;
        state.textSearch.needsUpdate = true;
        state.textSearch.currentResultIndex = -1;
        
        // Clear existing results
        state.textSearch.results.clear();
    }
}

void PerformTextSearch(PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths) {
    if (!state.textSearch.needsUpdate || state.textSearch.searchTerm.empty()) {
        return;
    }
      std::cout << "PerformTextSearch: Searching for '" << state.textSearch.searchTerm << "'" << std::endl;
    std::cout << "PerformTextSearch: Text pages loaded: " << state.textPages.size() << std::endl;
    
    // Clear previous search results and handles
    ClearSearchResults(state);
    
    // Convert search term to UTF-16 (required by PDFium)
    std::vector<unsigned short> searchTermUTF16;
    for (char c : state.textSearch.searchTerm) {
        if (c >= 0) { // Simple ASCII conversion
            searchTermUTF16.push_back(static_cast<unsigned short>(c));
        }
    }
    searchTermUTF16.push_back(0); // Null terminator
    
    // Set up search flags
    unsigned long searchFlags = 0;
    if (state.textSearch.matchCase) {
        searchFlags |= FPDF_MATCHCASE;
    }
    if (state.textSearch.matchWholeWord) {
        searchFlags |= FPDF_MATCHWHOLEWORD;
    }
      // Search through all loaded text pages
    for (int pageIndex = 0; pageIndex < (int)state.textPages.size(); pageIndex++) {
        if (!state.textPages[pageIndex].isLoaded) {
            std::cout << "PerformTextSearch: Page " << pageIndex << " not loaded, skipping" << std::endl;
            continue;
        }
        
        FPDF_TEXTPAGE textPage = state.textPages[pageIndex].textPage;
        if (!textPage) {
            std::cout << "PerformTextSearch: Page " << pageIndex << " has no text page, skipping" << std::endl;
            continue;
        }
        
        std::cout << "PerformTextSearch: Searching page " << pageIndex << std::endl;
        
        // Start search for this page
        FPDF_SCHHANDLE searchHandle = FPDFText_FindStart(
            textPage, 
            reinterpret_cast<FPDF_WIDESTRING>(searchTermUTF16.data()), 
            searchFlags, 
            0  // Start from beginning of page
        );
        
        if (searchHandle) {
            state.textSearch.searchHandles.push_back(searchHandle);
            
            // Find all matches on this page
            bool hasMoreResults = true;
            while (hasMoreResults) {
                if (FPDFText_FindNext(searchHandle)) {
                    SearchResult result;
                    result.pageIndex = pageIndex;
                    result.charIndex = FPDFText_GetSchResultIndex(searchHandle);
                    result.charCount = FPDFText_GetSchCount(searchHandle);
                    result.isValid = true;
                    
                    state.textSearch.results.push_back(result);
                } else {
                    hasMoreResults = false;
                }
            }
        }    }      // If we found results, navigate to the first one
    if (!state.textSearch.results.empty()) {
        state.textSearch.currentResultIndex = 0;
        state.textSearch.showNoMatchMessage = false; // Hide no match message
        state.textSearch.isActive = true; // Enable search highlighting
        std::cout << "PerformTextSearch: Found " << state.textSearch.results.size() << " results" << std::endl;
    } else if (!state.textSearch.searchTerm.empty()) {
        // Show "No match found" message if search term is not empty but no results
        state.textSearch.showNoMatchMessage = true;
        state.textSearch.noMatchMessageTime = glfwGetTime();
        state.textSearch.isActive = false; // Disable highlighting when no results
        std::cout << "PerformTextSearch: No results found" << std::endl;
    } else {
        state.textSearch.isActive = false; // Disable highlighting when search is empty
    }
    
    state.textSearch.needsUpdate = false;
    state.textSearch.searchChanged = false;
}

void NavigateToNextSearchResult(PDFScrollState& state, const std::vector<int>& pageHeights) {
    std::ofstream logFile("build/debug.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "NavigateToNextSearchResult: CALLED" << std::endl;
        logFile << "  results.size()=" << state.textSearch.results.size() << std::endl;
        logFile << "  currentResultIndex BEFORE=" << state.textSearch.currentResultIndex << std::endl;
    }
    logFile.close();
    
    if (state.textSearch.results.empty()) {
        std::ofstream logFile2("build/debug.log", std::ios::app);
        if (logFile2.is_open()) {
            logFile2 << "NavigateToNextSearchResult: EARLY RETURN - no results" << std::endl;
        }
        logFile2.close();
        return;
    }
    
    // Advance, skipping duplicate of current selection if any
    int startIndex = state.textSearch.currentResultIndex;
    int count = (int)state.textSearch.results.size();
    for (int step = 0; step < count; ++step) {
        int next = (startIndex + 1 + step) % count;
        const auto& res = state.textSearch.results[next];
        bool isDuplicateOfSelection = false;
        if (state.textSelection.isActive && res.pageIndex == state.textSelection.startPageIndex) {
            // If selection fully matches this result range on the same page, treat as duplicate
            int selStart = state.textSelection.startCharIndex;
            int selEnd = state.textSelection.endCharIndex;
            int resStart = res.charIndex;
            int resEnd = res.charIndex + res.charCount - 1;
            if (selStart == resStart && selEnd == resEnd) {
                isDuplicateOfSelection = true;
            }
        }
        if (!isDuplicateOfSelection) {
            state.textSearch.currentResultIndex = next;
            break;
        }
        // If all were duplicates, we will exit with the last computed index anyway
        if (step == count - 1) {
            state.textSearch.currentResultIndex = (startIndex + 1) % count;
        }
    }
      std::ofstream logFile3("build/debug.log", std::ios::app);
    if (logFile3.is_open()) {
        logFile3 << "NavigateToNextSearchResult: currentResultIndex AFTER=" << state.textSearch.currentResultIndex << std::endl;
        logFile3 << "  Calling NavigateToSearchResultPrecise with index=" << state.textSearch.currentResultIndex << std::endl;
    }
    logFile3.close();
    
    // Use the precise navigation function with proper window height
    NavigateToSearchResultPrecise(state, pageHeights, state.textSearch.currentResultIndex);
}

void NavigateToPreviousSearchResult(PDFScrollState& state, const std::vector<int>& pageHeights) {
    std::ofstream logFile("build/debug.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "NavigateToPreviousSearchResult: CALLED" << std::endl;
        logFile << "  results.size()=" << state.textSearch.results.size() << std::endl;
        logFile << "  currentResultIndex BEFORE=" << state.textSearch.currentResultIndex << std::endl;
    }
    logFile.close();
    
    if (state.textSearch.results.empty()) {
        std::ofstream logFile2("build/debug.log", std::ios::app);
        if (logFile2.is_open()) {
            logFile2 << "NavigateToPreviousSearchResult: EARLY RETURN - no results" << std::endl;
        }
        logFile2.close();
        return;
    }
    
    // Go back, skipping duplicate of current selection if any
    int startIndex = state.textSearch.currentResultIndex;
    int count = (int)state.textSearch.results.size();
    for (int step = 0; step < count; ++step) {
        int prev = (startIndex - 1 - step + count) % count;
        const auto& res = state.textSearch.results[prev];
        bool isDuplicateOfSelection = false;
        if (state.textSelection.isActive && res.pageIndex == state.textSelection.startPageIndex) {
            int selStart = state.textSelection.startCharIndex;
            int selEnd = state.textSelection.endCharIndex;
            int resStart = res.charIndex;
            int resEnd = res.charIndex + res.charCount - 1;
            if (selStart == resStart && selEnd == resEnd) {
                isDuplicateOfSelection = true;
            }
        }
        if (!isDuplicateOfSelection) {
            state.textSearch.currentResultIndex = prev;
            break;
        }
        if (step == count - 1) {
            state.textSearch.currentResultIndex = (startIndex - 1 + count) % count;
        }
    }
    
    std::ofstream logFile3("build/debug.log", std::ios::app);
    if (logFile3.is_open()) {
        logFile3 << "NavigateToPreviousSearchResult: currentResultIndex AFTER=" << state.textSearch.currentResultIndex << std::endl;
        logFile3 << "  Calling NavigateToSearchResultPrecise with index=" << state.textSearch.currentResultIndex << std::endl;
    }    logFile3.close();
    
    // Use the precise navigation function with proper window height
    NavigateToSearchResultPrecise(state, pageHeights, state.textSearch.currentResultIndex);
}

void ClearSearchResults(PDFScrollState& state) {
    // Close all search handles
    for (FPDF_SCHHANDLE handle : state.textSearch.searchHandles) {
        if (handle) {
            FPDFText_FindClose(handle);
        }
    }
    
    state.textSearch.searchHandles.clear();    state.textSearch.results.clear();
    state.textSearch.currentResultIndex = -1;
}







void PopulateSearchFromSelection(PDFScrollState& state) {
    std::cout << "PopulateSearchFromSelection called" << std::endl;
    
    if (state.textSelection.isActive) {
        std::string selectedText = GetSelectedText(state);
        
        std::cout << "PopulateSearchFromSelection: GetSelectedText returned '" << selectedText << "' (length: " << selectedText.length() << ")" << std::endl;
        
        if (!selectedText.empty()) {
            // Limit search term length for performance
            if (selectedText.length() > 100) {
                selectedText = selectedText.substr(0, 100);
                std::cout << "PopulateSearchFromSelection: Truncated to '" << selectedText << "'" << std::endl;
            }
            
            // Update search term and trigger search
            state.textSearch.searchTerm = selectedText;
            state.textSearch.needsUpdate = true;
            state.textSearch.searchChanged = true;
            
            std::cout << "PopulateSearchFromSelection: Set search term to '" << state.textSearch.searchTerm << "'" << std::endl;
            
            // Show search box if not already visible
            if (!state.textSearch.isSearchBoxVisible) {
                ToggleSearchBox(state);
            }
        } else {
            std::cout << "PopulateSearchFromSelection: Selected text is empty" << std::endl;
        }
    } else {
        std::cout << "PopulateSearchFromSelection: No active text selection" << std::endl;
    }
}

bool HandleSearchButtonClick(PDFScrollState& state, double mouseX, double mouseY, float winWidth, float winHeight) {
    // Search menu bar is always visible now - updated to match new 50px height
    float menuHeight = 50.0f;
    
    // Check if click is within menu bar area
    if (mouseY > menuHeight) {
        return false;
    }
    
    // Calculate positions to match DrawSearchMenuBar layout
    float labelX = -0.95f;
    float inputX = labelX + 0.2f;
    float inputWidth = 0.5f;
    float spacing = 0.01f;
    
    // Navigation buttons
    float navButtonX = inputX + inputWidth + spacing;
    float buttonSize = 0.05f;
    float nextButtonX = navButtonX + buttonSize + spacing;    
    // Checkbox positions (updated for new layout)
    float optionsX = 0.4f;
    float checkboxSize = 0.02f;
    float wholeWordX = optionsX + 0.08f;
    float clearX = 0.9f;
    float clearSize = 0.025f;
    
    // Convert NDC to screen coordinates for hit testing
    float navButtonXScreen = (navButtonX + 1.0f) * winWidth / 2.0f;
    float nextButtonXScreen = (nextButtonX + 1.0f) * winWidth / 2.0f;
    float optionsXScreen = (optionsX + 1.0f) * winWidth / 2.0f;
    float wholeWordXScreen = (wholeWordX + 1.0f) * winWidth / 2.0f;
    float clearXScreen = (clearX + 1.0f) * winWidth / 2.0f;
    
    float buttonSizeScreen = buttonSize * winWidth / 2.0f;
    float checkboxSizeScreen = checkboxSize * winWidth / 2.0f;
    float clearSizeScreen = clearSize * winWidth / 2.0f;
    float buttonYScreen = menuHeight / 2.0f;
    
    // Check previous button click
    if (mouseX >= navButtonXScreen && mouseX <= navButtonXScreen + buttonSizeScreen &&
        mouseY >= buttonYScreen - buttonSizeScreen && mouseY <= buttonYScreen) {
        if (!state.textSearch.results.empty() && state.textSearch.currentResultIndex > 0) {
            NavigateToPreviousSearchResult(state, *state.pageHeights);
        }
        return true;
    }
    
    // Check next button click
    if (mouseX >= nextButtonXScreen && mouseX <= nextButtonXScreen + buttonSizeScreen &&
        mouseY >= buttonYScreen - buttonSizeScreen && mouseY <= buttonYScreen) {
        if (!state.textSearch.results.empty() && state.textSearch.currentResultIndex < (int)state.textSearch.results.size() - 1) {
            NavigateToNextSearchResult(state, *state.pageHeights);
        }
        return true;
    }
      // Check case sensitive checkbox
    if (mouseX >= optionsXScreen && mouseX <= optionsXScreen + checkboxSizeScreen &&
        mouseY >= buttonYScreen - checkboxSizeScreen && mouseY <= buttonYScreen) {
        state.textSearch.matchCase = !state.textSearch.matchCase;
        state.textSearch.needsUpdate = true;
        return true;
    }
    
    // Check whole word checkbox
    if (mouseX >= wholeWordXScreen && mouseX <= wholeWordXScreen + checkboxSizeScreen &&
        mouseY >= buttonYScreen - checkboxSizeScreen && mouseY <= buttonYScreen) {
        state.textSearch.matchWholeWord = !state.textSearch.matchWholeWord;
        state.textSearch.needsUpdate = true;
        return true;
    }
    
    // Check close button (X)
    if (mouseX >= clearXScreen && mouseX <= clearXScreen + clearSizeScreen &&
        mouseY >= buttonYScreen - clearSizeScreen && mouseY <= buttonYScreen) {
        // Clear search term instead of hiding (since search is always visible)
        state.textSearch.searchTerm.clear();
        ClearSearchResults(state);
        return true;
    }
    
    return false;
}

void HandleSearchInput(PDFScrollState& state, int key, int mods) {
    // Search is always active now
    if (key >= 32 && key <= 126) { // Printable ASCII characters
        // Add character to search term
        state.textSearch.searchTerm += static_cast<char>(key);
        state.textSearch.needsUpdate = true;
        state.textSearch.lastInputTime = glfwGetTime();
        
        // Set typing state for cursor animation
        state.textSearch.isTyping = true;
        state.textSearch.searchBoxFocused = true;
        
        // Hide no match message when typing
        state.textSearch.showNoMatchMessage = false;
        
    } else if (key == GLFW_KEY_BACKSPACE && !state.textSearch.searchTerm.empty()) {
        // Remove last character
        state.textSearch.searchTerm.pop_back();
        state.textSearch.needsUpdate = true;
        state.textSearch.lastInputTime = glfwGetTime();
        
        // Set typing state for cursor animation
        state.textSearch.isTyping = true;
        state.textSearch.searchBoxFocused = true;
        
        // Hide no match message when typing
        state.textSearch.showNoMatchMessage = false;
    }
}

void UpdateSearchBoxAnimation(PDFScrollState& state, double currentTime) {
    // Only keep the timing logic needed for Win32 UI
    // No OpenGL animations needed since we use Win32 toolbar
    (void)state;     // Suppress unused parameter warning
    (void)currentTime; // Suppress unused parameter warning
}

void DrawSearchResultsHighlighting(const PDFScrollState& state, const std::vector<int>& pageHeights, const std::vector<int>& pageWidths, float winWidth, float winHeight) {
    // UI requirement: suppress all search result highlights; only show the active blue selection.
    // Keep function to preserve API and call sites; do nothing here.
    (void)state; (void)pageHeights; (void)pageWidths; (void)winWidth; (void)winHeight;
}

// PRECISE NAVIGATION FUNCTION - MATCHES RENDERING COORDINATE SYSTEM EXACTLY
void NavigateToSearchResultPrecise(PDFScrollState& state, const std::vector<int>& pageHeights, int resultIndex) {
    if (resultIndex < 0 || resultIndex >= (int)state.textSearch.results.size()) return;
    
    const SearchResult& result = state.textSearch.results[resultIndex];
    if (!result.isValid || result.pageIndex >= (int)pageHeights.size()) return;
    
    std::ofstream logFile("build/debug.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "NavigateToSearchResultPrecise: CALLED for result " << resultIndex << std::endl;
        logFile << "  result.pageIndex=" << result.pageIndex << ", result.charIndex=" << result.charIndex << std::endl;
        logFile << "  Current viewport height=" << state.viewportHeight << std::endl;
        logFile << "  Current zoom scale=" << state.zoomScale << std::endl;
    }
    logFile.close();
    
    // SIMPLIFIED AND ROBUST NAVIGATION ALGORITHM
    // This uses the SAME coordinate system as your goToPage() function for consistency
    
    // Step 1: Calculate page offset
    float targetPageOffset = 0.0f;
    for (int i = 0; i < result.pageIndex && i < (int)pageHeights.size(); ++i) {
        // IMPORTANT: Rendering stacks pages without spacing; keep math identical here
        targetPageOffset += pageHeights[i] * state.zoomScale;
    }
    
    // Step 2: Get text position within the page (use rect center for stability at high zoom)
    float textOffsetInPage = 0.0f;
    float selectionHeightInPage = 0.0f; // in screen pixels at current zoom
    
    // Get the text page for this result
    if (result.pageIndex < (int)state.textPages.size() && state.textPages[result.pageIndex].isLoaded) {
        FPDF_TEXTPAGE textPage = state.textPages[result.pageIndex].textPage;
        if (textPage) {
            // Get text rectangles for this search result
            int rectCount = FPDFText_CountRects(textPage, result.charIndex, result.charCount);
            double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
            bool gotRect = false;
            if (rectCount > 0) {
                // Use UNION of all rects for robust placement (handles ligatures/wrap)
                double uL = std::numeric_limits<double>::infinity();
                double uR = -std::numeric_limits<double>::infinity();
                double uT = -std::numeric_limits<double>::infinity();
                double uB = std::numeric_limits<double>::infinity();
                for (int ridx = 0; ridx < rectCount; ++ridx) {
                    double l, t, r, b;
                    if (FPDFText_GetRect(textPage, ridx, &l, &t, &r, &b)) {
                        uL = std::min(uL, l);
                        uR = std::max(uR, r);
                        uT = std::max(uT, t); // top is larger Y in PDF coords
                        uB = std::min(uB, b); // bottom is smaller Y
                        gotRect = true;
                    }
                }
                if (gotRect) { left = uL; right = uR; top = uT; bottom = uB; }
            }

            // Fallback: if no rects were returned (rare), approximate from char boxes
            if (!gotRect) {
                int startChar = std::max(0, result.charIndex);
                int endChar = std::max(startChar, std::min(state.textPages[result.pageIndex].charCount - 1, result.charIndex + result.charCount - 1));
                double sL, sT, sR, sB, eL, eT, eR, eB;
                if (FPDFText_GetCharBox(textPage, startChar, &sL, &sT, &sR, &sB) &&
                    FPDFText_GetCharBox(textPage, endChar, &eL, &eT, &eR, &eB)) {
                    // Build a minimal rect from start/end boxes
                    left = std::min(sL, eL);
                    right = std::max(sR, eR);
                    // top is max Y, bottom is min Y in PDF (origin bottom-left)
                    top = std::max(sT, eT);
                    bottom = std::min(sB, eB);
                    gotRect = true;
                }
            }

            if (gotRect && state.originalPageHeights && result.pageIndex < (int)state.originalPageHeights->size()) {
                double originalPageHeight = (*state.originalPageHeights)[result.pageIndex];
                float renderedPageHeight = pageHeights[result.pageIndex] * state.zoomScale;
                // Horizontal centering inputs
                double originalPageWidth = (state.originalPageWidths && result.pageIndex < (int)state.originalPageWidths->size())
                    ? (*state.originalPageWidths)[result.pageIndex] : 0.0;
                float renderedPageWidth = (state.pageWidths && result.pageIndex < (int)state.pageWidths->size())
                    ? (float)((*state.pageWidths)[result.pageIndex] * state.zoomScale) : 0.0f;

                // Compute center Y of the rect in PDF units (bottom-up), then convert to top-down relative
                double rectCenterY = (top + bottom) * 0.5; // still bottom-up units
                double relativeCenterY = (originalPageHeight - rectCenterY) / originalPageHeight; // 0=top
                textOffsetInPage = (float)(relativeCenterY * renderedPageHeight);

                // Also compute selection height in pixels to keep it fully visible if possible
                double rectHeightPDF = std::max(0.0, top - bottom);
                selectionHeightInPage = (float)((rectHeightPDF / originalPageHeight) * renderedPageHeight);

                // Compute relative center X and defer horizontal centering to embedder (has window width)
                if (originalPageWidth > 0.0 && renderedPageWidth > 0.0f) {
                    double rectCenterX = (left + right) * 0.5; // PDF coords
                    double relativeCenterX = rectCenterX / originalPageWidth; // 0..1 from left
                    state.pendingHorizCenter = true;
                    state.pendingHorizPage = result.pageIndex;
                    state.pendingHorizRelX = (float)relativeCenterX;
                }

                std::ofstream logFile2("build/debug.log", std::ios::app);
                if (logFile2.is_open()) {
                    logFile2 << "NavigateToSearchResultPrecise: TEXT POSITIONING (CENTER)" << std::endl;
                    logFile2 << "  PDF rect: L=" << left << ", T=" << top << ", R=" << right << ", B=" << bottom << std::endl;
                    logFile2 << "  Original page height=" << originalPageHeight << " PDF units" << std::endl;
                    logFile2 << "  Rendered page height=" << renderedPageHeight << " screen pixels" << std::endl;
                    logFile2 << "  Rect center Y (PDF)=" << rectCenterY << std::endl;
                    logFile2 << "  Relative center Y=" << relativeCenterY << " (0=top)" << std::endl;
                    logFile2 << "  Text offset in page (center)=" << textOffsetInPage << std::endl;
                    logFile2 << "  Selection height in page px=" << selectionHeightInPage << std::endl;
                }
                logFile2.close();
            }
        }
    }
    
    // Step 3: Calculate final scroll offset to place text near upper-middle of viewport
    // We aim ~40% from the top for readability, consistent across zoom levels
    float centerY = state.viewportHeight * 0.42f; // slight shift for better readability
    
    // Total offset to the text = page offset + text offset within page
    float totalTextOffset = targetPageOffset + textOffsetInPage;
    
    // To center the text: scrollOffset should position text at centerY
    // scrollOffset makes content move up, so: 
    // textScreenPosition = totalTextOffset - scrollOffset
    // We want: textScreenPosition = centerY
    // Therefore: scrollOffset = totalTextOffset - centerY
    float targetScrollOffset = totalTextOffset - centerY;
    
    // Try to keep the entire selection rect visible when possible, especially at high zoom
    if (selectionHeightInPage > 0.0f) {
        // Compute the selection's top and bottom in document pixels
        float selectionTop = totalTextOffset - (selectionHeightInPage * 0.5f);
        float selectionBottom = totalTextOffset + (selectionHeightInPage * 0.5f);

        // After we set targetScrollOffset, selection will appear at:
        // topOnScreen = selectionTop - targetScrollOffset
        // bottomOnScreen = selectionBottom - targetScrollOffset
        // We want: marginTop <= topOnScreen and bottomOnScreen <= viewportHeight - marginBottom
        float marginTop = std::max(8.0f, state.viewportHeight * 0.05f);
        float marginBottom = std::max(8.0f, state.viewportHeight * 0.05f);

        // Adjust targetScrollOffset minimally to satisfy visibility constraints
        float topOnScreen = selectionTop - targetScrollOffset;
        float bottomOnScreen = selectionBottom - targetScrollOffset;

        if (topOnScreen < marginTop) {
            targetScrollOffset -= (marginTop - topOnScreen);
        }
        if (bottomOnScreen > (state.viewportHeight - marginBottom)) {
            targetScrollOffset += (bottomOnScreen - (state.viewportHeight - marginBottom));
        }
    } else if (state.zoomScale > 2.0f) {
        // If we don't know the selection height, add a small context-only adjustment at high zoom
        float zoomAdjustment = std::min(50.0f, 25.0f * (state.zoomScale - 2.0f));
        targetScrollOffset -= zoomAdjustment;
    }
    
    // Step 4: Clamp to valid scroll range
    // Calculate max scroll offset using the same stacking as render (no spacing)
    float totalDocumentHeight = 0.0f;
    for (int h : pageHeights) {
        totalDocumentHeight += h * state.zoomScale;
    }
    // Use same bottom padding as UpdateScrollState to avoid overscrolling past last content
    float bottomPadding = state.viewportHeight * 0.1f; // 10% of viewport height
    float maxScrollOffset = std::max(0.0f, totalDocumentHeight - state.viewportHeight + bottomPadding);
    
    // Clamp the target scroll offset
    if (targetScrollOffset < 0.0f) targetScrollOffset = 0.0f;
    if (targetScrollOffset > maxScrollOffset) targetScrollOffset = maxScrollOffset;

    // Post-clamp visibility correction: if there is room within [0, maxScrollOffset], try to keep selection visible
    if (selectionHeightInPage > 0.0f && maxScrollOffset > 0.0f) {
        float marginTop = std::max(8.0f, state.viewportHeight * 0.05f);
        float marginBottom = std::max(8.0f, state.viewportHeight * 0.05f);
        float selectionTop = totalTextOffset - (selectionHeightInPage * 0.5f);
        float selectionBottom = totalTextOffset + (selectionHeightInPage * 0.5f);
        float topOnScreen = selectionTop - targetScrollOffset;
        float bottomOnScreen = selectionBottom - targetScrollOffset;

        // Compute how much we can move down/up within clamp range
        float slackUp = targetScrollOffset; // how much we can decrease towards 0
        float slackDown = maxScrollOffset - targetScrollOffset; // how much we can increase towards max

        if (topOnScreen < marginTop) {
            float needed = (marginTop - topOnScreen);
            float adjust = std::min(needed, slackDown);
            targetScrollOffset += adjust;
        }
        // recompute after potential adjust
        topOnScreen = selectionTop - targetScrollOffset;
        bottomOnScreen = selectionBottom - targetScrollOffset;
        if (bottomOnScreen > (state.viewportHeight - marginBottom)) {
            float needed = bottomOnScreen - (state.viewportHeight - marginBottom);
            float adjust = std::min(needed, targetScrollOffset); // can only move up by at most current offset
            targetScrollOffset -= adjust;
        }

        // Final clamp after adjustments
        if (targetScrollOffset < 0.0f) targetScrollOffset = 0.0f;
        if (targetScrollOffset > maxScrollOffset) targetScrollOffset = maxScrollOffset;
    }
    
    // Step 5: Apply the navigation
    state.scrollOffset = targetScrollOffset;
    state.maxOffset = maxScrollOffset;
    state.preventScrollOffsetOverride = true;
    state.forceRedraw = true;
    // Request a high-quality regeneration of the newly visible pages so the target result
    // area is rendered crisply without needing an extra pan/zoom interaction.
    state.requestHighQualityVisibleRegen = true;
    
    // Also set active text selection to current result so it renders in blue only
    if (result.pageIndex < (int)state.textPages.size() && state.textPages[result.pageIndex].isLoaded) {
        int pageCharCount = state.textPages[result.pageIndex].charCount;
        int selStart = std::max(0, result.charIndex);
        int selEnd = std::min(pageCharCount - 1, result.charIndex + result.charCount - 1);
        if (pageCharCount > 0 && selStart <= selEnd) {
            state.textSelection.isActive = true;
            state.textSelection.isDragging = false;
            state.textSelection.startPageIndex = result.pageIndex;
            state.textSelection.endPageIndex = result.pageIndex;
            state.textSelection.startCharIndex = selStart;
            state.textSelection.endCharIndex = selEnd;

            // Populate selection bounds (optional, improves accuracy for rendering and copy)
            double sL, sT, sR, sB;
            double eL, eT, eR, eB;
            FPDF_TEXTPAGE tp = state.textPages[result.pageIndex].textPage;
            if (tp && FPDFText_GetCharBox(tp, selStart, &sL, &sT, &sR, &sB) &&
                    FPDFText_GetCharBox(tp, selEnd, &eL, &eT, &eR, &eB)) {
                state.textSelection.startX = sL; state.textSelection.startY = sT;
                state.textSelection.endX = eR;   state.textSelection.endY = eB;
            }

            state.textSelection.selectionZoomScale = state.zoomScale;
            state.textSelection.selectionScrollOffset = state.scrollOffset;
            state.textSelection.selectionHorizontalOffset = state.horizontalOffset;
            state.textSelection.needsCoordinateUpdate = true;
            state.textSelection.isDoubleClick = false;
        }
    }
    
    // Force immediate redraw
    glfwPostEmptyEvent();
    
    // Final debug output
    std::ofstream logFile3("build/debug.log", std::ios::app);
    if (logFile3.is_open()) {
        logFile3 << "NavigateToSearchResultPrecise: NAVIGATION COMPLETED" << std::endl;
        logFile3 << "  Target page offset=" << targetPageOffset << std::endl;
        logFile3 << "  Text offset in page=" << textOffsetInPage << std::endl;
        logFile3 << "  Total text offset=" << totalTextOffset << std::endl;
        logFile3 << "  Viewport center Y=" << centerY << std::endl;
        logFile3 << "  Target scroll offset=" << targetScrollOffset << std::endl;
        logFile3 << "  Max scroll offset=" << maxScrollOffset << std::endl;
        logFile3 << "  Final scroll offset=" << state.scrollOffset << std::endl;
        logFile3 << "  Expected text screen position=" << (totalTextOffset - state.scrollOffset) << std::endl;
        logFile3 << "  Distance from center=" << std::abs((totalTextOffset - state.scrollOffset) - centerY) << std::endl;
        
        // Enhanced debug info for visual positioning analysis
        logFile3 << std::endl << "VIEWPORT POSITIONING ANALYSIS:" << std::endl;
        logFile3 << "  Viewport height=" << state.viewportHeight << std::endl;
        logFile3 << "  Current zoom scale=" << state.zoomScale << std::endl;
        logFile3 << "  Page=" << result.pageIndex + 1 << " of " << pageHeights.size() << std::endl;
        
        // Calculate viewport regions for debugging where text will appear
        float viewportQuarter = state.viewportHeight * 0.25f;
        float viewportMiddle = state.viewportHeight * 0.5f;
        float viewportThreeQuarters = state.viewportHeight * 0.75f;
        
        float finalTextPosition = totalTextOffset - state.scrollOffset;
        std::string textPosition;
        
        if (finalTextPosition < viewportQuarter) {
            textPosition = "TOP QUARTER";
        } else if (finalTextPosition < viewportMiddle) {
            textPosition = "UPPER HALF";
        } else if (finalTextPosition < viewportThreeQuarters) {
            textPosition = "LOWER HALF";
        } else {
            textPosition = "BOTTOM QUARTER";
        }
        
        logFile3 << "  Text will appear in: " << textPosition << " of viewport" << std::endl;
        logFile3 << "  Distance from top of viewport: " << finalTextPosition << " pixels" << std::endl;
        logFile3 << "  % of viewport height: " << (finalTextPosition / state.viewportHeight) * 100.0f << "%" << std::endl;
        
        if (state.zoomScale > 2.0f) {
            logFile3 << "  High zoom adjustment applied: " << std::min(50.0f, 25.0f * (state.zoomScale - 2.0f)) << " pixels" << std::endl;
        }
    }
    logFile3.close();
}

