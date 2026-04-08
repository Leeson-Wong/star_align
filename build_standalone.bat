@echo off
echo Building standalone test...

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d E:\final\star_align

echo Compiling standalone_test.cpp...
cl.exe /nologo /std:c++17 /EHsc /I dss ^
    /c standalone_test.cpp /Fo:build\standalone_test.obj /O2 /arch:AVX2

if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed
    pause
    exit /b 1
)

echo Linking...
link.exe /nologo /out:build\standalone_test.exe build\standalone_test.obj ^
    /subsystem:console /entry:mainCRTStartup

if %ERRORLEVEL% NEQ 0 (
    echo Link failed
    pause
    exit /b 1
)

echo Build successful!
echo.
echo Running test:
echo -------------
build\standalone_test.exe
echo.
pause