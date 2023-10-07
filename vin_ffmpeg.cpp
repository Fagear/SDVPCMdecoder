#include "vin_ffmpeg.h"

//------------------------
VideoInFFMPEG::VideoInFFMPEG(QObject *parent) : QObject(parent)
{
    log_level = 0;
    proc_state = STG_IDLE;
    new_state = STG_IDLE;
    event_usr = EVT_USR_NO;
    event_cap = EVT_CAP_NO;
    evt_frame_cnt = 0;
    evt_errcode = FFMPEGWrapper::FFMERR_OK;
    last_real_width = 0;
    last_height = 0;
    frame_counter = 1;
    frames_total = 0;
    src_path.clear();
    out_lines = NULL;
    mtx_lines = NULL;
    ffmpeg_src = NULL;

    ffmpeg_thread = NULL;
    ffmpeg_thread = new QThread;
    ffmpeg_src = new FFMPEGWrapper();
    ffmpeg_src->moveToThread(ffmpeg_thread);
    connect(ffmpeg_thread, SIGNAL(started()), ffmpeg_src, SLOT(slotLogStart()));
    connect(ffmpeg_thread, SIGNAL(finished()), ffmpeg_thread, SLOT(deleteLater()));
    connect(this, SIGNAL(finished()), ffmpeg_thread, SLOT(quit()));
    connect(this, SIGNAL(requestDropDet(bool)), ffmpeg_src, SLOT(slotSetDropDetector(bool)));
    connect(this, SIGNAL(requestColor(vid_preset_t)), ffmpeg_src, SLOT(slotSetCropColor(vid_preset_t)));
    connect(this, SIGNAL(openDevice(QString,QString)), ffmpeg_src, SLOT(slotOpenInput(QString,QString)));
    connect(this, SIGNAL(closeDevice()), ffmpeg_src, SLOT(slotCloseInput()));
    connect(this, SIGNAL(requestFrame()), ffmpeg_src, SLOT(slotGetNextFrame()));
    connect(ffmpeg_src, SIGNAL(sigInputReady(int, int, uint32_t, float)), this, SLOT(captureReady(int, int, uint32_t, float)));
    connect(ffmpeg_src, SIGNAL(sigInputClosed()), this, SLOT(captureClosed()));
    connect(ffmpeg_src, SIGNAL(sigVideoError(uint32_t)), this, SLOT(captureError(uint32_t)));
    connect(ffmpeg_src, SIGNAL(newImage(QImage,bool)), this, SLOT(receiveFrame(QImage,bool)));

    setDefaultFineSettings();
    new_file = false;
    src_open = false;
    step_play = false;
    detect_frame_drop = false;
    finish_work = false;
}

//------------------------ Reset all counters and stats per source.
void VideoInFFMPEG::resetCounters()
{
    new_file = true;
    src_path.clear();
    last_real_width = last_height = 0;
    frame_counter = 1;
    frames_total = 0;
    gray_line.clear();
    dummy_line.clear();
    evt_frames.clear();
    evt_double.clear();
}

