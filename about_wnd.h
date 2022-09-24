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
