#pragma once
#include <QString>
#include <QSize>
#include <QColor>

namespace ui {

// ============================================================================
// 设计令牌（与 HTML 设计稿一一对应的具体色值）
// ============================================================================
namespace color {
    // 背景层级
    constexpr auto BG_DEEP    = "#0B0E13";
    constexpr auto BG_BASE    = "#11161D";
    constexpr auto BG_CARD    = "#161C24";
    constexpr auto BG_CARD_HI = "#1C2330";
    constexpr auto BG_INSET   = "#0E1318";

    // 边框
    constexpr auto BD_BASE   = "#232A35";
    constexpr auto BD_HI     = "#2D3744";
    constexpr auto BD_ACCENT = "#00D4FF";

    // 文字
    constexpr auto TXT_H1   = "#E6EDF3";
    constexpr auto TXT_H2   = "#C9D1D9";
    constexpr auto TXT_BODY = "#9BA5B1";
    constexpr auto TXT_DIM  = "#5C6873";
    constexpr auto TXT_MUTE = "#3D4853";

    // 强调色
    constexpr auto CYAN   = "#00D4FF";
    constexpr auto BLUE   = "#3C86E5";
    constexpr auto GREEN  = "#3FB950";
    constexpr auto AMBER  = "#FFB454";
    constexpr auto RED    = "#F85149";
    constexpr auto PURPLE = "#A371F7";
}  // namespace color

// 间距系统（8 的倍数）
namespace sp {
    constexpr int S1 = 4;
    constexpr int S2 = 8;
    constexpr int S3 = 12;
    constexpr int S4 = 16;
    constexpr int S5 = 24;
    constexpr int S6 = 32;
    constexpr int S7 = 48;
    constexpr int S8 = 64;
}  // namespace sp

// 字号
namespace fs {
    constexpr int H1   = 14;  // 主标题
    constexpr int H2   = 12;  // 副标题/卡片标题
    constexpr int BODY = 11;  // 正文
    constexpr int SMALL = 10; // 辅助
    constexpr int TINY  = 9;  // 弱化
}  // namespace fs

// 圆角
namespace rad {
    constexpr int R1 = 2;
    constexpr int R2 = 4;
    constexpr int R3 = 6;
    constexpr int R4 = 8;
}  // namespace rad

// 工具方法
QColor withAlpha(const QString& hex, int alpha);  // hex + alpha -> rgba string

// 全局 QSS 主题字符串
QString globalQss();

}  // namespace ui
