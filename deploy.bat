@echo off
echo ====================================
echo Way2Repair Deployment Script
echo ====================================

set BUILD_DIR=%~dp0build
set DEPLOY_DIR=%~dp0deploy
set QT_DIR=C:\Qt\6.9.1\mingw_64

echo Checking build directory...
if not exist "%BUILD_DIR%\Way2RepairLoginSystem.exe" (
    echo ERROR: Executable not found! Please build the project first.
    pause
    exit /b 1
)

echo Creating deployment directory...
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"

echo Copying executable...
copy "%BUILD_DIR%\Way2RepairLoginSystem.exe" "%DEPLOY_DIR%\"

echo Deploying Qt dependencies...
if exist "%QT_DIR%\bin\windeployqt.exe" (
    "%QT_DIR%\bin\windeployqt.exe" --verbose 2 --no-translations --no-system-d3d-compiler --no-opengl-sw --dir "%DEPLOY_DIR%" "%DEPLOY_DIR%\Way2RepairLoginSystem.exe"
    echo Qt deployment completed.
) else (
    echo WARNING: windeployqt.exe not found. Copying Qt DLLs manually...
    
    REM Copy essential Qt DLLs
    copy "%QT_DIR%\bin\Qt6Core.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\Qt6Gui.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\Qt6Widgets.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\Qt6Network.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\Qt6Sql.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\Qt6OpenGL.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\Qt6OpenGLWidgets.dll" "%DEPLOY_DIR%\" 2>nul
    
    REM Copy Qt platforms
    mkdir "%DEPLOY_DIR%\platforms" 2>nul
    copy "%QT_DIR%\plugins\platforms\qwindows.dll" "%DEPLOY_DIR%\platforms\" 2>nul
    
    REM Copy Qt styles
    mkdir "%DEPLOY_DIR%\styles" 2>nul
    copy "%QT_DIR%\plugins\styles\qwindowsvistastyle.dll" "%DEPLOY_DIR%\styles\" 2>nul
    
    REM Copy Qt imageformats
    mkdir "%DEPLOY_DIR%\imageformats" 2>nul
    copy "%QT_DIR%\plugins\imageformats\qico.dll" "%DEPLOY_DIR%\imageformats\" 2>nul
    copy "%QT_DIR%\plugins\imageformats\qjpeg.dll" "%DEPLOY_DIR%\imageformats\" 2>nul
    copy "%QT_DIR%\plugins\imageformats\qpng.dll" "%DEPLOY_DIR%\imageformats\" 2>nul
    
    REM Copy MinGW runtime
    copy "%QT_DIR%\bin\libgcc_s_seh-1.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\libstdc++-6.dll" "%DEPLOY_DIR%\" 2>nul
    copy "%QT_DIR%\bin\libwinpthread-1.dll" "%DEPLOY_DIR%\" 2>nul
)

echo Copying PDFium and GLEW DLLs...
if exist "src\pdf\third_party\extern\pdfium\bin\pdfium.dll" (
    copy "src\pdf\third_party\extern\pdfium\bin\pdfium.dll" "%DEPLOY_DIR%\"
    echo PDFium DLL copied.
) else (
    echo WARNING: PDFium DLL not found at src\pdf\third_party\extern\pdfium\bin\pdfium.dll
)

if exist "src\pdf\third_party\extern\glew\bin\Release\x64\glew32.dll" (
    copy "src\pdf\third_party\extern\glew\bin\Release\x64\glew32.dll" "%DEPLOY_DIR%\"
    echo GLEW DLL copied.
) else (
    echo WARNING: GLEW DLL not found at src\pdf\third_party\extern\glew\bin\Release\x64\glew32.dll
)

echo Copying database file...
if exist "database\w2r_login.db" (
    copy "database\w2r_login.db" "%DEPLOY_DIR%\"
    echo Database file copied.
)

echo Copying additional resources...
if exist "resources" (
    xcopy "resources" "%DEPLOY_DIR%\resources" /E /I /Q >nul 2>&1
    echo Resources copied.
)

echo Copying configuration files...
if exist "config" (
    xcopy "config" "%DEPLOY_DIR%\config" /E /I /Q >nul 2>&1
    echo Configuration files copied.
)

echo Creating run script...
(
echo @echo off
echo echo Starting Way2Repair Login System...
echo echo.
echo rem Set environment variables if needed
echo set QT_QPA_PLATFORM_PLUGIN_PATH=%%~dp0platforms
echo set QT_PLUGIN_PATH=%%~dp0
echo.
echo rem Check for required DLLs
echo if not exist "Qt6Core.dll" ^(
echo     echo ERROR: Qt6Core.dll not found!
echo     echo Please run deploy.bat to copy required files.
echo     pause
echo     exit /b 1
echo ^)
echo.
echo rem Run the application
echo start "" "Way2RepairLoginSystem.exe"
echo.
echo rem Uncomment the line below if you want to keep the console open
echo rem pause
) > "%DEPLOY_DIR%\run.bat"

echo Creating version info...
(
echo Way2Repair Login System v4.75
echo Build Date: %DATE% %TIME%
echo.
echo System Requirements:
echo - Windows 10/11 ^(64-bit^)
echo - OpenGL 3.3 or higher
echo - 4GB RAM minimum
echo - 500MB disk space
echo.
echo Components:
echo - Qt 6.9.1 Framework
echo - PDFium PDF Engine
echo - GLEW OpenGL Extension Library
echo - SQLite Database
echo.
echo For support, contact: support@way2repair.com
) > "%DEPLOY_DIR%\README.txt"

echo ====================================
echo Deployment completed successfully!
echo ====================================
echo.
echo Deployment location: %DEPLOY_DIR%
echo.
echo Files deployed:
dir /b "%DEPLOY_DIR%\*.exe" "%DEPLOY_DIR%\*.dll" 2>nul
echo.
echo To run the application:
echo 1. Navigate to: %DEPLOY_DIR%
echo 2. Run: Way2RepairLoginSystem.exe
echo    or: run.bat
echo.
echo Do you want to run the deployed application now? (y/n)
set /p choice=
if /i "%choice%"=="y" (
    cd /d "%DEPLOY_DIR%"
    start "" "Way2RepairLoginSystem.exe"
    echo Application started!
) else (
    echo You can run the application later from: %DEPLOY_DIR%
)

echo.
pause
