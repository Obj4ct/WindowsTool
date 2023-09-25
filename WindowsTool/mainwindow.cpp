#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QLabel>
#include <QFont>
#include <QDialog>
#include <QPushButton>
#include <QProcess>
#include <QMessageBox>
#include <QtTest/QTest>
#include <Windows.h>
#include <QString>
#include <filesystem>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //qDebug()<<QCoreApplication::applicationDirPath();
    setWindowIcon(QIcon(":/ico/toolBox.png"));
    ui->stackedWidget->setCurrentIndex(0);
    AboutUnderMore();
    NetWork();
    System();
    OtherTools();
}
void MainWindow::AboutUnderMore()
{
    connect(ui->actionAbout, &QAction::triggered, [=]() {
        //模态对话框
        QDialog qd(this);
        qd.setWindowTitle("关于");
        qd.setWindowIcon(QIcon(":/ico/cool.png"));
        qd.setMinimumSize(350,200);
        qd.setMaximumSize(350,200);
        QLabel* ql1 = new QLabel(&qd);
        ql1->setText("开发人员:我真的不会写代码");
        ql1->move(5, 20);
        ql1->setFont(*font);
        ql1->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QLabel* ql2 = new QLabel(&qd);
        ql2->setText("邮箱:ztheng010726@gmail.com");
        ql2->move(5, 60);
        ql2->setFont(*font);
        ql2->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QLabel* ql3 = new QLabel(&qd);
        ql3->setText("QQ:2321852318");
        ql3->move(5, 100);
        ql3->setFont(*font);
        ql3->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QLabel* ql4 = new QLabel(&qd);
        ql4->setText("编程语言：C++、QT、bat脚本");
        ql4->move(5, 140);
        ql4->setFont(*font);
        ql4->setTextInteractionFlags(Qt::TextSelectableByMouse);
        qd.exec();

    });
}
//网络模块
void MainWindow::NetWork()
{
    //自动检测
    connect(ui->btn_autoCheck,&QPushButton::clicked,[=](){
        //是否可以ping通
loop:
        //fixDNSWithWLAN();
        ui->textBrowser->clear();
        ui->textBrowser->setText(QStringLiteral("请等待,正在检测网络,电脑可能会出现短暂卡顿，此时请勿进行任何操作....."));
        QTest::qWait(2000);
        ui->textBrowser->update();
        QProcess myProcess(this);
        QString program = "C:/Windows/System32/cmd.exe";
        QStringList arguments;
        arguments <<"/c" <<"ping www.baidu.com";
        myProcess.start(program,arguments);

        while (myProcess.waitForFinished(100) == false) {
            QByteArray qByteRead = myProcess.readAllStandardOutput();
            if (!qByteRead.isEmpty()) {
                ui->textBrowser->append(QString::fromLocal8Bit(qByteRead));
                repaint();
            }
        }
        QByteArray qByteRead = myProcess.readAllStandardOutput();
        ui->textBrowser->append(QString::fromLocal8Bit(qByteRead));
        QString str=QString::fromLocal8Bit(qByteRead);
        if(str.contains("请求超时",Qt::CaseSensitive))
        {
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("网站请求超时，请检查你的网络是否连接！并确保在电脑设置中关闭了代理");
        }
        else if(str.contains("找不到主机",Qt::CaseSensitive)){
            ui->textBrowser->append("ping错误，正在尝试修复网络配置");
            ui->textBrowser->append("-------------------");
            QTest::qWait(1000);
            ui->textBrowser->append("-------------------");
            QTest::qWait(1000);
            ui->textBrowser->append("-------------------");
            QTest::qWait(1000);
            ui->textBrowser->append("-------------------");
            QTest::qWait(2000);
            //检测DNS
            RunningBat("dnsCheck.bat","dns",false);

        }
        else {
            ui->textBrowser->append("第一项检测成功，正在进行下一步操作..");
            QTest::qWait(2000);
            //检测DNS
            RunningBat("dnsCheck.bat","dns",false);
            QTest::qWait(1000);
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            QTest::qWait(1000);
            ui->textBrowser->append("自动检测完成");

        }

    });
    //查看网络信息
    connect(ui->btn_showNetWork, &QPushButton::clicked, [=]() {
        ui->textBrowser->clear();
        ui->textBrowser->update();
        QProcess p(0);
        p.start("ipconfig");
        p.waitForStarted();
        p.waitForFinished();
        QString temp = QString::fromLocal8Bit(p.readAllStandardOutput()); //程序输出信息
        ui->textBrowser->setPlainText(temp);
        ui->textBrowser->moveCursor(QTextCursor::End);

    });
    //刷新DNS服务器
    connect(ui->btn_refreshDNS, &QPushButton::clicked, [=]() {
        //QApplication::setQuitOnLastWindowClosed(false);
        QPushButton* okBtn = new QPushButton("确定");
        QPushButton* cancelBtn = new QPushButton("取消");
        QMessageBox* myBox = new QMessageBox;
        QString str = "这个操作可以修复网络配置，确保网络配置正常工作。点击确定开始执行。";

        myBox->setIcon(QMessageBox::Warning);
        myBox->setWindowTitle("提示");
        myBox->setWindowIcon(QIcon(":/ico/attention.png"));
        myBox->setText(str);
        myBox->addButton(okBtn, QMessageBox::AcceptRole);
        myBox->addButton(cancelBtn, QMessageBox::RejectRole);
        myBox->show();
        myBox->exec();//阻塞等待用户输入

        if (myBox->clickedButton() == okBtn)
        {
            ui->textBrowser->clear();
            ui->textBrowser->setText(QStringLiteral("请等待"));
            QTest::qWait(3000);
            ui->textBrowser->update();
            fixDNS();
        }

    });
    //刷新winsock
    connect(ui->btn_resetWinSock, &QPushButton::clicked, [=]() {

        QPushButton* okBtn = new QPushButton("确定");
        QPushButton* cancelBtn = new QPushButton("取消");
        QMessageBox* myBox = new QMessageBox;
        QString str = "这个命令可以重新初始化网络环境，以解决由于软件冲突、病毒原因造成的参数错误问题。但是会导致部分电脑网速变慢，具体情况取决于电脑网络，除非别无选择，否则请谨慎操作！操作完成之后请重启电脑，然后在本软件选择“刷新网络配置”，使用之后如果出现网速变慢等问题，请联系开发人员";
        myBox->setIcon(QMessageBox::Warning);
        myBox->setWindowTitle("警告！");
        myBox->setWindowIcon(QIcon(":/ico/scared.png"));
        myBox->setText(str);
        myBox->addButton(okBtn, QMessageBox::AcceptRole);
        myBox->addButton(cancelBtn, QMessageBox::RejectRole);
        myBox->show();
        myBox->exec();//阻塞等待用户输入

        if (myBox->clickedButton() == okBtn)
        {
            RunningBat("winsockReset.bat",nullptr,true);


        }
    });
    //网络测试
    connect(ui->btn_netWorkTest,&QPushButton::clicked,[=](){
        ui->textBrowser->clear();
        ui->textBrowser->setText(QStringLiteral("请等待"));
        ui->textBrowser->update();
        QProcess myProcess(this);
        QString program = "C:/Windows/System32/cmd.exe";
        QStringList arguments;
        arguments <<"/c" <<"ping www.baidu.com";
        myProcess.start(program,arguments);

        while (myProcess.waitForFinished(100) == false) {
            QByteArray qByteRead = myProcess.readAllStandardOutput();
            if (!qByteRead.isEmpty()) {
                ui->textBrowser->append(QString::fromLocal8Bit(qByteRead));
                repaint();
            }
        }
        QByteArray qByteRead = myProcess.readAllStandardOutput();
        ui->textBrowser->append(QString::fromLocal8Bit(qByteRead));
        QString str=QString::fromLocal8Bit(qByteRead);
        if(str.contains("请求超时",Qt::CaseSensitive))
        {

            QMessageBox UnSuccess(QMessageBox::Critical,"连接错误","网络连接出现问题,请检查是否连接网络(建议多检测几次)。如果已经连接，建议使用自动检测来检查问题");
            //success.setParent(this);
            UnSuccess.setWindowIcon(QIcon(":/ico/scared.png"));
            UnSuccess.exec();
            // QMessageBox::critical(this,"连接错误","网络连接出现问题,请检查是否连接网络(建议多检测几次)。如果已经连接，建议使用自动检测来检查问题",QMessageBox::Close);


        }
        else
        {

            QMessageBox success(QMessageBox::Information,"成功","你可以正常上网！");
            //success.setParent(this);
            success.setWindowIcon(QIcon(":/ico/kiss.png"));
            success.exec();

            //QMessageBox::information(this,"成功!","你可以正常上网！",QMessageBox::Ok);
        }
    });
    //页面切换
    connect(ui->btn_nextPage,&QPushButton::clicked,[=](){
        ui->stackedWidget->setCurrentIndex(1);
    });
}


