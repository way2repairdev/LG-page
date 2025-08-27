#pragma once

#include <string>

// Lightweight theme spec used to apply colors from JSON/config at runtime
// All color components are 0..1 floats.
struct PCBThemeSpec {
    std::string name;                 // Display name in the combo
    std::string base;                 // Optional: "Default", "Light", or "HighContrast" to start from
    bool overridePinColors = false;   // If true, use pin_color for all non-special pins

    // Colors
    float background_r = 0.0f, background_g = 0.0f, background_b = 0.0f;
    float outline_r = 1.0f, outline_g = 1.0f, outline_b = 1.0f;
    float part_outline_r = 1.0f, part_outline_g = 1.0f, part_outline_b = 1.0f;
    float pin_r = 1.0f, pin_g = 1.0f, pin_b = 0.0f;                     // default accent
    float same_net_pin_r = 1.0f, same_net_pin_g = 1.0f, same_net_pin_b = 0.0f; // same-net highlight
    float nc_pin_r = 0.0f, nc_pin_g = 0.3f, nc_pin_b = 0.3f;            // NC pins
    float ground_pin_r = 0.376f, ground_pin_g = 0.376f, ground_pin_b = 0.376f; // GND family
    float ratsnet_r = 0.0f, ratsnet_g = 1.0f, ratsnet_b = 1.0f;
    float part_highlight_border_r = 1.0f, part_highlight_border_g = 1.0f, part_highlight_border_b = 0.0f;
    float part_highlight_fill_r = 1.0f, part_highlight_fill_g = 1.0f, part_highlight_fill_b = 0.0f;

    // Alphas
    float part_alpha = 0.8f;
    float pin_alpha = 0.9f;
    float outline_alpha = 1.0f;
    float part_outline_alpha = 0.9f;

    // Text/label colors (for runtime text overlays)
    // Pin text (pin number/name) and net text colors
    float pin_text_r = 0.5f, pin_text_g = 0.5f, pin_text_b = 0.5f;       // default gray
    float net_text_r = 0.5f, net_text_g = 0.5f, net_text_b = 0.5f;       // default gray
    float diode_text_r = 0.0f, diode_text_g = 1.0f, diode_text_b = 1.0f;  // cyan

    // Component name (part name) text and background
    float component_name_text_r = 1.0f, component_name_text_g = 1.0f, component_name_text_b = 1.0f; // white
    float component_name_bg_r = 0.0f, component_name_bg_g = 0.0f, component_name_bg_b = 0.0f;        // black
    float component_name_bg_a = 0.5f; // semi-transparent background
};
