@echo off
echo ====================================
echo Way2Repair Build Script
echo ====================================

:: Set environment
set PROJECT_NAME=Way2RepairLoginSystem
set BUILD_DIR=build
set CMAKE_BUILD_TYPE=Release

:: Auto-detect Qt installation
set QT_ROOT=
set QT_VERSION=
set QT_MINGW_PATH=
set MINGW_TOOLS_PATH=

:: Check for Qt installations (common versions)
for %%v in (6.9.1 6.9.0 6.8.0 6.7.0 6.6.0 6.5.0) do (
    if exist "C:\Qt\%%v\mingw_64" (
        set QT_VERSION=%%v
        set QT_ROOT=C:\Qt\%%v
        set QT_MINGW_PATH=C:\Qt\%%v\mingw_64
        goto :qt_found
    )
)

:qt_found
if "%QT_VERSION%"=="" (
    echo Error: No supported Qt version found!
    echo Checked for Qt versions: 6.9.1, 6.8.0, 6.7.0, 6.6.0, 6.5.0
    echo Please install Qt with MinGW compiler to C:\Qt\
    pause
    exit /b 1
)

:: Auto-detect MinGW tools
for %%m in (mingw1310_64 mingw1120_64 mingw900_64 mingw810_64) do (
    if exist "C:\Qt\Tools\%%m\bin" (
        set MINGW_TOOLS_PATH=C:\Qt\Tools\%%m\bin
        goto :mingw_found
    )
)

:mingw_found
if "%MINGW_TOOLS_PATH%"=="" (
    echo Error: MinGW tools not found!
    echo Please install Qt with MinGW tools
    pause
    exit /b 1
)

echo Found Qt %QT_VERSION% at %QT_MINGW_PATH%
echo Found MinGW tools at %MINGW_TOOLS_PATH%

:: Add Qt to PATH
set PATH=%QT_MINGW_PATH%\bin;%MINGW_TOOLS_PATH%;%PATH%

:: Check if Qt directory exists
if not exist "C:\Qt\6.9.1\mingw_64" (
    echo Error: Qt 6.9.1 MinGW installation not found at C:\Qt\6.9.1\mingw_64
    echo Please install Qt 6.9.1 with MinGW compiler
    pause
    exit /b 1
)

:: Check if MinGW tools exist
if not exist "C:\Qt\Tools\mingw1310_64\bin" (
    echo Error: MinGW tools not found at C:\Qt\Tools\mingw1310_64\bin
    echo Please install Qt MinGW tools
    pause
    exit /b 1
)

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

:: Clean up debug files from previous runs
echo Cleaning up debug files from build directory...
:: Clean log files
if exist "%BUILD_DIR%\*.log" (
    del /Q "%BUILD_DIR%\*.log" >nul 2>&1
)
:: Clean specific debug files (not all txt files to preserve CMake files)
if exist "%BUILD_DIR%\pdf_debug.txt" (
    del /Q "%BUILD_DIR%\pdf_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\pdf_embedder_debug.txt" (
    del /Q "%BUILD_DIR%\pdf_embedder_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\opengl_debug.txt" (
    del /Q "%BUILD_DIR%\opengl_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\pipeline_debug.txt" (
    del /Q "%BUILD_DIR%\pipeline_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\pcb_debug.txt" (
    del /Q "%BUILD_DIR%\pcb_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\dualtab_debug.txt" (
    del /Q "%BUILD_DIR%\dualtab_debug.txt" >nul 2>&1
)
:: Clean any files with "debug" in the name (but preserve cmake and other important files)
for %%f in ("%BUILD_DIR%\*debug*.txt") do (
    if exist "%%f" del /Q "%%f" >nul 2>&1
)
echo Debug files cleaned.

:: Configure CMake
echo Configuring CMake...
cd %BUILD_DIR%
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% -DCMAKE_PREFIX_PATH="%QT_MINGW_PATH%"

if %errorlevel% neq 0 (
    echo CMake configuration failed!
    echo.
    echo Common issues:
    echo - Qt not found: Make sure Qt %QT_VERSION% MinGW is installed
    echo - MinGW not in PATH: Check that %MINGW_TOOLS_PATH% is in PATH
    echo - CMake cache: Try deleting the build directory and running again
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
    if exist "%QT_MINGW_PATH%\bin\windeployqt.exe" (
        set QT_DEPLOY_TOOL=%QT_MINGW_PATH%\bin\windeployqt.exe
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
    if exist "src\viewers\pdf\third_party\third_party\extern\pdfium\bin\pdfium.dll" (
        copy "src\viewers\pdf\third_party\third_party\extern\pdfium\bin\pdfium.dll" "%BUILD_DIR%\" >nul 2>&1
        echo PDFium DLL copied.
    )
    
    if exist "src\viewers\pdf\third_party\third_party\extern\glew\bin\Release\x64\glew32.dll" (
        copy "src\viewers\pdf\third_party\third_party\extern\glew\bin\Release\x64\glew32.dll" "%BUILD_DIR%\" >nul 2>&1
        echo GLEW DLL copied.
    )
    
    rem Copy PCB vcpkg DLLs
    if exist "src\viewers\pcb\vcpkg_installed\x64-windows\bin\glfw3.dll" (
        copy "src\viewers\pcb\vcpkg_installed\x64-windows\bin\glfw3.dll" "%BUILD_DIR%\" >nul 2>&1
        echo PCB GLFW DLL copied.
    )
    
    if exist "src\viewers\pcb\vcpkg_installed\x64-windows\bin\glew32.dll" (
        copy "src\viewers\pcb\vcpkg_installed\x64-windows\bin\glew32.dll" "%BUILD_DIR%\" >nul 2>&1
        echo PCB GLEW DLL copied.
    )
    
    rem Copy database file
    if exist "database\w2r_login.db" (
        copy "database\w2r_login.db" "%BUILD_DIR%\" >nul 2>&1
        echo Database file copied.
    )
    
    echo Dependency deployment completed.
    echo.
)

:: Final cleanup of debug files before runtime
echo Performing final cleanup of debug files...
:: Clean log files
if exist "%BUILD_DIR%\*.log" (
    del /Q "%BUILD_DIR%\*.log" >nul 2>&1
)
:: Clean specific debug files that might be generated (preserve CMake and other important files)
if exist "%BUILD_DIR%\pdf_debug.txt" (
    del /Q "%BUILD_DIR%\pdf_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\pdf_embedder_debug.txt" (
    del /Q "%BUILD_DIR%\pdf_embedder_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\opengl_debug.txt" (
    del /Q "%BUILD_DIR%\opengl_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\pipeline_debug.txt" (
    del /Q "%BUILD_DIR%\pipeline_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\pcb_debug.txt" (
    del /Q "%BUILD_DIR%\pcb_debug.txt" >nul 2>&1
)
if exist "%BUILD_DIR%\dualtab_debug.txt" (
    del /Q "%BUILD_DIR%\dualtab_debug.txt" >nul 2>&1
)
:: Clean any files with "debug" in the name
for %%f in ("%BUILD_DIR%\*debug*.txt") do (
    if exist "%%f" del /Q "%%f" >nul 2>&1
)
echo Runtime environment prepared - debug files removed.

:: Ask if user wants to run the application
set /p run_app=Do you want to run the application? (y/n): 
if /i "%run_app%"=="y" (
    echo Running application...
    start "" "%BUILD_DIR%\%PROJECT_NAME%.exe"
)

pause
