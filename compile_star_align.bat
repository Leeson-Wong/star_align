@echo off
echo Compiling star_align.cpp directly...

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d E:\final\star_align

echo.
echo Attempt 1: With DSS headers...
cl.exe /nologo /std:c++17 /EHsc /I dss /I libraw\include ^
    /c star_align.cpp /Fo:build\star_align.obj /O2 /arch:AVX2 /DWIN32 /DNDEBUG

if %ERRORLEVEL% EQU 0 (
    echo star_align.cpp compiled successfully!
    echo Linking standalone star_align...
    link.exe /nologo /out:build\star_align.exe build\star_align.obj ^
        /subsystem:console /entry:mainCRTStartup /LIBPATH:build\libraw\Release raw.lib

    if %ERRORLEVEL% EQU 0 (
        echo.
        echo Build successful! Output: build\star_align.exe
    ) else (
        echo Link failed!
    )
) else (
    echo Compilation failed!
    echo.
    echo Attempt 2: Minimal compilation...
    cl.exe /nologo /std:c++17 /EHsc /I dss ^
        /c star_align.cpp /Fo:build\star_align.obj /O2 /arch:AVX2 /DWIN32 /DNDEBUG /DSINGLE_FILE

    if %ERRORLEVEL% EQU 0 (
        echo star_align.cpp compiled with minimal includes!
        echo Linking standalone star_align...
        link.exe /nologo /out:build\star_align.exe build\star_align.obj ^
            /subsystem:console /entry:mainCRTStartup

        if %ERRORLEVEL% EQU 0 (
            echo.
            echo Build successful! Output: build\star_align.exe
        ) else (
            echo Link failed!
        )
    ) else (
        echo Minimal compilation also failed!
    )
)

pause