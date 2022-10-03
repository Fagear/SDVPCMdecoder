#include "capt_sel.h"
#include "ui_capt_sel.h"

//------------------------
VCapDevice::VCapDevice()
{
    this->clear();
}

VCapDevice::VCapDevice(const VCapDevice &in_object)
{
    dev_name = in_object.dev_name;
    dev_class = in_object.dev_class;
    dev_path = in_object.dev_path;
}

VCapDevice& VCapDevice::operator= (const VCapDevice &in_object)
{
    if(this==&in_object) return *this;

    dev_name = in_object.dev_name;
    dev_class = in_object.dev_class;
    dev_path = in_object.dev_path;

    return *this;
}

//------------------------
void VCapDevice::clear()
{
    dev_name.clear();
    dev_class.clear();
    dev_path.clear();
}

//------------------------
VCapList::VCapList()
{
    this->clear();
}

VCapList::VCapList(const VCapList &in_object)
{
    dev_list = in_object.dev_list;
}

VCapList& VCapList::operator= (const VCapList &in_object)
{
    if(this==&in_object) return *this;

    dev_list = in_object.dev_list;

    return *this;
}

void VCapList::clear()
{
    dev_list.clear();
}

//------------------------
capt_sel::capt_sel(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::capt_sel)
{
    ui->setupUi(this);

    scene = NULL;
    pixels = NULL;
    format_ctx = NULL;
    video_dec_ctx = NULL;
    dec_frame = NULL;
    dec_packet.buf = NULL;
    conv_ctx = NULL;
    img_buf_free = true;

    scene = new QGraphicsScene(this);
    ui->viewport->setScene(scene);
    ui->viewport->setAlignment(Qt::AlignLeft|Qt::AlignTop);

    preview_pix = QPixmap(CSEL_PREV_WIDTH, CSEL_PREV_HEIGTH);
    preview_pix.fill(Qt::darkGray);
    pixels = new QGraphicsPixmapItem(preview_pix);
    pixels->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    scene->addItem(pixels);

    connect(this, SIGNAL(newDeviceList(VCapList)), this, SLOT(refillDevList(VCapList)));
    connect(this, SIGNAL(newPreview(QPixmap)), this, SLOT(redrawPreview(QPixmap)));
    connect(ui->btnClose, SIGNAL(clicked(bool)), this, SLOT(usrClose()));
    connect(ui->btnSave, SIGNAL(clicked(bool)), this, SLOT(usrSave()));
    connect(ui->btnRefresh, SIGNAL(clicked(bool)), this, SLOT(usrRefresh()));
    connect(ui->cbxDefaults, SIGNAL(toggled(bool)), this, SLOT(toggleDefaults()));
    connect(ui->btnNTSC, SIGNAL(clicked(bool)), this, SLOT(usrSetNTSC()));
    connect(ui->btnPAL, SIGNAL(clicked(bool)), this, SLOT(usrSetPAL()));
    connect(&capture_poll, SIGNAL(timeout()), this, SLOT(pollCapture()));

    capture_poll.setSingleShot(false);

    qInfo()<<"[CSEL] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

capt_sel::~capt_sel()
{
    // Prevent crash when dialog closes while device enumeration is in progress.
    disconnect(this, SIGNAL(newDeviceList(VCapList)), this, SLOT(refillDevList(VCapList)));
    resetFFMPEG();
    qInfo()<<"[CSEL] Dialog destroyed";
    delete ui;
}

//------------------------ Dialog about to appear.
int capt_sel::exec()
{
    // Register all demuxers.
    avdevice_register_all();

    usrRefresh();
    // Open the dialog.
    return QDialog::exec();
}

//------------------------ Get list of compatible video capture devices.
void capt_sel::getVideoCaptureList()
{
    bool suppress_log;
    int av_res;
    AVDictionary *inopt = NULL;
    const AVInputFormat *infmt = NULL;
    AVFormatContext *format_ctx = NULL;

    VCapDevice temp_dev;
    VCapList capture_list;
    //format_ctx = avformat_alloc_context();

    suppress_log = false;

#ifdef VIP_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[CSEL] Video capture devices listing:";
    }
#endif
    // Register all demuxers.
    avdevice_register_all();

    // Get first registered video capture device class and iterate until the last one.
    while((infmt = av_input_video_device_next(infmt)))
    {
        AVDeviceInfoList *dev_list = NULL;
        std::string fmt_name(infmt->name);
        if(fmt_name.compare(CSEL_WIN_GDI_CLASS)==0)
        {
            // Found Windows GDI capture class.
            infmt = av_find_input_format(CSEL_WIN_GDI_CLASS);
            // Avoid listing devices of this class, it's not implemented in FFMPEG.
            // Set capturing parameters.
            av_dict_set(&inopt, "framerate", "60", 0);          // Frame rate
            av_dict_set(&inopt, "video_size", "640x480", 0);    // Size of the capture window.
            // Try to open GDI capture.
            av_res = avformat_open_input(&format_ctx, CSEL_WIN_GDI_CAP, infmt, &inopt);
            // Check result.
            if(av_res>=0)
            {
                // Capture is available.
#ifdef VIP_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[CSEL] FFMPEG found"<<infmt->long_name<<"device";
                }
#endif
                // Add device to the list.
                temp_dev.clear();
                temp_dev.dev_name = CSEL_WIN_GDI_NAME.toStdString();
                temp_dev.dev_class = CSEL_WIN_GDI_CLASS;
                temp_dev.dev_path = CSEL_WIN_GDI_CAP;
                capture_list.dev_list.push_back(temp_dev);
                // Close capture for now.
                avformat_close_input(&format_ctx);
            }
        }
        else
        {
            if(fmt_name.compare(CSEL_WIN_DSHOW_CLASS)==0)
            {
                // Construct name for the device.
                fmt_name = "video=";
                fmt_name = fmt_name+CSEL_WIN_DSHOW_CAP;
                // Try to open DirectShow capture.
                av_res = avformat_open_input(&format_ctx, fmt_name.c_str(), infmt, &inopt);
                // Check result.
                if(av_res>=0)
                {
                    // Capture is available.
#ifdef VIP_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[CSEL] FFMPEG found"<<infmt->long_name<<"device";
                    }
#endif
                    // Add device to the list.
                    temp_dev.clear();
                    temp_dev.dev_name = CSEL_WIN_DSHOW_NAME.toStdString();
                    temp_dev.dev_class = CSEL_WIN_DSHOW_CAP;
                    temp_dev.dev_path = fmt_name;
                    capture_list.dev_list.push_back(temp_dev);
                    // Close capture for now.
                    avformat_close_input(&format_ctx);
                }
            }

            /*AVDeviceInfoList *list = NULL;
            AVFormatContext *fmt_ctx = NULL;
            fmt_ctx = avformat_alloc_context();
            // This just crashes FFMPEG DLL.
            av_res = avdevice_list_devices(fmt_ctx, &list);*/

            // Get list of devices for current class.
            // Note: FFMPEG seems only to enumerate inputs for dshow on Windows.
            // Note: FFMPEG v4xx does not implement device listing at all.
            av_res = avdevice_list_input_sources(infmt, NULL, inopt, &dev_list);

            if(av_res>=0)
            {
                // Found some devices.
#ifdef VIP_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[CSEL] FFMPEG found"<<av_res<<infmt->long_name<<"devices";
                }
#endif
                // Cycle through all found devices.
                for(uint16_t idx=0;idx<dev_list->nb_devices;idx++)
                {
#if LIBAVUTIL_VERSION_MAJOR > 56
                    // Cycle through media types that device can provide.
                    for(int type_idx=0;type_idx<dev_list->devices[idx]->nb_media_types;type_idx++)
                    {
                        // Check if video is available.
                        if(dev_list->devices[idx]->media_types[type_idx]==AVMEDIA_TYPE_VIDEO)
                        {
                            // Construct name for the device.
                            fmt_name = "video=";
                            fmt_name = fmt_name+dev_list->devices[idx]->device_name;
                            // Try to open the device.
                            av_res = avformat_open_input(&format_ctx, fmt_name.c_str(), infmt, &inopt);
                            if(av_res>=0)
                            {
                                // Capture is available.
#ifdef VIP_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[CSEL] FFMPEG found"<<infmt->long_name<<"device:"<<dev_list->devices[idx]->device_description;
                                }
#endif
                                // Add device to the list.
                                temp_dev.clear();
                                temp_dev.dev_name = dev_list->devices[idx]->device_description;
                                temp_dev.dev_class = infmt->name;
                                temp_dev.dev_path = fmt_name;
                                capture_list.dev_list.push_back(temp_dev);
                                // Close capture for now.
                                avformat_close_input(&format_ctx);
                            }
#ifdef VIP_EN_DBG_OUT
                            else
                            {
                                // Error while trying to open capture device.
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[CSEL] FFMPEG"<<infmt->long_name<<"device"<<dev_list->devices[idx]->device_description<<"failed to capture";
                                }
                            }
#endif
                        }
#ifdef VIP_EN_DBG_OUT
                        else
                        {
                            // Current media type is not video.
                            if(suppress_log==false)
                            {
                                qInfo()<<"[CSEL] FFMPEG"<<infmt->long_name<<"device"<<dev_list->devices[idx]->device_description<<"has no video capture";
                            }
                        }
#endif
                    }
