#include "videotodigital.h"

VideoToDigital::VideoToDigital(QObject *parent) : QObject(parent)
{
    in_video = NULL;
    out_pcm1 = NULL;
    out_pcm16x0 = NULL;
    out_stc007 = NULL;
    mtx_vid = NULL;
    mtx_pcm1 = NULL;
    mtx_pcm16x0 = NULL;
    mtx_stc007 = NULL;
    log_level = 0;
    pcm_type = TYPE_STC007;
    pcm_sample_fmt = PCM_FMT_NOT_SET;
    binarization_mode = Binarizer::MODE_NORMAL;
    signal_quality.clear();
    line_dump_help_done = false;
    coordinate_damper = true;
    check_line_copy = true;
    reset_stats = true;
    finish_work = false;
}

//------------------------ Set pointers to shared input data.
void VideoToDigital::setInputPointers(std::deque<VideoLine> *in_vline, QMutex *mtx_vline)
{
    if((in_vline==NULL)||(mtx_vline==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[V2D] Empty input pointer provided, unable to apply!";
    }
    else
    {
        in_video = in_vline;
        mtx_vid = mtx_vline;
    }
}

//------------------------ Set pointers to shared PCM-1 output data.
void VideoToDigital::setOutPCM1Pointers(std::deque<PCM1Line> *out_pcmline, QMutex *mtx_pcmline)
{
    if((out_pcmline==NULL)||(mtx_pcmline==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[V2D] Empty output pointer provided, unable to apply!";
    }
    else
    {
        out_pcm1 = out_pcmline;
        mtx_pcm1 = mtx_pcmline;
    }
}

//------------------------ Set pointers to shared PCM-16x0 output data.
void VideoToDigital::setOutPCM16X0Pointers(std::deque<PCM16X0SubLine> *out_pcmline, QMutex *mtx_pcmline)
{
    if((out_pcmline==NULL)||(mtx_pcmline==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[V2D] Empty output pointer provided, unable to apply!";
    }
    else
    {
        out_pcm16x0 = out_pcmline;
        mtx_pcm16x0 = mtx_pcmline;
    }
}

//------------------------ Set pointers to shared STC-007 output data.
void VideoToDigital::setOutSTC007Pointers(std::deque<STC007Line> *out_pcmline, QMutex *mtx_pcmline)
{
    if((out_pcmline==NULL)||(mtx_pcmline==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[V2D] Empty output pointer provided, unable to apply!";
    }
    else
    {
        out_stc007 = out_pcmline;
        mtx_stc007 = mtx_pcmline;
    }
}

//------------------------ Wait until one full frame is in the input queue.
bool VideoToDigital::waitForOneFrame()
{
    bool frame_lock;
    std::deque<VideoLine>::iterator buf_scaner;

    frame_lock = false;

    // Pick start of the queue.
    buf_scaner = in_video->begin();

    // Scan the buffer until there is nothing in the input.
    while(buf_scaner!=in_video->end())
    {
        // Still at the first frame in the buffer.
        if(frame_lock==false)
        {
            // End of the frame is not detected yet.
            // Check if there is service tag for the end of the frame.
            if((*buf_scaner).isServEndFrame()!=false)
            {
                // Got to the end of the first frame: one full frame is in the buffer.
                frame_lock = true;
#ifdef LB_EN_DBG_OUT
                if((log_level&Binarizer::LOG_PROCESS)!=0)
                {
                    qInfo()<<"[V2D] Detected end of a frame, exiting search...";
                }
#endif
                break;
            }
        }
        // Go to the next line in the input.
        buf_scaner++;
    }

    if(frame_lock!=false)
    {
#ifdef LB_EN_DBG_OUT
        if((log_level&Binarizer::LOG_PROCESS)!=0)
        {
            qInfo()<<"[V2D] Dumping one frame into internal buffer...";
        }
#endif
        while(in_video->size()>0)
        {
            frame_buf.push_back(in_video->front());
            in_video->pop_front();
            if(frame_buf.back().isServEndFrame()!=false)
            {
#ifdef LB_EN_DBG_OUT
                if((log_level&Binarizer::LOG_PROCESS)!=0)
                {
                    qInfo()<<"[V2D] One frame buffered";
                }
#endif
                break;
            }
        }
    }

    return frame_lock;
}

//------------------------ Pre-scan whole buffer and calculate average data coordinates.
void VideoToDigital::prescanCoordinates(CoordinatePair *out_coords, uint8_t *out_ref)
{
    // TODO: prescan most of the frame in INSANE mode.
    bool suppress_log;
    uint8_t index;
    uint16_t lines_cnt, gap_length;
    std::vector<uint16_t> scan_ofs;
    std::vector<uint8_t> refs_list;
    std::vector<CoordinatePair> coord_list;
    VideoLine *source_line;
    PCM1Line pcm1_line;
    PCM16X0SubLine pcm16x0_line;
    STC007Line stc007_line;
    PCMLine *work_line;
    bin_preset_t bin_set;

    suppress_log = ((log_level&(Binarizer::LOG_PROCESS))==0);
    //suppress_log = false;

    if((out_coords==NULL)||(out_ref==NULL))
    {
#ifdef LB_EN_DBG_OUT
        qWarning()<<"[V2D] Unable to prescan, null pointer provided";
#endif
        return;
    }

    lines_cnt = frame_buf.size();
    bin_set = line_converter.getCurrentFineSettings();
    if((lines_cnt<=COORD_CHECK_PARTS)
       ||(pcm_type==TYPE_STC007)||(pcm_type==TYPE_M2)
       ||(bin_set.en_force_coords!=false))
    {
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if((pcm_type==TYPE_STC007)||(pcm_type==TYPE_M2))
            {
                qInfo()<<"[V2D] Unable to prescan, its disabled for STC-007";
            }
            else if(bin_set.en_force_coords!=false)
            {
                qInfo()<<"[V2D] Unable to prescan, its disabled in fine binarization settings";
            }
            else if(lines_cnt<=COORD_CHECK_PARTS)
            {
                QString log_line;
                log_line.sprintf("[V2D] Unable to prescan, not enough lines in the buffer: [%03u]<[%03u]",
                                 lines_cnt, COORD_CHECK_PARTS);
                qInfo()<<log_line;
            }
        }
#endif
        return;
    }

    scan_ofs.resize(COORD_CHECK_LINES);
    refs_list.reserve(COORD_CHECK_LINES);
    coord_list.reserve(COORD_CHECK_LINES);
    // Calculate line gap between checked lines.
    gap_length = lines_cnt/(COORD_CHECK_PARTS-1);
    // Calculate line offsets to perform scan.
    for(index=0;index<COORD_CHECK_LINES;index++)
    {
        scan_ofs[index] = (index+1)*gap_length;
    }
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        qInfo()<<"[V2D] ---------- Starting frame data coordinates prescan...";
        log_line = "[V2D] Prescan buffer indexes: ";
        for(index=0;index<COORD_CHECK_LINES;index++)
        {
            log_line += QString::number(scan_ofs[index]);
            if(index!=(COORD_CHECK_LINES-1))
            {
                log_line += ", ";
            }
        }
        log_line += " (scan gap: "+QString::number(gap_length)+" of "+QString::number(lines_cnt)+" lines)";
        qInfo()<<log_line;
    }
#endif
    // Reset everything in binarizer.
    line_converter.setGoodParameters();
    // Enable coordinates search.
    line_converter.setCoordinatesSearch(true);
    if(pcm_type==TYPE_PCM1)
    {
        // Process video line as single PCM line.
        line_converter.setLinePartMode(Binarizer::FULL_LINE);
        // Take pointer to PCM-1 line object.
        work_line = static_cast<PCMLine *>(&pcm1_line);
    }
    else if(pcm_type==TYPE_PCM16X0)
    {
        // Right part of the video line (it usually has narrower window of good coordinates).
        line_converter.setLinePartMode(Binarizer::PART_PCM16X0_RIGHT);
        // Take pointer to PCM-16X0 line object.
        work_line = static_cast<PCMLine *>(&pcm16x0_line);
    }
    else
    {
        // Process video line as single PCM line.
        line_converter.setLinePartMode(Binarizer::FULL_LINE);
        // Take pointer to STC-007 line object.
        work_line = static_cast<PCMLine *>(&stc007_line);
    }
    // Set pointer to output PCM line for binarizer.
    line_converter.setOutput(work_line);

    for(index=0;index<COORD_CHECK_LINES;index++)
    {
        // Pick source line from the buffer.
        source_line = &frame_buf.at(scan_ofs[index]);
        if(source_line->isServiceLine()==false)
        {
            line_converter.setSource(source_line);
            line_converter.setMode(binarization_mode);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                QString log_line;
                log_line = "[V2D] ---------- Starting prescan at frame buffer index [";
                log_line += QString::number(scan_ofs[index]);
                log_line += "] line ["+QString::number(source_line->frame_number)+":"+QString::number(source_line->line_number)+"]...";
                qInfo()<<log_line;
            }
#endif
            // Perform binarization.
            line_converter.processLine();
            // Check if CRC is valid for the line.
            if(work_line->isCRCValid()!=false)
            {
                // Save found coordinates.
                coord_list.push_back(work_line->coords);
                // Save reference level for valid coordinates.
                refs_list.push_back(work_line->ref_level);
                // Update binarizator coordinates.
                //line_converter.setGoodParameters(work_line);
                //if(coord_list.size()>2) break;
            }
        }
    }
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        if(coord_list.size()>0)
        {
            qInfo()<<"[V2D] Coordinates list:";
            for(index=0;index<coord_list.size();index++)
            {
                log_line.sprintf("[%03d:%04d]@[%03u] at line index [%03u]",
                                 coord_list[index].data_start, coord_list[index].data_stop, refs_list[index], scan_ofs[index]);
                qInfo()<<log_line;
            }
        }
    }
#endif
    // Check if any coordinates were found.
    if(coord_list.size()>0)
    {
        // Sort coordinates.
        std::sort(coord_list.begin(), coord_list.end());
        // Pick center value.
        *out_coords = coord_list.at(coord_list.size()/2);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[V2D] Prescan is finished, coordinates: [%03d:%04d] from [%01u] lines of [%01u]",
                             out_coords->data_start, out_coords->data_stop, coord_list.size(), COORD_CHECK_LINES);
            qInfo()<<log_line;
        }
