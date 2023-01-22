/**************************************************************************************************************************************************************
frame_vis.h

Copyright © 2023 Maksim Kryukov <fagear@mail.ru>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Created: 2021-01

Visualizer module.
This module draws visualized data onto a dialog.
Gets its data from [RenderPCM] module.

**************************************************************************************************************************************************************/

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
#include <QPixmap>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QThread>
#include <QtDebug>
#include <QGLFormat>
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
    void resizeEvent(QResizeEvent *event);
    void showEvent(QShowEvent *event);

private:
    Ui::frame_vis *ui;
    QTimer pos_timer;
    QElapsedTimer update_time;
    QGraphicsScene *scene;
    QGraphicsPixmapItem *pixels;
    QPixmap pix_data;
    QString win_title;
    QString set_label;
    QSettings *settings_hdl;
    bool en_pos_save;
    uint16_t prev_width;
    uint16_t prev_height;

public slots:
    void setTitle(QString);
    void drawFrame(QImage);

private slots:
    void updateWindowPosition();
    void redrawDone();

signals:
    void readyToDraw();
};

#endif // VISUALIZER_H
