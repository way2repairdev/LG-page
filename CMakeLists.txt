cmake_minimum_required(VERSION 3.16)

# Configure vcpkg toolchain before project declaration
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

project(Way2RepairLoginSystem VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler specific flags
if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra)
    # Fix for AutoMoc with MinGW
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_WIN32_WINNT=0x0601")
endif()

# Qt configuration
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Ensure Qt tools can be found - using dynamic detection
if(NOT Qt${QT_VERSION_MAJOR}_FOUND)
    if(WIN32 AND MSVC)
        # For Visual Studio, try to find MSVC Qt installations dynamically
        set(QT_POSSIBLE_PATHS 
            "C:/Qt/6.9.1/msvc2022_64"
            "C:/Qt/6.9.1/msvc2019_64"
            "C:/Qt/6.8.0/msvc2022_64"
            "C:/Qt/6.7.0/msvc2022_64"
            "C:/Qt/6.6.0/msvc2022_64"
            "C:/Qt/6.5.0/msvc2022_64"
        )
        foreach(QT_PATH ${QT_POSSIBLE_PATHS})
            if(EXISTS ${QT_PATH})
                set(CMAKE_PREFIX_PATH "${QT_PATH};${CMAKE_PREFIX_PATH}")
                break()
            endif()
        endforeach()
    else()
        # For MinGW, try to find MinGW Qt installations dynamically
        set(QT_POSSIBLE_PATHS 
            "C:/Qt/6.9.1/mingw_64"
            "C:/Qt/6.9.0/mingw_64"
            "C:/Qt/6.8.0/mingw_64"
            "C:/Qt/6.7.0/mingw_64"
            "C:/Qt/6.6.0/mingw_64"
            "C:/Qt/6.5.0/mingw_64"
        )
        foreach(QT_PATH ${QT_POSSIBLE_PATHS})
            if(EXISTS ${QT_PATH})
                set(CMAKE_PREFIX_PATH "${QT_PATH};${CMAKE_PREFIX_PATH}")
                break()
            endif()
        endforeach()
    endif()
endif()

# Find Qt6 packages
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Sql Network OpenGL OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Sql Network OpenGL OpenGLWidgets)
# Optionally find QtSvg for icon generation helper tool
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Svg)

# Try to find PDF modules (optional)
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Pdf PdfWidgets)
if(Qt${QT_VERSION_MAJOR}Pdf_FOUND AND Qt${QT_VERSION_MAJOR}PdfWidgets_FOUND)
    message(STATUS "Qt PDF modules found - enabling hybrid PDF viewer")
    set(HAVE_QT_PDF TRUE)
else()
    message(STATUS "Qt PDF modules not found - using custom PDF viewer only")
    set(HAVE_QT_PDF FALSE)
endif()

# Find OpenGL
find_package(OpenGL REQUIRED)

# PCB Viewer vcpkg dependencies - use MinGW-compatible libraries
set(PCB_VCPKG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/vcpkg/packages)
set(PCB_VCPKG_IMGUI_ROOT ${PCB_VCPKG_ROOT}/imgui_x64-mingw-dynamic)
set(PCB_VCPKG_GLFW_ROOT ${PCB_VCPKG_ROOT}/glfw3_x64-mingw-dynamic)
set(PCB_VCPKG_GLEW_ROOT ${PCB_VCPKG_ROOT}/glew_x64-mingw-dynamic)

# ImGui backend source directory
set(IMGUI_BACKENDS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/vcpkg/buildtrees/imgui/src/v1.91.9-afb09617a6.clean/backends)

# Set include and library paths for MinGW libraries
set(PCB_VCPKG_INCLUDE ${PCB_VCPKG_IMGUI_ROOT}/include ${PCB_VCPKG_GLFW_ROOT}/include ${PCB_VCPKG_GLEW_ROOT}/include ${IMGUI_BACKENDS_DIR})
set(PCB_VCPKG_LIB ${PCB_VCPKG_IMGUI_ROOT}/lib ${PCB_VCPKG_GLFW_ROOT}/lib ${PCB_VCPKG_GLEW_ROOT}/lib)
set(PCB_VCPKG_BIN ${PCB_VCPKG_GLFW_ROOT}/bin ${PCB_VCPKG_GLEW_ROOT}/bin)

