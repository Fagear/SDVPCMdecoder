#include "ffmpegwrapper.h"

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


FFMPEGWrapper::FFMPEGWrapper(QObject *parent) : QObject(parent)
{
    format_ctx = NULL;
    video_dec_ctx = NULL;
    v_decoder = NULL;
    stream_index = 0;
    dec_frame = NULL;
    // Init packet container.
    dec_packet.buf = NULL;
    dec_packet.data = NULL;
    dec_packet.size = 0;
    conv_ctx = NULL;
    target_pixfmt = last_frame_fmt = (AVPixelFormat)BUF_FMT_BW;
    img_buf_free = true;
    source_open = false;
    file_capture = true;
    detect_drops = true;
    dts_detected = false;
    last_dts = 0;
    last_double = false;
    last_width = last_height = set_x_ofs = set_y_ofs = 0;
    last_color_mode = vid_preset_t::COLOR_BW;
    set_width = set_height = 0;
    set_fps = 0;
}

//------------------------ Read next packet for video decoder.
bool FFMPEGWrapper::getNextPacket()
{
    int av_res;

    // Cycle untill usable video frame in the packet or EOF.
    // TODO: add watchdog
    while(1)
    {
        //qDebug()<<"[FFWR] Reading...";
        av_res = av_read_frame(format_ctx, &dec_packet);
        if(av_res<0)
        {
            if(av_res==AVERROR_EOF)
            {
                emit sigVideoError(FFMERR_EOF);
            }
            else if(av_res==AVERROR(ENOMEM))
            {
                emit sigVideoError(FFMERR_NO_RAM_READ);
            }
            else
            {
                emit sigVideoError(FFMERR_UNKNOWN);
            }
#ifdef FFWR_EN_DBG_OUT
            if((av_res<0)&&(av_res!=AVERROR_EOF))
            {
                char err_str[256];
                av_strerror(av_res, err_str, 256);
                qWarning()<<DBG_ANCHOR<<"[FFWR] Failed to read a frame:"<<err_str;
            }
#endif
            slotCloseInput();
            return false;
        }
        // Check if it is the selected stream.
        if(dec_packet.stream_index==stream_index)
        {
            //qDebug()<<"[FFWR] Frame found";
            // It is - quit cycle and go decode.
            break;
        }
        else
        {
            //qDebug()<<"[FFWR] No suitable frame yet...";
            // It was not a desirable video frame.
            // Free up memory from discarded packet and continue reading.
            av_packet_unref(&dec_packet);
        }
    }
    //qDebug()<<"[FFWR] Decoding...";
    // Decode the packet.
    av_res = avcodec_send_packet(video_dec_ctx, &dec_packet);
    if(av_res<0)
    {
        // There was an error while decoding.
        if(av_res==AVERROR_EOF)
        {
            emit sigVideoError(FFMERR_EOF);
        }
        else if(av_res==AVERROR(EAGAIN))
        {
            emit sigVideoError(FFMERR_DECODER_REP);
        }
        else if(av_res==AVERROR(EINVAL))
        {
            emit sigVideoError(FFMERR_DECODER_NR);
        }
        else if(av_res==AVERROR(ENOMEM))
        {
            emit sigVideoError(FFMERR_NO_RAM_READ);
        }
        else
        {
            emit sigVideoError(FFMERR_UNKNOWN);
        }
#ifdef FFWR_EN_DBG_OUT
        if((av_res<0)&&(av_res!=AVERROR_EOF))
        {
            char err_str[256];
            av_strerror(av_res, err_str, 256);
            qWarning()<<DBG_ANCHOR<<"[FFWR] Failed to decode a packet:"<<err_str;
        }
#endif
        slotCloseInput();
        return false;
    }
    return true;
}

//------------------------ Has if source has to be resized to double the width.
bool FFMPEGWrapper::needsDoubleWidth(int in_width)
{
    if((file_capture!=false)&&(in_width<MAX_DBL_WIDTH)&&(in_width>MIN_DBL_WIDTH))
    {
        return true;
    }
    return false;
}

//------------------------ Get final frame width.
int FFMPEGWrapper::getFinalWidth(int in_width)
{
    if(needsDoubleWidth(in_width)!=false)
    {
        in_width = in_width*2;
    }
    return in_width;
}