//------------------------ Check if provided width should be doubled.
bool VideoInFFMPEG::hasWidthDoubling(uint16_t in_width)
{
    if(in_width<FFMPEGWrapper::MAX_DBL_WIDTH)
    {
        if(in_width>FFMPEGWrapper::MIN_DBL_WIDTH)
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

//------------------------ Request next frame from FFMPEG.
void VideoInFFMPEG::askNextFrame()
{
    // Set settings.
    emit requestDropDet(detect_frame_drop);
    emit requestColor(vip_set);
    // Request new frame from the source.
    emit requestFrame();
}

//------------------------ Wait for free space in output queue (blocking).
bool VideoInFFMPEG::waitForOutQueue(uint16_t line_count)
{
    size_t queue_size;
    uint32_t front_frame;
    if(line_count>=MAX_VLINE_QUEUE_SIZE)
    {
        return false;
    }
#ifdef VIP_EN_DBG_OUT
    bool wait_lock;
    QElapsedTimer wait_cnt;
    wait_lock = false;
    wait_cnt.start();
#endif
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
            // Allow only up to [FRAMES_READ_AHEAD_MAX] frames read-ahead.
            if((front_frame==0)||(frame_counter<(front_frame+FRAMES_READ_AHEAD_MAX)))
            {
                break;
            }
        }
        // Process events.
        QApplication::processEvents();
        if(finish_work==false)
        {
#ifdef VIP_EN_DBG_OUT
            if((wait_lock==false)&&((log_level&LOG_PROCESS)!=0))
            {
                wait_lock = true;
                qInfo()<<"[VIP] Waiting for free space in output queue...";
            }
#endif
            // Wait for queue to empty and not waste CPU cycles.
            QThread::msleep(5);
        }
        else
        {
            // Received stop signal.
            // No need to wait for anything.
            return false;
        }
    }
#ifdef VIP_EN_DBG_OUT
    if((wait_lock!=false)&&((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[VIP] Waiting took"<<wait_cnt.elapsed()<<"ms";
    }
#endif
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

//------------------------ Insert service line to signal that previous lines were last from the file.
void VideoInFFMPEG::insertFileEndLine(uint16_t line_number)
{
    VideoLine service_line;
    service_line.setServEndFile();
    service_line.frame_number = frame_counter;
    service_line.line_number = line_number;
    // Put service line into output queue.
    outNewLine(&service_line);
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] File ended";
    }
#endif
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

//------------------------ Splice frame into individual video lines and perform deinterlacing.
void VideoInFFMPEG::spliceFrame(QImage *in_frame, bool in_double)
{
    uint8_t line_jump, field_idx;
    uint16_t safety_cnt, line_offset, line_len, line_num;
    QElapsedTimer frame_timer, line_timer;

#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        if(new_file==false)
        {
            qInfo()<<"[VIP] Slicing frame #"<<frame_counter;
        }
        else
        {
            qInfo()<<"[VIP] Slicing frame #"<<frame_counter<<"(first in a source)";
        }
    }
#endif

    // Get frame parameters.
    last_height = in_frame->height();
    line_len = last_real_width = in_frame->width();
    if(in_double!=false)
    {
        last_real_width = last_real_width/2;
    }
    safety_cnt = (last_height+1);

    // Enable deinterlacing (skip over each other line).
    line_jump = 2;

    // Wait for enough space in output queue.
    if(waitForOutQueue(last_height)==false)
    {
        if(finish_work==false)
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] Unsupported height of the frame! ("<<last_height<<")";
        }
        return;
    }

    // Report about new frame.
    emit newFrame(last_real_width, last_height);

    // Start frame processing timer.
    frame_timer.start();

#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] Processing with line jump:"<<line_jump<<", using"<<last_height<<"lines";
    }
#endif

    // Process first field.
    field_idx = 0;
    // Set length of the output line.
    gray_line.setLength(line_len);
    // Lock shared access.
    mtx_lines->lock();
    // Check if this is the first frame of a new source.
    if(new_file!=false)
    {
        new_file = false;
        // Put service line "new file" into queue.
        insertNewFileLine();
    }
    // Cycle through line-step-cycle iterations (two passes for deinterlacing).
    while(field_idx<line_jump)
    {
        // Preset starting line offset for the frame/field.
        // (frame buffer contains data from top to bottom)
        line_offset = 0;
        if(field_idx==1)
        {
            // Switch lines down by one for second field.
            line_offset++;
        }
        // Calculate starting line coordinate.
        line_num = line_offset+1;

        // Cycle through lines.
        do
        {
            // Start line processing timer.
            line_timer.start();

            safety_cnt--;

            // Set frame/line counters.
            gray_line.line_number = line_num;
            gray_line.frame_number = frame_counter;
            gray_line.setDoubleWidth(in_double);
            // Set source color channel.
            gray_line.colors = vip_set.colors;

            // Copy data from frame line into the video line object.
            std::copy(in_frame->scanLine(line_offset), in_frame->scanLine(line_offset)+in_frame->bytesPerLine(), gray_line.pixel_data.begin());

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
            if(line_offset<(last_height-line_jump))
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
            line_num += line_jump;
        }
        while(safety_cnt>0);
        // Go to the next field.
        field_idx++;
    }
    // Frame is done.
    line_num += line_jump;
    insertFrameEndLine(line_num);
    // Unlock shared access.
    mtx_lines->unlock();

    // Notify about new lines from frame.
    emit frameDecoded(frame_counter);
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_FRAME)!=0)
    {
        qInfo()<<"[VIP] Frame"<<frame_counter<<"sliced by"<<frame_timer.nsecsElapsed()/1000<<"us";
    }
