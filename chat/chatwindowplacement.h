#pragma once
#include <QRect>
#include <QWidget>
#include <QWindow>
#include <algorithm>

/** @brief 将聊天窗口从登录尺寸切换到指定屏幕的可用区域，尺寸使用 Qt 逻辑像素。 */
inline void placeChatWindow(QWidget& window, const QRect& available)
{
    if (!available.isValid()) return;
    // 先解除登录页的固定上限，避免提高最小尺寸时被旧上限约束。
    window.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    const bool wasVisible = window.isVisible();
    // Qt 6.5 的 Windows 后端不会随尺寸约束变化刷新原生最大化按钮，需显式更新标题栏标志。
    window.setWindowFlags(window.windowFlags() | Qt::CustomizeWindowHint | Qt::WindowMaximizeButtonHint);
    if (wasVisible && !window.isVisible()) window.show();

    const QMargins frame = window.windowHandle() ? window.windowHandle()->frameMargins() : QMargins();
    const QSize decoration(frame.left() + frame.right(), frame.top() + frame.bottom());
    const QSize clientLimit = (available.size() - decoration).expandedTo(QSize(1, 1));
    window.setMinimumSize(QSize(800, 520).boundedTo(clientLimit));
    window.resize(QSize(1050, 900).boundedTo(clientLimit));

    const QSize outer = window.size() + decoration;
    const QPoint centered = available.center() - QPoint(outer.width() / 2, outer.height() / 2);
    const int rightmost = std::max(available.left(), available.right() - outer.width() + 1);
    const int bottommost = std::max(available.top(), available.bottom() - outer.height() + 1);
    window.move(std::clamp(centered.x(), available.left(), rightmost),
        std::clamp(centered.y(), available.top(), bottommost));
}
