#pragma once

#include "PCBRenderer.h"
#include "BRDFileBase.h"
#include <memory>

/**
 * BRDRenderer - Specialized renderer for BRD and BRD2 file formats
 * 
 * This renderer extends PCBRenderer with specific optimizations and features
 * for BRD file formats, including:
 * - Bottom side mirroring support
 * - BRD-specific pin and part rendering
 * - Enhanced text positioning for mirrored components
 * - Format-specific color schemes
 */
class BRDRenderer : public PCBRenderer {
public:
    BRDRenderer();
    ~BRDRenderer();

    // Override base rendering methods for BRD-specific behavior
    void Render(int window_width, int window_height);
    
    // BRD-specific rendering methods
    void RenderBRDPins(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height);
    void RenderBRDParts(ImDrawList* draw_list, float zoom, float offset_x, float offset_y);
    void RenderBRDOutline(ImDrawList* draw_list, float zoom, float offset_x, float offset_y);
    
    // Override text rendering for BRD mirroring support
    void RenderPinNumbersAsText(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height);
    void CollectPartNamesForRendering(float zoom, float offset_x, float offset_y);
    
    // BRD-specific features
    void SetBottomSideMirroringEnabled(bool enabled) { mirror_bottom_side = enabled; }
    bool IsBottomSideMirroringEnabled() const { return mirror_bottom_side; }
    
    void SetSideSeparationEnabled(bool enabled) { separate_sides = enabled; }
    bool IsSideSeparationEnabled() const { return separate_sides; }
    
    void SetSideSeparationOffset(float offset) { side_offset = offset; }
    float GetSideSeparationOffset() const { return side_offset; }

protected:
    // BRD-specific helper methods
    bool IsPinOnBottomSide(const BRDPin& pin) const;
    bool IsPartOnBottomSide(const BRDPart& part) const;
    
    // Coordinate transformation for bottom side elements
    void ApplyBRDTransform(float& x, float& y, bool is_bottom_side) const;
    void ApplyBRDPinTransform(const BRDPin& pin, float& x, float& y) const;
    void ApplyBRDPartTransform(const BRDPart& part, float& x, float& y) const;
    
    // BRD-specific color schemes
    ImU32 GetPinColor(const BRDPin& pin) const;
    ImU32 GetPartColor(const BRDPart& part) const;
    
private:
    // BRD-specific settings
    bool mirror_bottom_side = true;     // Enable Y-axis mirroring for bottom side
    bool separate_sides = false;        // Enable spatial separation of top/bottom sides
    float side_offset = 0.0f;           // Offset for bottom side when separated
    
    // BRD-specific rendering state
    bool brd_rendering_initialized = false;
    
    // Helper methods
    void InitializeBRDRendering();
    void UpdateBRDSettings();
};
