// Test file to verify BRD format support

#include "format/BRDFile.h"
#include "format/BRD2File.h"
#include <iostream>
#include <vector>

int main() {
    std::cout << "Testing BRD file format detection..." << std::endl;
    
    // Test BRD format detection
    std::vector<char> brd_test_data = {'s', 't', 'r', '_', 'l', 'e', 'n', 'g', 't', 'h', ':', '\n',
                                       'v', 'a', 'r', '_', 'd', 'a', 't', 'a', ':', '\n'};
    
    BRDFile brd_file;
    bool is_brd = brd_file.VerifyFormat(brd_test_data);
    std::cout << "BRD format detected: " << (is_brd ? "YES" : "NO") << std::endl;
    
    // Test BRD2 format detection
    std::vector<char> brd2_test_data = {'B', 'R', 'D', 'O', 'U', 'T', ':', '\n',
                                        'N', 'E', 'T', 'S', ':', '\n'};
    
    BRD2File brd2_file;
    bool is_brd2 = brd2_file.VerifyFormat(brd2_test_data);
    std::cout << "BRD2 format detected: " << (is_brd2 ? "YES" : "NO") << std::endl;
    
    std::cout << "Format detection test completed." << std::endl;
    return 0;
}
