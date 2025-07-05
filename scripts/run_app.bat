@echo off
echo Building and running LoginPage application...

cd /d "c:\Users\Rathe\OneDrive\Documents\QT\LoginPage\build\Desktop_Qt_6_9_1_MinGW_64_bit-Debug"

echo Building...
ninja

if %errorlevel% == 0 (
    echo Build successful! Starting application...
    
    rem Check if application is already running and kill it
    tasklist /FI "IMAGENAME eq LoginPage.exe" 2>NUL | find /I /N "LoginPage.exe">NUL
    if "%ERRORLEVEL%"=="0" (
        echo Terminating existing instance...
        taskkill /F /IM LoginPage.exe >NUL 2>&1
        timeout /T 2 >NUL
    )
    
    rem Start the application
    start "" "LoginPage.exe"
    echo Application started successfully!
) else (
    echo Build failed!
    pause
)
