#include "uiicons.h"

#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <QtMath>

namespace UiIcons {
namespace {

/// 图标逻辑坐标系边长
constexpr qreal kView = 24.0;

/// 线宽（逻辑单位）
constexpr qreal kStroke = 1.7;

/** 由点集构造折线路径 */
QPainterPath polyline(const QVector<QPointF> &pts, bool closed = false)
{
    QPainterPath path;
    if (pts.isEmpty())
        return path;
    path.moveTo(pts.first());
    for (int i = 1; i < pts.size(); ++i)
        path.lineTo(pts[i]);
    if (closed)
        path.closeSubpath();
    return path;
}

/** 圆角矩形路径 */
QPainterPath roundedRect(qreal x, qreal y, qreal w, qreal h, qreal r)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(x, y, w, h), r, r);
    return path;
}

/**
 * 圆形箭头（刷新 / 重置）。
 *
 * 用圆弧 + 填充三角箭头绘制，箭头沿切线方向指向弧的终点。
 */
void drawCircularArrow(QPainter &p, const QColor &color,
                        qreal startDeg, qreal sweepDeg, bool clockwise)
{
    const QRectF rect(4.0, 4.0, 16.0, 16.0);
    const qreal radius = 8.0;
    const QPointF center(12.0, 12.0);

    // 圆弧
    QPainterPath arc;
    arc.arcMoveTo(rect, startDeg);
    arc.arcTo(rect, startDeg, sweepDeg);

    p.setPen(QPen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(arc);

    // 箭头落在弧的终点
    const qreal endDeg = startDeg + sweepDeg;
    const qreal rad = qDegreesToRadians(endDeg);
    const QPointF end(center.x() + radius * qCos(rad),
                      center.y() - radius * qSin(rad));

    // 切线方向（弧度增加的方向）
    const double dir = clockwise ? 1.0 : -1.0;
    const QPointF tangent(-qSin(rad) * dir, -qCos(rad) * dir);
    const QPointF normal(-tangent.y(), tangent.x());

    const QPointF tip = end + tangent * 2.6;
    const QPointF b1 = end + normal * 2.4 - tangent * 0.6;
    const QPointF b2 = end - normal * 2.4 - tangent * 0.6;

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(polyline({tip, b1, b2}, true));
}

/** 具体图标绘制 */
void drawIcon(QPainter &p, Icon icon, const QColor &color)
{
    p.setPen(QPen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (icon) {
    case Icon::Logo: {
        // 提手
        p.drawPath(roundedRect(8.6, 3.2, 6.8, 5.2, 1.7));
        // 箱体
        p.drawPath(roundedRect(3.0, 8.6, 18.0, 11.4, 2.2));
        // 横向束带
        p.drawLine(QPointF(3.0, 13.6), QPointF(21.0, 13.6));
        // 锁扣
        p.drawPath(roundedRect(10.5, 11.5, 3.0, 4.2, 1.0));
        break;
    }
    case Icon::Network: {
        p.drawEllipse(QRectF(3.0, 3.0, 18.0, 18.0));
        p.drawLine(QPointF(3.0, 12.0), QPointF(21.0, 12.0));
        p.drawEllipse(QRectF(9.0, 3.0, 6.0, 18.0));
        break;
    }
    case Icon::Grid: {
        p.drawPath(roundedRect(3.4, 3.4, 7.2, 7.2, 1.8));
        p.drawPath(roundedRect(13.4, 3.4, 7.2, 7.2, 1.8));
        p.drawPath(roundedRect(3.4, 13.4, 7.2, 7.2, 1.8));
        p.drawPath(roundedRect(13.4, 13.4, 7.2, 7.2, 1.8));
        break;
    }
    case Icon::Monitor: {
        p.drawPath(roundedRect(2.4, 3.8, 19.2, 13.4, 2.2));
        p.drawLine(QPointF(8.0, 20.6), QPointF(16.0, 20.6));
        p.drawLine(QPointF(12.0, 17.2), QPointF(12.0, 20.6));
        break;
    }
    case Icon::ShieldCheck: {
        QPainterPath shield;
        shield.moveTo(12.0, 2.6);
        shield.lineTo(20.0, 5.6);
        shield.lineTo(20.0, 11.4);
        shield.cubicTo(20.0, 16.2, 16.6, 19.9, 12.0, 21.4);
        shield.cubicTo(7.4, 19.9, 4.0, 16.2, 4.0, 11.4);
        shield.lineTo(4.0, 5.6);
        shield.closeSubpath();
        p.drawPath(shield);
        p.drawPath(polyline({QPointF(8.6, 11.8), QPointF(11.0, 14.3),
                              QPointF(15.6, 9.2)}));
        break;
    }
    case Icon::Info: {
        p.drawEllipse(QRectF(3.0, 3.0, 18.0, 18.0));
        p.drawLine(QPointF(12.0, 11.2), QPointF(12.0, 16.6));
        p.drawLine(QPointF(12.0, 7.4), QPointF(12.0, 8.4));
        break;
    }
    case Icon::Activity: {
        p.drawPath(polyline({QPointF(2.6, 12.0), QPointF(7.0, 12.0),
                              QPointF(9.6, 5.4), QPointF(13.0, 18.4),
                              QPointF(15.6, 12.0), QPointF(21.4, 12.0)}));
        break;
    }
    case Icon::Refresh:
        drawCircularArrow(p, color, 55.0, 280.0, true);
        break;
    case Icon::Reset:
        drawCircularArrow(p, color, 125.0, -280.0, false);
        break;
    case Icon::Clean: {
        // 大星芒
        QPainterPath star;
        star.moveTo(12.0, 2.6);
        star.lineTo(13.9, 10.1);
        star.lineTo(21.4, 12.0);
        star.lineTo(13.9, 13.9);
        star.lineTo(12.0, 21.4);
        star.lineTo(10.1, 13.9);
        star.lineTo(2.6, 12.0);
        star.lineTo(10.1, 10.1);
        star.closeSubpath();
        p.drawPath(star);
        // 小星芒
        QPainterPath spark;
        spark.moveTo(19.0, 3.2);
        spark.lineTo(19.9, 6.1);
        spark.lineTo(22.8, 7.0);
        spark.lineTo(19.9, 7.9);
        spark.lineTo(19.0, 10.8);
        spark.lineTo(18.1, 7.9);
        spark.lineTo(15.2, 7.0);
        spark.lineTo(18.1, 6.1);
        spark.closeSubpath();
        p.drawPath(spark);
        break;
    }
    case Icon::Picture: {
        p.drawPath(roundedRect(2.8, 4.2, 18.4, 15.6, 2.2));
        p.drawLine(QPointF(3.4, 16.4), QPointF(9.0, 10.8));
        p.drawLine(QPointF(9.0, 10.8), QPointF(13.4, 15.2));
        p.drawLine(QPointF(13.4, 15.2), QPointF(16.6, 12.0));
        p.drawLine(QPointF(16.6, 12.0), QPointF(20.8, 16.2));
        p.drawEllipse(QRectF(7.0, 7.0, 3.0, 3.0));
        break;
    }
    case Icon::Health: {
        // 心形轮廓
        QPainterPath heart;
        heart.moveTo(12.0, 20.2);
        heart.cubicTo(9.4, 17.4, 3.2, 13.8, 3.2, 9.0);
        heart.cubicTo(3.2, 5.8, 6.0, 3.8, 8.6, 3.8);
        heart.cubicTo(10.4, 3.8, 11.6, 4.9, 12.0, 6.0);
        heart.cubicTo(12.4, 4.9, 13.6, 3.8, 15.4, 3.8);
        heart.cubicTo(18.0, 3.8, 20.8, 5.8, 20.8, 9.0);
        heart.cubicTo(20.8, 13.8, 14.6, 17.4, 12.0, 20.2);
        p.drawPath(heart);
        // 脉搏
        p.drawPath(polyline({QPointF(6.4, 10.6), QPointF(9.2, 10.6),
                              QPointF(10.6, 8.2), QPointF(12.3, 13.2),
                              QPointF(13.8, 10.6), QPointF(17.6, 10.6)}));
        break;
    }
    case Icon::Rocket: {
        // 机身
        QPainterPath body;
        body.moveTo(12.0, 2.4);
        body.lineTo(16.2, 9.6);
        body.lineTo(16.2, 15.0);
        body.lineTo(7.8, 15.0);
        body.lineTo(7.8, 9.6);
        body.closeSubpath();
        p.drawPath(body);
        // 舷窗
        p.drawEllipse(QRectF(10.3, 7.8, 3.4, 3.4));
        // 尾翼
        p.drawPath(polyline({QPointF(7.8, 11.8), QPointF(4.4, 15.6), QPointF(7.8, 15.0)}));
        p.drawPath(polyline({QPointF(16.2, 11.8), QPointF(19.6, 15.6), QPointF(16.2, 15.0)}));
        // 尾焰
        p.drawPath(polyline({QPointF(10.4, 15.4), QPointF(12.0, 20.8), QPointF(13.6, 15.4)}));
        break;
    }
    case Icon::Disk: {
        // 盘体
        p.drawRoundedRect(QRectF(3.0, 6.0, 18.0, 12.0), 3.0, 3.0);
        // 读写指示灯
        p.setBrush(color);
        p.drawEllipse(QPointF(17.2, 14.6), 1.3, 1.3);
        p.setBrush(Qt::NoBrush);
        // 数据线
        p.drawLine(QPointF(6.8, 10.2), QPointF(13.2, 10.2));
        p.drawLine(QPointF(6.8, 13.0), QPointF(10.8, 13.0));
        break;
    }
    case Icon::DiskPulse: {
        // 盘体
        p.drawPath(roundedRect(2.6, 7.0, 18.8, 10.0, 2.4));
        // 盘内的健康脉搏
        p.drawPath(polyline({QPointF(5.4, 12.0), QPointF(8.6, 12.0),
                             QPointF(10.4, 9.6), QPointF(12.6, 14.4),
                             QPointF(14.4, 12.0), QPointF(18.6, 12.0)}));
        break;
    }
    case Icon::Camera: {
        p.drawLine(QPointF(6.0, 2.6), QPointF(6.0, 6.6));
        p.drawLine(QPointF(6.0, 2.6), QPointF(10.0, 2.6));
        p.drawLine(QPointF(18.0, 2.6), QPointF(18.0, 6.6));
        p.drawLine(QPointF(18.0, 2.6), QPointF(14.0, 2.6));
        p.drawLine(QPointF(6.0, 21.4), QPointF(6.0, 17.4));
        p.drawLine(QPointF(6.0, 21.4), QPointF(10.0, 21.4));
        p.drawLine(QPointF(18.0, 21.4), QPointF(18.0, 17.4));
        p.drawLine(QPointF(18.0, 21.4), QPointF(14.0, 21.4));
        p.drawPath(roundedRect(6.0, 6.6, 12.0, 10.8, 1.6));
        break;
    }
    case Icon::Keyboard: {
        p.drawPath(roundedRect(2.2, 6.0, 19.6, 12.0, 2.2));
        // 简化：只画一行键位 + 一条空格键，避免小尺寸下细节过密
        const qreal xs[] = {6.2, 9.8, 13.4, 17.0};
        for (qreal x : xs)
            p.drawEllipse(QRectF(x, 9.6, 1.6, 1.6));
        p.drawLine(QPointF(7.0, 14.2), QPointF(17.0, 14.2));
        break;
    }
    case Icon::Dots: {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(4.4, 10.6, 3.0, 3.0));
        p.drawEllipse(QRectF(10.5, 10.6, 3.0, 3.0));
        p.drawEllipse(QRectF(16.6, 10.6, 3.0, 3.0));
        break;
    }
    case Icon::Link: {
        p.drawPath(roundedRect(2.6, 9.0, 8.0, 6.0, 3.0));
        p.drawPath(roundedRect(13.4, 9.0, 8.0, 6.0, 3.0));
        p.drawLine(QPointF(10.6, 12.0), QPointF(13.4, 12.0));
        break;
    }
    case Icon::None:
        break;
    }
}

} // namespace