//------------------------ Prepare frame converter.
bool FFMPEGWrapper::initFrameConverter(AVFrame *new_frame, uint16_t new_width, uint16_t new_height, uint8_t new_colors)
{
    int av_res;

#ifdef FFWR_EN_DBG_OUT
    qInfo()<<"[FFWR] Init converter for"<<new_width<<"x"<<new_height;
#endif
    // Setup destination frame buffer.
    if(new_colors==vid_preset_t::COLOR_BW)
    {
        // Setup target buffer for Y plane data pickup.
        target_pixfmt = (AVPixelFormat)BUF_FMT_BW;
        av_res = av_image_alloc(video_dst_data, video_dst_linesize, new_width, new_height, target_pixfmt, 4);
    }
    else
    {
        // Setup target buffer for R/G/B planes data pickup.
        target_pixfmt = (AVPixelFormat)BUF_FMT_COLOR;
        av_res = av_image_alloc(video_dst_data, video_dst_linesize, new_width, new_height, target_pixfmt, 4);
        if(av_res<0)
        {
            // Setup failed, fallback to BW buffer.
#ifdef FFWR_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[FFWR] Failed to allocate RGB destination buffer, falling back to YUV buffer!";
#endif
            target_pixfmt = (AVPixelFormat)BUF_FMT_BW;
            av_res = av_image_alloc(video_dst_data, video_dst_linesize, new_width, new_height, target_pixfmt, 4);
        }
    }
    if(av_res<0)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[FFWR] Failed to allocate destination buffer!";
#endif
        return false;
    }
    // Image buffer was allocated.
    img_buf_free = false;

    // Setup frame conversion context.
    conv_ctx = sws_getContext(new_frame->width, new_frame->height, (AVPixelFormat)new_frame->format,
                              new_width, new_height, target_pixfmt,
                              SWS_GAUSS, NULL, NULL, NULL);

    if(conv_ctx==NULL)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[FFWR] Failed to init image converter context!";
#endif
        return false;
    }
    return true;
}

//------------------------ Check if current final frame conversion paramerers are suitable for the new frame and fix those if not.
bool FFMPEGWrapper::keepFrameInCheck(AVFrame *new_frame)
{
    bool target_double;
    int target_width, target_height;

    target_double = false;
    // Check capture type.
    if(file_capture==false)
    {
        // Device capture is selected.
        if((set_width!=0)&&(set_height!=0))
        {
            // Non-zero capture dimensions are set: use those.
            target_width = set_width;
            target_height = set_height;
        }
        else
        {
            // No valid target size is preset, use default size.
            target_width = DEF_CAP_WIDTH;
            target_height = DEF_CAP_HEIGHT;
        }
    }
    else
    {
        // For file capture set target size the same as the source (except double width).
        target_width = getFinalWidth(new_frame->width);
        target_height = new_frame->height;
        if(needsDoubleWidth(new_frame->width)!=false)
        {
            target_double = true;
        }
    }
    // Check if frame resolution or format has changed.
    if((target_width!=last_width)||(target_height!=last_height)||(crop_n_color.colors!=last_color_mode)||(new_frame->format!=last_frame_fmt))
    {
        // Free up frame converter context.
        freeFrameConverter();
        // Update values for later.
        last_width = target_width;
        last_height = target_height;
        last_double = target_double;
        last_color_mode = crop_n_color.colors;
        last_frame_fmt = (AVPixelFormat)new_frame->format;
        // Initialize frame converter.
        return initFrameConverter(new_frame, last_width, last_height, last_color_mode);
    }
    else
    {
        return true;
    }
}

//------------------------ Free resources used for final frame resizing and converting.
void FFMPEGWrapper::freeFrameConverter()
{
#ifdef FFWR_EN_DBG_OUT
    qInfo()<<"[FFWR] Frame converter freed";
#endif
    last_width = last_height = 0;
    // Free up memory from libsws contexts.
    sws_freeContext(conv_ctx); conv_ctx = NULL;
    // Check if final frame buffer was allocated.
    if(img_buf_free==false)
    {
        // Free up frame buffer memory.
        av_freep(video_dst_data);
        img_buf_free = true;
    }
}

