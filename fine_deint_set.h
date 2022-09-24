#ifndef FINE_DEINT_SET_H
#define FINE_DEINT_SET_H

#include <stdint.h>
#include <QDebug>
#include <QDialog>
#include <QMessageBox>
#include <QThread>

namespace Ui {
class fine_deint_set;
}

class fine_deint_set : public QDialog
{
    Q_OBJECT

public:
    explicit fine_deint_set(QWidget *parent = 0);
    ~fine_deint_set();
    int exec();

private:
    void closeEvent(QCloseEvent *event);
    void reject();

    void blockInputs();
    void blockSave();
    void enableSave();

private:
    Ui::fine_deint_set *ui;
    bool no_change;

private slots:
    void setChange();
    void usrDefaults();
    void usrRevert();
    void usrSave();
    void usrClose();

public slots:
    void newMaxUnchecked14(uint8_t in_set);
    void newMaxUnchecked16(uint8_t in_set);
    void newUseECC(bool in_set);
    void newInsertLine(bool in_set);
    void newMaskSeams(bool in_set);
    void newBrokeMask(uint8_t in_set);

signals:
    void requestFineCurrent();
    void setFineDefaults();
    void setMaxUnchecked14(uint8_t);
    void setMaxUnchecked16(uint8_t);
    void setUseECC(bool);
    void setInsertLine(bool);
    void setMaskSeams(bool);
    void setBrokeMask(uint8_t);

};

#endif // FINE_DEINT_SET_H
