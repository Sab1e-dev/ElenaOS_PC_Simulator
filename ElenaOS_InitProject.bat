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

:: ===================================================================
:: 主脚本开始
:: ===================================================================

:: ========DEBUG========
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


@REM cd /d "%GenJSONPath%"
if not exist output md output

:: 使用检测到的 Python 3.10 创建虚拟环境
"!PYTHON_EXE!" -m venv .venv

:: 安装依赖
"%GenJSONPath%"\venv\Scripts\python -m pip install -r requirements.txt

:: 生成 JSON 文件
"%GenJSONPath%"\venv\Scripts\python gen_json.py --output-path=output
echo ✅ 生成的 JSON 文件位于: %GenJSONPath%\output\lvgl.json

:GEN_LV_BINDING_C
echo.
echo ==============================
echo Generating lv_bindings.c...
echo ==============================
cd /d "%CWD%"
"!PYTHON_EXE!" %CWD%\scripts\gen_lvgl_binding.py ^
 --json-path=%GenJSONPath%\output\lvgl.json ^
 --output-c-path=%CWD%\appsys\src\lv_bindings.c

:END
endlocal
exit /b 0
