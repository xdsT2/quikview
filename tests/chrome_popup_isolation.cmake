# 弹出菜单 / 上下悬浮铬件显隐隔离 · 单用例执行器（CTest 以 cmake -P 调用）
#
# 用法：
#   cmake -DQUIKVIEW_EXE=<quikview.exe> -DQUIKVIEW_SOURCE_DIR=<repo>
#         -DQUIKVIEW_WORK_ROOT=<build>/ctest-chrome-popup -DQUIKVIEW_CASE=<name>
#         -P tests/chrome_popup_isolation.cmake
#
# 判定完全基于 --smoke report 的状态报告；--screenshot 只用来让进程按确定时序自动退出。
# 每个用例独占工作目录 + 独立 QUIKVIEW_CONFIG_PATH，不污染用户 config.ini，
# 用例之间也不会互相覆盖 Temp/zoom-report.txt。

cmake_minimum_required(VERSION 3.16)

foreach(_required QUIKVIEW_EXE QUIKVIEW_SOURCE_DIR QUIKVIEW_WORK_ROOT QUIKVIEW_CASE)
    if(NOT ${_required})
        message(FATAL_ERROR "缺少 -D${_required}=...")
    endif()
endforeach()

include("${QUIKVIEW_SOURCE_DIR}/tests/chrome_popup_cases.cmake")

set(_prefix "QV_CASE_${QUIKVIEW_CASE}")
if(NOT DEFINED ${_prefix}_SMOKE)
    message(FATAL_ERROR "未定义的用例：${QUIKVIEW_CASE}")
endif()
set(_smoke "${${_prefix}_SMOKE}")
set(_exp1 "${${_prefix}_EXPECT1}")
set(_exp2 "${${_prefix}_EXPECT2}")
set(_extra_files "${${_prefix}_FILES}")

set(_case_dir "${QUIKVIEW_WORK_ROOT}/${QUIKVIEW_CASE}")
file(MAKE_DIRECTORY "${_case_dir}/images")

# 样本图从版本管理内的 docs/screenshots 复制，保证「多图目录」（滑杆可用），
# 且不依赖 Temp/ 下任何未入库的历史产物。
file(GLOB _fixture_images "${QUIKVIEW_SOURCE_DIR}/docs/screenshots/*.png")
list(LENGTH _fixture_images _fixture_count)
if(_fixture_count LESS 2)
    message(FATAL_ERROR "样本图不足 2 张：${QUIKVIEW_SOURCE_DIR}/docs/screenshots/*.png")
endif()
file(COPY ${_fixture_images} DESTINATION "${_case_dir}/images")
list(GET _fixture_images 0 _first_image)
get_filename_component(_image_name "${_first_image}" NAME)
set(_image "${_case_dir}/images/${_image_name}")
file(TO_NATIVE_PATH "${_image}" _image_native)

if(CMAKE_HOST_WIN32)
    set(_path_sep ";")
else()
    set(_path_sep ":")
endif()
get_filename_component(_exe_dir "${QUIKVIEW_EXE}" DIRECTORY)

string(REPLACE "," ";" _commands "${_smoke}")
list(LENGTH _commands _command_count)
# main.cpp：第 N 条命令在 1500+1300*(N-1) ms 执行；全部命令跑完再留 900ms 渲染余量。
math(EXPR _shot_delay "1500 + 1300 * ${_command_count} + 900")

if("${_exp2}" STREQUAL "")
    set(_expected_reports 1)
else()
    set(_expected_reports 2)
endif()

