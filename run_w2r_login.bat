@echo off
echo Starting Way2Repair Login System...
echo.
echo Database: MySQL via ODBC (login_system)
echo Requirements:
echo   - WAMP Server running
echo   - MySQL ODBC Driver installed
echo   - Database 'login_system' created
echo.
echo Default credentials:
echo   Admin: admin / password
echo   User:  user / 1234
echo.
cd /d "%~dp0build\Desktop_Qt_6_9_1_MinGW_64_bit-Debug"
LoginPage.exe
pause