QPixmap render(Icon icon, const QColor &color, int size)
{
    if (size <= 0)
        return QPixmap();

    // 2 倍绘制再交给 Qt 缩放，高 DPI 屏下依然锐利
    const int dpr = 2;
    QPixmap pm(size * dpr, size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // 注意：pixmap 设了 devicePixelRatio 后，painter 的逻辑坐标已经是 0..size，
    // 这里只需把 24x24 的图标坐标系映射到 size 即可，不能再乘 dpr，否则图形会被裁掉。
    p.scale(size / kView, size / kView);
    drawIcon(p, icon, color);
    p.end();

    return pm;
}

QIcon make(Icon icon, const QColor &color, int size)
{
    return QIcon(render(icon, color, size));
}

QPixmap makeBadge(Icon icon, const QColor &background,
                  const QColor &foreground, int size, qreal radius)
{
    if (size <= 0)
        return QPixmap();

    const int dpr = 2;
    QPixmap pm(size * dpr, size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 圆角底板（逻辑坐标 0..size）
    QPainterPath plate;
    plate.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
    p.fillPath(plate, background);

    // 图标居中：内容区占 70%，四周留白
    const qreal inset = size * 0.15;
    const qreal content = size - inset * 2;
    p.save();
    p.translate(inset, inset);
    p.scale(content / kView, content / kView);
    drawIcon(p, icon, foreground);
    p.restore();

    p.end();
    return pm;
}

} // namespace UiIcons
