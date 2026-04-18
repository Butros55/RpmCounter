@echo off
setlocal
call "%~dp0simulator\run_simulator.cmd" %*
exit /b %ERRORLEVEL%
