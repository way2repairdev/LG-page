@echo off
echo ================================
echo MySQL Connection Test Compiler
echo ================================
echo.

rem Find Qt installation
set QT_DIR=
for /d %%i in ("C:\Qt\*") do (
    if exist "%%i\msvc*\bin\qmake.exe" (
        set QT_DIR=%%i\msvc*
        goto found_qt
    )
    if exist "%%i\mingw*\bin\qmake.exe" (
        set QT_DIR=%%i\mingw*
        goto found_qt
    )
)

echo Qt installation not found in C:\Qt\
echo Please run this from Qt Creator or set Qt environment manually.
goto end

:found_qt
echo Found Qt at: %QT_DIR%
echo.

rem Set Qt environment
set PATH=%QT_DIR%\bin;%PATH%
set QT_PLUGIN_PATH=%QT_DIR%\plugins

echo Compiling MySQL connection test...
qmake -project "QT += sql core" "CONFIG += console" "TARGET = test_mysql_connection"
qmake
mingw32-make clean
mingw32-make

if exist "test_mysql_connection.exe" (
    echo.
    echo Running connection test...
    echo ================================
    test_mysql_connection.exe
) else (
    echo.
    echo Compilation failed. Please check Qt installation.
)

:end
echo.
echo Press any key to continue...
pause >nul
