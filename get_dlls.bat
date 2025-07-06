@echo off
echo ===============================================
echo      Downloading Required DLLs
echo ===============================================
echo.

set SCRIPT_DIR=%~dp0
set PDFIUM_BIN_DIR=%SCRIPT_DIR%src\pdf\third_party\extern\pdfium\bin
set GLEW_BIN_DIR=%SCRIPT_DIR%src\pdf\third_party\extern\glew\bin\Release\x64

:: Create directories if they don't exist
if not exist "%PDFIUM_BIN_DIR%" mkdir "%PDFIUM_BIN_DIR%"
if not exist "%GLEW_BIN_DIR%" mkdir "%GLEW_BIN_DIR%"

:: Download PDFium DLL
echo Downloading PDFium DLL...
echo Note: PDFium doesn't provide pre-built DLLs. You have these options:
echo 1. Build PDFium from source (complex, takes hours)
echo 2. Use a pre-built version from a third-party
echo 3. Use Qt's PDF module instead (already integrated in CMakeLists.txt)
echo.

:: Download GLEW DLL
echo Downloading GLEW DLL...
echo GLEW DLL should be available from the official GLEW releases...
echo.

:: Check if curl is available
where curl >nul 2>nul
if %errorlevel% neq 0 (
    echo curl is not available. Please install curl or download manually.
    goto :manual_instructions
)

:: Download GLEW DLL from official source
echo Attempting to download GLEW DLL...
curl -L -o "%GLEW_BIN_DIR%\glew32.dll" "https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0-win32.zip"
if %errorlevel% neq 0 (
    echo Failed to download GLEW. Please download manually.
    goto :manual_instructions
)

:: Extract GLEW (this won't work directly with curl, need to extract zip)
echo Note: Downloaded ZIP file needs to be extracted manually.
echo Extract the glew32.dll from the zip to: %GLEW_BIN_DIR%
echo.

:manual_instructions
echo ===============================================
echo      Manual Download Instructions
echo ===============================================
echo.
echo 1. GLEW DLL:
echo    - Download from: https://github.com/nigels-com/glew/releases/latest
echo    - Extract glew32.dll to: %GLEW_BIN_DIR%
echo.
echo 2. PDFium DLL (Choose one option):
echo    Option A: Use Qt's PDF module (recommended, already integrated)
echo    Option B: Download pre-built PDFium from:
echo             https://github.com/bblanchon/pdfium-binaries/releases/latest
echo    Option C: Build PDFium from source (advanced)
echo.
echo 3. After downloading, run: check_deps.bat
echo.
echo Alternative: Use Qt's built-in PDF viewer instead of custom OpenGL viewer
echo              (modify CMakeLists.txt to disable USE_HYBRID_PDF_VIEWER)
echo.
pause
