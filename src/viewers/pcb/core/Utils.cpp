#include "Utils.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace Utils {

std::vector<char> LoadFile(const std::string& filepath) {
    std::vector<char> data;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file: " + filepath);
        return data;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    data.resize(size);
    if (!file.read(data.data(), size)) {
        LOG_ERROR("Failed to read file: " + filepath);
        data.clear();
    }
    
    file.close();
    return data;
}

bool FileExists(const std::string& filepath) {
    std::ifstream file(filepath);
    return file.good();
}

std::string GetFileExtension(const std::string& filepath) {
    size_t pos = filepath.find_last_of('.');
    if (pos != std::string::npos) {
        return filepath.substr(pos + 1);
    }
    return "";
}

std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

}
