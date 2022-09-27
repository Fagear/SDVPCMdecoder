#include "vin_ffmpeg.h"

static enum AVPixelFormat hw_fmt;                   // Frame format for HW decoder.

//------------------------
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *in_fmts)
{
    Q_UNUSED(ctx);

    const enum AVPixelFormat *found_fmt;

    for(found_fmt=in_fmts;*found_fmt!=-1;found_fmt++)
    {
        if(*found_fmt==hw_fmt)
        {
            return *found_fmt;
        }
    }

    qWarning()<<DBG_ANCHOR<<"Failed to get HW surface format in [get_hw_format()]";
    return AV_PIX_FMT_NONE;
}

VideoInFFMPEG::VideoInFFMPEG(QObject *parent) : QObject(parent)
{
    log_level = 0;
    proc_state = STG_IDLE;
    new_state = STG_IDLE;
    frame_advancing = ADV_CONST;
    target_pixfmt = VIP_BUF_FMT_BW;
    last_real_width = 0;
    last_width = 0;
    last_height = 0;
    last_color_mode = vid_preset_t::COLOR_BW;
    frame_counter = 1;
    last_dts = 0;
    src_path.clear();
    out_lines = NULL;
    mtx_lines = NULL;
    format_ctx = NULL;
    video_dec_ctx = NULL;
    hw_dev_ctx = NULL;
    hw_type = AV_HWDEVICE_TYPE_NONE;
    hw_fmt = AV_PIX_FMT_NONE;
    dec_frame = NULL;
    conv_ctx = NULL;
    video_dst_data[0] = NULL;
    stream_index = 0;
    setDefaultFineSettings();
    step_play = false;
    detect_frame_drop = false;
    dts_detect = false;
    finish_work = false;
}

//------------------------ Throw an error if playback is not possible for some reason.
uint8_t VideoInFFMPEG::failToPlay(QString in_err)
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        if(in_err.isEmpty()==false)
        {
            qWarning()<<DBG_ANCHOR<<"[VIP]"<<in_err;
        }
        else
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] Some unknown error occured!";
        }
    }
#endif
    if(in_err.isEmpty()!=false)
    {
        in_err = tr("Неизвестная ошибка");
    }
    // Store error description.
    last_error_txt = in_err;
    return STG_IDLE;
}

//------------------------
void VideoInFFMPEG::initHWDecoder(AVCodecContext *in_dec_ctx)
{
    qInfo()<<"[VIP] Compatible HW decoder:"<<av_hwdevice_get_type_name(hw_type)<<hw_type;
    int ret = av_hwdevice_ctx_create(&hw_dev_ctx, hw_type, NULL, NULL, 0);
    if(ret>=0)
    {
        qInfo()<<"[VIP] HW device init ok";
        in_dec_ctx->get_format = get_hw_format;
        in_dec_ctx->hw_device_ctx = av_buffer_ref(hw_dev_ctx);
    }
    else
    {
        qInfo()<<"[VIP] HW failed"<<ret;
    }
}

//------------------------ Try to get hardware decoder for the input.
void VideoInFFMPEG::findHWDecoder(AVCodec *in_codec)
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        AVHWDeviceType hwtype = AV_HWDEVICE_TYPE_NONE;
        do
        {
            hwtype = av_hwdevice_iterate_types(hwtype);
            if(hwtype!=AV_HWDEVICE_TYPE_NONE)
            {
                qInfo()<<"[VIP] Supported HW decoder:"<<av_hwdevice_get_type_name(hwtype);
            }
        }
        while(hwtype!=AV_HWDEVICE_TYPE_NONE);
    }
#endif

    int i;
    i = 0;
    // Reset HW data (preset "no HW decoding").
    hw_dev_ctx = NULL;
    hw_type = AV_HWDEVICE_TYPE_NONE;
    hw_fmt = AV_PIX_FMT_NONE;
    while(1)
    {
        const AVCodecHWConfig *hw_config;
        hw_config = avcodec_get_hw_config(in_codec, i);
        i++;
        if(hw_config!=NULL)
        {
            if(hw_config->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            {
                // Save HW pixel format.
                hw_fmt = hw_config->pix_fmt;
                hw_type = hw_config->device_type;
                qInfo()<<"[VIP] HW decoder found";
                break;
            }
            else
            {
                qInfo()<<"[VIP]"<<in_codec->name<<"does not support"<<av_hwdevice_get_type_name(hw_config->device_type);
            }
        }
        else
        {
            break;
        }
    }
}

