/**************************************************************************************************************************************************************
fine_deint_set.h

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

Created: 2020-12

**************************************************************************************************************************************************************/

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
