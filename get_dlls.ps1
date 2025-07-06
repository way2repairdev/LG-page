# PowerShell script to download required DLLs
Write-Host "===============================================" -ForegroundColor Green
Write-Host "      Downloading Required DLLs" -ForegroundColor Green
Write-Host "===============================================" -ForegroundColor Green
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$pdfiumBinDir = Join-Path $scriptDir "src\pdf\third_party\extern\pdfium\bin"
$glewBinDir = Join-Path $scriptDir "src\pdf\third_party\extern\glew\bin\Release\x64"

# Create directories if they don't exist
if (!(Test-Path $pdfiumBinDir)) {
    New-Item -ItemType Directory -Path $pdfiumBinDir -Force | Out-Null
}
if (!(Test-Path $glewBinDir)) {
    New-Item -ItemType Directory -Path $glewBinDir -Force | Out-Null
}

# Download GLEW DLL
Write-Host "Downloading GLEW DLL..." -ForegroundColor Yellow
$glewUrl = "https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0-win32.zip"
$glewZipPath = Join-Path $env:TEMP "glew-2.2.0-win32.zip"
$glewExtractPath = Join-Path $env:TEMP "glew-extract"

try {
    Write-Host "Downloading GLEW archive..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $glewUrl -OutFile $glewZipPath -UseBasicParsing
    
    Write-Host "Extracting GLEW..." -ForegroundColor Cyan
    Expand-Archive -Path $glewZipPath -DestinationPath $glewExtractPath -Force
    
    # Find and copy glew32.dll
    $glewDllSource = Get-ChildItem -Path $glewExtractPath -Name "glew32.dll" -Recurse | Select-Object -First 1
    if ($glewDllSource) {
        $glewDllSourcePath = Join-Path $glewExtractPath $glewDllSource.FullName
        $glewDllDestPath = Join-Path $glewBinDir "glew32.dll"
        Copy-Item -Path $glewDllSourcePath -Destination $glewDllDestPath -Force
        Write-Host "✓ GLEW DLL copied successfully!" -ForegroundColor Green
    } else {
        Write-Host "✗ Could not find glew32.dll in the archive" -ForegroundColor Red
    }
    
    # Cleanup
    Remove-Item -Path $glewZipPath -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $glewExtractPath -Recurse -Force -ErrorAction SilentlyContinue
    
} catch {
    Write-Host "✗ Failed to download GLEW: $($_.Exception.Message)" -ForegroundColor Red
}

# Download PDFium DLL
Write-Host ""
Write-Host "Downloading PDFium DLL..." -ForegroundColor Yellow
$pdfiumUrl = "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F6099/pdfium-win-x64.tgz"
$pdfiumArchivePath = Join-Path $env:TEMP "pdfium-win-x64.tgz"
$pdfiumExtractPath = Join-Path $env:TEMP "pdfium-extract"

try {
    Write-Host "Downloading PDFium archive..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $pdfiumUrl -OutFile $pdfiumArchivePath -UseBasicParsing
    
    Write-Host "Extracting PDFium..." -ForegroundColor Cyan
    # Create extraction directory
    if (!(Test-Path $pdfiumExtractPath)) {
        New-Item -ItemType Directory -Path $pdfiumExtractPath -Force | Out-Null
    }
    
    # Extract tar.gz file (requires tar command available in Windows 10+)
    $tarCommand = "tar -xzf `"$pdfiumArchivePath`" -C `"$pdfiumExtractPath`""
    Invoke-Expression $tarCommand
    
    # Find and copy pdfium.dll
    $pdfiumDllSource = Get-ChildItem -Path $pdfiumExtractPath -Name "pdfium.dll" -Recurse | Select-Object -First 1
    if ($pdfiumDllSource) {
        $pdfiumDllSourcePath = $pdfiumDllSource.FullName
        $pdfiumDllDestPath = Join-Path $pdfiumBinDir "pdfium.dll"
        Copy-Item -Path $pdfiumDllSourcePath -Destination $pdfiumDllDestPath -Force
        Write-Host "✓ PDFium DLL copied successfully!" -ForegroundColor Green
    } else {
        Write-Host "✗ Could not find pdfium.dll in the archive" -ForegroundColor Red
    }
    
    # Cleanup
    Remove-Item -Path $pdfiumArchivePath -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $pdfiumExtractPath -Recurse -Force -ErrorAction SilentlyContinue
    
} catch {
    Write-Host "✗ Failed to download PDFium: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Note: PDFium requires tar command (available in Windows 10+)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "===============================================" -ForegroundColor Green
Write-Host "      Download Complete" -ForegroundColor Green
Write-Host "===============================================" -ForegroundColor Green
Write-Host ""
Write-Host "Checking downloaded files..." -ForegroundColor Cyan

$glewDllPath = Join-Path $glewBinDir "glew32.dll"
$pdfiumDllPath = Join-Path $pdfiumBinDir "pdfium.dll"

if (Test-Path $glewDllPath) {
    Write-Host "✓ GLEW DLL found at: $glewDllPath" -ForegroundColor Green
} else {
    Write-Host "✗ GLEW DLL not found" -ForegroundColor Red
}

if (Test-Path $pdfiumDllPath) {
    Write-Host "✓ PDFium DLL found at: $pdfiumDllPath" -ForegroundColor Green
} else {
    Write-Host "✗ PDFium DLL not found" -ForegroundColor Red
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Run: check_deps.bat" -ForegroundColor White
Write-Host "2. Run: build.bat" -ForegroundColor White
Write-Host "3. Test the application" -ForegroundColor White
Write-Host ""
Write-Host "Press any key to continue..." -ForegroundColor Gray
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
