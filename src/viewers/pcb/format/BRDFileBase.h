#pragma once

#include "BRDTypes.h"
#include "Utils.h"
#include <vector>
#include <string>
#include <memory>

// Base class for all PCB file formats
class BRDFileBase {
public:
    // File format information
    unsigned int num_format = 0;
    unsigned int num_parts = 0;
    unsigned int num_pins = 0;
    unsigned int num_nails = 0;

    // PCB data
    std::vector<BRDPoint> format;                                    // Board outline
    std::vector<std::pair<BRDPoint, BRDPoint>> outline_segments;    // Board outline segments
    std::vector<std::pair<BRDPoint, BRDPoint>> part_outline_segments; // Part outline segments
    std::vector<BRDPart> parts;                                     // Components
    std::vector<BRDPin> pins;                                       // Pins/pads
    std::vector<BRDNail> nails;                                     // Test points
    std::vector<BRDCircle> circles;                                 // Circles for rendering
    std::vector<BRDRectangle> rectangles;                           // Rectangles for rendering
    std::vector<BRDOval> ovals;                                     // Ovals for rendering

    // Status
    bool valid = false;
    std::string error_msg = "";

    // Constructor/Destructor
    BRDFileBase() = default;
    virtual ~BRDFileBase() = default;

    // Pure virtual methods to be implemented by derived classes
    virtual bool Load(const std::vector<char>& buffer, const std::string& filepath = "") = 0;
    virtual bool VerifyFormat(const std::vector<char>& buffer) = 0;    // Helper methods
    bool IsValid() const { return valid; }
    const std::string& GetErrorMessage() const { return error_msg; }
    void SetValid(bool v) { valid = v; }
    
    // Get bounding box of the PCB (original positions)
    void GetBoundingBox(BRDPoint& min_point, BRDPoint& max_point) const;
    
    // Get bounding box including rendered geometry (both top and bottom sides)
    void GetRenderingBoundingBox(BRDPoint& min_point, BRDPoint& max_point) const;
    
    // Get center point of the PCB
    BRDPoint GetCenter() const;

protected:
    // Helper functions for derived classes
    void ClearData();
    bool ValidateData();
};
