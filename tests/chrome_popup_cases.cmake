# 弹出菜单 / 上下悬浮铬件显隐隔离 · 回归用例表
#
# 由 CMakeLists.txt（注册 CTest）与 tests/chrome_popup_isolation.cmake（执行）共同 include，
# 避免「测试名列表」和「用例定义」两处漂移。
#
# 每个用例只用 MainWindow::runSmokeCommand 已有命令驱动真实显隐状态机，
# 断言全部来自 --smoke report 落盘的 Temp/zoom-report.txt（dock / top / popup 等字段），
# 截图只作辅助取证，不参与判定。
#   EXPECT1/EXPECT2 与 report 命令一一对应；EXPECT2 为空表示该用例只有一行报告。
#   额外 FILES 是命令顺带落盘的结构报告，用来证明菜单确实被构造并弹出。

function(quikview_chrome_case name smoke expect1 expect2)
    set(QV_CASE_${name}_SMOKE "${smoke}" PARENT_SCOPE)
    set(QV_CASE_${name}_EXPECT1 "${expect1}" PARENT_SCOPE)
    set(QV_CASE_${name}_EXPECT2 "${expect2}" PARENT_SCOPE)
    set(QV_CASE_${name}_FILES "${ARGN}" PARENT_SCOPE)
endfunction()

set(QUIKVIEW_CHROME_CASES
    baseline
    canvas_context_menu
    settings_combo_popup
    topbar_hamburger_menu
    topbar_menu_close
    settings_combo_close
    dock_locked
    dock_locked_with_canvas_menu
    dock_unlock_restores
    dock_hotzone_hover
    dock_keyboard_focus
    dock_slider_drag
    filmstrip_toggle
    filmstrip_navigation
    filmstrip_context_menu
    hud_tab_toggle
    hud_esc_priority
    hud_deactivate
    hud_hist_fold_persist
)

# 合成光标固定在窗口中央（1080x815），既不在顶栏 76px 热区，也不在底部 124px 热区。
# 这样 dockvis / top 的变化只能来自被测试的那一个条件，不会被真实鼠标位置干扰。

quikview_chrome_case(baseline
    "hover:540:400,report"
    "dock=0;dockvis=0;dockop=0.00;top=0;popup=0;popuptop=0;docklock=0;toplock=0"
    "")

# 画布中央右键：菜单必须真的开着（popup=1），且不是顶栏菜单（popuptop=0）；
# 底岛与顶栏都必须保持隐藏。
quikview_chrome_case(canvas_context_menu
    "hover:540:400,ctxmenu,report"
    "popup=1;popuptop=0;dock=0;dockvis=0;dockop=0.00;top=0"
    ""
    "Temp/ctxmenu-report.txt")

# 设置页 QComboBox 下拉：与底岛、顶栏都无关的第三方弹窗。
quikview_chrome_case(settings_combo_popup
    "hover:540:400,settingscombo:0:0,report"
    "popup=1;popuptop=0;top=0;dock=0;dockvis=0;dockop=0.00"
    ""
    "Temp/settings-combos.txt")

# 顶栏三横菜单：只认顶栏自有菜单的范围判断必须继续生效（top 保持可见），
# 同时不能把底岛误唤出来。
quikview_chrome_case(topbar_hamburger_menu
    "hover:540:400,hamburger,report"
    "popup=1;popuptop=1;top=2;dock=0;dockvis=0;dockop=0.00"
    "")

# 同一次运行里先记录「弹出时」再记录「关闭后」：theme: 会走 Theme::apply 的
# 收起全部弹出层路径，等价于用户按 Esc / 点击别处关闭菜单。
quikview_chrome_case(topbar_menu_close
    "hover:540:400,hamburger,report,theme:dark,report"
    "popup=1;popuptop=1;top=2;dockvis=0;dockop=0.00"
    "popup=0;popuptop=0;top=0;dock=0;dockvis=0;dockop=0.00")

quikview_chrome_case(settings_combo_close
    "hover:540:400,settingscombo:0:0,report,theme:dark,report"
    "popup=1;popuptop=0;top=0;dockvis=0;dockop=0.00"
    "popup=0;popuptop=0;top=0;dock=0;dockvis=0;dockop=0.00"
    "Temp/settings-combos.txt")

