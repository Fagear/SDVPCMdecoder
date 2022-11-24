#include "fine_bin_set.h"
#include "ui_fine_bin_set.h"

fine_bin_set::fine_bin_set(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::fine_bin_set)
{
    ui->setupUi(this);

    connect(ui->spbMaxBlack, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbMinWhite, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbMinContrast, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbMinRef, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbMaxRef, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbMinValidCRC, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->cbxForceCoords, SIGNAL(toggled(bool)), this, SLOT(setChange()));
    connect(ui->spbLeftDataOfs, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbRightDataOfs, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->cbxCoordSearch, SIGNAL(toggled(bool)), this, SLOT(setChange()));
    connect(ui->spbPickLeft, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbPickRight, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbSearchWidth, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->cbxAllowNoMarker, SIGNAL(toggled(bool)), this, SLOT(setChange()));

    connect(ui->btnClose, SIGNAL(clicked(bool)), this, SLOT(usrClose()));
    connect(ui->btnRevert, SIGNAL(clicked(bool)), this, SLOT(usrRevert()));
    connect(ui->btnDefaults, SIGNAL(clicked(bool)), this, SLOT(usrDefaults()));
    connect(ui->btnSave, SIGNAL(clicked(bool)), this, SLOT(usrSave()));

    no_change = true;
    blockInputs();
    blockSave();

    qInfo()<<"[FBIN] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

fine_bin_set::~fine_bin_set()
{
    qInfo()<<"[FBIN] Dialog destroyed";
    delete ui;
}

//------------------------ Dialog about to appear.
int fine_bin_set::exec()
{
    // Request settings to populate in dialog.
    emit requestFineCurrent();
    // Open the dialog.
    return QDialog::exec();
}

//------------------------ Dialog is about to close.
void fine_bin_set::closeEvent(QCloseEvent *event)
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
void fine_bin_set::reject()
{
    this->close();
}

//------------------------ Block all user input fields.
void fine_bin_set::blockInputs()
{
    ui->spbMaxBlack->setEnabled(false);
    ui->spbMinWhite->setEnabled(false);
    ui->spbMinContrast->setEnabled(false);
    ui->spbMinRef->setEnabled(false);
    ui->spbMaxRef->setEnabled(false);
    ui->spbMinValidCRC->setEnabled(false);

    ui->cbxForceCoords->setEnabled(false);
    ui->spbLeftDataOfs->setEnabled(false);
    ui->spbRightDataOfs->setEnabled(false);
    ui->cbxCoordSearch->setEnabled(false);
    ui->spbPickLeft->setEnabled(false);
    ui->spbPickRight->setEnabled(false);
    ui->spbSearchWidth->setEnabled(false);
    ui->cbxAllowNoMarker->setEnabled(false);
}

//------------------------ Block Save and Revert buttons.
void fine_bin_set::blockSave()
{
    ui->btnRevert->setEnabled(false);
    ui->btnSave->setEnabled(false);
    no_change = true;

    ui->btnClose->setFocus();
}

//------------------------ Unblock all user input fields.
void fine_bin_set::enableInputs()
{
    ui->spbMaxBlack->setEnabled(true);
    ui->spbMinWhite->setEnabled(true);
    ui->spbMinContrast->setEnabled(true);
    ui->spbMinRef->setEnabled(true);
    ui->spbMaxRef->setEnabled(true);
    ui->spbMinValidCRC->setEnabled(true);

    ui->cbxForceCoords->setEnabled(true);
    if(ui->cbxForceCoords->isChecked()==false)
    {
        ui->cbxCoordSearch->setEnabled(true);
        ui->spbSearchWidth->setEnabled(true);
        ui->spbLeftDataOfs->setEnabled(false);
        ui->spbRightDataOfs->setEnabled(false);
    }
    else
    {
        ui->cbxCoordSearch->setEnabled(false);
        ui->spbSearchWidth->setEnabled(false);
        ui->spbLeftDataOfs->setEnabled(true);
        ui->spbRightDataOfs->setEnabled(true);
    }
    ui->spbPickLeft->setEnabled(true);
    ui->spbPickRight->setEnabled(true);
    ui->cbxAllowNoMarker->setEnabled(true);
}

//------------------------ Unblock Save and Revert buttons.
void fine_bin_set::enableSave()
{
    ui->btnRevert->setEnabled(true);
    if(ui->btnSave->isEnabled()==false)
    {
        ui->btnSave->setEnabled(true);
        ui->btnSave->setFocus();
    }
}

//------------------------ React to any change of settings.
void fine_bin_set::setChange()
{
    no_change = false;
    enableSave();
    enableInputs();
}

//------------------------ Request reset to defaults.
void fine_bin_set::usrDefaults()
{
    // Show a dialog for reset confirmation.
    QMessageBox usrConfirm(this);
    usrConfirm.setWindowTitle(tr("Сброс настроек"));
    usrConfirm.setText(tr("Сброс настроек бинаризатора"));
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
void fine_bin_set::usrRevert()
{
    // Block all inputs.
    blockInputs();
    blockSave();
    // Request current settings.
    emit requestFineCurrent();
}

//------------------------ Saving settings (reporting inputs).
void fine_bin_set::usrSave()
{
    // Read all user inputs.
    new_set.max_black_lvl = (uint8_t)(ui->spbMaxBlack->value());
    new_set.min_white_lvl = (uint8_t)(ui->spbMinWhite->value());
    new_set.min_contrast = (uint8_t)(ui->spbMinContrast->value());
    new_set.min_ref_lvl = (uint8_t)(ui->spbMinRef->value());
    new_set.max_ref_lvl = (uint8_t)(ui->spbMaxRef->value());
    new_set.min_valid_crcs = (uint8_t)(ui->spbMinValidCRC->value());

    new_set.en_force_coords = (bool)(ui->cbxForceCoords->isChecked());
    new_set.horiz_coords.data_start = (int16_t)(ui->spbLeftDataOfs->value());
    new_set.horiz_coords.data_stop = (int16_t)(ui->spbRightDataOfs->value());
    new_set.en_coord_search = (bool)(ui->cbxCoordSearch->isChecked());
    new_set.left_bit_pick = (uint8_t)(ui->spbPickLeft->value());
    new_set.right_bit_pick = (uint8_t)(ui->spbPickRight->value());
    new_set.mark_max_dist = (uint8_t)(ui->spbSearchWidth->value());
    new_set.en_good_no_marker = (bool)(ui->cbxAllowNoMarker->isChecked());

    // Block all inputs.
    blockInputs();
    blockSave();
    // Report settings and request report back.
    emit setFineCurrent(new_set);
}

//------------------------ Close the dialog.
void fine_bin_set::usrClose()
{
    this->close();
}

//------------------------ Populate reported settings in the dialog.
void fine_bin_set::newSettings(bin_preset_t in_set)
{
    new_set = in_set;

    ui->spbMaxBlack->setValue(new_set.max_black_lvl);
    ui->spbMinWhite->setValue(new_set.min_white_lvl);
    ui->spbMinContrast->setValue(new_set.min_contrast);
    ui->spbMinRef->setValue(new_set.min_ref_lvl);
    ui->spbMaxRef->setValue(new_set.max_ref_lvl);
    ui->spbMinValidCRC->setValue(new_set.min_valid_crcs);

    ui->cbxForceCoords->setChecked(new_set.en_force_coords);
    ui->spbLeftDataOfs->setValue(new_set.horiz_coords.data_start);
    ui->spbRightDataOfs->setValue(new_set.horiz_coords.data_stop);
    ui->cbxCoordSearch->setChecked(new_set.en_coord_search);
    ui->spbPickLeft->setValue(new_set.left_bit_pick);
    ui->spbPickRight->setValue(new_set.right_bit_pick);
    ui->spbSearchWidth->setValue(new_set.mark_max_dist);
    ui->cbxAllowNoMarker->setChecked(new_set.en_good_no_marker);

    // Enable inputs.
    enableInputs();

    // Enable buttons.
    blockSave();
}
