/**************************************************************************************************************************************************************
fine_bin_set.h

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

#ifndef FINE_BIN_SET_H
#define FINE_BIN_SET_H

#include <stdint.h>
#include <QDebug>
#include <QDialog>
#include <QMessageBox>
#include <QThread>
#include "binarizer.h"

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