//系统优化
void MainWindow::System()
{
    //清理系统垃圾
    connect(ui->btn_cleaner,&QPushButton::clicked,[=](){
        ui->textBrowser->clear();
        ui->textBrowser->update();
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("此操作不会清理重要文件，请放心操作！");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("之后后会弹出一个窗口，请点击 “是” ，清理时长取决于电脑垃圾数量，完成后会显示 “清理系统垃圾完成！” 请耐心等待.. ");
        QTest::qWait(2000);
        ui->textBrowser->append("-------------------");
        QTest::qWait(2000);
        ui->textBrowser->append("-------------------");
        QTest::qWait(2000);
        ui->textBrowser->append("-------------------");
        QTest::qWait(2000);
        ui->textBrowser->append("-------------------");
        QTest::qWait(2000);
        RunningBat("cleaner.bat",nullptr,false);


    });
    //修复桌面图标变白
    connect(ui->btn_fixWhite,&QPushButton::clicked,[=](){
        RunningBat("fixWhite.bat",nullptr,true);
    });
    //截图工具
    connect(ui->btn_prt,&QPushButton::clicked,[=](){
        QPushButton* okBtn = new QPushButton("知道了");
        QPushButton* cancelBtn = new QPushButton("你管得着吗？");
        QMessageBox* myBox = new QMessageBox;
        QString str = "别傻傻地登录QQ/微信进行截图啦，你不觉得太麻烦了？按下win(Alt左边那个和开始菜单一样的图标)+shift+s 就可以快速截图了(只支持win10和win10以上系统)，然后直接ctrl+v粘贴就好";
        myBox->setIcon(QMessageBox::Question);
        myBox->setWindowTitle("别开玩笑");
        myBox->setText(str);
        myBox->setWindowIcon(QIcon(":/ico/what.png"));
        myBox->addButton(okBtn, QMessageBox::AcceptRole);
        myBox->addButton(cancelBtn, QMessageBox::RejectRole);
        myBox->show();
        myBox->exec();//阻塞等待用户输入

        if (myBox->clickedButton() == okBtn)
        {
            QMessageBox ok(QMessageBox::Information,"开发者的话","真是个提高效率的好方法");
            //success.setParent(this);
            ok.setWindowIcon(QIcon(":/ico/kiss.png"));
            ok.exec();
            //QMessageBox::information(this,"开发者的话","真是个提高效率的好方法");
            ui->textBrowser->clear();
            ui->textBrowser->update();
            ui->textBrowser->append("按下win(Alt左边那个和开始菜单一样的图标)+shift+s 就可以快速截图了(只支持win10和win10以上系统)，然后直接ctrl+v粘贴就好");
            return;
        }
        if (myBox->clickedButton() == cancelBtn)
        {
            QMessageBox no(QMessageBox::Information,"开发者的话","555，你开心就好了");
            //success.setParent(this);
            no.setWindowIcon(QIcon(":/ico/cry.png"));
            no.exec();
            //QMessageBox::information(this,"开发者的话","555，你开心就好了");
            return;
        }
    });
    //切换页面
    connect(ui->btn_prePage,&QPushButton::clicked,[=](){
        ui->stackedWidget->setCurrentIndex(0);
    });
}

