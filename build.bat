@echo off
REM Build tabsess.exe
REM Requires MinGW (gcc) in PATH
REM For MSVC:  rc tabsess.rc && cl /O2 tabsess.c tabsess.res /link user32.lib gdi32.lib shell32.lib /SUBSYSTEM:WINDOWS

windres tabsess.rc -o tabsess_res.o
gcc -O2 -mwindows tabsess.c tabsess_res.o -o tabsess.exe -luser32 -lgdi32 -lshell32

if %errorlevel% == 0 (
    echo Build OK: tabsess.exe
) else (
    echo Build FAILED
)
