@echo off

call :build raygen.rgen
call :build chit.rchit
call :build miss.rmiss

goto :eof

:build
%VULKAN_SDK%\bin\glslangValidator.exe -V %1 -o %1.spv
goto :eof
