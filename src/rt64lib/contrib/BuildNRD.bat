@echo off

REM Builds NRD into contrib/NRD/_Bin, and generates contrib/NRD/Shaders/NRDConfig.hlsli.
REM Must be run before rt64lib, whose shaders include NRD.hlsli.
REM
REM Every option is passed explicitly on every run. CMake caches option() values, so dropping a flag
REM here does NOT revert it to its default - the previously cached value silently survives.

cmake -S NRD -B NRD/_Build ^
  -DNRD_NRI=OFF ^
  -DNRD_EMBEDS_SPIRV_SHADERS=OFF ^
  -DNRD_EMBEDS_DXBC_SHADERS=OFF ^
  -DNRD_SUPPORTS_VIEWPORT_OFFSET=OFF ^
  -DNRD_SUPPORTS_CHECKERBOARD=OFF ^
  -DNRD_SUPPORTS_DISOCCLUSION_THRESHOLD_MIX=OFF ^
  -DNRD_SUPPORTS_HISTORY_CONFIDENCE=ON ^
  -DNRD_SUPPORTS_ANTIFIREFLY=ON
if %ERRORLEVEL% NEQ 0 exit /B %ERRORLEVEL%

findstr /C:"NRD_SUPPORTS_HISTORY_CONFIDENCE 1" NRD\Shaders\NRDConfig.hlsli >nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: NRDConfig.hlsli still has history confidence disabled - stale CMake cache.
    echo Delete NRD\_Build and run this script again.
    exit /B 1
)

cmake --build NRD/_Build --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% NEQ 0 exit /B %ERRORLEVEL%

cmake --build NRD/_Build --config Debug -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% NEQ 0 exit /B %ERRORLEVEL%

exit /B 0
