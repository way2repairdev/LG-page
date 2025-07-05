@echo off
echo ====================================
echo MySQL ODBC Driver Installation Test
echo ====================================
echo.

echo Checking for ODBC drivers...
echo.

rem Check if odbcad32.exe exists
if exist "%SystemRoot%\System32\odbcad32.exe" (
    echo ODBC Administrator found.
    echo.
    echo Opening ODBC Data Source Administrator...
    echo Please check the "Drivers" tab for MySQL ODBC drivers.
    echo.
    echo Look for entries like:
    echo - MySQL ODBC 8.0 ANSI Driver
    echo - MySQL ODBC 8.0 Unicode Driver
    echo.
    start odbcad32.exe
) else (
    echo ERROR: ODBC Administrator not found!
)

echo.
echo After checking the drivers, note the exact driver name
echo and update databasemanager.cpp if needed.
echo.
echo Press any key to continue...
pause >nul
