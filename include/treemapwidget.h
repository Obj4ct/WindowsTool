#ifndef TREEMAPWIDGET_H
#define TREEMAPWIDGET_H

#include <QList>
#include <QRectF>
#include <QString>
#include <QWidget>

/**
 * 目录占用矩形树图。
 *
 * 输入一组「子目录 + 文件合计」的面积数据，用 squarified 算法把矩形
 * 切成大小成比例的方块：面积越大，占位越醒目。点击文件夹方块会发出
 * directoryActivated()，由主窗口重新扫描该目录，实现逐层查看。
 *
 * 只读、无外部依赖，全部用 QPainter 绘制，避免为一张图引入第三方库。
 */
class TreemapWidget : public QWidget
{
    Q_OBJECT

public:
    struct Item {
        QString name;
        QString path;      ///< 文件夹的完整路径；文件合计项为空
        qint64 size = 0;
        int files = 0;
        bool isDir = false;
    };

    explicit TreemapWidget(QWidget *parent = nullptr);

    /** 设置数据（内部按大小降序保存后重排） */
    void setItems(const QList<Item> &items);

    /** 显示居中提示文字（无数据或正在统计时使用） */
    void showPlaceholder(const QString &text);

signals:
    void directoryActivated(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void relayout();
    int indexAt(const QPointF &pos) const;

    QList<Item> m_items;
    QList<QRectF> m_rects;
    QString m_placeholder;
    qint64 m_total = 0;
    int m_hover = -1;
};

#endif // TREEMAPWIDGET_H
