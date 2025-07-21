cmake_minimum_required(VERSION 3.15)
project(PCBOpenViewer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set up local vcpkg paths
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(VCPKG_TARGET_TRIPLET "x64-windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(VCPKG_TARGET_TRIPLET "x64-linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(VCPKG_TARGET_TRIPLET "x64-osx")
endif()

set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
set(CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR};${VCPKG_INSTALLED_DIR}/share")

# Add include directories
include_directories("${VCPKG_INSTALLED_DIR}/include")
link_directories("${VCPKG_INSTALLED_DIR}/lib")

# For debug builds, also add debug lib directory
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    link_directories("${VCPKG_INSTALLED_DIR}/debug/lib")
endif()

# Find packages
find_package(OpenGL REQUIRED)

# Include directories
include_directories(src)
include_directories(src/core)
include_directories(src/formats)
include_directories(src/renderer)

# Source files
set(CORE_SOURCES
    src/core/BRDTypes.cpp
    src/core/Utils.cpp
)

set(FORMAT_SOURCES
    src/formats/BRDFileBase.cpp
    src/formats/XZZPCBFile.cpp
    src/formats/des.cpp
)

set(RENDERER_SOURCES
    src/renderer/PCBRenderer.cpp
    src/renderer/Window.cpp
)

set(MAIN_SOURCES
    src/main.cpp
)

# Create executable
add_executable(pcb_viewer
    ${CORE_SOURCES}
    ${FORMAT_SOURCES}
    ${RENDERER_SOURCES}
    ${MAIN_SOURCES}
)

# Link libraries
target_link_libraries(pcb_viewer
    ${OPENGL_LIBRARIES}
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(pcb_viewer
        glfw3dll
        glew32
        imgui
        comdlg32
    )
elseif(UNIX AND NOT APPLE)
    # Linux
    target_link_libraries(pcb_viewer
        glfw
        GLEW
        imgui
        ${CMAKE_DL_LIBS}
    )
elseif(APPLE)
    # macOS
    target_link_libraries(pcb_viewer
        glfw
        GLEW
        imgui
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()

# Compiler-specific options
if(MSVC)
    target_compile_definitions(pcb_viewer PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
endif()

# Copy test files to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_files DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Copy required DLLs to output directory
if(WIN32)
    # Copy vcpkg DLLs to the output directory
    set(VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/bin")
    
    # List of required DLLs
    set(REQUIRED_DLLS
        "${VCPKG_BIN_DIR}/glew32.dll"
        "${VCPKG_BIN_DIR}/glfw3.dll"
    )
    
    # Copy DLLs to both Debug and Release directories
    foreach(DLL_FILE ${REQUIRED_DLLS})
        if(EXISTS ${DLL_FILE})
            file(COPY ${DLL_FILE} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Debug)
            file(COPY ${DLL_FILE} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Release)
        endif()
    endforeach()
    
    # Also copy DLLs using a custom target that runs after build
    add_custom_command(TARGET pcb_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_BIN_DIR}/glew32.dll"
        $<TARGET_FILE_DIR:pcb_viewer>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_BIN_DIR}/glfw3.dll"
        $<TARGET_FILE_DIR:pcb_viewer>
        COMMENT "Copying required DLLs to output directory"
    )
endif()
