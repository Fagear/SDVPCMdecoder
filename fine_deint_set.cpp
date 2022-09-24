#include "fine_deint_set.h"
#include "ui_fine_deint_set.h"

fine_deint_set::fine_deint_set(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::fine_deint_set)
{
    ui->setupUi(this);

    connect(ui->btnClose, SIGNAL(clicked(bool)), this, SLOT(usrClose()));
    connect(ui->btnRevert, SIGNAL(clicked(bool)), this, SLOT(usrRevert()));
    connect(ui->btnDefaults, SIGNAL(clicked(bool)), this, SLOT(usrDefaults()));
    connect(ui->btnSave, SIGNAL(clicked(bool)), this, SLOT(usrSave()));
    connect(ui->spbMaxUnch14, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->spbMaxUnch16, SIGNAL(valueChanged(int)), this, SLOT(setChange()));
    connect(ui->cbxUseECC, SIGNAL(toggled(bool)), this, SLOT(setChange()));
    connect(ui->cbxInsertAbove, SIGNAL(toggled(bool)), this, SLOT(setChange()));
    connect(ui->cbxMaskSeams, SIGNAL(toggled(bool)), this, SLOT(setChange()));
    connect(ui->spbBrokeMask, SIGNAL(valueChanged(int)), this, SLOT(setChange()));

    no_change = true;
    blockInputs();
    blockSave();

    qInfo()<<"[FASM] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

fine_deint_set::~fine_deint_set()
{
    qInfo()<<"[FASM] Dialog destroyed";
    delete ui;
}

//------------------------ Dialog about to appear.
int fine_deint_set::exec()
{
    // Request settings to populate in dialog.
    emit requestFineCurrent();
    // Open the dialog.
    return QDialog::exec();
}

//------------------------ Dialog is about to close.
void fine_deint_set::closeEvent(QCloseEvent *event)
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
void fine_deint_set::reject()
{
    this->close();
}

//------------------------ Block all user input fields.
void fine_deint_set::blockInputs()
{
    ui->spbMaxUnch14->setEnabled(false);
    ui->spbMaxUnch16->setEnabled(false);
    ui->cbxUseECC->setEnabled(false);
    ui->cbxInsertAbove->setEnabled(false);
    ui->cbxMaskSeams->setEnabled(false);
    ui->spbBrokeMask->setEnabled(false);
}

//------------------------ Block Save and Revert buttons.
void fine_deint_set::blockSave()
{
    ui->btnRevert->setEnabled(false);
    ui->btnSave->setEnabled(false);
    no_change = true;

    ui->btnClose->setFocus();
}

//------------------------ Unblock Save and Revert buttons.
void fine_deint_set::enableSave()
{
    ui->btnRevert->setEnabled(true);
    if(ui->btnSave->isEnabled()==false)
    {
        ui->btnSave->setEnabled(true);
        ui->btnSave->setFocus();
    }
}

//------------------------ React to any change of settings.
void fine_deint_set::setChange()
{
    no_change = false;
    enableSave();
}

//------------------------ Request reset to defaults.
void fine_deint_set::usrDefaults()
{
    // Show a dialog for reset confirmation.
    QMessageBox usrConfirm(this);
    usrConfirm.setWindowTitle(tr("Сброс настроек"));
    usrConfirm.setText(tr("Сброс настроек деинтерливера"));
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
void fine_deint_set::usrRevert()
{
    // Block all inputs.
    blockInputs();
    blockSave();
    // Request current settings.
    emit requestFineCurrent();
}

//------------------------ Saving settings (reporting inputs).
void fine_deint_set::usrSave()
{
    // Block all inputs.
    blockInputs();
    blockSave();
    // Report settings and request report back.
    emit setMaxUnchecked14((uint8_t)(ui->spbMaxUnch14->value()));
    emit setMaxUnchecked16((uint8_t)(ui->spbMaxUnch16->value()));
    emit setUseECC(ui->cbxUseECC->isChecked());
    emit setInsertLine(ui->cbxInsertAbove->isChecked());
    emit setMaskSeams(ui->cbxMaskSeams->isChecked());
    emit setBrokeMask((uint8_t)(ui->spbBrokeMask->value()));
}

//------------------------ Close the dialog.
void fine_deint_set::usrClose()
{
    this->close();
}

//------------------------ Populate reported setting in the dialog.
void fine_deint_set::newMaxUnchecked14(uint8_t in_set)
{
    ui->spbMaxUnch14->setValue(in_set);
    ui->spbMaxUnch14->setEnabled(true);

    // Enable buttons.
    blockSave();
}

//------------------------ Populate reported setting in the dialog.
void fine_deint_set::newMaxUnchecked16(uint8_t in_set)
{
    ui->spbMaxUnch16->setValue(in_set);
    ui->spbMaxUnch16->setEnabled(true);

    // Enable buttons.
    blockSave();
}

//------------------------ Populate reported setting in the dialog.
void fine_deint_set::newUseECC(bool in_set)
{
    ui->cbxUseECC->setChecked(in_set);
    ui->cbxUseECC->setEnabled(true);

    // Enable buttons.
    blockSave();
}

//------------------------ Populate reported setting in the dialog.
void fine_deint_set::newInsertLine(bool in_set)
{
    ui->cbxInsertAbove->setChecked(in_set);
    ui->cbxInsertAbove->setEnabled(true);

    // Enable buttons.
    blockSave();
}

//------------------------ Populate reported setting in the dialog.
void fine_deint_set::newMaskSeams(bool in_set)
{
    ui->cbxMaskSeams->setChecked(in_set);
    ui->cbxMaskSeams->setEnabled(true);

    // Enable buttons.
    blockSave();
}

//------------------------ Populate reported setting in the dialog.
void fine_deint_set::newBrokeMask(uint8_t in_set)
{
    ui->spbBrokeMask->setValue(in_set);
    ui->spbBrokeMask->setEnabled(true);

    // Enable buttons.
    blockSave();
}