//------------------------ Do everything to prepare FFMPEG decoder.
uint8_t VideoInFFMPEG::decoderInit()
{
    int av_res, refcount;
    AVStream *v_stream;
    AVCodec *v_decoder = NULL;
    AVDictionary *opts = NULL;

    // Reset frame counter.
    frame_advancing = ADV_CONST;
    frame_counter = 1;

    // Reset DTS drop detection.
    last_dts = 0;
    dts_detect = false;

    refcount = 1;

    // Prepare FFMPEG library.
    format_ctx = avformat_alloc_context();
    if(!format_ctx)
    {
        new_state = failToPlay(tr("Не удалось выделить память для контекста FFMPEG"));
        return VIP_RET_NO_DEC_CTX;
    }

    // Open a new source with FFMPEG.
    av_res = avformat_open_input(&format_ctx, src_path.toLocal8Bit().constData(), NULL, NULL);
    if(av_res<0)
    {
        new_state = failToPlay(tr("Не удалось открыть источник"));
        return VIP_RET_NO_SRC;
    }
    // Preset muxer settings.
    format_ctx->flags &= ~(AVFMT_FLAG_SHORTEST|AVFMT_FLAG_FAST_SEEK);   // Clear unwanted flags.
    format_ctx->flags |= (AVFMT_FLAG_AUTO_BSF|AVFMT_FLAG_GENPTS);       // Set required flags.

    //qInfo()<<"[VIP] Error flags:"<<format_ctx->error_recognition;
    //av_dump_format(format_ctx, 0, src_path.toLocal8Bit().constData(), 0);

    // Get stream info.
    av_res = avformat_find_stream_info(format_ctx, NULL);
    if(av_res<0)
    {
        new_state = failToPlay(tr("Не удалось определить информацию о потоках источника"));
        return VIP_RET_NO_STREAM;
    }

    // Find best video stream.
    stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &v_decoder, 0);
    if(stream_index<0)
    {
        if(stream_index==AVERROR_STREAM_NOT_FOUND)
        {
            new_state = failToPlay(tr("Не удалось найти видео-поток в источнике"));
            return VIP_RET_NO_STREAM;
        }
        else if(stream_index==AVERROR_DECODER_NOT_FOUND)
        {
            new_state = failToPlay(tr("Не удалось найти декодер для видео-потока"));
            return VIP_RET_NO_DECODER;
        }
        else
        {
            new_state = failToPlay(tr("Неизвестная ошибка при поиске видео-потока в источнике, код: ")+QString::number(av_res));
            return VIP_RET_UNK_STREAM;
        }
    }
    if(v_decoder==NULL)
    {
        new_state = failToPlay(tr("Не удалось найти декодер для видео-потока"));
        return VIP_RET_NO_DECODER;
    }

    // Try to get hardware to decode it.
    // TODO: maybe add hardware video decoding
    //findHWDecoder(v_decoder);

    // Allocate a codec context for the decoder.
    video_dec_ctx = avcodec_alloc_context3(v_decoder);
    if(video_dec_ctx==NULL)
    {
        new_state = failToPlay(tr("Не удалось выделить память для контекста декодера"));
        return VIP_RET_NO_DEC_CTX;
    }
    // Select detected stream.
    v_stream = format_ctx->streams[stream_index];

    // Copy codec parameters from input stream to decoder context.
    av_res = avcodec_parameters_to_context(video_dec_ctx, v_stream->codecpar);
    if(av_res<0)
    {
        new_state = failToPlay(tr("Не удалось применить параметры декодера"));
        return VIP_RET_ERR_DEC_PARAM;
    }

    if(hw_fmt!=AV_PIX_FMT_NONE)
    {
        initHWDecoder(video_dec_ctx);
    }

    video_dec_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    //video_dec_ctx->flags |= AV_CODEC_FLAG_GRAY;
    //video_dec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    video_dec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;
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
    video_dec_ctx->thread_type = FF_THREAD_FRAME;       // causes frame decoding out of order
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        QString log_line;
        log_line = "[VIP] Threads available: "+QString::number(QThread::idealThreadCount())
                   +", using "+QString::number(video_dec_ctx->thread_count)+" threads to decode video";
        qInfo()<<log_line;
    }
#endif

    // Init the decoders, with or without reference counting.
    av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);
    av_res = avcodec_open2(video_dec_ctx, v_decoder, &opts);
    if(av_res<0)
    {
        new_state = failToPlay(tr("Не удалось запустить декодер видео"));
        return VIP_RET_ERR_DEC_START;
    }

    // Allocate frame container.
    dec_frame = av_frame_alloc();
    if(dec_frame==NULL)
    {
        new_state = failToPlay(tr("Не удалось выделить память для буфера кадра"));
        return VIP_RET_NO_FRM_BUF;
    }

    // Init packet container.
    av_init_packet(&dec_packet);
    dec_packet.data = NULL;
    dec_packet.size = 0;

#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        QString log_line("[VIP] Found video");
        log_line += ", pixel format ID: " + QString::number(v_stream->codecpar->format);
        if(v_stream->codecpar->field_order==AV_FIELD_PROGRESSIVE)
        {
            log_line += ", progressive.";
        }
        else if(v_stream->codecpar->field_order==AV_FIELD_TT)
        {
            log_line += ", TFF (decode: TFF)";
        }
        else if(v_stream->codecpar->field_order==AV_FIELD_BB)
        {
            log_line += ", BFF (decode: BFF)";
        }
        else if(v_stream->codecpar->field_order==AV_FIELD_TB)
        {
            log_line += ", TFF (decode: BFF)";
        }
        else if(v_stream->codecpar->field_order==AV_FIELD_BT)
        {
            log_line += ", BFF (decode: TFF)";
        }
        qInfo()<<log_line;
        if(hw_fmt==AV_PIX_FMT_NONE)
        {
            qInfo()<<"[VIP] Using codec:"<<v_decoder->name<<"(SW)";
        }
        else
        {
            qInfo()<<"[VIP] Using codec:"<<v_decoder->name<<"(HW)";
        }
        // v_stream->codecpar->field_order
        // v_stream->codecpar->request_sample_fmt

        qInfo()<<"[VIP] New source:"<<video_dec_ctx->width<<"x"<<video_dec_ctx->height;
    }
#endif
    return VIP_RET_OK;
}

//------------------------ Free decoder resources.
void VideoInFFMPEG::decoderStop()
{
    // Free up resources.
    av_frame_free(&dec_frame);
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&format_ctx);
    if(hw_fmt==AV_PIX_FMT_NONE)
    {
        av_buffer_unref(&hw_dev_ctx);
    }
}

//------------------------ Clip bottom of the frame if does not fit into the buffer.
uint16_t VideoInFFMPEG::getFinalHeight(uint16_t in_height)
{
    uint16_t new_height;
    if(in_height>MAX_VLINE_QUEUE_SIZE)
    {
        new_height = MAX_VLINE_QUEUE_SIZE-10;
    }
    else
    {
        new_height = in_height;
    }
    return new_height;
}

//------------------------ Resize frame to a wider one.
uint16_t VideoInFFMPEG::getFinalWidth(uint16_t in_width)
{
    uint16_t new_width;
    if(hasWidthDoubling(in_width)==false)
    {
        new_width = in_width;
    }
    else
    {
        new_width = in_width*2;
    }
    return new_width;
}