//------------------------ Dump thread ID.
void FFMPEGWrapper::slotLogStart()
{
    qInfo()<<"[FFWR] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

//------------------------ Get list of compatible video capture devices.
void FFMPEGWrapper::slotGetDeviceList()
{
    int av_res;
    AVDictionary *inopt = NULL;
    const AVInputFormat *infmt = NULL;
    AVFormatContext *format_ctx = NULL;

    VCapDevice temp_dev;
    VCapList capture_list;

#ifdef FFWR_EN_DBG_OUT
    qInfo()<<"[FFWR] Video capture devices listing:";
#endif
    // Register all demuxers.
    avdevice_register_all();

    // Get first registered video capture device class and iterate until the last one.
    while((infmt = av_input_video_device_next(infmt)))
    {
        AVDeviceInfoList *dev_list = NULL;
        std::string fmt_name(infmt->name);
        if(fmt_name.compare(FFWR_WIN_GDI_CLASS)==0)
        {
            // Found Windows GDI capture class.
            infmt = av_find_input_format(FFWR_WIN_GDI_CLASS);
            // Avoid listing devices of this class, it's not implemented in FFMPEG.
            // Set capturing parameters.
            av_dict_set(&inopt, "framerate", "60", 0);          // Frame rate
            av_dict_set(&inopt, "video_size", "640x480", 0);    // Size of the capture window.
            // Try to open GDI capture.
            av_res = avformat_open_input(&format_ctx, FFWR_WIN_GDI_CAP, infmt, &inopt);
            // Check result.
            if(av_res>=0)
            {
                // Capture is available.
#ifdef FFWR_EN_DBG_OUT
                qInfo()<<"[FFWR] FFMPEG found"<<infmt->long_name<<"device";
#endif
                // Add device to the list.
                temp_dev.clear();
                temp_dev.dev_name = FFWR_WIN_GDI_NAME.toStdString();
                temp_dev.dev_class = FFWR_WIN_GDI_CLASS;
                temp_dev.dev_path = FFWR_WIN_GDI_CAP;
                capture_list.dev_list.push_back(temp_dev);
                // Close capture for now.
                avformat_close_input(&format_ctx);
            }
        }
        else
        {
            if(fmt_name.compare(FFWR_WIN_DSHOW_CLASS)==0)
            {
                // Construct name for the device.
                fmt_name = "video=";
                fmt_name = fmt_name+FFWR_WIN_DSHOW_CAP;
                // Try to open DirectShow capture.
                av_res = avformat_open_input(&format_ctx, fmt_name.c_str(), infmt, &inopt);
                // Check result.
                if(av_res>=0)
                {
                    // Capture is available.
#ifdef FFWR_EN_DBG_OUT
                    qInfo()<<"[FFWR] FFMPEG found"<<infmt->long_name<<"device";
#endif
                    // Add device to the list.
                    temp_dev.clear();
                    temp_dev.dev_name = FFWR_WIN_DSHOW_NAME.toStdString();
                    temp_dev.dev_class = FFWR_WIN_DSHOW_CAP;
                    temp_dev.dev_path = fmt_name;
                    capture_list.dev_list.push_back(temp_dev);
                    // Close capture for now.
                    avformat_close_input(&format_ctx);
                }
            }

            // Get list of devices for current class.
            // Note: FFMPEG seems only to enumerate inputs for dshow on Windows.
            // Note: FFMPEG v4.x does not implement device listing at all.
            av_res = avdevice_list_input_sources(infmt, NULL, inopt, &dev_list);

            if(av_res>=0)
            {
                // Found some devices.
#ifdef FFWR_EN_DBG_OUT
                qInfo()<<"[FFWR] FFMPEG found"<<av_res<<infmt->long_name<<"devices";
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
#ifdef FFWR_EN_DBG_OUT
                                qInfo()<<"[FFWR] FFMPEG found"<<infmt->long_name<<"device:"<<dev_list->devices[idx]->device_description;
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
#ifdef FFWR_EN_DBG_OUT
                            else
                            {
                                // Error while trying to open capture device.
                                qInfo()<<"[FFWR] FFMPEG"<<infmt->long_name<<"device"<<dev_list->devices[idx]->device_description<<"failed to capture";
                            }
#endif
                        }
#ifdef FFWR_EN_DBG_OUT
                        else
                        {
                            // Current media type is not video.
                            qInfo()<<"[FFWR] FFMPEG"<<infmt->long_name<<"device"<<dev_list->devices[idx]->device_description<<"has no video capture";
                        }
#endif
                    }
#endif  /* LIBAVUTIL_VERSION_MAJOR */
                }
                // Free device list because it was allocated.
                avdevice_free_list_devices(&dev_list);
            }
#ifdef FFWR_EN_DBG_OUT
            else
            {
                // Error, no devices found.
                char err_str[80];
                if(av_strerror(av_res, err_str, 80)==0)
                {
                    qInfo()<<"[FFWR] FFMPEG unable to find"<<infmt->long_name<<"devices!"<<err_str;
                }
                else
                {
                    qInfo()<<"[FFWR] FFMPEG unable to find"<<infmt->long_name<<"devices!";
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

//------------------------ Set source size of the frame for capture (call before [slotOpenInput()]).
void FFMPEGWrapper::slotSetResize(uint16_t in_width, uint16_t in_height)
{
    if((in_height==0)||(in_width==0))
    {
        set_width = set_height = 0;
    }
    else
    {
        set_width = in_width;
        set_height = in_height;
    }
    // Clear last frame dimensions to force calling [initFrameConverter()].
    last_width = last_height = 0;
}

//------------------------ Set source offset from top left for capture (call before [slotOpenInput()]).
void FFMPEGWrapper::slotSetTLOffset(uint16_t in_x, uint16_t in_y)
{
    set_x_ofs = in_x;
    set_y_ofs = in_y;
}

//------------------------ Set source framerate for capture (call before [slotOpenInput()]).
void FFMPEGWrapper::slotSetFPS(uint8_t in_fps)
{
    set_fps = in_fps;
}

//------------------------ Set which color channel (Y/R/G/B) to use and how many pixels to crop.
void FFMPEGWrapper::slotSetCropColor(vid_preset_t in_preset)
{
    if(in_preset.colors<vid_preset_t::COLOR_MAX)
    {
        crop_n_color = in_preset;
    }
#ifdef FFWR_EN_DBG_OUT
    else
    {
        qWarning()<<"[FFWR] Unsupported color channel provided!";
    }
#endif
}

//------------------------ Set if droped frames should be detected.
void FFMPEGWrapper::slotSetDropDetector(bool in_drops)
{
    detect_drops = in_drops;
}

//------------------------ Open video source.
void FFMPEGWrapper::slotOpenInput(QString path, QString class_type)
{
    int av_res;
    uint8_t open_tries;
    std::string device_path, target_class;
    std::string str_fps, str_resolution, str_xofs, str_yofs;
    AVDictionary *def_opts = NULL;
    AVDictionary *size_opts = NULL;
    AVDictionary *size_fps_opts = NULL;
    const AVInputFormat *infmt = NULL;
    AVStream *v_stream;

    // Close previous source if any.
    slotCloseInput();

    // Prepare FFMPEG library.
    avdevice_register_all();

    av_dict_set(&size_opts, "rtbufsize", "100M", 0);
    av_dict_set(&size_fps_opts, "rtbufsize", "200M", 0);
    // Get user set capture parameters and convert those to [AVDictionary] values.
    if(set_fps!=0)
    {
        str_fps = QString::number(set_fps).toStdString();
        // Note: FFMPEG v5.x randomly crashes if gets its [const char *] from QString::toLocal8Bit().constData() but std::string.c_str() works fine.
        av_dict_set(&size_fps_opts, "framerate", str_fps.c_str(), 0);       // Frame rate
    }
    if((set_width!=0)&&(set_height!=0))
    {
        str_resolution = QString::number(set_width).toStdString()+"x"+QString::number(set_height).toStdString();
        av_dict_set(&size_opts, "video_size", str_resolution.c_str(), 0);   // Capture resolution.
        av_dict_set(&size_fps_opts, "video_size", str_resolution.c_str(), 0);
    }
    if(set_x_ofs!=0)
    {
        str_xofs = QString::number(set_x_ofs).toStdString();                // Horizontal offset from the top left corner.
        av_dict_set(&size_opts, "offset_x", str_xofs.c_str(), 0);
        av_dict_set(&size_fps_opts, "offset_x", str_xofs.c_str(), 0);
    }
    if(set_y_ofs!=0)
    {
        str_yofs = QString::number(set_y_ofs).toStdString();                // Vertical offset from the top left corner.
        av_dict_set(&size_opts, "offset_y", str_yofs.c_str(), 0);
        av_dict_set(&size_fps_opts, "offset_y", str_yofs.c_str(), 0);
    }

    device_path = path.toStdString();
    target_class = class_type.toStdString();
#ifdef FFWR_EN_DBG_OUT
    qInfo()<<"[FFWR] Opening source:"<<device_path.c_str();
#endif

    // Open new source.
    if(target_class.empty()==false)
    {
        // Class provided - assumed device capture.
        infmt = av_find_input_format(target_class.c_str());
        file_capture = false;
        open_tries = 3;
        while(open_tries>0)
        {
            open_tries--;
            if(open_tries==0)
            {
                // Third try - with default device's parameters.
                def_opts = NULL;
            }
            else if(open_tries==1)
            {
                // Second try - with some parameters set.
                def_opts = size_opts;
            }
            else
            {
                // First try - with all parameters set.
                def_opts = size_fps_opts;
            }
            // Note: [avformat_open_input()] does NOT open capture device without provided [AVInputFormat]!
            av_res = avformat_open_input(&format_ctx, device_path.c_str(), infmt, &def_opts);
            if(av_res>=0)
            {
                break;
            }
        }
    }
    else
    {
        // No class provided - assumed file capture.
        file_capture = true;
        av_res = avformat_open_input(&format_ctx, device_path.c_str(), NULL, &def_opts);
    }

    if(av_res<0)
    {
#ifdef FFWR_EN_DBG_OUT
        if(av_res!=AVERROR_EOF)
        {
            char err_str[256];
            av_strerror(av_res, err_str, 256);
            qWarning()<<DBG_ANCHOR<<"[FFWR] Unable to open source:"<<err_str;
        }
#endif
        emit sigVideoError(FFMERR_NO_SRC);
        slotCloseInput();
        return;
    }

    // Preset muxer settings.
    format_ctx->flags &= ~(AVFMT_FLAG_SHORTEST|AVFMT_FLAG_FAST_SEEK);   // Clear unwanted flags.
    format_ctx->flags |= (AVFMT_FLAG_AUTO_BSF|AVFMT_FLAG_GENPTS);       // Set required flags.

    // Get stream info.
    av_res = avformat_find_stream_info(format_ctx, NULL);
    if(av_res<0)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<"[FFWR] Unable to get info about video stream!"<<av_res;
#endif
        emit sigVideoError(FFMERR_NO_INFO);
        slotCloseInput();
        return;
    }

    // Find best video stream and save its index.
    stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &v_decoder, 0);
    if(stream_index<0)
    {
        if(stream_index==AVERROR_STREAM_NOT_FOUND)
        {
#ifdef FFWR_EN_DBG_OUT
            qWarning()<<"[FFWR] Unable to find video stream in the source!";
#endif
            emit sigVideoError(FFMERR_NO_VIDEO);
        }
        else if(stream_index==AVERROR_DECODER_NOT_FOUND)
        {
#ifdef FFWR_EN_DBG_OUT
            qWarning()<<"[FFWR] Unable to find decoder for the video stream!";
#endif
            emit sigVideoError(FFMERR_NO_DECODER);
        }
        else
        {
#ifdef FFWR_EN_DBG_OUT
            qWarning()<<"[FFWR] Unknown error while searching for the video stream!"<<av_res;
#endif
            emit sigVideoError(FFMERR_UNKNOWN);
        }
        slotCloseInput();
        return;
    }
    if(v_decoder==NULL)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<"[FFWR] Unable to find decoder for the video stream!";
#endif
        emit sigVideoError(FFMERR_NO_DECODER);
        slotCloseInput();
        return;
    }
    // Select detected stream.
    v_stream = format_ctx->streams[stream_index];

    // TODO: try to find HW-accelerated codecs.
    //codec = avcodec_find_decoder(AV_CODEC_ID_H264);

    // Allocate a codec context for the decoder.
    video_dec_ctx = avcodec_alloc_context3(v_decoder);
    if(video_dec_ctx==NULL)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<"[FFWR] Unable to allocate RAM for video decoder!";
#endif
        emit sigVideoError(FFMERR_NO_RAM_DEC);
        slotCloseInput();
        return;
    }
    //qInfo()<<"Range:"<<video_dec_ctx->color_range;

    // Copy codec parameters from input stream to decoder context.
    av_res = avcodec_parameters_to_context(video_dec_ctx, v_stream->codecpar);
    if(av_res<0)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<"[FFWR] Unable to apply decoder parameters!";
