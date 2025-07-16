@echo off
echo Building GLFW from source with MinGW...
echo.

REM Set build directory
set BUILD_DIR=%~dp0..\build\glfw_mingw
set SOURCE_DIR=%~dp0..\src\pdf\third_party\glfw_source
set INSTALL_DIR=%~dp0..\src\pdf\third_party\extern\glfw_mingw

REM Create directories
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%SOURCE_DIR%" mkdir "%SOURCE_DIR%"
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

echo Checking if GLFW source exists...
if not exist "%SOURCE_DIR%\CMakeLists.txt" (
    echo GLFW source not found. Downloading...
    cd /d "%SOURCE_DIR%\.."
    
    echo Downloading GLFW 3.3.9...
    powershell -Command "& {Invoke-WebRequest -Uri 'https://github.com/glfw/glfw/releases/download/3.3.9/glfw-3.3.9.zip' -OutFile 'glfw-3.3.9.zip'}"
    
    if not exist "glfw-3.3.9.zip" (
        echo Failed to download GLFW. Please check internet connection.
        pause
        exit /b 1
    )
    
    echo Extracting GLFW...
    powershell -Command "& {Expand-Archive -Path 'glfw-3.3.9.zip' -DestinationPath '.' -Force}"
    
    REM Move contents from extracted folder to our source directory
    if exist "glfw-3.3.9" (
        xcopy "glfw-3.3.9\*" "glfw_source\" /E /H /Y
        rmdir "glfw-3.3.9" /S /Q
        del "glfw-3.3.9.zip"
    )
)

if not exist "%SOURCE_DIR%\CMakeLists.txt" (
    echo Failed to extract GLFW source. Please download manually.
    pause
    exit /b 1
)

echo GLFW source found. Building with MinGW...
cd /d "%BUILD_DIR%"

REM Set MinGW environment (using detected path)
set PATH=C:\Qt\Tools\mingw1310_64\bin;%PATH%
set CMAKE_MAKE_PROGRAM=C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe
set CMAKE_C_COMPILER=C:\Qt\Tools\mingw1310_64\bin\gcc.exe
set CMAKE_CXX_COMPILER=C:\Qt\Tools\mingw1310_64\bin\g++.exe

REM Configure with CMake for MinGW
echo Configuring GLFW build...
cmake -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DCMAKE_MAKE_PROGRAM="C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe" ^
    -DCMAKE_C_COMPILER="C:\Qt\Tools\mingw1310_64\bin\gcc.exe" ^
    -DCMAKE_CXX_COMPILER="C:\Qt\Tools\mingw1310_64\bin\g++.exe" ^
    -DGLFW_BUILD_EXAMPLES=OFF ^
    -DGLFW_BUILD_TESTS=OFF ^
    -DGLFW_BUILD_DOCS=OFF ^
    -DBUILD_SHARED_LIBS=OFF ^
    "%SOURCE_DIR%"

if errorlevel 1 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo Building GLFW...
cmake --build . --config Release

if errorlevel 1 (
    echo GLFW build failed!
    pause
    exit /b 1
)

echo Installing GLFW...
cmake --install .

if errorlevel 1 (
    echo GLFW installation failed!
    pause
    exit /b 1
)

echo.
echo GLFW built successfully!
echo Location: %INSTALL_DIR%
echo.

REM Verify installation
if exist "%INSTALL_DIR%\lib\libglfw3.a" (
    echo ✓ Static library found: %INSTALL_DIR%\lib\libglfw3.a
) else (
    echo ✗ Static library not found!
)

if exist "%INSTALL_DIR%\include\GLFW\glfw3.h" (
    echo ✓ Headers found: %INSTALL_DIR%\include\GLFW\
) else (
    echo ✗ Headers not found!
)

echo.
echo Build complete! Press any key to continue...
pause > nul