message(STATUS "PCB vcpkg root: ${PCB_VCPKG_ROOT}")
message(STATUS "PCB vcpkg include: ${PCB_VCPKG_INCLUDE}")
message(STATUS "PCB vcpkg lib: ${PCB_VCPKG_LIB}")

# Set directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)

# Tell Qt where to find UI files
set(CMAKE_AUTOUIC_SEARCH_PATHS ${RESOURCES_DIR}/ui)

# Include directories - all paths relative to project directory
include_directories(
    ${INCLUDE_DIR}
    ${INCLUDE_DIR}/ui
    ${INCLUDE_DIR}/database
    ${INCLUDE_DIR}/viewers/pdf
    ${INCLUDE_DIR}/viewers/pcb
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/database
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/pdfium/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/glew/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/glfw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/rendering
    ${PCB_VCPKG_INCLUDE}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Source files - all paths relative to project directory
set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/memoryfilemanager.cpp
)

# UI sources - conditionally include hybrid PDF viewer
set(UI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/mainwindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/mainapplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/awsconfigdialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/titlebarwidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dualtabwidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LoadingOverlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/toastnotifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/ui/pdfviewerwidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/core/PDFPreviewLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/core/PDFViewerEmbedder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/rendering/OpenGLPipelineManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/ui/pcbviewerwidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/core/PCBViewerEmbedder.cpp
)

# Add hybrid PDF viewer only if Qt PDF modules are available
if(HAVE_QT_PDF)
    list(APPEND UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/hybridpdfviewer.cpp)
endif()

# PDF viewer sources
set(PDF_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/src/rendering/pdf-render.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/src/core/feature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/src/ui/menu-integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/src/ui/tab-manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/src/globals.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/core/AsyncRender.cpp
)

