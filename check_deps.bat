@echo off
echo ====================================
echo Dependency Checker for Way2Repair
echo ====================================
echo.

set BUILD_DIR=build
set EXE_PATH=%BUILD_DIR%\Way2RepairLoginSystem.exe

if not exist "%EXE_PATH%" (
    echo ERROR: Executable not found at %EXE_PATH%
    echo Please build the project first using build.bat
    echo.
    pause
    exit /b 1
)

echo Checking dependencies for: %EXE_PATH%
echo.

echo Required Qt DLLs:
set QT_DLLS=Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll Qt6Sql.dll Qt6OpenGL.dll Qt6OpenGLWidgets.dll

for %%i in (%QT_DLLS%) do (
    if exist "%BUILD_DIR%\%%i" (
        echo [OK] %%i
    ) else (
        echo [MISSING] %%i
    )
)

echo.
echo Required MinGW Runtime DLLs:
set MINGW_DLLS=libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll

for %%i in (%MINGW_DLLS%) do (
    if exist "%BUILD_DIR%\%%i" (
        echo [OK] %%i
    ) else (
        echo [MISSING] %%i
    )
)

echo.
echo Required PDF/OpenGL DLLs:
set PDF_DLLS=pdfium.dll glew32.dll

for %%i in (%PDF_DLLS%) do (
    if exist "%BUILD_DIR%\%%i" (
        echo [OK] %%i
    ) else (
        echo [MISSING] %%i
    )
)

echo.
echo Required Qt Plugins:
if exist "%BUILD_DIR%\platforms\qwindows.dll" (
    echo [OK] platforms\qwindows.dll
) else (
    echo [MISSING] platforms\qwindows.dll
)

if exist "%BUILD_DIR%\styles\qwindowsvistastyle.dll" (
    echo [OK] styles\qwindowsvistastyle.dll
) else (
    echo [MISSING] styles\qwindowsvistastyle.dll
)

echo.
echo Database Files:
if exist "%BUILD_DIR%\w2r_login.db" (
    echo [OK] w2r_login.db
) else (
    if exist "database\w2r_login.db" (
        echo [INFO] w2r_login.db exists in database\ folder but not in build directory
    ) else (
        echo [MISSING] w2r_login.db
    )
)

echo.
echo ====================================
echo Dependency Check Complete
echo ====================================
echo.
echo If any dependencies are missing, you can:
echo 1. Run: build.bat (will auto-deploy dependencies)
echo 2. Run: deploy.bat (comprehensive deployment)
echo 3. Manually copy missing files to %BUILD_DIR%
echo.

pause
