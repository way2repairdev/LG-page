#include "BRDRenderer.h"
#include "BRDTypes.h"
#include <iostream>

BRDRenderer::BRDRenderer() : PCBRenderer() {
    InitializeBRDRendering();
}

BRDRenderer::~BRDRenderer() {
    // Cleanup handled by base class
}

void BRDRenderer::InitializeBRDRendering() {
    // Set BRD-specific default settings
    UpdateBRDSettings();
    brd_rendering_initialized = true;
}

void BRDRenderer::UpdateBRDSettings() {
    // Configure BRD-specific render settings
    auto& settings = GetSettings();
    
    // Enable all BRD-specific features by default
    settings.show_parts = true;
    settings.show_pins = true;
    settings.show_outline = true;
    settings.show_part_outlines = true;
    
    // Adjust alpha values for better visibility with overlapping sides
    if (!separate_sides && mirror_bottom_side) {
        settings.pin_alpha = 0.8f;  // Slightly transparent for overlapping view
        settings.part_alpha = 0.7f;
    } else {
        settings.pin_alpha = 1.0f;
        settings.part_alpha = 1.0f;
    }
}

void BRDRenderer::Render(int window_width, int window_height) {
    if (!brd_rendering_initialized) {
        InitializeBRDRendering();
    }
    
    // Update settings based on current configuration
    UpdateBRDSettings();
    
    // Use base class rendering with BRD-specific enhancements
    PCBRenderer::Render(window_width, window_height);
}

void BRDRenderer::RenderBRDPins(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height) {
    // Use the existing pin rendering methods from base class
    // They now include BRD-specific mirroring logic
    RenderCirclePinsImGui(draw_list, zoom, offset_x, offset_y, window_width, window_height);
    RenderRectanglePinsImGui(draw_list, zoom, offset_x, offset_y, window_width, window_height);
    RenderOvalPinsImGui(draw_list, zoom, offset_x, offset_y, window_width, window_height);
}

void BRDRenderer::RenderBRDParts(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    // Use the existing part rendering methods from base class
    RenderPartOutlineImGui(draw_list, zoom, offset_x, offset_y);
}

void BRDRenderer::RenderBRDOutline(ImDrawList* draw_list, float zoom, float offset_x, float offset_y) {
    // Use the existing outline rendering method from base class
    RenderOutlineImGui(draw_list, zoom, offset_x, offset_y);
}

void BRDRenderer::RenderPinNumbersAsText(ImDrawList* draw_list, float zoom, float offset_x, float offset_y, int window_width, int window_height) {
    // Call base class method - text positioning will be handled by pin geometry which is already mirrored
    PCBRenderer::RenderPinNumbersAsText(draw_list, zoom, offset_x, offset_y, window_width, window_height);
}

void BRDRenderer::CollectPartNamesForRendering(float zoom, float offset_x, float offset_y) {
    // Call base class method - part positioning will be handled by part geometry which is already mirrored
    PCBRenderer::CollectPartNamesForRendering(zoom, offset_x, offset_y);
}

bool BRDRenderer::IsPinOnBottomSide(const BRDPin& pin) const {
    return pin.side == BRDPinSide::Bottom;
}

bool BRDRenderer::IsPartOnBottomSide(const BRDPart& part) const {
    return part.mounting_side == BRDPartMountingSide::Bottom;
}

void BRDRenderer::ApplyBRDTransform(float& x, float& y, bool is_bottom_side) const {
    if (!mirror_bottom_side || !is_bottom_side) {
        return; // No transformation needed
    }
    
    // Apply Y-axis mirroring for bottom side
    y = -y;
    
    // Apply separation offset if enabled
    if (separate_sides) {
        y += side_offset;
    }
}

void BRDRenderer::ApplyBRDPinTransform(const BRDPin& pin, float& x, float& y) const {
    bool is_bottom = IsPinOnBottomSide(pin);
    ApplyBRDTransform(x, y, is_bottom);
}

void BRDRenderer::ApplyBRDPartTransform(const BRDPart& part, float& x, float& y) const {
    bool is_bottom = IsPartOnBottomSide(part);
    ApplyBRDTransform(x, y, is_bottom);
}

ImU32 BRDRenderer::GetPinColor(const BRDPin& pin) const {
    if (IsPinOnBottomSide(pin)) {
        // Blue for bottom side pins
        return IM_COL32(0, 0, 179, 255);  // Dark blue
    } else {
        // Red for top side pins
        return IM_COL32(179, 0, 0, 255);  // Dark red
    }
}

ImU32 BRDRenderer::GetPartColor(const BRDPart& part) const {
    if (IsPartOnBottomSide(part)) {
        // Cyan for bottom side parts
        return IM_COL32(0, 179, 179, 255);  // Dark cyan
    } else {
        // Green for top side parts
        return IM_COL32(0, 179, 0, 255);   // Dark green
    }
}
