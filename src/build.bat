@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo vcvars failed & exit /b 1)
cd /d %~dp0
cl /nologo /EHsc /O2 /std:c++17 /utf-8 /W3 /DUNICODE /D_UNICODE main.cpp /Fe:SideOpen.exe /link /SUBSYSTEM:WINDOWS
exit /b %ERRORLEVEL%
