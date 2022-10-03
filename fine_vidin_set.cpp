#include "fine_vidin_set.h"
#include "ui_fine_vidin_set.h"

fine_vidin_set::fine_vidin_set(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::fine_vidin_set)
{
    ui->setupUi(this);

    connect(ui->spbCropLeft, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbCropRight, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbCropTop, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbCropBottom, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->lbxColorUse, SIGNAL(currentIndexChanged(int)), this, SLOT(setChange()));
    connect(ui->cbxDrawDeint, SIGNAL(toggled(bool)), this, SLOT(setChange()));

    connect(ui->btnClose, SIGNAL(clicked(bool)), this, SLOT(usrClose()));
    connect(ui->btnRevert, SIGNAL(clicked(bool)), this, SLOT(usrRevert()));
    connect(ui->btnDefaults, SIGNAL(clicked(bool)), this, SLOT(usrDefaults()));
    connect(ui->btnSave, SIGNAL(clicked(bool)), this, SLOT(usrSave()));

    no_change = true;
    blockInputs();
    blockSave();

    qInfo()<<"[FVIN] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

fine_vidin_set::~fine_vidin_set()
{
    qInfo()<<"[FVIN] Dialog destroyed";
    delete ui;
}

//------------------------ Dialog about to appear.
int fine_vidin_set::exec()
{
    // Request settings to populate in dialog.
    emit requestFineCurrent();
    // Open the dialog.
    return QDialog::exec();
}

//------------------------ Dialog is about to close.
void fine_vidin_set::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    // Check if any setting were changed.
    if(no_change==false)
    {
        // Show a dialog for saving confirmation.
        QMessageBox usrConfirm(this);
        usrConfirm.setWindowTitle(tr("Сохранение настроек"));
        usrConfirm.setText(tr("Настройки не были сохранены"));
        usrConfirm.setInformativeText(tr("Сохранить изменения перед закрытием диалога?"));
        usrConfirm.setIcon(QMessageBox::Warning);
        usrConfirm.setStandardButtons(QMessageBox::Discard|QMessageBox::Save);
        usrConfirm.setDefaultButton(QMessageBox::Save);
        if(usrConfirm.exec()==QMessageBox::Save)
        {
            // User confirmed saving.
            usrSave();
        }
    }
}

//------------------------ Deny hiding dialog by Esc key, do normal close.
void fine_vidin_set::reject()
{
    this->close();
}

//------------------------ Block all user input fields.
void fine_vidin_set::blockInputs()
{
    ui->spbCropLeft->setEnabled(false);
    ui->spbCropRight->setEnabled(false);
    ui->spbCropTop->setEnabled(false);
    ui->spbCropBottom->setEnabled(false);

    ui->lbxColorUse->setEnabled(false);
    ui->cbxDrawDeint->setEnabled(false);
}

//------------------------ Block Save and Revert buttons.
void fine_vidin_set::blockSave()
{
    ui->btnRevert->setEnabled(false);
    ui->btnSave->setEnabled(false);
    no_change = true;

    ui->btnClose->setFocus();
}

//------------------------ Unblock all user input fields.
void fine_vidin_set::enableInputs()
{
    ui->spbCropLeft->setEnabled(true);
    ui->spbCropRight->setEnabled(true);
    ui->spbCropTop->setEnabled(true);
    ui->spbCropBottom->setEnabled(true);

    ui->lbxColorUse->setEnabled(true);
    ui->cbxDrawDeint->setEnabled(true);
}

//------------------------ Unblock Save and Revert buttons.
void fine_vidin_set::enableSave()
{
    ui->btnRevert->setEnabled(true);
    if(ui->btnSave->isEnabled()==false)
    {
        ui->btnSave->setEnabled(true);
    }
}

//------------------------ React to any change of settings.
void fine_vidin_set::setChange()
{
    no_change = false;
    enableSave();
}

