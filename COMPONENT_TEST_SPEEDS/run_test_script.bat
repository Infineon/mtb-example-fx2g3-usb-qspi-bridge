@echo off

where python >nul 2>nul
if errorlevel 1 (
    echo python not found, please ensure python is installed and added to PATH
    exit /b 1
)

if not exist ".venv" (
    echo Python virtual environment not found, creating one...
    python -m venv .venv
    if errorlevel 1 (
        echo Failed to create Python virtual environment.
        exit /b 1
    )
)

call .venv\Scripts\activate
if errorlevel 1 (
    echo Failed to activate Python virtual environment.
    exit /b 1
)

python -m pip install --upgrade pip
if errorlevel 1 (
    echo Failed to upgrade pip.
    exit /b 1
)

cmd /k python scripts\qspi_bridge_test_app.py %*
