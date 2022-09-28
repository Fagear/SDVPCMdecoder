#ifndef CAPT_SEL_H
#define CAPT_SEL_H

#include <QDebug>
#include <QDialog>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QListWidgetItem>
#include <QThread>
#include <QtConcurrent>
#include "config.h"

#define VIP_WIN_GDI_NAME    "Windows GDI screen capture"
#define VIP_WIN_GDI_CLASS   "gdigrab"
#define VIP_WIN_GDI_CAP     "desktop"
#define VIP_WIN_DSHOW_NAME  "Windows DirectShow screen capture"
#define VIP_WIN_DSHOW_CLASS "dshow"
#define VIP_WIN_DSHOW_CAP   "screen-capture-recorder"
#define VIP_LIN_X11_CLASS   "x11grab"
#define VIP_MAC_AVF_CLASS   "avfoundation"

namespace Ui {
class capt_sel;
}

class VCapDevice
{
public:
    std::string dev_name;
    std::string dev_class;
    std::string dev_path;

public:
    VCapDevice();
    VCapDevice(const VCapDevice &in_object);
    VCapDevice& operator= (const VCapDevice &in_object);
    void clear();
};

class capt_sel : public QDialog
{
    Q_OBJECT

public:
    explicit capt_sel(QWidget *parent = 0);
    ~capt_sel();
    int exec();

private:
    void getVideoCaptureList();

private:
    Ui::capt_sel *ui;
    QGraphicsScene *scene;

private slots:
    void setChange();
    void usrRefresh();
    void usrSave();
    void usrClose();
    void selectDevice(int);
    void refillDevList(QVector<VCapDevice>);

signals:
    void newDeviceList(QVector<VCapDevice>);
};

#endif // CAPT_SEL_H