#endif
        // Sort reference levels.
        std::sort(refs_list.begin(), refs_list.end());
        // Pick center value.
        *out_ref = refs_list.at(refs_list.size()/2);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[V2D] Reference level selected: [%03u]", (*out_ref));
            qInfo()<<log_line;
        }
#endif
    }
#ifdef LB_EN_DBG_OUT
    else
    {
        if(suppress_log==false)
        {
            qInfo()<<"[V2D] Prescan is done, no valid coordinates found";
        }
    }
#endif
}

//------------------------ Apply median filter to the list of coordinates.
CoordinatePair VideoToDigital::medianCoordinates(std::deque<CoordinatePair> *in_list)
{
    CoordinatePair dummy;
    if(in_list->size()>0)
    {
        size_t buf_middle;
        std::deque<CoordinatePair> sort_buf;

        // Copy list for sorting (so source will not be affected).
        std::copy(in_list->begin(), in_list->end(), std::inserter(sort_buf, sort_buf.begin()));
        // Calculate the middle of the list (for median filtering).
        buf_middle = sort_buf.size()/2;
        // Put sorted value in that spot.
        std::nth_element(sort_buf.begin(), sort_buf.begin()+buf_middle, sort_buf.end());
        // Return filtered value.
        return sort_buf[buf_middle];
    }
    else
    {
        // Nothing in the input list.
        // Return dummy invalid coordinates.
        return dummy;
    }
}

//------------------------ Output PCM line into output queue (blocking).
void VideoToDigital::outNewLine(PCMLine *in_line)
{
    size_t queue_size;
    bool size_lock;
    size_lock = false;

    if(in_line->getPCMType()==PCMLine::TYPE_PCM1)
    {
        //return;
        if((out_pcm1==NULL)||(mtx_pcm1==NULL))
        {
            qWarning()<<DBG_ANCHOR<<"[V2D] Empty PCM-1 pointer provided, result discarded!";
        }
        else
        {
            while(1)
            {
                // Put processed line into the queue.
                mtx_pcm1->lock();
                queue_size = (out_pcm1->size()+1);
                if(queue_size<MAX_PCMLINE_QUEUE_SIZE)
                {
                    out_pcm1->push_back(*static_cast<PCM1Line *>(in_line));
                    mtx_pcm1->unlock();
                    if((in_line->isServiceLine()==false)||(in_line->isServFiller()!=false))
                    {
                        // Duplicate new line through event.
                        emit newLine(*static_cast<PCM1Line *>(in_line));
                    }
                    if(size_lock!=false)
                    {
                        size_lock = false;
#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[V2D] Output PCM-1 line queue has some space, continuing...";
                        }
#endif
                    }
                    break;
                }
                else
                {
                    mtx_pcm1->unlock();
                    if(size_lock==false)
                    {
                        size_lock = true;
#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[V2D] Output PCM-1 line queue is at size limit ("<<MAX_PCMLINE_QUEUE_SIZE<<"), waiting...";
                        }
#endif
                    }
                    // Wait for queue to free up.
                    QThread::msleep(50);
                }
            }
        }
    }
    else if(in_line->getPCMType()==PCMLine::TYPE_PCM16X0)
    {
        //return;
        if((out_pcm16x0==NULL)||(mtx_pcm16x0==NULL))
        {
            qWarning()<<DBG_ANCHOR<<"[V2D] Empty PCM-16x0 pointer provided, result discarded!";
        }
        else
        {
            while(1)
            {
                // Put processed line into the queue.
                mtx_pcm16x0->lock();
                queue_size = (out_pcm16x0->size()+1);
                if(queue_size<(MAX_PCMLINE_QUEUE_SIZE*PCM16X0SubLine::SUBLINES_PER_LINE))
                {
                    out_pcm16x0->push_back(*static_cast<PCM16X0SubLine *>(in_line));
                    mtx_pcm16x0->unlock();
                    if((in_line->isServiceLine()==false)||(in_line->isServFiller()!=false))
                    {
                        // Duplicate new line through event.
                        emit newLine(*static_cast<PCM16X0SubLine *>(in_line));
                    }
                    if(size_lock!=false)
                    {
                        size_lock = false;
#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[V2D] Output PCM-16x0 line queue has some space, continuing...";
                        }
#endif
                    }
                    break;
                }
                else
                {
                    mtx_pcm16x0->unlock();
                    if(size_lock==false)
                    {
                        size_lock = true;
#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[V2D] Output PCM-16x0 line queue is at size limit ("<<(MAX_PCMLINE_QUEUE_SIZE*PCM16X0SubLine::SUBLINES_PER_LINE)<<"), waiting...";
                        }
#endif
                    }
                    // Wait for queue to free up.
                    QThread::msleep(50);
                }
            }
        }
    }
    else if(in_line->getPCMType()==PCMLine::TYPE_STC007)
    {
        //return;
        if((out_stc007==NULL)||(mtx_stc007==NULL))
        {
            qWarning()<<DBG_ANCHOR<<"[V2D] Empty STC-007 pointer provided, result discarded!";
        }
        else
        {
            while(1)
            {
                // Put processed line into the queue.
                mtx_stc007->lock();
                queue_size = (out_stc007->size()+1);
                //qInfo()<<"[V2D] Sizes:"<<sizeof(STC007Line)<<queue_size<<MAX_PCMLINE_QUEUE_SIZE;
                if(queue_size<MAX_PCMLINE_QUEUE_SIZE)
                {
                    out_stc007->push_back(*static_cast<STC007Line *>(in_line));
                    mtx_stc007->unlock();
                    if((in_line->isServiceLine()==false)||(in_line->isServFiller()!=false))
                    {
                        // Duplicate new line through event.
                        emit newLine(*static_cast<STC007Line *>(in_line));
                    }
                    if(size_lock!=false)
                    {
                        size_lock = false;
#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[V2D] Output STC-007 line queue has some space, continuing...";
                        }
#endif
                    }
                    break;
                }
                else
                {
                    mtx_stc007->unlock();
                    if(size_lock==false)
                    {
                        size_lock = true;
#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[V2D] Output STC-007 line queue is at size limit ("<<MAX_PCMLINE_QUEUE_SIZE<<"), waiting...";
                        }
#endif
                    }
                    // Wait for queue to free up.
                    QThread::msleep(50);
                }
            }
        }
    }
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void VideoToDigital::setLogLevel(uint8_t new_log)
{
    log_level = new_log;
    if((log_level&Binarizer::LOG_LINE_DUMP)==0)
    {
        line_dump_help_done = false;
    }
    line_converter.setLogLevel(log_level);
}