void MainWindow::RunningBat(QString fileName,QString modelName,bool fresh)
{


    QString filename =fileName;
    QString command=QCoreApplication::applicationDirPath()+"/bat/"+fileName;

    QFile *file=new QFile(command);
    if(!file->exists())
    {
        ui->textBrowser->setText(QStringLiteral("找不到指定文件!"));
        //ui->textBrowser->setText(QStringLiteral("操作执行失败！"));
        return;
    }
    if(fresh==true)
    {
        //重置textBrowser
        ui->textBrowser->clear();
        ui->textBrowser->setText(QStringLiteral("请等待"));
        QTest::qWait(3000);
        ui->textBrowser->update();
    }

    //cmd
    QProcess myProcess(this);
    QString program = "C:/Windows/System32/cmd.exe";
    QStringList arguments;
    arguments <<"/c" <<command;
    myProcess.start(program,arguments);
    while (myProcess.waitForFinished(100) == false) {
        QByteArray qByteRead = myProcess.readAllStandardOutput();
        if (!qByteRead.isEmpty()) {
            ui->textBrowser->append(QString::fromLocal8Bit(qByteRead));
            repaint();

        }
    }
    QByteArray qByteRead = myProcess.readAllStandardOutput();
    ui->textBrowser->append(QString::fromLocal8Bit(qByteRead));
    QString str=QString::fromLocal8Bit(qByteRead);

    QString textBrowerStr=ui->textBrowser->toPlainText();

    if(modelName=="dns")
    {

        if(textBrowerStr.contains("服务器:  UnKnown",Qt::CaseSensitive))
        {
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("网络配置出现问题了(无法找到DNS服务器)");
            QTest::qWait(1000);
            fixDNS();
            return;
        }
        else{
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("-------------------");
            ui->textBrowser->append("网络连接正常");
            return;
        }
        return;
    }
}

