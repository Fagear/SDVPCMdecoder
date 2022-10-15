#include "capt_sel.h"
#include "ui_capt_sel.h"

//------------------------
capt_sel::capt_sel(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::capt_sel)
{
    ui->setupUi(this);

    scene = NULL;
    pixels = NULL;
    capt_dev = NULL;
    capture_rate = 100;

    scene = new QGraphicsScene(this);
    ui->viewport->setScene(scene);
    ui->viewport->setAlignment(Qt::AlignLeft|Qt::AlignTop);

    preview_pix = QPixmap(PV_WIDTH, PV_HEIGHT);
    preview_pix.fill(Qt::darkGray);
    pixels = new QGraphicsPixmapItem(preview_pix);
    pixels->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    scene->addItem(pixels);

    ffmpeg_thread = NULL;
    ffmpeg_thread = new QThread;
    capt_dev = new FFMPEGWrapper();
    capt_dev->moveToThread(ffmpeg_thread);
    connect(ffmpeg_thread, SIGNAL(finished()), ffmpeg_thread, SLOT(deleteLater()));
    connect(this, SIGNAL(finished(int)), ffmpeg_thread, SLOT(quit()));
    connect(this, SIGNAL(requestDropDet(bool)), capt_dev, SLOT(slotSetDropDetector(bool)));
    connect(this, SIGNAL(requestColor(vid_preset_t)), capt_dev, SLOT(slotSetCropColor(vid_preset_t)));
    connect(this, SIGNAL(requestResize(uint16_t,uint16_t)), capt_dev, SLOT(slotSetResize(uint16_t,uint16_t)));
    connect(this, SIGNAL(requestFPS(uint8_t)), capt_dev, SLOT(slotSetFPS(uint8_t)));
    connect(this, SIGNAL(requestOffset(uint16_t,uint16_t)), capt_dev, SLOT(slotSetTLOffset(uint16_t,uint16_t)));
    connect(this, SIGNAL(requestDeviceList()), capt_dev, SLOT(slotGetDeviceList()));
    connect(this, SIGNAL(openDevice(QString,QString)), capt_dev, SLOT(slotOpenInput(QString,QString)));
    connect(this, SIGNAL(closeDevice()), capt_dev, SLOT(slotCloseInput()));
    connect(this, SIGNAL(requestFrame()), capt_dev, SLOT(slotGetNextFrame()));
    connect(capt_dev, SIGNAL(sigInputReady(int, int, uint32_t, float)), this, SLOT(captureReady(int, int, uint32_t, float)));
    connect(capt_dev, SIGNAL(sigInputClosed()), this, SLOT(captureClosed()));
    connect(capt_dev, SIGNAL(sigVideoError(uint32_t)), this, SLOT(captureError(uint32_t)));
    connect(capt_dev, SIGNAL(newDeviceList(VCapList)), this, SLOT(refillDevList(VCapList)));
    ffmpeg_thread->start();

    connect(ui->btnClose, SIGNAL(clicked(bool)), this, SLOT(usrClose()));
    connect(ui->btnSave, SIGNAL(clicked(bool)), this, SLOT(usrSave()));
    connect(ui->btnRefresh, SIGNAL(clicked(bool)), this, SLOT(usrRefresh()));
    connect(ui->cbxDefaults, SIGNAL(toggled(bool)), this, SLOT(toggleDefaults()));
    connect(ui->btnNTSC, SIGNAL(clicked(bool)), this, SLOT(usrSetNTSC()));
    connect(ui->btnPAL, SIGNAL(clicked(bool)), this, SLOT(usrSetPAL()));
    connect(&capture_poll, SIGNAL(timeout()), this, SLOT(pollCapture()));
    capture_poll.setSingleShot(true);
    connect(&capt_busy, SIGNAL(timeout()), this, SLOT(lostFFMPEG()));
    capt_busy.setInterval(FMPG_TIME_DEV_LIST);
    capt_busy.setSingleShot(true);
}

capt_sel::~capt_sel()
{
    // Prevent crash when dialog closes while device enumeration is in progress.
    disconnect(capt_dev, 0, 0, 0);

    if(ffmpeg_thread!=NULL)
    {
        ffmpeg_thread->quit();
        ffmpeg_thread->wait(1000);
    }
    qInfo()<<"[CSEL] Dialog destroyed";
    delete ui;
}

//------------------------ Dialog about to appear.
int capt_sel::exec()
{
    qInfo()<<"[CSEL] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());

    // Register all demuxers.
    avdevice_register_all();

    usrRefresh();
    // Open the dialog.
    return QDialog::exec();
}

