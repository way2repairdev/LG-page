#pragma once

#include <string>
#include <vector>
#include <iostream>

// Simple logging macros
#define LOG_ERROR(msg) std::cerr << "ERROR: " << msg << std::endl
#define LOG_INFO(msg) std::cout << "INFO: " << msg << std::endl
#define LOG_DEBUG(msg) std::cout << "DEBUG: " << msg << std::endl

// Simple assertion macro
#define ENSURE(condition, error_msg) \
    if (!(condition)) { \
        LOG_ERROR(error_msg); \
        return false; \
    }

namespace Utils {
    // Load entire file into memory
    std::vector<char> LoadFile(const std::string& filepath);
    
    // Check if file exists
    bool FileExists(const std::string& filepath);
    
    // Get file extension
    std::string GetFileExtension(const std::string& filepath);
    
    // Convert string to lowercase
    std::string ToLower(const std::string& str);
}
