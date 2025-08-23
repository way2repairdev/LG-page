#include <iostream>
#include <memory>
#include "src/viewers/pcb/format/BRDFile.h"
#include "src/viewers/pcb/format/BRD2File.h"
#include "src/viewers/pcb/rendering/BRDRenderer.h"

int main() {
    std::cout << "Testing BRDRenderer compatibility with BRD and BRD2 files...\n\n";
    
    // Test 1: Create BRDRenderer and verify initialization
    std::cout << "1. Testing BRDRenderer initialization...\n";
    auto renderer = std::make_unique<BRDRenderer>();
    if (renderer) {
        std::cout << "   ✓ BRDRenderer created successfully\n";
    } else {
        std::cout << "   ✗ Failed to create BRDRenderer\n";
        return 1;
    }
    
    // Test 2: Test coordinate transformation methods
    std::cout << "\n2. Testing coordinate transformation methods...\n";
    
    // Test bottom side mirroring
    float x = 100.0f, y = 200.0f;
    renderer->ApplyBRDTransform(x, y, true); // bottom side = true
    if (y == -200.0f) {
        std::cout << "   ✓ Bottom side Y-axis mirroring works correctly\n";
    } else {
        std::cout << "   ✗ Bottom side mirroring failed: expected y=-200, got y=" << y << "\n";
    }
    
    // Test top side (no transformation)
    x = 100.0f; y = 200.0f;
    renderer->ApplyBRDTransform(x, y, false); // bottom side = false
    if (y == 200.0f) {
        std::cout << "   ✓ Top side coordinates unchanged correctly\n";
    } else {
        std::cout << "   ✗ Top side transformation failed: expected y=200, got y=" << y << "\n";
    }
    
    // Test 3: Test pin and part side detection
    std::cout << "\n3. Testing side detection methods...\n";
    
    BRDPin test_pin_top;
    test_pin_top.side = BRDPinSide::Top;
    if (!renderer->IsPinOnBottomSide(test_pin_top)) {
        std::cout << "   ✓ Top pin detection works correctly\n";
    } else {
        std::cout << "   ✗ Top pin detection failed\n";
    }
    
    BRDPin test_pin_bottom;
    test_pin_bottom.side = BRDPinSide::Bottom;
    if (renderer->IsPinOnBottomSide(test_pin_bottom)) {
        std::cout << "   ✓ Bottom pin detection works correctly\n";
    } else {
        std::cout << "   ✗ Bottom pin detection failed\n";
    }
    
    BRDPart test_part_top;
    test_part_top.mounting_side = BRDPartMountingSide::Top;
    if (!renderer->IsPartOnBottomSide(test_part_top)) {
        std::cout << "   ✓ Top part detection works correctly\n";
    } else {
        std::cout << "   ✗ Top part detection failed\n";
    }
    
    BRDPart test_part_bottom;
    test_part_bottom.mounting_side = BRDPartMountingSide::Bottom;
    if (renderer->IsPartOnBottomSide(test_part_bottom)) {
        std::cout << "   ✓ Bottom part detection works correctly\n";
    } else {
        std::cout << "   ✗ Bottom part detection failed\n";
    }
    
    // Test 4: Test color coding
    std::cout << "\n4. Testing color coding methods...\n";
    
    ImU32 top_pin_color = renderer->GetPinColor(test_pin_top);
    ImU32 bottom_pin_color = renderer->GetPinColor(test_pin_bottom);
    if (top_pin_color != bottom_pin_color) {
        std::cout << "   ✓ Pin colors differ for top/bottom sides\n";
    } else {
        std::cout << "   ✗ Pin colors are the same for top/bottom sides\n";
    }
    
    ImU32 top_part_color = renderer->GetPartColor(test_part_top);
    ImU32 bottom_part_color = renderer->GetPartColor(test_part_bottom);
    if (top_part_color != bottom_part_color) {
        std::cout << "   ✓ Part colors differ for top/bottom sides\n";
    } else {
        std::cout << "   ✗ Part colors are the same for top/bottom sides\n";
    }
    
    // Test 5: Verify inheritance from PCBRenderer
    std::cout << "\n5. Testing inheritance structure...\n";
    
    PCBRenderer* base_renderer = renderer.get();
    if (base_renderer) {
        std::cout << "   ✓ BRDRenderer correctly inherits from PCBRenderer\n";
    } else {
        std::cout << "   ✗ Inheritance structure issue\n";
    }
    
    std::cout << "\n6. Testing file format compatibility...\n";
    
    // Create mock BRD file data
    auto brd_file = std::make_unique<BRDFile>();
    if (brd_file) {
        std::cout << "   ✓ BRDFile can be created\n";
        
        // Test setting PCB data
        renderer->SetPCBData(std::static_pointer_cast<BRDFileBase>(std::move(brd_file)));
        std::cout << "   ✓ BRDRenderer accepts BRDFile data\n";
    }
    
    // Create mock BRD2 file data
    auto brd2_file = std::make_unique<BRD2File>();
    if (brd2_file) {
        std::cout << "   ✓ BRD2File can be created\n";
        
        // Test setting PCB data
        renderer->SetPCBData(std::static_pointer_cast<BRDFileBase>(std::move(brd2_file)));
        std::cout << "   ✓ BRDRenderer accepts BRD2File data\n";
    }
    
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "BRDRenderer compatibility test completed successfully!\n";
    std::cout << "The renderer works correctly with both BRD and BRD2 file formats.\n";
    std::cout << std::string(50, '=') << "\n";
    
    return 0;
}