//------------------------ Check if provided width should be doubled.
bool VideoInFFMPEG::hasWidthDoubling(uint16_t in_width)
{
    if(in_width<MAX_DOUBLE_WIDTH)
    {
        if(in_width>MIN_DOUBLE_WIDTH)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

//------------------------ Init libsws.
bool VideoInFFMPEG::initFrameConverter(AVFrame *new_frame, uint16_t new_width, uint16_t new_height, uint8_t new_colors)
{
    int av_res;

#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_FRAME)!=0)
    {
        qInfo()<<"[VIP] New AVPixelFormat:"<<new_frame->format;
    }
#endif
    // Setup destination frame buffer.
    if(new_colors==vid_preset_t::COLOR_BW)
    {
        // Setup target buffer for Y plane data pickup.
        target_pixfmt = VIP_BUF_FMT_BW;
        av_res = av_image_alloc(video_dst_data, video_dst_linesize, new_width, new_height, target_pixfmt, 4);
    }
    else
    {
        // Setup target buffer for R/G/B planes data pickup.
        target_pixfmt = VIP_BUF_FMT_COLOR;
        av_res = av_image_alloc(video_dst_data, video_dst_linesize, new_width, new_height, target_pixfmt, 4);
        if(av_res<0)
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] Failed to allocate RGB destination buffer in [VideoInFFMPEG::initFrameConverter()], falling back to YUV buffer!";
            target_pixfmt = VIP_BUF_FMT_BW;
            av_res = av_image_alloc(video_dst_data, video_dst_linesize, new_width, new_height, target_pixfmt, 4);
        }
    }
    if(av_res<0)
    {
        qWarning()<<DBG_ANCHOR<<"[VIP] Failed to allocate destination buffer in [VideoInFFMPEG::initFrameConverter()]!";
        return false;
    }

    // Setup frame conversion context.
    conv_ctx = sws_getContext(new_frame->width, new_frame->height, (AVPixelFormat)new_frame->format,
                              new_width, new_height, target_pixfmt,
                              SWS_GAUSS, NULL,
                              NULL, NULL);

    if(conv_ctx==NULL)
    {
        qWarning()<<DBG_ANCHOR<<"[VIP] Failed to init resize context in [VideoInFFMPEG::initFrameConverter()]!";
        return false;
    }
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_FRAME)!=0)
    {
        qInfo()<<"[VIP] Destination buffer"<<video_dst_linesize[0]<<"x"<<new_height<<"is ready.";
    }
#endif
    return true;
}

//------------------------ End work with libsws.
void VideoInFFMPEG::freeFrameConverter()
{
    // Free up memory from libsws contexts.
    sws_freeContext(conv_ctx);
    av_freep(video_dst_data);
}

//------------------------ Check new frame dimensions and update internal structures to stay in check.
bool VideoInFFMPEG::keepFrameInCheck(AVFrame *new_frame, uint16_t new_width, uint16_t new_height, uint8_t new_colors)
{
    bool result;
    result = false;
    if(new_frame!=NULL)
    {
        // Check if frame resolution or format is changed.
        if((new_width!=last_width)||(new_height!=last_height)||(new_colors!=last_color_mode)||(new_frame->format!=last_frame_fmt))
        {
            // Save new values for later.
            last_width = new_width;
            last_height = new_height;
            last_color_mode = new_colors;
            last_frame_fmt = (AVPixelFormat)new_frame->format;
#ifdef VIP_EN_DBG_OUT
            if((log_level&LOG_FRAME)!=0)
            {
                qInfo()<<"[VIP] Re-init of frame converter:"<<last_width<<","<<last_height;
            }
#endif
            // Free up frame converter context.
            freeFrameConverter();
            // Initialize frame converter.
            result = initFrameConverter(new_frame, last_width, last_height, last_color_mode);
            video_dst_linesize[0] = last_width;
            if(result!=false)
            {
                // Resize gray line to fit all pixels.
                gray_line.setLength(video_dst_linesize[0]);
            }
        }
        else
        {
            result = true;
        }
    }
    return result;
}

//------------------------ Wait for free space in output queue (blocking).
bool VideoInFFMPEG::waitForOutQueue(uint16_t line_count)
{
    size_t queue_size;
    uint16_t front_frame;
    if(line_count>=MAX_VLINE_QUEUE_SIZE)
    {
        return false;
    }
    while(1)
    {
        front_frame = 0;
        // Lock shared access.
        mtx_lines->lock();
        // Refresh queue size.
        queue_size = (out_lines->size()+1);
        if(queue_size>1)
        {
            // TODO: fix crash on exit
            if(out_lines->front().isServiceLine()==false)
            {
                front_frame = out_lines->front().frame_number;
            }
        }
        // Unlock shared access.
        mtx_lines->unlock();
        if(queue_size<(size_t)(MAX_VLINE_QUEUE_SIZE-line_count))
        {
            // There is enough space in output queue.
            //qInfo()<<front_frame<<frame_counter;
            // Allow only up to 2 frames read-ahead.
            if((front_frame==0)||(frame_counter<(front_frame+2)))
            {
                break;
            }
        }
        // Process events.
        QApplication::processEvents();
        if(finish_work==false)
        {
            // Wait for queue to empty.
            QThread::msleep(20);
        }
        else
        {
            // Received stop signal.
            // No need to wait for anything.
            return false;
        }
    }
    return true;
}

//------------------------ Output video line into output queue (blocking).
void VideoInFFMPEG::outNewLine(VideoLine *in_line)
{
    out_lines->push_back(*in_line);
    // Duplicate new line through event.
    if((in_line->isServiceLine()==false)||(in_line->isServFiller()!=false))
    {
        emit newLine(*in_line);
    }
}