#endif
                }
                // Free device list because it was allocated.
                avdevice_free_list_devices(&dev_list);
            }
#ifdef VIP_EN_DBG_OUT
            else
            {
                // Error, no devices found.
                if(suppress_log==false)
                {
                    char err_str[80];
                    if(av_strerror(av_res, err_str, 80)==0)
                    {
                        qInfo()<<"[CSEL] FFMPEG unable to find"<<infmt->long_name<<"devices!"<<err_str;
                    }
                    else
                    {
                        qInfo()<<"[CSEL] FFMPEG unable to find"<<infmt->long_name<<"devices!";
                    }
                }
            }
#endif
        }
    }
    // Free memory from capture context.
    avformat_free_context(format_ctx);
    // Report new list of capture devices.
    emit newDeviceList(capture_list);
}

//------------------------ Free up all resources used by FFMPEG.
void capt_sel::resetFFMPEG()
{
    qInfo()<<"[CSEL] FFMPEG is being reset...";
    // Stop polling.
    capture_poll.stop();
    // Free up memory from libsws contexts.
    sws_freeContext(conv_ctx); conv_ctx = NULL;
    if(img_buf_free==false)
    {
        av_freep(video_dst_data);
        img_buf_free = true;
    }
    // Free up resources.
    if(dec_frame!=NULL)
    {
        av_frame_free(&dec_frame);
    }
    if(dec_packet.buf!=NULL)
    {
        av_packet_unref(&dec_packet);
    }
    if(video_dec_ctx!=NULL)
    {
        avcodec_free_context(&video_dec_ctx);
    }
    if(format_ctx!=NULL)
    {
        avformat_close_input(&format_ctx);
    }
}