//------------------------ Request reset to defaults.
void fine_vidin_set::usrDefaults()
{
    // Show a dialog for reset confirmation.
    QMessageBox usrConfirm(this);
    usrConfirm.setWindowTitle(tr("Сброс настроек"));
    usrConfirm.setText(tr("Сброс настроек видео-декодера"));
    usrConfirm.setInformativeText(tr("Вернуть настройки к состоянию по умолчанию?"));
    usrConfirm.setIcon(QMessageBox::Question);
    usrConfirm.setStandardButtons(QMessageBox::Cancel|QMessageBox::Ok);
    usrConfirm.setDefaultButton(QMessageBox::Cancel);
    if(usrConfirm.exec()==QMessageBox::Ok)
    {
        // User confirmed settings reset.
        // Block all inputs.
        blockInputs();
        blockSave();
        // Request reset to defaults and report back.
        emit setFineDefaults();
    }
}

//------------------------ Request last saved settings.
void fine_vidin_set::usrRevert()
{
    // Block all inputs.
    blockInputs();
    blockSave();
    // Request current settings.
    emit requestFineCurrent();
}

//------------------------ Saving settings (reporting inputs).
void fine_vidin_set::usrSave()
{
    // Read all user inputs.
    new_set.crop_left = (uint16_t)(ui->spbCropLeft->value());
    new_set.crop_right = (uint16_t)(ui->spbCropRight->value());
    new_set.crop_top = (uint16_t)(ui->spbCropTop->value());
    new_set.crop_bottom = (uint16_t)(ui->spbCropBottom->value());

    if(ui->lbxColorUse->currentIndex()==LIST_COLOR_R)
    {
        new_set.colors = vid_preset_t::COLOR_R;
    }
    else if(ui->lbxColorUse->currentIndex()==LIST_COLOR_G)
    {
        new_set.colors = vid_preset_t::COLOR_G;
    }
    else if(ui->lbxColorUse->currentIndex()==LIST_COLOR_B)
    {
        new_set.colors = vid_preset_t::COLOR_B;
    }
    else
    {
        new_set.colors = vid_preset_t::COLOR_BW;
    }

    // Block all inputs.
    blockInputs();
    blockSave();
    // Report settings and request report back.
    emit setFineCurrent(new_set);
    emit setDrawDeint(ui->cbxDrawDeint->isChecked());
}

//------------------------ Close the dialog.
void fine_vidin_set::usrClose()
{
    this->close();
}

//------------------------ Populate reported settings in the dialog.
void fine_vidin_set::newSettings(vid_preset_t in_set)
{
    new_set = in_set;

    ui->spbCropLeft->setValue(new_set.crop_left);
    ui->spbCropRight->setValue(new_set.crop_right);
    ui->spbCropTop->setValue(new_set.crop_top);
    ui->spbCropBottom->setValue(new_set.crop_bottom);
    if(new_set.colors==vid_preset_t::COLOR_R)
    {
        ui->lbxColorUse->setCurrentIndex(LIST_COLOR_R);
    }
    else if(new_set.colors==vid_preset_t::COLOR_G)
    {
        ui->lbxColorUse->setCurrentIndex(LIST_COLOR_G);
    }
    else if(new_set.colors==vid_preset_t::COLOR_B)
    {
        ui->lbxColorUse->setCurrentIndex(LIST_COLOR_B);
    }
    else
    {
        ui->lbxColorUse->setCurrentIndex(LIST_COLORS_ALL);
    }

    // Enable inputs.
    ui->spbCropLeft->setEnabled(true);
    ui->spbCropRight->setEnabled(true);
    ui->spbCropTop->setEnabled(true);
    ui->spbCropBottom->setEnabled(true);
    ui->lbxColorUse->setEnabled(true);

    // Enable buttons.
    blockSave();
}

//------------------------ Populate reported settings in the dialog (another part).
void fine_vidin_set::newDrawDeint(bool in_set)
{
    // Enable inputs.
    ui->cbxDrawDeint->setChecked(in_set);
    ui->cbxDrawDeint->setEnabled(true);

    // Enable buttons.
    blockSave();
}
