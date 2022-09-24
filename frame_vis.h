#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <stdint.h>
#include <QBitmap>
#include <QColor>
#include <QDialog>
#include <QElapsedTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QThread>
#include <QtDebug>
#include <QtWidgets>
#include "config.h"
#include "ui_frame_vis.h"

#define VIS_FRAME_TXT       (QObject::tr("Кадр:"))

namespace Ui {
 class frame_vis;
}

class frame_vis : public QDialog
{
    Q_OBJECT

public:
    explicit frame_vis(QWidget *parent = 0);
    ~frame_vis();
    void setSettingsLabel(QString in_label);

private:
    void moveEvent(QMoveEvent *event);
    void resizeEvent(QMoveEvent *event);
    void showEvent(QShowEvent *event);
    void updateWindowPosition();

private:
    Ui::frame_vis *ui;
    QGraphicsScene *scene;
    QGraphicsPixmapItem *pixels;
    QPixmap pix_data;
    QImage *img_data;
    QString win_title;
    QString set_label;
    QSettings *settings_hdl;
    bool en_pos_save;
    uint16_t prev_width;
    uint16_t prev_height;

public slots:
    void setTitle(QString);
    void drawFrame(QPixmap, uint16_t);
};

#endif // VISUALIZER_H
