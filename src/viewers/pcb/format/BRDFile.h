#pragma once
#include "BRDFileBase.h"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <memory>

class BRDFile : public BRDFileBase {
public:
    BRDFile() = default;
    ~BRDFile();

    // Implementation of pure virtual methods
    bool Load(const std::vector<char>& buffer, const std::string& filepath = "") override;
    bool VerifyFormat(const std::vector<char>& buffer) override;

    // Static factory method
    static std::unique_ptr<BRDFile> LoadFromFile(const std::string& filepath);

private:
    static constexpr std::array<uint8_t, 4> signature = {{0x23, 0xe2, 0x63, 0x28}};
    char *file_buf = nullptr;
    
    // Helper method to generate rendering geometry for pins
    void GenerateRenderingGeometry();
};
