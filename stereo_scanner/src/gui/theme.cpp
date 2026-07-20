#include "theme.h"
#include <QString>

namespace ui {

QColor withAlpha(const QString& hex, int alpha) {
    QColor c(hex);
    c.setAlpha(alpha);
    return c;
}

QString globalQss() {
    return QStringLiteral(R"(
/* ============================================================
   全局 QSS 主题 - 深色科技风
   ============================================================ */

* {
    color: #C9D1D9;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 12px;
    outline: none;
}

QWidget#MainWindow, QWidget#TopBar, QWidget#SideBar,
QWidget#MainArea, QWidget#RightPanel, QWidget#BottomBar {
    background: #11161D;
}

/* ---- 通用卡片 ---- */
QFrame#Card {
    background: #161C24;
    border: 1px solid #232A35;
    border-radius: 6px;
}

QFrame#CardHead {
    background: #161C24;
    border-bottom: 1px solid #232A35;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 4px 14px;
    min-height: 32px;
}

QLabel#CardTitle {
    color: #E6EDF3;
    font-size: 12px;
    font-weight: 600;
}

QLabel#CardStatus {
    font-family: "Consolas", "JetBrains Mono", monospace;
    font-size: 10px;
    padding: 2px 6px;
    border-radius: 2px;
}
QLabel#CardStatus[status="ok"]   { color: #3FB950; background: rgba(63,185,80,16); border: 1px solid rgba(63,185,80,48); }
QLabel#CardStatus[status="warn"] { color: #FFB454; background: rgba(255,180,84,16); border: 1px solid rgba(255,180,84,48); }
QLabel#CardStatus[status="err"]  { color: #F85149; background: rgba(248,81,73,16); border: 1px solid rgba(248,81,73,48); }

QFrame#CardBody {
    background: #161C24;
    padding: 12px 14px;
    border-bottom-left-radius: 6px;
    border-bottom-right-radius: 6px;
}

/* ---- KV 表格 ---- */
QLabel#KvLabel {
    color: #5C6873;
    font-size: 10px;
}
QLabel#KvValue {
    color: #E6EDF3;
    font-family: "Consolas", "JetBrains Mono", monospace;
    font-size: 11px;
}
QLabel#KvValue[role="accent"] { color: #00D4FF; }

/* ---- 按钮 ---- */
QPushButton {
    background: #1C2330;
    border: 1px solid #2D3744;
    border-radius: 6px;
    color: #C9D1D9;
    padding: 8px 12px;
    font-size: 12px;
    min-height: 18px;
}
QPushButton:hover {
    background: #1C2330;
    border-color: rgba(0,212,255,80);
    color: #E6EDF3;
}
QPushButton:pressed { background: #161C24; }
QPushButton:disabled { color: #3D4853; border-color: #232A35; }

QPushButton#BtnPrimary {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #00D4FF, stop:1 #3C86E5);
    border: 1px solid #00D4FF;
    color: #0B0E13;
    font-weight: 600;
}
QPushButton#BtnPrimary:hover {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #00D4FF, stop:1 #3C86E5);
    color: #0B0E13;
}

QPushButton#BtnDanger {
    border-color: rgba(248,81,73,80);
    color: #F85149;
}
QPushButton#BtnDanger:hover {
    background: rgba(248,81,73,16);
    border-color: #F85149;
    color: #F85149;
}

/* ---- 滑杆 ---- */
QSlider::groove:horizontal {
    height: 4px;
    background: #0E1318;
    border: 1px solid #232A35;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #00D4FF, stop:1 #3C86E5);
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #00D4FF;
    border: 2px solid #0B0E13;
    width: 12px;
    height: 12px;
    margin: -6px 0;
    border-radius: 8px;
}
QSlider::handle:horizontal:hover {
    background: #00D4FF;
    width: 14px;
    height: 14px;
    margin: -7px 0;
    border-radius: 9px;
}

/* ---- 切换组 ---- */
QPushButton#ToggleBtn {
    background: transparent;
    border: none;
    color: #5C6873;
    padding: 6px 8px;
    border-radius: 4px;
    font-size: 11px;
}
QPushButton#ToggleBtn:hover { color: #C9D1D9; }
QPushButton#ToggleBtn:checked {
    background: rgba(0,212,255,32);
    color: #00D4FF;
}

/* ---- 进度条 ---- */
QProgressBar {
    background: #0E1318;
    border: 1px solid #232A35;
    border-radius: 3px;
    height: 6px;
    text-align: center;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #00D4FF, stop:1 #3C86E5);
    border-radius: 3px;
}

/* ---- 工作流导航 ---- */
QLabel#WfStepActive { color: #00D4FF; }
QLabel#WfStepDone    { color: #3FB950; }
QLabel#WfStepPending { color: #5C6873; }
QLabel#WfArrow       { color: #3D4853; }

QLabel#WfNum {
    font-family: "Consolas", monospace;
    font-size: 10px;
    font-weight: 600;
    padding: 2px;
    border-radius: 10px;
    min-width: 18px;
    min-height: 18px;
    max-width: 18px;
    max-height: 18px;
    alignment: center;
}
QLabel#WfNum[state="active"] {
    background: #00D4FF;
    color: #0B0E13;
}
QLabel#WfNum[state="done"] {
    background: rgba(63,185,80,32);
    color: #3FB950;
    border: 1px solid #3FB950;
}
QLabel#WfNum[state="pending"] {
    background: #0E1318;
    color: #5C6873;
    border: 1px solid #2D3744;
}