//------------------------ Disable offset controls.
void capt_sel::disableOffset()
{
    ui->spbXOffset->setEnabled(false);
    ui->spbYOffset->setEnabled(false);
    ui->spbXOffset->setValue(0);
    ui->spbYOffset->setValue(0);
}

//------------------------ Enable offset controls.
void capt_sel::enableOffset()
{
    ui->spbXOffset->setEnabled(true);
    ui->spbYOffset->setEnabled(true);
}

//------------------------ Stop capturing.
void capt_sel::stopCapture()
{
    // Close current capture if any.
    emit closeDevice();
    QApplication::processEvents();
}

//------------------------ Clear preview area.
void capt_sel::stopPreview()
{
    // Disconnect preview updating slot.
    disconnect(capt_dev, SIGNAL(newImage(QImage,bool)), this, SLOT(redrawPreview(QImage,bool)));
    // Stop preview updating.
    capture_poll.stop();
    // Fill preview with gray.
    preview_pix.fill(Qt::darkGray);
    pixels->setPixmap(preview_pix);
    ui->viewport->viewport()->repaint();
}

//------------------------ Refresh list of capture devices available in the system.
void capt_sel::usrRefresh()
{
    // Stop previous capture.
    stopCapture();
    // Stop preview updating.
    stopPreview();
    // Clear device list and block it.
    // Prevent emitting "row selected" signal while clearing and refilling.
    disconnect(ui->listWidget, SIGNAL(clicked(QModelIndex)), this, SLOT(selectDevice()));
    disconnect(ui->btnApply, SIGNAL(clicked(bool)), this, SLOT(selectDevice()));
    // Clear device list.
    ui->listWidget->clear();
    ui->listWidget->setEnabled(false);
    ui->viewport->setEnabled(false);
    // Block buttons.
    disableOffset();
    ui->btnApply->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
    ui->btnSave->setEnabled(false);
    // Insert dummy line to indicate refill progress.
    QListWidgetItem *new_line = new QListWidgetItem;
    new_line->setText(tr("Список обновляется..."));
    ui->listWidget->addItem(new_line);
    // Request capture devices list.
    capt_busy.setInterval(FMPG_TIME_DEV_LIST);
    capt_busy.start();
    emit requestDeviceList();
}

//------------------------ Save capture device selection.
void capt_sel::usrSave()
{

}

//------------------------ Close the dialog.
void capt_sel::usrClose()
{
    this->close();
}

//------------------------
void capt_sel::toggleDefaults()
{
    if(ui->cbxDefaults->isChecked()==false)
    {
        ui->btnNTSC->setEnabled(true);
        ui->btnPAL->setEnabled(true);
        ui->spbSetWidth->setEnabled(true);
        ui->spbSetHeight->setEnabled(true);
        ui->spbSetFPS->setEnabled(true);
        ui->lbxColorCh->setEnabled(true);
        ui->btnApply->setEnabled(true);
        enableOffset();
    }
    else
    {
        ui->btnNTSC->setEnabled(false);
        ui->btnPAL->setEnabled(false);
        ui->spbSetWidth->setEnabled(false);
        ui->spbSetHeight->setEnabled(false);
        ui->spbSetFPS->setEnabled(false);
        ui->lbxColorCh->setEnabled(false);
        ui->btnApply->setEnabled(false);
        disableOffset();
    }
    selectDevice();
}

