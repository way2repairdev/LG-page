#pragma once
#include "BRDFileBase.h"
#include <memory>

class BRD2File : public BRDFileBase {
public:
    BRD2File() = default;
    ~BRD2File();

    // Implementation of pure virtual methods
    bool Load(const std::vector<char>& buffer, const std::string& filepath = "") override;
    bool VerifyFormat(const std::vector<char>& buffer) override;

    // Static factory method
    static std::unique_ptr<BRD2File> LoadFromFile(const std::string& filepath);

private:
    char *file_buf = nullptr;
};