//------------------------ Set PCM type in source video.
void VideoToDigital::setPCMType(uint8_t in_pcm)
{
    line_dump_help_done = false;
#ifdef LB_EN_DBG_OUT
    if((log_level&Binarizer::LOG_SETTINGS)!=0)
    {
        if(in_pcm==TYPE_PCM1)
        {
            qInfo()<<"[V2D] PCM type set to 'PCM-1'.";
        }
        else if(in_pcm==TYPE_PCM16X0)
        {
            qInfo()<<"[V2D] PCM type set to 'PCM-1600/1610/1630'.";
        }
        else if(in_pcm==TYPE_STC007)
        {
            qInfo()<<"[V2D] PCM type set to 'STC-007/008/PCM-F1'.";
        }
        else if(in_pcm==TYPE_M2)
        {
            qInfo()<<"[V2D] PCM type set to 'M2'.";
        }
        else
        {
            qInfo()<<"[V2D] Unknown PCM type provided, ignored!";
        }
    }
#endif
    if(in_pcm<TYPE_MAX)
    {
        // Set value from the input.
        pcm_type = in_pcm;
        // Special case for M2, using STC-007 processing but with different sample format.
        if(pcm_type==TYPE_M2)
        {
            pcm_type = TYPE_STC007;
            pcm_sample_fmt = PCM_FMT_M2;
        }
        else
        {
            pcm_sample_fmt = PCM_FMT_NOT_SET;
        }
        // Reset "good" settings.
        line_converter.setGoodParameters();
        // Clear stats.
        reset_stats = true;
    }
}

//------------------------ Set binarizator mode.
void VideoToDigital::setBinarizationMode(uint8_t in_mode)
{
#ifdef LB_EN_DBG_OUT
    if(binarization_mode!=in_mode)
    {
        if((log_level&Binarizer::LOG_SETTINGS)!=0)
        {
            if(in_mode==Binarizer::MODE_DRAFT)
            {
                qInfo()<<"[V2D] Binarization mode set to 'draft'.";
            }
            else if(in_mode==Binarizer::MODE_FAST)
            {
                qInfo()<<"[V2D] Binarization mode set to 'fast'.";
            }
            else if(in_mode==Binarizer::MODE_NORMAL)
            {
                qInfo()<<"[V2D] Binarization mode set to 'normal'.";
            }
            else if(in_mode==Binarizer::MODE_INSANE)
            {
                qInfo()<<"[V2D] Binarization mode set to 'insane'.";
            }
            else
            {
                qInfo()<<"[V2D] Unknown binarization mode provided, ignored!";
            }
        }
    }
#endif
    if(in_mode<Binarizer::MODE_MAX)
    {
        // Set value from the input.
        binarization_mode = in_mode;
    }
}

//------------------------ Set line duplication detection mode.
void VideoToDigital::setCheckLineDup(bool flag)
{
#ifdef LB_EN_DBG_OUT
    if(check_line_copy!=flag)
    {
        if((log_level&Binarizer::LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[V2D] Line duplication detection set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[V2D] Line duplication detection set to 'enabled'.";
            }
        }
    }
#endif
    check_line_copy = flag;
}

//------------------------ Set fine binarization settings.
void VideoToDigital::setFineSettings(bin_preset_t in_set)
{
    // Clear stats.
    reset_stats = true;
    // Set binarization fine settings.
    line_converter.setFineSettings(in_set);
    // Report about new fine settings.
    emit guiUpdFineSettings(in_set);
}

//------------------------ Set fine binarization settings to defaults.
void VideoToDigital::setDefaultFineSettings()
{
    bin_preset_t tmp_set;
    // Clear stats.
    reset_stats = true;
    // Reset binarization fine settings.
    tmp_set = line_converter.getDefaultFineSettings();
    line_converter.setFineSettings(tmp_set);
    // Report about new fine settings.
    emit guiUpdFineSettings(tmp_set);
}

//------------------------ Get current fine binarization settings.
void VideoToDigital::requestCurrentFineSettings()
{
    bin_preset_t tmp_set;
    tmp_set = line_converter.getCurrentFineSettings();
    emit guiUpdFineSettings(tmp_set);
}

