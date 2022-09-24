#ifndef FINE_BIN_SET_H
#define FINE_BIN_SET_H

#include <stdint.h>
#include <QDebug>
#include <QDialog>
#include <QMessageBox>
#include <QThread>
#include "bin_preset_t.h"

namespace Ui {
class fine_bin_set;
}

class fine_bin_set : public QDialog
{
    Q_OBJECT

public:
    explicit fine_bin_set(QWidget *parent = 0);
    ~fine_bin_set();
    int exec();

private:
    void closeEvent(QCloseEvent *event);
    void reject();

    void blockInputs();
    void blockSave();
    void enableInputs();
    void enableSave();

private:
    Ui::fine_bin_set *ui;
    bool no_change;
    bin_preset_t new_set;

private slots:
    void setChange();
    void usrDefaults();
    void usrRevert();
    void usrSave();
    void usrClose();

public slots:
    void newSettings(bin_preset_t in_set);

signals:
    void requestFineCurrent();
    void setFineDefaults();
    void setFineCurrent(bin_preset_t);
};

#endif // FINE_BIN_SET_H