#endif
    // Count frames.
    frame_counter++;
}

//------------------------ Insert dummy frame to keep sync on dropped frames.
void VideoInFFMPEG::insertDummyFrame(bool last_frame, bool report)
{
    uint8_t line_jump, step_cycle;
    uint16_t safety_cnt, line_len, lines_count, line_offset, line_num;
    QElapsedTimer frame_timer, line_timer;

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

    // Wait for enough space in output queue.
    if(waitForOutQueue(lines_count)==false)
    {
        return;
    }

    emit newFrame(line_len, last_height);

#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        frame_timer.start();
        if(report==false)
        {
            qInfo()<<"[VIP] Dummy frame"<<frame_counter<<"(filler)";
        }
        else
        {
            qInfo()<<"[VIP] Dummy frame"<<frame_counter<<"(empty)";
        }
    }
#endif

    step_cycle = 0;
    // Lock shared access.
    mtx_lines->lock();
    // Check if this is the first frame of a new source.
    if(new_file!=false)
    {
        new_file = false;
        // Put service line "new file" into queue.
        insertNewFileLine();
    }
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

    if(last_frame!=false)
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
}

//------------------------ Insert service line to signal that next lines will be for new file.
void VideoInFFMPEG::insertNewFileLine()
{
    VideoLine service_line;

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
    // Add suffix for selected primary color channel.
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
    file_name += "."+source_file.suffix().toStdString();        // File extension.
    // Set file path in service line.
    service_line.setServNewFile(file_name);
    service_line.frame_number = frame_counter;
    service_line.line_number = 0;
    // Put service line into output queue.
    outNewLine(&service_line);
}