#endif
        emit sigVideoError(FFMERR_DECODER_PARAM);
        slotCloseInput();
        return;
    }

    video_dec_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;       // Allow processing corrupted streams.
    video_dec_ctx->flags2 |= /*AV_CODEC_FLAG2_FAST|*/AV_CODEC_FLAG2_CHUNKS;
    // Try to enable multithreaded single-frame decoding.
    if(QThread::idealThreadCount()>4)
    {
        // Limit decoding thread count to 4 if more than 4 thread CPU is available.
        video_dec_ctx->thread_count = 4;
    }
    else if(QThread::idealThreadCount()>1)
    {
        video_dec_ctx->thread_count = (QThread::idealThreadCount()-1);
    }
    else
    {
        video_dec_ctx->thread_count = 1;
    }
    //video_dec_ctx->thread_type = FF_THREAD_SLICE;
    video_dec_ctx->thread_type = FF_THREAD_FRAME;               // Causes frame decoding out of order

    AVDictionary *dec_opts = NULL;
    // Init the decoders, with or without reference counting.
    av_dict_set(&dec_opts, "refcounted_frames", "1", 0);
    av_res = avcodec_open2(video_dec_ctx, v_decoder, &dec_opts);
    if(av_res<0)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<"[FFWR] Unable to start the decoder!";
