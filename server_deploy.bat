@echo off
echo === AssaultCube Anti-Cheat Server Deploy ===
echo.

echo [1/4] Stopping existing containers...
docker compose down
if errorlevel 1 (
    echo WARNING: docker compose down failed, continuing anyway...
)
echo.

echo [2/4] Cleaning stale data...
docker volume rm assaultcubeanti-cheat_dashboard-data >nul 2>&1
echo // AC server vita database> config\servervita.cfg
echo // fresh start>> config\servervita.cfg
echo Cleared servervita.cfg
echo.

echo [3/4] Rebuilding images...
docker compose build --no-cache
if errorlevel 1 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)
echo.

echo [4/4] Starting containers...
docker compose up -d
if errorlevel 1 (
    echo ERROR: Failed to start containers.
    pause
    exit /b 1
)
echo.

timeout /t 3 /nobreak >nul
docker compose ps
echo.

echo === Deploy complete ===
echo   Server:    localhost:28763
echo   Dashboard: http://localhost:8080
echo.
echo Showing logs (Ctrl+C to stop)...
docker compose logs -f