//------------------------ Process first frame from the queue, received from FFMPEG.
void VideoInFFMPEG::processFrame()
{
    // Check if received frame is a dummy for a missed frame.
    if(evt_frames.front().height()==FFMPEGWrapper::DUMMY_HEIGTH)
    {
        int dummy_cnt;
        // Take number of dropped frames, encoded in width of the image.
        dummy_cnt = evt_frames.front().width();
        // Remove processed frame from the queue.
        evt_frames.pop_front();
        evt_double.pop_front();
        for(int32_t dummy_idx=0;dummy_idx<dummy_cnt;dummy_idx++)
        {
            // Insert a dummy in the output.
            // Note: [insertDummyFrame()] can call [QApplication::processEvents()] and slot [receiveFrame()] will alter [evt_frames].
            insertDummyFrame();
            QApplication::processEvents();
            if((event_usr&(EVT_USR_LOAD|EVT_USR_STOP|EVT_USR_UNLOAD))!=0)
            {
                break;
            }
        }
    }
    else
    {
        bool img_double;
        QImage task_img;
        // Make copy of the frame from the queue.
        task_img = evt_frames.front();
        img_double = evt_double.front();
        // Remove first frame from the queue.
        evt_frames.pop_front();
        evt_double.pop_front();
        // Splice new frame into individual video lines.
        // Note: [spliceFrame()] can call [QApplication::processEvents()] and slot [receiveFrame()] will alter [evt_frames] and [evt_double].
        spliceFrame(&task_img, img_double);
    }
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
#ifdef VIP_EN_DBG_OUT
        if((log_level&LOG_SETTINGS)!=0)
        {
            qInfo()<<"[VIP] New source requested:"<<in_path;
        }
#endif

        // Check file existance and availability.
        QFile file_check(in_path, this);
        if(file_check.exists()==false)
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] Requested file not found at the path:"<<in_path;
            emit mediaError(tr("Указанный файл источника не найден!"));
        }
        // Qt bugs out on Windows and returns only Owners flags
        /*else if((file_check.permissions()&QFileDevice::ReadUser)==0)
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] Not enough permissions (0x"+QString::number(file_check.permissions(), 16)+") to open file for read:"<<in_path;
            emit mediaError(tr("Недостаточно прав для чтения файла источника!"));
        }*/
        else
        {
            // Source is ready to be read.
            event_usr |= EVT_USR_LOAD;
            evt_source = in_path;
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

//------------------------ Dump capture event flags.
QString VideoInFFMPEG::logEventCapture(uint8_t in_evt)
{
    QString log_line;
    log_line = "Capture events: ";
    if((in_evt&EVT_CAP_OPEN)!=0)
    {
        log_line += "|OPEN";
    }
    if((in_evt&EVT_CAP_FRAME)!=0)
    {
        log_line += "|FRAME";
    }
    if((in_evt&EVT_CAP_CLOSE)!=0)
    {
        log_line += "|CLOSE";
    }
    if((in_evt&EVT_CAP_ERROR)!=0)
    {
        log_line += "|ERROR";
    }
    return log_line;
}

//------------------------ Dump capture user flags.
QString VideoInFFMPEG::logEventUser(uint8_t in_evt)
{
    QString log_line;
    log_line = "User events: ";
    if((in_evt&EVT_USR_LOAD)!=0)
    {
        log_line += "|LOAD";
    }
    if((in_evt&EVT_USR_PLAY)!=0)
    {
        log_line += "|PLAY";
    }
    if((in_evt&EVT_USR_PAUSE)!=0)
    {
        log_line += "|PAUSE";
    }
    if((in_evt&EVT_USR_STOP)!=0)
    {
        log_line += "|STOP";
    }
    if((in_evt&EVT_USR_UNLOAD)!=0)
    {
        log_line += "|UNLOAD";
    }
    return log_line;
}

//------------------------ Dump processing state.
QString VideoInFFMPEG::logState(uint8_t in_state)
{
    QString log_line;
    if(in_state==STG_IDLE)
    {
        log_line = "IDLE";
    }
    else if(in_state==STG_LOADING)
    {
        log_line = "LOAD";
    }
    else if(in_state==STG_STOP)
    {
        log_line = "STOP";
    }
    else if(in_state==STG_PLAY)
    {
        log_line = "PLAY";
    }
    else if(in_state==STG_PAUSE)
    {
        log_line = "PAUSE";
    }
    else if(in_state==STG_STOPPING)
    {
        log_line = "STOPPING";
    }
    else if(in_state==STG_REOPEN)
    {
        log_line = "REOPEN";
    }
    return log_line;
}

//------------------------ Main execution loop.
void VideoInFFMPEG::runFrameDecode()
{
    QElapsedTimer timer;

    qInfo()<<"[VIP] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
    // Check working pointers.
    if((out_lines==NULL)||(mtx_lines==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[VIP] Empty output pointer provided, unable to continue!";
        emit finished();
        return;
    }

    QString log_line;
    // Dump compile-time FFmpeg versions.
    log_line = QString::fromLocal8Bit(AV_STRINGIFY(LIBAVDEVICE_VERSION));
    qInfo()<<"[VIP] FFMPEG avcodec compile-time version:"<<log_line;
    log_line = QString::fromLocal8Bit(AV_STRINGIFY(LIBAVCODEC_VERSION));
    qInfo()<<"[VIP] FFMPEG avdevice compile-time version:"<<log_line;
    log_line = QString::fromLocal8Bit(AV_STRINGIFY(LIBAVUTIL_VERSION));
    qInfo()<<"[VIP] FFMPEG avutil compile-time version:"<<log_line;
    log_line = QString::fromLocal8Bit(AV_STRINGIFY(LIBSWSCALE_VERSION));
    qInfo()<<"[VIP] FFMPEG swscale compile-time version:"<<log_line;

    unsigned vers;
    // Dump run-time FFmpeg versions.
    vers = avdevice_version();
    log_line = QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    qInfo()<<"[VIP] FFMPEG avdevice run-time version:"<<log_line;
    vers = avcodec_version();
    log_line = QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    qInfo()<<"[VIP] FFMPEG avcodec run-time version:"<<log_line;
    vers = avutil_version();
    log_line = QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    qInfo()<<"[VIP] FFMPEG avutil run-time version:"<<log_line;
    vers = swscale_version();
    log_line = QString::number(AV_VERSION_MAJOR(vers))+
               "."+QString::number(AV_VERSION_MINOR(vers))+
               "."+QString::number(AV_VERSION_MICRO(vers))+
               " ("+QString::number(vers)+")";
    qInfo()<<"[VIP] FFMPEG swscale run-time version:"<<log_line;

    qInfo()<<"[VIP] Starting FFMPEG thread...";
    // Start the thread with FFMPEG wrapper.
    ffmpeg_thread->start(QThread::HighPriority);

    // Inf. loop in a thread.
    while(finish_work==false)
    {
        // Process Qt events (collect event flags).
        QApplication::processEvents();
        if(finish_work!=false)
        {
            // Break the loop and do nothing if got shutdown event.
            break;
        }
        if(proc_state==new_state)
        {
            // Current state is static, can process capture events.
            // Check source capture events.
            if(event_cap!=EVT_CAP_NO)
            {
                // Source capture events.
#ifdef VIP_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[VIP] State:"<<logState(proc_state);
                    qInfo()<<"[VIP]"<<logEventCapture(event_cap);
                }
#endif
                if((event_cap&EVT_CAP_CLOSE)!=0)
                {
                    // Source is closed.
                    event_cap &= ~(EVT_CAP_CLOSE|EVT_CAP_OPEN|EVT_CAP_FRAME);
                }
                if((event_cap&EVT_CAP_OPEN)!=0)
                {
                    // Source is opened and ready to read.
                    event_cap &= ~(EVT_CAP_OPEN|EVT_CAP_FRAME);
                    if(proc_state==STG_LOADING)
                    {
                        new_state = STG_STOP;
                    }
                    else
                    {
                        new_state = STG_IDLE;
                    }
                }
                if((event_cap&EVT_CAP_FRAME)!=0)
                {
                    // New frame received and put into [evt_frames] queue.
                    event_cap &= ~(EVT_CAP_FRAME);
                }
                if((event_cap&EVT_CAP_ERROR)!=0)
                {
                    // Capture error.
                    event_cap &= ~(EVT_CAP_ERROR);
                    if(proc_state==STG_LOADING)
                    {
                        // Was trying to opened the source but it never happened.
                        new_state = STG_IDLE;
#ifdef VIP_EN_DBG_OUT
                        if((log_level&LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[VIP] Unable to open file due to error:"<<evt_errcode;
                        }
#endif
                    }
                    else if((proc_state==STG_PLAY)||(proc_state==STG_PAUSE))
                    {
                        if(evt_errcode==FFMPEGWrapper::FFMERR_EOF)
                        {
                            // Playback stopped due to EOF.
                            new_state = STG_REOPEN;
#ifdef VIP_EN_DBG_OUT
                            if((log_level&LOG_PROCESS)!=0)
                            {
                                qInfo()<<"[VIP] Got to EOF, re-opening...";
                            }
#endif
                        }
                        else
                        {
                            new_state = STG_STOPPING;
#ifdef VIP_EN_DBG_OUT
                            if((log_level&LOG_PROCESS)!=0)
                            {
                                qInfo()<<"[VIP] Error"<<evt_errcode<<"while playing, stopping...";
                            }
#endif
                        }
                    }
                    // Report about the error.
                    if(evt_errcode!=FFMPEGWrapper::FFMERR_EOF)
                    {
                        if(evt_errcode==FFMPEGWrapper::FFMERR_NO_SRC)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось открыть файл");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_NO_INFO)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось получить информацию о потоке видео");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_NO_VIDEO)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось найти поток видео");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_NO_DECODER)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось найти декодер для видео");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_NO_RAM_DEC)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось выделить ОЗУ для декодера видео");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_DECODER_PARAM)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось установить параметры для декодера видео");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_DECODER_START)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось запустить декодер видео");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_NO_RAM_FB)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось выделить ОЗУ для буфера кадра");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_NO_RAM_READ)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось считать пакет данных из декодера");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_CONV_INIT)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось инициализировать преобразователь кадра");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_FRM_CONV)
                        {
                            last_error_txt = tr("FFMPEG: Не удалось преобразовать формат кадра видео");
                        }
                        else if(evt_errcode==FFMPEGWrapper::FFMERR_NOT_INIT)
                        {
                            last_error_txt = tr("FFMPEG: Не задан источник видео");
                        }
                        else
                        {
                            last_error_txt = tr("FFMPEG: Неизвестная ошибка чтения видео");
                        }
                        qWarning()<<DBG_ANCHOR<<"[VIP] FFMPEG error occured, code:"<<evt_errcode;
                        emit mediaError(last_error_txt);
                    }
                }
            }
        }
        if(proc_state==new_state)
        {
            // Current state is static, can process user events.
            if(event_usr!=EVT_USR_NO)
            {
                // User inputs.
#ifdef VIP_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[VIP] State:"<<logState(proc_state);
                    qInfo()<<"[VIP]"<<logEventUser(event_usr);
                }
#endif
                if((event_usr&EVT_USR_UNLOAD)!=0)
                {
                    // User requested any source to be closed/released.
                    // Clear events with less priority.
                    event_usr &= ~(EVT_USR_LOAD|EVT_USR_STOP|EVT_USR_PLAY|EVT_USR_PAUSE);
                    // Check current mode.
                    if((proc_state==STG_PLAY)||(proc_state==STG_PAUSE))
                    {
                        // Playing old source or on pause.
                        // First, go to STOPPING (to put service lines into queue), than repeat cycle.
                        new_state = STG_STOPPING;
                    }
                    else
                    {
                        event_usr &= ~(EVT_USR_UNLOAD);
                        new_state = STG_IDLE;
                    }
                }
                else if((event_usr&EVT_USR_LOAD)!=0)
                {
                    // User requested new source to be opened.
                    // Clear events with less priority.
                    event_usr &= ~(EVT_USR_STOP|EVT_USR_PLAY|EVT_USR_PAUSE);
                    // Check current mode.
                    if((proc_state==STG_PLAY)||(proc_state==STG_PAUSE))
                    {
                        // Playing old source or on pause.
                        // First, go to STOPPING (to put service lines into queue), than repeat cycle.
                        new_state = STG_STOPPING;
                        // Do not clear [EVT_USR_LOAD] so it can be picked up in [STG_STOPPING] state.
                    }
                    /*else if(proc_state==STG_STOPPING)
                    {
                        // Second part, after playback is properly finished, go to STOP.
                        event_usr &= ~(EVT_USR_LOAD);
                        new_state = STG_LOADING;
                    }*/
                    else if((proc_state==STG_IDLE)||(proc_state==STG_STOP))
                    {
                        // No playback and maybe no source.
                        event_usr &= ~(EVT_USR_LOAD);
                        new_state = STG_LOADING;
                    }
                }
                else if((event_usr&EVT_USR_STOP)!=0)
                {
                    // User requested STOP.
                    event_usr &= ~(EVT_USR_STOP|EVT_USR_PLAY|EVT_USR_PAUSE);
                    if((proc_state==STG_PLAY)||(proc_state==STG_PAUSE))
                    {
                        new_state = STG_REOPEN;
                    }
                    else if(proc_state==STG_IDLE)
                    {
                        emit mediaNotFound();
                    }
                }
                else if((event_usr&EVT_USR_PLAY)!=0)
                {
                    // User requested PLAY.
                    event_usr &= ~(EVT_USR_PLAY|EVT_USR_PAUSE);
                    if((proc_state==STG_STOP)||(proc_state==STG_PAUSE))
                    {
                        new_state = STG_PLAY;
                    }
                    else if(proc_state==STG_IDLE)
                    {
                        emit mediaNotFound();
                    }
                }
                else if((event_usr&EVT_USR_PAUSE)!=0)
                {
                    // User toggled PAUSE.
                    event_usr &= ~(EVT_USR_PAUSE);
                    if(proc_state==STG_PLAY)
                    {
                        // Currently playing: go to PAUSE.
                        new_state = STG_PAUSE;
                    }
                    else if(proc_state==STG_PAUSE)
                    {
                        // Currently on pause: go to PLAY.
                        new_state = STG_PLAY;
                    }
                    else if(proc_state==STG_IDLE)
                    {
                        emit mediaNotFound();
                    }
                }
            }
        }
        // Change states.
        if(proc_state!=new_state)
        {
            // State has to be changed.
#ifdef VIP_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[VIP] State:"<<logState(proc_state)<<"->"<<logState(new_state);
            }