/* ---- 顶栏 ---- */
QLabel#BrandName {
    color: #E6EDF3;
    font-size: 14px;
    font-weight: 600;
}
QLabel#BrandModel {
    color: #00D4FF;
    font-family: "Consolas", monospace;
    font-size: 10px;
}
QLabel#SysLabel {
    color: #5C6873;
    font-size: 9px;
    letter-spacing: 0.5px;
}
QLabel#SysValue {
    color: #C9D1D9;
    font-family: "Consolas", monospace;
    font-size: 11px;
}
QLabel#SysValue[state="green"] { color: #3FB950; }
QLabel#SysValue[state="amber"] { color: #FFB454; }
QLabel#SysValue[state="red"]   { color: #F85149; }

QLabel#Clock {
    color: #C9D1D9;
    font-family: "Consolas", monospace;
    font-size: 11px;
}

/* ---- 录制指示器 ---- */
QFrame#RecIndicator {
    background: #161C24;
    border: 1px solid #232A35;
    border-radius: 6px;
    padding: 4px 12px;
}
QLabel#RecLabel[role="rec"]  { color: #F85149; font-weight: 600; font-size: 11px; }
QLabel#RecLabel[role="idle"] { color: #5C6873; font-size: 11px; }

/* ---- 侧栏导航 ---- */
QPushButton#NavBtn {
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 6px;
    margin: 0 6px;
    min-width: 48px;
    min-height: 48px;
    max-width: 48px;
    max-height: 48px;
}
QPushButton#NavBtn:hover {
    background: #161C24;
}
QPushButton#NavBtn:checked {
    background: rgba(0,212,255,16);
    border-left: 3px solid #00D4FF;
    margin-left: 0;
    padding-left: 9px;
}
QLabel#NavLabel {
    color: #5C6873;
    font-size: 9px;
    alignment: center;
}
QPushButton#NavBtn:checked QLabel#NavLabel,
QPushButton#NavBtn:checked > * { color: #00D4FF; }

/* ---- 滚动条 ---- */
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #2D3744;
    border-radius: 4px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover { background: rgba(0,212,255,80); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QScrollBar:horizontal {
    background: transparent;
    height: 8px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #2D3744;
    border-radius: 4px;
    min-width: 24px;
}

/* ---- 工具提示 ---- */
QToolTip {
    background: #0B0E13;
    color: #E6EDF3;
    border: 1px solid #2D3744;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 11px;
}

/* ---- 预览卡片 ---- */
QFrame#PreviewHead {
    background: #161C24;
    border-bottom: 1px solid #232A35;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 4px 12px;
    min-height: 28px;
}
QLabel#PreviewTitle {
    color: #C9D1D9;
    font-size: 11px;
    font-weight: 500;
}
QLabel#PreviewMeta {
    color: #5C6873;
    font-family: "Consolas", monospace;
    font-size: 10px;
}
QLabel#PreviewOverlay {
    color: #E6EDF3;
    font-family: "Consolas", monospace;
    font-size: 10px;
    background: rgba(11,14,19,180);
    padding: 2px 6px;
    border-radius: 2px;
}

/* ---- HUD 标签 ---- */
QLabel#HudTag {
    background: rgba(11,14,19,180);
    border: 1px solid #232A35;
    border-radius: 4px;
    padding: 4px 8px;
    font-family: "Consolas", monospace;
    font-size: 10px;
}
QLabel#HudLabel {
    color: #5C6873;
    font-size: 9px;
}
QLabel#HudValue {
    color: #00D4FF;
    font-size: 11px;
    font-weight: 600;
}

/* ---- 扫描状态浮层 ---- */
QFrame#ScanStatusOverlay {
    background: rgba(11,14,19,220);
    border: 1px solid #2D3744;
    border-radius: 6px;
}

QLabel#SsoLabel {
    color: #5C6873;
    font-size: 11px;
}
QLabel#SsoValue {
    color: #E6EDF3;
    font-family: "Consolas", monospace;
    font-size: 11px;
}
QLabel#SsoValue[role="accent"] { color: #00D4FF; }
QLabel#SsoValue[role="warn"]   { color: #FFB454; }

/* ---- 底栏统计 ---- */
QLabel#BotStatLabel {
    color: #5C6873;
    font-size: 9px;
    letter-spacing: 0.5px;
}
QLabel#BotStatValue {
    color: #E6EDF3;
    font-family: "Consolas", monospace;
    font-size: 12px;
    font-weight: 500;
}
QLabel#BotStatValue[role="accent"] { color: #00D4FF; }

QLabel#ProgressBarLabel {
    color: #5C6873;
    font-family: "Consolas", monospace;
    font-size: 10px;
}
QLabel#ProgressBarPct {
    color: #00D4FF;
    font-family: "Consolas", monospace;
    font-size: 11px;
    font-weight: 600;
}

/* ---- 任务徽章 ---- */
QLabel#TaskBadge {
    background: #2D3744;
    border-radius: 2px;
    min-width: 8px;
    max-width: 8px;
    min-height: 24px;
    max-height: 24px;
}
QLabel#TaskBadge[state="active"] { background: #00D4FF; }
QLabel#TaskBadge[state="done"]   { background: rgba(63,185,80,80); }

/* ---- 区隔线 ---- */
QFrame[role="vline"] {
    background: #232A35;
    min-width: 1px;
    max-width: 1px;
}
QFrame[role="hline"] {
    background: #232A35;
    min-height: 1px;
    max-height: 1px;
}

QToolTip { color: #E6EDF3; }

/* ---- 单选切换组容器 ---- */
QFrame#ToggleGroup {
    background: #0E1318;
    border: 1px solid #232A35;
    border-radius: 6px;
    padding: 2px;
}

    )");
}

}  // namespace ui
