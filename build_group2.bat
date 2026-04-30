@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
echo INCLUDE=%INCLUDE%
cmake --build build\debug --target group2 -- -k0