# 底栏原有合法保持条件：锁定、解锁、底部热区、键盘焦点、滑杆拖动。
quikview_chrome_case(dock_locked
    "hover:540:400,lockdock,report"
    "docklock=1;dock=2;dockvis=1;dockop=1.00;top=0;popup=0"
    "")

quikview_chrome_case(dock_locked_with_canvas_menu
    "hover:540:400,lockdock,ctxmenu,report"
    "popup=1;popuptop=0;docklock=1;dock=2;dockvis=1;dockop=1.00;top=0"
    ""
    "Temp/ctxmenu-report.txt")

quikview_chrome_case(dock_unlock_restores
    "hover:540:400,lockdock,unlockdock,report"
    "docklock=0;dock=0;dockvis=0;dockop=0.00;popup=0"
    "")

quikview_chrome_case(dock_hotzone_hover
    "hover:540:745,report"
    "dock=2;dockvis=1;dockop=1.00;top=0;popup=0"
    "")

quikview_chrome_case(dock_keyboard_focus
    "hover:540:400,ctrlfocus,report"
    "dock=2;dockvis=1;dockop=1.00;popup=0"
    "")

quikview_chrome_case(dock_slider_drag
    "hover:540:400,ctrldragstart,ctrldragto:2,report"
    "dock=2;dockvis=1;dockop=1.00;popup=0;docklock=0"
    "")

# The standard window's bottom island is 72 logical pixels from the window edge.
quikview_chrome_case(filmstrip_toggle
    "hover:540:400,lockdock,browser,report,browser,report"
    "filmstrip=1;filmcurrent=0;filminview=1;dockbottom=72;margins=0,0,0,0;dockvis=1"
    "filmstrip=0;dockbottom=72;margins=0,0,0,0;dockvis=1")

quikview_chrome_case(filmstrip_navigation
    "hover:540:400,browser,filmright,report,filmclick,report"
    "filmstrip=1;filmcurrent=1;filminview=1;dockvis=0"
    "filmstrip=1;filmcurrent=0;filminview=1;dockvis=0")

quikview_chrome_case(filmstrip_context_menu
    "hover:540:400,browser,ctxmenu,report"
    "filmstrip=1;popup=1;dockvis=0;top=0"
    ""
    "Temp/ctxmenu-report.txt")

# ===========================================================================
# 图片信息 HUD（Tab-HUD 设计定稿 2026-11-13）：Tab 双向开关走真实路由，
# Esc 优先、失焦隐藏、直方图折叠跨切图；HUD 显隐不得误触发上下栏与托盘。
# ===========================================================================

# Tab 双向：第一次 tab 开（hud/menuhud 同步勾选），第二次 tab 关；
# 全程底岛/顶栏/缩略图托盘都保持隐藏（dockvis=0/top=0/filmstrip=0/popup=0）。
quikview_chrome_case(hud_tab_toggle
    "hover:540:400,tab,report,tab,report"
    "hud=1;menuhud=1;dockvis=0;top=0;popup=0;filmstrip=0"
    "hud=0;menuhud=0;dockvis=0;top=0;popup=0;filmstrip=0")

# Esc 优先：HUD 可见时第一次 Esc 只关 HUD 并消费该键——第二行报告存在本身就
# 证明窗口没有退出（esc_exit 默认 true，若 Esc 泄漏走退出规则进程就没了），
# 同时菜单勾选态被同步清理。
quikview_chrome_case(hud_esc_priority
    "hover:540:400,tab,report,esc,report"
    "hud=1;menuhud=1;winstate=0"
    "hud=0;menuhud=0;winstate=0")

# 失焦隐藏：应用失活立即收起 HUD 且菜单勾选清理；回前台不自动恢复（无再开命令）。
quikview_chrome_case(hud_deactivate
    "hover:540:400,tab,report,deactivate,report"
    "hud=1;menuhud=1"
    "hud=0;menuhud=0")

# 直方图折叠跨切图：折叠后翻到下一张，折叠态保持（hudfold=1→1）且 HUD 不被关掉。
quikview_chrome_case(hud_hist_fold_persist
    "hover:540:400,info,histtoggle,report,next,report"
    "hud=1;hudfold=1;menuhud=1"
    "hud=1;hudfold=1;menuhud=1")
