#ifndef UIICONS_H
#define UIICONS_H

#include <QColor>
#include <QIcon>
#include <QPixmap>

/**
 * 轻量矢量图标工厂。
 *
 * 用 QPainter 直接绘制单色线性图标，而不是为每个按钮准备多套 PNG：
 *  - 任意尺寸都清晰（缩放不失真）
 *  - 任意颜色都可用（跟随主题切换）
 *  - 不增加资源体积
 *
 * 所有图标都在 24x24 的逻辑坐标系内绘制，绘制时按目标尺寸整体缩放。
 */
namespace UiIcons {

enum class Icon {
    None,
    Logo,        ///< 工具箱（应用标识）
    Network,     ///< 网络（地球）
    Grid,        ///< 工具（宫格）
    Monitor,     ///< 系统（显示器）
    ShieldCheck, ///< 自动检测（盾牌 + 对勾）
    Info,        ///< 查看信息
    Activity,    ///< 连通性测试（心跳）
    Refresh,     ///< 刷新（环形箭头）
    Reset,       ///< 重置（反向环形箭头）
    Clean,       ///< 清理（星芒）
    Picture,     ///< 桌面图标（图片）
    Health,      ///< 电脑体检（心形 + 脉搏）
    Rocket,      ///< 开机启动（火箭）
    Disk,        ///< 大文件扫描（磁盘）
    Camera,      ///< 截图（裁切框）
    Keyboard,    ///< 快捷键
    Dots,        ///< 更多（三点）
    Link         ///< 外部链接
};

/** 渲染为指定尺寸的位图（2 倍绘制，适配高 DPI 屏） */
QPixmap render(Icon icon, const QColor &color, int size = 24);

/** 渲染为 QIcon，可直接交给 QPushButton::setIcon() */
QIcon make(Icon icon, const QColor &color, int size = 24);

/**
 * 渲染一个圆角方形徽章：底色 + 居中图标。
 * 用于标题栏的品牌标识，比裸图标更有质感。
 */
QPixmap makeBadge(Icon icon, const QColor &background,
                  const QColor &foreground, int size = 38, qreal radius = 10);

} // namespace UiIcons

#endif // UIICONS_H
