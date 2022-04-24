@echo off
set PATH=%PATH%;"C:\Program Files\7-Zip"
set ARCH=x64

for /f %%# in ('WMIC Path Win32_LocalTime Get /Format:value') do @for /f %%@ in ("%%#") do @set %%@

set "month=0%month%"
set "month=%month:~-2%"

set "day=0%day%"
set "day=%day:~-2%"

7z a mtn-%ARCH%-%year%%month%%day%.zip ..\bin\%ARCH%\Release\mtn.exe ..\ffmpeg\%ARCH%\dll\*.dll ..\README.md ..\LICENSE