//------------------------ Open selected capture device and start its preview.
void capt_sel::selectDevice()
{
    int selected_row;
    std::string device_path;
    vid_preset_t color_channel;

    // Disable preview updating.
    stopPreview();

    // Store selected row.
    selected_row = ui->listWidget->currentRow();
    //qInfo()<<"[CSEL] Row"<<selected_row<<"selected";

    // Avoid opening capture on "-1" (list cleared) and "0" (dummy first line).
    if(selected_row<=0)
    {
        // Disable offset controls (used only for screen capture).
        disableOffset();
        stopCapture();
        return;
    }

    // Get OS path for the device.
    device_path = ui->listWidget->item(selected_row)->data(Qt::UserRole).toString().toStdString();
    qInfo()<<"[CSEL] Opening capture device:"<<device_path.c_str();
    emit requestDropDet(false);
    // Check if defaults for the device should be used.
    if(ui->cbxDefaults->isChecked()==false)
    {
        // Get user set capture parameters and send those to FFMPEG.
        if(ui->lbxColorCh->currentIndex()==LIST_COLOR_R)
        {
            color_channel.colors = vid_preset_t::COLOR_R;
            emit requestColor(color_channel);
        }
        else if(ui->lbxColorCh->currentIndex()==LIST_COLOR_G)
        {
            color_channel.colors = vid_preset_t::COLOR_G;
            emit requestColor(color_channel);
        }
        else if(ui->lbxColorCh->currentIndex()==LIST_COLOR_B)
        {
            color_channel.colors = vid_preset_t::COLOR_B;
            emit requestColor(color_channel);
        }
        else
        {
            color_channel.colors = vid_preset_t::COLOR_BW;
            emit requestColor(color_channel);
        }
        emit requestFPS((uint8_t)ui->spbSetFPS->value());
        emit requestResize((uint16_t)ui->spbSetWidth->value(), (uint16_t)ui->spbSetHeight->value());
        if((device_path==FFWR_WIN_GDI_CAP)||(device_path==FFWR_WIN_DSHOW_CAP))
        {
            emit requestOffset((uint16_t)ui->spbXOffset->value(), (uint16_t)ui->spbYOffset->value());
            // Enable offset controls for screencap.
            enableOffset();
        }
        else
        {
            emit requestOffset(0, 0);
            // Disable offset controls (used only for screen capture).
            disableOffset();
        }
    }
    else
    {
        // Skip trying open device with user settings.
        color_channel.colors = vid_preset_t::COLOR_BW;
        emit requestColor(color_channel);
        emit requestOffset(0, 0);
        emit requestFPS(0);
        emit requestResize(0, 0);
    }
    // Start FFMPEG watchdog.
    capt_busy.setInterval(FMPG_TIME_OPEN);
    capt_busy.start();
    // Request opening selected device.
    emit openDevice(ui->listWidget->item(selected_row)->data(Qt::UserRole).toString(),
                    ui->listWidget->item(selected_row)->data(Qt::ToolTipRole).toString());
}

//------------------------ Set capture parameters for NTSC video.
void capt_sel::usrSetNTSC()
{
    ui->spbSetWidth->setValue(640);
    ui->spbSetHeight->setValue(480);
    ui->spbSetFPS->setValue(30);
}

//------------------------ Set capture parameters for PAL video.
void capt_sel::usrSetPAL()
{
    ui->spbSetWidth->setValue(768);
    ui->spbSetHeight->setValue(576);
    ui->spbSetFPS->setValue(25);
}

//------------------------ Poll capture device for the next frame.
void capt_sel::pollCapture()
{
    // Start FFMPEG watchdog.
    capt_busy.setInterval(FMPG_TIME_FRAME);
    capt_busy.start();
    // Start capture time measuring.
    capt_meas.start();
    // Request new frame from the capture.
    emit requestFrame();
}

//------------------------ FFMPEG thread took too long to respond.
void capt_sel::lostFFMPEG()
{
    stopCapture();
    // Disable preview updating.
    stopPreview();
    // Display error dialog.
    QMessageBox::critical(this, tr("Ошибка"), tr("Не получен вовремя ответ от FFMPEG, возможно библиотека зависла."));
    // Unblock buttons.
    ui->btnApply->setEnabled(true);
    ui->btnRefresh->setEnabled(true);
    //ui->btnSave->setEnabled(true);  // TODO: implement device selection
}

//------------------------ Refill device list in the dialog.
void capt_sel::refillDevList(VCapList in_list)
{
    // Stop FFMPEG watchdog.
    capt_busy.stop();
    // Remove placeholder.
    ui->listWidget->clear();
    // Check if there are any devices.
    if(in_list.dev_list.size()==0)
    {
        // Nothing was found.
        QListWidgetItem *new_line = new QListWidgetItem;
        new_line->setText(tr("Нет устройств захвата видео"));
        ui->listWidget->addItem(new_line);
    }
    else
    {
        // Insert dummy line for no selection.
        QListWidgetItem *dummy_line = new QListWidgetItem;
        dummy_line->setText(tr("Устройство не выбрано"));
        ui->listWidget->addItem(dummy_line);
        // Refill device list.
        for(size_t idx=0;idx<in_list.dev_list.size();idx++)
        {
            QListWidgetItem *new_line = new QListWidgetItem;
            new_line->setText(QString::fromStdString(in_list.dev_list[idx].dev_name));
            new_line->setData(Qt::ToolTipRole, QString::fromStdString(in_list.dev_list[idx].dev_class));
            new_line->setData(Qt::UserRole, QVariant(QString::fromStdString(in_list.dev_list[idx].dev_path)));
            ui->listWidget->addItem(new_line);
        }
        // Unblock list.
        ui->listWidget->setEnabled(true);
        ui->viewport->setEnabled(true);
        // Enable preview capture on device selection.
        connect(ui->listWidget, SIGNAL(clicked(QModelIndex)), this, SLOT(selectDevice()));
        connect(ui->btnApply, SIGNAL(clicked(bool)), this, SLOT(selectDevice()));
    }
    // Unblock buttons.
    ui->btnApply->setEnabled(true);
    ui->btnRefresh->setEnabled(true);
    //ui->btnSave->setEnabled(true); // TODO: implement device selection
}