#endif
        emit sigVideoError(FFMERR_DECODER_START);
        slotCloseInput();
        return;
    }

    // Allocate frame container.
    dec_frame = av_frame_alloc();
    if(dec_frame==NULL)
    {
#ifdef FFWR_EN_DBG_OUT
        qWarning()<<"[FFWR] Unable to allocate RAM for the frame buffer!";
#endif
        emit sigVideoError(FFMERR_NO_RAM_FB);
        slotCloseInput();
        return;
    }

    // Init packet container.
    dec_packet.buf = NULL;
    dec_packet.data = NULL;
    dec_packet.size = 0;

#ifdef FFWR_EN_DBG_OUT
    QString log_line("[FFWR] Source opened. ");
    log_line += "Codec: "+QString::fromLocal8Bit(v_decoder->name);
    log_line += ", resolution: "+QString::number(v_stream->codecpar->width)+"x"+QString::number(v_stream->codecpar->height);
    log_line += "@"+QString::number(v_stream->avg_frame_rate.num/v_stream->avg_frame_rate.den);
    if(v_stream->codecpar->field_order==AV_FIELD_PROGRESSIVE)
    {
        log_line += ", progressive";
    }
    else if(v_stream->codecpar->field_order==AV_FIELD_TT)
    {
        log_line += ", interlaced TFF (decode: TFF)";
    }
    else if(v_stream->codecpar->field_order==AV_FIELD_BB)
    {
        log_line += ", interlaced BFF (decode: BFF)";
    }
    else if(v_stream->codecpar->field_order==AV_FIELD_TB)
    {
        log_line += ", interlaced TFF (decode: BFF)";
    }
    else if(v_stream->codecpar->field_order==AV_FIELD_BT)
    {
        log_line += ", interlaced BFF (decode: TFF)";
    }
    qInfo()<<log_line;
