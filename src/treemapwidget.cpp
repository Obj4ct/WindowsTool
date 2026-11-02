#include "treemapwidget.h"

#include <QDir>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolTip>
#include <algorithm>
#include <limits>

namespace {

/** 方块配色：同一色系深浅变化，保证相邻方块能区分又不花哨 */
const char *const kCardColors[] = {
    "#2F6FDE", "#3C7EE8", "#4A8CFF", "#5B9AF5",
    "#3E86D8", "#2C7AB8", "#3D8FB0", "#4E9AB8",
    "#5AA6C4", "#2B6CD4", "#4C79C6", "#5C86CE"
};
constexpr int kCardColorCount =
    static_cast<int>(sizeof(kCardColors) / sizeof(kCardColors[0]));

QString formatSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(value / (1024.0 * 1024 * 1024), 0, 'f', 1);
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(value / (1024.0 * 1024), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(bytes);
}

/** squarified 布局的最差长宽比，值越小方块越接近正方形 */
qreal worstRatio(const QVector<qreal> &row, qreal side)
{
    if (row.isEmpty() || side <= 0)
        return std::numeric_limits<qreal>::max();

    qreal sum = 0;
    qreal maximum = 0;
    qreal minimum = std::numeric_limits<qreal>::max();
    for (qreal value : row) {
        sum += value;
        maximum = qMax(maximum, value);
        minimum = qMin(minimum, value);
    }
    if (sum <= 0 || minimum <= 0)
        return std::numeric_limits<qreal>::max();

    const qreal a = side * side * maximum / (sum * sum);
    const qreal b = sum * sum / (side * side * minimum);
    return qMax(a, b);
}

/**
 * squarified treemap：values 为面积（总和等于 rect 面积），按顺序切成矩形。
 */
void squarify(const QVector<qreal> &values, QRectF rect, QList<QRectF> &out)
{
    QVector<qreal> remaining = values;

    while (!remaining.isEmpty() && rect.width() > 0.5 && rect.height() > 0.5) {
        const bool fillColumn = rect.width() >= rect.height();
        const qreal side = qMin(rect.width(), rect.height());

        QVector<qreal> row;
        row.append(remaining.first());
        int next = 1;
        while (next < remaining.size()) {
            QVector<qreal> candidate = row;
            candidate.append(remaining.at(next));
            if (worstRatio(candidate, side) <= worstRatio(row, side)) {
                row = candidate;
                ++next;
            } else {
                break;
            }
        }
        remaining = remaining.mid(row.size());

        qreal rowSum = 0;
        for (qreal value : row)
            rowSum += value;
        if (rowSum <= 0)
            break;

        if (fillColumn) {
            const qreal stripWidth = rowSum / rect.height();
            qreal y = rect.top();
            for (qreal value : row) {
                const qreal height = value / stripWidth;
                out.append(QRectF(rect.left(), y, stripWidth, height));
                y += height;
            }
            rect.setLeft(rect.left() + stripWidth);
        } else {
            const qreal stripHeight = rowSum / rect.width();
            qreal x = rect.left();
            for (qreal value : row) {
                const qreal width = value / stripHeight;
                out.append(QRectF(x, rect.top(), width, stripHeight));
                x += width;
            }
            rect.setTop(rect.top() + stripHeight);
        }
    }
}

} // namespace

TreemapWidget::TreemapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TreemapWidget::setItems(const QList<Item> &items)
{
    m_items = items;
    std::sort(m_items.begin(), m_items.end(),
              [](const Item &a, const Item &b) { return a.size > b.size; });

    m_total = 0;
    for (const Item &item : m_items)
        m_total += item.size;

    m_placeholder.clear();
    m_hover = -1;
    relayout();
    update();
}

void TreemapWidget::showPlaceholder(const QString &text)
{
    m_items.clear();
    m_rects.clear();
    m_total = 0;
    m_hover = -1;
    m_placeholder = text;
    update();
}

void TreemapWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void TreemapWidget::relayout()
{
    m_rects.clear();
    if (m_items.isEmpty() || m_total <= 0)
        return;

    QRectF area = QRectF(rect()).adjusted(3, 3, -3, -3);
    if (area.width() < 8 || area.height() < 8)
        return;

    const qreal areaPx = area.width() * area.height();
    QVector<qreal> values;
    values.reserve(m_items.size());
    for (const Item &item : m_items)
        values.append(areaPx * static_cast<qreal>(item.size) / static_cast<qreal>(m_total));

    squarify(values, area, m_rects);
}

