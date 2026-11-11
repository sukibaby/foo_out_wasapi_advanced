@echo off
setlocal

set "SCRIPT_DIR=%~dp0"

for %%D in ("x64" "Win32") do (
    if exist "%SCRIPT_DIR%%%~D\" (
        echo Removing "%SCRIPT_DIR%%%~D"
        rmdir /s /q "%SCRIPT_DIR%%%~D"
        if errorlevel 1 goto :failed
    )
)

if exist "%SCRIPT_DIR%foo_out_wasapi_custom.fb2k-component" (
    echo Removing "%SCRIPT_DIR%foo_out_wasapi_custom.fb2k-component"
    del /q "%SCRIPT_DIR%foo_out_wasapi_custom.fb2k-component"
    if errorlevel 1 goto :failed
)

echo Build artifacts cleaned.
exit /b 0

:failed
echo ERROR: Failed to remove one or more build artifacts.
exit /b 1
