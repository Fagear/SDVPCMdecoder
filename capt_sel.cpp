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
capt_sel::capt_sel(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::capt_sel)
{
    ui->setupUi(this);

    scene = NULL;
    scene = new QGraphicsScene(this);
    ui->viewport->setScene(scene);
    ui->viewport->setAlignment(Qt::AlignLeft|Qt::AlignTop);

    connect(this, SIGNAL(newDeviceList(QVector<VCapDevice>)), this, SLOT(refillDevList(QVector<VCapDevice>)));
    connect(ui->btnClose, SIGNAL(clicked(bool)), this, SLOT(usrClose()));
    connect(ui->btnSave, SIGNAL(clicked(bool)), this, SLOT(usrSave()));
    connect(ui->btnRefresh, SIGNAL(clicked(bool)), this, SLOT(usrRefresh()));
    connect(ui->listWidget, SIGNAL(currentRowChanged(int)), this, SLOT(selectDevice(int)));

    qInfo()<<"[CSEL] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

capt_sel::~capt_sel()
{
    qInfo()<<"[CSEL] Dialog destroyed";
    delete ui;
}

//------------------------ Dialog about to appear.
int capt_sel::exec()
{
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
    QVector<VCapDevice> capture_list;
    format_ctx = avformat_alloc_context();

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
        if(fmt_name.compare(VIP_WIN_GDI_CLASS)==0)
        {
            // Found Windows DGI capture class.
            infmt = av_find_input_format(VIP_WIN_GDI_CLASS);
            // Avoid listing devices of this class, it's not implemented in FFMPEG.
            // Set capturing parameters.
            av_dict_set(&inopt, "framerate", "60", 0);          // Frame rate
            av_dict_set(&inopt, "offset_x", "20", 0);           // Horizontal offset from the top left corner.
            av_dict_set(&inopt, "offset_y", "30", 0);           // Vertical offset from the top left corner.
            av_dict_set(&inopt, "video_size", "640x480", 0);    // Size of the capture window.
            // Try to open GDI capture.
            av_res = avformat_open_input(&format_ctx, VIP_WIN_GDI_CAP, infmt, &inopt);
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
                temp_dev.dev_name = VIP_WIN_GDI_NAME;
                temp_dev.dev_class = VIP_WIN_GDI_CLASS;
                temp_dev.dev_path = VIP_WIN_GDI_CAP;
                capture_list.push_back(temp_dev);
                // Close capture for now.
                avformat_close_input(&format_ctx);
            }
            // Clear parameters.
            av_dict_free(&inopt);
        }
        else
        {
            if(fmt_name.compare(VIP_WIN_DSHOW_CLASS)==0)
            {
                // Construct name for the device.
                fmt_name = "video=";
                fmt_name = fmt_name+VIP_WIN_DSHOW_CAP;
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
                    temp_dev.dev_name = VIP_WIN_DSHOW_NAME;
                    temp_dev.dev_class = VIP_WIN_DSHOW_CAP;
                    temp_dev.dev_path = fmt_name;
                    capture_list.push_back(temp_dev);
                    // Close capture for now.
                    avformat_close_input(&format_ctx);
                }
            }
            // Get list of devices for current class.
            // Note: FFMPEG seems only to enumerate inputs for dshow on Windows.
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
                                //avformat_find_stream_info();
                                //avcodec_open2();
                                // Add device to the list.
                                temp_dev.clear();
                                temp_dev.dev_name = dev_list->devices[idx]->device_description;
                                temp_dev.dev_class = infmt->name;
                                temp_dev.dev_path = fmt_name;
                                capture_list.push_back(temp_dev);
                                // Close capture for now.
                                avformat_close_input(&format_ctx);
                            }
#ifdef VIP_EN_DBG_OUT
                            else
                            {
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
                            if(suppress_log==false)
                            {
                                qInfo()<<"[CSEL] FFMPEG"<<infmt->long_name<<"device"<<dev_list->devices[idx]->device_description<<"has no video capture";
                            }
                        }
#endif
                    }
                }
                // Free device list because it was allocated.
                avdevice_free_list_devices(&dev_list);
            }
