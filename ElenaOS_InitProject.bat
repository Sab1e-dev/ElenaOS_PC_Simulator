@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

set "PYTHON_EXE="
set "PYTHON_REQUIRED=3.10"

set CWD=%cd%
set JerryScriptPath=%CWD%\external\jerryscript
set LVGLPath=%CWD%\LvglPlatform\lvgl
set GenJSONPath=%LVGLPath%\scripts\gen_json

cls
:: ===================================================================
:: 检查并提取 Python 3.10 路径
:: ===================================================================
for /f "tokens=1,*" %%A in ('py -0p 2^>nul') do (
    echo %%A | findstr /c:"-V:%PYTHON_REQUIRED%" >nul
    if !errorlevel! == 0 (
        set "PYTHON_EXE=%%B"
        goto :SHOW_VERSION
    )
)

echo ❌ 未找到 Python %PYTHON_REQUIRED%，请安装并确保已注册到 py 启动器。
goto :END

:SHOW_VERSION
echo.
echo ✅ 已检测到 Python %PYTHON_REQUIRED%
echo 使用解释器: !PYTHON_EXE!

:: 使用检测到的 Python 3.10 创建虚拟环境
"!PYTHON_EXE!" -m venv .venv

set "VIRTUAL_PYTHON_EXE=%CWD%\.venv\Scripts\python.exe"

if not exist "!VIRTUAL_PYTHON_EXE!" echo 虚拟Python环境创建失败

:: ===================================================================
:: 主脚本开始
:: ===================================================================

:: ========DEBUG========
@REM goto :GEN_LVGL_JSON
goto :GEN_LV_BINDING_C

:BUILD_JERRYSCRIPT
echo.
echo ==============================
echo Building JerryScript...
echo ==============================

"!PYTHON_EXE!" %JerryScriptPath%\tools\build.py ^
 --cmake-param="-DCMAKE_C_FLAGS_DEBUG=/MTd" ^
 --cmake-param="-DCMAKE_CXX_FLAGS_DEBUG=/MTd" ^
 --cmake-param="-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug" ^
 --clean --debug ^
 --cmake-param=-DCMAKE_CXX_FLAGS="/guard:ehcont" ^
 --cmake-param=-DCMAKE_C_FLAGS="/guard:ehcont"

@REM goto :eof

:GEN_LVGL_JSON
echo.
echo ==============================
echo Generating lvgl.json...
echo ==============================

if not exist output md output

if not exist "%GenJSONPath%\gen_json.py" echo Python脚本不存在

if not exist "%CWD%\LvglWindowsSimulator\lv_conf.h" echo 配置文件不存在

:: 安装依赖
"!VIRTUAL_PYTHON_EXE!" -m pip install -r "%GenJSONPath%"\requirements.txt

if exist "%GenJSONPath%\output\lvgl.json" (
    del /q "%GenJSONPath%\output\lvgl.json"
)

echo 正在生成 [lvgl.json]
:: 生成 JSON 文件
set LV_CONF_PARAMETER=--lvgl-config="%CWD%\lv_conf.h"

set OUTPUT_PATH_PARAMETER=--output-path="%GenJSONPath%\output"

"!VIRTUAL_PYTHON_EXE!" "%GenJSONPath%\gen_json.py" %OUTPUT_PATH_PARAMETER% %LV_CONF_PARAMETER%

if exist "%GenJSONPath%\output\lvgl.json" (
    echo ✅ 生成的 JSON 文件位于: %GenJSONPath%\output\lvgl.json
) else (
    echo ❌ 生成失败，正在推出程序...
    goto :END
)

:GEN_LV_BINDING_C
echo.
echo ==============================
echo Generating lv_bindings.c...
echo ==============================
cd /d "%CWD%"

"!VIRTUAL_PYTHON_EXE!" -m pip install -r "%CWD%"\requirements.txt

"!VIRTUAL_PYTHON_EXE!" %CWD%\scripts\lv_binding_jerryscript\gen_lvgl_binding.py ^
 --json-path=%GenJSONPath%\output\lvgl.json ^
 --output-c-path=%CWD%\appsys\src\lv_bindings.c ^
 --extract-funcs-from=%CWD%\LvglWindowsSimulator\main.js ^
 --cfg-path=%CWD%\scripts\lv_binding_jerryscript\examples\ElenaOS_PC_Simulator

:END
endlocal
exit /b 0
