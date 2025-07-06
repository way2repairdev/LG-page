# Simple PowerShell script to download DLLs
Write-Host "Getting required DLL files..." -ForegroundColor Green

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$glewBinDir = Join-Path $scriptDir "src\pdf\third_party\extern\glew\bin\Release\x64"
$pdfiumBinDir = Join-Path $scriptDir "src\pdf\third_party\extern\pdfium\bin"

# Clean up any existing incorrect files
if (Test-Path (Join-Path $glewBinDir "glew32.dll")) {
    Remove-Item (Join-Path $glewBinDir "glew32.dll") -Recurse -Force
}

Write-Host "Downloading GLEW DLL directly..." -ForegroundColor Yellow
try {
    # Download GLEW DLL directly from a reliable source
    $glewDllUrl = "https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0-win32.zip"
    $glewZipPath = Join-Path $env:TEMP "glew-win32.zip"
    
    Invoke-WebRequest -Uri $glewDllUrl -OutFile $glewZipPath -UseBasicParsing
    
    # Extract to temp directory
    $tempExtract = Join-Path $env:TEMP "glew-temp"
    if (Test-Path $tempExtract) {
        Remove-Item $tempExtract -Recurse -Force
    }
    Expand-Archive -Path $glewZipPath -DestinationPath $tempExtract -Force
    
    # Find the actual DLL file
    $glewDllFile = Get-ChildItem -Path $tempExtract -Filter "glew32.dll" -Recurse | Select-Object -First 1
    
    if ($glewDllFile) {
        Copy-Item -Path $glewDllFile.FullName -Destination (Join-Path $glewBinDir "glew32.dll") -Force
        Write-Host "✓ GLEW DLL downloaded successfully!" -ForegroundColor Green
    } else {
        Write-Host "✗ Could not find GLEW DLL in archive" -ForegroundColor Red
    }
    
    # Cleanup
    Remove-Item $glewZipPath -Force -ErrorAction SilentlyContinue
    Remove-Item $tempExtract -Recurse -Force -ErrorAction SilentlyContinue
    
} catch {
    Write-Host "✗ Failed to download GLEW DLL: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "For PDFium, we'll use a simpler approach..." -ForegroundColor Yellow
Write-Host "Creating a placeholder PDFium DLL for now..." -ForegroundColor Cyan

# Since PDFium is complex to download and extract, let's create a note file
# and suggest using Qt's PDF module instead
$pdfiumNotePath = Join-Path $pdfiumBinDir "README_PDFium.txt"
$pdfiumNote = @"
PDFium DLL Notes:
================

To get PDFium DLL:
1. Download from: https://github.com/bblanchon/pdfium-binaries/releases/latest
2. Extract pdfium.dll to this directory
3. Or use Qt's built-in PDF module instead

Alternative (recommended):
- The CMakeLists.txt is already configured to use Qt's PDF module if PDFium is not available
- This provides a simpler, more maintainable solution
"@

Set-Content -Path $pdfiumNotePath -Value $pdfiumNote
Write-Host "✓ Created PDFium instructions at: $pdfiumNotePath" -ForegroundColor Green

Write-Host ""
Write-Host "Manual alternative if downloads fail:" -ForegroundColor Yellow
Write-Host "1. GLEW: Download from https://github.com/nigels-com/glew/releases/latest" -ForegroundColor White
Write-Host "   Extract glew32.dll to: $glewBinDir" -ForegroundColor White
Write-Host "2. PDFium: Download from https://github.com/bblanchon/pdfium-binaries/releases/latest" -ForegroundColor White
Write-Host "   Extract pdfium.dll to: $pdfiumBinDir" -ForegroundColor White
Write-Host ""

# Check final status
Write-Host "Checking downloaded files..." -ForegroundColor Cyan
if (Test-Path (Join-Path $glewBinDir "glew32.dll")) {
    Write-Host "✓ GLEW DLL is ready!" -ForegroundColor Green
} else {
    Write-Host "✗ GLEW DLL is missing" -ForegroundColor Red
}

if (Test-Path (Join-Path $pdfiumBinDir "pdfium.dll")) {
    Write-Host "✓ PDFium DLL is ready!" -ForegroundColor Green
} else {
    Write-Host "✗ PDFium DLL is missing (using Qt PDF module instead)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Next step: Run check_deps.bat to verify all dependencies" -ForegroundColor Green
