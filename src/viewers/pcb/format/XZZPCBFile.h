#pragma once

#include "BRDFileBase.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <list>

class XZZPCBFile : public BRDFileBase {
public:
    XZZPCBFile() = default;
    ~XZZPCBFile() = default;

    // Implementation of pure virtual methods
    bool Load(const std::vector<char>& buffer, const std::string& filepath = "") override;
    bool VerifyFormat(const std::vector<char>& buffer) override;

    // Static factory method
    static std::unique_ptr<XZZPCBFile> LoadFromFile(const std::string& filepath);

    // Legacy compatibility method
    void CreateEnhancedSampleData();

private:
    std::unordered_map<uint32_t, std::string> net_dict;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> diode_dict; // <Net Name, <Pin Name, Reading>>
    std::unordered_map<std::string, std::string> part_alias_dict; // <Reference (original part name), Alias (new part name)>
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> json_diode_dict; // <Reference (part name), <Pin Name, Diode Reading>>
    BRDPoint xy_translation = {0, 0};
    int diode_readings_type = 0; // 0 = No readings, 1 = Based on part name and pin name, 2 = Based on net

    // Core parsing method
    bool ParseXZZPCBOriginal(std::vector<char>& buf);
    
    // DES decryption
    void des_decrypt(std::vector<char>& buf);
    
    // Arc conversion
    std::vector<std::pair<BRDPoint, BRDPoint>> xzz_arc_to_segments(int startAngle, int endAngle, int r, BRDPoint pc);
    
    // Block parsing methods
    void ProcessBlockOriginal(uint8_t block_type, std::vector<char>& block_buf);
    void ParseArcBlockOriginal(std::vector<uint32_t>& buf);
    void ParseLineSegmentBlockOriginal(std::vector<uint32_t>& buf);
    void ParsePartBlockOriginal(std::vector<char>& buf);
    void ParseTestPadBlockOriginal(std::vector<uint8_t>& buf);
    void ParsePostV6(std::vector<char>::iterator v6_pos, std::vector<char>& buf);
    void ParseNetBlockOriginal(std::vector<char>& buf);
    void ParseJsonData(std::vector<char>::iterator json_start, std::vector<char>& buf);
    void DumpHexAroundPosition(const std::vector<char>& buf, size_t pos, size_t range = 50);
    
    // String handling
    char read_utf8_char(char c) const;
    std::string read_cb2312_string(const std::string& str);
    
    // Translation functions
    void FindXYTranslation();
    void TranslateSegments();
    void TranslatePartOutlineSegments();
    void TranslatePoints(BRDPoint& point) const;
    void TranslatePins();
    void TranslateCircles();
    void TranslateRectangles();
    void TranslateOvals();
};
