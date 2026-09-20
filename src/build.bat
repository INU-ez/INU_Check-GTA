@echo off
rem gtacheck — standalone build (MSVC, x64, D3D9).  Needs librw-southland built
rem (lib\win-amd64-d3d9\Release\rw.lib) next to this repo, like ariane itself.
rem   build.bat            -> release
rem   build.bat debug      -> debug (/Zi, no /O2)
setlocal
set VS=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1
if errorlevel 1 ( echo cl.exe not found - vcvars64 did not take & exit /b 1 )

cd /d "%~dp0"
set ROOT=%~dp0..\..
set LIBRW=%ROOT%\..\librw-southland
set OUT=%ROOT%\bin\win-amd64-d3d9\Release
set OBJ=%~dp0build
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%" mkdir "%OUT%"

set CFLAGS=/nologo /EHsc /MD /W3 /std:c++17 /utf-8 /DRW_D3D9 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /wd4996 /wd4244 /I"%LIBRW%" /I"%LIBRW%\skeleton" /I"%ROOT%\src" /I"%ROOT%" /Fo"%OBJ%\\" /Fd"%OBJ%\\"
if /I "%1"=="debug" ( set CFLAGS=%CFLAGS% /Zi /Od ) else ( set CFLAGS=%CFLAGS% /O2 /Zi )

set SRC=main.cpp imgui_render.cpp fix.cpp util.cpp gamedata.cpp check_txd.cpp check_dff.cpp check_veh.cpp check_col.cpp check_ifp.cpp check_misc.cpp check_handling.cpp check_weapon.cpp check_ped.cpp check_clothes.cpp check_cuts.cpp check_text.cpp check_audio.cpp lang.cpp rule_help.cpp txd_edit.cpp runner.cpp
set SKEL="%LIBRW%\skeleton\skeleton.cpp" "%LIBRW%\skeleton\win.cpp" "%LIBRW%\skeleton\imgui\imgui.cpp" "%LIBRW%\skeleton\imgui\imgui_draw.cpp" "%LIBRW%\skeleton\imgui\imgui_tables.cpp" "%LIBRW%\skeleton\imgui\imgui_widgets.cpp" "%LIBRW%\skeleton\imgui\imgui_impl_rw.cpp" "%LIBRW%\skeleton\imgui\ImGuizmo.cpp"

cl %CFLAGS% %SRC% %SKEL% /link /OUT:"%OUT%\gtacheck.exe" /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup /LIBPATH:"%LIBRW%\lib\win-amd64-d3d9\Release" rw.lib d3d9.lib gdi32.lib user32.lib ole32.lib shell32.lib Xinput9_1_0.lib /DEBUG
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo.
echo BUILD OK  -^>  %OUT%\gtacheck.exe