#endif
            if(new_state==STG_IDLE)
            {
                // Reset old path and frame counter.
                resetCounters();
                // Report about changing source.
                emit mediaLoaded("");
                emit mediaStopped();
                emit closeDevice();
            }
            else if(new_state==STG_LOADING)
            {
                // Loading new source.
                if((proc_state==STG_STOP)||(proc_state==STG_REOPEN))
                {
                    // Previous state was STOP, indicating that some other source was opened.
                    emit closeDevice();
                }
                // Reset old path.
                resetCounters();
                // Report about changing source.
                emit mediaLoaded("");
                // Ask FFMPEG to open new source.
                emit openDevice(evt_source, "");
                // TODO: add FFMPEG watchdog.
            }
            else if(new_state==STG_STOP)
            {
                // Entering STOP mode.
                if(proc_state==STG_LOADING)
                {
                    // Previous state was loading a new source.
                    // Save new path to the source.
                    src_path = evt_source;
                    // Save new total frame count.
                    frames_total = evt_frame_cnt;
                    // Report about new opened source.
                    emit mediaLoaded(src_path);
                }
                else if((proc_state==STG_PLAY)||(proc_state==STG_PAUSE))
                {
                    // Previous state was active playback.
                }
                // Reset counters.
                frame_counter = 1;
                new_file = true;
                // Report about player entering STOP state (ready to play).
                emit mediaStopped();
            }
            else if(new_state==STG_PLAY)
            {
                // Ask for a frame from the source.
                askNextFrame();
                // Prevent multiple [askNextFrame()] runs while FFMPEG starts decoding.
                QThread::msleep(150);
                // Report about going into PLAY state.
                emit mediaPlaying(frames_total);
            }
            else if(new_state==STG_PAUSE)
            {
                // Report about going into PAUSE state.
                emit mediaPaused();
            }
            proc_state = new_state;
        }
        else
        {
            // State is static.
            if((proc_state==STG_IDLE)||(proc_state==STG_STOP))
            {
                // Wait for some input.
                QThread::msleep(250);
            }
            else if(proc_state==STG_PLAY)
            {
                if(evt_frames.size()>0)
                {
                    if(step_play!=false)
                    {
                        // Stepped playback enabled.
                        // Go to pause after this.
                        new_state = STG_PAUSE;
                    }
                    // Check if there is enough space in the internal frame queue.
                    else if(evt_frames.size()<10)
                    {
                        // Request next frame.
                        askNextFrame();
                    }
                    // Processing received frames.
                    processFrame();
                }
                else
                {
                    //askNextFrame();
                    QThread::msleep(5);
                }
            }
            else if(proc_state==STG_PAUSE)
            {
                // Finish playback already decoded frames.
                if(evt_frames.size()>0)
                {
                    // Processing received frames.
                    processFrame();
                }
                else
                {
                    QThread::msleep(100);
                }
            }
            else if(proc_state==STG_STOPPING)
            {
                // Finish playback already decoded frames.
                if(evt_frames.size()>0)
                {
                    // Processing received frames.
                    processFrame();
                }
                else
                {
                    // Insert trailing frame with service lines.
                    insertDummyFrame(true, false);
                    // Check if there is unprocessed new source event.
                    if((event_usr&EVT_USR_LOAD)!=0)
                    {
                        event_usr &= ~(EVT_USR_LOAD);
                        // Load new source.
                        new_state = STG_LOADING;
                    }
                    else
                    {
                        // Now safely go to STOP.
                        new_state = STG_STOP;
                    }
                }
            }
            else if(proc_state==STG_REOPEN)
            {
                // Finish playback already decoded frames.
                if(evt_frames.size()>0)
                {
                    // Processing received frames.
                    processFrame();
                }
                else
                {
                    // Insert trailing frame with service lines.
                    insertDummyFrame(true, false);
                    // Save current reopening path to event string ([src_path] will be cleared in [resetCounters()]).
                    evt_source = src_path;
                    // Reset everything.
                    resetCounters();
                    // Set new mode to LOADING to reload source from [evt_source].
                    new_state = STG_LOADING;
                }
            }
        }
        if(proc_state>=STG_MAX)
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] -------------------- Impossible state"<<proc_state<<"detected, breaking...";
            break;
        }
    }

    qInfo()<<"[VIP] Loop stop.";
    emit finished();
}

