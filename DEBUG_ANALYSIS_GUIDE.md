# Zoom Jumping Issue - Debug Analysis Guide

## What to Look For in zoom_debug.txt

### 1. **Problematic Jump Pattern**
Look for entries like:
```
NEAR LAST PAGE: YES
Visible pages: 8 to 10
... (after HandleZoom) ...
New visible pages: 3 to 5
!!! PROBLEMATIC JUMP: Was on last pages, now on earlier pages !!!
```

### 2. **Scroll Offset Issues**
Compare before/after values:
```
Current scrollOffset: 2500.0
... (after zoom) ...
New scrollOffset: 1200.0  <-- This should NOT decrease when zooming in on last pages
```

### 3. **Cursor Position vs Calculation**
Check if cursor position is properly calculated:
```
Cursor position: (400, 500)
Window size: 800x600
```
The cursor should be within window bounds.

## Likely Root Causes

### **Cause 1: Boundary Condition in HandleZoom**
- The HandleZoom function may not properly handle cursor positions when on last pages
- It might assume there's always content below the cursor position

### **Cause 2: Page Height Calculation Error**
- The total document height calculation might be wrong
- When zooming, the scroll offset gets recalculated incorrectly

### **Cause 3: Viewport vs Document Coordinate Mismatch**
- The cursor coordinates might be in viewport space
- But HandleZoom expects document space coordinates

### **Cause 4: Embedded vs Standalone Context Difference**
- Window dimensions might be different between embedded and standalone
- Different scroll state initialization

## Testing Strategy

### Test 1: Compare Standalone vs Embedded
1. Test the SAME PDF file in both standalone and embedded viewer
2. Navigate to the same last page
3. Use identical zoom operations
4. Compare the zoom_debug.txt files

### Test 2: Window Size Impact
1. Try resizing the embedded viewer window
2. Test zoom on last pages with different window sizes
3. Check if the issue correlates with window dimensions

### Test 3: Cursor Position Testing
1. Try zooming at different cursor positions on the last page:
   - Top-left corner
   - Center of page
   - Bottom-right corner
2. See which positions trigger the jump

## Next Steps After Debugging

Based on the debug output, we can:

1. **If scroll offset jumps unexpectedly:**
   - Fix the HandleZoom scroll offset calculation
   - Add boundary checks for last pages

2. **If cursor coordinates are wrong:**
   - Fix coordinate system conversion
   - Ensure proper viewport-to-document mapping

3. **If page height calculations are wrong:**
   - Fix the total document height calculation
   - Ensure proper page dimension scaling

4. **If it's a boundary condition:**
   - Add special handling for last pages
   - Prevent scroll offset from going beyond valid range
