@echo off
cd /d "%~dp0..\.."
python .orca\dispatcher\dispatch.py resume
echo.
pause