//------------------------ Start playback from current position.
void VideoInFFMPEG::mediaPlay()
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] User requested PLAY";
    }
#endif
    event_usr |= EVT_USR_PLAY;
}

//------------------------ Pause playback.
void VideoInFFMPEG::mediaPause()
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] User toggled PAUSE";
    }
#endif
    event_usr |= EVT_USR_PAUSE;
}

//------------------------ Stop playback.
void VideoInFFMPEG::mediaStop()
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] User requested STOP";
    }
#endif
    event_usr |= EVT_USR_STOP;
}

//------------------------ Close the source.
void VideoInFFMPEG::mediaUnload()
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] User requested UNLOAD";
    }
#endif
    event_usr |= EVT_USR_UNLOAD;
}

//------------------------ Set the flag to break execution loop and exit.
void VideoInFFMPEG::stop()
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] Received termination request";
    }
#endif
    finish_work = true;
    mediaStop();
    proc_state = new_state = STG_STOP;
}

//------------------------ Capture is opened and ready.
void VideoInFFMPEG::captureReady(int in_width, int in_height, uint32_t in_frames, float in_fps)
{
    // Decoder init ok, playback ready to be started.
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] New source,"<<in_frames<<"frames at resolution:"<<in_width<<"x"<<in_height<<"@"<<in_fps;
    }
