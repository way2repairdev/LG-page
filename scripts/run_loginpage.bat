@echo off
echo Starting Qt LoginPage Application...

REM Set Qt environment variables
set PATH=C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%
set QT_QPA_PLATFORM_PLUGIN_PATH=C:\Qt\6.9.1\mingw_64\plugins\platforms

REM Run the application
build\LoginPage.exe

pause
