/**************************************************************************************************************************************************************
about_wnd.h

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

Created: 2021-10

"About" window class.

**************************************************************************************************************************************************************/

#ifndef ABOUT_WND_H
#define ABOUT_WND_H

#include <QDialog>
#include "config.h"

namespace Ui {
class about_wnd;
}

class about_wnd : public QDialog
{
    Q_OBJECT

public:
    explicit about_wnd(QWidget *parent = 0);
    ~about_wnd();

private:
    Ui::about_wnd *ui;
};

#endif // ABOUT_WND_H