# PCB viewer sources
set(PCB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/core/BRDTypes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/core/Utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRDFileBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/XZZPCBFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRDFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRD2File.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/des.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/rendering/PCBRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/rendering/BRDRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/rendering/Window.cpp
    # ImGui implementation sources for GLFW and OpenGL
    ${IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_opengl3.cpp
    # Also compile the OpenGL2 backend so we can run on intermediate (GL 2.1) contexts
    ${IMGUI_BACKENDS_DIR}/imgui_impl_opengl2.cpp
)

set(DATABASE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/database/databasemanager.cpp
)

set(NETWORK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/awsclient.cpp
)

# Header files
# UI headers - conditionally include hybrid PDF viewer
set(UI_HEADERS
    ${INCLUDE_DIR}/ui/mainwindow.h
    ${INCLUDE_DIR}/ui/mainapplication.h
    ${INCLUDE_DIR}/ui/awsconfigdialog.h
    ${INCLUDE_DIR}/ui/titlebarwidget.h
    ${INCLUDE_DIR}/ui/dualtabwidget.h
    ${INCLUDE_DIR}/ui/LoadingOverlay.h
    ${INCLUDE_DIR}/viewers/pdf/pdfviewerwidget.h
    ${INCLUDE_DIR}/viewers/pdf/PDFPreviewLoader.h
    ${INCLUDE_DIR}/viewers/pdf/PDFViewerEmbedder.h
    ${INCLUDE_DIR}/viewers/pdf/OpenGLPipelineManager.h
    ${INCLUDE_DIR}/viewers/pcb/PCBViewerWidget.h
    ${INCLUDE_DIR}/viewers/pcb/PCBViewerEmbedder.h
)

# Add hybrid PDF viewer header only if Qt PDF modules are available
if(HAVE_QT_PDF)
    list(APPEND UI_HEADERS ${INCLUDE_DIR}/ui/hybridpdfviewer.h)
endif()

# PDF viewer headers - all paths relative to project directory
set(PDF_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/include/rendering/pdf-render.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/include/core/feature.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/include/ui/menu-integration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/include/ui/tab-manager.h
)

# PCB viewer headers
set(PCB_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/core/BRDTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/core/Utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRDFileBase.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/XZZPCBFile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRDFile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/BRD2File.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/format/des.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/rendering/PCBRenderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/rendering/BRDRenderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/rendering/Window.h
)

set(DATABASE_HEADERS
    ${INCLUDE_DIR}/database/databasemanager.h
)

set(NETWORK_HEADERS
    ${INCLUDE_DIR}/network/awsclient.h
)

# UI files - using dynamic paths
set(UI_FORMS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/mainwindow.ui
)

# Resource files - using dynamic paths
set(RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc
)

# All project sources
set(PROJECT_SOURCES
    ${CORE_SOURCES}
    ${UI_SOURCES}
    ${PDF_SOURCES}
    ${PCB_SOURCES}
    ${DATABASE_SOURCES}
    ${NETWORK_SOURCES}
    ${UI_HEADERS}
    ${PDF_HEADERS}
    ${PCB_HEADERS}
    ${DATABASE_HEADERS}
    ${NETWORK_HEADERS}
    ${UI_FORMS}
    ${RESOURCE_FILES}
)

# Create executable
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Way2RepairLoginSystem
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
    # Optionally embed Windows .ico into the executable (improves taskbar/shortcut icon)
    if(WIN32)
        set(APP_ICO_SVG "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/icons/Way2Repair_Logo.svg")
        set(APP_ICO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/icons/Way2Repair_Logo.ico")
        set(APP_ICO_GEN "${CMAKE_CURRENT_BINARY_DIR}/Way2Repair_Logo.ico")

        # Try to generate an .ico from SVG if missing
        if(NOT EXISTS ${APP_ICO_SRC})
            set(APP_RC_GEN "${CMAKE_CURRENT_BINARY_DIR}/app_icon_gen.rc")
            # Prefer building a tiny Qt-based svg2ico tool if QtSvg is available
            if(Qt${QT_VERSION_MAJOR}Svg_FOUND)
                add_executable(svg2ico ${CMAKE_CURRENT_SOURCE_DIR}/tools/svg2ico.cpp)
                target_link_libraries(svg2ico PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Svg)
                set_target_properties(svg2ico PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
                add_custom_command(
                    OUTPUT ${APP_ICO_GEN}
                    COMMAND $<TARGET_FILE:svg2ico> "${APP_ICO_SVG}" "${APP_ICO_GEN}" 256
                    DEPENDS svg2ico ${APP_ICO_SVG}
                    COMMENT "Generating ICO from SVG using svg2ico helper"
                )
                add_custom_target(gen_app_ico ALL DEPENDS ${APP_ICO_GEN})
                file(WRITE ${APP_RC_GEN} "IDI_APP_ICON ICON \"${APP_ICO_GEN}\"\n")
                target_sources(Way2RepairLoginSystem PRIVATE ${APP_RC_GEN})
                add_dependencies(Way2RepairLoginSystem gen_app_ico)
                message(STATUS "App icon (.ico) will be auto-generated via svg2ico and embedded")
            else()
                # Fallback: try ImageMagick if installed
                find_program(IMAGEMAGICK_MAGICK magick)
                if(IMAGEMAGICK_MAGICK)
                    set(IMAGEMAGICK_EXE ${IMAGEMAGICK_MAGICK})
                    add_custom_command(
                        OUTPUT ${APP_ICO_GEN}
                        COMMAND ${IMAGEMAGICK_EXE} "${APP_ICO_SVG}" -define icon:auto-resize=256 "${APP_ICO_GEN}"
                        DEPENDS ${APP_ICO_SVG}
                        COMMENT "Generating ICO from SVG using ImageMagick"
                    )
                    add_custom_target(gen_app_ico ALL DEPENDS ${APP_ICO_GEN})
                    file(WRITE ${APP_RC_GEN} "IDI_APP_ICON ICON \"${APP_ICO_GEN}\"\n")
                    target_sources(Way2RepairLoginSystem PRIVATE ${APP_RC_GEN})
                    add_dependencies(Way2RepairLoginSystem gen_app_ico)
                    message(STATUS "App icon (.ico) will be auto-generated via ImageMagick and embedded")
                else()
                    message(WARNING "QtSvg/ImageMagick not found. Provide resources/images/icons/Way2Repair_Logo.ico to embed EXE icon.")
                endif()
            endif()
        endif()

        # If a source .ico exists in resources, prefer that
        if(EXISTS ${APP_ICO_SRC})
            message(STATUS "App icon (.ico) found - embedding into executable")
            # Temporarily disabled due to windres error
            # target_sources(Way2RepairLoginSystem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.rc)
        elseif(EXISTS ${APP_ICO_GEN})
            # Fallback if generated earlier in configuration
            set(APP_RC_GEN "${CMAKE_CURRENT_BINARY_DIR}/app_icon_gen.rc")
            if(EXISTS ${APP_RC_GEN})
                target_sources(Way2RepairLoginSystem PRIVATE ${APP_RC_GEN})
            endif()
        else()
            message(STATUS "App icon (.ico) not found - taskbar/shortcut will use default icon")
        endif()
    endif()
else()
    if(ANDROID)
        add_library(Way2RepairLoginSystem SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(Way2RepairLoginSystem
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# Link libraries
target_link_libraries(Way2RepairLoginSystem PRIVATE 
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets 
    Qt${QT_VERSION_MAJOR}::Sql 
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    ${OPENGL_LIBRARIES}
)

# AWS SDK detection and configuration using vcpkg
# Add vcpkg prefix path for package discovery - use MinGW static triplet
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-mingw-static")
set(AWSSDK_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-mingw-static")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
message(STATUS "AWSSDK_ROOT_DIR set to: ${AWSSDK_ROOT_DIR}")

find_package(AWSSDK CONFIG COMPONENTS core s3)

if(AWSSDK_FOUND)
    message(STATUS "AWS SDK found via vcpkg - enabling AWS functionality")
    message(STATUS "AWSSDK_INCLUDE_DIRS: ${AWSSDK_INCLUDE_DIRS}")
    message(STATUS "AWSSDK_LIBRARIES: ${AWSSDK_LIBRARIES}")
    target_link_libraries(Way2RepairLoginSystem PRIVATE ${AWSSDK_LIBRARIES})
    target_include_directories(Way2RepairLoginSystem PRIVATE ${AWSSDK_INCLUDE_DIRS})
    target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_AWS_SDK)
else()
    message(WARNING "AWS SDK not found via vcpkg - AWS functionality will be disabled")
    message(STATUS "To enable AWS functionality, install AWS SDK via: ./vcpkg/vcpkg install aws-sdk-cpp[core,s3]:x64-mingw-static")
endif()

# Link Windows DbgHelp for minidump generation
if(WIN32)
    target_link_libraries(Way2RepairLoginSystem PRIVATE Dbghelp)
endif()

# Link Qt PDF modules if available
if(HAVE_QT_PDF)
    target_link_libraries(Way2RepairLoginSystem PRIVATE 
        Qt${QT_VERSION_MAJOR}::Pdf
        Qt${QT_VERSION_MAJOR}::PdfWidgets
    )
    target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_QT_PDF)
endif()

# Add PDFium, GLEW, and GLFW library paths and link (conditionally)
# Skip third-party libraries for Visual Studio due to MinGW/MSVC compatibility issues
if(NOT MSVC)
    # Dynamic paths relative to project directory
    set(PDFIUM_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/pdfium/lib")
    set(GLEW_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/glew/lib/Release/x64")
    set(GLFW_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/glfw_mingw/lib")

    # Check if PDFium library exists
    if(EXISTS "${PDFIUM_LIB_PATH}/pdfium.lib")
        message(STATUS "PDFium library found - enabling custom PDF viewer")
        target_link_directories(Way2RepairLoginSystem PRIVATE "${PDFIUM_LIB_PATH}")
        target_link_libraries(Way2RepairLoginSystem PRIVATE pdfium)
        target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_PDFIUM)
        set(HAVE_PDFIUM TRUE)
    else()
        message(STATUS "PDFium library not found - using Qt PDF module only")
        set(HAVE_PDFIUM FALSE)
    endif()

    # Check if GLEW library exists and set up MinGW-compatible linking
    if(EXISTS "${GLEW_LIB_PATH}/glew32.lib")
        message(STATUS "GLEW library found - enabling OpenGL features")
        target_link_directories(Way2RepairLoginSystem PRIVATE "${GLEW_LIB_PATH}")
        target_link_libraries(Way2RepairLoginSystem PRIVATE glew32 opengl32)
        target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_GLEW)
        set(HAVE_GLEW TRUE)
    else()
        message(STATUS "GLEW library not found - using basic OpenGL")
        set(HAVE_GLEW FALSE)
    endif()

    # Check if GLFW library exists (using MinGW compatible build) - DISABLED to avoid conflicts with PCB GLFW
    # Using PCB vcpkg GLFW instead
    message(STATUS "GLFW library skipped - using PCB vcpkg GLFW to avoid conflicts")
    set(HAVE_GLFW FALSE)
else()
    message(STATUS "Visual Studio detected - skipping third-party libraries due to MinGW/MSVC compatibility")
    message(STATUS "PDF viewer will use Qt-only implementation")
    set(HAVE_PDFIUM FALSE)
    set(HAVE_GLEW FALSE)
    set(HAVE_GLFW FALSE)
endif()

# Add PCB viewer vcpkg dependencies - use MinGW-compatible libraries for GCC compatibility
set(PCB_VCPKG_MINGW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/vcpkg/packages)
set(PCB_IMGUI_ROOT ${PCB_VCPKG_MINGW_ROOT}/imgui_x64-mingw-dynamic)
set(PCB_GLFW_ROOT ${PCB_VCPKG_MINGW_ROOT}/glfw3_x64-mingw-dynamic)
set(PCB_GLEW_ROOT ${PCB_VCPKG_MINGW_ROOT}/glew_x64-mingw-dynamic)

if(EXISTS "${PCB_IMGUI_ROOT}/lib/libimgui.a" AND EXISTS "${PCB_GLFW_ROOT}/lib/libglfw3dll.a" AND EXISTS "${PCB_GLEW_ROOT}/lib/libglew32.dll.a")
    message(STATUS "PCB MinGW vcpkg libraries found - enabling PCB viewer with ImGui")
    
    # Add include directories
    target_include_directories(Way2RepairLoginSystem PRIVATE 
        "${PCB_IMGUI_ROOT}/include"
        "${PCB_GLFW_ROOT}/include" 
        "${PCB_GLEW_ROOT}/include"
    )
    
    # Add library directories and link libraries with MinGW-style library names
    target_link_directories(Way2RepairLoginSystem PRIVATE 
        "${PCB_IMGUI_ROOT}/lib"
        "${PCB_GLFW_ROOT}/lib"
        "${PCB_GLEW_ROOT}/lib"
    )
    
    target_link_libraries(Way2RepairLoginSystem PRIVATE 
        "${PCB_IMGUI_ROOT}/lib/libimgui.a"
        "${PCB_GLFW_ROOT}/lib/libglfw3dll.a"  
        "${PCB_GLEW_ROOT}/lib/libglew32.dll.a"
        opengl32
    )
    target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_PCB_IMGUI HAVE_PCB_GLFW HAVE_PCB_GLEW)
    set(HAVE_PCB_VCPKG TRUE)
    
    message(STATUS "Linked MinGW-compatible ImGui, GLFW, and GLEW libraries")
else()
    message(STATUS "PCB MinGW vcpkg libraries not found - PCB viewer may not work properly")
    set(HAVE_PCB_VCPKG FALSE)
endif()

# Set target properties
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.way2repair.loginsystem)
endif()

set_target_properties(Way2RepairLoginSystem PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Installation
include(GNUInstallDirs)
install(TARGETS Way2RepairLoginSystem
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Finalize executable (Qt6 only)
if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Way2RepairLoginSystem)
endif()

# Auto-deploy Qt dependencies and other required files
if(WIN32)
    # Find Qt deployment tool
    find_program(QT_DEPLOY_TOOL 
        NAMES windeployqt.exe windeployqt
        HINTS ${Qt${QT_VERSION_MAJOR}_DIR}/../../../bin
              ${Qt${QT_VERSION_MAJOR}_DIR}/bin
              ${QT_INSTALL_PREFIX}/bin
        DOC "Qt deployment tool"
    )
    
    if(QT_DEPLOY_TOOL)
        message(STATUS "Found Qt deployment tool: ${QT_DEPLOY_TOOL}")
        
        # Custom target to deploy Qt dependencies
        add_custom_target(deploy
            COMMAND ${QT_DEPLOY_TOOL} --verbose 2 --no-translations --no-system-d3d-compiler --no-opengl-sw --dir ${CMAKE_CURRENT_BINARY_DIR} $<TARGET_FILE:Way2RepairLoginSystem>
            DEPENDS Way2RepairLoginSystem
            COMMENT "Deploying Qt dependencies"
        )
        
        # Auto-deploy after build
        add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
            COMMAND ${QT_DEPLOY_TOOL} --verbose 2 --no-translations --no-system-d3d-compiler --no-opengl-sw --dir ${CMAKE_CURRENT_BINARY_DIR} $<TARGET_FILE:Way2RepairLoginSystem>
            COMMENT "Auto-deploying Qt dependencies to build directory"
        )
    else()
        message(WARNING "Qt deployment tool not found. You may need to manually copy Qt DLLs.")
    endif()
    
    # Copy PDFium and GLEW DLLs if they exist - using dynamic paths
    set(PDFIUM_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/pdfium/bin/pdfium.dll")
    set(GLEW_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pdf/third_party/third_party/extern/glew/bin/Release/x64/glew32.dll")
    
    if(EXISTS ${PDFIUM_DLL})
        add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PDFIUM_DLL} ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Copying PDFium DLL"
        )
        message(STATUS "PDFium DLL will be copied: ${PDFIUM_DLL}")
    else()
        message(STATUS "PDFium DLL not found - application will use Qt PDF module: ${PDFIUM_DLL}")
    endif()
    
    if(EXISTS ${GLEW_DLL})
        add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GLEW_DLL} ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Copying GLEW DLL"
        )
        message(STATUS "GLEW DLL will be copied: ${GLEW_DLL}")
    else()
        message(STATUS "GLEW DLL not found - application will use basic OpenGL: ${GLEW_DLL}")
    endif()
    
    # Copy PCB MinGW vcpkg DLLs if they exist
    set(PCB_VCPKG_GLFW_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/vcpkg/packages/glfw3_x64-mingw-dynamic/bin/glfw3.dll")
    set(PCB_VCPKG_GLEW_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/viewers/pcb/vcpkg/packages/glew_x64-mingw-dynamic/bin/glew32.dll")
    
    if(EXISTS ${PCB_VCPKG_GLFW_DLL})
        add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PCB_VCPKG_GLFW_DLL} ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Copying PCB vcpkg GLFW DLL"
        )
        message(STATUS "PCB vcpkg GLFW DLL will be copied: ${PCB_VCPKG_GLFW_DLL}")
    endif()
    
    if(EXISTS ${PCB_VCPKG_GLEW_DLL})
        add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PCB_VCPKG_GLEW_DLL} ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Copying PCB vcpkg GLEW DLL"
        )
        message(STATUS "PCB vcpkg GLEW DLL will be copied: ${PCB_VCPKG_GLEW_DLL}")
    endif()
    
    # Copy additional runtime dependencies (database files, etc.)
    set(DATABASE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/database/w2r_login.db")
    if(EXISTS ${DATABASE_FILE})
        add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DATABASE_FILE} ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Copying database file"
        )
        message(STATUS "Database file found: ${DATABASE_FILE}")
    else()
        message(STATUS "Database file not found: ${DATABASE_FILE}")
    endif()
    
    # Create a complete deployment target
    add_custom_target(package
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/deploy
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/deploy
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/deploy/CMakeFiles
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/deploy/cmake_install.cmake
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/deploy/Makefile
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/deploy/CMakeCache.txt
        DEPENDS Way2RepairLoginSystem deploy
        COMMENT "Creating complete deployment package"
    )