//------------------------ Splice frame into individual video lines.
void VideoInFFMPEG::spliceFrame(AVFrame *in_frame)
{
    uint8_t line_jump, step_cycle;
    uint16_t safety_cnt, frame_width, lines_count, real_line_count, line_offset, line_len, line_num;
    uint32_t arr_coord;
    int av_res;
    QElapsedTimer frame_timer, line_timer;

#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_FRAME)!=0)
    {
        qInfo()<<"[VIP] New frame #"<<frame_counter;
    }
#endif

    // Get frame parameters.
    lines_count = getFinalHeight(in_frame->height);
    frame_width = getFinalWidth(in_frame->width);
    last_real_width = in_frame->width;
    safety_cnt = (lines_count+1);

    // Set number of passes to two for full frame deinterlacing (one field, than another field).
    real_line_count = lines_count;
    if(vip_set.skip_lines!=false)
    {
        // Limit number of passes to one (only first field).
        real_line_count = lines_count/2;
    }

    // Enable deinterlacing.
    line_jump = 2;
    // Check conditions for enabling frame counter hold.
    /*if(real_line_count<(DS_STC007_PAL_LINES_PER_FIELD*4/3))
    {
        // Too little lines for one full frame, only one field would fit.
        if(vip_set.skip_lines==false)
        {
            line_jump = 1;
        }
        // Check frame number counter advancing mode.
        if(frame_advancing==ADV_CONST)
        {
            // Need to switch to half-frame counter and skip advancing on current frame.
            frame_advancing = ADV_HOLD;
#ifdef VIP_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[VIP] Switched to half-frame counter advancing";
            }
#endif
        }
        else if(frame_advancing==ADV_MOVE)
        {
            // In half-frame counter mode last frame has advanced the counter, switch to hold for current one.
            frame_advancing = ADV_HOLD;
        }
        else
        {
            // In half-frame counter mode last frame was not advancing the counter, switch to advancing at current frame.
            frame_advancing = ADV_MOVE;
        }
    }
    else if(frame_advancing!=ADV_CONST)
    {
        // Set normal once-per-frame counter advancing.
        frame_advancing = ADV_CONST;
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[VIP] Switched to every-frame counter advancing";
        }
#endif
    }*/

    // Re-initialize frame converter if frame parameters change.
    if(false==keepFrameInCheck(in_frame, frame_width, lines_count, vip_set.colors))
    {
        new_state = failToPlay(tr("Не удалось инициализировать конвертер кадра"));
        return;
    }

    // Get "true" aligned line width.
    line_len = video_dst_linesize[0];

    // Wait for enough space in output queue.
    if(waitForOutQueue(real_line_count)==false)
    {
        if(finish_work==false)
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] Unsupported height of the frame in [VideoInFFMPEG::spliceFrame()]! ("<<real_line_count<<")";
        }
        return;
    }

    // Start frame processing timer.
    frame_timer.start();

    // Convert frame to grayscale.
    av_res = sws_scale(conv_ctx, (const uint8_t * const *)in_frame->data, in_frame->linesize, 0, lines_count, video_dst_data, video_dst_linesize);
    //av_image_copy(video_dst_data, video_dst_linesize, (const uint8_t **)(in_frame->data), in_frame->linesize, in_frame->format, in_frame->width, in_frame->height);

    if(av_res<0)
    {
        new_state = failToPlay(tr("Не удалось преобразовать кадр (не поддерживается)"));
        return;
    }
    else
    {
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_FRAME)!=0)
        {
            qInfo()<<"[VIP] Frame resize time:"<<frame_timer.nsecsElapsed()/1000<<"us";
        }
#endif
    }

    // Report about new frame.
    if(hasWidthDoubling(in_frame->width)==false)
    {
        if(frame_advancing==ADV_CONST)
        {
            emit newFrame(line_len, real_line_count);
        }
        else if(frame_advancing==ADV_HOLD)
        {
            emit newFrame(line_len, real_line_count*2);
        }
    }
    else
    {
        if(frame_advancing==ADV_CONST)
        {
            emit newFrame(line_len/2, real_line_count);
        }
        else if(frame_advancing==ADV_HOLD)
        {
            emit newFrame(line_len/2, real_line_count*2);
        }
    }

#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] Processing with advancing:"<<frame_advancing<<", line jump:"<<line_jump<<", using"<<real_line_count<<"lines";
    }
#endif

    step_cycle = 0;
    // Lock shared access.
    mtx_lines->lock();
    // Cycle through line-step-cycle iterations (two passes for deinterlacing).
    while(step_cycle<line_jump)
    {
        // Preset starting line offset for the frame/field.
        // (frame buffer contains data from top to bottom)
        line_offset = 0;
        if(step_cycle==1)
        {
            if(frame_advancing==ADV_CONST)
            {
                // Not in half-frame mode, allow processing of second field.
                // Switch lines down by one for second field.
                line_offset++;
            }
            else
            {
                break;
            }
        }
        // Calculate starting line coordinate.
        if(frame_advancing==ADV_MOVE)
        {
            line_num = line_offset+2;
        }
        else
        {
            line_num = line_offset+1;
        }

        // Cycle through lines (backwards through frame).
        do
        {
            // Start line processing timer.
            line_timer.start();

            safety_cnt--;

            // Set frame/line counters.
            gray_line.line_number = line_num;
            gray_line.frame_number = frame_counter;
            gray_line.setDoubleWidth(hasWidthDoubling(in_frame->width));
            // Set source color channel.
            gray_line.colors = last_color_mode;
            // Calculate offset in frame data.
            arr_coord = line_offset*line_len;
#ifdef VIP_EN_DBG_OUT
            if(((log_level&LOG_LINES)!=0)||((log_level&LOG_PIXELS)!=0))
            {
                qInfo()<<"[VIP] Current line in frame:"<<line_num<<", offset:"<<arr_coord<<", size:"<<line_len;
            }
#endif
            if(target_pixfmt==VIP_BUF_FMT_BW)
            {
                // Copy data from frame line (Y plane, brightness only) to video line object.
                std::copy(&video_dst_data[0][arr_coord], &video_dst_data[0][arr_coord+line_len], gray_line.pixel_data.begin());
            }
            else
            {
                // Single color channel decoding enabled.
                if(vip_set.colors==vid_preset_t::COLOR_R)
                {
                    // Copy data from frame line (RED plane) to video line object.
                    std::copy(&video_dst_data[2][arr_coord], &video_dst_data[2][arr_coord+line_len], gray_line.pixel_data.begin());
                }
                else if(vip_set.colors==vid_preset_t::COLOR_B)
                {
                    // Copy data from frame line (BLUE plane) to video line object.
                    std::copy(&video_dst_data[1][arr_coord], &video_dst_data[1][arr_coord+line_len], gray_line.pixel_data.begin());
                }
                else
                {
                    // Copy data from frame line (GREEN plane) to video line object.
                    std::copy(&video_dst_data[0][arr_coord], &video_dst_data[0][arr_coord+line_len], gray_line.pixel_data.begin());
                }
            }

            // Store amount of spent time.
            gray_line.process_time = line_timer.nsecsElapsed()/1000;

            // Add resulting line into output queue.
            outNewLine(&gray_line);

#ifdef VIP_EN_DBG_OUT
            if((log_level&LOG_LINES)!=0)
            {
                if(gray_line.isEmpty()==false)
                {
                    qInfo()<<"[VIP] Line"<<line_num<<"done, time:"<<gray_line.process_time<<"us";
                }
            }
#endif
            // Check for limits of the line count in the frame.
            if(line_offset<(lines_count-line_jump))
            {
                // Go to the next line in the field.
                line_offset = line_offset+line_jump;
            }
            else
            {
                // Field is done.
                line_num += line_jump;
                insertFieldEndLine(line_num);
                break;
            }
            // Advance final line counter.
            //line_num += 2;
            line_num += line_jump;
        }
        while(safety_cnt>0);
        // Go to the next field.
        step_cycle++;
    }
    // Frame is done.
    line_num += line_jump;
    insertFrameEndLine(line_num);
    // Unlock shared access.
    mtx_lines->unlock();

    if(frame_advancing!=ADV_HOLD)
    {
        // Notify about new lines from frame.
        emit frameDecoded(frame_counter);
        // Count frames.
        frame_counter++;
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_FRAME)!=0)
        {
            qInfo()<<"[VIP] Frame"<<(frame_counter-1)<<"sliced by"<<frame_timer.nsecsElapsed()/1000<<"us";
        }