#endif
    // Source is opened and ready.
    source_open = true;
    frame_count = 1;
    emit sigInputReady(v_stream->codecpar->width, v_stream->codecpar->height, (uint32_t)v_stream->nb_frames, (v_stream->avg_frame_rate.num/v_stream->avg_frame_rate.den));
}

//------------------------ Read next frame from the stream.
void FFMPEGWrapper::slotGetNextFrame()
{
    int av_res;
    int64_t dts_diff;

#ifdef FFWR_EN_DBG_OUT
    //qDebug()<<"[FFWR] New frame requested";
#endif
    // Check if all stages of opening a source are completed and there is a final buffer.
    if(source_open==false)
    {
        emit sigVideoError(FFMERR_NOT_INIT);
        return;
    }

    // Get next packet from the stram.
    if(getNextPacket()==false) return;

    //qDebug()<<"[FFWR] Receiving from decoder...";
    // Cycle through all the data in the packet.
    do
    {
        //qDebug()<<"[FFWR] Reading frame...";
        // Read data from the packet.
        av_res = avcodec_receive_frame(video_dec_ctx, dec_frame);
        if(av_res==0)
        {
            //qDebug()<<"[FFWR] Received a frame";
            // Got decoded frame from the packet.
            vid_preset_t crop_settings;
            crop_settings = crop_n_color;
            // Check frame line count.
            if(dec_frame->height>MAX_LINES_PER_FRAME)
            {
                // Force bottom cropping.
                crop_settings.crop_bottom = (dec_frame->height-MAX_LINES_PER_FRAME);
            }
            // Crop the frame if required.
            if((crop_settings.crop_left!=0)||(crop_settings.crop_right!=0)||(crop_settings.crop_top!=0)||(crop_settings.crop_bottom!=0))
            {
                // Set crop parameters.
                dec_frame->crop_top = crop_settings.crop_top;
                dec_frame->crop_bottom = crop_settings.crop_bottom;
                dec_frame->crop_left = crop_settings.crop_left;
                dec_frame->crop_right = crop_settings.crop_right;
                // Apply cropping.
                av_res = av_frame_apply_cropping(dec_frame, AV_FRAME_CROP_UNALIGNED);
                if(av_res<0)
                {
                    qWarning()<<DBG_ANCHOR<<"[FFWR] Frame crop failed!";
                }
            }
            // Check is drop detection is enabled.
            if(detect_drops!=false)
            {
                //qDebug()<<"[FFWR] DTS:"<<dec_frame->pkt_dts<<"| Last DTS:"<<last_dts<<"| Duration:"<<dec_frame->pkt_duration<<"| Stamp:"<<dec_frame->best_effort_timestamp<<"| Base:"<<((float)dec_frame->time_base.num/dec_frame->time_base.den);
                // Frame dropout detector is active.
                if(dts_detected==false)
                {
                    // It's the first frame since dropout detector was activated.
                    dts_detected = true;
                    // Store DTS of the first decoded frame.
                    last_dts = dec_frame->pkt_dts;
                    dts_diff = 0;
                }
                else
                {
                    // Calculate DTS difference between current and last frames.
                    dts_diff = (dec_frame->pkt_dts-last_dts)-dec_frame->pkt_duration;
                }
                // Save new "last DTS".
                last_dts = dec_frame->pkt_dts;
                // Check if current DTS is one step after the previous one.
                if((dts_diff>0)&&(dec_frame->pkt_duration>0))
                {
                    uint32_t missed_frames;
                    // Calculate how many frames were missed.
                    missed_frames = (uint32_t)dts_diff/dec_frame->pkt_duration;
                    //qDebug()<<"[FFWR] Droped"<<missed_frames<<"frames! DTS:"<<dec_frame->pkt_dts<<"Diff.:"<<dts_diff<<"| Duration:"<<dec_frame->pkt_duration;
                    // Limit maximum number of dropped frames.
                    if(missed_frames>DUMMY_CNT_MAX)
                    {
                        qWarning()<<DBG_ANCHOR<<"[FFWR] Maximum number of dropped frames exceeded!"<<missed_frames<<DUMMY_CNT_MAX;
                        missed_frames = DUMMY_CNT_MAX;
                    }
                    // Report missed frames.
                    // Encode number of dropped frame in the width of the image.
                    QImage dummy_image(missed_frames, DUMMY_HEIGTH, QImage::Format_Grayscale8);
                    // Send QImage for displaying.
                    emit newImage(dummy_image.copy(), last_double);
                }
            }
            else
            {
                // Reset DTS difference detector.
                dts_detected = false;
            }
            //qDebug()<<"[FFWR] Frame size:"<<dec_frame->width<<"x"<<dec_frame->height;
            // Make sure that frame converter is setup properly.
            if(keepFrameInCheck(dec_frame)==false)
            {
                emit sigVideoError(FFMERR_CONV_INIT);
                slotCloseInput();
                return;
            }

            // Convert frame to grayscale (and resize if required).
            av_res = sws_scale(conv_ctx, (const uint8_t * const *)dec_frame->data, dec_frame->linesize, 0, dec_frame->height, video_dst_data, video_dst_linesize);
            if(av_res<0)
            {
                emit sigVideoError(FFMERR_FRM_CONV);
                slotCloseInput();
                return;
            }
            //qDebug()<<"[FFWR] Resized to size:"<<last_width<<"x"<<last_height<<"with result:"<<av_res;
            // Check target pixel format.
            if(target_pixfmt==(AVPixelFormat)BUF_FMT_BW)
            {
                // Y-channel selected.
                // Convert frame from FFMPEG format to grayscale QImage.
                QImage conv_image(video_dst_data[PLANE_Y],      // Take only Y-plane for grayscale.
                             video_dst_linesize[PLANE_Y],       // Line length in Y-plane.
                             av_res,                            // Height of the output slice.
                             video_dst_linesize[PLANE_Y],
                             QImage::Format_Grayscale8);
                // Send QImage for displaying.
                emit newImage(conv_image.copy(), last_double);
            }
            else
            {
                // One of the primary color channels is selected.
                if(crop_n_color.colors==vid_preset_t::COLOR_R)
                {
                    // RED channel selected.
                    // Convert frame from FFMPEG format to grayscale QImage.
                    QImage conv_image(video_dst_data[PLANE_R],  // Take only RED-plane for grayscale.
                                 video_dst_linesize[PLANE_R],   // Line length in RED-plane.
                                 av_res,                        // Height of the output slice.
                                 video_dst_linesize[PLANE_R],
                                 QImage::Format_Grayscale8);
                    // Send QImage for displaying.
                    emit newImage(conv_image.copy(), last_double);
                }
                else if(crop_n_color.colors==vid_preset_t::COLOR_B)
                {
                    // BLUE channel selected.
                    // Convert frame from FFMPEG format to grayscale QImage.
                    QImage conv_image(video_dst_data[PLANE_B],  // Take only BLUE-plane for grayscale.
                                 video_dst_linesize[PLANE_B],   // Line length in BLUE-plane.
                                 av_res,                        // Height of the output slice.
                                 video_dst_linesize[PLANE_B],
                                 QImage::Format_Grayscale8);
                    // Send QImage for displaying.
                    emit newImage(conv_image.copy(), last_double);
                }
                else
                {
                    // GREEN channel selected.
                    // Convert frame from FFMPEG format to grayscale QImage.
                    QImage conv_image(video_dst_data[PLANE_G],  // Take only GREEN-plane for grayscale.
                                 video_dst_linesize[PLANE_G],   // Line length in GREEN-plane.
                                 av_res,                        // Height of the output slice.
                                 video_dst_linesize[PLANE_G],
                                 QImage::Format_Grayscale8);
                    // Send QImage for displaying.
                    emit newImage(conv_image.copy(), last_double);
                }
            }
            //qDebug()<<"[FFWR] Emitted frame #"<<frame_count;
            frame_count++;
        }
        else if(av_res==AVERROR(EAGAIN))
        {
            //qDebug()<<"[FFWR] Not everything yet...";
            // Not enough data to get the frame, need more data.
            if(getNextPacket()==false) return;
        }
    }
    while((av_res==0)||(av_res==AVERROR(EAGAIN)));
    // Free up memory from processed packet.
    av_packet_unref(&dec_packet);
}

//------------------------ Close video source and free all resources.
void FFMPEGWrapper::slotCloseInput()
{
#ifdef FFWR_EN_DBG_OUT
    qInfo()<<"[FFWR] FFMPEG is being reset...";
#endif
    freeFrameConverter();
    // Free up decoder resources.
    if(dec_frame!=NULL)
    {
        av_frame_free(&dec_frame); dec_frame = NULL;
    }
    // Free up packet resources.
    if(dec_packet.buf!=NULL)
    {
        av_packet_unref(&dec_packet);
    }
    // Free up video decoder resources.
    if(video_dec_ctx!=NULL)
    {
        avcodec_free_context(&video_dec_ctx); video_dec_ctx = NULL;
    }
    // Free up input context.
    if(format_ctx!=NULL)
    {
        avformat_close_input(&format_ctx); format_ctx = NULL;
    }
    v_decoder = NULL;
    if(source_open!=false)
    {
        source_open = false;
#ifdef FFWR_EN_DBG_OUT
        qInfo()<<"[FFWR] Source is closed";
#endif
        // Report about closed source.
        emit sigInputClosed();
    }
}
