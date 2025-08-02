/*
 * POTENTIAL FIX for Zoom Jumping Issue on Last Pages
 * 
 * PROBLEM IDENTIFIED:
 * In HandleZoom function (feature.cpp line 308), the boundary calculations use:
 *   float zoomedPageHeightSum = state.pageHeightSum;
 * 
 * But state.pageHeightSum is updated LATER in the function (lines 376-380).
 * This means boundary constraints use the OLD pageHeightSum, which could cause 
 * incorrect scroll offset calculations, especially for last pages.
 * 
 * SOLUTION:
 * Calculate the correct pageHeightSum BEFORE using it for boundary constraints.
 */

// BEFORE FIX (line 307-308 in feature.cpp):
// float zoomedPageWidthMax = GetVisiblePageMaxWidth(state, pageHeights);
// float zoomedPageHeightSum = state.pageHeightSum; // <-- USES OLD VALUE

// AFTER FIX:
// Calculate CORRECT pageHeightSum with new zoom level
float zoomedPageHeightSum = 0.0f;
for (int i = 0; i < (int)pageHeights.size(); ++i) {
    zoomedPageHeightSum += pageHeights[i] * state.zoomScale;  // Use NEW zoomScale
}
float zoomedPageWidthMax = GetVisiblePageMaxWidth(state, pageHeights);

/*
 * This ensures that the boundary constraints (lines 314-350) use the correct
 * content dimensions at the NEW zoom level, preventing the scroll offset
 * from being incorrectly constrained.
 * 
 * This fix is particularly important for last pages because:
 * 1. Last pages are near the maximum scroll offset boundary
 * 2. Incorrect pageHeightSum causes maxVerticalOffset to be wrong
 * 3. This leads to incorrect scroll offset clamping
 * 4. Result: cursor zoom on last pages jumps to earlier pages
 */
