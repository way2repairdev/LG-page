@echo off
echo ====================================
echo Way2Repair Build Script
echo ====================================

:: Set environment
set PROJECT_NAME=Way2RepairLoginSystem
set BUILD_DIR=build
set CMAKE_BUILD_TYPE=Release

:: Add Qt to PATH
set PATH=C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%

:: Check if Qt is in PATH
where qmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: Qt not found in PATH
    echo Please install Qt and add it to your PATH
    pause
    exit /b 1
)

:: Check if CMake is available
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: CMake not found in PATH
    echo Please install CMake and add it to your PATH
    pause
    exit /b 1
)

:: Create build directory
if not exist %BUILD_DIR% (
    mkdir %BUILD_DIR%
)

:: Configure CMake
echo Configuring CMake...
cd %BUILD_DIR%
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE%

if %errorlevel% neq 0 (
    echo CMake configuration failed!
    cd ..
    pause
    exit /b 1
)

:: Build project
echo Building project...
cmake --build . --config %CMAKE_BUILD_TYPE%

if %errorlevel% neq 0 (
    echo Build failed!
    cd ..
    pause
    exit /b 1
)

:: Go back to root
cd ..

echo.
echo ====================================
echo Build completed successfully!
echo ====================================
echo Executable location: %BUILD_DIR%\%PROJECT_NAME%.exe
echo.

:: Ask if user wants to run the application
set /p run_app=Do you want to run the application? (y/n): 
if /i "%run_app%"=="y" (
    echo Running application...
    start "" "%BUILD_DIR%\%PROJECT_NAME%.exe"
)

pause