//------------------------ Capture device is opened and ready.
void capt_sel::captureReady(int in_width, int in_height, uint32_t in_frames, float in_fps)
{
    Q_UNUSED(in_frames)
    QString resolution_str;
    // Stop FFMPEG watchdog.
    capt_busy.stop();
    // Update current capture resolution in the window.
    resolution_str = QString::number(in_width)+"x"+QString::number(in_height)+"@"+QString::number(in_fps);
    ui->edtApplied->setText(resolution_str);
    // Connect new frame event to redrawer.
    connect(capt_dev, SIGNAL(newImage(QImage,bool)), this, SLOT(redrawPreview(QImage,bool)));
    // Configure polling timer.
    capture_rate = (uint8_t)(1000/in_fps);
    capture_poll.setInterval(capture_rate);
    qDebug()<<"[CSEL] Capture rate"<<capture_poll.interval()<<"ms,"<<(1000/(float)capture_rate)<<"Hz";
    // Request first frame from the device.
    pollCapture();
}

//------------------------ Capture device is closed.
void capt_sel::captureClosed()
{
    qDebug()<<"[CSEL] Capture closed";
    // Stop FFMPEG watchdog.
    capt_busy.stop();
    // Disable deffered frame request.
    stopPreview();
}

//------------------------ Something went wrong with capture process.
void capt_sel::captureError(uint32_t in_err)
{
    QString err_string;
    // Stop FFMPEG watchdog.
    capt_busy.stop();
    // Disable preview updating.
    stopPreview();
    qWarning()<<"[CSEL] Capture error"<<in_err;
    // Decode error code.
    if(in_err==FFMPEGWrapper::FFMERR_NOT_INIT)
    {
        return;
    }
    else if(in_err==FFMPEGWrapper::FFMERR_NO_SRC)
    {
        err_string = tr("FFMPEG: Не удалось открыть захват с заданными параметрами");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_NO_INFO)
    {
        err_string = tr("FFMPEG: Не удалось получить информацию о потоке видео");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_NO_VIDEO)
    {
        err_string = tr("FFMPEG: Не удалось найти поток видео");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_NO_DECODER)
    {
        err_string = tr("FFMPEG: Не удалось найти декодер для видео");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_NO_RAM_DEC)
    {
        err_string = tr("FFMPEG: Не удалось выделить ОЗУ для декодера видео");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_DECODER_PARAM)
    {
        err_string = tr("FFMPEG: Не удалось установить параметры для декодера видео");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_DECODER_START)
    {
        err_string = tr("FFMPEG: Не удалось запустить декодер видео");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_NO_RAM_FB)
    {
        err_string = tr("FFMPEG: Не удалось выделить ОЗУ для буфера кадра");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_NO_RAM_READ)
    {
        err_string = tr("FFMPEG: Не удалось считать пакет данных из декодера");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_CONV_INIT)
    {
        err_string = tr("FFMPEG: Не удалось инициализировать преобразователь кадра");
    }
    else if(in_err==FFMPEGWrapper::FFMERR_FRM_CONV)
    {
        err_string = tr("FFMPEG: Не удалось преобразовать формат кадра видео");
    }
    else
    {
        err_string = tr("FFMPEG: Неизвестная ошибка захвата видео");
    }
    // Display error dialog.
    QMessageBox::critical(this, tr("Ошибка захвата видео"), err_string);
}

//------------------------ Redraw new image on the preview area.
void capt_sel::redrawPreview(QImage in_image, bool in_double)
{
    Q_UNUSED(in_double)
    uint32_t draw_rate;
    capt_busy.stop();
    if(in_image.isNull()!=false)
    {
        return;
    }
    draw_rate = capt_meas.elapsed();
    if(in_image.height()!=FFMPEGWrapper::DUMMY_HEIGTH)
    {
        //qDebug()<<"[CSEL] New image";
        // Update pixmap.
        pixels->setPixmap(QPixmap::fromImage(in_image.copy()));
        //ui->viewport->viewport()->repaint();
        if(draw_rate>capture_rate)
        {
            // Frame updating took too long, rate is already lower than is should be, request new frame ASAP.
            pollCapture();
            //qInfo()<<"Redraw in"<<draw_rate<<"(too slow)";
        }
        else
        {
            // Calculate how many ms is left for waiting.
            capture_poll.setInterval(capture_rate - (uint8_t)draw_rate);
            //qInfo()<<"Redraw in"<<draw_rate<<", timer set to"<<capture_poll.interval();
            capture_poll.start();
        }
    }
    else
    {
        qDebug()<<"[CSEL] New dummy image";
        // Skip dummy frame and request next one.
        pollCapture();
    }
}
