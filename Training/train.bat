@echo off
rem Auto training launcher. Double-click to be asked for a run name.
rem   train.bat                        ask for the run name
rem   train.bat A                      train A (new: from scratch, existing: resume latest snapshot)
rem   train.bat B --from A --note "x"  start new run B from A's latest policy
rem   train.bat C -- -SHApproach=1     new run C with extra Unreal options
rem Stop with Ctrl+C in this window. See Training\README.md.

chcp 65001 >nul
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8
cd /d "%~dp0.."

where python >nul 2>nul
if %errorlevel%==0 (
    python "%~dp0train.py" %*
) else (
    py -3 "%~dp0train.py" %*
)
echo.
pause