endif()

# Always copy PCB theme config into the build output so runtime can find it
# The application looks for "config/pcb_themes.json" relative to the exe/CWD.
set(PCB_THEME_JSON "${CMAKE_CURRENT_SOURCE_DIR}/config/pcb_themes.json")
if(EXISTS ${PCB_THEME_JSON})
    add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/config
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PCB_THEME_JSON} ${CMAKE_CURRENT_BINARY_DIR}/config/pcb_themes.json
        COMMENT "Copying PCB themes JSON to build/config"
    )
    # Also install it when using "cmake --install"
    install(FILES ${PCB_THEME_JSON} DESTINATION config)
    message(STATUS "PCB themes JSON will be copied/installed: ${PCB_THEME_JSON}")
else()
    message(WARNING "PCB themes JSON not found at ${PCB_THEME_JSON}. Themes may fall back to built-ins.")
endif()

# Reorganize loose icon/logo files in build folder without affecting EXE icon
# - Moves build/icons/* into build/assets/icons
# - Copies generated Way2Repair_Logo.ico into build/assets/icons (keeps original for rebuilds)
set(ORGANIZE_ICONS_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrganizeIcons.cmake")
file(WRITE ${ORGANIZE_ICONS_SCRIPT} "# Auto-generated by CMake to tidy icon assets\n"
    "if(EXISTS \"${CMAKE_CURRENT_BINARY_DIR}/icons\")\n"
    "  file(MAKE_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}/assets/icons\")\n"
    "  file(GLOB ICON_FILES \"${CMAKE_CURRENT_BINARY_DIR}/icons/*\")\n"
    "  foreach(f ${ICON_FILES})\n"
    "    if(EXISTS \"${CMAKE_CURRENT_BINARY_DIR}/assets/icons\")\n"
    "      file(COPY \"${f}\" DESTINATION \"${CMAKE_CURRENT_BINARY_DIR}/assets/icons\")\n"
    "    endif()\n"
    "  endforeach()\n"
    "  file(REMOVE_RECURSE \"${CMAKE_CURRENT_BINARY_DIR}/icons\")\n"
    "endif()\n"
    "# Also copy the generated ICO (if present) for convenience; keep original for future builds\n"
    "if(EXISTS \"${CMAKE_CURRENT_BINARY_DIR}/Way2Repair_Logo.ico\")\n"
    "  file(MAKE_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}/assets/icons\")\n"
    "  file(COPY \"${CMAKE_CURRENT_BINARY_DIR}/Way2Repair_Logo.ico\" DESTINATION \"${CMAKE_CURRENT_BINARY_DIR}/assets/icons\")\n"
    "endif()\n"
)

add_custom_command(TARGET Way2RepairLoginSystem POST_BUILD
    COMMAND ${CMAKE_COMMAND} -P ${ORGANIZE_ICONS_SCRIPT}
    COMMENT "Organizing icon/logo assets into build/assets/icons"
)

# Print configuration info
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Qt Version: ${QT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source Directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Binary Directory: ${CMAKE_CURRENT_BINARY_DIR}")
