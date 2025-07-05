@echo off
echo Starting Way2Repair Login System...
echo.

:: Add Qt to PATH
set PATH=C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%

:: Check if executable exists
if not exist "build\Way2RepairLoginSystem.exe" (
    echo Error: Application not found!
    echo Please build the project first using build.bat
    pause
    exit /b 1
)

:: Run the application
echo Running Way2RepairLoginSystem...
start "" "build\Way2RepairLoginSystem.exe"

echo Application started successfully!
pause
