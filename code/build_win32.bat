@ECHO off

SET WARNING_FLAGS=/WX /W4 /wd4201 /wd4204 /wd4702
SET WARNING_FLAGS=%WARNING_FLAGS% /wd4100 /wd4101 /wd4189

SET COMPILER_FLAGS=/nologo /Z7 /Oi /FC /MT /diagnostics:column %WARNING_FLAGS%
SET LINKER_FLAGS=/opt:ref /incremental:no user32.lib gdi32.lib winmm.lib ole32.lib mmdevapi.lib

IF NOT EXIST ..\build mkdir ..\build
PUSHD ..\build

REM NOTE(law): Compile any resources needed by the executable
rc /nologo /i ..\data /fo sokoban.res ..\data\sokoban.rc

REM NOTE(law): Compile the actual executables
cl ..\code\platform_win32_main.c sokoban.res /Od /DDEVELOPMENT_BUILD=1 %COMPILER_FLAGS% /Fe:sokoban_debug   /link %LINKER_FLAGS%
cl ..\code\platform_win32_main.c sokoban.res /O2 /DDEVELOPMENT_BUILD=0 %COMPILER_FLAGS% /Fe:sokoban_release /link %LINKER_FLAGS%

POPD
