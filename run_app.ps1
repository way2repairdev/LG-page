# PowerShell script to build and run the LoginPage application

# Set execution policy for this session if needed
# Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser

# Change to build directory
$buildDir = "c:\Users\Rathe\OneDrive\Documents\QT\LoginPage\build\Desktop_Qt_6_9_1_MinGW_64_bit-Debug"
Set-Location $buildDir

Write-Host "Building application..." -ForegroundColor Green
ninja

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build successful! Starting application..." -ForegroundColor Green
    
    # Check if application is already running
    $runningProcess = Get-Process -Name "LoginPage" -ErrorAction SilentlyContinue
    if ($runningProcess) {
        Write-Host "Application is already running. Terminating existing instance..." -ForegroundColor Yellow
        $runningProcess | Stop-Process -Force
        Start-Sleep -Seconds 2
    }
    
    # Start the application
    $exePath = Join-Path $buildDir "LoginPage.exe"
    Start-Process -FilePath $exePath
    
    Write-Host "Application started successfully!" -ForegroundColor Green
} else {
    Write-Host "Build failed!" -ForegroundColor Red
}
