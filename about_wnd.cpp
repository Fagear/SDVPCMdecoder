#include "about_wnd.h"
#include "ui_about_wnd.h"

about_wnd::about_wnd(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::about_wnd)
{
    ui->setupUi(this);

    unsigned vers;
    QString log_line;

    // Add decoder version.
    ui->lblDescription->setText(ui->lblDescription->text()+" v"+QString(APP_VERSION)+" ("+QString(COMPILE_DATE)+")");
    // Add Qt compile-time version.
    ui->lblQtCompiled->setText(ui->lblQtCompiled->text()+" v"+QString::fromLocal8Bit(QT_VERSION_STR));
    // Add Qt run-time version.
    ui->lblQtRunnig->setText(ui->lblQtRunnig->text()+" v"+QString::fromLocal8Bit(qVersion()));
    // Add FFMPEG avcodec version.
    vers = avcodec_version();
    log_line = " v"+QString::number(AV_VERSION_MAJOR(vers))+
            "."+QString::number(AV_VERSION_MINOR(vers))+
            "."+QString::number(AV_VERSION_MICRO(vers))+
            " ("+QString::number(vers)+")";
    ui->lblFFavcodec->setText(ui->lblFFavcodec->text()+log_line);
    // Add FFMPEG avdevice version.
    vers = avdevice_version();
    log_line = " v"+QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    ui->lblFFavdevice->setText(ui->lblFFavdevice->text()+log_line);
    // Add FFMPEG swscale version.
    vers = swscale_version();
    log_line = " v"+QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    ui->lblFFswscale->setText(ui->lblFFswscale->text()+log_line);
}

about_wnd::~about_wnd()
{
    delete ui;
}
