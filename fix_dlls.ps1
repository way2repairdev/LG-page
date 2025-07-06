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

Write-Host "Downloading PDFium DLL..." -ForegroundColor Yellow
try {
    # For PDFium, we'll use a simpler approach
    # Download from pdfium-binaries repository
    $pdfiumDllUrl = "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F6099/pdfium-win-x64.tgz"
    $pdfiumArchivePath = Join-Path $env:TEMP "pdfium.tgz"
    
    Invoke-WebRequest -Uri $pdfiumDllUrl -OutFile $pdfiumArchivePath -UseBasicParsing
    
    # For now, let's try a different approach - use a pre-built binary
    # Alternative URL for PDFium DLL
    $altPdfiumUrl = "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F6099/pdfium-win-x64.zip"
    $pdfiumZipPath = Join-Path $env:TEMP "pdfium.zip"
    
    try {
        Invoke-WebRequest -Uri $altPdfiumUrl -OutFile $pdfiumZipPath -UseBasicParsing
        
        $tempExtract = Join-Path $env:TEMP "pdfium-temp"
        if (Test-Path $tempExtract) {
            Remove-Item $tempExtract -Recurse -Force
        }
        Expand-Archive -Path $pdfiumZipPath -DestinationPath $tempExtract -Force
        
        # Find the PDFium DLL
        $pdfiumDllFile = Get-ChildItem -Path $tempExtract -Filter "pdfium.dll" -Recurse | Select-Object -First 1
        
        if ($pdfiumDllFile) {
            Copy-Item -Path $pdfiumDllFile.FullName -Destination (Join-Path $pdfiumBinDir "pdfium.dll") -Force
            Write-Host "✓ PDFium DLL downloaded successfully!" -ForegroundColor Green
        } else {
            Write-Host "✗ Could not find PDFium DLL in archive" -ForegroundColor Red
        }
        
        # Cleanup
        Remove-Item $pdfiumZipPath -Force -ErrorAction SilentlyContinue
        Remove-Item $tempExtract -Recurse -Force -ErrorAction SilentlyContinue
        
    } catch {
        Write-Host "✗ ZIP version failed, trying TAR version..." -ForegroundColor Yellow
        
        # Try extracting the TAR file if available
        if (Test-Path $pdfiumArchivePath) {
            $tempExtract = Join-Path $env:TEMP "pdfium-temp"
            if (Test-Path $tempExtract) {
                Remove-Item $tempExtract -Recurse -Force
            }
            New-Item -ItemType Directory -Path $tempExtract -Force | Out-Null
            
            # Use tar command (available in Windows 10+)
            $tarCommand = "tar -xzf `"$pdfiumArchivePath`" -C `"$tempExtract`""
            Invoke-Expression $tarCommand
            
            # Find the PDFium DLL
            $pdfiumDllFile = Get-ChildItem -Path $tempExtract -Filter "pdfium.dll" -Recurse | Select-Object -First 1
            
            if ($pdfiumDllFile) {
                Copy-Item -Path $pdfiumDllFile.FullName -Destination (Join-Path $pdfiumBinDir "pdfium.dll") -Force
                Write-Host "✓ PDFium DLL extracted successfully!" -ForegroundColor Green
            } else {
                Write-Host "✗ Could not find PDFium DLL in TAR archive" -ForegroundColor Red
            }
            
            # Cleanup
            Remove-Item $tempExtract -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    
    # Cleanup
    Remove-Item $pdfiumArchivePath -Force -ErrorAction SilentlyContinue
    
} catch {
    Write-Host "✗ Failed to download PDFium DLL: $($_.Exception.Message)" -ForegroundColor Red
}

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
    Write-Host "✗ PDFium DLL is missing" -ForegroundColor Red
}

Write-Host ""
Write-Host "Next step: Run check_deps.bat to verify all dependencies" -ForegroundColor Green