#endif
    }
    else
    {
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_FRAME)!=0)
        {
            qInfo()<<"[VIP] Half of the frame"<<(frame_counter-1)<<"sliced by"<<frame_timer.nsecsElapsed()/1000<<"us";
        }
#endif
    }
}

//------------------------ Insert dummy frame to keep sync on dropped frames.
void VideoInFFMPEG::insertDummyFrame(int64_t frame_cnt, bool last_frame, bool report)
{
    uint8_t line_jump, step_cycle;
    uint16_t safety_cnt, line_len, lines_count, line_offset, line_num;
    QElapsedTimer frame_timer, line_timer;

    // Cycle through empty frames.
    while(frame_cnt>0)
    {
        frame_cnt--;

        // Set line step through frame.
        line_jump = 2;
        // Get frame parameters.
        lines_count = last_height;
        line_len = last_real_width;
        safety_cnt = (lines_count+1);

        dummy_line.setServNo();
        // Resize line to fit all pixels.
        dummy_line.setLength(line_len);
        dummy_line.setDoubleWidth(hasWidthDoubling(last_real_width));
        // Cycle through all pixels in line.
        /*for(uint16_t pixel=0; pixel<line_len; pixel++)
        {
            // Generate empty dummy lines.
            dummy_line.pixel_data[pixel] = 0;
        }*/

        // Wait for enough space in output queue.
        if(waitForOutQueue(lines_count)==false)
        {
            return;
        }

        emit newFrame(line_len, lines_count);

#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_FRAME)!=0)
        {
            frame_timer.start();
            if(report==false)
            {
                qInfo()<<"[VIP] New frame"<<frame_counter<<"(filler)";
            }
            else
            {
                qInfo()<<"[VIP] New frame"<<frame_counter<<"(empty dummy)";
            }
        }
#endif

        step_cycle = 0;
        // Lock shared access.
        mtx_lines->lock();
        // Cycle through line-step-cycle iterations (for deinterlacing).
        while(step_cycle<line_jump)
        {
            // Set starting line offset for the frame/field.
            // (frame buffer contains data from top to bottom)
            line_offset = 0;
            if(step_cycle==1)
            {
                // Shift to next field.
                line_offset++;
            }

            // Cycle through lines (backwards through frame).
            do
            {
                line_timer.start();

                safety_cnt--;

                // Calculate line coordinate.
                line_num = line_offset+1;

                // Set frame/line counters.
                dummy_line.line_number = line_num;
                dummy_line.frame_number = frame_counter;
                if(report==false)
                {
                    dummy_line.setServFiller();
                }
                else
                {
                    dummy_line.setEmpty(true);
                }
                // Store amount of time spent on line.
                dummy_line.process_time = line_timer.nsecsElapsed()/1000;

                // Add resulting line into output queue.
                outNewLine(&dummy_line);

#ifdef VIP_EN_DBG_OUT
                if((log_level&LOG_LINES)!=0)
                {
                    if(report==false)
                    {
                        qInfo()<<"[VIP] Filler line"<<line_num<<"generated";
                    }
                    else
                    {
                        qInfo()<<"[VIP] Empty line"<<line_num<<"generated";
                    }
                }
#endif
                // Check for limits of the line count in the frame.
                //qInfo()<<">"<<line_offset<<"-"<<lines_count<<"-"<<step_cycle<<"="<<safety_cnt;
                if(line_offset<(lines_count-line_jump))
                {
                    line_offset = line_offset+line_jump;
                }
                else
                {
                    // Field is done.
                    line_num += line_jump;
                    insertFieldEndLine(line_num);
#ifdef VIP_EN_DBG_OUT
                    if((log_level&LOG_LINES)!=0)
                    {
                        qInfo()<<"[VIP] End-field line generated";
                    }
#endif
                    break;
                }
            }
            while(safety_cnt>0);
            step_cycle++;
        }
        // Frame is done.
        line_num += line_jump;

        if((frame_cnt==0)&&(last_frame!=false))
        {
            insertFileEndLine(line_num);
            line_num += line_jump;
        }

        insertFrameEndLine(line_num);
        // Unlock shared access.
        mtx_lines->unlock();

#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_FRAME)!=0)
        {
            qInfo()<<"[VIP] Dummy frame"<<frame_counter<<"generated by"<<frame_timer.nsecsElapsed()/1000<<"us";
        }
#endif

        if(report!=false)
        {
            // Notify about new lines from frame.
            emit frameDecoded(frame_counter);
            emit frameDropDetected();
        }

        // Count frames.
        frame_counter++;

        // Process Qt events in case of very corrupted file with many skipped frames.
        QApplication::processEvents();
        // Check if "stop" command has been issued.
        if(new_state==STG_SRC_DET)
        {
            break;
        }
    }
}

//------------------------ Insert service line to signal that next lines will be for new file.
void VideoInFFMPEG::insertNewFileLine()
{
    VideoLine service_line;
    // Set file path in service line.
    std::string file_name;
    QFileInfo source_file(src_path);
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] Starting new decoding...";
        qInfo()<<"[VIP] Source directory:"<<source_file.absolutePath();
        qInfo()<<"[VIP] Source name (no ext.):"<<source_file.completeBaseName();
        qInfo()<<"[VIP] Source extension:"<<source_file.suffix();
    }
#endif
    // Re-assemble file path.
    file_name = source_file.absolutePath().toStdString();       // File path.
    file_name += "/";                                           // Should be fine for cross-platform Qt file operations.
    file_name += source_file.completeBaseName().toStdString();  // File name.
    if(vip_set.colors==vid_preset_t::COLOR_R)
    {
        file_name += "_R";
    }
    else if(vip_set.colors==vid_preset_t::COLOR_G)
    {
        file_name += "_G";
    }
    else if(vip_set.colors==vid_preset_t::COLOR_B)
    {
        file_name += "_B";
    }
    file_name += "."+source_file.suffix().toStdString();            // File extension.

    service_line.setServNewFile(file_name);
    service_line.frame_number = frame_counter;
    service_line.line_number = 0;
    // Wait for enough space in output queue.
    if(waitForOutQueue(1)!=false)
    {
        // Lock shared access.
        mtx_lines->lock();
        // Put service line into output queue.
        outNewLine(&service_line);
        // Unlock shared access.
        mtx_lines->unlock();
    }
}