#endif
    evt_frame_cnt = in_frames;
    event_cap |= EVT_CAP_OPEN;
    src_open = true;
}


//------------------------ Capture is closed.
void VideoInFFMPEG::captureClosed()
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] Capture has closed";
    }
#endif
    event_cap |= EVT_CAP_CLOSE;
    src_open = false;
}

//------------------------ Something went wrong with capture process.
void VideoInFFMPEG::captureError(uint32_t in_err)
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[VIP] Error occured during capture";
    }
#endif
    evt_errcode = in_err;
    event_cap |= EVT_CAP_ERROR;
}

//------------------------ Receive new frame from FFMPEG.
void VideoInFFMPEG::receiveFrame(QImage in_frame, bool in_double)
{
#ifdef VIP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        if(in_frame.isNull()==false)
        {
            qInfo()<<"[VIP] Received new frame"<<in_frame.width()<<"x"<<in_frame.height()<<", current frame:"<<frame_counter<<", queue:"<<evt_frames.size();
        }
        else
        {
            qWarning()<<DBG_ANCHOR<<"[VIP] Received NULL frame! Frame index:"<<frame_counter;
        }
    }
#endif
    if(in_frame.isNull()==false)
    {
        evt_frames.push_back(in_frame);
        evt_double.push_back(in_double);
        event_cap |= EVT_CAP_FRAME;
    }
}
