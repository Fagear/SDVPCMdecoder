#ifndef FINE_VIDIN_SET_H
#define FINE_VIDIN_SET_H

#include <stdint.h>
#include <QDebug>
#include <QDialog>
#include <QMessageBox>
#include <QThread>
#include "vid_preset_t.h"
#include "vin_ffmpeg.h"

namespace Ui {
class fine_vidin_set;
}

class fine_vidin_set : public QDialog
{
    Q_OBJECT

    // Dropout action list indexes for [lbxColorUse].
    enum
    {
        LIST_COLORS_ALL,
        LIST_COLOR_R,
        LIST_COLOR_G,
        LIST_COLOR_B,
    };

public:
    explicit fine_vidin_set(QWidget *parent = 0);
    ~fine_vidin_set();
    int exec();

private:
    void closeEvent(QCloseEvent *event);
    void reject();

    void blockInputs();
    void blockSave();
    void enableInputs();
    void enableSave();

private:
    Ui::fine_vidin_set *ui;
    bool no_change;
    vid_preset_t new_set;

private slots:
    void setChange();
    void usrDefaults();
    void usrRevert();
    void usrSave();
    void usrClose();

public slots:
    void newSettings(vid_preset_t in_set);
    void newDrawDeint(bool in_set);

signals:
    void requestFineCurrent();
    void setFineDefaults();
    void setFineCurrent(vid_preset_t);
    void setDrawDeint(bool);
};

#endif // FINE_VIDIN_SET_H