//------------------------ Insert service line to signal that previous lines were last from the file.
void VideoInFFMPEG::insertFileEndLine(uint16_t line_number)
{
    VideoLine service_line;
    service_line.setServEndFile();
    service_line.frame_number = frame_counter;
    service_line.line_number = line_number;
    // Put service line into output queue.
    outNewLine(&service_line);
    qInfo()<<"[VIP] File ended";
}

//------------------------ Insert service line to signal that field of the frame has ended.
void VideoInFFMPEG::insertFieldEndLine(uint16_t line_number)
{
    VideoLine service_line;
    service_line.setServEndField();
    service_line.frame_number = frame_counter;
    service_line.line_number = line_number;
    // Put service line into output queue.
    outNewLine(&service_line);
}

//------------------------ Insert service line to signal that frame has ended.
void VideoInFFMPEG::insertFrameEndLine(uint16_t line_number)
{
    VideoLine service_line;
    service_line.setServEndFrame();
    service_line.frame_number = frame_counter;
    service_line.line_number = line_number;
    // Put service line into output queue.
    outNewLine(&service_line);
}

//------------------------ Set debug logging level.
void VideoInFFMPEG::setLogLevel(uint8_t new_log_lvl)
{
    log_level = new_log_lvl;
    //qInfo()<<"[VIP] New log mask:"<<log_level;
}

//------------------------ Set the pointer to queue of lines to output from the frame.
void VideoInFFMPEG::setOutputPointers(std::deque<VideoLine> *in_queue, QMutex *in_mtx)
{
    if((in_queue!=NULL)&&(in_mtx!=NULL))
    {
        out_lines = in_queue;
        mtx_lines = in_mtx;
    }
}

//------------------------ Set new location for the media source.
void VideoInFFMPEG::setSourceLocation(QString in_path)
{
    if(in_path.isEmpty()==false)
    {

        // Add prefix for FFMPEG.
        //in_path = QString("file:")+in_path;
        //if(src_path!=in_path)
        {
            // Stop playback and splicing.
            mediaStop();

            src_path = in_path;
#ifdef VIP_EN_DBG_OUT
            if((log_level&LOG_SETTINGS)!=0)
            {
                qInfo()<<"[VIP] Source location set to"<<src_path;
            }
#endif

            // Check file existance and availability.
            QFile file_check(src_path, this);
            if(file_check.exists()==false)
            {
                failToPlay(tr("Указанный файл источника не найден!"));
            }
            else if((file_check.permissions()&QFileDevice::ReadUser)==0)
            {
                failToPlay(tr("Недостаточно разрешений для чтения файла источника!"));
            }
            else
            {
                // Source is ready to be read.
                new_state = STG_SRC_DET;
            }
        }
    }
}

//------------------------ Set stepping playback mode.
void VideoInFFMPEG::setStepPlay(bool in_step)
{
    if(step_play!=in_step)
    {
        step_play = in_step;
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(step_play==false)
            {
                qInfo()<<"[VIP] Stepped playback is disabled";
            }
            else
            {
                qInfo()<<"[VIP] Stepped playback is enabled";
            }
        }
#endif
    }
}

//------------------------ Set dropout detector mode.
void VideoInFFMPEG::setDropDetect(bool in_detect)
{
    if(detect_frame_drop!=in_detect)
    {
        detect_frame_drop = in_detect;
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(detect_frame_drop==false)
            {
                qInfo()<<"[VIP] Dropout detection is disabled";
            }
            else
            {
                qInfo()<<"[VIP] Dropout detection is enabled";
            }
        }
#endif
    }
}

//------------------------ Set fine video processing settings.
void VideoInFFMPEG::setFineSettings(vid_preset_t in_set)
{
    vip_set = in_set;
#ifdef LB_EN_DBG_OUT
    if((log_level&LOG_SETTINGS)!=0)
    {
        qInfo()<<"[VIP] Fine video processing settings updated";
    }
#endif
    emit guiUpdFineSettings(vip_set);
}

//------------------------ Set fine video processor settings to defaults.
void VideoInFFMPEG::setDefaultFineSettings()
{
    vid_preset_t tmp_set;
    vip_set = tmp_set;
    emit guiUpdFineSettings(vip_set);
}

//------------------------ Get current fine binarization settings.
void VideoInFFMPEG::requestCurrentFineSettings()
{
    emit guiUpdFineSettings(vip_set);
}

