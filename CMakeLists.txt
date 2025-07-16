cmake_minimum_required(VERSION 3.16)

project(Way2RepairLoginSystem VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler specific flags
if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra)
endif()

# Qt configuration
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6 packages
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Sql Network OpenGL OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Sql Network OpenGL OpenGLWidgets)

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

# Set directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)

# Tell Qt where to find UI files
set(CMAKE_AUTOUIC_SEARCH_PATHS ${RESOURCES_DIR}/ui)

# Include directories
include_directories(
    ${INCLUDE_DIR}
    ${INCLUDE_DIR}/core
    ${INCLUDE_DIR}/ui
    ${INCLUDE_DIR}/database
    ${INCLUDE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/pdfium/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glew/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glfw_mingw/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Source files
set(CORE_SOURCES
    ${SRC_DIR}/core/main.cpp
)

# UI sources - conditionally include hybrid PDF viewer
set(UI_SOURCES
    ${SRC_DIR}/ui/mainwindow.cpp
    ${SRC_DIR}/ui/mainapplication.cpp
    ${SRC_DIR}/ui/pdfviewerwidget.cpp
    ${SRC_DIR}/ui/PDFViewerEmbedder.cpp
    ${SRC_DIR}/ui/QtPDFViewerWidget.cpp
)

# Add hybrid PDF viewer only if Qt PDF modules are available
if(HAVE_QT_PDF)
    list(APPEND UI_SOURCES ${SRC_DIR}/ui/hybridpdfviewer.cpp)
endif()

# PDF viewer sources
set(PDF_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/src/rendering/pdf-render.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/src/core/feature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/src/ui/menu-integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/src/ui/tab-manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/src/globals.cpp
)

set(DATABASE_SOURCES
    ${SRC_DIR}/database/databasemanager.cpp
)

set(NETWORK_SOURCES
    # Add network source files here if needed
)

# Header files
# UI headers - conditionally include hybrid PDF viewer
set(UI_HEADERS
    ${INCLUDE_DIR}/ui/mainwindow.h
    ${INCLUDE_DIR}/ui/mainapplication.h
    ${INCLUDE_DIR}/ui/pdfviewerwidget.h
    ${SRC_DIR}/ui/PDFViewerEmbedder.h
    ${SRC_DIR}/ui/QtPDFViewerWidget.h
)

# Add hybrid PDF viewer header only if Qt PDF modules are available
if(HAVE_QT_PDF)
    list(APPEND UI_HEADERS ${INCLUDE_DIR}/ui/hybridpdfviewer.h)
endif()

# PDF viewer headers
set(PDF_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/include/rendering/pdf-render.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/include/core/feature.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/include/ui/menu-integration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/include/ui/tab-manager.h
)

set(DATABASE_HEADERS
    ${INCLUDE_DIR}/database/databasemanager.h
)

set(NETWORK_HEADERS
    # Add network header files here if needed
)

# UI files
set(UI_FORMS
    resources/ui/mainwindow.ui
)

# Resource files
set(RESOURCE_FILES
    resources/icons.qrc
)

# All project sources
set(PROJECT_SOURCES
    ${CORE_SOURCES}
    ${UI_SOURCES}
    ${PDF_SOURCES}
    ${DATABASE_SOURCES}
    ${NETWORK_SOURCES}
    ${UI_HEADERS}
    ${PDF_HEADERS}
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

# Link Qt PDF modules if available
if(HAVE_QT_PDF)
    target_link_libraries(Way2RepairLoginSystem PRIVATE 
        Qt${QT_VERSION_MAJOR}::Pdf
        Qt${QT_VERSION_MAJOR}::PdfWidgets
    )
    target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_QT_PDF)
endif()

# Add PDFium, GLEW, and GLFW library paths and link (conditionally)
set(PDFIUM_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/pdfium/lib")
set(GLEW_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glew/lib/Release/x64")
set(GLFW_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glfw/lib-vc2022")

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

# Check if GLEW library exists
if(EXISTS "${GLEW_LIB_PATH}/glew32.lib")
    message(STATUS "GLEW library found - enabling OpenGL features")
    target_link_directories(Way2RepairLoginSystem PRIVATE "${GLEW_LIB_PATH}")
    target_link_libraries(Way2RepairLoginSystem PRIVATE glew32)
    target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_GLEW)
    set(HAVE_GLEW TRUE)
else()
    message(STATUS "GLEW library not found - using basic OpenGL")
    set(HAVE_GLEW FALSE)
endif()

# Check if GLFW library exists (using MinGW-compatible build)
set(GLFW_MINGW_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glfw_mingw/lib")
set(GLFW_MINGW_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glfw_mingw/include")

if(EXISTS "${GLFW_MINGW_LIB_PATH}/libglfw3.a")
    message(STATUS "MinGW-compatible GLFW library found - enabling GLFW window management")
    target_link_directories(Way2RepairLoginSystem PRIVATE "${GLFW_MINGW_LIB_PATH}")
    target_include_directories(Way2RepairLoginSystem PRIVATE "${GLFW_MINGW_INCLUDE_PATH}")
    target_link_libraries(Way2RepairLoginSystem PRIVATE glfw3)
    # Link additional Windows libraries required by GLFW
    target_link_libraries(Way2RepairLoginSystem PRIVATE opengl32 gdi32 user32 kernel32 shell32)
    target_compile_definitions(Way2RepairLoginSystem PRIVATE HAVE_GLFW)
    set(HAVE_GLFW TRUE)
else()
    message(STATUS "MinGW-compatible GLFW library not found - using Qt native embedding")
    set(HAVE_GLFW FALSE)
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
    
    # Copy PDFium and GLEW DLLs if they exist
    set(PDFIUM_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/pdfium/bin/pdfium.dll")
    set(GLEW_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/third_party/extern/glew/bin/Release/x64/glew32.dll")
    
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

# Print configuration info
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Qt Version: ${QT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source Directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Binary Directory: ${CMAKE_CURRENT_BINARY_DIR}")
