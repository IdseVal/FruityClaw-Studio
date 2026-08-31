@echo off
rem Soft-pause THIS project: nothing new starts, work in flight lands. Lift with resume.
cd /d "%~dp0..\.."
python .orca\dispatcher\dispatch.py pause -m "paused from desktop button"
echo.
pause
