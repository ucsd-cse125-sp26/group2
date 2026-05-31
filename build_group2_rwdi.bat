@echo off
REM Build the actual configured preset (relwithdebinfo) with the real toolchain.
REM NOTE: build_group2.bat targets build\debug (not configured) AND references VS Enterprise
REM (not installed). The machine has VS 2022 BuildTools + CLion's bundled CMake/Ninja.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
"C:\Program Files\JetBrains\CLion 2026.1.2\bin\cmake\win\x64\bin\cmake.exe" --build "%~dp0build\relwithdebinfo" --target group2 -- -k0
