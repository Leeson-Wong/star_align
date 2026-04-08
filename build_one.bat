@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d E:\final\star_align
cl.exe /nologo /std:c++17 /EHsc /I dss /I libraw\include /c dss\MatchingStars.cpp /Fo:build\MatchingStars.obj
