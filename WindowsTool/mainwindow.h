#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    //更多 菜单下面的 关于
    void AboutUnderMore();
    //网络
    void NetWork();
    //系统优化
    void System();
    //运行bat
    void RunningBat(QString fileName,QString modelName,bool fresh);
    //修复DNS
    void fixDNS();
    //其他工具
    void OtherTools();


private:
    QFont* font = new QFont("等线", 14, 75);
    Ui::MainWindow *ui;
    void closeEvent(QCloseEvent *event) override;
};
#endif // MAINWINDOW_H
