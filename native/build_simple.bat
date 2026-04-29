@echo off
echo Building ChineseIME Native DLL...

set "INCLUDE_DIRS=src;src\include"
set "LIB_DIRS="
set "LIBS=user32.lib ole32.lib oleaut32.lib imm32.lib"

cl /nologo ^
   /c ^
   /O2 ^
   /D "CHINESEIME_EXPORTS" ^
   /D "UNICODE" ^
   /D "_UNICODE" ^
   /I "src" ^
   src\ime_bridge.cpp

if %errorlevel% neq 0 goto error

echo Linking DLL...
link /nologo ^
     /DLL ^
     /OUT:chineseime_native.dll ^
     ime_bridge.obj ^
     %LIBS%

if %errorlevel% neq 0 goto error

echo DLL built successfully!
del ime_bridge.obj
copy chineseime_native.dll ..\natives\Release\
goto end

:error
echo Build failed!
pause

:end