# 一行报告里逐字段断言；失败原因写到 QV_LINE_ERR，由调用方统一收口。
function(qv_check_line line pairs tag)
    foreach(_pair IN LISTS pairs)
        string(REPLACE "=" ";" _kv "${_pair}")
        list(LENGTH _kv _kv_len)
        if(NOT _kv_len EQUAL 2)
            set(QV_LINE_OK FALSE PARENT_SCOPE)
            set(QV_LINE_ERR "${tag}: 用例表格式错误 '${_pair}'" PARENT_SCOPE)
            return()
        endif()
        list(GET _kv 0 _key)
        list(GET _kv 1 _want)
        string(REGEX MATCH " ${_key}=([^ ]+)" _match "${line}")
        if(NOT _match)
            set(QV_LINE_OK FALSE PARENT_SCOPE)
            set(QV_LINE_ERR "${tag}: 状态报告缺少字段 ${_key}" PARENT_SCOPE)
            return()
        endif()
        if(NOT "${CMAKE_MATCH_1}" STREQUAL "${_want}")
            set(QV_LINE_OK FALSE PARENT_SCOPE)
            set(QV_LINE_ERR "${tag}: ${_key}=${CMAKE_MATCH_1}，期望 ${_want}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(QV_LINE_OK TRUE PARENT_SCOPE)
    set(QV_LINE_ERR "" PARENT_SCOPE)
endfunction()

# 单次尝试：启动一次程序，收集状态报告并断言。结果写入 QV_TRY_* （父作用域）。
function(qv_try attempt)
    set(_dir "${_case_dir}/attempt${attempt}")
    file(MAKE_DIRECTORY "${_dir}/Temp")
    set(_report "${_dir}/Temp/zoom-report.txt")
    foreach(_stale ${_report} "${_dir}/Temp/ctxmenu-report.txt"
                   "${_dir}/Temp/settings-combos.txt" "${_dir}/shot.png"
                   "${_dir}/quikview-test.ini")
        file(REMOVE "${_stale}")
    endforeach()

    # 独立配置 + exe 同目录 DLL 优先，避免读取真实 config.ini 或串到别的 Qt。
    set(ENV{QUIKVIEW_CONFIG_PATH} "${_dir}/quikview-test.ini")
    set(ENV{PATH} "${_exe_dir}${_path_sep}$ENV{PATH}")
    file(TO_NATIVE_PATH "${_dir}/shot.png" _shot_native)

    # Windows 上不能直接用 execute_process 拉 GUI 进程：CMake 用 SW_HIDE 启动子进程，
    # 主窗口不是活动窗口，Qt 会把 setFocus 推迟到激活时，键盘焦点用例就永远验不出来。
    # 经 PowerShell Start-Process 以正常显示状态启动即可复现真实交互环境。
    if(CMAKE_HOST_WIN32)
        set(_ps_exe "${QUIKVIEW_EXE}")
        set(_ps_image "${_image_native}")
        set(_ps_smoke "${_smoke}")
        set(_ps_shot "${_shot_native}")
        set(_ps_dir "${_dir}")
        foreach(_v _ps_exe _ps_image _ps_smoke _ps_shot _ps_dir)
            string(REPLACE "'" "''" ${_v} "${${_v}}")
        endforeach()
        # 真实光标挪到屏幕左上角：应用窗口居中，光标不会落到底岛本体上，
        # 于是 ctrlfocus 用例里 dockvis=1 只能来自键盘焦点，而不是真实 hover。
        # 沿用 HANDOFF 的冒烟前置约定（SetCursorPos 挪开鼠标）。
        string(CONCAT _ps_script
            "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Cursor]::Position = [System.Drawing.Point]::new(3,3); "
            "$p = Start-Process -FilePath '${_ps_exe}' -ArgumentList @('${_ps_image}','--smoke','${_ps_smoke}','--screenshot','${_ps_shot}','--shot-delay','${_shot_delay}') -WorkingDirectory '${_ps_dir}' -PassThru; $p.WaitForExit(); exit $p.ExitCode")
        execute_process(
            COMMAND "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe" -NoProfile -NonInteractive -Command "${_ps_script}"
            WORKING_DIRECTORY "${_dir}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _stdout
            ERROR_VARIABLE _stderr
            TIMEOUT 60)
    else()
        execute_process(
            COMMAND "${QUIKVIEW_EXE}" "${_image_native}"
                    --smoke "${_smoke}"
                    --screenshot "${_shot_native}"
                    --shot-delay "${_shot_delay}"
            WORKING_DIRECTORY "${_dir}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _stdout
            ERROR_VARIABLE _stderr
            TIMEOUT 60)
    endif()
    file(WRITE "${_dir}/process.log"
         "smoke=${_smoke}\nshot-delay=${_shot_delay}\nresult=${_result}\n"
         "stdout=${_stdout}\nstderr=${_stderr}\n")

    set(_err "")
    if(NOT "${_result}" STREQUAL "0")
        set(_err "进程退出码 ${_result}（详见 ${_dir}/process.log）")
    elseif(NOT EXISTS "${_report}")
        set(_err "没有生成状态报告 ${_report}")
    endif()

    set(_report_text "")
    if(NOT _err)
        file(STRINGS "${_report}" _lines ENCODING UTF-8)
        list(LENGTH _lines _line_count)
        if(NOT _line_count EQUAL _expected_reports)
            set(_err "状态报告行数 ${_line_count}，期望 ${_expected_reports}")
        else()
            foreach(_line IN LISTS _lines)
                set(_report_text "${_report_text}${_line}\n")
            endforeach()
        endif()
    endif()

    if(NOT _err)
        list(GET _lines 0 _line1)
        qv_check_line("${_line1}" "${_exp1}" "r1")
        if(NOT QV_LINE_OK)
            set(_err "${QV_LINE_ERR}")
        endif()
    endif()
    if(NOT _err AND _expected_reports EQUAL 2)
        list(GET _lines 1 _line2)
        qv_check_line("${_line2}" "${_exp2}" "r2")
        if(NOT QV_LINE_OK)
            set(_err "${QV_LINE_ERR}")
        endif()
    endif()

    # 菜单结构报告：证明 ctxmenu / settingscombo 确实构造并弹出了菜单。
    if(NOT _err)
        foreach(_file IN LISTS _extra_files)
            set(_path "${_dir}/${_file}")
            if(NOT EXISTS "${_path}")
                set(_err "缺少菜单结构报告 ${_path}")
                break()
            endif()
            file(SIZE "${_path}" _size)
            if(_size EQUAL 0)
                set(_err "菜单结构报告为空 ${_path}")
                break()
            endif()
        endforeach()
    endif()

    if(_err)
        set(QV_TRY_OK FALSE PARENT_SCOPE)
        set(QV_TRY_REASON "${_err}" PARENT_SCOPE)
        set(QV_TRY_REPORT "" PARENT_SCOPE)
    else()
        set(QV_TRY_OK TRUE PARENT_SCOPE)
        set(QV_TRY_REASON "" PARENT_SCOPE)
        set(QV_TRY_REPORT "${_report_text}" PARENT_SCOPE)
    endif()
    set(QV_TRY_DIR "${_dir}" PARENT_SCOPE)
endfunction()

# GUI 弹窗偶尔会因为窗口激活时序被 Qt 立刻收起；同例最多重试 2 次，
# 任一尝试完整通过即算通过，重试次数会写进证据。
set(_passed FALSE)
set(_attempts 0)
foreach(_attempt RANGE 1 3)
    set(_attempts ${_attempt})
    qv_try(${_attempt})
    if(QV_TRY_OK)
        set(_passed TRUE)
        set(_pass_dir "${QV_TRY_DIR}")
        set(_pass_report "${QV_TRY_REPORT}")
        break()
    endif()
    message(STATUS "[chrome-popup] ${QUIKVIEW_CASE} 第 ${_attempt} 次未通过：${QV_TRY_REASON}，重试")
endforeach()

if(NOT _passed)
    file(WRITE "${_case_dir}/result.txt"
         "case=${QUIKVIEW_CASE}\nstatus=FAIL\nattempts=${_attempts}\n"
         "smoke=${_smoke}\nreason=${QV_TRY_REASON}\n")
    message(FATAL_ERROR "[chrome-popup] ${QUIKVIEW_CASE} FAIL（${_attempts} 次尝试）：${QV_TRY_REASON}")
endif()

file(WRITE "${_case_dir}/result.txt"
     "case=${QUIKVIEW_CASE}\nstatus=PASS\nattempts=${_attempts}\n"
     "smoke=${_smoke}\nshot-delay=${_shot_delay}\n"
     "report=${_pass_dir}/Temp/zoom-report.txt\n"
     "expect1=${_exp1}\nexpect2=${_exp2}\n"
     "--- state report ---\n${_pass_report}")
message(STATUS "[chrome-popup] ${QUIKVIEW_CASE} PASS (attempt ${_attempts})")
message(STATUS "[chrome-popup]   ${_pass_report}")
