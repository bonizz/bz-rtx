@echo off

call :build raygen.rgen
call :build chit.rchit
call :build shadow-chit.rchit
call :build miss.rmiss
call :build shadow-miss.rmiss

goto :eof

:build
%VULKAN_SDK%\bin\glslangValidator.exe -V %1 -o %1.spv
goto :eof