//------------------------ Get new frame from the capture device.
void capt_sel::getNextFrame()
{
    int av_res;

    // Cycle till read video frame in the packet or EOF.
    while(1)
    {
        av_res = av_read_frame(format_ctx, &dec_packet);
        if(av_res<0)
        {
            if(av_res==AVERROR_EOF)
            {
                qInfo()<<"[CSEL] EOF";
            }
            else
            {
                qWarning()<<"[CSEL] Error reading packet!"<<av_res;
            }
            avcodec_free_context(&video_dec_ctx);
            avformat_close_input(&format_ctx);
            break;
        }
        // Check if it is the video stream.
        if(dec_packet.stream_index==stream_index)
        {
            // It is - quit cycle and go decode.
            break;
        }
        else
        {
            // It was not a video frame.
            // Free up memory from discarded packet.
            av_packet_unref(&dec_packet);
        }
    }
    // Check packet read result.
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Source read is stopped!";
        resetFFMPEG();
        return;
    }
    av_res = avcodec_send_packet(video_dec_ctx, &dec_packet);
    if(av_res<0)
    {
        // There was an error while decoding.
        // Unref frame if it will be not used by [avcodec_receive_frame()].
        qWarning()<<"[CSEL] Error decoding packet!"<<av_res;
        resetFFMPEG();
        return;
    }
    // Read decoded frame from the packet.
    av_res = avcodec_receive_frame(video_dec_ctx, dec_frame);
    if(av_res!=0)
    {
        qWarning()<<"[CSEL] Error recieving packet!"<<av_res;
        resetFFMPEG();
        return;
    }
    av_packet_unref(&dec_packet);
    av_res = sws_scale(conv_ctx, (const uint8_t * const *)dec_frame->data, dec_frame->linesize, 0, dec_frame->height, video_dst_data, video_dst_linesize);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Failed to resize the frame!"<<av_res;
        resetFFMPEG();
        return;
    }

    // First frame captures and resized successfully.
    // Convert it from FFMPEG format to grayscale QImage.
    QImage conv_image(video_dst_data[0],
                 video_dst_linesize[0],
                 av_res,
                 video_dst_linesize[0],
                 QImage::Format_Grayscale8);
    // Convert QImage to QPixmap for displaying.
    emit newPreview(QPixmap::fromImage(conv_image));
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