//------------------------ Main execution loop.
void VideoInFFMPEG::runFrameDecode()
{
    int av_res;
    int64_t dts_diff;
    bool new_file;
    QElapsedTimer timer;

    dts_diff = 0;

    qInfo()<<"[VIP] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
    // Check working pointers.
    if((out_lines==NULL)||(mtx_lines==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[VIP] Empty output pointer provided in [VideoInFFMPEG::runFrameDecode()], unable to continue!";
        emit finished();
        return;
    }

    QString log_line;
    // Dump compile-time FFmpeg versions.
    log_line = QString::fromLocal8Bit(AV_STRINGIFY(LIBAVDEVICE_VERSION));
    qInfo()<<"[VIP] FFMPEG avcodec compile-time version:"<<log_line;
    log_line = QString::fromLocal8Bit(AV_STRINGIFY(LIBAVCODEC_VERSION));
    qInfo()<<"[VIP] FFMPEG avdevice compile-time version:"<<log_line;
    log_line = QString::fromLocal8Bit(AV_STRINGIFY(LIBSWSCALE_VERSION));
    qInfo()<<"[VIP] FFMPEG swscale compile-time version:"<<log_line;

    unsigned vers;
    // Dump run-time FFmpeg versions.
    vers = avcodec_version();
    log_line = QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    qInfo()<<"[VIP] FFMPEG avcodec run-time version:"<<log_line;
    vers = avdevice_version();
    log_line = QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    qInfo()<<"[VIP] FFMPEG avdevice run-time version:"<<log_line;
    vers = swscale_version();
    log_line = QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    qInfo()<<"[VIP] FFMPEG swscale run-time version:"<<log_line;

    // Register all demuxers.
    /*avdevice_register_all();

    AVDictionary *inopt = NULL;
    AVInputFormat *infmt = NULL;
    AVDeviceInfoList *dev_list;
    //infmt = av_find_input_format("gdigrab");
    infmt = av_find_input_format("dshow");
    av_res = avdevice_list_input_sources(infmt, NULL, inopt, &dev_list);
    if(av_res<0)
    {
        qDebug()<<"[VIP] Unable to find capture devices";
    }
    else
    {
        qDebug()<<"[VIP] Found"<<av_res<<"capture devices";
        if(av_res>0)
        {
            qInfo()<<dev_list->devices[0]->device_description<<dev_list->devices[0]->device_name;
        }
        avdevice_free_list_devices(dev_list);
    }*/

    // Inf. loop in a thread.
    while(finish_work==false)
    {
        // Process Qt events.
        QApplication::processEvents();
        new_file = false;
        if(finish_work!=false)
        {
            // Break the loop and do nothing if got shutdown event.
            break;
        }
        // Apply new state if required.
        if(proc_state!=new_state)
        {
            if(new_state==STG_PLAY)
            {
                // Try to start new playback.
                if(proc_state==STG_PAUSE)
                {
                    // Playback was paused before - just resume.
                    emit mediaPlaying();
                }
                else
                {
                    // Try to init new decoder.
                    if(decoderInit()==VIP_RET_OK)
                    {
                        // Decoder init ok, playback ready to be started.
                        emit mediaPlaying();
                        new_file = true;
#ifdef VIP_EN_DBG_OUT
                        if((log_level&LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[VIP] New file";
                        }
#endif
                    }
                }
            }
            // No "else" to allow error fall through from [decoderInit()].
            if(new_state==STG_IDLE)
            {
                // Free up resources.
                decoderStop();
                emit mediaError(last_error_txt);
            }
            else if(new_state==STG_SRC_DET)
            {
                if(dec_frame!=NULL)
                {
#ifdef VIP_EN_DBG_OUT
                    if((log_level&LOG_FRAME)!=0)
                    {
                        qInfo()<<"[VIP] Inserting trailing frames...";
                    }
#endif
                    // Insert two trailing empty frame to push out all audio data.
                    insertDummyFrame(1, true, false);
                }
                // Free up resources.
                decoderStop();
                emit mediaStopped();
            }
            else if(new_state==STG_PAUSE)
            {
                emit mediaPaused();
            }
            proc_state = new_state;
        }
        // Do things.
        if(proc_state==STG_IDLE)
        {
            QThread::msleep(250);
        }
        else if((proc_state==STG_SRC_DET)||(proc_state==STG_PAUSE))
        {
            QThread::msleep(50);
        }
        else if(proc_state==STG_PLAY)
        {
            // Playback state.
            // Cycle till read video frame or EOF.
            while(1)
            {
                // Read next packet into container.
                timer.start();
                av_res = av_read_frame(format_ctx, &dec_packet);
#ifdef VIP_EN_DBG_OUT
                if((log_level&LOG_FRAME)!=0)
                {
                    qInfo()<<"[VIP] Packet read time:"<<timer.nsecsElapsed()/1000<<"us";
                }
#endif

                if(av_res<0)
                {
                    if(av_res==AVERROR_EOF)
                    {
#ifdef VIP_EN_DBG_OUT
                        if((log_level&LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[VIP] End of file";
                        }
#endif
                    }
                    else
                    {
                        char err_str[80];
                        if(av_strerror(av_res, err_str, 80)==0)
                        {
                            qWarning()<<DBG_ANCHOR<<"[VIP] Error while reading a frame:"<<err_str;
                        }
                        else
                        {
                            qWarning()<<DBG_ANCHOR<<"[VIP] Error while reading a frame!"<<av_res;
                        }
                    }
                    break;
                }
                // Check if it is the video stream.
                if(dec_packet.stream_index==stream_index)
                {
#ifdef VIP_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        qInfo()<<"[VIP] Video packet found, decoding...";
                    }
#endif
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
#ifdef VIP_EN_DBG_OUT
                if((log_level&LOG_FRAME)!=0)
                {
                    qInfo()<<"[VIP] New frame at position"<<dec_packet.pos<<"bytes, dts:"<<dec_packet.dts<<"pts:"<<dec_packet.pts;
                }
#endif
                // Decode packet.
                timer.start();
                av_res = avcodec_send_packet(video_dec_ctx, &dec_packet);
#ifdef VIP_EN_DBG_OUT
                if((log_level&LOG_FRAME)!=0)
                {
                    qInfo()<<"[VIP] Frame decode time:"<<timer.nsecsElapsed()/1000<<"us";
                }
#endif
                bool skip_packet;
                skip_packet = true;
                if(av_res<0)
                {
                    // There was an error while decoding.
#ifdef VIP_EN_DBG_OUT
                    if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FRAME)!=0))
                    {
                        if(av_res==AVERROR_EOF)
                        {
                            qInfo()<<"[VIP] Decoding finished: end of file";
                        }
                        else if(av_res==AVERROR(EAGAIN))
                        {
                            qInfo()<<"[VIP] Decoding error: not all frames are fetched!";
                        }
                        else if(av_res==AVERROR(EINVAL))
                        {
                            qInfo()<<"[VIP] Decoding error: decoder not ready or needs flush!";
                        }
                        else if(av_res==AVERROR(ENOMEM))
                        {
                            qInfo()<<"[VIP] Decoding error: not enough memory!";
                        }
                        else
                        {
                            qInfo()<<"[VIP] Decoding error: unknown FFMPEG error code"<<av_res;
                        }
                    }
#endif
                    // Unref frame if it will be not used by [avcodec_receive_frame()].
                    av_packet_unref(&dec_packet);
                    //mediaStop();
                    new_state = failToPlay(tr("Не удалось декодировать поток источника"));
                }
                else
                {
                    // No error while decoding.
                    do
                    {
                        // Read decoded frame from the packet.
                        av_res = avcodec_receive_frame(video_dec_ctx, dec_frame);
                        if((av_res==0)||(av_res==AVERROR(EAGAIN)))
                        {
                            // Check if new file has started.
                            if(new_file!=false)
                            {
                                new_file = false;
#ifdef VIP_EN_DBG_OUT
                                if((log_level&LOG_FRAME)!=0)
                                {
                                    qInfo()<<"[VIP] Inserting leading service line...";
                                }
#endif
                                // Put service line "new file" into queue.
                                insertNewFileLine();
                                // Reset frame size beforce splicing first real frame
                                // to force reinit of image buffers.
                                last_height = last_width = last_real_width = 0;
                            }
                        }
                        if(av_res==0)
                        {
                            // No error with received frame.
                            // Check if HW decoding is used.
                            if(dec_frame->format==hw_fmt)
                            {
                                //AVFrame hw_frame;
                                // Transfer frame data from accelerator.
                                //av_hwframe_transfer_data(hw_frame);
                                qWarning()<<DBG_ANCHOR<<"[VIP] HW decoding is not implemented!";
                            }

                            // Crop frame.
                            if((vip_set.crop_left!=0)||(vip_set.crop_right!=0)||(vip_set.crop_top!=0)||(vip_set.crop_bottom!=0))
                            {
                                // Set crop parameters.
                                dec_frame->crop_top = vip_set.crop_top;
                                dec_frame->crop_bottom = vip_set.crop_bottom;
                                dec_frame->crop_left = vip_set.crop_left;
                                dec_frame->crop_right = vip_set.crop_right;
                                // Apply cropping.
                                av_res = av_frame_apply_cropping(dec_frame, AV_FRAME_CROP_UNALIGNED);
                                if(av_res<0)
                                {
                                    qWarning()<<DBG_ANCHOR<<"[VIP] Crop failed!";
                                }
                            }

                            // Found usefull data in the packet, allow auto-pause.
                            skip_packet = false;
                            if(detect_frame_drop!=false)
                            {
                                // Frame dropout detector is active.
                                if(dts_detect==false)
                                {
                                    // It's the first frame since dropout detector was activated.
                                    // Store DTS of the first decoded frame.
                                    dts_detect = true;
                                    last_dts = dec_frame->pkt_dts;
                                    dts_diff = 0;
                                }
                                else
                                {
                                    // Calculate DTS difference between current and last frames.
                                    dts_diff = (dec_frame->pkt_dts-last_dts)-dec_frame->pkt_duration;
                                }
#ifdef VIP_EN_DBG_OUT
                                if((log_level&LOG_FRAME)!=0)
                                {
                                    qInfo()<<"[VIP] Units per frame:"<<dec_frame->pkt_duration<<"| current DTS:"<<dec_frame->pkt_dts<<"| diff:"<<dts_diff;
                                }
#endif
                                // Save new "last DTS".
                                last_dts = dec_frame->pkt_dts;
                                // Check if current DTS is one step after previous.
                                if(dts_diff>0)
                                {
#ifdef VIP_EN_DBG_OUT
                                    if((log_level&LOG_PROCESS)!=0)
                                    {
                                        qInfo()<<"[VIP] Detected"<<dts_diff/dec_frame->pkt_duration<<"dropped frames in the stream!";
                                    }
#endif
                                    // Insert dummy frames in place of droped frames.
                                    insertDummyFrame(dts_diff/dec_frame->pkt_duration, false, true);
                                    // Finally put good frame into queue.
                                    // Cut frame into video lines and put those into output queue.
                                    spliceFrame(dec_frame);
                                }
                                else
                                {
                                    // Cut frame into video lines and put those into output queue.
                                    spliceFrame(dec_frame);
                                }
                            }
                            else
                            {
#ifdef VIP_EN_DBG_OUT
                                if((log_level&LOG_FRAME)!=0)
                                {
                                    qInfo()<<"[VIP] Units per frame:"<<dec_frame->pkt_duration<<"| current PTS:"<<dec_frame->pts<<"| current DTS:"<<dec_frame->pkt_dts<<"| diff:"<<dts_diff;
                                }
#endif
                                // Reset DTS difference detector.
                                dts_detect = false;
                                // Cut frame into video lines and put those into output queue.
                                spliceFrame(dec_frame);
                            }
                        }
                        else if(av_res==AVERROR(EAGAIN))
                        {
                            // Not enough data to get the frame, need more data.
#ifdef VIP_EN_DBG_OUT
                            if((log_level&LOG_FRAME)!=0)
                            {
                                qInfo()<<"[VIP] Next packet needed for a frame...";
                            }
#endif
                            continue;
                        }
                    }
                    while(av_res==0);
                    // Free up memory from processed packet.
                    av_packet_unref(&dec_packet);
                }
                // Switch pause on if stepped playback is enabled and allowed.
                if((step_play!=false)&&(skip_packet==false))
                {
                    // Check if playback should be stopped due to an error.
                    if(new_state!=STG_IDLE)
                    {
                        new_state = STG_PAUSE;
#ifdef VIP_EN_DBG_OUT
                        if((log_level&LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[VIP] Auto pausing...";
                        }
#endif
                    }
                }
            }
            else
            {
                mediaStop();
            }
        }
        else
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] -------------------- Impossible state detected in [VideoInFFMPEG::runFrameDecode()], breaking...";
            break;
        }
    }

    freeFrameConverter();

    qInfo()<<"[VIP] Loop stop.";
    emit finished();
}

