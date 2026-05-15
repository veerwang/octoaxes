@echo off
REM 启动 octoaxesplus 上位机（共享 software/.venv）
pushd "%~dp0\.."
if not exist .venv\Scripts\python.exe (
    echo [ERROR] .venv not found, creating...
    python -m venv .venv
    .venv\Scripts\pip install -r requirements.txt
)
.venv\Scripts\python octoaxesplus\main.py
popd
pause
