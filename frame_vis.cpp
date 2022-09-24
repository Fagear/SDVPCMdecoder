#include "frame_vis.h"

frame_vis::frame_vis(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::frame_vis)
{
    ui->setupUi(this);

    scene = NULL;
    pixels = NULL;
    img_data = NULL;
    win_title = VIS_FRAME_TXT+" ";
    set_label = "vis_window";
    settings_hdl = NULL;
    en_pos_save = false;
    prev_width = 640;
    prev_height = 480;

    scene = new QGraphicsScene(this);

    ui->viewport->setScene(scene);
    ui->viewport->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    //ui->viewport->setCacheMode(QGraphicsView::CacheBackground);
    //ui->viewport->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    //ui->viewport->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->viewport->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    ui->viewport->setMouseTracking(false);

    //QImage pixels(640, 480, QImage::Format_Grayscale8);
    img_data = new QImage(prev_width, prev_height, QImage::Format_RGB32);
    //img_data->fill(Qt::black);

    pix_data = QPixmap::fromImage(*img_data);
    pixels = new QGraphicsPixmapItem(pix_data);
    pixels->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);

    settings_hdl = new QSettings(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    if(settings_hdl!=NULL)
    {
        qInfo()<<"[VIS] Settings path:"<<settings_hdl->fileName();
        settings_hdl->sync();
    }

    scene->addItem(pixels);

    qInfo()<<"[VIS] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

frame_vis::~frame_vis()
{
    if(img_data!=NULL)
    {
        delete img_data;
    }

    if(settings_hdl!=NULL)
    {
        settings_hdl->sync();
        delete settings_hdl;
    }

    qInfo()<<"[VIS] Visualizer destroyed";
    delete ui;
}

void frame_vis::setSettingsLabel(QString in_label)
{
    set_label = in_label;
}

void frame_vis::moveEvent(QMoveEvent *event)
{
    updateWindowPosition();
    event->accept();
}

void frame_vis::resizeEvent(QMoveEvent *event)
{
    updateWindowPosition();
    event->accept();
}

void frame_vis::showEvent(QShowEvent *event)
{
    if(settings_hdl!=NULL)
    {
        settings_hdl->beginGroup(set_label);
        if((settings_hdl->contains("size")!=false)&&(settings_hdl->contains("position")!=false))
        {
            qInfo()<<"[VIS] Loading window position"<<set_label;
            this->setGeometry(settings_hdl->value("size").toRect());
            this->move(settings_hdl->value("position").toPoint());
        }
        settings_hdl->endGroup();
        en_pos_save = true;
    }
    event->accept();
}

void frame_vis::updateWindowPosition()
{
    if(en_pos_save!=false)
    {
        if(settings_hdl!=NULL)
        {
            //qInfo()<<"[VIS] Updating window position"<<set_label;
            settings_hdl->beginGroup(set_label);
            settings_hdl->setValue("size", this->geometry());
            settings_hdl->setValue("position", this->pos());
            settings_hdl->endGroup();
        }
    }
}

void frame_vis::setTitle(QString in_str)
{
    if(in_str.isEmpty()==false)
    {
        win_title = in_str+". "+VIS_FRAME_TXT+" ";
    }
    else
    {
        win_title = VIS_FRAME_TXT+" ";
    }
    this->setWindowTitle(win_title+QString::number(0));
}

void frame_vis::drawFrame(QPixmap in_pixmap, uint16_t in_frame_no)
{
    if((prev_width!=in_pixmap.width())||(prev_height!=in_pixmap.height()))
    {
        qInfo()<<"[VIS] Resized to"<<in_pixmap.width()<<"x"<<in_pixmap.height();
        prev_width = in_pixmap.width();
        prev_height = in_pixmap.height();
        pixels->setPixmap(pix_data);

        scene->setSceneRect(0,0,prev_width,prev_height);
        ui->viewport->setSceneRect(scene->sceneRect());
        ui->viewport->adjustSize();
        this->setMinimumSize(ui->viewport->size());
        this->setMaximumSize(ui->viewport->size());
        this->resize(ui->viewport->size());
        this->adjustSize();
    }
    pixels->setPixmap(in_pixmap);
    this->setWindowTitle(win_title+QString::number(in_frame_no));
}