//------------------------ Start playback from current position.
void VideoInFFMPEG::mediaPlay()
{
    if((proc_state==STG_SRC_DET)||(proc_state==STG_PAUSE))
    {
        new_state = STG_PLAY;
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            if(proc_state==STG_SRC_DET)
            {
                qInfo()<<"[VIP] Starting playback...";
            }
            else
            {
                qInfo()<<"[VIP] Resuming playback...";
            }
        }
#endif
    }
    else
    {
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[VIP] Unable to start playback.";
        }
#endif
    }
}

//------------------------ Pause playback.
void VideoInFFMPEG::mediaPause()
{
    if(proc_state==STG_PLAY)
    {
        new_state = STG_PAUSE;
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[VIP] Pausing...";
        }
#endif
    }
#ifdef VIP_EN_DBG_OUT
    else
    {
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[VIP] Unable to pause: no playback.";
        }
    }
#endif
}

//------------------------ Stop playback.
void VideoInFFMPEG::mediaStop()
{
    if((proc_state==STG_PLAY)||(proc_state==STG_PAUSE))
    {
        new_state = STG_SRC_DET;
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[VIP] Stopping playback...";
        }
#endif
    }
}

//------------------------ Set the flag to break execution loop and exit.
void VideoInFFMPEG::stop()
{
#ifdef VIP_EN_DBG_OUT
    qInfo()<<"[VIP] Received termination request";
#endif

    finish_work = true;
    mediaStop();
    proc_state = new_state = STG_SRC_DET;
}
