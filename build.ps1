# Small Oscilloscope Test Program Build Script
Write-Host "Building Small Oscilloscope Test Program..." -ForegroundColor Green

# ESP-IDF 환경 설정 (사용자 환경에 맞게 수정 필요)
# $env:IDF_PATH = "C:\esp\esp-idf"
# & "$env:IDF_PATH\export.ps1"

# 프로젝트 빌드
try {
    idf.py build
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "Build successful!" -ForegroundColor Green
        Write-Host ""
        Write-Host "To flash the program, run: idf.py -p COM_PORT flash" -ForegroundColor Yellow
        Write-Host "To monitor output, run: idf.py -p COM_PORT monitor" -ForegroundColor Yellow
        Write-Host ""
    } else {
        Write-Host ""
        Write-Host "Build failed!" -ForegroundColor Red
        Write-Host ""
    }
} catch {
    Write-Host ""
    Write-Host "Error: idf.py command not found. Please install ESP-IDF first." -ForegroundColor Red
    Write-Host "Visit: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/" -ForegroundColor Cyan
    Write-Host ""
}

Read-Host "Press Enter to continue" 