#include "BRDFileBase.h"
#include <limits>
#include <algorithm>

void BRDFileBase::GetBoundingBox(BRDPoint& min_point, BRDPoint& max_point) const {
    if (pins.empty() && parts.empty() && format.empty()) {
        min_point = {0, 0};
        max_point = {0, 0};
        return;
    }

    int min_x = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int min_y = std::numeric_limits<int>::max();
    int max_y = std::numeric_limits<int>::min();

    // Check pins
    for (const auto& pin : pins) {
        min_x = std::min(min_x, pin.pos.x);
        max_x = std::max(max_x, pin.pos.x);
        min_y = std::min(min_y, pin.pos.y);
        max_y = std::max(max_y, pin.pos.y);
    }

    // Check parts
    for (const auto& part : parts) {
        min_x = std::min({min_x, part.p1.x, part.p2.x});
        max_x = std::max({max_x, part.p1.x, part.p2.x});
        min_y = std::min({min_y, part.p1.y, part.p2.y});
        max_y = std::max({max_y, part.p1.y, part.p2.y});
    }

    // Check format points (board outline)
    for (const auto& point : format) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }

    min_point = {min_x, min_y};
    max_point = {max_x, max_y};
}

void BRDFileBase::GetRenderingBoundingBox(BRDPoint& min_point, BRDPoint& max_point) const {
    if (circles.empty() && outline_segments.empty() && part_outline_segments.empty()) {
        // Fallback to original bounding box if no rendering geometry
        GetBoundingBox(min_point, max_point);
        return;
    }

    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::min();
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::min();

    // Check circles (pins and nails with offsets applied)
    for (const auto& circle : circles) {
        float x = static_cast<float>(circle.center.x);
        float y = static_cast<float>(circle.center.y);
        float r = circle.radius;
        
        min_x = std::min(min_x, x - r);
        max_x = std::max(max_x, x + r);
        min_y = std::min(min_y, y - r);
        max_y = std::max(max_y, y + r);
    }

    // Check outline segments (board outline for both sides)
    for (const auto& segment : outline_segments) {
        min_x = std::min({min_x, static_cast<float>(segment.first.x), static_cast<float>(segment.second.x)});
        max_x = std::max({max_x, static_cast<float>(segment.first.x), static_cast<float>(segment.second.x)});
        min_y = std::min({min_y, static_cast<float>(segment.first.y), static_cast<float>(segment.second.y)});
        max_y = std::max({max_y, static_cast<float>(segment.first.y), static_cast<float>(segment.second.y)});
    }

    // Check part outline segments
    for (const auto& segment : part_outline_segments) {
        min_x = std::min({min_x, static_cast<float>(segment.first.x), static_cast<float>(segment.second.x)});
        max_x = std::max({max_x, static_cast<float>(segment.first.x), static_cast<float>(segment.second.x)});
        min_y = std::min({min_y, static_cast<float>(segment.first.y), static_cast<float>(segment.second.y)});
        max_y = std::max({max_y, static_cast<float>(segment.first.y), static_cast<float>(segment.second.y)});
    }

    min_point = {static_cast<int>(min_x), static_cast<int>(min_y)};
    max_point = {static_cast<int>(max_x), static_cast<int>(max_y)};
}

BRDPoint BRDFileBase::GetCenter() const {
    BRDPoint min_point, max_point;
    GetBoundingBox(min_point, max_point);
    
    return {
        (min_point.x + max_point.x) / 2,
        (min_point.y + max_point.y) / 2
    };
}

void BRDFileBase::ClearData() {
    format.clear();
    outline_segments.clear();
    part_outline_segments.clear();
    parts.clear();
    pins.clear();
    nails.clear();
    circles.clear();
    rectangles.clear();
    ovals.clear();
    
    num_format = 0;
    num_parts = 0;
    num_pins = 0;
    num_nails = 0;
    
    valid = false;
    error_msg.clear();
}

bool BRDFileBase::ValidateData() {
    // Basic validation
    if (parts.empty() && pins.empty()) {
        error_msg = "No parts or pins found in file";
        return false;
    }

    // Update counts
    num_format = static_cast<unsigned int>(format.size());
    num_parts = static_cast<unsigned int>(parts.size());
    num_pins = static_cast<unsigned int>(pins.size());
    num_nails = static_cast<unsigned int>(nails.size());

    return true;
}