void MainWindow::fixDNS()
{
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("修复网络配置中..");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(1500);
    RunningBat("fixDNS.bat",nullptr,false);
    QTest::qWait(3000);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("网络配置修复完成，正在检测是否可以上网...");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(500);
    ui->textBrowser->append("-------------------");
    QTest::qWait(2500);
    QPushButton* okBtn = new QPushButton("可以");
    QPushButton* cancelBtn = new QPushButton("不可以"); QMessageBox* myBox = new QMessageBox; QString str = "是否可以上网？";
    myBox->setIcon(QMessageBox::Question);
    myBox->setWindowTitle("疑问");
    myBox->setText(str);
    myBox->setWindowIcon(QIcon(":/ico/question.png"));
    myBox->addButton(okBtn, QMessageBox::AcceptRole);
    myBox->addButton(cancelBtn, QMessageBox::RejectRole);
    myBox->show();
    myBox->exec();//阻塞等待用户输入

    if (myBox->clickedButton() == okBtn)
    {
        QMessageBox ok(QMessageBox::Information,"开发者的话","好的,这对你来说是一个好软件");
        //success.setParent(this);
        ok.setWindowIcon(QIcon(":/ico/kiss.png"));
        ok.exec();
        //QMessageBox::information(this,"开发者的话","好的,这对你来说是一个好软件");
        ui->textBrowser->append("成功啦！感谢使用本软件.");
        return;
    }
    if (myBox->clickedButton() == cancelBtn)
    {
        QMessageBox no(QMessageBox::Information,"开发者的话","软件为第一个版本，难免会有问题，请点击 更多-关于 来联系作者");
        //success.setParent(this);
        no.setWindowIcon(QIcon(":/ico/cry.png"));
        no.exec();
        //QMessageBox::information(this,"开发者的不好意思","软件为第一个版本，难免会有问题，请点击 更多-关于 来联系作者");
        ui->textBrowser->append("失败了!请联系开发者...555(哭了)");
        return;
    }

}

void MainWindow::OtherTools()
{
    //快捷键
    connect(ui->btn_key,&QPushButton::clicked,[=](){
        ui->textBrowser->clear();
        ui->textBrowser->update();
        ui->textBrowser->append("ctrl+C/V:这么简单你应该不会不知道");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("win(Alt左边那个和开始菜单一样的图标)+shift+s:非常强大的快速截图工具，支持多方式截图，只支持win10和win10以上系统");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("win+ctrl+shift+B:刷新显卡驱动，如果你玩游戏遇到游戏声音还在但是画面卡了，就可以用这个刷新显卡驱动，只支持win10和win10以上系统");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("win+V:打开剪贴板历史记录，选中内容之后直接ctrl+v，只支持win10和win10以上系统");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("win+D:显示桌面");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("win+G:打开游戏模式，只支持win10和win10以上系统");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("win+L:锁屏");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("ctrl+f:搜索当前页面内容");
        ui->textBrowser->append("                   ");
        ui->textBrowser->append("ctrl+s:虽然都知道是“保存”，但是希望在工作的时候经常使用，为了避免未知的情况，造成损失");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("-------------------");
        ui->textBrowser->append("更多内容请点击下方链接");
        ui->textBrowser->setOpenExternalLinks(true);
        ui->textBrowser->append("<a href = \"https://support.microsoft.com/zh-cn/windows/windows-%E7%9A%84%E9%94%AE%E7%9B%98%E5%BF%AB%E6%8D%B7%E6%96%B9%E5%BC%8F-dcc61a57-8ff0-cffe-9796-cb9706c75eec\">快捷键大全</a>");
    });
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    QMessageBox::StandardButton button;

    button=QMessageBox::question(this,tr("退出程序"),QString(tr("确认退出程序")),QMessageBox::Yes|QMessageBox::No);

    if(button==QMessageBox::No)

    {

        event->ignore(); // 忽略退出信号，程序继续进行

    }

    else if(button==QMessageBox::Yes)

    {

        event->accept(); // 接受退出信号，程序退出

    }



    //TODO: 在退出窗口之前，实现希望做的操作


}

MainWindow::~MainWindow()
{
    delete ui;
}