int TreemapWidget::indexAt(const QPointF &pos) const
{
    for (int i = 0; i < m_rects.size(); ++i) {
        if (m_rects.at(i).contains(pos))
            return i;
    }
    return -1;
}

void TreemapWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_items.isEmpty() || m_rects.isEmpty()) {
        painter.setPen(QColor("#93A0B4"));
        QFont hintFont = font();
        hintFont.setPixelSize(13);
        painter.setFont(hintFont);
        painter.drawText(rect(), Qt::AlignCenter,
                         m_placeholder.isEmpty() ? QStringLiteral("暂无数据")
                                                 : m_placeholder);
        return;
    }

    QFont nameFont = font();
    nameFont.setPixelSize(12);
    nameFont.setBold(true);

    QFont valueFont = font();
    valueFont.setPixelSize(11);

    for (int i = 0; i < m_rects.size(); ++i) {
        QRectF box = m_rects.at(i).adjusted(1.5, 1.5, -1.5, -1.5);
        if (box.width() < 3 || box.height() < 3)
            continue;

        const Item &item = m_items.at(i);
        QColor fill(item.isDir ? kCardColors[i % kCardColorCount] : "#8FA3BE");
        if (i == m_hover)
            fill = fill.lighter(118);

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(box, 4, 4);

        if (box.width() < 46 || box.height() < 18)
            continue;

        painter.setPen(QColor(255, 255, 255, 235));
        QRectF textRect = box.adjusted(6, 3, -6, -3);

        if (box.width() >= 76 && box.height() >= 34) {
            painter.setFont(nameFont);
            QFontMetrics nameMetrics(nameFont);
            const QString name = nameMetrics.elidedText(item.name, Qt::ElideMiddle,
                                                        static_cast<int>(textRect.width()));
            painter.drawText(QRectF(textRect.left(), textRect.top(), textRect.width(), 16),
                             Qt::AlignLeft | Qt::AlignVCenter, name);

            if (box.height() >= 54) {
                painter.setFont(valueFont);
                QFontMetrics valueMetrics(valueFont);
                const QString detail = valueMetrics.elidedText(
                    formatSize(item.size), Qt::ElideRight,
                    static_cast<int>(textRect.width()));
                painter.drawText(
                    QRectF(textRect.left(), textRect.top() + 17, textRect.width(), 14),
                    Qt::AlignLeft | Qt::AlignVCenter, detail);
            }
        } else {
            QFont smallFont = nameFont;
            smallFont.setPixelSize(10);
            painter.setFont(smallFont);
            QFontMetrics metrics(smallFont);
            const QString name = metrics.elidedText(item.name, Qt::ElideRight,
                                                    static_cast<int>(textRect.width()));
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, name);
        }
    }
}

void TreemapWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int index = indexAt(event->pos());
    if (index != m_hover) {
        m_hover = index;
        update();
    }

    if (index >= 0) {
        const Item &item = m_items.at(index);
        QString text = QStringLiteral("%1\n%2 · %3 个文件")
                           .arg(item.name, formatSize(item.size))
                           .arg(item.files);
        if (!item.path.isEmpty())
            text += QStringLiteral("\n") + QDir::toNativeSeparators(item.path);
        if (item.isDir)
            text += QStringLiteral("\n点击进入这个文件夹");
        QToolTip::showText(event->globalPos(), text, this);
        setCursor(item.isDir ? Qt::PointingHandCursor : Qt::ArrowCursor);
    } else {
        QToolTip::hideText();
        unsetCursor();
    }

    QWidget::mouseMoveEvent(event);
}

void TreemapWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = indexAt(event->pos());
        if (index >= 0) {
            const Item &item = m_items.at(index);
            if (item.isDir && !item.path.isEmpty()) {
                emit directoryActivated(item.path);
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void TreemapWidget::leaveEvent(QEvent *event)
{
    m_hover = -1;
    update();
    unsetCursor();
    QWidget::leaveEvent(event);
}