//------------------------ Main processing loop.
void VideoToDigital::doBinarize()
{
    bool force_bad_line, even_line;
    uint8_t sub_line_cnt;
    uint8_t field_state;
    uint8_t prescan_ref;
    uint16_t line_in_field_cnt;
    uint16_t good_coords_in_field, pcm_lines_in_field;
    CoordinatePair frame_avg, target_coord, coord_delta;
    std::deque<CoordinatePair> last_coord_stats;    // Last [COORD_HISTORY_DEPTH] lines' valid data coordinates history.
    std::deque<CoordinatePair> frame_coord_stats;   // Queue of all valid data coordinates for the current frame.
    std::deque<CoordinatePair> long_coord_stats;    // Last [COORD_LONG_HISTORY] frames' valid data coordinates history.
    quint64 time_spent;
    VideoLine source_line;

    bin_preset_t bin_set;
    PCM1Line pcm1_line;
    PCM16X0SubLine pcm16x0_line;
    STC007Line stc007_line;
    PCMLine *work_line;
    PCM1Line last_pcm1_line;
    PCM16X0SubLine last_pcm16x0_p0_line, last_pcm16x0_p1_line, last_pcm16x0_p2_line;
    STC007Line last_stc007_line;
    //ArVidLine arvid_line;

#ifdef LB_EN_DBG_OUT
    qInfo()<<"[V2D] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
    qInfo()<<"[V2D] Damper buffer depth:"<<COORD_HISTORY_DEPTH;
#endif

    work_line = NULL;
    sub_line_cnt = 0;
    prescan_ref = 128;
    line_in_field_cnt = 0;
    good_coords_in_field = pcm_lines_in_field = 0;
    signal_quality.clear();
    // Check working pointers.
    if((in_video==NULL)||(mtx_vid==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[V2D] Empty video pointer provided, unable to continue!";
        emit finished();
        return;
    }

    QElapsedTimer time_per_line, time_per_frame;

    // Inf. loop in a thread.
    while(finish_work==false)
    {
        // Process Qt events.
        QApplication::processEvents();

        if(finish_work!=false)
        {
            // Break the loop and do nothing if got shutdown event.
            break;
        }
        // Lock shared access.
        mtx_vid->lock();
        if(in_video->empty()!=false)
        {
            // Input queue is empty.
            // Unlock shared access.
            mtx_vid->unlock();
            // Wait (~a frame) for input to fill up.
            QThread::msleep(50);
        }
        else
        {
            // Wait for sufficient data in the input queue.
            if(waitForOneFrame()!=false)
            {
                // One full frame is available.
                // Unlock shared access.
                mtx_vid->unlock();
                time_per_frame.start();
                // Reset field state to "searching new field".
                field_state = FIELD_NEW;
                // New frame started: reset "good coordinates found" counter (for PCM-16x0).
                good_coords_in_field = pcm_lines_in_field = 0;

                if(reset_stats!=false)
                {
                    reset_stats = false;
                    // Reset stats.
                    last_coord_stats.clear();
                    frame_coord_stats.clear();
                    long_coord_stats.clear();
                    target_coord.clear();
                    frame_avg.clear();
                    // Reset "good" parameters.
                    line_converter.setGoodParameters();
                }

                // Save fine settings for the current frame.
                bin_set = line_converter.getCurrentFineSettings();

                // Try to prepare some averaged target horizontal data coordinates.
                frame_avg.clear();
                // Check if forced horizontal data coordinates are set.
                if(bin_set.en_force_coords==false)
                {
                    if(binarization_mode!=Binarizer::MODE_DRAFT)
                    {
                        // Find average coordinates for the frame buffer (usefull for markerless PCM formats).
                        // Don't run this in fastest mode, cause it takes some time.
                        prescanCoordinates(&frame_avg, &prescan_ref);
                    }
                    // Check if frame prescan returned valid coordinates.
                    if(frame_avg.areValid()==false)
                    {
                        // No valid coordinates with prescan.
                        // Take average from multi-frame stats.
                        frame_avg = medianCoordinates(&long_coord_stats);
                    }
                    else
                    {
                        // Valid coordinates were found during prescan.
                        // Set reference level by prescan.
                        line_converter.setReferenceLevel(prescan_ref);
                    }
                    // Re-check coordinates again.
                    if(frame_avg.areValid()!=false)
                    {
                        // Something valid was found, set it as target data coordinates.
                        line_converter.setDataCoordinates(frame_avg.data_start, frame_avg.data_stop);
                    }
                }

                // Process the buffer line by line.
                while(frame_buf.empty()==false)
                {
                    // Pick first line in the buffer.
                    source_line = frame_buf.front();
                    frame_buf.pop_front();

                    // Determine if current line is from even field.
                    even_line = ((source_line.line_number%2)==0);

                    line_converter.setLogLevel(log_level);
                    // Set parameters for converter.
                    line_converter.setMode(binarization_mode);
                    line_converter.setSource(&source_line);

                    if(pcm_type==TYPE_PCM16X0)
                    {
                        // Preset number of passes on one video line for PCM-16x0.
                        sub_line_cnt = PCM16X0SubLine::SUBLINES_PER_LINE;
                    }
                    else
                    {
                        // Preset default setting: 1 video line = 1 PCM line.
                        sub_line_cnt = 1;
                    }

                    force_bad_line = false;
                    // Cycle through sub-lines.
                    while(sub_line_cnt>0)
                    {
                        sub_line_cnt--;
                        time_per_line.start();

                        // ArVid Audio Debug
                        //pcm_type = PCMLine::TYPE_ARVA;

                        // Preset settings for binarizator for selected PCM type.
                        if(pcm_type==TYPE_PCM1)
                        {
                            // Process video line as single PCM-1 line.
                            line_converter.setLinePartMode(Binarizer::FULL_LINE);
                            // Check for "real-time" modes.
                            if((binarization_mode==Binarizer::MODE_DRAFT)||(binarization_mode==Binarizer::MODE_FAST))
                            {
                                // Let binarizator run data search until enough lines has passed.
                                if((good_coords_in_field>2)||(pcm_lines_in_field>2))
                                {
                                    // Disable coordinates search for fast modes if good enough coordinates were found.
                                    line_converter.setCoordinatesSearch(false);
#ifdef LB_EN_DBG_OUT
                                    if((good_coords_in_field==3)&&((log_level&Binarizer::LOG_PROCESS)!=0))
                                    {
                                        qInfo()<<"[V2D] Enough good CRCs in the frame, coordinates search is disabled for speed.";
                                    }
#endif
                                }
                                else
                                {
                                    // Enable coordinates search.
                                    line_converter.setCoordinatesSearch(true);
                                }
                            }
                            else
                            {
                                // Always enable coordinates search for non-realtime modes.
                                line_converter.setCoordinatesSearch(true);
                            }
                            // Make pointer to PCM-1 line object.
                            work_line = static_cast<PCMLine *>(&pcm1_line);
                        }
                        else if(pcm_type==TYPE_PCM16X0)
                        {
                            // In this format one video line contains three PCM-16x0 sub-lines.
                            // Pick part of the line to process.
                            if(sub_line_cnt==(PCM16X0SubLine::SUBLINES_PER_LINE-1))
                            {
                                // Left part of the video line.
                                line_converter.setLinePartMode(Binarizer::PART_PCM16X0_LEFT);
                            }
                            else if(sub_line_cnt==(PCM16X0SubLine::SUBLINES_PER_LINE-2))
                            {
                                // Middle part of the video line.
                                line_converter.setLinePartMode(Binarizer::PART_PCM16X0_MIDDLE);
                            }
                            else if(sub_line_cnt==(PCM16X0SubLine::SUBLINES_PER_LINE-3))
                            {
                                // Right part of the video line.
                                line_converter.setLinePartMode(Binarizer::PART_PCM16X0_RIGHT);
                            }
                            else
                            {
                                qWarning()<<DBG_ANCHOR<<"[V2D] Illegal line part detected:"<<sub_line_cnt;
                                continue;
                            }

                            // Check for "real-time" modes.
                            if((binarization_mode==Binarizer::MODE_DRAFT)||(binarization_mode==Binarizer::MODE_FAST))
                            {
                                // Let binarizator run data search until enough lines has passed.
                                if((good_coords_in_field>9)||(pcm_lines_in_field>15))
                                {
                                    // Disable coordinates search for fast modes if good enough coordinates were found.
                                    line_converter.setCoordinatesSearch(false);
#ifdef LB_EN_DBG_OUT
                                    if((good_coords_in_field==4)&&((log_level&Binarizer::LOG_PROCESS)!=0))
                                    {
                                        qInfo()<<"[V2D] Enough good CRCs in the frame, coordinates search is disabled for speed.";
                                    }
#endif
                                }
                                else
                                {
                                    // Enable coordinates search.
                                    line_converter.setCoordinatesSearch(true);
                                }
                            }
                            else
                            {
                                // Always enable coordinates search for non-realtime modes.
                                line_converter.setCoordinatesSearch(true);
                            }
                            // Make pointer to PCM-16X0 line object.
                            work_line = static_cast<PCMLine *>(&pcm16x0_line);
                        }
                        else if(pcm_type==TYPE_STC007)
                        {
                            // Process video line as single PCM line.
                            line_converter.setLinePartMode(Binarizer::FULL_LINE);
                            // Always enable coordinates search, it's fast for STC-007 because of markers.
                            line_converter.setCoordinatesSearch(true);
                            // Take pointer to STC-007 line object.
                            work_line = static_cast<PCMLine *>(&stc007_line);
                        }
                        /*else if(pcm_type==TYPE_ARVA)
                        {
                            // Process video line as single PCM line.
                            line_converter.setLinePartMode(Binarizer::FULL_LINE);
                            // Always enable coordinates search, it's fast for STC-007 because of markers.
                            line_converter.setCoordinatesSearch(false);
                            // Take pointer to ArVid Audio line object.
                            work_line = static_cast<PCMLine *>(&arvid_line);
                        }*/
                        else
                        {
                            qWarning()<<DBG_ANCHOR<<"[V2D] Unknown PCM type provided:"<<pcm_type;
                            continue;
                        }
                        // Set pointer to output PCM line for binarizer.
                        line_converter.setOutput(work_line);

#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                        {
                            QString log_line, sprint_line;
                            log_line = "[V2D] Frame average coordinates: ";
                            if(frame_avg.areValid()==false)
                            {
                                sprint_line = "[N/A: N/A]";
                            }
                            else
                            {
                                sprint_line.sprintf("[%03d:%04d]", frame_avg.data_start, frame_avg.data_stop);
                            }
                            log_line += sprint_line+", last target: ";
                            if(target_coord.areValid()==false)
                            {
                                sprint_line = "[N/A: N/A]";
                            }
                            else
                            {
                                sprint_line.sprintf("[%03d:%04d]", target_coord.data_start, target_coord.data_stop);
                            }
                            log_line += sprint_line;
                            qInfo()<<log_line;
                        }
#endif

                        // Convert video line into PCM line.
                        // (perorm AGC, TBC, binarization)
                        line_converter.processLine();

                        // Check if processed line contains service tags.
                        if(work_line->isServiceLine()!=false)
                        {
                            // Make no more than one service line from a video line (for PCM-16x0).
                            sub_line_cnt = 0;
                            // Current line contains service line.
                            if((work_line->isServNewFile()!=false)||(work_line->isServEndFile()!=false))
                            {
                                // New file has started or playback ended.
                                line_in_field_cnt = 0;
                                // Reset stats.
                                last_coord_stats.clear();
                                frame_coord_stats.clear();
                                long_coord_stats.clear();
                                target_coord.clear();

                                if((work_line->isServEndFile()!=false)||(frame_avg.areValid()==false))
                                {
                                    // Reset data for the new source.
                                    line_converter.setGoodParameters();
                                }
#ifdef LB_EN_DBG_OUT
                                if((log_level&Binarizer::LOG_PROCESS)!=0)
                                {
                                    qInfo()<<"[V2D] Stats and settings are reset for new source";
                                }
#endif
                            }
                            else if(work_line->isServEndField()!=false)
                            {
                                // Video field has ended.
                                // Set state to "searching new field".
                                field_state = FIELD_NEW;
                                line_in_field_cnt = 0;
                                // New field started: reset "good coordinates found" counter.
                                good_coords_in_field = 0;
                                pcm_lines_in_field = 0;
                                // Reset "last line".
                                last_pcm1_line.clear();
                                last_pcm16x0_p0_line.clear(); last_pcm16x0_p1_line.clear(); last_pcm16x0_p2_line.clear();
                                last_stc007_line.clear();
#ifdef LB_EN_DBG_OUT
                                if((log_level&Binarizer::LOG_PROCESS)!=0)
                                {
                                    qInfo()<<"[V2D] Last line copy flushed at the end of the field.";
                                }
#endif
                            }
                            else if(work_line->getPCMType()==PCMLine::TYPE_PCM1)
                            {
                                // Check for special service line for PCM-1.
                                if(pcm1_line.isServHeader()!=false)
                                {
                                    // Line contains Header.
                                    // Check if Header is at the start of the field.
                                    if(field_state==FIELD_NEW)
                                    {
                                        // Set state to "All next lines in the field are safe to process".
                                        field_state = FIELD_SAFE;
#ifdef LB_EN_DBG_OUT
                                        if(((log_level&Binarizer::LOG_PROCESS)!=0)||((log_level&Binarizer::LOG_LINE_DUMP)!=0))
                                        {
                                            qInfo()<<"[V2D] First line with PCM in new field is safe to process (Header at the top)";
                                        }
#endif
                                    }
                                    else
                                    {
#ifdef LB_EN_DBG_OUT
                                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                                        {
                                            qInfo()<<"[V2D] Header not at the top of the field, possibly Emphasis flag is set";
                                        }
#endif
                                    }
                                }
                            }
                            else if(work_line->getPCMType()==PCMLine::TYPE_STC007)
                            {
                                // Check for special service line for STC-007.
                                if(stc007_line.isServCtrlBlk()!=false)
                                {
                                    // Line contains Control Block.
                                    //qInfo()<<QString::fromStdString("[DEBUG] "+work_line->dumpContentString());

                                    // Check if Control Block is at the start of the field.
                                    if(field_state==FIELD_NEW)
                                    {
                                        // Set state to "All next lines in the field are safe to process".
                                        field_state = FIELD_SAFE;
#ifdef LB_EN_DBG_OUT
                                        if(((log_level&Binarizer::LOG_PROCESS)!=0)||((log_level&Binarizer::LOG_LINE_DUMP)!=0))
                                        {
                                            qInfo()<<"[V2D] First line with PCM in new field is safe to process (Control Block at the top)";
                                        }
#endif
                                    }
                                    else
                                    {
#ifdef LB_EN_DBG_OUT
                                        if((log_level&Binarizer::LOG_PROCESS)!=0)
                                        {
                                            qInfo()<<"[V2D] Frame composition error: Control Block in the middle of the field!";
                                        }
#endif
                                    }
                                }
                            }
                        }
                        else
                        {
                            // Current line does not contain service tag.
                            // Count total number of video lines in the frame.
                            if(even_line==false)
                            {
                                signal_quality.lines_odd++;
                            }
                            else
                            {
                                signal_quality.lines_even++;
                            }

                            bool count_has_pcm;
                            count_has_pcm = false;

                            // Determine if current line should be count as having PCM data in it.
                            if(work_line->getPCMType()==PCMLine::TYPE_PCM1)
                            {
                                // Check if line contains PCM data.
                                if((work_line->isCRCValid()!=false)||(work_line->hasBWSet()!=false))
                                {
                                    // CRC is valid or at least BLACK and WHITE levels were found.
                                    count_has_pcm = true;
                                }
                            }
                            else if(work_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                            {
                                // Check if line contains PCM data.
                                if((work_line->isCRCValid()!=false)||(work_line->hasBWSet()!=false))
                                {
                                    // CRC is valid or at least BLACK and WHITE levels were found.
                                    count_has_pcm = true;
                                }
                                // Add number of the line into the PCM line object.
                                pcm16x0_line.queue_order = line_in_field_cnt;
                            }
                            else if(work_line->getPCMType()==PCMLine::TYPE_STC007)
                            {
                                // Check if line contains PCM data.
                                if((work_line->isCRCValid()!=false)||(stc007_line.hasMarkers()!=false))
                                {
                                    // CRC is valid or at least PCM markers were found.
                                    count_has_pcm = true;
                                }
                                // Set M2 sample format to correctly process silent samples.
                                if(pcm_sample_fmt==PCM_FMT_M2)
                                {
                                    stc007_line.setM2Format(true);
                                }
                                else
                                {
                                    stc007_line.setM2Format(false);
                                }
                            }

                            // Check if line contains PCM data.
                            if(count_has_pcm!=false)
                            {
                                // Count lines with PCM.
                                if(even_line==false)
                                {
                                    signal_quality.lines_pcm_odd++;
                                }
                                else
                                {
                                    signal_quality.lines_pcm_even++;
                                }
                                pcm_lines_in_field++;
                                // Check if this line as right after the start of the field.
                                if(field_state==FIELD_NEW)
                                {
                                    // Set state to "First line in the field is not safe to process with line-copy detection".
                                    // The fact that this line (and not Control Block or Header) is first means that part of the video was cut at the top
                                    // and dropout compensator may have compensated this line from previous ones in cut region.
                                    field_state = FIELD_UNSAFE;
                                }
                            }

                            // Check if previous sub-line (PCM-16x0) was forced bad.
                            if((work_line->isCRCValid()!=false)&&(force_bad_line!=false))
                            {
                                // Force bad state for the line.
                                work_line->setForcedBad();
#ifdef LB_EN_DBG_OUT
                                if((log_level&Binarizer::LOG_PROCESS)!=0)
                                {
                                    qInfo()<<"[V2D] Previous sub-line in the same line was forced bad: forcing sub-line"<<pcm16x0_line.line_part<<"bad";
                                }
#endif
                            }

                            // Check if valid audio data was found.
                            if(work_line->isCRCValid()!=false)
                            {
                                // Line has VALID CRC.
                                // Increase counter of good CRCs in frame to disable data coordinates search.
                                good_coords_in_field++;
                                // Update line length.
                                signal_quality.line_length = (uint16_t)source_line.pixel_data.size();
                                // Check if line-copy detection is enabled.
                                if(check_line_copy!=false)
                                {
                                    // Current line contains regular audio data.
                                    if(field_state==FIELD_UNSAFE)
                                    {
                                        // This is first line of the new field
                                        // and it is unsafe to check line copy against it.
                                        // Update "good" binarizing parameters (BW levels, ref. level, data coordinates) if CRC is OK.
                                        line_converter.setGoodParameters(work_line);
                                        // Force bad state for the line.
                                        work_line->setForcedBad();
                                        force_bad_line = true;
#ifdef LB_EN_DBG_OUT
                                        if(((log_level&Binarizer::LOG_PROCESS)!=0)||((log_level&Binarizer::LOG_LINE_DUMP)!=0))
                                        {
                                            qInfo()<<"[V2D] First line with PCM in new field: unsafe to process!";
                                        }
#endif
                                    }
                                    else
                                    {
                                        // Check if current line equals to previous one.
                                        // (that could indicate "too smart" VTR with full-line dropout compensator)
                                        bool same_words;
                                        same_words = false;
                                        // Determine if current line has the same data words as the last one.
                                        if(work_line->getPCMType()==PCMLine::TYPE_PCM1)
                                        {
                                            // Single line for PCM-1.
                                            same_words = pcm1_line.hasSameWords(&last_pcm1_line);
                                        }
                                        else if(work_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                                        {
                                            // Sub-lines for PCM-16x0.
                                            if(pcm16x0_line.line_part==PCM16X0SubLine::PART_LEFT)
                                            {
                                                same_words = pcm16x0_line.hasSameWords(&last_pcm16x0_p0_line);
                                            }
                                            else if(pcm16x0_line.line_part==PCM16X0SubLine::PART_MIDDLE)
                                            {
                                                same_words = pcm16x0_line.hasSameWords(&last_pcm16x0_p1_line);
                                            }
                                            else if(pcm16x0_line.line_part==PCM16X0SubLine::PART_RIGHT)
                                            {
                                                same_words = pcm16x0_line.hasSameWords(&last_pcm16x0_p2_line);
                                            }
                                        }
                                        else if(work_line->getPCMType()==PCMLine::TYPE_STC007)
                                        {
                                            // Single line for STC-007.
                                            same_words = stc007_line.hasSameWords(&last_stc007_line);
                                        }
                                        // Check if line repeats and it is not silent.
                                        if((work_line->isAlmostSilent()==false)&&(same_words!=false))
                                        {
                                            // Lines are not silent but are identical!
                                            work_line->setForcedBad();
                                            // Count bad CRCs and duplicated lines.
                                            if(even_line==false)
                                            {
                                                signal_quality.lines_dup_odd++;
                                            }
                                            else
                                            {
                                                signal_quality.lines_dup_even++;
                                            }
#ifdef LB_EN_DBG_OUT
                                            if((log_level&Binarizer::LOG_PROCESS)!=0)
                                            {
                                                QString log_line;
                                                log_line = "[V2D] Repeated line detected! At ["+QString::number(work_line->frame_number)+":"+QString::number(work_line->line_number)+"]";
                                                qInfo()<<log_line;
                                            }
#endif
                                        }
                                    }
                                }
                                // Re-check if line is still valid.
                                if(work_line->isCRCValidIgnoreForced()!=false)
                                {
#ifdef LB_EN_DBG_OUT
                                    if((log_level&Binarizer::LOG_PROCESS)!=0)
                                    {
                                        QString log_line;
                                        log_line.sprintf("[V2D] Adding to coordinates history: [%03d|%04d]",
                                                         work_line->coords.data_start, work_line->coords.data_stop);
                                        qInfo()<<log_line;
                                    }
#endif
                                    // Add coordinates from valid data to the lists.
                                    last_coord_stats.push_back(work_line->coords);
                                    frame_coord_stats.push_back(work_line->coords);
                                    // Keep sliding window at the limited size.
                                    if(work_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                                    {
                                        // Keep all sub-lines in the history.
                                        while(last_coord_stats.size()>(COORD_HISTORY_DEPTH*PCM16X0SubLine::SUBLINES_PER_LINE))
                                        {
                                            last_coord_stats.pop_front();
                                        }
                                    }
                                    else
                                    {
                                        while(last_coord_stats.size()>COORD_HISTORY_DEPTH)
                                        {
                                            last_coord_stats.pop_front();
                                        }
                                    }

                                    // Check if coordinate damper is enabled and has collected enough data.
                                    if((coordinate_damper!=false)&&(bin_set.en_force_coords==false)&&(last_coord_stats.size()>(COORD_HISTORY_DEPTH/2)))
                                    {
                                        // Get last lines median coordinates.
                                        target_coord = medianCoordinates(&last_coord_stats);
                                        // Check if those are valid.
                                        if(target_coord.areValid()==false)
                                        {
                                            // Not enought history.
                                            // Copy pre-scanned median (or last frames median is falled back).
                                            target_coord = frame_avg;
                                        }
                                        // Re-check if now there are valid coordinates.
                                        if(target_coord.areValid()!=false)
                                        {
                                            // Calculate data coordinates delta between current data coordinates and stats.
                                            coord_delta = work_line->coords;
                                            coord_delta = coord_delta-target_coord;
#ifdef LB_EN_DBG_OUT
                                            if((log_level&Binarizer::LOG_PROCESS)!=0)
                                            {
                                                QString log_line;
                                                log_line.sprintf("[V2D] Coordinate delta: [%03d|%04d], target: [%03d|%04d]",
                                                                 coord_delta.data_start, coord_delta.data_stop,
                                                                 target_coord.data_start, target_coord.data_stop);
                                                qInfo()<<log_line;
                                            }
#endif
                                            // Check if coordinate delta is above threshold.
                                            if(coord_delta.hasDeltaWarning(work_line->getPPB()*3)!=false)
                                            {
                                                // Data coordinates drifted too far.
                                                // Make this line invalid.
                                                work_line->setForcedBad();
                                                force_bad_line = true;
#ifdef LB_EN_DBG_OUT
                                                if((log_level&Binarizer::LOG_PROCESS)!=0)
                                                {
                                                    QString log_line;
                                                    log_line.sprintf("[V2D] Coordinate delta warning! Line [%03u:%03u] data at [%03d|%04d] (d[%03d|%04d]), target at [%03d|%04d]",
                                                                     work_line->frame_number, work_line->line_number,
                                                                     work_line->coords.data_start, work_line->coords.data_stop,
                                                                     coord_delta.data_start, coord_delta.data_stop,
                                                                     target_coord.data_start, target_coord.data_stop);
                                                    qInfo()<<log_line;
                                                }
#endif
                                            }
                                        }
                                    }
                                }
                                // Re-check if line is still valid.
                                if(work_line->isCRCValid()!=false)
                                {
                                    // Update "good" binarizing parameters (BW levels, ref. level, data coordinates) if CRC is OK.
                                    line_converter.setGoodParameters(work_line);
                                }
                                else
                                {
                                    // Count bad CRCs.
                                    if(even_line==false)
                                    {
                                        signal_quality.lines_bad_odd++;
                                    }
                                    else
                                    {
                                        signal_quality.lines_bad_even++;
                                    }
                                }
                                // Update last line for future comparisons.
                                if(work_line->getPCMType()==PCMLine::TYPE_PCM1)
                                {
                                    // Update stored last line.
                                    last_pcm1_line = pcm1_line;
                                    // Begining of the field has passed.
                                    field_state = FIELD_INIT;
                                }
                                else if(work_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                                {
                                    // Update stored last line.
                                    if(pcm16x0_line.line_part==PCM16X0SubLine::PART_LEFT)
                                    {
                                        // Last sub-line for the left part.
                                        last_pcm16x0_p0_line = pcm16x0_line;
                                    }
                                    else if(pcm16x0_line.line_part==PCM16X0SubLine::PART_MIDDLE)
                                    {
                                        // Last sub-line for the middle part.
                                        last_pcm16x0_p1_line = pcm16x0_line;
                                    }
                                    else if(pcm16x0_line.line_part==PCM16X0SubLine::PART_RIGHT)
                                    {
                                        // Last sub-line for the right part.
                                        last_pcm16x0_p2_line = pcm16x0_line;
                                        // Begining of the field has passed.
                                        field_state = FIELD_INIT;
                                    }
                                }
                                else if(work_line->getPCMType()==PCMLine::TYPE_STC007)
                                {
                                    // Update stored last line.
                                    last_stc007_line = stc007_line;
                                    // Begining of the field has passed.
                                    field_state = FIELD_INIT;
                                }
                            }
                            // Check if at least PCM was found with bad CRC.
                            else
                            {
                                // CRC was invalid.
                                bool count_bad_crc;
                                count_bad_crc = false;
                                if(signal_quality.line_length==0)
                                {
                                    // Update line length if not set by valid lines yet.
                                    signal_quality.line_length = (uint16_t)source_line.pixel_data.size();
                                }
                                // Update last line data to compare next time.
                                if(work_line->getPCMType()==PCMLine::TYPE_PCM1)
                                {
                                    if(work_line->hasBWSet()!=false)
                                    {
                                        count_bad_crc = true;
                                        // Update stored last line.
                                        last_pcm1_line = pcm1_line;
                                    }
                                }
                                else if(work_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                                {
                                    if(work_line->hasBWSet()!=false)
                                    {
                                        count_bad_crc = true;
                                        // Update stored last line.
                                        if(pcm16x0_line.line_part==PCM16X0SubLine::PART_LEFT)
                                        {
                                            // Last sub-line for the left part.
                                            last_pcm16x0_p0_line = pcm16x0_line;
                                        }
                                        else if(pcm16x0_line.line_part==PCM16X0SubLine::PART_MIDDLE)
                                        {
                                            // Last sub-line for the middle part.
                                            last_pcm16x0_p1_line = pcm16x0_line;

                                        }
                                        else if(pcm16x0_line.line_part==PCM16X0SubLine::PART_RIGHT)
                                        {
                                            // Last sub-line for the right part.
                                            last_pcm16x0_p2_line = pcm16x0_line;
                                        }
                                    }
                                }
                                else if(work_line->getPCMType()==PCMLine::TYPE_STC007)
                                {
                                    if(stc007_line.hasMarkers()!=false)
                                    {
                                        count_bad_crc = true;
                                        // Update stored last line.
                                        last_stc007_line = stc007_line;
                                    }
                                }
                                // Check if any PCM data was registered.
                                if(count_bad_crc!=false)
                                {
                                    // CRC was bad, count it.
                                    if(even_line==false)
                                    {
                                        signal_quality.lines_bad_odd++;
                                    }
                                    else
                                    {
                                        signal_quality.lines_bad_even++;
                                    }

                                    // Some data probably is there but CRC is invalid.
                                    CoordinatePair preset_coords;
                                    if(bin_set.en_force_coords==false)
                                    {
                                        // First, preset for median if last valid coordinates.
                                        preset_coords = medianCoordinates(&last_coord_stats);
                                        // Check if those are valid.
                                        if(preset_coords.areValid()==false)
                                        {
                                            // Set to last frame median coordinates.
                                            preset_coords = frame_avg;
                                        }
                                    }
#ifdef LB_EN_DBG_OUT
                                    if(((log_level&Binarizer::LOG_PROCESS)!=0)||((log_level&Binarizer::LOG_LINE_DUMP)!=0))
                                    {
                                        if(field_state==FIELD_SAFE)
                                        {
                                            qInfo()<<"[V2D] First line with PCM in new field: safe to process, but bad CRC";
                                        }
                                        else if(field_state==FIELD_UNSAFE)
                                        {
                                            qInfo()<<"[V2D] First line with PCM in new field: unsafe to process and already bad CRC";
                                        }
                                    }
#endif
                                    if(work_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                                    {
                                        if(pcm16x0_line.line_part==PCM16X0SubLine::PART_RIGHT)
                                        {
                                            // Begining of the field has passed.
                                            field_state = FIELD_INIT;
#ifdef LB_EN_DBG_OUT
                                            if((log_level&Binarizer::LOG_PROCESS)!=0)
                                            {
                                                QString log_line;
                                                log_line.sprintf("[V2D] No valid data found, last sub-line passed, set default coordinates to [%03d|%04d]",
                                                                 preset_coords.data_start, preset_coords.data_stop);
                                                qInfo()<<log_line;
                                            }
#endif
                                            // Try to get a result next time with averaged coordinates.
                                            line_converter.setDataCoordinates(preset_coords);
                                            // Reset preset BW levels.
                                            line_converter.setBWLevels();
                                        }
                                        else
                                        {
                                            // Transfer reference level to next subline in the same line.
                                            line_converter.setReferenceLevel(pcm16x0_line.ref_level);
                                            line_converter.setBWLevels(pcm16x0_line.black_level, pcm16x0_line.white_level);
                                            //if((work_line->isCRCValidIgnoreForced()!=false)||(source_line.scan_done!=false))
                                            if(source_line.scan_done!=false)
                                            {
#ifdef LB_EN_DBG_OUT
                                                if((log_level&Binarizer::LOG_PROCESS)!=0)
                                                {
                                                    QString log_line;
                                                    log_line.sprintf("[V2D] No valid data found, currently processing sub-line from one line, set default coordinates as to previous part at [%03d|%04d]",
                                                                     work_line->coords.data_start, work_line->coords.data_stop);
                                                    qInfo()<<log_line;
                                                }
#endif
                                                // Try to get a result for next sub-line time with already valid coordinates.
                                                line_converter.setDataCoordinates(work_line->coords);
                                            }
                                            else
                                            {
#ifdef LB_EN_DBG_OUT
                                                if((log_level&Binarizer::LOG_PROCESS)!=0)
                                                {
                                                    QString log_line;
                                                    log_line.sprintf("[V2D] No valid data found, currently processing sub-line from one line, set default coordinates to [%03d|%04d]",
                                                                     preset_coords.data_start, preset_coords.data_stop);
                                                    qInfo()<<log_line;
                                                }
#endif
                                                // Try to get a result next time with averaged coordinates.
                                                line_converter.setDataCoordinates(preset_coords);
                                            }
                                        }
                                    }
                                    else
                                    {
                                        // Begining of the field has passed.
                                        field_state = FIELD_INIT;
                                        // Try to get a result next time with averaged coordinates.
                                        line_converter.setDataCoordinates(preset_coords);
                                        // Reset preset BW levels.
                                        line_converter.setBWLevels();
                                    }
                                }
                                else
                                {
                                    // Reset preset BW levels.
                                    line_converter.setBWLevels();
                                }
                            }
                            line_in_field_cnt++;
                        }   // service tag presence check
                        // Check if frame ended.
                        if(work_line->isServEndFrame()!=false)
                        {
                            if(pcm_type==TYPE_PCM1)
                            {
                                // Limit number of lines with PCM to standard.
                                signal_quality.lines_odd = signal_quality.lines_even = PCM1DataStitcher::LINES_PF;
                            }
                            else if(pcm_type==TYPE_PCM16X0)
                            {
                                // Limit number of lines with PCM to standard.
                                signal_quality.lines_odd = signal_quality.lines_even = PCM16X0DataStitcher::LINES_PF;
                                // Account for sub-lines in one line in PCM-16x0.
                                signal_quality.lines_pcm_odd = signal_quality.lines_pcm_odd/PCM16X0SubLine::SUBLINES_PER_LINE;
                                signal_quality.lines_pcm_even = signal_quality.lines_pcm_even/PCM16X0SubLine::SUBLINES_PER_LINE;
                                signal_quality.lines_bad_odd = signal_quality.lines_bad_odd/PCM16X0SubLine::SUBLINES_PER_LINE;
                                signal_quality.lines_bad_even = signal_quality.lines_bad_even/PCM16X0SubLine::SUBLINES_PER_LINE;
                            }
                            // Put counters in limits for GUI.
                            if(signal_quality.lines_pcm_odd>signal_quality.lines_odd)
                            {
                                signal_quality.lines_pcm_odd = signal_quality.lines_odd;
                            }
                            if(signal_quality.lines_pcm_even>signal_quality.lines_even)
                            {
                                signal_quality.lines_pcm_even = signal_quality.lines_even;
                            }
                            if(signal_quality.lines_bad_odd>signal_quality.lines_odd)
                            {
                                signal_quality.lines_bad_odd = signal_quality.lines_odd;
                            }
                            if(signal_quality.lines_bad_even>signal_quality.lines_even)
                            {
                                signal_quality.lines_bad_even = signal_quality.lines_even;
                            }
                            // Save processed frame number.
                            signal_quality.frame_id = work_line->frame_number;

                            // Get median coordinates for the frame.
                            // Overwrite pre-scan median, it will be recalculated at the start of the next frame.
                            frame_avg = medianCoordinates(&frame_coord_stats);
                            if(frame_avg.areValid()!=false)
                            {
                                // Add filtered coordinates from the current frame to the multi-frame history.
                                long_coord_stats.push_back(frame_avg);
                                while(long_coord_stats.size()>COORD_LONG_HISTORY)
                                {
                                    // Remove oldest entry to keep history size in check.
                                    long_coord_stats.pop_front();
                                }
                            }
                            else
                            {
                                // No valid coordinates for current frame, take median from last frames.
                                frame_avg = medianCoordinates(&long_coord_stats);
                                signal_quality.data_coord.not_sure = true;
                            }
                            // Reset frame coordinates stats.
                            frame_coord_stats.clear();
                            // Save target data coordinates.
                            signal_quality.data_coord = frame_avg;
                            // Measure time spent on the frame.
                            time_spent = time_per_frame.nsecsElapsed()/1000;
                            signal_quality.time_odd = time_spent;
                            signal_quality.time_even = 0;       // TODO: threads per field
                            emit loopTime(signal_quality.totalProcessTime());
                            // Report about new binarized frame.
                            emit guiUpdFrameBin(signal_quality);
                            //qDebug()<<"[DBG] Time per frame:"<<signal_quality.totalProcessTime();
                            // Reset tracking.
                            signal_quality.clear();
                        }

                        // Put the resulting PCM line with into PCM lines queue.
                        outNewLine(work_line);
                        //qDebug()<<"S1"<<pcm16x0_line.line_part<<time_per_line.nsecsElapsed()/1000;
#ifdef LB_EN_DBG_OUT
                        if((log_level&Binarizer::LOG_LINE_DUMP)!=0)
                        {
                            if(line_dump_help_done==false)
                            {
                                uint8_t line_count;
                                std::string help_line;
                                line_dump_help_done = true;
                                // Restart internal help line counter.
                                work_line->helpDumpRestart();
                                if(work_line->getPCMType()==PCMLine::TYPE_PCM1)
                                {
                                    // Set limits for the dump cycle.
                                    line_count = PCM1_LINE_HELP_SIZE;
                                    while(line_count!=0)
                                    {
                                        // Get next help line.
                                        help_line = pcm1_line.helpDumpNext();
                                        if(help_line.size()>0)
                                        {
                                            // Dump help.
                                            qInfo()<<QString::fromStdString("[V2D] "+help_line);
                                        }
                                        else
                                        {
                                            // No more help left.
                                            break;
                                        }
                                        line_count--;
                                    };
                                }
                                else if(work_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                                {
                                    // Set limits for the dump cycle.
                                    line_count = PCM16X0_LINE_HELP_SIZE;
                                    while(line_count!=0)
                                    {
                                        // Get next help line.
                                        help_line = pcm16x0_line.helpDumpNext();
                                        if(help_line.size()>0)
                                        {
                                            // Dump help.
                                            qInfo()<<QString::fromStdString("[V2D] "+help_line);
                                        }
                                        else
                                        {
                                            // No more help left.
                                            break;
                                        }
                                        line_count--;
                                    };
                                }
                                else if(work_line->getPCMType()==PCMLine::TYPE_STC007)
                                {
                                    // Set limits for the dump cycle.
                                    line_count = STC_LINE_HELP_SIZE;
                                    while(line_count!=0)
                                    {
                                        // Get next help line.
                                        help_line = stc007_line.helpDumpNext();
                                        if(help_line.size()>0)
                                        {
                                            // Dump help.
                                            qInfo()<<QString::fromStdString("[V2D] "+help_line);
                                        }
                                        else
                                        {
                                            // No more help left.
                                            break;
                                        }
                                        line_count--;
                                    };
                                }
                            }
                            // Dump raw PCM line into console.
                            qInfo()<<QString::fromStdString("[V2D] "+work_line->dumpContentString());
                        }
                        //qDebug()<<time_per_line.nsecsElapsed()/1000;
#endif
                    }
                    // Sub-lines sub-cycle ended.
                }
                // Internal buffer is empty.
            }
            else
            {
                // Not enough data in the input queue.
                // Unlock shared access.
                mtx_vid->unlock();
                // Wait (~a frame) for input to fill up.
                QThread::msleep(25);
            }
        }
    }
    qInfo()<<"[V2D] Loop stop.";
    emit finished();
}

//------------------------ Set "stop thread" flag.
void VideoToDigital::stop()
{
#ifdef LB_EN_DBG_OUT
    qInfo()<<"[V2D] Received termination request";
#endif
    finish_work = true;
}