#ifdef VIP_EN_DBG_OUT
            else
            {
                if(suppress_log==false)
                {
                    // Error, no devices found.
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
    avformat_free_context(format_ctx);

    emit newDeviceList(capture_list);
}

void capt_sel::setChange()
{

}

void capt_sel::usrRefresh()
{
    // Clear device list and block it.
    ui->listWidget->clear();
    ui->listWidget->setEnabled(false);
    ui->viewport->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
    ui->btnSave->setEnabled(false);
    QListWidgetItem *new_line = new QListWidgetItem;
    new_line->setText(tr("Список обновляется..."));
    ui->listWidget->addItem(new_line);
    // Request capture devices list.
    //QtConcurrent::run(this, &capt_sel::getVideoCaptureList);
    getVideoCaptureList();
}

void capt_sel::usrSave()
{

}

void capt_sel::usrClose()
{
    this->close();
}

void capt_sel::selectDevice(int in_row)
{
    int av_res;
    int stream_index;
    QString dev_path;
    AVDictionary *opts = NULL;
    AVFormatContext *format_ctx = NULL;
    const AVInputFormat *infmt = NULL;
    const AVCodec *v_decoder = NULL;
    AVCodecContext *video_dec_ctx;
    AVStream *v_stream;
    AVPacket dec_packet;
    AVFrame *dec_frame;
    SwsContext *conv_ctx;
    uint8_t *video_dst_data[4];         // Container for frame data from the decoder.
    int video_dst_linesize[4];          // Container for frame parameters from the decoder.

    dev_path = ui->listWidget->item(in_row)->data(Qt::UserRole).toString();

    avdevice_register_all();

    qInfo()<<"[CSEL] Opening capture device:"<<dev_path.toLocal8Bit().constData();
    // Open a new source with FFMPEG.
    infmt = av_find_input_format(ui->listWidget->item(in_row)->data(Qt::ToolTipRole).toString().toLocal8Bit().constData());
    av_res = avformat_open_input(&format_ctx, dev_path.toLocal8Bit().constData(), infmt, NULL);
    if(av_res<0)
    {
        char err_str[80];
        if(av_strerror(av_res, err_str, 80)==0)
        {
            qWarning()<<"[CSEL] Unable to open source!"<<err_str;
        }
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
        return;
    }

    // Find best video stream.
    stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &v_decoder, 0);
    if(stream_index<0)
    {
        if(stream_index==AVERROR_STREAM_NOT_FOUND)
        {
            qWarning()<<"[CSEL] Unable to find video stream in the source!";
            return;
        }
        else if(stream_index==AVERROR_DECODER_NOT_FOUND)
        {
            qWarning()<<"[CSEL] Unable to find decoder for the video stream!";
            return;
        }
        else
        {
            qWarning()<<"[CSEL] Unknown error while searching for the video stream!"<<av_res;
            return;
        }
    }

    // Allocate a codec context for the decoder.
    video_dec_ctx = avcodec_alloc_context3(v_decoder);
    if(video_dec_ctx==NULL)
    {
        qWarning()<<"[CSEL] Unable to allocate RAM for decoder!";
        return;
    }
    // Select detected stream.
    v_stream = format_ctx->streams[stream_index];

    // Copy codec parameters from input stream to decoder context.
    av_res = avcodec_parameters_to_context(video_dec_ctx, v_stream->codecpar);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Unable to apply decoder parameters!";
        return;
    }

    video_dec_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    video_dec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;

    // Init the decoders, with or without reference counting.
    av_dict_set(&opts, "refcounted_frames", "1", 0);
    av_res = avcodec_open2(video_dec_ctx, v_decoder, &opts);
    if(av_res<0)
    {
        qWarning()<<"[CSEL] Unable to start the decoder!";
        return;
    }

    // Allocate frame container.
    dec_frame = av_frame_alloc();
    if(dec_frame==NULL)
    {
        qWarning()<<"[CSEL] Unable to allocate RAM for the frame buffer!";
        return;
    }

    // Cycle till read video frame or EOF.
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
    if(av_res>=0)
    {
        av_res = avcodec_send_packet(video_dec_ctx, &dec_packet);
        if(av_res<0)
        {
            // There was an error while decoding.
            // Unref frame if it will be not used by [avcodec_receive_frame()].
            av_packet_unref(&dec_packet);
            qWarning()<<"[CSEL] Error decoding packet!"<<av_res;
            return;
        }
        // Read decoded frame from the packet.
        av_res = avcodec_receive_frame(video_dec_ctx, dec_frame);
        if(av_res!=0)
        {
            qWarning()<<"[CSEL] Error recieving packet!"<<av_res;
            return;
        }

        av_res = av_image_alloc(video_dst_data, video_dst_linesize, 640, 480, AV_PIX_FMT_YUV420P, 4);
        if(av_res<0)
        {
            qWarning()<<"[CSEL] Failed to allocate destination buffer!"<<av_res;
            return;
        }

        // Setup frame conversion context.
        conv_ctx = sws_getContext(dec_frame->width, dec_frame->height, (AVPixelFormat)dec_frame->format,
                                  640, 480, AV_PIX_FMT_YUV420P,
                                  SWS_GAUSS, NULL,
                                  NULL, NULL);
        if(conv_ctx==NULL)
        {
            qWarning()<<"[CSEL] Failed to init resize context!";
            return;
        }

        av_res = sws_scale(conv_ctx, (const uint8_t * const *)dec_frame->data, dec_frame->linesize, 0, dec_frame->height, video_dst_data, video_dst_linesize);
        if(av_res<0)
        {
            qWarning()<<"[CSEL] Failed to resize the frame!"<<av_res;
            return;
        }

        QImage image(dec_frame->data[0],
                     video_dec_ctx->width,
                     video_dec_ctx->height,
                     dec_frame->linesize[0],
                     QImage::Format_RGB888);

        QGraphicsPixmapItem *pixels;

        pixels = new QGraphicsPixmapItem(QPixmap::fromImage(image));
        pixels->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
        scene->addItem(pixels);
    }
    // Free up resources.
    av_frame_free(&dec_frame);
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&format_ctx);
}

void capt_sel::refillDevList(QVector<VCapDevice> in_list)
{
    // Remove placeholder.
    ui->listWidget->clear();
    // Refill device list.
    for(int idx=0;idx<in_list.size();idx++)
    {
        QListWidgetItem *new_line = new QListWidgetItem;
        new_line->setText(QString::fromStdString(in_list[idx].dev_name));
        new_line->setData(Qt::ToolTipRole, QString::fromStdString(in_list[idx].dev_class));
        new_line->setData(Qt::UserRole, QVariant(QString::fromStdString(in_list[idx].dev_path)));
        ui->listWidget->addItem(new_line);
    }
    if(in_list.size()==0)
    {
        QListWidgetItem *new_line = new QListWidgetItem;
        new_line->setText(tr("Нет устройств захвата видео"));
        ui->listWidget->addItem(new_line);
    }
    else
    {
        // Unblock list.
        ui->listWidget->setEnabled(true);
        ui->viewport->setEnabled(true);
    }
    // Unblock buttons.
    ui->btnRefresh->setEnabled(true);
    ui->btnSave->setEnabled(true);
}