//------------------------ Clear preview area.
void capt_sel::clearPreview()
{
    preview_pix.fill(Qt::darkGray);
    pixels->setPixmap(preview_pix);
    ui->viewport->viewport()->repaint();
}

//------------------------ Read a frame from capture and draw it in the preview.
void capt_sel::redrawCapture()
{

}

//------------------------
void capt_sel::setChange()
{

}

//------------------------ Refresh list of capture devices available in the system.
void capt_sel::usrRefresh()
{
    // Stop preview updating.
    capture_poll.stop();
    // Clear device list and block it.
    // Prevent emitting "row selected" signal while clearing and refilling.
    disconnect(ui->listWidget, SIGNAL(clicked(QModelIndex)), this, SLOT(selectDevice()));
    disconnect(ui->btnApply, SIGNAL(clicked(bool)), this, SLOT(selectDevice()));
    // Clear device list.
    ui->listWidget->clear();
    ui->listWidget->setEnabled(false);
    // Clear preview.
    clearPreview();
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
    QtConcurrent::run(this, &capt_sel::getVideoCaptureList);
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
        ui->btnApply->setEnabled(false);
        disableOffset();
    }
    selectDevice();
}

//------------------------ Open selected capture device and start its preview.
void capt_sel::selectDevice()
{
    int selected_row, av_res;
    uint8_t open_tries;
    std::string device_path, target_class;
    std::string set_fps, set_resolution, set_xofs, set_yofs;
    AVDictionary *highspeed_cap = NULL;
    AVDictionary *lowspeed_cap = NULL;
    AVDictionary *inopts = NULL;
    const AVInputFormat *infmt = NULL;
    const AVCodec *v_decoder = NULL;
    AVStream *v_stream;

    // Clear preview frame.
    clearPreview();

    selected_row = ui->listWidget->currentRow();
    //qInfo()<<"[CSEL] Row"<<selected_row<<"selected";

    // Reset FFMPEG to defaults.
    resetFFMPEG();

    // Avoid opening capture on "-1" (list cleared) and "0" (dummy first line).
    if(selected_row<=0)
    {
        // Clear preview.
        clearPreview();
        // Stop preview updating.
        capture_poll.stop();
        disableOffset();
        return;
    }

    // Get OS path for the device.
    device_path = ui->listWidget->item(selected_row)->data(Qt::UserRole).toString().toStdString();
    open_tries = 0;

    if(ui->cbxDefaults->isChecked()==false)
    {
        // Get user set capture parameters and convert those to [AVDictionary] values.
        set_fps = QString::number(ui->spbSetFPS->value()*2).toStdString();
        set_resolution = QString::number(ui->spbSetWidth->value()).toStdString()+"x"+QString::number(ui->spbSetHeight->value()).toStdString();
        set_xofs = QString::number(ui->spbXOffset->value()).toStdString();
        set_yofs = QString::number(ui->spbYOffset->value()).toStdString();
        // Set capturing parameters.
        // Note: FFMPEG v5.x randomly crashes if gets its [const char *] from QString::toLocal8Bit().constData(), std::string.c_str() works fine.
        av_dict_set(&highspeed_cap, "framerate", set_fps.c_str(), 0);         // Frame rate
        av_dict_set(&highspeed_cap, "video_size", set_resolution.c_str(), 0); // Size of the capture window.
        av_dict_set(&highspeed_cap, "rtbufsize", "200M", 0);
        set_fps = QString::number(ui->spbSetFPS->value()).toStdString();
        av_dict_set(&lowspeed_cap, "framerate", set_fps.c_str(), 0);          // Frame rate
        av_dict_set(&lowspeed_cap, "video_size", set_resolution.c_str(), 0);  // Size of the capture window.
        av_dict_set(&lowspeed_cap, "rtbufsize", "100M", 0);
        if((device_path==CSEL_WIN_GDI_CAP)||(device_path==CSEL_WIN_DSHOW_CAP))
        {
            // Add offset for screen capture.
            av_dict_set(&highspeed_cap, "offset_x", set_xofs.c_str(), 0);     // Horizontal offset from the top left corner.
            av_dict_set(&highspeed_cap, "offset_y", set_yofs.c_str(), 0);     // Vertical offset from the top left corner.
            av_dict_set(&lowspeed_cap, "offset_x", set_xofs.c_str(), 0);     // Horizontal offset from the top left corner.
            av_dict_set(&lowspeed_cap, "offset_y", set_yofs.c_str(), 0);     // Vertical offset from the top left corner.
            enableOffset();
        }
        else
        {
            disableOffset();
        }
    }
    else
    {
        // Skip trying open device with user settings.
        open_tries = 2;
    }

    qInfo()<<"[CSEL] Opening capture device:"<<device_path.c_str();
    while(open_tries<3)
    {
        // Open a new source with FFMPEG.
        target_class = ui->listWidget->item(selected_row)->data(Qt::ToolTipRole).toString().toStdString();
        infmt = av_find_input_format(target_class.c_str());
        if(open_tries==0)
        {
            // First try - with double framerate.
            inopts = highspeed_cap;
        }
        else if(open_tries==1)
        {
            // Second try - with normal framerate.
            inopts = lowspeed_cap;
        }
        else
        {
            // Third try - with default device's parameters.
            inopts = NULL;
        }
        // Set capturing parameters and open capture device.
        // Note: [avformat_open_input()] does NOT open capture device without provided [AVInputFormat]!
        av_res = avformat_open_input(&format_ctx, device_path.c_str(), infmt, &inopts);
        if(av_res<0)
        {
            char err_str[80];
            if(av_strerror(av_res, err_str, 80)==0)
            {
                qWarning()<<"[CSEL] Unable to open source!"<<err_str;
            }
            qInfo()<<"[CSEL] Try"<<(open_tries+1)<<"failed, unsupported features count:"<<av_dict_count(inopts);
        }
        else
        {
            qInfo()<<"[CSEL] Device opened with resolution:"<<format_ctx->streams[0]->codecpar->width<<"x"<<format_ctx->streams[0]->codecpar->height;
            break;
        }
        open_tries++;
    }
    if(av_res<0)
    {
        // All tries failed.
        resetFFMPEG();
        return;
    }

    // Preset muxer settings.
    format_ctx->flags &= ~(AVFMT_FLAG_SHORTEST|AVFMT_FLAG_FAST_SEEK);   // Clear unwanted flags.
    format_ctx->flags |= (AVFMT_FLAG_AUTO_BSF|AVFMT_FLAG_GENPTS);       // Set required flags.

    // Get stream info.
    av_res = avformat_find_stream_info(format_ctx, NULL);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Unable to get info about video stream!"<<av_res;
        resetFFMPEG();
        return;
    }

    // Find best video stream.
    stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &v_decoder, 0);
    if(stream_index<0)
    {
        if(stream_index==AVERROR_STREAM_NOT_FOUND)
        {
            qWarning()<<"[CSEL] Unable to find video stream in the source!";
        }
        else if(stream_index==AVERROR_DECODER_NOT_FOUND)
        {
            qWarning()<<"[CSEL] Unable to find decoder for the video stream!";
        }
        else
        {
            qWarning()<<"[CSEL] Unknown error while searching for the video stream!"<<av_res;
        }
        resetFFMPEG();
        return;
    }

    // Allocate a codec context for the decoder.
    video_dec_ctx = avcodec_alloc_context3(v_decoder);
    if(video_dec_ctx==NULL)
    {
        qWarning()<<"[CSEL] Unable to allocate RAM for decoder!";
        resetFFMPEG();
        return;
    }
    // Select detected stream.
    v_stream = format_ctx->streams[stream_index];

    // Copy codec parameters from input stream to decoder context.
    av_res = avcodec_parameters_to_context(video_dec_ctx, v_stream->codecpar);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Unable to apply decoder parameters!";
        resetFFMPEG();
        return;
    }

    video_dec_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    video_dec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;

    // Init the decoders, with or without reference counting.
    av_dict_set(&inopts, "refcounted_frames", "1", 0);
    av_res = avcodec_open2(video_dec_ctx, v_decoder, &inopts);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Unable to start the decoder!";
        resetFFMPEG();
        return;
    }

    qInfo()<<"[CSEL] Device opened with parameters:"<<v_stream->codecpar->width<<"x"<<v_stream->codecpar->height<<"@"<<(v_stream->avg_frame_rate.num/v_stream->avg_frame_rate.den);
    set_resolution = QString::number(v_stream->codecpar->width).toStdString()+"x"+QString::number(v_stream->codecpar->height).toStdString()+"@"+QString::number(v_stream->avg_frame_rate.num/v_stream->avg_frame_rate.den).toStdString();
    ui->edtApplied->setText(QString::fromStdString(set_resolution));

    // Allocate frame container.
    dec_frame = av_frame_alloc();
    if(dec_frame==NULL)
    {
        qWarning()<<"[CSEL] Unable to allocate RAM for the frame buffer!";
        resetFFMPEG();
        return;
    }

    // Cycle till read video frame in the packet or EOF.
    while(1)
    {
        av_res = av_read_frame(format_ctx, &dec_packet);
        if(av_res<0)
        {
            qWarning()<<"[CSEL] Error reading packet!";
            if(av_res==AVERROR_EOF)
            {
                qInfo()<<"[CSEL] EOF";
            }
            else if(av_res==AVERROR(ENOMEM))
            {
                qWarning()<<"[CSEL] Out of RAM";
            }
            avcodec_free_context(&video_dec_ctx);
            avformat_close_input(&format_ctx);
            return;
        }
        // Check if it is the video stream.
        if(dec_packet.stream_index==stream_index)
        {
            // It is - quit cycle and go decode.
            break;
        }
        else
        {
            // It was not a video frame.
            // Free up memory from discarded packet.
            av_packet_unref(&dec_packet);
        }
    }
    // Check packet read result.
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Source read is stopped!";
        resetFFMPEG();
        return;
    }
    av_res = avcodec_send_packet(video_dec_ctx, &dec_packet);
    if(av_res<0)
    {
        // There was an error while decoding.
        // Unref frame if it will be not used by [avcodec_receive_frame()].
        qWarning()<<"[CSEL] Error decoding packet!"<<av_res;
        resetFFMPEG();
        return;
    }
    // Read decoded frame from the packet.
    av_res = avcodec_receive_frame(video_dec_ctx, dec_frame);
    if(av_res!=0)
    {
        qWarning()<<"[CSEL] Error recieving packet!"<<av_res;
        resetFFMPEG();
        return;
    }
    // Unreference packet.
    av_packet_unref(&dec_packet);

    av_res = av_image_alloc(video_dst_data, video_dst_linesize, CSEL_PREV_WIDTH, CSEL_PREV_HEIGTH, CSEL_PREV_PIXFMT, 4);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Failed to allocate destination buffer!"<<av_res;
        resetFFMPEG();
        return;
    }
    img_buf_free = false;

    // Setup frame conversion context.
    conv_ctx = sws_getContext(dec_frame->width, dec_frame->height, (AVPixelFormat)dec_frame->format,
                              CSEL_PREV_WIDTH, CSEL_PREV_HEIGTH, CSEL_PREV_PIXFMT,
                              SWS_GAUSS, NULL,
                              NULL, NULL);
    if(conv_ctx==NULL)
    {
        qWarning()<<"[CSEL] Failed to init resize context!";
        resetFFMPEG();
        return;
    }

    av_res = sws_scale(conv_ctx, (const uint8_t * const *)dec_frame->data, dec_frame->linesize, 0, dec_frame->height, video_dst_data, video_dst_linesize);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Failed to resize the frame!"<<av_res;
        resetFFMPEG();
        return;
    }

    // First frame captures and resized successfully.
    // Convert it from FFMPEG format to grayscale QImage.
    QImage conv_image(video_dst_data[0],    // Take only Y-plane for grayscale.
                 video_dst_linesize[0],     // Line length in Y-plane.
                 av_res,
                 video_dst_linesize[0],
                 QImage::Format_Grayscale8);
    // Convert QImage to QPixmap for displaying.
    emit newPreview(QPixmap::fromImage(conv_image));

    // Start polling timer.
    capture_poll.setInterval(1000*v_stream->avg_frame_rate.den/v_stream->avg_frame_rate.num);
    qInfo()<<"[CSEL] Timer polling at"<<capture_poll.interval()<<"ms,"<<(1000/(float)capture_poll.interval())<<"Hz";
    capture_poll.start();
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

//------------------------ Refill device list in the dialog.
void capt_sel::refillDevList(VCapList in_list)
{
    // Remove placeholder.
    ui->listWidget->clear();

    if(in_list.dev_list.size()==0)
    {
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
    ui->btnSave->setEnabled(true);
}

//------------------------ Poll capture device and fetch new frame.
void capt_sel::pollCapture()
{
    // Request new frame from the capture.
    //QtConcurrent::run(this, &capt_sel::getNextFrame);
    getNextFrame();
}

//------------------------ Redraw new image on the preview area.
void capt_sel::redrawPreview(QPixmap in_pixmap)
{
    // Update pixmap.
    pixels->setPixmap(in_pixmap);
    ui->viewport->viewport()->repaint();
}
