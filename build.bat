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

echo Checking for required DLLs in build directory...
if exist "%BUILD_DIR%\Qt6Core.dll" (
    echo Qt DLLs found in build directory.
) else (
    echo Qt DLLs not found. Running deployment...
    echo.
    echo ====================================
    echo Auto-deploying dependencies...
    echo ====================================
    
    rem Try to find and run windeployqt
    set QT_DEPLOY_TOOL=
    if exist "C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe" (
        set QT_DEPLOY_TOOL=C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe
    )
    
    if defined QT_DEPLOY_TOOL (
        echo Running Qt deployment tool...
        "%QT_DEPLOY_TOOL%" --verbose 2 --no-translations --no-system-d3d-compiler --no-opengl-sw --dir %BUILD_DIR% %BUILD_DIR%\%PROJECT_NAME%.exe
        echo Qt deployment completed.
    ) else (
        echo Qt deployment tool not found. Please run deploy.bat manually.
    )
    
    rem Copy PDFium and GLEW DLLs
    echo Copying additional DLLs...
    if exist "src\pdf\third_party\extern\pdfium\bin\pdfium.dll" (
        copy "src\pdf\third_party\extern\pdfium\bin\pdfium.dll" "%BUILD_DIR%\" >nul 2>&1
        echo PDFium DLL copied.
    )
    
    if exist "src\pdf\third_party\extern\glew\bin\Release\x64\glew32.dll" (
        copy "src\pdf\third_party\extern\glew\bin\Release\x64\glew32.dll" "%BUILD_DIR%\" >nul 2>&1
        echo GLEW DLL copied.
    )
    
    echo Dependency deployment completed.
    echo.
)

:: Ask if user wants to run the application
set /p run_app=Do you want to run the application? (y/n): 
if /i "%run_app%"=="y" (
    echo Running application...
    start "" "%BUILD_DIR%\%PROJECT_NAME%.exe"
)

pause
