@echo off
echo Checking star_align.h compilation...

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d E:\final\star_align

cl.exe /nologo /std:c++17 /EHsc /I dss ^
    /c check_header.cpp /Fo:build\check_header.obj

if %ERRORLEVEL% EQU 0 (
    echo Header compilation successful!
    link.exe /nologo /out:build\check_header.exe build\check_header.obj ^
        /subsystem:console /entry:mainCRTStartup

    if %ERRORLEVEL% EQU 0 (
        echo.
        echo Running check_header.exe...
        echo.
        build\check_header.exe
    )
) else (
    echo Header compilation failed!
)

pause