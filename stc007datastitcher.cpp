#include "stc007datastitcher.h"

STC007DataStitcher::STC007DataStitcher(QObject *parent) : QObject(parent)
{
    // Reset pointers to input/output queues.
    in_lines = NULL;
    out_samples = NULL;
    mtx_lines = NULL;
    mtx_samples = NULL;

    // Set internal frame and field buffer sizes.
    trim_buf.resize(BUF_SIZE_TRIM);
    frame1_even.resize(BUF_SIZE_FIELD);
    frame1_odd.resize(BUF_SIZE_FIELD);
    frame2_even.resize(BUF_SIZE_FIELD);
    frame2_odd.resize(BUF_SIZE_FIELD);

    file_name.clear();

    preset_video_mode = FrameAsmDescriptor::VID_UNKNOWN;
    preset_field_order = FrameAsmDescriptor::ORDER_UNK;
    preset_audio_res = SAMPLE_RES_UNKNOWN;
    preset_sample_rate = PCMSamplePair::SAMPLE_RATE_AUTO;

    log_level = 0;
    trim_fill = 0;
    file_start = file_end = false;
    enable_P_code = false;
    enable_Q_code = false;
    enable_CWD = true;
    mode_m2 = false;
    finish_work = false;

    // Reset internal state.
    resetState();
    // Preset default fine parameters.
    setDefaultFineSettings();
}

//------------------------ Set pointers to shared input data.
void STC007DataStitcher::setInputPointers(std::deque<STC007Line> *in_pcmline, QMutex *mtx_pcmline)
{
    if((in_pcmline==NULL)||(mtx_pcmline==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Empty input pointer provided, unable to apply!";
    }
    else
    {
        in_lines = in_pcmline;
        mtx_lines = mtx_pcmline;
    }
}

//------------------------ Set pointers to shared output data.
void STC007DataStitcher::setOutputPointers(std::deque<PCMSamplePair> *out_pcmsamples, QMutex *mtx_pcmsamples)
{
    if((out_pcmsamples==NULL)||(mtx_pcmsamples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Empty output pointer provided, unable to apply!";
    }
    else
    {
        out_samples = out_pcmsamples;
        mtx_samples = mtx_pcmsamples;
    }
}

//------------------------ Reset all stats from the last file, start from scratch.
void STC007DataStitcher::resetState()
{
    // Clear good field order history.
    clearFieldOrderStats();
    // Clear detected audio resolution history.
    clearResolutionStats();
    // Clear queue.
    conv_queue.clear();
    // Set default assembling parameters.
    last_pad_counter = 0xFF;
    broken_countdown = 0;
    frasm_f0.clear();
    frasm_f1.clearMisc();
    frasm_f2.clearMisc();
#ifdef DI_EN_DBG_OUT
    //if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-007] Internal state is reset";
    }
#endif
}

//------------------------ Wait until two full frames are in the input queue.
bool STC007DataStitcher::waitForTwoFrames()
{
    bool frame1_lock, frame2_lock, two_frame_lock;
    std::deque<STC007Line>::iterator buf_scaner;

    frasm_f1.clear();
    frasm_f2.clear();
    frame1_lock = frame2_lock = two_frame_lock = false;

#ifdef DI_LOG_BUF_WAIT_VERBOSE
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-007] Searching for two full frames in the input buffer within"<<in_lines->size()<<"lines...";
    }
#endif

    // Pick start of the queue.
    buf_scaner = in_lines->begin();

    // Fill up buffer until there is nothing in the input.
    while(buf_scaner!=in_lines->end())
    {
#ifdef DI_LOG_BUF_WAIT_VERBOSE
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[L2B-007] Current line:"<<(*buf_scaner).frame_number<<"|"<<(*buf_scaner).line_number;
        }
#endif
        // Still at the first frame in the buffer.
        if(frame1_lock==false)
        {
            // End of the first frame is not detected yet.
            // Check if there is service tag for the end of the frame.
            if((*buf_scaner).isServEndFrame()!=false)
            {
                // Got to the end of the first frame: one full frame is in the buffer.
                frame1_lock = true;
                // Set frame number for the second frame in the buffer.
                frasm_f1.frame_number = (*buf_scaner).frame_number;
#ifdef DI_LOG_BUF_WAIT_VERBOSE
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-007] Detected end of the Frame A";
                }
#endif
            }
        }
        else if(frame2_lock==false)
        {
            // Second frame is not locked yet.
            // Check if there is a service tag for the end of the frame.
            if((*buf_scaner).isServEndFrame()!=false)
            {
                // Mark second frame as detected.
                frame2_lock = true;
                // Set frame number for the second frame in the buffer.
                frasm_f2.frame_number = (*buf_scaner).frame_number;
#ifdef DI_LOG_BUF_WAIT_VERBOSE
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-007] Detected end of the Frame B";
                }
#endif
            }
        }

        if((frame1_lock!=false)&&(frame2_lock!=false))
        {
            // Two full frames found in the buffer.
            two_frame_lock = true;
#ifdef DI_LOG_BUF_WAIT_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] Two finished frames in the buffer, exiting search...";
            }
#endif
            break;
        }
        // Check for the End Of File.
        if((*buf_scaner).isServEndFile()!=false)
        {
#ifdef DI_LOG_BUF_WAIT_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] EOF detected";
            }
#endif
            //break;
        }
        // Go to the next line in the input.
        buf_scaner++;
    };

    return two_frame_lock;
}

//------------------------ Fill internal buffer up to two frames from input queue.
void STC007DataStitcher::fillUntilTwoFrames()
{
    std::deque<STC007Line>::iterator buf_scaner;
    uint8_t frames_cnt;

    // Reset internal buffer fill counter.
    trim_fill = 0;
    frames_cnt = 0;

#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-007] Copying up to two full frames from the input queue...";
    }
#endif
    buf_scaner = in_lines->begin();

    // Fill up buffer until there is nothing in the input.
    while(buf_scaner!=in_lines->end())
    {
        // Check if there is a service tag for the end of the frame.
        if((*buf_scaner).isServEndFrame()!=false)
        {
            // Increase number of finished frames.
            frames_cnt++;
#ifdef DI_LOG_BUF_FILL_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-007] Frame end at line %u-%u, fill index: %u", (*buf_scaner).frame_number, (*buf_scaner).line_number, trim_fill);
                qInfo()<<log_line;
            }
#endif
            if(frames_cnt>=2)
            {
                // Two frames are enough.
                break;
            }
        }
        // Check for internal buffer overflow.
        else if(trim_fill<BUF_SIZE_TRIM)
        {
            // Copy data into internal buffer.
            trim_buf[trim_fill] = (*buf_scaner);
#ifdef DI_LOG_BUF_FILL_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-007] Added line %u-%u (%u) into buffer at index: %u",
                                 (*buf_scaner).frame_number,
                                 (*buf_scaner).line_number,
                                 (*buf_scaner).isServiceLine(),
                                 trim_fill);
                qInfo()<<log_line;
            }
#endif
            // Increase number of filled lines.
            trim_fill++;
        }
        else
        {
            qWarning()<<DBG_ANCHOR<<"[L2B-007] Line buffer index out of bound! Logic error! Line skipped!";
            qWarning()<<DBG_ANCHOR<<"[L2B-007] Max lines:"<<BUF_SIZE_TRIM;
        }
        // Go to the next line in the input.
        buf_scaner++;
    }
}

//------------------------ Detect frame trimming (how many lines to skip from top and bottom of each field).
void STC007DataStitcher::findFramesTrim()
{
    uint16_t line_ind, f1o_good, f1e_good, f2o_good, f2e_good;
    bool f1e_top, f1e_bottom, f1o_top, f1o_bottom;
    bool f2e_top, f2e_bottom, f2o_top, f2o_bottom;
    bool f1o_skip_bad, f1e_skip_bad, f2o_skip_bad, f2e_skip_bad;

    f1e_top = f1e_bottom = f1o_top = f1o_bottom = false;
    f2e_top = f2e_bottom = f2o_top = f2o_bottom = false;
    f1o_skip_bad = f1e_skip_bad = f2o_skip_bad = f2e_skip_bad = false;
    f1o_good = f1e_good = f2o_good = f2e_good = 0;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-007] -------------------- Trim search starting...";
    }
#endif

    // Check if frame A data was moved from frame B.
    if(frasm_f1.trim_ok!=false)
    {
        // Don't need to adjust trimming.
        f1e_top = f1o_top = f1e_bottom = f1o_bottom = true;
#ifdef DI_LOG_TRIM_VERBOSE
        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
        {
            qInfo()<<"[L2B-007] Frame A trim does not require adjustments.";
        }
#endif
    }
    else
    {
        // Preset default trim.
        frasm_f1.even_top_data = frasm_f1.even_bottom_data = frasm_f1.odd_top_data = frasm_f1.odd_bottom_data = 0;
    }
    // Check if both frames are already processed.
    if(frasm_f2.trim_ok!=false)
    {
        // Don't need to adjust trimming.
        f2e_top = f2o_top = f2e_bottom = f2o_bottom = true;
#ifdef DI_LOG_TRIM_VERBOSE
        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
        {
            qInfo()<<"[L2B-007] Frame B trim does not require adjustments.";
        }
#endif
    }
    else
    {
        // Preset default trim.
        frasm_f2.even_top_data = frasm_f2.even_bottom_data = frasm_f2.odd_top_data = frasm_f2.odd_bottom_data = 0;
    }

    // Pre-scan buffer to count up number of good lines.
    line_ind = 0;
    while(line_ind<trim_fill)
    {
        if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
        {
            // Frame A number is ok.
            if(trim_buf[line_ind].isServiceLine()==false)
            {
                // Current line is not a service one.
                if(trim_buf[line_ind].isCRCValid()!=false)
                {
                    // Binarization gave good result.
                    if((trim_buf[line_ind].line_number%2)==0)
                    {
                        // Line from even field.
                        f1e_good++;
                        if(f1e_good>MIN_GOOD_LINES_PF)
                        {
                            f1e_skip_bad = true;
                        }
                    }
                    else
                    {
                        // Line from odd field.
                        f1o_good++;
                        if(f1o_good>MIN_GOOD_LINES_PF)
                        {
                            f1o_skip_bad = true;
                        }
                    }
                }
            }
            else if(trim_buf[line_ind].isServNewFile()!=false)
            {
                // Line contains Service line with "New file" tag.
                file_start = true;
                file_name = trim_buf[line_ind].file_path;
            }
            else if(trim_buf[line_ind].isServEndFile()!=false)
            {
                // Line contains Service line with "File end" tag.
                file_end = true;
            }
            else if(trim_buf[line_ind].isServCtrlBlk()!=false)
            {
                // Line contains Service line with STC-007 Control Block data.
                // Check if this line is at the top of the field
                // and not somewhere in the middle due to broken frame sync.
                if((f1e_good==0)&&(f1o_good==0))
                {
                    // Copy addresss data into frame descriptor.
                    frasm_f1.ctrl_index = trim_buf[line_ind].getCtrlIndex();
                    frasm_f1.ctrl_hour = trim_buf[line_ind].getCtrlHour();
                    frasm_f1.ctrl_minute = trim_buf[line_ind].getCtrlMinute();
                    frasm_f1.ctrl_second = trim_buf[line_ind].getCtrlSecond();
                    frasm_f1.ctrl_field = trim_buf[line_ind].getCtrlFieldCode();
                }
            }
        }
        else if(trim_buf[line_ind].frame_number==frasm_f2.frame_number)
        {
            // Frame B number is ok.
            if(trim_buf[line_ind].isServiceLine()==false)
            {
                // Current line is not a service one.
                if(trim_buf[line_ind].isCRCValid()!=false)
                {
                    // Binarization gave good result.
                    if((trim_buf[line_ind].line_number%2)==0)
                    {
                        // Line from even field.
                        f2e_good++;
                        if(f2e_good>MIN_GOOD_LINES_PF)
                        {
                            f2e_skip_bad = true;
                        }
                    }
                    else
                    {
                        // Line from odd field.
                        f2o_good++;
                        if(f2o_good>MIN_GOOD_LINES_PF)
                        {
                            f2o_skip_bad = true;
                        }
                    }
                }
            }
            else if(trim_buf[line_ind].isServEndFile()!=false)
            {
                // Line contains Service line with "File end" tag.
                file_end = true;
            }
            else if(trim_buf[line_ind].isServCtrlBlk()!=false)
            {
                // Line contains Service line with STC-007 Control Block data.
                // Check if this line is at the top of the field
                // and not somewhere in the middle due to broken frame sync.
                if((f2e_good==0)&&(f2o_good==0))
                {
                    // Copy addresss data into frame descriptor.
                    frasm_f2.ctrl_index = trim_buf[line_ind].getCtrlIndex();
                    frasm_f2.ctrl_hour = trim_buf[line_ind].getCtrlHour();
                    frasm_f2.ctrl_minute = trim_buf[line_ind].getCtrlMinute();
                    frasm_f2.ctrl_second = trim_buf[line_ind].getCtrlSecond();
                    frasm_f2.ctrl_field = trim_buf[line_ind].getCtrlFieldCode();
                }
            }
        }
        // Go to next line.
        line_ind++;
    }

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        if(file_start!=false)
        {
            qInfo()<<"[L2B-007] New file tag detected in the frame";
        }
        if(file_end!=false)
        {
            qInfo()<<"[L2B-007] File end tag detected in the frame";
        }
        if(f1o_skip_bad!=false)
        {
            qInfo()<<"[L2B-007]"<<f1o_good<<"lines with good CRC in Frame A odd field, aggresive trimming enabled for Frame A odd field";
        }
        if(f1e_skip_bad!=false)
        {
            qInfo()<<"[L2B-007]"<<f1e_good<<"lines with good CRC in Frame A even field, aggresive trimming enabled for Frame A even field";
        }
        if(f2o_skip_bad!=false)
        {
            qInfo()<<"[L2B-007]"<<f2o_good<<"lines with good CRC in Frame B odd field, aggresive trimming enabled for Frame B odd field";
        }
        if(f2e_skip_bad!=false)
        {
            qInfo()<<"[L2B-007]"<<f2e_good<<"lines with good CRC in Frame B even field, aggresive trimming enabled for Frame B even field";
        }
    }
#endif
    if(file_end!=false)
    {
        qInfo()<<"[L2B-007] File end tag detected in the frame";
    }

    // Cycle through the whole buffer.
    // (assuming that buffer starts on the first line of the frame)
    line_ind = 0;
    while(line_ind<trim_fill)
    {
        if((frasm_f1.trim_ok!=false)&&(frasm_f2.trim_ok!=false))
        {
#ifdef DI_EN_DBG_OUT
            if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                qInfo()<<"[L2B-007] Both frames are already trimmed, aborting search.";
            }
#endif
            break;
        }

        // Check for service lines in the input.
        if((trim_buf[line_ind].isServiceLine()!=false)&&(trim_buf[line_ind].isServFiller()==false))
        {
            // Go to next line.
            line_ind++;
#ifdef DI_LOG_TRIM_VERBOSE
            if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                qInfo()<<"[L2B-007] Skipping service line at"<<trim_buf[line_ind].frame_number<<"/"<<trim_buf[line_ind].line_number;
            }
#endif
            // Skip service lines.
            continue;
        }

#ifdef DI_LOG_TRIM_VERBOSE
        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
        {
            QString log_line;
            log_line.sprintf("[L2B-007] Check: at %04u, line: %03u/%03u, PCM: ",
                             line_ind,
                             trim_buf[line_ind].frame_number,
                             trim_buf[line_ind].line_number);
            if(trim_buf[line_ind].isCRCValidIgnoreForced()==false)
            {
                log_line += "no ";
            }
            else
            {
                log_line += "yes";
            }
            qInfo()<<log_line;
        }
#endif

        // Check frame numbers (that are set in [waitForTwoFrames()]).
        if((trim_buf[line_ind].frame_number==frasm_f1.frame_number)&&(frasm_f1.trim_ok==false))
        {
            // Frame A number is ok.
            // Check field.
            if((trim_buf[line_ind].line_number%2)==0)
            {
                // Line from even field.
                if(((f1e_skip_bad==false)&&(trim_buf[line_ind].hasMarkers()!=false))||
                   ((f1e_skip_bad==false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false))||
                   ((f1e_skip_bad!=false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false)))
                {
                    // PCM detected in the line.
                    if(f1e_top==false)
                    {
                        // Set number of the line that PCM is starting.
                        frasm_f1.even_top_data = trim_buf[line_ind].line_number;
                        // Detect only first encounter in the buffer.
                        f1e_top = true;
#ifdef DI_EN_DBG_OUT
                        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
                        {
                            qInfo()<<"[L2B-007] Frame A even field new top trim:"<<frasm_f1.even_top_data;
                        }
#endif
                    }
                    // Update last line with PCM from the bottom.
                    frasm_f1.even_bottom_data = trim_buf[line_ind].line_number;
                    f1e_bottom = true;
                }
            }
            else
            {
                // Line from odd field.
                if(((f1o_skip_bad==false)&&(trim_buf[line_ind].hasMarkers()!=false))||
                   ((f1o_skip_bad==false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false))||
                   ((f1o_skip_bad!=false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false)))
                {
                    // PCM detected in the line.
                    if(f1o_top==false)
                    {
                        // Set number of the line that PCM is starting.
                        frasm_f1.odd_top_data = trim_buf[line_ind].line_number;
                        // Detect only first encounter in the buffer.
                        f1o_top = true;
#ifdef DI_EN_DBG_OUT
                        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
                        {
                            qInfo()<<"[L2B-007] Frame A odd field new top trim:"<<frasm_f1.odd_top_data;
                        }
#endif
                    }
                    // Update last line with PCM from the bottom.
                    frasm_f1.odd_bottom_data = trim_buf[line_ind].line_number;
                    f1o_bottom = true;
                }
            }
        }
        else if((trim_buf[line_ind].frame_number==frasm_f2.frame_number)&&(frasm_f2.trim_ok==false))
        {
            // Frame B number is ok.
            // Check field.
            if((trim_buf[line_ind].line_number%2)==0)
            {
                // Line from even field.
                if(((f2e_skip_bad==false)&&(trim_buf[line_ind].hasMarkers()!=false))||
                   ((f2e_skip_bad==false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false))||
                   ((f2e_skip_bad!=false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false)))
                {
                    // PCM detected in the line.
                    if(f2e_top==false)
                    {
                        // Set number of the line that PCM is starting.
                        frasm_f2.even_top_data = trim_buf[line_ind].line_number;
                        // Detect only first encounter in the buffer.
                        f2e_top = true;
#ifdef DI_EN_DBG_OUT
                        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
                        {
                            qInfo()<<"[L2B-007] Frame B even field new top trim:"<<frasm_f2.even_top_data;
                        }
#endif
                    }
                    // Update last line with PCM from the bottom.
                    frasm_f2.even_bottom_data = trim_buf[line_ind].line_number;
                    f2e_bottom = true;
                }
            }
            else
            {
                // Line from odd field.
                if(((f2o_skip_bad==false)&&(trim_buf[line_ind].hasMarkers()!=false))||
                   ((f2o_skip_bad==false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false))||
                   ((f2o_skip_bad!=false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false)))
                {
                    // PCM detected in the line.
                    if(f2o_top==false)
                    {
                        // Set number of the line that PCM is starting.
                        frasm_f2.odd_top_data = trim_buf[line_ind].line_number;
                        // Detect only first encounter in the buffer.
                        f2o_top = true;
#ifdef DI_EN_DBG_OUT
                        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
                        {
                            qInfo()<<"[L2B-007] Frame B odd field new top trim:"<<frasm_f2.odd_top_data;
                        }
#endif
                    }
                    // Update last line with PCM from the bottom.
                    frasm_f2.odd_bottom_data = trim_buf[line_ind].line_number;
                    f2o_bottom = true;
                }
            }
        }

        // Go to next line.
        line_ind++;
    }
    // Check if trim is done for each frame.
    if((f1e_top!=false)&&(f1o_top!=false)&&(f1e_bottom!=false)&&(f1o_bottom!=false))
    {
        if(frasm_f1.trim_ok==false)
        {
            frasm_f1.trim_ok = true;
#ifdef DI_LOG_TRIM_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] Frame A trim is complete";
            }
#endif
        }
    }
    if((f2e_top!=false)&&(f2o_top!=false)&&(f2e_bottom!=false)&&(f2o_bottom!=false))
    {
        if(frasm_f2.trim_ok==false)
        {
            frasm_f2.trim_ok = true;
#ifdef DI_LOG_TRIM_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] Frame B trim is complete";
            }
#endif
        }
    }

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        QString log_line;
        log_line = "[L2B-007] Frame A ("+QString::number(frasm_f1.frame_number)+") trim (OT, OB, ET, EB): ";
        if(f1o_top==false)
        {
            log_line += "UNK, ";
        }
        else
        {
            log_line += QString::number(frasm_f1.odd_top_data)+", ";
        }
        if(f1o_bottom==false)
        {
            log_line += "UNK, ";
        }
        else
        {
            log_line += QString::number(frasm_f1.odd_bottom_data)+", ";
        }
        if(f1e_top==false)
        {
            log_line += "UNK, ";
        }
        else
        {
            log_line += QString::number(frasm_f1.even_top_data)+", ";
        }
        if(f1e_bottom==false)
        {
            log_line += "UNK";
        }
        else
        {
            log_line += QString::number(frasm_f1.even_bottom_data);
        }
        qInfo()<<log_line;
        log_line.clear();
        log_line = "[L2B-007] Frame B ("+QString::number(frasm_f2.frame_number)+") trim (OT, OB, ET, EB): ";
        if(f2o_top==false)
        {
            log_line += "UNK, ";
        }
        else
        {
            log_line += QString::number(frasm_f2.odd_top_data)+", ";
        }
        if(f2o_bottom==false)
        {
            log_line += "UNK, ";
        }
        else
        {
            log_line += QString::number(frasm_f2.odd_bottom_data)+", ";
        }
        if(f2e_top==false)
        {
            log_line += "UNK, ";
        }
        else
        {
            log_line += QString::number(frasm_f2.even_top_data)+", ";
        }
        if(f2e_bottom==false)
        {
            log_line += "UNK";
        }
        else
        {
            log_line += QString::number(frasm_f2.even_bottom_data);
        }
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Split 2 frames (current [Frame A] and the next one [Frame B]) into 4 internal buffers per field.
void STC007DataStitcher::splitFramesToFields()
{
    uint16_t line_ind, line_num;
    uint32_t ref_lvl_odd, ref_lvl_even, ref_lvl_odd_bad, ref_lvl_even_bad;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-007] -------------------- Splitting frames into field buffers...";
        qInfo()<<"[L2B-007]"<<trim_fill<<"total lines in the buffer";
    }
#endif

    line_ind = 0;
    ref_lvl_odd = ref_lvl_even = ref_lvl_odd_bad = ref_lvl_even_bad = 0;
    f1_max_line = f2_max_line = 0;
    frasm_f1.odd_data_lines = frasm_f1.even_data_lines = frasm_f2.odd_data_lines = frasm_f2.even_data_lines = 0;
    frasm_f1.odd_valid_lines = frasm_f1.even_valid_lines = frasm_f2.odd_valid_lines = frasm_f2.even_valid_lines = 0;
    // Reset assembly stats.
    frasm_f1.clearAsmStats();

    // Cycle splitting 2-frame buffer into 4 field buffers.
    while(line_ind<trim_fill)
    {
        // Save current line number.
        line_num = trim_buf[line_ind].line_number;
        // Check for stray service line.
        if(trim_buf[line_ind].isServiceLine()!=false)
        {
            if(trim_buf[line_ind].isServFiller()==false)
            {
                if(trim_buf[line_ind].isServCtrlBlk()!=false)
                {
                    qDebug()<<"[L2B-007] Stray Control Block"<<trim_buf[line_ind].frame_number<<trim_buf[line_ind].line_number;
                }
                // Skip stray unfiltered or lost sync service line.
                line_ind++;
                continue;
            }
        }
        // Pick lines for frame A.
        if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
        {
            // Update maximum line number for Frame A.
            if(f1_max_line<line_num)
            {
                f1_max_line = line_num;
            }
            // Fill up frame A, splitting fields.
            if((line_num%2)==0)
            {
                // Check if lines are available after trimming (not top = bottom = current = 0).
                if((frasm_f1.even_top_data!=frasm_f1.even_bottom_data)||(frasm_f1.even_top_data!=0))
                {
                    // Check frame trimming.
                    if((line_num>=frasm_f1.even_top_data)&&(line_num<=frasm_f1.even_bottom_data))
                    {
                        // Check array index bounds.
                        if(frasm_f1.even_data_lines<BUF_SIZE_FIELD)
                        {
                            // Fill up even field.
                            frame1_even[frasm_f1.even_data_lines] = trim_buf[line_ind];
                            frasm_f1.even_data_lines++;
                            // Pre-calculate average reference level for all lines.
                            ref_lvl_even_bad += trim_buf[line_ind].ref_level;
                            if(trim_buf[line_ind].isCRCValid()!=false)
                            {
                                // Calculate number of lines with valid CRC in the field.
                                frasm_f1.even_valid_lines++;
                                // Pre-calculate average reference level for valid lines.
                                ref_lvl_even += trim_buf[line_ind].ref_level;
                            }
                        }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                        else
                        {
                            qInfo()<<"[L2B-007] Frame A even field index out of bound! Line skipped!"<<line_num;
                        }
#endif
                    }
                }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                else
                {
                    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
                    {
                        qInfo()<<"[L2B-007] Frame A no line even field skip";
                    }
                }
#endif
            }
            else
            {
                // Check frame trimming.
                if((line_num>=frasm_f1.odd_top_data)&&(line_num<=frasm_f1.odd_bottom_data))
                {
                    // Check array index bounds.
                    if(frasm_f1.odd_data_lines<BUF_SIZE_FIELD)
                    {
                        // Fill up odd field.
                        frame1_odd[frasm_f1.odd_data_lines] = trim_buf[line_ind];
                        frasm_f1.odd_data_lines++;
                        // Pre-calculate average reference level for all lines.
                        ref_lvl_odd_bad += trim_buf[line_ind].ref_level;
                        if(trim_buf[line_ind].isCRCValid()!=false)
                        {
                            // Calculate number of lines with valid CRC in the field.
                            frasm_f1.odd_valid_lines++;
                            // Pre-calculate average reference level for valid lines.
                            ref_lvl_odd += trim_buf[line_ind].ref_level;
                        }
                    }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                    else
                    {
                        qInfo()<<"[L2B-007] Frame A odd field index out of bound! Line skipped!"<<line_num;
                    }
#endif
                }
            }
        }
        // Pick lines for frame B.
        else if(trim_buf[line_ind].frame_number==frasm_f2.frame_number)
        {
            // Update maximum line number for Frame B.
            if(f2_max_line<line_num)
            {
                f2_max_line = line_num;
            }
            // Fill up frame B, splitting fields.
            if((line_num%2)==0)
            {
                // Check if lines are available after trimming (not top = bottom = current = 0).
                if((frasm_f2.even_top_data!=frasm_f2.even_bottom_data)||(frasm_f2.even_top_data!=0))
                {
                    // Check frame trimming.
                    if((line_num>=frasm_f2.even_top_data)&&(line_num<=frasm_f2.even_bottom_data))
                    {
                        // Check array index bounds.
                        if(frasm_f2.even_data_lines<BUF_SIZE_FIELD)
                        {
                            // Fill up even field.
                            frame2_even[frasm_f2.even_data_lines] = trim_buf[line_ind];
                            frasm_f2.even_data_lines++;
                            if(trim_buf[line_ind].isCRCValid()!=false)
                            {
                                // Calculate number of lines with valid CRC in the field.
                                frasm_f2.even_valid_lines++;
                            }
                        }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                        else
                        {
                            qInfo()<<"[L2B-007] Frame B even field index out of bound! Line skipped!"<<line_num;
                        }
#endif
                    }
                }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                else
                {
                    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
                    {
                        qInfo()<<"[L2B-007] Frame B no line even field skip";
                    }
                }
#endif
            }
            else
            {
                // Check frame trimming.
                if((line_num>=frasm_f2.odd_top_data)&&(line_num<=frasm_f2.odd_bottom_data))
                {
                    // Check array index bounds.
                    if(frasm_f2.odd_data_lines<BUF_SIZE_FIELD)
                    {
                        // Fill up odd field.
                        frame2_odd[frasm_f2.odd_data_lines] = trim_buf[line_ind];
                        frasm_f2.odd_data_lines++;
                        if(trim_buf[line_ind].isCRCValid()!=false)
                        {
                            // Calculate number of lines with valid CRC in the field.
                            frasm_f2.odd_valid_lines++;
                        }
                    }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                    else
                    {
                        qInfo()<<"[L2B-007] Frame B odd field index out of bound! Line skipped!"<<line_num;
                    }
#endif
                }
            }
        }
        // Continue search through the buffer.
        line_ind++;
    }

    // Calculate average reference level for odd field.
    if(frasm_f1.odd_valid_lines>0)
    {
        // There was valid data, calculate reference only by valid lines.
        ref_lvl_odd = ref_lvl_odd/frasm_f1.odd_valid_lines;
        frasm_f1.odd_ref = (uint8_t)ref_lvl_odd;
    }
    else if(frasm_f1.odd_data_lines>0)
    {
        // There was no valid data, calculate reference by all lines with something in those.
        ref_lvl_odd_bad = ref_lvl_odd_bad/frasm_f1.odd_data_lines;
        frasm_f1.odd_ref = (uint8_t)ref_lvl_odd_bad;
    }
    else
    {
        frasm_f1.odd_ref = 0;
    }
    // Calculate average reference level for even field.
    if(frasm_f1.even_valid_lines>0)
    {
        // There was valid data, calculate reference only by valid lines.
        ref_lvl_even = ref_lvl_even/frasm_f1.even_valid_lines;
        frasm_f1.even_ref = (uint8_t)ref_lvl_even;
    }
    else if(frasm_f1.even_data_lines>0)
    {
        ref_lvl_even_bad = ref_lvl_even_bad/frasm_f1.even_data_lines;
        frasm_f1.even_ref = (uint8_t)ref_lvl_even_bad;
    }
    else
    {
        frasm_f1.even_ref = 0;
    }

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        QString log_line;
        log_line.sprintf("[L2B-007] %03u lines (%03u valid) in Frame A odd field buffer", frasm_f1.odd_data_lines, frasm_f1.odd_valid_lines);
        qInfo()<<log_line;
        log_line.sprintf("[L2B-007] %03u lines (%03u valid) in Frame A even field buffer", frasm_f1.even_data_lines, frasm_f1.even_valid_lines);
        qInfo()<<log_line;
        log_line.sprintf("[L2B-007] %03u lines (%03u valid) in Frame B odd field buffer", frasm_f2.odd_data_lines, frasm_f2.odd_valid_lines);
        qInfo()<<log_line;
        log_line.sprintf("[L2B-007] %03u lines (%03u valid) in Frame B even field buffer", frasm_f2.even_data_lines, frasm_f2.even_valid_lines);
        qInfo()<<log_line;
        log_line.sprintf("[L2B-007] Frame A average reference level: %03u/%03u (odd/even)", frasm_f1.odd_ref, frasm_f1.even_ref);
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Try to detect audio resolution from data in the field.
//------------------------ If audio resolution is preset from outside - return that and skip detection.
// Input:
//  [*field] pointer to std::vector of lines from a field
//  [f_size] number of lines in that field (start offset always 0)
// Returns:
//  [SAMPLE_RES_16BIT] if field data seems to contain 16-bit audio
//  [SAMPLE_RES_14BIT] if field data seems to contain 14-bit audio
//  [SAMPLE_RES_UNKNOWN] if unable to determine data resolution
uint8_t STC007DataStitcher::getFieldResolution(std::vector<STC007Line> *field, uint16_t f_size)
{
    uint8_t result;
    uint16_t test_size, res14_count, res16_count;

    //QElapsedTimer dbg_timer;
    test_size = res14_count = res16_count = 0;

    if(preset_audio_res==SAMPLE_RES_14BIT)
    {
#ifdef DI_LOG_AUDRES_VERBOSE
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[L2B-007] Audio resolution is preset by user to 14 bits";
        }
#endif
        // Audio resolution is externally preset, return that value.
        return SAMPLE_RES_14BIT;
    }
    else if(preset_audio_res==SAMPLE_RES_16BIT)
    {
#ifdef DI_LOG_AUDRES_VERBOSE
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[L2B-007] Audio resolution is preset by user to 16 bits";
        }
#endif
        // Audio resolution is externally preset, return that value.
        return SAMPLE_RES_16BIT;
    }

    // Check if provided size exceeds data size in the container.
    if(field->size()<f_size)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Array index out-of-bound!"<<field->size()<<f_size;
        return SAMPLE_RES_UNKNOWN;
    }

    // Calculate last line number before a field seam.
    if(f_size>STC007DataBlock::MIN_DEINT_DATA)
    {
        test_size = f_size-STC007DataBlock::MIN_DEINT_DATA;
    }
    else
    {
        //qWarning()<<DBG_ANCHOR<<"[L2B-007] Not enough data ("<<f_size<<")!";
        return SAMPLE_RES_UNKNOWN;
    }

    // Perform automatic resolution detection.
    pad_checker.setInput(field);
    pad_checker.setOutput(&padding_block);
    //pad_checker.setLogLevel(STC007Deinterleaver::LOG_PROCESS);
    pad_checker.setLogLevel(0);
    //pad_checker.setIgnoreCRC(ignore_CRC);
    pad_checker.setIgnoreCRC(false);
    pad_checker.setForceParity(true);
    // Check only P-code, that's enough. Q-code can cause false "corrections" and "valid" CRCs on 16-bit data.
    pad_checker.setPCorrection(true);
    pad_checker.setQCorrection(false);
    for(uint16_t index=0;index<test_size;index++)
    {
#ifdef DI_LOG_AUDRES_VERBOSE
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[L2B-007] Converting at index"<<index<<"to data block, assuming 14-bit samples...";
        }
#endif
        // First, test in 14-bit mode.
        pad_checker.setResMode(STC007Deinterleaver::RES_MODE_14BIT);
        // Fill up data block, performing de-interleaving.
        pad_checker.processBlock(index);
        // Check data block state.
        //if((padding_block.isBlockValid()!=false)&&(padding_block.canForceCheck()!=false)&&(padding_block.isAlmostSilent()==false))
        if((padding_block.isBlockValid()!=false)&&(padding_block.canForceCheck()!=false)&&(padding_block.isSilent()==false))
        {
            // Count valid 14-bit data blocks (excluding total silence).
            res14_count++;
#ifdef DI_LOG_AUDRES_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] Valid 14-bit results:"<<res14_count;
            }
#endif
        }
        else
        {
#ifdef DI_LOG_AUDRES_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] No good 14-bit result this time.";
            }
#endif
            // Check if data is BROKEN.
            if((padding_block.isDataBroken()!=false)&&(res14_count>0))
            {
                // Reduce count of valid blocks for BROKEN ones (wrong resolution should produce many BROKEN blocks).
                res14_count--;
            }
        }
#ifdef DI_LOG_AUDRES_VERBOSE
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[L2B-007] Converting at index"<<index<<"to data block, assuming 16-bit samples...";
        }
#endif
        // Second, test in 16-bit mode.
        pad_checker.setResMode(STC007Deinterleaver::RES_MODE_16BIT);
        // Fill up data block, performing de-interleaving.
        pad_checker.processBlock(index);
        // Check data block state.
        //if((padding_block.isBlockValid()!=false)&&(padding_block.canForceCheck()!=false)&&(padding_block.isAlmostSilent()==false))
        if((padding_block.isBlockValid()!=false)&&(padding_block.canForceCheck()!=false)&&(padding_block.isSilent()==false))
        {
            // Count valid 16-bit data blocks.
            res16_count++;
#ifdef DI_LOG_AUDRES_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] Valid 16-bit results:"<<res16_count;
            }
#endif
        }
        else
        {
#ifdef DI_LOG_AUDRES_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-007] No good 16-bit result this time.";
            }
#endif
            // Check if data is BROKEN.
            if((padding_block.isDataBroken()!=false)&&(res16_count>0))
            {
                // Reduce count of valid blocks for BROKEN ones.
                res16_count--;
            }
        }
    }
    // Restore CRC mode.
    pad_checker.setIgnoreCRC(ignore_CRC);

    //qInfo()<<"Test size:"<<test_size<<", 14 bit:"<<res14_count<<", 16-bit:"<<res16_count;

    // Determine the resolution.
    result = SAMPLE_RES_UNKNOWN;
    if(res14_count>(STC007DataBlock::INTERLEAVE_OFS*2))
    {
        // If data is 16-bit there will be ~ the same count for both.
        // (16-bit is backwards-compatible with 14-bit decoders.)
        test_size = res16_count*128;
        test_size = test_size/res14_count;
        if(test_size>32)
        {
            result = SAMPLE_RES_16BIT;
        }
        else
        {
            result = SAMPLE_RES_14BIT;
        }
    }

#ifdef DI_EN_DBG_OUT
    if(/*((log_level&LOG_PADDING)!=0)&&*/((log_level&LOG_PROCESS)!=0))
    {
        QString log_line;
        log_line = "[L2B-007] Frame ("+QString::number(padding_block.getStartFrame())+") ";
        if((padding_block.getStartLine()%2)==0)
        {
            log_line += "even";
        }
        else
        {
            log_line += "odd";
        }
        log_line += " field resolution: ";
        if(result==SAMPLE_RES_14BIT)
        {
            log_line += "14-bit";
        }
        else if(result==SAMPLE_RES_16BIT)
        {
            log_line += "16-bit";
        }
        else if(result==SAMPLE_RES_UNKNOWN)
        {
            log_line += "UNK";
        }
        if((preset_audio_res==SAMPLE_RES_14BIT)||(preset_audio_res==SAMPLE_RES_16BIT))
        {
            log_line += " (preset)";
        }
        log_line += " (14/16 bit good results: "+QString::number(res14_count)+"/"+QString::number(res16_count)+")";
        qInfo()<<log_line;
    }
#endif
    return result;
}

//------------------------ Determine resulting resolution mode by two other modes in field pair.
// 14S + 14S = 14S
// 16S + 16S = 16S
// 14A + 14A = 14S
// 16A + 16A = 16S
// 14S + 14A = 14S
// 14S + 16A = 16A
// 14S + 16S = 16A
// 14A + 14S = 14A
// 14A + 16A = 16A
// 14A + 16S = 16A
// 16A + 14S = 16A
// 16A + 14A = 16A
// 16A + 16S = 16A
// 16S + 14S = 14A
// 16S + 14A = 16A
// 16S + 16A = 16A
uint8_t STC007DataStitcher::getResolutionModeForSeam(uint8_t in_res_field1, uint8_t in_res_field2)
{
    uint8_t fin_res;
    // Set default resolution mode as auto 16-bit.
    fin_res = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
    if(in_res_field1==in_res_field2)
    {
        fin_res = in_res_field1;
        if(in_res_field1==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
        {
            fin_res = STC007Deinterleaver::RES_MODE_14BIT;
        }
        else if(in_res_field1==STC007Deinterleaver::RES_MODE_16BIT_AUTO)
        {
            fin_res = STC007Deinterleaver::RES_MODE_16BIT;
        }
    }
    else if(in_res_field1==STC007Deinterleaver::RES_MODE_14BIT)
    {
        if(in_res_field2==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
        {
            fin_res = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
        }
    }
    else if(in_res_field1==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
    {
        if(in_res_field2==STC007Deinterleaver::RES_MODE_14BIT)
        {
            fin_res = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
        }
    }
    else if(in_res_field1==STC007Deinterleaver::RES_MODE_16BIT)
    {
        if(in_res_field2==STC007Deinterleaver::RES_MODE_14BIT)
        {
            fin_res = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
        }
    }
    return fin_res;
}

//------------------------ Determine resulting data block resolution by two resolution modes in field pair.
uint8_t STC007DataStitcher::getResolutionForSeam(uint8_t in_res_field1, uint8_t in_res_field2)
{
    uint8_t fin_res;
    fin_res = getResolutionModeForSeam(in_res_field1, in_res_field2);
    if((fin_res==STC007Deinterleaver::RES_MODE_16BIT)||(fin_res==STC007Deinterleaver::RES_MODE_16BIT_AUTO))
    {
        fin_res = STC007DataBlock::RES_16BIT;
    }
    else
    {
        fin_res = STC007DataBlock::RES_14BIT;
    }
    return fin_res;
}

//------------------------ Get resolution of audio samples after [findFieldResolution()] run.
uint8_t STC007DataStitcher::getDataBlockResolution(std::deque<STC007Line> *in_line_buffer, uint16_t line_sh)
{
    if(mode_m2!=false)
    {
        // Force strict 14-bit samples for M2 mode.
        return STC007Deinterleaver::RES_MODE_14BIT;
    }

    if(in_line_buffer==NULL)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Null pointer for buffer provided, exiting...";
        return STC007Deinterleaver::RES_MODE_14BIT_AUTO;
    }

    if(in_line_buffer->size()<=(size_t)(line_sh+STC007DataBlock::MIN_DEINT_DATA))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Buffer index out-of-bound, exiting..."<<in_line_buffer->size()<<(line_sh+STC007DataBlock::MIN_DEINT_DATA);
        return STC007Deinterleaver::RES_MODE_14BIT_AUTO;
    }

    uint8_t first_line_ofs, last_line_ofs, first_res, last_res, fin_res;

    first_line_ofs = STC007DataBlock::LINE_L0;
    last_line_ofs = STC007DataBlock::LINE_Q0;
    first_res = last_res = fin_res = STC007Deinterleaver::RES_MODE_14BIT;

    // Check first line of the block.
    if((*in_line_buffer)[first_line_ofs+line_sh].frame_number==frasm_f2.frame_number)
    {
        // First line is from the Frame B.
        if((((*in_line_buffer)[first_line_ofs+line_sh].line_number)%2)==0)
        {
            // First line is from the even field.
            first_res = frasm_f2.even_resolution;
        }
        else
        {
            // First line is from the odd field.
            first_res = frasm_f2.odd_resolution;
        }
    }
    else if((*in_line_buffer)[first_line_ofs+line_sh].frame_number==frasm_f1.frame_number)
    {
        // First line is from the Frame A.
        if((((*in_line_buffer)[first_line_ofs+line_sh].line_number)%2)==0)
        {
            // First line is from the even field.
            first_res = frasm_f1.even_resolution;
        }
        else
        {
            // First line is from the odd field.
            first_res = frasm_f1.odd_resolution;
        }
    }
    else if((*in_line_buffer)[first_line_ofs+line_sh].frame_number==frasm_f0.frame_number)
    {
        // First line is from the last Frame (Frame 0).
        if((((*in_line_buffer)[first_line_ofs+line_sh].line_number)%2)==0)
        {
            // First line is from the even field.
            first_res = frasm_f0.even_resolution;
        }
        else
        {
            // First line is from the odd field.
            first_res = frasm_f0.odd_resolution;
        }
    }
    // Check last line of the block.
    if((*in_line_buffer)[last_line_ofs+line_sh].frame_number==frasm_f2.frame_number)
    {
        // Last line is from the Frame B.
        if((((*in_line_buffer)[last_line_ofs+line_sh].line_number)%2)==0)
        {
            // Last line is from the even field.
            last_res = frasm_f2.even_resolution;
        }
        else
        {
            // Last line is from the odd field.
            last_res = frasm_f2.odd_resolution;
        }
    }
    else if((*in_line_buffer)[last_line_ofs+line_sh].frame_number==frasm_f1.frame_number)
    {
        // Last line is from the Frame A.
        if((((*in_line_buffer)[last_line_ofs+line_sh].line_number)%2)==0)
        {
            // Last line is from the even field.
            last_res = frasm_f1.even_resolution;
        }
        else
        {
            // Last line is from the odd field.
            last_res = frasm_f1.odd_resolution;
        }
    }
    else if((*in_line_buffer)[last_line_ofs+line_sh].frame_number==frasm_f0.frame_number)
    {
        // Last line is from the last Frame (Frame 0).
        if((((*in_line_buffer)[last_line_ofs+line_sh].line_number)%2)==0)
        {
            // Last line is from the even field.
            last_res = frasm_f0.even_resolution;
        }
        else
        {
            // Last line is from the odd field.
            last_res = frasm_f0.odd_resolution;
        }
    }

    // Determine resulting resolution mode.
    fin_res = getResolutionModeForSeam(first_res, last_res);

#ifdef DI_EN_DBG_OUT
    /*if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
    {
        QString log_line;
        log_line = "[L2B-007] Frame ("+QString::number((*in_line_buffer)[STC007DataBlock::LINE_Q0+line_sh].frame_number)+
                   ") line ("+QString::number((*in_line_buffer)[STC007DataBlock::LINE_Q0+line_sh].line_number)+") resolution: ";
        if(fin_res==STC007Deinterleaver::RES_MODE_14BIT)
        {
             log_line += "14-bit";
        }
        else if(fin_res==STC007Deinterleaver::RES_MODE_16BIT)
        {
             log_line += "16-bit";
        }
        else if(fin_res==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
        {
             log_line += "14-bit (auto)";
        }
        else
        {
             log_line += "16-bit (auto)";
        }
        qInfo()<<log_line;
    }*/
#endif
    return fin_res;
}

//------------------------ Try to stitch fields with provided padding, collect stats.
uint8_t STC007DataStitcher::tryPadding(std::vector<STC007Line> *field1, uint16_t f1_size,
                                        std::vector<STC007Line> *field2, uint16_t f2_size,
                                        uint16_t padding,
                                        FieldStitchStats *stitch_stats)
{
    bool suppress_log, run_lock;
    uint8_t ext_di_log_lvl;
    uint8_t unchecked_lim;
    uint16_t line_count, line_num;
    uint16_t valid_burst_count, silence_burst_count, uncheck_burst_count, broken_count;
    uint16_t valid_burst_max, silence_burst_max, uncheck_burst_max, brk_burst_max;
    uint32_t frame_num;
    size_t buf_size;
    // Empty line container.
    STC007Line empty_line;

    suppress_log = !((log_level&LOG_PADDING_BLOCK)!=0);

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING_LINE)!=0)||(suppress_log==false))
    {
        qInfo()<<"[L2B-007] Checking padding ="<<padding;
    }
#endif
    if((field1==NULL)||(field2==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Null pointer for buffer provided, exiting...";
        return DS_RET_NO_DATA;
    }

    if((field1->size()<f1_size)||(field2->size()<f2_size))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Buffer index out-of-bound, exiting..."<<field1->size()<<f1_size<<field2->size()<<f2_size;
        return DS_RET_NO_DATA;
    }

    // Clear the combined fields storage.
    padding_queue.clear();

    // Dump two fields sequently into one array, inserting padding in process.
    // Calculate how many lines to copy.
    if(f1_size>(STC007DataBlock::MIN_DEINT_DATA+STC007DataBlock::INTERLEAVE_OFS/2-padding))
    {
        // Start from defined line from the end of the field.
        line_count = f1_size-(STC007DataBlock::MIN_DEINT_DATA+STC007DataBlock::INTERLEAVE_OFS/2-padding);
    }
    else
    {
        // Copy all.
        line_count = 0;
    }

    // Copy data from field 1.
    for(uint16_t index=line_count;index<f1_size;index++)
    {
        // Fill from the bottom of [field1] to the top.
        padding_queue.push_back((*field1)[index]);
    }
    // Save frame parameters to carry those into padding.
    line_num = (*field1)[f1_size-1].line_number;
    frame_num = (*field1)[f1_size-1].frame_number;
    // Insert padding lines.
    empty_line.coords.setToZero();
    for(uint16_t ind_pad=0;ind_pad<padding;ind_pad++)
    {
        // Set valid frame and line numbers.
        empty_line.frame_number = frame_num;
        line_num = line_num+2;
        empty_line.line_number = line_num;
        padding_queue.push_back(empty_line);
    }
    // Calculate how many lines to copy.
    if(f2_size>(STC007DataBlock::MIN_DEINT_DATA+STC007DataBlock::INTERLEAVE_OFS/2))
    {
        // Copy only defined number of lines.
        line_count = (STC007DataBlock::MIN_DEINT_DATA+STC007DataBlock::INTERLEAVE_OFS/2);
    }
    else
    {
        // Copy all.
        line_count = f2_size;
    }
    // Copy data from field 2.
    for(uint16_t index=0;index<line_count;index++)
    {
        // Fill from the top of [field2] to the bottom.
        padding_queue.push_back((*field2)[index]);
    }

    // Get final buffer size.
    buf_size = padding_queue.size();
    if(buf_size<STC007DataBlock::MIN_DEINT_DATA)
    {
        // Not enough lines to perform deinterleaving.
        return DS_RET_NO_DATA;
    }

#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_PADDING_LINE)!=0)
    {
        // Dump test lines array.
        for(size_t index=0;index<padding_queue.size();index++) {qInfo()<<"[L2B-007]"<<QString::fromStdString(padding_queue[index].dumpContentString());}
    }
#endif

    valid_burst_count = silence_burst_count = uncheck_burst_count = broken_count = 0;
    valid_burst_max = silence_burst_max = uncheck_burst_max = brk_burst_max = 0;

    unchecked_lim = max_unchecked_14b_blocks;
    if(enable_Q_code==false)
    {
        unchecked_lim = max_unchecked_16b_blocks;
    }

    // Set parameters for test conversion.
    ext_di_log_lvl = 0;
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_DEINTERLEAVE)!=0))
    {
        ext_di_log_lvl |= STC007Deinterleaver::LOG_PROCESS;
    }
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_ERROR_CORR)!=0))
    {
        ext_di_log_lvl |= STC007Deinterleaver::LOG_ERROR_CORR;
    }
    pad_checker.setInput(&padding_queue);
    pad_checker.setOutput(&padding_block);
    pad_checker.setLogLevel(ext_di_log_lvl);
    pad_checker.setResMode(getDataBlockResolution(&padding_queue, 0));
    pad_checker.setIgnoreCRC(ignore_CRC);
    pad_checker.setForceParity(true);
    pad_checker.setPCorrection(enable_P_code);
    pad_checker.setQCorrection(enable_Q_code);
    pad_checker.setCWDCorrection(false);

    // Run deinterleaving and error-detection on combined and padded buffer.
    buf_size = 0;
    while(1)
    {
        // Fill up data block, performing de-interleaving and error-correction.
        if(pad_checker.processBlock(buf_size)!=STC007Deinterleaver::DI_RET_OK)
        {
            // No data left in the buffer or some other error while de-interleaving.
            // Exit line cycle.
            break;
        }
        // At least cycle run once.
        run_lock = true;

        // Check for valid (and not silent) audio data in the block.
        if((padding_block.isBlockValid()!=false)
            &&(padding_block.isSilent()==false)
            &&(padding_block.canForceCheck()!=false))
        {
            valid_burst_count++;
        }
        else
        {
            if(valid_burst_count>valid_burst_max)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Updating 'valid' limit from"<<valid_burst_max<<"to"<<valid_burst_count<<"at pass"<<buf_size;
                }
#endif
                valid_burst_max = valid_burst_count;
            }
        }
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007]"<<QString::fromStdString(padding_block.dumpContentString());
        }
#endif
        // Is there too much silence?
        // P-check and Q-check will not have any use on silence.
        if(padding_block.isSilent()!=false)
        {
            // Found block with too much silence.
            silence_burst_count++;
            if(silence_burst_count>=MAX_BURST_SILENCE)
            {
                valid_burst_count = 0;
            }
        }
        else
        {
            // Not silent at the moment.
            if(silence_burst_count>silence_burst_max)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Updating 'silence' limit from"<<silence_burst_max<<"to"<<silence_burst_count<<"at pass"<<buf_size;
                }
#endif
                // Update longest burst of silence.
                silence_burst_max = silence_burst_count;
            }
            // Reset counter if burst ended.
            silence_burst_count = 0;
        }
        // Check for "uncheckable" data blocks: invalid, with no P and Q, with Q-corrected samples.
        // Q-correction can "correct" samples from broken data blocks, resulting in loud popping noises.
        // Make sure that there are no too many Q-corrections in a row to make data block validation possible.
        if(((enable_Q_code!=false)&&((padding_block.canForceCheck()==false)||(padding_block.isDataFixedByQ()!=false)))||
          ((enable_Q_code==false)&&(padding_block.isDataFixedByP()!=false)))        // With disabled Q for 14-bit burst of P-corrections can skew padding result.
        {
            // Block can not be checked for validity.
            uncheck_burst_count++;
            // Choose limit according to resolution.
            if(uncheck_burst_count>=unchecked_lim)
            {
                valid_burst_count = 0;
            }
        }
        else
        {
            if(uncheck_burst_count>uncheck_burst_max)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Updating 'unchecked' limit from"<<uncheck_burst_max<<"to"<<uncheck_burst_count<<"at pass"<<buf_size;
                }
#endif
                // Update longest burst of unchecked blocks.
                uncheck_burst_max = uncheck_burst_count;
            }
            // Reset counter if burst ended.
            uncheck_burst_count = 0;
        }
        // Check for BROKEN data block.
        if(padding_block.isDataBroken()!=false)
        {
            // Found broken "ladders", stitching is incorrect.
            broken_count++;
            if(broken_count>=MAX_BURST_BROKEN)
            {
                valid_burst_count = 0;
            }
        }
        buf_size++;
    }
    // Clear padding queue.
    padding_queue.clear();

    // Update post-cycle counters if required.
    if(valid_burst_count>valid_burst_max)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Updating 'valid' limit from"<<valid_burst_max<<"to"<<valid_burst_count<<"at pass"<<buf_size;
        }
#endif
        valid_burst_max = valid_burst_count;
    }
    if(silence_burst_count>silence_burst_max)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Updating 'silence' limit from"<<silence_burst_max<<"to"<<silence_burst_count<<"at pass"<<buf_size;
        }
#endif
        // Update longest burst of silence.
        silence_burst_max = silence_burst_count;
    }
    if(uncheck_burst_count>uncheck_burst_max)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Updating 'unchecked' limit from"<<uncheck_burst_max<<"to"<<uncheck_burst_count<<"at pass"<<buf_size;
        }
#endif
        // Update longest burst of unchecked blocks.
        uncheck_burst_max = uncheck_burst_count;
    }

    // Output stats for the seam.
    if((stitch_stats!=NULL)&&(run_lock!=false))
    {
        stitch_stats->index = padding;
        stitch_stats->valid = valid_burst_max;
        stitch_stats->silent = silence_burst_max;
        stitch_stats->unchecked = uncheck_burst_max;
        stitch_stats->broken = broken_count;
    }

    // Check for BROKEN data blocks.
    if(broken_count>=MAX_BURST_BROKEN)
    {
        // BROKEN data.
        return DS_RET_BROKE;
    }
    // Check for too much silence.
    if(silence_burst_max>MAX_BURST_SILENCE)
    {
        // Too much silence.
        return DS_RET_SILENCE;
    }
    if(uncheck_burst_max>unchecked_lim)
    {
        // Too many unchecked.
        return DS_RET_NO_PAD;
    }
    // Check is any good blocks were found.
    if(valid_burst_max==0)
    {
        // No good blocks.
        return DS_RET_NO_PAD;
    }
    // Padding is OK.
    return DS_RET_OK;
}

//------------------------ Find line padding to properly (without parity errors) stitch two fields.
uint8_t STC007DataStitcher::findPadding(std::vector<STC007Line> *field1, uint16_t f1_size,
                                        std::vector<STC007Line> *field2, uint16_t f2_size,
                                        uint8_t in_std, uint8_t in_resolution, uint16_t *padding)
{
    bool suppress_log;
    uint16_t pad, max_padding, min_broken;
    uint8_t unchecked_lim, no_brk_idx, stitch_res;

    stitch_res = DS_RET_NO_PAD;
    suppress_log = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    if((field1==NULL)||(field2==NULL)||(padding==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Null pointer provided, exiting...";
        return stitch_res;
    }

    pad = f1_size;
    // Assume default padding according to PAL/NTSC standards.
    if(in_std==FrameAsmDescriptor::VID_PAL)
    {
        // There can be more that standard count of total lines per field
        // while much less valid lines after trimming,
        // check for limits to avoid underrun.
        if(pad>LINES_PF_PAL)
        {
            (*padding) = 0;
        }
        else
        {
            (*padding) = LINES_PF_PAL - pad;
        }
    }
    else if(in_std==FrameAsmDescriptor::VID_NTSC)
    {
        if(pad>LINES_PF_NTSC)
        {
            (*padding) = 0;
        }
        else
        {
            (*padding) = LINES_PF_NTSC - pad;
        }
    }
    else
    {
        (*padding) = 0;
    }
#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-007] Fallback padding:"<<(*padding);
    }
#endif

    max_padding = MAX_PADDING_14BIT;
    unchecked_lim = max_unchecked_14b_blocks;
    // Check Q-code availability to set maximum padding distance.
    if((in_resolution==STC007DataBlock::RES_16BIT)||(enable_Q_code==false))
    {
        // Without Q-code maximum padding is half of that of PQ mode.
        max_padding = MAX_PADDING_16BIT;
        unchecked_lim = max_unchecked_16b_blocks;
    }
#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-007] Maximum available padding:"<<max_padding;
    }
#endif

    last_pad_counter = 0xFF;
    // Check if P-code and/or Q-code are available for force-checking.
    if((enable_P_code!=false)||(enable_Q_code!=false))
    {
        // Initialize stats data.
        std::vector<FieldStitchStats> stitch_data;
        stitch_data.resize(max_padding);
        min_broken = 0xFFFF;
        no_brk_idx = 0;

        // Padding cycle, collect stats data.
        for(pad=0;pad<max_padding;pad++)
        {
            // Try to stitch fields with set padding.
            tryPadding(field1, f1_size, field2, f2_size, pad, &stitch_data[pad]);
            // Find minimum broken blocks within all passes.
            if(min_broken>stitch_data[pad].broken)
            {
                // Min minimum.
                min_broken = stitch_data[pad].broken;
                if(min_broken==0)
                {
                    no_brk_idx = pad;
                }
            }
            else if(min_broken==0)
            {
                // Padding with no broken blocks already found.
                if((stitch_data[no_brk_idx].valid>0)&&(stitch_data[no_brk_idx].unchecked<unchecked_lim)&&(stitch_data[pad].broken>0))
                {
                    // Padding with no broken blocks passes checks for number of valid and unchecked blocks.
                    // Current one has more broken blocks, no need to check further.
                    break;
                }
            }
        }

        // Search for minimum number of broken blocks.
        // Sort by number of valid blocks from max to min.
        // Sort by number of unchecked blocks from min to max.
        // Pick first after sort.
        // If its unchecked blocks count is less than allowed - set as detected padding.
        std::sort(stitch_data.begin(), stitch_data.end());

#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Minimal count of BROKEN blocks:"<<min_broken;
            QString index_line, valid_line, silence_line, unchecked_line, broken_line, tmp;
            for(pad=0;pad<max_padding;pad++)
            {
                tmp.sprintf("%02u", stitch_data[pad].index);
                index_line += "|"+tmp;
                tmp.sprintf("%02x", stitch_data[pad].broken);
                broken_line += "|"+tmp;
                tmp.sprintf("%02x", stitch_data[pad].valid);
                valid_line += "|"+tmp;
                tmp.sprintf("%02x", stitch_data[pad].unchecked);
                unchecked_line += "|"+tmp;
                tmp.sprintf("%02x", stitch_data[pad].silent);
                silence_line += "|"+tmp;
            }
            qInfo()<<"[L2B-007] Sorted stats:";
            qInfo()<<"[L2B-007] Padding:  "<<index_line;
            qInfo()<<"[L2B-007] Broken:   "<<broken_line;
            qInfo()<<"[L2B-007] Valid:    "<<valid_line;
            qInfo()<<"[L2B-007] Unchecked:"<<unchecked_line;
            qInfo()<<"[L2B-007] Silent:   "<<silence_line;
        }
#endif

        last_pad_counter = stitch_data[0].broken;
        // Check for silence.
        if(stitch_data[0].silent<MAX_BURST_SILENCE)
        {
            // Not so many silent blocks.
            if(stitch_data[0].unchecked<unchecked_lim)
            {
                // Not too many unchecked blocks.
                if((stitch_data[0].broken<2)&&(stitch_data[0].broken<stitch_data[1].broken))
                {
                    // After sorting first padding has less BROKEN blocks.
                    stitch_res = DS_RET_OK;
                    (*padding) = stitch_data[0].index;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Detected valid padding of"<<stitch_data[0].index<<"by least count of BROKEN blocks";
                    }
#endif
                }
                else if((((int16_t)stitch_data[0].valid-(int16_t)stitch_data[1].valid)>MAX_BURST_UNCH_DELTA)&&(stitch_data[0].broken==0))
                {
                    stitch_res = DS_RET_OK;
                    (*padding) = stitch_data[0].index;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Detected valid padding of"<<stitch_data[0].index<<"by most valid blocks";
                    }
#endif
                }
                /*else if((((int16_t)stitch_data[1].unchecked-(int16_t)stitch_data[0].unchecked)>MAX_BURST_UNCH_DELTA)&&(stitch_data[0].broken==0))
                {
                    stitch_res = DS_RET_OK;
                    (*padding) = stitch_data[0].index;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Detected valid (but suspicious) padding of"<<stitch_data[0].index<<"by least unchecked blocks";
                    }
#endif
                }*/
#ifdef DI_EN_DBG_OUT
                else
                {
                    // First entry after sorting doesn't differ much from the second one.
                    // Valid padding (at the first positiob) should produce vastly different result.
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Unable to find padding, unsufficient valid blocks";
                    }
                }
#endif
            }
            else
            {
                // Too many unchecked blocks.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Unable to find padding, too many unchecked blocks";
                }
#endif
                // In case of BROKEN blocks on the seam sorting can screw up and ignore valid blocks.
                // Equal out all BROKEN counters to sort by valid blocks.
                for(pad=0;pad<max_padding;pad++)
                {
                    stitch_data[pad].broken = min_broken;
                    if(stitch_data[pad].unchecked>=unchecked_lim)
                    {
                        stitch_data[pad].broken = 0xFF;
                    }
                }
                // Re-sort stats.
                std::sort(stitch_data.begin(), stitch_data.end());
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString index_line, valid_line, silence_line, unchecked_line, tmp;
                    qInfo()<<"[L2B-007] Maybe too many BROKEN blocks on the seam, ignoring BROKEN counter and re-sorting...";
                    for(pad=0;pad<max_padding;pad++)
                    {
                        tmp.sprintf("%02u", stitch_data[pad].index);
                        index_line += "|"+tmp;
                        tmp.sprintf("%02x", stitch_data[pad].valid);
                        valid_line += "|"+tmp;
                        tmp.sprintf("%02x", stitch_data[pad].unchecked);
                        unchecked_line += "|"+tmp;
                        tmp.sprintf("%02x", stitch_data[pad].silent);
                        silence_line += "|"+tmp;
                    }
                    qInfo()<<"[L2B-007] Re-sorted stats:";
                    qInfo()<<"[L2B-007] Padding:  "<<index_line;
                    qInfo()<<"[L2B-007] Valid:    "<<valid_line;
                    qInfo()<<"[L2B-007] Unchecked:"<<unchecked_line;
                    qInfo()<<"[L2B-007] Silent:   "<<silence_line;
                }
#endif
                if(stitch_data[0].unchecked<unchecked_lim)
                {
                    // Not too many unchecked blocks.
                    if(((int16_t)stitch_data[0].valid-(int16_t)stitch_data[1].valid)>MAX_BURST_UNCH_DELTA)
                    {
                        stitch_res = DS_RET_OK;
                        (*padding) = stitch_data[0].index;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-007] Detected valid padding of"<<stitch_data[0].index<<"by most valid blocks";
                        }
#endif
                    }
                    /*else if(((int16_t)stitch_data[1].unchecked-(int16_t)stitch_data[0].unchecked)>MAX_BURST_UNCH_DELTA)
                    {
                        stitch_res = DS_RET_OK;
                        (*padding) = stitch_data[0].index;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-007] Detected valid (but suspicious) padding of"<<stitch_data[0].index<<"by least unchecked blocks";
                        }
#endif
                    }*/
#ifdef DI_EN_DBG_OUT
                    else
                    {
                        // First entry after sorting doesn't differ much from the second one.
                        // Valid padding (at the first positiob) should produce vastly different result.
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-007] Unable to find padding, unsufficient valid blocks";
                        }
                    }
#endif
                }
#ifdef DI_EN_DBG_OUT
                else
                {
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Unable to find padding, too many unchecked blocks";
                    }
                }
#endif
            }
        }
        else
        {
            // Too many silent blocks.
            stitch_res = DS_RET_SILENCE;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] Unable to find padding due to silence";
            }
#endif
        }
    }
#ifdef DI_EN_DBG_OUT
    else
    {
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] No P and Q codes, no data for padding detection";
        }
    }
#endif

    return stitch_res;
}

//------------------------ Clear field order history.
void STC007DataStitcher::clearFieldOrderStats()
{
    stats_field_order.fill(FrameAsmDescriptor::ORDER_UNK);
}

//------------------------ Add new field order to the list, remove oldest one.
void STC007DataStitcher::updateFieldOrderStats(uint8_t new_order)
{
    // Replace oldest value with the new one.
    stats_field_order.push(new_order);
}

//------------------------ Get most probable field order from stats.
uint8_t STC007DataStitcher::getProbableFieldOrder()
{
    uint8_t count_tff, count_bff;
    count_tff = count_bff = 0;
    // Count good field orders.
    for(uint8_t index=0;index<STATS_DEPTH;index++)
    {
        if(stats_field_order[index]==FrameAsmDescriptor::ORDER_TFF)
        {
            count_tff++;
        }
        else if(stats_field_order[index]==FrameAsmDescriptor::ORDER_BFF)
        {
            count_bff++;
        }
    }
    // Determine field order by statistics.
    if((count_tff>0)||(count_bff>0))
    {
        if(count_tff<count_bff)
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-007] Good field order by stats is BFF (TFF: %02u, BFF: %02u)", count_tff, count_bff);
                qInfo()<<log_line;
            }
#endif
            return FrameAsmDescriptor::ORDER_BFF;
        }
        else
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-007] Good field order by stats is TFF (TFF: %02u, BFF: %02u)", count_tff, count_bff);
                qInfo()<<log_line;
            }
#endif
            return FrameAsmDescriptor::ORDER_TFF;
        }
    }
    else
    {
#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_FIELD_ASSEMBLY)!=0)
        {
            QString log_line;
            log_line.sprintf("[L2B-007] Field order not found by stats (TFF: %02u, BFF: %02u)", count_tff, count_bff);
            qInfo()<<log_line;
        }
#endif
        return FrameAsmDescriptor::ORDER_UNK;
    }
}

//------------------------ Clear audio sample resolution history.
void STC007DataStitcher::clearResolutionStats()
{
    stats_resolution.fill(SAMPLE_RES_UNKNOWN);
}

//------------------------ Add new resolution to the list, remove oldest one.
void STC007DataStitcher::updateResolutionStats(uint8_t new_res)
{
    // Replace oldest value with the new one.
    stats_resolution.push(new_res);
}

//------------------------ Get most probable resolution from stats.
uint8_t STC007DataStitcher::getProbableResolution()
{
    uint8_t count_14bit, count_16bit;
    count_14bit = count_16bit = 0;
    // Count good resolution entries.
    for(uint8_t index=0;index<STATS_DEPTH;index++)
    {
        if(stats_resolution[index]==SAMPLE_RES_14BIT)
        {
            count_14bit++;
        }
        if(stats_resolution[index]==SAMPLE_RES_16BIT)
        {
            count_16bit++;
        }
    }
    // Determine resolution by statistics.
    if((count_14bit>0)||(count_16bit>0))
    {
        if(count_14bit<count_16bit)
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-007] Good resolution by stats is 16-bit (14-bit: %02u, 16-bit: %02u)", count_14bit, count_16bit);
                qInfo()<<log_line;
            }
#endif
            return SAMPLE_RES_16BIT;
        }
        else
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-007] Good resolution by stats is 14-bit (14-bit: %02u, 16-bit: %02u)", count_14bit, count_16bit);
                qInfo()<<log_line;
            }
#endif
            return SAMPLE_RES_14BIT;
        }
    }
    else
    {
#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            QString log_line;
            log_line.sprintf("[L2B-007] Resolution not found by stats (14-bit: %02u, 16-bit: %02u)", count_14bit, count_16bit);
            qInfo()<<log_line;
        }
#endif
        return SAMPLE_RES_UNKNOWN;
    }
}

//------------------------ Detect audio sample resolution in both field for Frame A and for Frame B.
// Input:
//  4x internal field containers for Frame A and Frame B.
// Updates:
//  [frasm_f1] and [frasm_f2] fields [odd_resolution] and [even_resolution].
// Returns:
//  Nothing.
void STC007DataStitcher::detectAudioResolution()
{
    bool suppress_log;
    uint8_t f1o_res, f1e_res, f2o_res, f2e_res;

    suppress_log = !(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0));

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-007] -------------------- Resolution detection...";
    }
#endif

    // Check if M2 sample format is set.
    if(mode_m2==false)
    {
        // Non-M2 format, normal samples.
        // Get audio resolution in the fields (preset or by testing data in the fields).
        f1o_res = getFieldResolution(&frame1_odd, frasm_f1.odd_data_lines);
        f1e_res = getFieldResolution(&frame1_even, frasm_f1.even_data_lines);
        f2o_res = getFieldResolution(&frame2_odd, frasm_f2.odd_data_lines);
        f2e_res = getFieldResolution(&frame2_even, frasm_f2.even_data_lines);
        // Update resolution history if detected or preset.
        if((f1o_res==SAMPLE_RES_14BIT)||(f1o_res==SAMPLE_RES_16BIT))
        {
            updateResolutionStats(f1o_res);
        }
        if((f1e_res==SAMPLE_RES_14BIT)||(f1e_res==SAMPLE_RES_16BIT))
        {
            updateResolutionStats(f1e_res);
        }

        if((f1o_res==SAMPLE_RES_UNKNOWN)&&(f1e_res==SAMPLE_RES_UNKNOWN))
        {
            // No resolution data for both fields of Frame A.
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] No resolution data on both fields of Frame A";
            }
#endif
            if((f2o_res==SAMPLE_RES_UNKNOWN)&&(f2e_res==SAMPLE_RES_UNKNOWN))
            {
                // No resolution data for both fields of Frame B.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] No resolution data on both fields of Frame B either";
                }
#endif
                // Get stats on resolution.
                f1o_res = getProbableResolution();
                if(f1o_res==SAMPLE_RES_16BIT)
                {
                    // 16-bit by stats.
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = frasm_f2.odd_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] All fields (Frame A and Frame B) are assumed to be 16-bit by stats";
                    }
#endif
                }
                else
                {
                    // If 14-bit or unknown by stats, assume 14-bit.
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = frasm_f2.odd_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        if(f1o_res==SAMPLE_RES_14BIT)
                        {
                            qInfo()<<"[L2B-007] All fields (Frame A and Frame B) are assumed to be 14-bit by stats";
                        }
                        else
                        {
                            qInfo()<<"[L2B-007] All fields (Frame A and Frame B) are assumed to be 14-bit by default, no stats available yet";
                        }
                    }
#endif
                }
            }
            else if(f2o_res==SAMPLE_RES_UNKNOWN)
            {
                // No resolution for odd field of Frame B.
                if(f2e_res==SAMPLE_RES_16BIT)
                {
                    // Frame B even field has 16-bit resolution.
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT;
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B even field is detected as 16-bit, assume all other fields to be 16-bit";
                    }
#endif
                }
                else
                {
                    // Frame B even field has 14-bit resolution.
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B even field is detected as 14-bit, assume all other fields to be 14-bit";
                    }
#endif
                }
            }
            else if(f2e_res==SAMPLE_RES_UNKNOWN)
            {
                // No resolution for even field of Frame B.
                if(f2o_res==SAMPLE_RES_16BIT)
                {
                    // Frame B odd field has 16-bit resolution.
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT;
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B odd field is detected as 16-bit, assume all other fields to be 16-bit";
                    }
#endif
                }
                else
                {
                    // Frame B odd field has 14-bit resolution.
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT;
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B odd field is detected as 14-bit, assume all other fields to be 14-bit";
                    }
#endif
                }
            }
            else
            {
                // Resolution detected for both fields of Frame B.
                if((f2o_res==f2e_res)&&(f2o_res==SAMPLE_RES_16BIT))
                {
                    // Both fields of Frame B have 16-bit resolution.
                    frasm_f2.odd_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT;
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Both fields of Frame B are detected as 16-bit, assume Frame A fields to be 16-bit";
                    }
#endif
                }
                else
                {
                    if(f2o_res==SAMPLE_RES_16BIT)
                    {
                        frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-007] Frame B odd field is detected as 16-bit";
                        }
#endif
                    }
                    else
                    {
                        frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-007] Frame B odd field is detected as 14-bit";
                        }
#endif
                    }
                    if(f2e_res==SAMPLE_RES_16BIT)
                    {
                        frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-007] Frame B even field is detected as 16-bit";
                        }
#endif
                    }
                    else
                    {
                        frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-007] Frame B even field is detected as 14-bit";
                        }
#endif
                    }
                    // Different or 14-bit fields of Frame B, assume 14 bits for Frame A.
                    frasm_f1.odd_resolution = frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Assume Frame A fields to be 14-bit";
                    }
#endif
                }
            }
        }
        else
        {
            if(f1o_res==SAMPLE_RES_UNKNOWN)
            {
                if(f1e_res==SAMPLE_RES_16BIT)
                {
                    frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_16BIT;
                    frasm_f1.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A even field is detected as 16-bit, assume Frame A odd field to be 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
                    frasm_f1.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A even field is detected as 14-bit, assume Frame A odd field to be 14-bit";
                    }
#endif
                }
            }
            else if(f1e_res==SAMPLE_RES_UNKNOWN)
            {
                if(f1o_res==SAMPLE_RES_16BIT)
                {
                    frasm_f1.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT;
                    frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A odd field is detected as 16-bit, assume Frame A even field to be 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f1.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT;
                    frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A odd field is detected as 14-bit, assume Frame A even field to be 14-bit";
                    }
#endif
                }
            }
            else
            {
                if(f1o_res==SAMPLE_RES_16BIT)
                {
                    frasm_f1.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A odd field is detected as 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f1.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A odd field is detected as 14-bit";
                    }
#endif
                }
                if(f1e_res==SAMPLE_RES_16BIT)
                {
                    frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_16BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A even field is detected as 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame A even field is detected as 14-bit";
                    }
#endif
                }
            }
            if((f2o_res==SAMPLE_RES_UNKNOWN)&&(f2e_res==SAMPLE_RES_UNKNOWN))
            {
                // No resolution data for both fields of Frame B.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] No resolution data on both fields of Frame B";
                }
#endif
                // Get stats on resolution.
                f2o_res = getProbableResolution();
                if(f2o_res==SAMPLE_RES_16BIT)
                {
                    // 16-bit by stats.
                    frasm_f2.odd_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Both fields of Frame B are assumed to be 16-bit by stats";
                    }
#endif
                }
                else
                {
                    // If 14-bit or unknown by stats, assume 14-bit.
                    frasm_f2.odd_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Both fields of Frame B are assumed to be 14-bit by stats";
                    }
#endif
                }
            }
            else if(f2o_res==SAMPLE_RES_UNKNOWN)
            {
                if(f2e_res==SAMPLE_RES_16BIT)
                {
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT;
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B even field is detected as 16-bit, assume Frame B odd field to be 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B even field is detected as 14-bit, assume Frame B odd field to be 14-bit";
                    }
#endif
                }
            }
            else if(f2e_res==SAMPLE_RES_UNKNOWN)
            {
                if(f2o_res==SAMPLE_RES_16BIT)
                {
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT;
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B odd field is detected as 16-bit, assume Frame B even field to be 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT;
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT_AUTO;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B odd field is detected as 14-bit, assume Frame B even field to be 14-bit";
                    }
#endif
                }
            }
            else
            {
                if(f2o_res==SAMPLE_RES_16BIT)
                {
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_16BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B odd field is detected as 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f2.odd_resolution = STC007Deinterleaver::RES_MODE_14BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B odd field is detected as 14-bit";
                    }
#endif
                }
                if(f2e_res==SAMPLE_RES_16BIT)
                {
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_16BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B even field is detected as 16-bit";
                    }
#endif
                }
                else
                {
                    frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Frame B even field is detected as 14-bit";
                    }
#endif
                }
            }
        }
    }
    else
    {
        // M2 sample mode based on 14-bit words.
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Both fields of Frame A are set to be 14-bit by M2 sample mode";
            qInfo()<<"[L2B-007] Both fields of Frame B are set to be 14-bit by M2 sample mode";
        }
#endif
        frasm_f1.odd_resolution = frasm_f1.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
        frasm_f2.odd_resolution = frasm_f2.even_resolution = STC007Deinterleaver::RES_MODE_14BIT;
    }

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        qInfo()<<"[L2B-007] Summary:";
        log_line = "[L2B-007] Frame A ("+QString::number(frasm_f1.frame_number)+") odd lines: "+QString::number(frasm_f1.odd_data_lines);
        if(frasm_f1.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT)
        {
            log_line += ", 14-bit";
            if(mode_m2!=false)
            {
                log_line += " (for M2 samples)";
            }
        }
        else if(frasm_f1.odd_resolution==STC007Deinterleaver::RES_MODE_16BIT)
        {
            log_line += ", 16-bit";
        }
        else if(frasm_f1.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
        {
            log_line += ", 14-bit (assumed)";
        }
        else if(frasm_f1.odd_resolution==STC007Deinterleaver::RES_MODE_16BIT_AUTO)
        {
            log_line += ", 16-bit (assumed)";
        }
        else
        {
            log_line += ", resolution UNKNOWN";
        }
        qInfo()<<log_line;
        log_line = "[L2B-007] Frame A ("+QString::number(frasm_f1.frame_number)+") even lines: "+QString::number(frasm_f1.even_data_lines);
        if(frasm_f1.even_resolution==STC007Deinterleaver::RES_MODE_14BIT)
        {
            log_line += ", 14-bit";
            if(mode_m2!=false)
            {
                log_line += " (for M2 samples)";
            }
        }
        else if(frasm_f1.even_resolution==STC007Deinterleaver::RES_MODE_16BIT)
        {
            log_line += ", 16-bit";
        }
        else if(frasm_f1.even_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
        {
            log_line += ", 14-bit (assumed)";
        }
        else if(frasm_f1.even_resolution==STC007Deinterleaver::RES_MODE_16BIT_AUTO)
        {
            log_line += ", 16-bit (assumed)";
        }
        else
        {
            log_line += ", resolution UNKNOWN";
        }
        qInfo()<<log_line;
        log_line = "[L2B-007] Frame B ("+QString::number(frasm_f2.frame_number)+") odd lines: "+QString::number(frasm_f2.odd_data_lines);
        if(frasm_f2.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT)
        {
            log_line += ", 14-bit";
            if(mode_m2!=false)
            {
                log_line += " (for M2 samples)";
            }
        }
        else if(frasm_f2.odd_resolution==STC007Deinterleaver::RES_MODE_16BIT)
        {
            log_line += ", 16-bit";
        }
        else if(frasm_f2.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
        {
            log_line += ", 14-bit (assumed)";
        }
        else if(frasm_f2.odd_resolution==STC007Deinterleaver::RES_MODE_16BIT_AUTO)
        {
            log_line += ", 16-bit (assumed)";
        }
        else
        {
            log_line += ", resolution UNKNOWN";
        }
        qInfo()<<log_line;
        log_line = "[L2B-007] Frame B ("+QString::number(frasm_f2.frame_number)+") even lines: "+QString::number(frasm_f2.even_data_lines);
        if(frasm_f2.even_resolution==STC007Deinterleaver::RES_MODE_14BIT)
        {
            log_line += ", 14-bit";
            if(mode_m2!=false)
            {
                log_line += " (for M2 samples)";
            }
        }
        else if(frasm_f2.even_resolution==STC007Deinterleaver::RES_MODE_16BIT)
        {
            log_line += ", 16-bit";
        }
        else if(frasm_f2.even_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
        {
            log_line += ", 14-bit (assumed)";
        }
        else if(frasm_f2.even_resolution==STC007Deinterleaver::RES_MODE_16BIT_AUTO)
        {
            log_line += ", 16-bit (assumed)";
        }
        else
        {
            log_line += ", resolution UNKNOWN";
        }
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Detect video standard for frame re-assembling.
// Input:
//  Number of data lines per field from [frasm_f1] and [frasm_f2],
//  maximum source line numbers from [f1_max_line] and [f2_max_line].
// Updates:
//  [frasm_f1] and [frasm_f2] fields [video_standard], [vid_std_preset], [field_order], [order_preset] and [order_guessed].
// Returns:
//  Nothing.
void STC007DataStitcher::detectVideoStandard()
{
    bool suppress_log;

    suppress_log = !(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0));

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-007] -------------------- Video standard detection starting...";
    }
#endif

    frasm_f1.video_standard = FrameAsmDescriptor::VID_UNKNOWN;
    frasm_f1.odd_std_lines = frasm_f1.even_std_lines = 0;
    // Check if video standard is preset.
    if(preset_video_mode==FrameAsmDescriptor::VID_UNKNOWN)
    {
        // Video standard is not preset externally.
        frasm_f1.vid_std_preset = false;
        // Next check count of lines with some data in the fields.
        // Note that count may increase above normal due to noise interpreted as PCM with bad CRC.
        // Check if at least one field contains more lines that PAL field could.
        if((frasm_f1.odd_data_lines>LINES_PF_MAX_PAL)||(frasm_f1.even_data_lines>LINES_PF_MAX_PAL)||(frasm_f2.odd_data_lines>LINES_PF_MAX_PAL)||(frasm_f2.even_data_lines>LINES_PF_MAX_PAL))
        {
            // Too many lines with PCM for PAL video.
            frasm_f1.video_standard = FrameAsmDescriptor::VID_UNKNOWN;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] Unknown video standard (too many lines for PAL)";
            }
#endif
        }
        // Check if at least one field contains more lines that NTSC field could.
        else if((frasm_f1.odd_data_lines>LINES_PF_MAX_NTSC)||(frasm_f1.even_data_lines>LINES_PF_MAX_NTSC)||(frasm_f2.odd_data_lines>LINES_PF_MAX_NTSC)||(frasm_f2.even_data_lines>LINES_PF_MAX_NTSC))
        {
            // Too many lines with PCM for NTSC video, but should fit PAL.
            frasm_f1.video_standard = FrameAsmDescriptor::VID_PAL;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] Seems to be PAL video (by PCM lines)";
            }
#endif
        }
        else
        {
            // Can not determine between PAL and NTSC by actual PCM line count, try determining by source line number.
            if(f1_max_line<=((LINES_PF_PAL-STC007DataBlock::INTERLEAVE_OFS)*2))
            {
                frasm_f1.video_standard = FrameAsmDescriptor::VID_NTSC;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Seems to be NTSC video (by frame lines)";
                }
#endif
            }
            else
            {
                frasm_f1.video_standard = FrameAsmDescriptor::VID_PAL;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Seems to be PAL video (by frame lines ="<<f1_max_line<<")";
                }
#endif
            }
        }
    }
    else
    {
        // Go with externally preset standard.
        frasm_f1.vid_std_preset = true;
        // Save video standard in Frame A trim data.
        frasm_f1.video_standard = preset_video_mode;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(frasm_f1.video_standard==FrameAsmDescriptor::VID_NTSC)
            {
                qInfo()<<"[L2B-007] Video standard preset to: NTSC";
            }
            else if(frasm_f1.video_standard==FrameAsmDescriptor::VID_PAL)
            {
                qInfo()<<"[L2B-007] Video standard preset to: PAL";
            }
            else
            {
                qWarning()<<DBG_ANCHOR<<"[L2B-007] Video standard is not preset: error!";
            }
        }
#endif
    }
    // Check if video standard was set correctly.
    if(frasm_f1.video_standard==FrameAsmDescriptor::VID_UNKNOWN)
    {
        // Try to assume by the previous frame.
        frasm_f1.video_standard = frasm_f0.video_standard;
    }

    // Set number of target/standard lines by the video standard.
    if(frasm_f1.video_standard==FrameAsmDescriptor::VID_NTSC)
    {
        // Both fields to NTSC line count.
        frasm_f1.odd_std_lines = frasm_f1.even_std_lines = LINES_PF_NTSC;
    }
    else if(frasm_f1.video_standard==FrameAsmDescriptor::VID_PAL)
    {
        // Both fields to PAL line count.
        frasm_f1.odd_std_lines = frasm_f1.even_std_lines = LINES_PF_PAL;
    }

    // Preset field order for Frame A and Frame B if required.
    if(preset_field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        // Preset TFF for Frame A and Frame B.
        frasm_f1.presetTFF();
        frasm_f2.presetTFF();
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Field order preset to: TFF";
        }
#endif
    }
    else if(preset_field_order==FrameAsmDescriptor::ORDER_BFF)
    {
        // Preset BFF for Frame A and Frame B.
        frasm_f1.presetBFF();
        frasm_f2.presetBFF();
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Field order preset to: BFF";
        }
#endif
    }
    else
    {
        // Field order is not preset.
        // Clear field order data for Frame B.
        frasm_f2.presetOrderClear();
        frasm_f2.setOrderUnknown();
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Field order is not preset";
        }
#endif
    }
}

//------------------------ Find a way to stitch next 4 fields together,
//------------------------ detect audio resolution (per field), detect video standard, detect field order and padding.
uint8_t STC007DataStitcher::findFieldStitching()
{
    bool suppress_log, suppress_anylog, en_sw_order;
    uint8_t proc_state, stage_count, stitch_resolution, f_res;

    suppress_log = !(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0));
    suppress_anylog = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    QElapsedTimer dbg_timer;
    dbg_timer.start();

    // Try to detect audio resolution in the fields.
    detectAudioResolution();

    // Try to detect video standard (number of PCM lines in the frame) and set field order.
    detectVideoStandard();

    // Set first stage.
    proc_state = STG_TRY_PREVIOUS;

    dbg_timer.start();

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-007] -------------------- Padding detection starting...";
    }
#endif
    // Now let's try to detect padding.
    en_sw_order = true;
    stage_count = 0;
    stitch_resolution = STC007DataBlock::RES_14BIT;
    do
    {
        // Count loops.
        stage_count++;

        //qDebug()<<"[DI] State #"<<proc_state;
        dbg_timer.start();

//------------------------ Try to perform the same stitching as on frame before.
        if(proc_state==STG_TRY_PREVIOUS)
        {
            // Preset "go full processing" state.
            proc_state = STG_A_PREPARE;
            // Check trim data (previous frame has the same line count per frame and valid padding).
            if((frasm_f0.odd_data_lines==frasm_f1.odd_data_lines)&&(frasm_f0.even_data_lines==frasm_f1.even_data_lines)
                &&(frasm_f0.inner_padding_ok!=false)&&(frasm_f0.outer_padding_ok!=false))
            {
                // Check if Frame A field order is preset externally in [detectVideoStandard()]
                // and if preset order is the same as for previous frame with valid padding.
                if((frasm_f1.isOrderPreset()==false)||(frasm_f0.field_order==frasm_f1.field_order))
                {
                    // Reset frame trim data.
                    frasm_f1.inner_silence = frasm_f1.outer_silence = frasm_f2.inner_silence = frasm_f2.outer_silence = true;
                    //frasm_f2.setOrderUnknown();
                    frasm_f2.inner_padding_ok = frasm_f2.outer_padding_ok = false;
                    frasm_f2.inner_padding = frasm_f2.outer_padding = 0;
                    // Check if there is enough data in fields of Frame A.
                    if((frasm_f1.odd_data_lines<MIN_FILL_LINES_PF)&&(frasm_f1.even_data_lines<MIN_FILL_LINES_PF))
                    {
                        // Not enough usefull lines, it will be impossible to compile data blocks.
                        // Unable to detect Frame A field padding and A-B padding.
                        frasm_f1.setOrderUnknown();
                        frasm_f1.inner_padding_ok = frasm_f1.outer_padding_ok = false;
                        frasm_f1.inner_padding = frasm_f1.outer_padding = 0;
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007](EASY) Not enough usefull lines in Frame A, padding unknown, skipping...";
                        }
#endif
                        // Nothing usefull can be done with Frame A.
                        proc_state = STG_PAD_NO_GOOD;
                    }
                    else
                    {
                        // Preset "bad" result of padding.
                        f_res = DS_RET_NO_PAD;
                        // Try to make frame field stitching, using previous data.
                        if(frasm_f0.isOrderTFF()!=false)
                        {
                            // Previous frame was stitched using TFF field order.
#ifdef DI_EN_DBG_OUT
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007](EASY) Try to stitch Frame A fields in TFF order using padding"<<frasm_f0.inner_padding;
                            }
#endif
                            // Try to stitch Frame A in the same way.
                            f_res = tryPadding(&frame1_odd, frasm_f1.odd_data_lines, &frame1_even, frasm_f1.even_data_lines, frasm_f0.inner_padding);
                        }
                        else if(frasm_f0.isOrderBFF()!=false)
                        {
                            // Previous frame was stitched using BFF field order.
#ifdef DI_EN_DBG_OUT
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007](EASY) Try to stitch Frame A fields in BFF order using padding"<<frasm_f0.inner_padding;
                            }
#endif
                            // Try to stitch Frame A in the same way.
                            f_res = tryPadding(&frame1_even, frasm_f1.even_data_lines, &frame1_odd, frasm_f1.odd_data_lines, frasm_f0.inner_padding);
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            // Somehow field order was not set for previous frame... should not happen.
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007](EASY) Previous frame has bad stitching, unable to stitch current one!";
                            }
                        }
#endif
                        // If frame field stitching is ok...
                        if(f_res==DS_RET_OK)
                        {
                            // Save current frame stitching info.
                            frasm_f1.updateVidStdSoft(frasm_f0.video_standard);
                            frasm_f1.field_order = frasm_f0.field_order;
                            frasm_f1.inner_padding = frasm_f0.inner_padding;
                            frasm_f1.inner_padding_ok = true;
                            // Preset "no silence" for padding.
                            frasm_f1.inner_silence = false;
                            // Try to get interframe stitching.
                            if(frasm_f1.isOrderTFF()!=false)
                            {
                                // Save counter for TFF.
                                frasm_f1.tff_cnt = last_pad_counter;
#ifdef DI_EN_DBG_OUT
                                if(suppress_anylog==false)
                                {
                                    qInfo()<<"[L2B-007](EASY) Frame A field order: TFF, inner padding:"<<frasm_f1.inner_padding;
                                }
#endif
                                proc_state = STG_TRY_TFF_TO_TFF;
                            }
                            else
                            {
                                // Save counter for BFF.
                                frasm_f1.bff_cnt = last_pad_counter;
#ifdef DI_EN_DBG_OUT
                                if(suppress_anylog==false)
                                {
                                    qInfo()<<"[L2B-007](EASY) Frame A field order: BFF, inner padding:"<<frasm_f1.inner_padding;
                                }
#endif
                                proc_state = STG_TRY_BFF_TO_BFF;
                            }
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            // Failed to stitch fields by previous frame settings, skip easy stitching (mode is preset above).
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007](EASY) Failed to stitch in easy-mode (by previous frame). Switching to hard-mode stitching...";
                            }
                        }
#endif
                    }
                }
#ifdef DI_EN_DBG_OUT
                else
                {
                    // Previous frame stitching was for different field order than preset for Frame A.
                    // Skip easy stitching (mode is preset above).
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007](EASY) Previous frame was stitched with different field order than preset, going hard-mode stitching...";
                    }
                }
#endif
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                // Frame trim data mismatch, skip easy stitching (mode is preset above).
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Previous frame trimming is invalid for easy-mode, going hard-mode stitching...";
                }
            }
#endif
        }
//------------------------ EASY stitching on inter-frame padding is done, try to EASY stitch frame A to B (TFF).
        else if(proc_state==STG_TRY_TFF_TO_TFF)
        {
            // Preset "bad" result.
            f_res = DS_RET_NO_PAD;
            if(frasm_f2.odd_data_lines>=MIN_FILL_LINES_PF)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Try to stitch frames A-B, assuming TFF-TFF and using padding"<<frasm_f0.outer_padding;
                }
#endif
                f_res = tryPadding(&frame1_even, frasm_f1.even_data_lines, &frame2_odd, frasm_f2.odd_data_lines, frasm_f0.outer_padding);
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Not enough usefull lines in odd field of Frame B.";
                }
            }
#endif
            // If frame field stitching is ok...
            if(f_res==DS_RET_OK)
            {
                // Save current frame stitching info.
                frasm_f1.outer_padding = frasm_f0.outer_padding;
                frasm_f1.outer_padding_ok = true;
                frasm_f2.setOrderTFF();
                // Preset "no silence" for padding.
                frasm_f1.outer_silence = false;
                // Save video mode.
                //updateVideoStandard(frasm_f1.video_standard);
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Frames A-B TFF-TFF padding:"<<frasm_f1.outer_padding;
                }
#endif
                // Work is done.
                proc_state = STG_PAD_OK;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Unable to get A-B stitching in easy-mode (by previous frame). Switching to hard-mode stitching...";
                }
#endif
                proc_state = STG_AB_TFF_TO_TFF;
                // Deny switching to another field combination.
                en_sw_order = false;
            }
        }
//------------------------ EASY stitching on inter-frame padding is done, try to EASY stitch frame A to B (BFF).
        else if(proc_state==STG_TRY_BFF_TO_BFF)
        {
            // Preset "bad" result.
            f_res = DS_RET_NO_PAD;
            if(frasm_f2.even_data_lines>=MIN_FILL_LINES_PF)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Try to stitch frames A-B, assuming BFF-BFF and using padding"<<frasm_f0.outer_padding;
                }
#endif
                f_res = tryPadding(&frame1_odd, frasm_f1.odd_data_lines, &frame2_even, frasm_f2.even_data_lines, frasm_f0.outer_padding);
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Not enough usefull lines in even field of Frame B.";
                }
            }
#endif
            // If frame field stitching is ok...
            if(f_res==DS_RET_OK)
            {
                // Save current frame stitching info.
                frasm_f1.outer_padding = frasm_f0.outer_padding;
                frasm_f1.outer_padding_ok = true;
                frasm_f2.setOrderBFF();
                // Preset "no silence" for padding.
                frasm_f1.outer_silence = false;
                // Save video mode.
                //updateVideoStandard(frasm_f1.video_standard);
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Frames A-B BFF-BFF padding:"<<frasm_f1.outer_padding;
                }
#endif
                // Work is done.
                proc_state = STG_PAD_OK;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007](EASY) Unable to get A-B stitching in easy-mode (by previous frame). Switching to hard-mode stitching...";
                }
#endif
                proc_state = STG_AB_BFF_TO_BFF;
                // Deny switching to another combination.
                en_sw_order = false;
            }
        }
//------------------------ Prepare normal (full) Frame A padding.
        else if(proc_state==STG_A_PREPARE)
        {
            frasm_f1.inner_padding_ok = frasm_f1.outer_padding_ok = false;
            frasm_f1.inner_padding = frasm_f1.outer_padding = 0;
            frasm_f1.tff_cnt = frasm_f1.bff_cnt = 0;
            // Check if there is enough data in fields of Frame A.
            if((frasm_f1.odd_data_lines<MIN_FILL_LINES_PF)&&(frasm_f1.even_data_lines<MIN_FILL_LINES_PF))
            {
                // Not enough usefull lines, it will be impossible to compile data blocks.
                // Unable to detect Frame A field padding and A-B padding.
                if(frasm_f1.isOrderPreset()==false)
                {
                    frasm_f1.setOrderUnknown();
                }
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Not enough usefull lines in Frame A, padding unknown, skipping...";
                }
#endif
                // Nothing usefull can be done with Frame A.
                proc_state = STG_PAD_NO_GOOD;
            }
            else if(frasm_f1.even_data_lines<MIN_FILL_LINES_PF)
            {
                // Not enough usefull lines in even field.
                // Unable to detect Frame A field padding,
                // but maybe A-B will work...
                // Check if frame was already successfully stitched with previous one by odd field.
                if(frasm_f1.isOrderTFF()!=false)
                {
                    // Odd field was already set to be stitched with previous frame, unable to make it 'second' field.
                    frasm_f1.outer_padding_ok = false;
                    frasm_f1.outer_padding = 0;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in even field of Frame A, but odd field was already stitched, skipping...";
                    }
#endif
                    // Nothing usefull can be done with Frame A.
                    proc_state = STG_PAD_NO_GOOD;
                }
                else
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in even field of Frame A, try to detect A-B BFF-BFF...";
                    }
#endif
                    // Try to detect A-B padding, assuming Frame A and Frame B both BFF.
                    proc_state = STG_AB_BFF_TO_BFF;
                    // Deny switching to another combination.
                    en_sw_order = false;
                }
            }
            else if(frasm_f1.odd_data_lines<MIN_FILL_LINES_PF)
            {
                // Not enough usefull lines in odd field.
                // Unable to detect Frame A field padding,
                // but maybe A-B will work...
                //frasm_f1.inner_padding = 0;
                //frasm_f1.inner_padding_ok = false;
                // Check if frame was already successfully stitched with previous one by even field.
                if(frasm_f1.isOrderBFF()!=false)
                {
                    // Even field was already set to be stitched with previous frame, unable to make it 'second' field.
                    frasm_f1.outer_padding_ok = false;
                    frasm_f1.outer_padding = 0;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in odd field of Frame A, but even field was already stitched, skipping...";
                    }
#endif
                    // Nothing usefull can be done with Frame A, check "next Frame A".
                    proc_state = STG_PAD_NO_GOOD;
                }
                else
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in odd field of Frame A, try to detect A-B TFF-TFF...";
                    }
#endif
                    // Try to detect A-B padding, assuming Frame A and Frame B both TFF.
                    proc_state = STG_AB_TFF_TO_TFF;
                    // Deny switching to another combination.
                    en_sw_order = false;
                }
            }
            else
            {
                // There are enough lines in Frame A.
                // Check if frame was already successfully stitched with previous one.
                if(frasm_f1.isOrderBFF()!=false)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frame A field order preset to BFF, try to detect field padding...";
                    }
#endif
                    // Try to detect Frame A field padding, with BFF order.
                    proc_state = STG_A_PAD_BFF;
                    // Deny switching to another field combination.
                    en_sw_order = false;
                }
                else if(frasm_f1.isOrderTFF()!=false)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frame A field order preset to TFF, try to detect field padding...";
                    }
#endif
                    // Try to detect Frame A field padding, with TFF order.
                    proc_state = STG_A_PAD_TFF;
                    // Deny switching to another field combination.
                    en_sw_order = false;
                }
                else
                {
                    // No stitching with previous frame.
                    // Guesstimate starting field order.
                    f_res = getProbableFieldOrder();
                    if(f_res==FrameAsmDescriptor::ORDER_TFF)
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Frame A field order is not preset, but stats says TFF...";
                        }
#endif
                        proc_state = STG_A_PAD_TFF;
                    }
                    else if(f_res==FrameAsmDescriptor::ORDER_BFF)
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Frame A field order is not preset, but stats says BFF...";
                        }
#endif
                        proc_state = STG_A_PAD_BFF;
                    }
                    else
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Frame A field order is not preset, defaulting to TFF...";
                        }
#endif
                        proc_state = STG_A_PAD_TFF;
                    }
                    // Allow switching to another field combination.
                    en_sw_order = true;
                }
            }
        }
//------------------------ Process Frame A padding (TFF).
        else if(proc_state==STG_A_PAD_TFF)
        {
            // Try to detect Frame A field padding, assuming TFF order.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Try to detect Frame A field padding assuming TFF...";
            }
#endif
            // Reset padding.
            frasm_f1.inner_padding = 0;
            // Get resolution for the seam.
            stitch_resolution = getResolutionForSeam(frasm_f1.odd_resolution, frasm_f1.even_resolution);

            // TODO: find new way to determine seam resolution.
            //f_res = findPadding(&frame1_odd, frasm_f1.odd_data_lines, &frame1_even, frasm_f1.even_data_lines, guess_vid, STC007DataBlock::RES_14BIT, &(frasm_f1.inner_padding));
            //f_res = findPadding(&frame1_odd, frasm_f1.odd_data_lines, &frame1_even, frasm_f1.even_data_lines, guess_vid, STC007DataBlock::RES_16BIT, &(frasm_f1.inner_padding));

            // Get padding result.
            f_res = findPadding(&frame1_odd, frasm_f1.odd_data_lines, &frame1_even, frasm_f1.even_data_lines, frasm_f1.video_standard, stitch_resolution, &(frasm_f1.inner_padding));
            // Save counter for TFF.
            frasm_f1.tff_cnt = last_pad_counter;
            // Preset "no silence" for padding.
            frasm_f1.inner_silence = false;
            if(f_res==DS_RET_OK)
            {
                // Padding detected successfully.
                frasm_f1.setOrderTFF();
                frasm_f1.inner_padding_ok = true;
                // Save video mode.
                //updateVideoStandard(guess_vid);
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Frame A field order: TFF, inner padding:"<<frasm_f1.inner_padding;
                }
#endif
                // Try to detect A-B padding, assuming the same TFF order.
                proc_state = STG_AB_TFF_TO_TFF;
                // Deny switching to another combination.
                en_sw_order = false;
            }
            else if(f_res==DS_RET_SILENCE)
            {
                // Can not detect padding due to silence, need to try later.
                frasm_f1.inner_silence = true;
                frasm_f1.outer_silence = true;
                frasm_f1.inner_padding_ok = false;
                frasm_f1.inner_padding = 0;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Unable to detect inner padding and field order of Frame A due to silence";
                }
#endif
                // Nothing usefull can be done with Frame A.
                proc_state = STG_PAD_SILENCE;
            }
            else
            {
                frasm_f1.inner_padding = 0;
                // Check if frame was already successfully stitched with previous one in TFF mode.
                if(frasm_f1.isOrderTFF()!=false)
                {
                    // Frame A is preset to be TFF, can not try to stitch in BFF mode, field padding not found.
                    frasm_f1.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Failed to find field padding for Frame A, but field order preset to TFF, try to detect A-B TFF-TFF...";
                    }
#endif
                    // Try to detect A-B padding, assuming the same TFF order.
                    proc_state = STG_AB_TFF_TO_TFF;
                    // Deny switching to another combination.
                    en_sw_order = false;
                }
                else if(en_sw_order!=false)
                {
                    // If not TFF, try to detect in BFF mode.
                    proc_state = STG_A_PAD_BFF;
                    // Deny switching to another combination.
                    en_sw_order = false;
                }
                else
                {
                    // Not TFF, not BFF... WTF?!
                    proc_state = STG_AB_UNK_PREPARE;
                }
            }
        }
//------------------------ Process Frame A padding (BFF).
        else if(proc_state==STG_A_PAD_BFF)
        {
            // Try to detect Frame A field padding, assuming BFF order.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Try detecting Frame A field padding assuming BFF...";
            }
#endif
            // Reset padding.
            frasm_f1.inner_padding = 0;
            // Get resolution for the seam.
            stitch_resolution = getResolutionForSeam(frasm_f1.even_resolution, frasm_f1.odd_resolution);

            // TODO: find new way to determine seam resolution.
            //f_res = findPadding(&frame1_even, frasm_f1.even_data_lines, &frame1_odd, frasm_f1.even_data_lines, guess_vid, STC007DataBlock::RES_14BIT, &(frasm_f1.inner_padding));
            //f_res = findPadding(&frame1_even, frasm_f1.even_data_lines, &frame1_odd, frasm_f1.even_data_lines, guess_vid, STC007DataBlock::RES_16BIT, &(frasm_f1.inner_padding));

            // Get padding result.
            f_res = findPadding(&frame1_even, frasm_f1.even_data_lines, &frame1_odd, frasm_f1.odd_data_lines, frasm_f1.video_standard, stitch_resolution, &(frasm_f1.inner_padding));
            // Save padding passes count for BFF.
            frasm_f1.bff_cnt = last_pad_counter;
            // Preset "no silence" for padding.
            frasm_f1.inner_silence = false;
            if(f_res==DS_RET_OK)
            {
                // Padding detected successfully.
                frasm_f1.setOrderBFF();
                frasm_f1.inner_padding_ok = true;
                // Save video mode.
                //updateVideoStandard(guess_vid);
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Frame A field order: BFF, inner padding:"<<frasm_f1.inner_padding;
                }
#endif
                // Try to detect A-B padding, assuming same BFF order.
                proc_state = STG_AB_BFF_TO_BFF;
                // Deny switching to another combination.
                en_sw_order = false;
            }
            else if(f_res==DS_RET_SILENCE)
            {
                // Can not detect padding due to silence, need to try later.
                frasm_f1.inner_silence = true;
                frasm_f1.outer_silence = true;
                frasm_f1.inner_padding_ok = false;
                frasm_f1.inner_padding = 0;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Unable to detect padding and field order of Frame A due to silence";
                }
#endif
                // Nothing usefull can be done with Frame A.
                proc_state = STG_PAD_SILENCE;
            }
            else
            {
                frasm_f1.inner_padding = 0;
                // Check if frame was already successfully stitched with previous one in BFF mode.
                if(frasm_f1.isOrderBFF()!=false)
                {
                    // Frame A is preset to be BFF, can not try to stitch in TFF mode, field padding not found.
                    frasm_f1.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Failed to find field padding for Frame A, but field order preset to BFF, try to detect A-B BFF-BFF...";
                    }
#endif
                    // Try to detect A-B padding, assuming same BFF order.
                    proc_state = STG_AB_BFF_TO_BFF;
                    // Deny switching to another combination.
                    en_sw_order = false;
                }
                else if(en_sw_order!=false)
                {
                    // If not BFF, try to detect in TFF mode.
                    proc_state = STG_A_PAD_TFF;
                    // Deny switching to another combination.
                    en_sw_order = false;
                }
                else
                {
                    // Not TFF, not BFF... WTF?!
                    proc_state = STG_AB_UNK_PREPARE;
                }
            }
        }
//------------------------ No valid inter-frame Frame A stitching, try A-B stitching.
        else if(proc_state==STG_AB_UNK_PREPARE)
        {
            // Frame A padding is not detected, still try to detect A-B padding.
            frasm_f1.inner_padding = 0;
            frasm_f1.inner_padding_ok = false;
            frasm_f1.setOrderUnknown();
            // Try to detect Frame A field padding from last good stats.
            f_res = getProbableFieldOrder();
            if(f_res==FrameAsmDescriptor::ORDER_TFF)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Can not detect padding for Frame A, assume TFF by stats.";
                }
#endif
                // Still try to stitch frames, start from TFF-TFF.
                proc_state = STG_AB_TFF_TO_TFF;
                // Allow switching to another combination.
                en_sw_order = true;
            }
            else if(f_res==FrameAsmDescriptor::ORDER_BFF)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Can not detect padding for Frame A, assume BFF by stats.";
                }
#endif
                // Still try to stitch frames, start from BFF-BFF.
                proc_state = STG_AB_BFF_TO_BFF;
                // Allow switching to another combination.
                en_sw_order = true;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Can not detect padding for Frame A, assume TFF...";
                }
#endif
                // Still try to stitch frames, start from TFF-TFF.
                proc_state = STG_AB_TFF_TO_TFF;
                // Allow switching to another combination.
                en_sw_order = true;
            }
        }
//------------------------ Processing A-B padding (TFF to TFF).
        else if(proc_state==STG_AB_TFF_TO_TFF)
        {
            // Check if Frame B has enough usefull lines to stitch.
            if((frasm_f2.odd_data_lines<MIN_FILL_LINES_PF)&&(frasm_f2.even_data_lines<MIN_FILL_LINES_PF))
            {
                // Not enough usefull lines, it will be impossible to compile data blocks.
                // Unable to detect Frame B field padding and A-B padding.
                frasm_f1.outer_padding = 0;
                frasm_f1.outer_padding_ok = false;
                frasm_f2.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Not enough usefull lines in Frame B, A-B padding unknown.";
                }
#endif
                proc_state = STG_PAD_NO_GOOD;
            }
            else if(frasm_f2.odd_data_lines<MIN_FILL_LINES_PF)
            {
                // Not enough usefull lines in odd field.
                if(frasm_f1.isOrderPreset()==false)
                {
                    // Field order for Frame A and B are not preset.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in odd field of Frame B, try to detect A-B TFF-BFF...";
                    }
#endif
                    // Try with another field of Frame B.
                    proc_state = STG_AB_TFF_TO_BFF;
                }
                else
                {
                    // Frame A (and Frame B should be as well) order is preset to TFF,
                    // impossible to stitch with BFF order.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in odd field of Frame B, field order preset to TFF, padding unknown.";
                    }
#endif
                    frasm_f1.outer_padding = 0;
                    frasm_f1.outer_padding_ok = false;
                    frasm_f2.inner_padding_ok = false;
                    proc_state = STG_PAD_NO_GOOD;
                }
            }
            else
            {
                // Enough usefull lines in odd field of Frame B.
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Try detecting A-B padding, assuming TFF-TFF...";
                }
#endif
                // Get resolution for the seam.
                stitch_resolution = getResolutionForSeam(frasm_f1.even_resolution, frasm_f2.odd_resolution);
                // Try to detect padding between TFF-TFF frames.
                f_res = findPadding(&frame1_even, frasm_f1.even_data_lines, &frame2_odd, frasm_f2.odd_data_lines, frasm_f1.video_standard, stitch_resolution, &(frasm_f1.outer_padding));
                // Preset "no silence" for padding.
                frasm_f1.outer_silence = false;
                if(f_res==DS_RET_OK)
                {
                    // Padding detected successfully.
                    frasm_f1.outer_padding_ok = true;
                    frasm_f2.setOrderTFF();
                    // Work is done.
                    proc_state = STG_PAD_OK;
                    if(frasm_f1.isOrderSet()==false)
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Frames A-B TFF-TFF padding:"<<frasm_f1.outer_padding;
                        }
#endif
                        // If Frame A field order was not set yet - update it.
                        frasm_f1.setOrderTFF();
                    }
                    else if(frasm_f1.isOrderBFF()!=false)
                    {
                        // If Frame A field is set to opposite setting - invalidate A-B padding.
                        frasm_f1.outer_padding_ok = false;
                        //frasm_f2.setOrderUnknown();
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Frames A-B TFF-TFF padding detected, but Frame A is preset as BFF, ignoring.";
                        }
#endif
                        // Conflicting field orders.
                        proc_state = STG_PAD_NO_GOOD;
                    }
                }
                else if(f_res==DS_RET_SILENCE)
                {
                    // Can not detect padding due to silence, need to try later.
                    frasm_f1.outer_silence = true;
                    frasm_f1.outer_padding = 0;
                    frasm_f1.outer_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Unable to detect A-B padding due to silence";
                    }
#endif
                    proc_state = STG_PAD_SILENCE;
                }
                else
                {
                    // Did not find padding between frames.
                    // Check Frame B line count.
                    if(frasm_f2.even_data_lines<MIN_FILL_LINES_PF)
                    {
                        // Not enough usefull lines in even field, impossible to detect padding.
                        frasm_f1.outer_padding = 0;
                        frasm_f1.outer_padding_ok = false;
                        frasm_f2.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] A-B TFF-TFF didn't work. Not enough usefull lines in even field of Frame B, padding unknown.";
                        }
#endif
                        proc_state = STG_PAD_NO_GOOD;
                    }
                    else
                    {
                        // Enough usefull lines in even field of frame B.
                        if(frasm_f1.isOrderPreset()==false)
                        {
                            // Field order for Frame A and B are not preset.
#ifdef DI_EN_DBG_OUT
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007] A-B TFF-TFF didn't work. Switching to TFF-BFF...";
                            }
#endif
                            // Try TFF - BFF.
                            proc_state = STG_AB_TFF_TO_BFF;
                        }
                        else
                        {
                            // Frame A (and Frame B should be as well) order is preset to TFF,
                            // impossible to stitch with BFF order.
#ifdef DI_EN_DBG_OUT
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007] A-B TFF-TFF didn't work. Field order preset to TFF, padding unknown.";
                            }
#endif
                            frasm_f1.outer_padding = 0;
                            frasm_f1.outer_padding_ok = false;
                            proc_state = STG_PAD_NO_GOOD;
                        }
                    }
                }
            }
        }
//------------------------ Processing A-B padding (BFF to BFF).
        else if(proc_state==STG_AB_BFF_TO_BFF)
        {
            // Check if Frame B has enough usefull lines to stitch.
            if((frasm_f2.odd_data_lines<MIN_FILL_LINES_PF)&&(frasm_f2.even_data_lines<MIN_FILL_LINES_PF))
            {
                // Not enough usefull lines, it will be impossible to compile data blocks.
                // Unable to detect Frame B field padding and A-B padding.
                frasm_f1.outer_padding = 0;
                frasm_f1.outer_padding_ok = false;
                frasm_f2.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Not enough usefull lines in Frame B, A-B padding unknown.";
                }
#endif
                proc_state = STG_PAD_NO_GOOD;
            }
            else if(frasm_f2.even_data_lines<MIN_FILL_LINES_PF)
            {
                // Not enough usefull lines in even field.
                if(frasm_f1.isOrderPreset()==false)
                {
                    // Field order for Frame A and B are not preset.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in even field of Frame B, try to detect A-B BFF-TFF...";
                    }
#endif
                    // Try with another field of Frame B.
                    proc_state = STG_AB_BFF_TO_TFF;
                }
                else
                {
                    // Frame A (and Frame B should be as well) order is preset to BFF,
                    // impossible to stitch with TFF order.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Not enough usefull lines in even field of Frame B, field order preset to BFF, padding unknown.";
                    }
#endif
                    frasm_f1.outer_padding = 0;
                    frasm_f1.outer_padding_ok = false;
                    frasm_f2.inner_padding_ok = false;
                    proc_state = STG_PAD_NO_GOOD;
                }
            }
            else
            {
                // Enough usefull lines in even field of frame B.
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Try detecting A-B padding, assuming BFF-BFF...";
                }
#endif
                // Get resolution for the seam.
                stitch_resolution = getResolutionForSeam(frasm_f1.odd_resolution, frasm_f2.even_resolution);
                // Try to detect padding between BFF-BFF frames.
                f_res = findPadding(&frame1_odd, frasm_f1.odd_data_lines, &frame2_even, frasm_f2.even_data_lines, frasm_f1.video_standard, stitch_resolution, &(frasm_f1.outer_padding));
                // Preset "no silence" for padding.
                frasm_f1.outer_silence = false;
                if(f_res==DS_RET_OK)
                {
                    // Padding detected successfully.
                    frasm_f1.outer_padding_ok = true;
                    frasm_f2.setOrderBFF();
                    // Work is done.
                    proc_state = STG_PAD_OK;
                    if(frasm_f1.isOrderSet()==false)
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Frames A-B BFF-BFF padding:"<<frasm_f1.outer_padding;
                        }
#endif
                        // If Frame A field order was not set yet - update it.
                        frasm_f1.setOrderBFF();
                    }
                    else if(frasm_f1.isOrderTFF()!=false)
                    {
                        // If Frame A field is set to opposite setting - invalidate A-B padding.
                        frasm_f1.outer_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Frames A-B BFF-BFF padding detected, but Frame A is preset as TFF, ignoring.";
                        }
#endif
                        // Conflicting field orders.
                        proc_state = STG_PAD_NO_GOOD;
                    }
                }
                else if(f_res==DS_RET_SILENCE)
                {
                    // Can not detect padding due to silence, need to try later.
                    frasm_f1.outer_silence = true;
                    frasm_f1.outer_padding = 0;
                    frasm_f1.outer_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Unable to detect A-B padding due to silence";
                    }
#endif
                    proc_state = STG_PAD_SILENCE;
                }
                else
                {
                    // Did not find padding between frames.
                    // Check Frame B line count.
                    if(frasm_f2.odd_data_lines<MIN_FILL_LINES_PF)
                    {
                        // Not enough usefull lines in odd field, impossible to detect padding.
                        frasm_f1.outer_padding = 0;
                        frasm_f1.outer_padding_ok = false;
                        frasm_f2.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] A-B BFF-BFF didn't work. Not enough usefull lines in odd field of Frame B, padding unknown.";
                        }
#endif
                        proc_state = STG_PAD_NO_GOOD;
                    }
                    else
                    {
                        // Enough usefull lines in odd field of frame B.
                        if(frasm_f1.isOrderPreset()==false)
                        {
                            // Field order for Frame A and B are not preset.
#ifdef DI_EN_DBG_OUT
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007] A-B BFF-BFF didn't work. Switching to BFF-TFF...";
                            }
#endif
                            // Try BFF - TFF.
                            proc_state = STG_AB_BFF_TO_TFF;
                        }
                        else
                        {
                            // Frame A (and Frame B should be as well) order is preset to BFF,
                            // impossible to stitch with TFF order.
#ifdef DI_EN_DBG_OUT
                            if(suppress_anylog==false)
                            {
                                qInfo()<<"[L2B-007] A-B BFF-BFF didn't work. Field order preset to BFF, padding unknown.";
                            }
#endif
                            frasm_f1.outer_padding = 0;
                            frasm_f1.outer_padding_ok = false;
                            proc_state = STG_PAD_NO_GOOD;
                        }
                    }
                }
            }
        }
//------------------------ Processing A-B padding (TFF to BFF).
        else if(proc_state==STG_AB_TFF_TO_BFF)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Try detecting A-B padding, assuming TFF-BFF...";
            }
#endif
            // Get resolution for the seam.
            stitch_resolution = getResolutionForSeam(frasm_f1.even_resolution, frasm_f2.even_resolution);
            // Try to detect padding between TFF-BFF frames.
            f_res = findPadding(&frame1_even, frasm_f1.even_data_lines, &frame2_even, frasm_f2.even_data_lines, frasm_f1.video_standard, stitch_resolution, &(frasm_f1.outer_padding));
            // Preset "no silence" for padding.
            frasm_f1.outer_silence = false;
            if(f_res==DS_RET_OK)
            {
                // Padding detected successfully.
                frasm_f1.outer_padding_ok = true;
                frasm_f2.setOrderBFF();
                // Work is done.
                proc_state = STG_PAD_OK;
                if(frasm_f1.isOrderSet()==false)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frames A-B TFF-BFF padding:"<<frasm_f1.outer_padding;
                    }
#endif
                    // If Frame A field order was not set yet - update it.
                    frasm_f1.setOrderTFF();
                }
                else if(frasm_f1.isOrderBFF()!=false)
                {
                    // If Frame A field is set to opposite setting - invalidate A-B padding.
                    frasm_f1.outer_padding_ok = false;
                    //frasm_f2.setOrderUnknown();
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frames A-B TFF-BFF padding detected, but Frame A is preset as BFF, ignoring.";
                    }
#endif
                    // Conflicting field orders.
                    proc_state = STG_PAD_NO_GOOD;
                }
            }
            else if(f_res==DS_RET_SILENCE)
            {
                // Can not detect padding due to silence, need to try later.
                frasm_f1.outer_silence = true;
                frasm_f1.outer_padding = 0;
                frasm_f1.outer_padding_ok = false;
                frasm_f2.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Unable to detect A-B padding due to silence";
                }
#endif
                proc_state = STG_PAD_SILENCE;
            }
            else
            {
                // Did not find padding between frames.
                frasm_f1.outer_padding = 0;
                frasm_f1.outer_padding_ok = false;
                frasm_f2.inner_padding_ok = false;
                // Check if Frame A is preset to be TFF.
                if((en_sw_order!=false)&&(frasm_f1.even_data_lines>=MIN_FILL_LINES_PF))
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] A-B TFF-BFF didn't work. Switching to BFF-BFF...";
                    }
#endif
                    // Try BFF - BFF.
                    proc_state = STG_AB_BFF_TO_BFF;
                    en_sw_order = false;
                }
                else
                {
                    // Nothing found for Frame A and A-B.
                    proc_state = STG_PAD_NO_GOOD;
                }
            }
        }
//------------------------ Processing A-B padding (BFF to TFF).
        else if(proc_state==STG_AB_BFF_TO_TFF)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Try detecting A-B padding, assuming BFF-TFF...";
            }
#endif
            // Get resolution for the seam.
            stitch_resolution = getResolutionForSeam(frasm_f1.odd_resolution, frasm_f2.odd_resolution);
            // Try to detect padding between BFF-TFF frames.
            f_res = findPadding(&frame1_odd, frasm_f1.odd_data_lines, &frame2_odd, frasm_f2.odd_data_lines, frasm_f1.video_standard, stitch_resolution, &(frasm_f1.outer_padding));
            // Preset "no silence" for padding.
            frasm_f1.outer_silence = false;
            if(f_res==DS_RET_OK)
            {
                // Padding detected successfully.
                frasm_f1.outer_padding_ok = true;
                frasm_f2.setOrderTFF();
                // Work is done.
                proc_state = STG_PAD_OK;
                if(frasm_f1.isOrderSet()==false)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frames A-B BFF-TFF padding:"<<frasm_f1.outer_padding;
                    }
#endif
                    // If Frame A field order was not set yet - update it.
                    frasm_f1.setOrderBFF();
                }
                else if(frasm_f1.isOrderTFF()!=false)
                {
                    // If Frame A field is set to opposite setting - invalidate A-B padding.
                    frasm_f1.outer_padding_ok = false;
                    //frasm_f2.setOrderUnknown();
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frames A-B BFF-TFF padding detected, but Frame A is preset as TFF, ignoring.";
                    }
#endif
                    // Conflicting field orders.
                    proc_state = STG_PAD_NO_GOOD;
                }
            }
            else if(f_res==DS_RET_SILENCE)
            {
                // Can not detect padding due to silence, need to try later.
                frasm_f1.outer_silence = true;
                frasm_f1.outer_padding = 0;
                frasm_f1.outer_padding_ok = false;
                frasm_f2.inner_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Unable to detect A-B padding due to silence";
                }
#endif
                proc_state = STG_PAD_SILENCE;
            }
            else
            {
                // Did not find padding between frames.
                frasm_f1.outer_padding = 0;
                frasm_f1.outer_padding_ok = false;
                frasm_f2.inner_padding_ok = false;
                // Check if Frame A is preset to be BFF.
                if((en_sw_order!=false)&&(frasm_f1.even_data_lines>=MIN_FILL_LINES_PF))
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] A-B BFF-TFF didn't work. Switching to TFF-TFF...";
                    }
#endif
                    // Try TFF - TFF.
                    proc_state = STG_AB_TFF_TO_TFF;
                    en_sw_order = false;
                }
                else
                {
                    // Nothing found for Frame A and A-B.
                    proc_state = STG_PAD_NO_GOOD;
                }
            }
        }
//------------------------ General states.
//------------------------ Padding is done successfully.
        else if(proc_state==STG_PAD_OK)
        {
            // Field order and paddings are found successfully.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Frame padding detected successfully.";
            }
#endif
            // Exit stage cycle.
            break;
        }
//------------------------ Unable to detect padding due to silence.
        else if(proc_state==STG_PAD_SILENCE)
        {
            // Padding can not be found on silence.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Unable to detect frame padding due to silence!";
            }
#endif
            // Exit stage cycle.
            break;
        }
//------------------------ Failed to detect padding.
        else if(proc_state==STG_PAD_NO_GOOD)
        {
            // Padding was not found.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Unable to detect valid frame padding!";
            }
#endif
            // Exit stage cycle.
            break;
        }
        //qDebug()<<"[DI] Stage"<<stage_count<<"time:"<<dbg_timer.nsecsElapsed();

        // Check for looping.
        if(stage_count>STG_PAD_MAX)
        {
#ifdef DI_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[L2B-007] Inf. loop detected, breaking...";
#endif
            return DS_RET_NO_PAD;
        }
    }
    while(1);   // Stages cycle.

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        qInfo()<<"[L2B-007] ----------- Padding results:";
        log_line = "[L2B-007] Frame A padding counters: TFF="+QString::number(frasm_f1.tff_cnt)+", BFF="+QString::number(frasm_f1.bff_cnt);
        qInfo()<<log_line;
        log_line = "[L2B-007] Frame A ("+QString::number(frasm_f1.frame_number)+") field padding: ";
        if(frasm_f1.inner_padding_ok!=false)
        {
            log_line += "OK (";
        }
        else if(frasm_f1.inner_silence!=false)
        {
            log_line += "Silence (";
        }
        else
        {
            log_line += "Fail (";
        }
        log_line += QString::number(frasm_f1.inner_padding)+"), field order: ";
        if(frasm_f1.isOrderTFF()!=false)
        {
            log_line += "TFF";
            if(frasm_f1.isOrderPreset()!=false)
            {
                log_line += " (preset)";
            }
        }
        else if(frasm_f1.isOrderBFF()!=false)
        {
            log_line += "BFF";
            if(frasm_f1.isOrderPreset()!=false)
            {
                log_line += " (preset)";
            }
        }
        else
        {
            log_line += "Unknown";
        }
        qInfo()<<log_line;
        log_line = "[L2B-007] Frames A-B padding: ";
        if(frasm_f1.outer_padding_ok!=false)
        {
            log_line += "OK (";
        }
        else if(frasm_f1.outer_silence!=false)
        {
            log_line += "Silence (";
        }
        else
        {
            log_line += "Fail (";
        }
        log_line += QString::number(frasm_f1.outer_padding)+")";
        qInfo()<<log_line;
        log_line = "[L2B-007] Frame B ("+QString::number(frasm_f2.frame_number)+") field order: ";
        if(frasm_f2.isOrderTFF()!=false)
        {
            log_line += "TFF";
            if(frasm_f2.isOrderPreset()!=false)
            {
                log_line += " (preset)";
            }
        }
        else if(frasm_f2.isOrderBFF()!=false)
        {
            log_line += "BFF";
            if(frasm_f2.isOrderPreset()!=false)
            {
                log_line += " (preset)";
            }
        }
        else
        {
            log_line += "not set";
        }
        qInfo()<<log_line;
    }
#endif

    if(proc_state==STG_PAD_OK)
    {
        return DS_RET_OK;
    }
    else if(proc_state==STG_PAD_SILENCE)
    {
        return DS_RET_SILENCE;
    }
    else
    {
        return DS_RET_NO_PAD;
    }
}

//------------------------ Get field order for frame assembly.
uint8_t STC007DataStitcher::getAssemblyFieldOrder()
{
    bool suppress_log;
    uint8_t last_good_order, cur_field_order;

    suppress_log = ((log_level&LOG_FIELD_ASSEMBLY)==0);
    cur_field_order = FrameAsmDescriptor::ORDER_UNK;

    // Check if field order is detected.
    if(frasm_f1.isOrderSet()!=false)
    {
        // Save known good field order.
        cur_field_order = frasm_f1.field_order;
        if(frasm_f1.isOrderPreset()==false)
        {
            // Update "last good" field order.
            updateFieldOrderStats(cur_field_order);
        }
    }
    else
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-007] Field order is not detected!";
        }
#endif
        // Field order is not set.
        // Check field order for next frame.
        if((frasm_f2.isOrderPreset()!=false)&&(frasm_f2.isOrderSet()!=false))
        {
            // Set it by next frame, which has field order preset.
            cur_field_order = frasm_f2.field_order;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] Setting field order by next frame ("<<frasm_f2.frame_number<<")";
            }
#endif
        }
        // Check field order for previous frame.
        else if((frasm_f0.isOrderSet()!=false)&&(frasm_f0.outer_padding_ok!=false))
        {
            // Set it by previous frame, which has field order set and has valid padding with current frame.
            cur_field_order = frasm_f0.field_order;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] Setting field order by previous frame ("<<frasm_f0.frame_number<<")";
            }
#endif
        }
    }

    // Check if field order was determined by FrameAsmSTC007 data.
    if((cur_field_order!=FrameAsmDescriptor::ORDER_TFF)&&(cur_field_order!=FrameAsmDescriptor::ORDER_BFF))
    {
        // Field order not set yet.
        // Try to get field order by stats (history of last good orders).
        last_good_order = getProbableFieldOrder();
        // Check if there was successfull field order detected before.
        if((last_good_order==FrameAsmDescriptor::ORDER_TFF)||(last_good_order==FrameAsmDescriptor::ORDER_BFF))
        {
            // There is previous good field order.
            cur_field_order = last_good_order;
#ifdef DI_EN_DBG_OUT
            if(last_good_order==FrameAsmDescriptor::ORDER_BFF)
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Setting field order by previous good to BFF";
                }
            }
            else
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Setting field order by previous good to TFF";
                }
            }
#endif
        }
        else
        {
            // Still no field order...
            // Check padding counters.
            if(frasm_f1.tff_cnt<frasm_f1.bff_cnt)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-007] Setting field order by counter to TFF (TFF=%u, BFF=%u)", frasm_f1.tff_cnt, frasm_f1.bff_cnt);
                    qInfo()<<log_line;
                }
#endif
                cur_field_order = FrameAsmDescriptor::ORDER_TFF;
            }
            else if(frasm_f1.tff_cnt>frasm_f1.bff_cnt)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-007] Setting field order by counter to BFF (TFF=%u, BFF=%u)", frasm_f1.tff_cnt, frasm_f1.bff_cnt);
                    qInfo()<<log_line;
                }
#endif
                cur_field_order = FrameAsmDescriptor::ORDER_BFF;
            }
            else
            {
                // Field order is still not set, choose by default.
                cur_field_order = FLD_ORDER_DEFAULT;
#ifdef DI_EN_DBG_OUT
                if((uint8_t)FLD_ORDER_DEFAULT==(uint8_t)FrameAsmDescriptor::ORDER_BFF)
                {
                    // Default is set to BFF, add lines from even field.
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Setting field order by default to BFF";
                    }
                }
                else
                {
                    // Default is set to TFF, add lines from even field.
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-007] Setting field order by default to TFF";
                    }
                }
#endif
            }
        }
    }

    // Field order must be set at this point.
    if(frasm_f1.isOrderSet()==false)
    {
        // Field order was not properly detected, but was assumed and guessed.
        // Set guessed value for stats (will not be used for frame assembly).
        frasm_f1.field_order = cur_field_order;
        frasm_f1.setOrderGuessed(true);
    }
    return cur_field_order;
}

//------------------------ Get first line number for first field to fill.
uint16_t STC007DataStitcher::getFirstFieldLineNum(uint8_t in_order)
{
    if(in_order==FrameAsmDescriptor::ORDER_TFF)
    {
        return 1;
    }
    else
    {
        return 2;
    }
}

//------------------------ Get first line number for second field to fill.
uint16_t STC007DataStitcher::getSecondFieldLineNum(uint8_t in_order)
{
    if(in_order==FrameAsmDescriptor::ORDER_TFF)
    {
        return 2;
    }
    else
    {
        return 1;
    }
}

//------------------------ Fill output line buffer with lines from provided field buffer.
uint16_t STC007DataStitcher::addLinesFromField(std::vector<STC007Line> *field_buf, uint16_t ind_start, uint16_t count, uint16_t *last_line_num)
{
    bool suppress_log;
    uint16_t lines_cnt;
    lines_cnt = 0;

#ifdef DI_EN_DBG_OUT
    uint16_t min_line_num, max_line_num;
    max_line_num = 0;
    min_line_num = 0xFFFF;
#endif

    suppress_log = !(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0));

    // Check array limits.
    if((field_buf->size()>=ind_start)&&(field_buf->size()>=(ind_start+count)))
    {
        // Check if there is anything to copy.
        if(count!=0)
        {
            for(uint16_t index=ind_start;index<(ind_start+count);index++)
            {
                // Copy PCM line into queue.
                conv_queue.push_back((*field_buf)[index]);
                if(last_line_num!=NULL)
                {
                    // Save its line number.
                    (*last_line_num) = (*field_buf)[index].line_number;
                    // Advance line number by 2 (skip a field).
                    (*last_line_num) = (*last_line_num)+2;
                }
#ifdef DI_EN_DBG_OUT
                if(min_line_num>(*field_buf)[index].line_number)
                {
                    min_line_num = (*field_buf)[index].line_number;
                }
                if(max_line_num<(*field_buf)[index].line_number)
                {
                    max_line_num = (*field_buf)[index].line_number;
                }
#endif
                // Count number of added lines.
                lines_cnt++;
            }
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007]"<<lines_cnt<<"lines from field ("<<ind_start<<"..."<<(ind_start+count-1)<<"), #("<<min_line_num<<"..."<<max_line_num<<") inserted into queue";
            }
#endif
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] No lines from field inserted";
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Array access out of bounds! Total:"<<field_buf->size()<<", start index:"<<ind_start<<", stop index:"<<(ind_start+count);
    }
    return lines_cnt;
}

//------------------------ Fill output line buffer with empty lines.
uint16_t STC007DataStitcher::addFieldPadding(uint32_t in_frame, uint16_t line_cnt, uint16_t *last_line_num)
{
    bool suppress_log;
    uint16_t lines_cnt;
    lines_cnt = 0;

#ifdef DI_EN_DBG_OUT
    uint16_t min_line_num, max_line_num;
    max_line_num = 0;
    min_line_num = 0xFFFF;
#endif

    suppress_log = !(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0));

    if(line_cnt==0)
    {
        return lines_cnt;
    }
    for(uint16_t index=0;index<line_cnt;index++)
    {
        STC007Line empty_line;
        // Set values for empty padding line.
        empty_line.frame_number = in_frame;
        if(last_line_num!=NULL)
        {
            empty_line.line_number = (*last_line_num);
            // Advance line number by 2 (skip a field).
            (*last_line_num) = (*last_line_num)+2;
        }
#ifdef DI_EN_DBG_OUT
        if(min_line_num>empty_line.line_number)
        {
            min_line_num = empty_line.line_number;
        }
        if(max_line_num<empty_line.line_number)
        {
            max_line_num = empty_line.line_number;
        }
#endif
        // Push padding line into queue.
        conv_queue.push_back(empty_line);
        lines_cnt++;
    }
#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-007]"<<lines_cnt<<"padding lines #("<<min_line_num<<"..."<<max_line_num<<") inserted into queue";
    }
#endif
    return lines_cnt;
}

//------------------------ Should data block not be reported for bad samples and not be visualized?
bool STC007DataStitcher::isBlockNoReport(STC007DataBlock *in_block)
{
    if(((file_start!=false)&&(in_block->getStartFrame()==frasm_f0.frame_number))
        ||((file_end!=false)&&(in_block->getStopFrame()==frasm_f2.frame_number)))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Fill in one frame into queue for conversion from field buffers, inserting padding.
void STC007DataStitcher::fillFrameForOutput()
{
    bool suppress_log, suppress_anylog;
    uint8_t cur_field_order;
    uint16_t field_1_cnt, field_2_cnt;
    std::vector<STC007Line> *p_field_1, *p_field_2;

    suppress_log = ((log_level&LOG_FIELD_ASSEMBLY)==0);
    suppress_anylog = (((log_level&LOG_PROCESS)==0)&&((log_level&LOG_FIELD_ASSEMBLY)==0));

    cur_field_order = FrameAsmDescriptor::ORDER_UNK;
    field_1_cnt = field_2_cnt = 0;
    p_field_1 = p_field_2 = NULL;

#ifdef DI_EN_DBG_OUT
    if(suppress_anylog==false)
    {
        qInfo()<<"[L2B-007] -------------------- Frame reassembling starting...";
    }
#endif

    // Determine field order for current frame assembly.
    cur_field_order = getAssemblyFieldOrder();

    // Select field data to add lines from.
    if(cur_field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        // Field order is set to TFF.
        // First field = odd, second = even.
        field_1_cnt = frasm_f1.odd_data_lines;
        field_2_cnt = frasm_f1.even_data_lines;
        p_field_1 = &frame1_odd;
        p_field_2 = &frame1_even;
        if(frasm_f0.isOrderSet()!=false)
        {
            if(frasm_f0.isOrderTFF()==false)
            {
                frasm_f0.outer_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Selected field order conflicts with previous frame field order!";
                }
#endif
            }
        }
    }
    else
    {
        // Field order is set to BFF.
        // First field = even, second = odd.
        field_1_cnt = frasm_f1.even_data_lines;
        field_2_cnt = frasm_f1.odd_data_lines;
        p_field_1 = &frame1_even;
        p_field_2 = &frame1_odd;
        if(frasm_f0.isOrderSet()!=false)
        {
            if(frasm_f0.isOrderBFF()==false)
            {
                frasm_f0.outer_padding_ok = false;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-007] Selected field order conflicts with previous frame field order!";
                }
#endif
            }
        }
    }

    // Get target line count by video standard.
    int16_t target_lines_per_field;
    if(frasm_f1.video_standard==FrameAsmDescriptor::VID_PAL)
    {
        target_lines_per_field = LINES_PF_PAL;
    }
    else if(frasm_f1.video_standard==FrameAsmDescriptor::VID_NTSC)
    {
        target_lines_per_field = LINES_PF_NTSC;
    }
    else
    {
        // Pick default standard.
        target_lines_per_field = LINES_PF_DEFAULT;
    }

#ifdef QT_VERSION
    if(field_1_cnt>target_lines_per_field)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Field 1 size over the target!"<<frasm_f1.frame_number<<field_1_cnt<<target_lines_per_field;
    }
    if(field_2_cnt>target_lines_per_field)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Field 2 size over the target!"<<frasm_f1.frame_number<<field_2_cnt<<target_lines_per_field;
    }
#endif

    // Crude fix to avoid frame overflow.
    if(field_1_cnt>target_lines_per_field)
    {
        field_1_cnt = target_lines_per_field;
    }
    if(field_2_cnt>target_lines_per_field)
    {
        field_2_cnt = target_lines_per_field;
    }

    // Determine if there is a need to enable top-line insertion due to uneven line count.
    bool insert_top_line = false;
    insert_top_line = fix_cut_above;

    // Choose a way to assemble a frame.
    uint16_t last_line, lines_to_fill, added_inner, added_outer, added_lines_cnt;
    last_line = lines_to_fill = added_inner = added_outer = added_lines_cnt = 0;

    // Check if leading lines should be added at the start of new file.
    if(file_start!=false)
    {
        uint8_t add_count;
#ifdef DI_EN_DBG_OUT
        if(suppress_anylog==false)
        {
            qInfo()<<"[L2B-007] File just started, adding leading padding...";
        }
#endif
        // Set previous frame number to 0 (number of the first frame in the file should be "1").
        frasm_f0.frame_number = 0;
        // Set previous frame resolution.
        if(cur_field_order==FrameAsmDescriptor::ORDER_TFF)
        {
            frasm_f0.even_resolution = frasm_f0.odd_resolution = frasm_f1.odd_resolution;
        }
        else
        {
            frasm_f0.even_resolution = frasm_f0.odd_resolution = frasm_f1.even_resolution;
        }
        // Set line number.
        if(frasm_f1.video_standard==FrameAsmDescriptor::VID_PAL)
        {
            last_line = LINES_PF_PAL;
        }
        else
        {
            last_line = LINES_PF_NTSC;
        }
        // Calculate padding to put [LINE_R2] sample at the top, no need to pad up to P and Q words.
        add_count = STC007DataBlock::LINE_R2;
        last_line = (last_line*2)-(add_count*2);
        // Add leading padding.
        addFieldPadding(0, add_count, &last_line);
        // Reset line number for normal frame assembly.
        last_line = 0;
    }

    // Check "anchors" for data.
    // Previous frame linked to current frame: anchor A.
    // Field of the current frame are linked: anchor B.
    // Сurrent frame linked to next frame: anchor C.
    if(frasm_f0.outer_padding_ok!=false)
    {
        // Anchor A is present.
        if(frasm_f1.inner_padding_ok!=false)
        {
            // Anchor A and anchor B are present.
            if(frasm_f1.outer_padding_ok!=false)
            {
                // Anchor A and anchor B and anchor C are present.
                // ABC
                // Calculate total number of lines for frame assembly.
                lines_to_fill = field_1_cnt+field_2_cnt+frasm_f1.inner_padding+frasm_f1.outer_padding;
                // Check line count according to video standard.
                if((target_lines_per_field*2)==lines_to_fill)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Data anchors: ABC, path: frame size is correct";
                    }
#endif
                    // Reset line number.
                    last_line = getFirstFieldLineNum(cur_field_order);
                    // Add lines from first field (full field).
                    added_lines_cnt += addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                    // Add field-to-field padding.
                    added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                    added_lines_cnt += added_inner;
                    // Reset line number.
                    last_line = getSecondFieldLineNum(cur_field_order);
                    // Add lines from second field (full field).
                    added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                    // Add frame-to-frame padding.
                    added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
                    added_lines_cnt += added_outer;
                }
                else if((target_lines_per_field*2)>lines_to_fill)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Data anchors: ABC, path: not enough lines";
                    }
#endif
                    // Calculate number of padding lines to fill full frame.
                    lines_to_fill = (target_lines_per_field*2)-lines_to_fill;
                    // Reset line number.
                    last_line = getFirstFieldLineNum(cur_field_order);
                    // Add lines from first field (full field).
                    added_lines_cnt += addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                    // Add field-to-field padding.
                    added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                    added_lines_cnt += added_inner;
                    // Reset line number.
                    last_line = getSecondFieldLineNum(cur_field_order);
                    // Add lines from second field (full field).
                    added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                    // Add frame-to-frame padding.
                    added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
                    // Add padding up to frame standard.
                    added_outer += addFieldPadding(frasm_f1.frame_number, lines_to_fill, &last_line);
                    added_lines_cnt += added_outer;
                    // Need to invalidate bottom seam.
                    frasm_f1.outer_padding_ok = false;
                    frasm_f2.setOrderUnknown();
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frames A-B ("<<frasm_f1.frame_number<<"-"<<frasm_f2.frame_number<<") seam invalidated!";
                    }
#endif
                }
                else
                {
                    // Re-calculate total number of lines for frame assembly (drop outer padding).
                    lines_to_fill = field_1_cnt+field_2_cnt+frasm_f1.inner_padding;
                    if((target_lines_per_field*2)>=lines_to_fill)
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Data anchors: ABC, path: too many lines, cutting padding";
                        }
#endif
                        // Calculate number of padding lines to fill full frame.
                        lines_to_fill = (target_lines_per_field*2)-lines_to_fill;
                        // Reset line number.
                        last_line = getFirstFieldLineNum(cur_field_order);
                        // Add lines from first field (full field).
                        added_lines_cnt += addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                        // Add field-to-field padding.
                        added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                        added_lines_cnt += added_inner;
                        // Reset line number.
                        last_line = getSecondFieldLineNum(cur_field_order);
                        // Add lines from second field (full field).
                        added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                        // Add padding up to frame standard.
                        added_outer = addFieldPadding(frasm_f1.frame_number, lines_to_fill, &last_line);
                        added_lines_cnt += added_outer;
                    }
                    else
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_anylog==false)
                        {
                            qInfo()<<"[L2B-007] Data anchors: ABC, path: too many lines, cutting second field";
                        }
#endif
                        // Calculate number of excess lines that should be cut.
                        lines_to_fill = lines_to_fill-(target_lines_per_field*2);
                        // Reset line number.
                        last_line = getFirstFieldLineNum(cur_field_order);
                        // Add lines from first field (full field).
                        added_lines_cnt += addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                        // Add field-to-field padding.
                        added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                        added_lines_cnt += added_inner;
                        // Reset line number.
                        last_line = getSecondFieldLineNum(cur_field_order);
                        // Add lines from second field (cut data from the end).
                        added_lines_cnt += addLinesFromField(p_field_2, 0, (field_2_cnt-lines_to_fill), &last_line);
                    }
                    // Need to invalidate bottom seam.
                    frasm_f1.outer_padding_ok = false;
                    frasm_f2.setOrderUnknown();
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Frames A-B ("<<frasm_f1.frame_number<<"-"<<frasm_f2.frame_number<<") seam invalidated!";
                    }
#endif
                }
            }
            else
            {
                // Anchor A and anchor B present.
                // No anchor C.
                // AB
                // Calculate total number of lines for frame assembly.
                lines_to_fill = field_1_cnt+field_2_cnt+frasm_f1.inner_padding;
                // Check line count according to video standard.
                if((target_lines_per_field*2)>=lines_to_fill)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Data anchors: AB, path: adding padding";
                    }
#endif
                    // Calculate number of padding lines to fill full frame.
                    lines_to_fill = (target_lines_per_field*2)-lines_to_fill;
                    // Reset line number.
                    last_line = getFirstFieldLineNum(cur_field_order);
                    // Add lines from first field (full field).
                    added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                    // Add field-to-field padding.
                    added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                    added_lines_cnt += added_inner;
                    // Reset line number.
                    last_line = getSecondFieldLineNum(cur_field_order);
                    // Add lines from second field (full field).
                    added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                    // Add padding up to frame standard.
                    added_outer = addFieldPadding(frasm_f1.frame_number, lines_to_fill, &last_line);
                    added_lines_cnt += added_outer;
                }
                else
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-007] Data anchors: AB, path: too many lines, cutting second field";
                    }
#endif
                    // Calculate number of excess lines that should be cut.
                    lines_to_fill = lines_to_fill-(target_lines_per_field*2);
                    // Reset line number.
                    last_line = getFirstFieldLineNum(cur_field_order);
                    // Add lines from first field (full field).
                    added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                    // Add field-to-field padding.
                    added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                    added_lines_cnt += added_inner;
                    // Reset line number.
                    last_line = getSecondFieldLineNum(cur_field_order);
                    // Add lines from second field (cut data from the end).
                    added_lines_cnt += addLinesFromField(p_field_2, 0, (field_2_cnt-lines_to_fill), &last_line);
                }
            }
        }
        else if(frasm_f1.outer_padding_ok!=false)
        {
            // Anchor A and anchor C present.
            // No anchor B.
            // AC
            // Calculate total number of lines for frame assembly.
            lines_to_fill = field_1_cnt+field_2_cnt+frasm_f1.outer_padding;
            // Check line count according to video standard.
            if((target_lines_per_field*2)>=lines_to_fill)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Data anchors: AC, path: adding padding";
                }
#endif
                // Calculate number of padding lines to fill full frame.
                lines_to_fill = (target_lines_per_field*2)-lines_to_fill;
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Add padding up to frame standard.
                added_inner = addFieldPadding(frasm_f1.frame_number, lines_to_fill, &last_line);
                added_lines_cnt += added_inner;
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (full field).
                added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                // Add frame-to-frame padding.
                added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
                added_lines_cnt += added_outer;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Data anchors: AC, path: too many lines, cutting second field";
                }
#endif
                // Calculate number of excess lines that should be cut.
                lines_to_fill = lines_to_fill-(target_lines_per_field*2);
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (cut data from the start).
                added_lines_cnt += addLinesFromField(p_field_2, lines_to_fill, (field_2_cnt-lines_to_fill), &last_line);
                // Add frame-to-frame padding.
                added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
                added_lines_cnt += added_outer;
            }
        }
        else
        {
            // Anchor A present.
            // No anchor B and anchor C.
            // A
            // Calculate total number of lines for frame assembly.
            lines_to_fill = field_1_cnt+field_2_cnt;
            // Check line count according to video standard.
            if((target_lines_per_field*2)>=lines_to_fill)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Data anchors: A, path: adding padding";
                }
#endif
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Add padding up to field standard.
                added_inner = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_1_cnt), &last_line);
                added_lines_cnt += added_inner;
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (full field).
                added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                // Add padding up to field standard.
                added_outer = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_2_cnt), &last_line);
                added_lines_cnt += added_outer;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Data anchors: A, path: too many lines, cutting second field";
                }
#endif
                // Calculate number of excess lines that should be cut.
                lines_to_fill = lines_to_fill-(target_lines_per_field*2);
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (cut data from the end).
                added_lines_cnt += addLinesFromField(p_field_2, 0, (field_2_cnt-lines_to_fill), &last_line);
            }
        }
    }
    // No anchor A.
    else if(frasm_f1.inner_padding_ok!=false)
    {
        // Anchor B is present.
        if(frasm_f1.outer_padding_ok!=false)
        {
            // Anchor B and anchor C are present.
            // No anchor A.
            // BC
            // Calculate total number of lines for frame assembly.
            lines_to_fill = field_1_cnt+field_2_cnt+frasm_f1.inner_padding+frasm_f1.outer_padding;
            // Check line count according to video standard.
            if((target_lines_per_field*2)>=lines_to_fill)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    if((target_lines_per_field*2)==lines_to_fill)
                    {
                        qInfo()<<"[L2B-007] Data anchors: BC, path: frame size is correct";
                    }
                    else
                    {
                        qInfo()<<"[L2B-007] Data anchors: BC, path: adding padding";
                    }
                }
#endif
                // Calculate number of padding lines to fill full frame.
                lines_to_fill = (target_lines_per_field*2)-lines_to_fill;
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add padding up to frame standard.
                added_inner = addFieldPadding(frasm_f1.frame_number, lines_to_fill, &last_line);
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Add field-to-field padding.
                added_inner += addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                added_lines_cnt += added_inner;
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (full field).
                added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                // Add frame-to-frame padding.
                added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
                added_lines_cnt += added_outer;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Data anchors: BC, path: too many lines, cutting first field";
                }
#endif
                // Calculate number of excess lines that should be cut.
                lines_to_fill = lines_to_fill-(target_lines_per_field*2);
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add lines from first field (cut data from the start).
                added_lines_cnt += addLinesFromField(p_field_1, lines_to_fill, (field_1_cnt-lines_to_fill), &last_line);
                // Add field-to-field padding.
                added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                added_lines_cnt += added_inner;
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (full field).
                added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                // Add frame-to-frame padding.
                added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
                added_lines_cnt += added_outer;
            }
        }
        else
        {
            // Anchor B present.
            // No anchor A and anchor C.
            // B
            // Calculate total number of lines for frame assembly.
            lines_to_fill = field_1_cnt+field_2_cnt+frasm_f1.inner_padding;
            // Check line count according to video standard.
            if((target_lines_per_field*2)>=lines_to_fill)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Data anchors: B, path: adding padding";
                }
#endif
                // Calculate number of padding lines to fill full frame.
                lines_to_fill = (target_lines_per_field*2)-lines_to_fill;
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Add field-to-field padding.
                added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                added_lines_cnt += added_inner;
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (full field).
                added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                // Add padding up to frame standard.
                added_outer = addFieldPadding(frasm_f1.frame_number, lines_to_fill, &last_line);
                added_lines_cnt += added_outer;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-007] Data anchors: B, path: too many lines, cutting second field";
                }
#endif
                // Calculate number of excess lines that should be cut.
                lines_to_fill = lines_to_fill-(target_lines_per_field*2);
                // Reset line number.
                last_line = getFirstFieldLineNum(cur_field_order);
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Add field-to-field padding.
                added_inner = addFieldPadding(frasm_f1.frame_number, frasm_f1.inner_padding, &last_line);
                added_lines_cnt += added_inner;
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (cut data from the end).
                added_lines_cnt += addLinesFromField(p_field_2, 0, (field_2_cnt-lines_to_fill), &last_line);
            }
        }
    }
    else if(frasm_f1.outer_padding_ok!=false)
    {
        // Anchor C present.
        // No anchor A and anchor B.
        // C
        // Calculate total number of lines for frame assembly.
        lines_to_fill = field_1_cnt+field_2_cnt+frasm_f1.outer_padding;
        // Check line count according to video standard.
        if((target_lines_per_field*2)>=lines_to_fill)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Data anchors: C, path: adding padding";
            }
#endif
            // Calculate number of padding lines to fill full frame.
            lines_to_fill = (target_lines_per_field*2)-lines_to_fill;
            // Reset line number.
            last_line = getFirstFieldLineNum(cur_field_order);
            // Add lines from first field (full field).
            added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
            // Add padding up to frame standard.
            added_inner = addFieldPadding(frasm_f1.frame_number, lines_to_fill, &last_line);
            added_lines_cnt += added_inner;
            // Reset line number.
            last_line = getSecondFieldLineNum(cur_field_order);
            // Add lines from second field (full field).
            added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
            // Add frame-to-frame padding.
            added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
            added_lines_cnt += added_outer;
        }
        else
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Data anchors: C, path: too many lines, cutting first field";
            }
#endif
            // Calculate number of excess lines that should be cut.
            lines_to_fill = lines_to_fill-(target_lines_per_field*2);
            // Reset line number.
            last_line = getFirstFieldLineNum(cur_field_order);
            // Add lines from first field (cut data from the end).
            added_lines_cnt = addLinesFromField(p_field_1, 0, (field_1_cnt-lines_to_fill), &last_line);
            // Reset line number.
            last_line = getSecondFieldLineNum(cur_field_order);
            // Add lines from second field (full field).
            added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
            // Add frame-to-frame padding.
            added_outer = addFieldPadding(frasm_f1.frame_number, frasm_f1.outer_padding, &last_line);
            added_lines_cnt += added_outer;
        }
    }
    else
    {
        // No data anchors.
        // Calculate total number of lines for frame assembly.
        lines_to_fill = field_1_cnt+field_2_cnt;
        // Check line count according to video standard.
        if((target_lines_per_field*2)>=lines_to_fill)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Data anchors: NONE, path: adding padding";
            }
#endif
            // Reset line number.
            last_line = getFirstFieldLineNum(cur_field_order);
            if((insert_top_line!=false)&&(field_1_cnt>0)&&(field_2_cnt>0))
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    if(insert_top_line!=false)
                    {
                        qInfo()<<"[L2B-007] One line shitfer enabled";
                    }
                }
#endif
                // Need to add line above one field to fix noise.
                if(cur_field_order==FrameAsmDescriptor::ORDER_BFF)
                {
                    added_outer = addFieldPadding(frasm_f1.frame_number, 1, &last_line);
                    // Add lines from first field (full field).
                    added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                    // Make next padding calculation one line less.
                    field_1_cnt++;
                    // Add padding up to field standard.
                    added_inner = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_1_cnt), &last_line);
                    added_lines_cnt += added_inner;
                    // Reset line number.
                    last_line = getSecondFieldLineNum(cur_field_order);
                    // Add lines from second field (full field).
                    added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                    // Add padding up to field standard.
                    added_outer += addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_2_cnt), &last_line);
                    added_lines_cnt += added_outer;
                }
                else
                {
                    // Add lines from first field (full field).
                    added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                    // Add padding up to field standard (plus one line into next field to move next field data one line down).
                    added_inner = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_1_cnt+1), &last_line);
                    added_lines_cnt += added_inner;
                    // Reset line number.
                    last_line = getSecondFieldLineNum(cur_field_order);
                    // Add lines from second field (full field).
                    added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                    // Make next padding calculation one line less.
                    field_2_cnt++;
                    // Add padding up to field standard.
                    added_outer = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_2_cnt), &last_line);
                    added_lines_cnt += added_outer;
                }
            }
            else
            {
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Add padding up to field standard.
                added_inner = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_1_cnt), &last_line);
                added_lines_cnt += added_inner;
                // Reset line number.
                last_line = getSecondFieldLineNum(cur_field_order);
                // Add lines from second field (full field).
                added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                // Add padding up to field standard.
                added_outer = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_2_cnt), &last_line);
                added_lines_cnt += added_outer;
            }
        }
        else
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-007] Data anchors: NONE, path: too many lines";
            }
#endif
            // Reset line number.
            last_line = getFirstFieldLineNum(cur_field_order);
            // Check if current frame field fits within standard limits.
            if(field_1_cnt<target_lines_per_field)
            {
                // Add lines from first field (full field).
                added_lines_cnt = addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
                // Add padding up to field standard.
                added_inner = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_1_cnt), &last_line);
                added_lines_cnt += added_inner;
            }
            else
            {
                // Add lines from first field (cut data from the end up to standard).
                added_lines_cnt = addLinesFromField(p_field_1, 0, target_lines_per_field, &last_line);
            }
            // Reset line number.
            last_line = getSecondFieldLineNum(cur_field_order);
            if(field_2_cnt<target_lines_per_field)
            {
                // Add lines from second field (full field).
                added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
                // Add padding up to field standard.
                added_outer = addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-field_2_cnt), &last_line);
                added_lines_cnt += added_outer;
            }
            else
            {
                // Add lines from second field (cut data from the end up to standard).
                added_lines_cnt += addLinesFromField(p_field_2, 0, target_lines_per_field, &last_line);
            }
        }
    }

    // Check if trailing lines should be added at the end of the file.
    if(file_end!=false)
    {
        uint8_t add_count;
#ifdef DI_EN_DBG_OUT
        if(suppress_anylog==false)
        {
            qInfo()<<"[L2B-007] File ended, adding trailing padding...";
        }
#endif
        // Set line number.
        last_line = 1;
        // Calculate padding.
        add_count = STC007DataBlock::MIN_DEINT_DATA;
        // Add trailing padding.
        addFieldPadding(frasm_f2.frame_number, add_count, &last_line);
    }

    // Store actual padding (in case if padding was not detected).
    frasm_f1.inner_padding = added_inner;
    frasm_f1.outer_padding = added_outer;

#ifdef DI_EN_DBG_OUT
    if(suppress_anylog==false)
    {
        if((target_lines_per_field!=0)&&((target_lines_per_field*2)!=added_lines_cnt))
        {
            qInfo()<<"[L2B-007] Addded"<<added_lines_cnt<<"lines per frame (WRONG COUNT, should be"<<(target_lines_per_field*2)<<")";
        }
        else
        {
            qInfo()<<"[L2B-007] Addded"<<added_lines_cnt<<"lines per frame";
        }
    }
#endif
}

//------------------------ Temporarily add lines from the field of the next frame to assist full-frame CWD.
bool STC007DataStitcher::fillNextFieldForCWD()
{
    bool suppress_log, lines_added;
    uint16_t field_cnt, last_line, added_lines_cnt;
    std::vector<STC007Line> *p_field;

    lines_added = false;
    suppress_log = ((log_level&LOG_FIELD_ASSEMBLY)==0);

    // Check stitching with the next frame.
    if(frasm_f1.outer_padding_ok!=false)
    {
        // Stitching with the next frame is valid.
        if(frasm_f1.isOrderSet()!=false)
        {
            // Field order is detected for current frame.
            last_line = getFirstFieldLineNum(frasm_f1.field_order);
            if(frasm_f1.isOrderTFF()!=false)
            {
                // Fill top field from the next frame to fit bottom field of the current frame.
                p_field = &frame2_odd;
                field_cnt = frasm_f2.odd_data_lines;
            }
            else
            {
                // Fill bottom field from the next frame to fit top field of the current frame.
                p_field = &frame2_even;
                field_cnt = frasm_f2.even_data_lines;
            }
            if(field_cnt>STC007DataBlock::MIN_DEINT_DATA)
            {
                // No need to fill full field, only cross-frame interleave part.
                field_cnt = STC007DataBlock::MIN_DEINT_DATA;
            }
            // Add lines from selected field.
            added_lines_cnt = addLinesFromField(p_field, 0, field_cnt, &last_line);
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] Added"<<added_lines_cnt<<"lines from the next frame's field to perform CWD";
            }
#endif
            lines_added = true;
        }
    }

    return lines_added;
}

//------------------------ Remove temporarily added lines from the deinterleave buffer after CWD is done.
void STC007DataStitcher::removeNextFieldAfterCWD()
{
    uint16_t remove_cnt;
    remove_cnt = 0;
    while(conv_queue.back().frame_number==frasm_f2.frame_number)
    {
        // Remove lines from the next frame.
        conv_queue.pop_back();
        remove_cnt++;
    }
#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_FIELD_ASSEMBLY)!=0)
    {
        qInfo()<<"[L2B-007] Removed"<<remove_cnt<<"temporary lines from the next frame's field";
    }
#endif
}

//------------------------ Scan ahead for BROKEN data blocks and mask false-positive PCM lines.
uint16_t STC007DataStitcher::patchBrokenLines(std::deque<STC007Line> *in_queue)
{
    bool suppress_log;
    uint8_t broken_max;
    uint16_t buf_size, buf_offset, line_offset, fix_count;
    std::array<uint16_t, (LINES_PER_PAL_FIELD*3)> broken_stat;
    STC007DataBlock pcm_block;

    suppress_log = !((log_level&LOG_FIELD_ASSEMBLY)!=0);
    suppress_log = false;

    // Set parameters for converter.
    lines_to_block.setInput(in_queue);
    lines_to_block.setOutput(&pcm_block);
    lines_to_block.setIgnoreCRC(ignore_CRC);
    lines_to_block.setForceParity(!ignore_CRC);
    lines_to_block.setPCorrection(enable_P_code);
    lines_to_block.setQCorrection(enable_Q_code);
    lines_to_block.setCWDCorrection(false);
    lines_to_block.setResMode(getDataBlockResolution(in_queue, 0));

    fix_count = buf_offset = 0;
    broken_max = 0;
    broken_stat.fill(0);
    buf_size = in_queue->size();

    // Dump buffered data into converter.
    while(buf_offset<(buf_size-STC007DataBlock::MIN_DEINT_DATA))
    {
        // Reset data block structure.
        pcm_block.clear();

        // Fill up data block, performing de-interleaving, convert lines to data blocks.
        if(lines_to_block.processBlock(buf_offset)!=STC007Deinterleaver::DI_RET_OK)
        {
            break;
        }

        // Perform BROKEN data detection.
        if(pcm_block.isDataBroken()!=false)
        {
            // Every source line can be false positive.
            // Put number of source lines into stats.
            line_offset = (buf_offset+STC007DataBlock::LINE_L0);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            line_offset = (buf_offset+STC007DataBlock::LINE_R0);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            line_offset = (buf_offset+STC007DataBlock::LINE_L1);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            line_offset = (buf_offset+STC007DataBlock::LINE_R1);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            line_offset = (buf_offset+STC007DataBlock::LINE_L2);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            line_offset = (buf_offset+STC007DataBlock::LINE_R2);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            line_offset = (buf_offset+STC007DataBlock::LINE_P0);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            line_offset = (buf_offset+STC007DataBlock::LINE_Q0);
            if((*in_queue)[line_offset].isCRCValid()!=false)
            {
                broken_stat[line_offset]++;
                if(broken_stat[line_offset]>broken_max)
                {
                    broken_max = broken_stat[line_offset];
                }
            }
            fix_count++;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                QString log_line;
                log_line.sprintf("[L2B-007] Broken block [%03u] from frames [%03u:%03u], valid [%03u,%03u,%03u,%03u,%03u,%03u,%03u,%03u], lines [%03u,%03u,%03u,%03u,%03u,%03u,%03u,%03u], offsets [%03u,%03u,%03u,%03u,%03u,%03u,%03u,%03u], count [%03u,%03u,%03u,%03u,%03u,%03u,%03u,%03u]",
                                 fix_count,
                                 pcm_block.getStartFrame(), pcm_block.getStopFrame(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_L0)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_R0)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_L1)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_R1)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_L2)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_R2)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_P0)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_Q0)].isCRCValid(),
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_L0)].line_number,
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_R0)].line_number,
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_L1)].line_number,
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_R1)].line_number,
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_L2)].line_number,
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_R2)].line_number,
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_P0)].line_number,
                                 (*in_queue)[(buf_offset+STC007DataBlock::LINE_Q0)].line_number,
                                 (buf_offset+STC007DataBlock::LINE_L0),
                                 (buf_offset+STC007DataBlock::LINE_R0),
                                 (buf_offset+STC007DataBlock::LINE_L1),
                                 (buf_offset+STC007DataBlock::LINE_R1),
                                 (buf_offset+STC007DataBlock::LINE_L2),
                                 (buf_offset+STC007DataBlock::LINE_R2),
                                 (buf_offset+STC007DataBlock::LINE_P0),
                                 (buf_offset+STC007DataBlock::LINE_Q0),
                                 broken_stat[buf_offset+STC007DataBlock::LINE_L0],
                                 broken_stat[buf_offset+STC007DataBlock::LINE_R0],
                                 broken_stat[buf_offset+STC007DataBlock::LINE_L1],
                                 broken_stat[buf_offset+STC007DataBlock::LINE_R1],
                                 broken_stat[buf_offset+STC007DataBlock::LINE_L2],
                                 broken_stat[buf_offset+STC007DataBlock::LINE_R2],
                                 broken_stat[buf_offset+STC007DataBlock::LINE_P0],
                                 broken_stat[buf_offset+STC007DataBlock::LINE_Q0]);
                qInfo()<<log_line;
            }
#endif
        }
        // Go to the next line in the buffer.
        buf_offset++;
    }
    // Check if there is enough broken block encounters per line.
    if(broken_max>2)
    {
        std::vector<uint16_t> filt_stat;
        for(buf_offset=0;buf_offset<(LINES_PER_PAL_FIELD*3);buf_offset++)
        {
            if((broken_stat[buf_offset]==broken_max)&&((*in_queue)[buf_offset].isCRCValid()!=false))
            {
                filt_stat.push_back(buf_offset);
            }
        }
        if((filt_stat.size()>0)&&(filt_stat.size()<STC007DataBlock::INTERLEAVE_OFS))
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-007] Lines with BROKEN blocks:";
                QString index_line, frame_line, line_line, broken_line, tmp;
                for(buf_offset=0;buf_offset<filt_stat.size();buf_offset++)
                {
                    tmp.sprintf("%03u", filt_stat[buf_offset]);
                    index_line += "|"+tmp;
                    tmp.sprintf("%03u", in_queue->at(filt_stat[buf_offset]).frame_number);
                    frame_line += "|"+tmp;
                    tmp.sprintf("%03u", in_queue->at(filt_stat[buf_offset]).line_number);
                    line_line += "|"+tmp;
                    tmp.sprintf("%03u", broken_stat[filt_stat[buf_offset]]);
                    broken_line += "|"+tmp;
                }
                qInfo()<<"[L2B-007] Lines, causing BROKEN blocks:";
                qInfo()<<"[L2B-007] Offset: "<<index_line;
                qInfo()<<"[L2B-007] Frame:  "<<frame_line;
                qInfo()<<"[L2B-007] Line:   "<<line_line;
                qInfo()<<"[L2B-007] Broken: "<<broken_line;
            }
#endif
            /*for(buf_offset=0;buf_offset<filt_stat.size();buf_offset++)
            {
                (*in_queue)[filt_stat[buf_offset]].setSilent();
                (*in_queue)[filt_stat[buf_offset]].setInvalidCRC();
                (*in_queue)[filt_stat[buf_offset]].setForcedBad();
            }*/
        }
    }

    return 0;
}

//------------------------ Scan ahead for BROKEN data blocks and mask false-positive PCM lines.
uint16_t STC007DataStitcher::patchBrokenLinesOld(std::deque<STC007Line> *in_queue)
{
    bool suppress_log;
    uint8_t brk_count;
    uint16_t buf_size, buf_offset;
    uint16_t fix_count, brk_ofs, trg_line_ofs;
    STC007DataBlock pcm_block;

    suppress_log = !((log_level&LOG_FIELD_ASSEMBLY)!=0);
    //suppress_log = false;

    // Set parameters for converter.
    lines_to_block.setInput(in_queue);
    lines_to_block.setOutput(&pcm_block);
    lines_to_block.setIgnoreCRC(ignore_CRC);
    lines_to_block.setForceParity(!ignore_CRC);
    lines_to_block.setPCorrection(enable_P_code);
    lines_to_block.setQCorrection(enable_Q_code);
    lines_to_block.setCWDCorrection(false);
    lines_to_block.setResMode(getDataBlockResolution(in_queue, 0));

    fix_count = buf_offset = 0;
    buf_size = in_queue->size();

    // Dump buffered data into converter.
    while(buf_offset<(buf_size-STC007DataBlock::MIN_DEINT_DATA))
    {
        // Reset data block structure.
        pcm_block.clear();

        // Fill up data block, performing de-interleaving, convert lines to data blocks.
        if(lines_to_block.processBlock(buf_offset)!=STC007Deinterleaver::DI_RET_OK)
        {
            break;
        }

        // Perform BROKEN data detection.
        if(pcm_block.isDataBroken()!=false)
        {
            brk_count = 1;
            if((enable_Q_code!=false)
               &&(pcm_block.getResolution()==STC007DataBlock::RES_14BIT)
               &&(pcm_block.isWordLineCRCOk(STC007DataBlock::WORD_Q0)!=false))
            {
                // Pick PCM line with Q-code.
                trg_line_ofs = buf_offset+STC007DataBlock::MIN_DEINT_DATA;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-007] Detected BROKEN data block with line offset %u [%03u-%03u...%03u-%03u] problem seems to be in PCM line [%03u-%03u] for Q-word",
                                     buf_offset,
                                     (*in_queue)[buf_offset].frame_number,
                                     (*in_queue)[buf_offset].line_number,
                                     pcm_block.getStopFrame(),
                                     pcm_block.getStopLine(),
                                     (*in_queue)[trg_line_ofs].frame_number,
                                     (*in_queue)[trg_line_ofs].line_number);
                    qInfo()<<log_line;
                }
#endif
            }
            else
            {
                // Pick PCM line with P-code.
                trg_line_ofs = buf_offset+STC007DataBlock::MIN_DEINT_DATA-STC007DataBlock::INTERLEAVE_OFS;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-007] Detected BROKEN data block with line offset %u [%03u-%03u...%03u-%03u] problem seems to be in PCM line [%03u-%03u] for P-word",
                                     buf_offset,
                                     (*in_queue)[buf_offset].frame_number,
                                     (*in_queue)[buf_offset].line_number,
                                     pcm_block.getStopFrame(),
                                     pcm_block.getStopLine(),
                                     (*in_queue)[trg_line_ofs].frame_number,
                                     (*in_queue)[trg_line_ofs].line_number);
                    qInfo()<<log_line;
                }
#endif
            }

            brk_ofs = buf_offset;
            // Cycle through all data blocks that will come next, containing the same PCM line.
            for(uint8_t iwork=0;iwork<(STC007DataBlock::WORD_CNT-1);iwork++)
            {
                // Take line coordinate of the word source and get coordinate of the next data block, using the same line for next word up.
                brk_ofs += STC007DataBlock::INTERLEAVE_OFS;
                // Check that that block is within buffer limits.
                if(brk_ofs<buf_size)
                {
                    // Assemble and check next data block with the same PCM line.
                    pcm_block.clear();
                    lines_to_block.processBlock(brk_ofs);
                    if(pcm_block.isDataBroken()!=false)
                    {
                        // Data in the block is BROKEN.
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            QString log_line;
                            log_line.sprintf("[L2B-007] Block at line offset %u [%03u-%03u...%03u-%03u]: BROKEN as well",
                                             brk_ofs,
                                             pcm_block.getStartFrame(),
                                             pcm_block.getStartLine(),
                                             pcm_block.getStopFrame(),
                                             pcm_block.getStopLine());
                            qInfo()<<log_line;
                        }
#endif
                        // Increase block counter.
                        brk_count++;
                    }
                    else if(pcm_block.isBlockValid()!=false)
                    {
                        // Data block is valid...
                        if(pcm_block.canForceCheck()==false)
                        {
                            // ...but data can not be force-checked, so it can be BROKEN.
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                QString log_line;
                                log_line.sprintf("[L2B-007] Block at line offset %u [%03u-%03u...%03u-%03u]: not checkable, suspicious",
                                                 brk_ofs,
                                                 pcm_block.getStartFrame(),
                                                 pcm_block.getStartLine(),
                                                 pcm_block.getStopFrame(),
                                                 pcm_block.getStopLine());
                                qInfo()<<log_line;
                            }
#endif
                            // Increase block counter.
                            brk_count++;
                        }
#ifdef DI_EN_DBG_OUT
                        else if(pcm_block.isSilent()!=false)
                        {
                            // ...but all samples are silent.
                            if(suppress_log==false)
                            {
                                QString log_line;
                                log_line.sprintf("[L2B-007] Block at line offset %u [%03u-%03u...%03u-%03u]: silent, suspicious",
                                                 brk_ofs,
                                                 pcm_block.getStartFrame(),
                                                 pcm_block.getStartLine(),
                                                 pcm_block.getStopFrame(),
                                                 pcm_block.getStopLine());
                                qInfo()<<log_line;
                            }
                        }
                        else
                        {
                            if(suppress_log==false)
                            {
                                QString log_line;
                                log_line.sprintf("[L2B-007] Block at line offset %u [%03u-%03u...%03u-%03u]: OK",
                                                 brk_ofs,
                                                 pcm_block.getStartFrame(),
                                                 pcm_block.getStartLine(),
                                                 pcm_block.getStopFrame(),
                                                 pcm_block.getStopLine());
                                qInfo()<<log_line;
                            }
                        }
#endif
                    }
                    else
                    {
                        // Data block is invalid.
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            QString log_line;
                            log_line.sprintf("[L2B-007] Block at line offset %u [%03u-%03u...%03u-%03u]: invalid (%u errors)",
                                             brk_ofs,
                                             pcm_block.getStartFrame(),
                                             pcm_block.getStartLine(),
                                             pcm_block.getStopFrame(),
                                             pcm_block.getStopLine(),
                                             pcm_block.getErrorsTotalSource());
                            qInfo()<<log_line;
                        }
#endif
                        // Increase block counter.
                        brk_count++;
                    }
                }
                else
                {
                    // Buffer is depleted.
                    break;
                }
            }

            // Check counter against the limit.
            if(brk_count>=6)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-007] Number of affected blocks with presumably BROKEN line: %01u",
                                     brk_count);
                    log_line.sprintf("[L2B-007] Forcing line [%03u-%03u] BAD",
                                     (*in_queue)[trg_line_ofs].frame_number,
                                     (*in_queue)[trg_line_ofs].line_number);
                    qInfo()<<log_line;
                }
#endif
                // Force PCM line bad.
                (*in_queue)[trg_line_ofs].setInvalidCRC();
                (*in_queue)[trg_line_ofs].setForcedBad();
                fix_count++;
            }
        }
        // Go to the next line in the buffer.
        buf_offset++;
    }

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        if(fix_count>0)
        {
            qInfo()<<"[L2B-007] Patched"<<fix_count<<"false-positive lines";
        }
    }
#endif

    return fix_count;
}

//------------------------ Perform Cross-Word Decoding, propagating fixed samples back to the PCM lines.
uint16_t STC007DataStitcher::performCWD(std::deque<STC007Line> *in_queue)
{
    bool suppress_log;
    uint8_t max_fixable;
    uint16_t buf_size, buf_ofs, line_ofs, line_fix_cnt;
    uint16_t old_word, old_bitword, old_src_crc, old_clc_crc, word_patch_cnt;
    STC007DataBlock pcm_block;

    //return;

    suppress_log = !((log_level&LOG_FIELD_ASSEMBLY)!=0);
    //suppress_log = false;

    // Set parameters for converter.
    lines_to_block.setInput(in_queue);
    lines_to_block.setOutput(&pcm_block);
    //lines_to_block.setLogLevel(STC007Deinterleaver::LOG_PROCESS|STC007Deinterleaver::LOG_ERROR_CORR);
    lines_to_block.setIgnoreCRC(ignore_CRC);
    lines_to_block.setForceParity(!ignore_CRC);
    lines_to_block.setPCorrection(enable_P_code);
    lines_to_block.setQCorrection(enable_Q_code);
    lines_to_block.setCWDCorrection(true);
    lines_to_block.setResMode(getDataBlockResolution(in_queue, 0));

    line_fix_cnt = word_patch_cnt = buf_ofs = 0;
    buf_size = in_queue->size();

    // Dump buffered data into converter.
    while(buf_ofs<(buf_size-STC007DataBlock::MIN_DEINT_DATA))
    {
        // Reset data block structure.
        pcm_block.clear();

        // Fill up data block, performing de-interleaving, convert lines to data blocks.
        if(lines_to_block.processBlock(buf_ofs)!=STC007Deinterleaver::DI_RET_OK)
        {
            // Exit cycle when not enough data left.
            break;
        }

        // Determine if Q-word can be fixed in current mode.
        if((enable_Q_code==false)||(pcm_block.getResolution()==STC007DataBlock::RES_16BIT))
        {
            // Q-code can not be fixed, skip checking it.
            max_fixable = STC007DataBlock::WORD_P0;
        }
        else
        {
            max_fixable = STC007DataBlock::WORD_Q0;
        }

        // Check if data block is valid and samples were fixed.
        if((pcm_block.isBlockValid()!=false)&&(pcm_block.isDataFixed()!=false))
        {
            //qInfo()<<QString::fromStdString(pcm_block.dumpContentString());
            // Cycle through all fixable words.
            for(uint8_t word_idx=STC007DataBlock::WORD_L0;word_idx<=max_fixable;word_idx++)
            {
                // Check if CRC of the word was bad.
                if(pcm_block.isWordLineCRCOk(word_idx)==false)
                {
                    // Calculate source PCM line offset in the buffer for the fixed sample.
                    line_ofs = buf_ofs+word_idx*STC007DataBlock::INTERLEAVE_OFS;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        QString log_line;
                        log_line.sprintf("[L2B-007] Block [%03u-%03u...%03u-%03u] has fixed sample #[%u] from line [%03u-%03u]",
                                         pcm_block.getStartFrame(),
                                         pcm_block.getStartLine(),
                                         pcm_block.getStopFrame(),
                                         pcm_block.getStopLine(),
                                         word_idx,
                                         (*in_queue)[line_ofs].frame_number,
                                         (*in_queue)[line_ofs].line_number);
                        qInfo()<<log_line;
                    }
#endif
                    // Check if source PCM line is not a filler one and has an invalid CRC in the first place.
                    //if(((*in_queue)[line_ofs].isCRCValidIgnoreForced()==false)&&((*in_queue)[line_ofs].getSourceCRC()!=(0xFFFF&(~STC007Line::CRC_SILENT))))
                    if(((*in_queue)[line_ofs].isCRCValidIgnoreForced()==false)
                       &&((*in_queue)[line_ofs].coords.areValid()!=false)
                       &&((*in_queue)[line_ofs].isForcedBad()==false)
                       &&((*in_queue)[line_ofs].frame_number!=frasm_f2.frame_number))
                    {
                        // Check audio sample resolution.
                        if(pcm_block.getResolution()==STC007DataBlock::RES_14BIT)
                        {
                            // Propagate back corrections for 14-bit mode.
                            // Save old word content and old CRC for comparison.
                            old_word = (*in_queue)[line_ofs].getWord(word_idx);
                            old_src_crc = (*in_queue)[line_ofs].getSourceCRC();
                            old_clc_crc = (*in_queue)[line_ofs].getCalculatedCRC();
                            // Dump PCM line before patching.
                            //qInfo()<<QString::fromStdString((*in_queue)[line_ofs].dumpContentString());
                            // Check if "fixed" word differs from the old value.
                            if(old_word!=pcm_block.getWord(word_idx))
                            {
                                // Put fixed word back into the PCM line in the queue.
                                (*in_queue)[line_ofs].setWord(word_idx, pcm_block.getWord(word_idx), (*in_queue)[line_ofs].isWordCRCOk(word_idx));
                                // Re-calculate line CRC.
                                (*in_queue)[line_ofs].calcCRC();
                                // Set word as fixed.
                                (*in_queue)[line_ofs].setFixed(word_idx);
                                // Count number of words that were fixed.
                                word_patch_cnt++;
                                // Check if PCM line CRC stayed invalid.
                                if((*in_queue)[line_ofs].isCRCValidIgnoreForced()!=false)
                                {
                                    // CRC became valid!
                                    for(uint8_t word=STC007Line::WORD_L_SH0;word<=STC007Line::WORD_CRCC_SH0;word++)
                                    {
                                        // Set all of the word CRC flags in the line as valid.
                                        (*in_queue)[line_ofs].setFixed(word);
                                    }
                                    // Count number of fixed PCM lines.
                                    line_fix_cnt++;
#ifdef DI_EN_DBG_OUT
                                    if(suppress_log==false)
                                    {
                                        QString log_line;
                                        log_line.sprintf("[L2B-007] Replaced #[%u] word [0x%04x] with [0x%04x] in PCM line, PCM line [%03u-%03u] now has valid CRC [0x%04x] (was [0x%04x])!!!",
                                                         word_idx,
                                                         old_word,
                                                         pcm_block.getWord(word_idx),
                                                         (*in_queue)[line_ofs].frame_number,
                                                         (*in_queue)[line_ofs].line_number,
                                                         (*in_queue)[line_ofs].getCalculatedCRC(),
                                                         old_clc_crc);
                                        qInfo()<<log_line;
                                    }
#endif
                                }
#ifdef DI_EN_DBG_OUT
                                else
                                {
                                    // Not all words in the line are fixed yet.
                                    if(suppress_log==false)
                                    {
                                        QString log_line;
                                        log_line.sprintf("[L2B-007] Replaced #[%u] word [0x%04x] with [0x%04x] in PCM line, bad CRC [0x%04x] remained bad as [0x%04x]",
                                                         word_idx,
                                                         old_word,
                                                         pcm_block.getWord(word_idx),
                                                         old_clc_crc,
                                                         (*in_queue)[line_ofs].getCalculatedCRC());
                                        qInfo()<<log_line;
                                    }
                                }
#endif
                            }
                            else
                            {
                                // Word stayed the same (was the same from the source).
                                // Set word as fixed.
                                (*in_queue)[line_ofs].setFixed(word_idx);
                                // Count number of words that were fixed.
                                word_patch_cnt++;
#ifdef DI_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    QString log_line;
                                    log_line.sprintf("[L2B-007] Updated #[%u] word [0x%04x] in PCM line [%03u-%03u] to be valid",
                                                     word_idx,
                                                     old_word,
                                                     (*in_queue)[line_ofs].frame_number,
                                                     (*in_queue)[line_ofs].line_number);
                                    qInfo()<<log_line;
                                }
#endif
                            }
                            // Re-check line CRC.
                            if((*in_queue)[line_ofs].isCRCValidIgnoreForced()==false)
                            {
                                // CRC stayed invalid.
                                bool all_fixed;
                                all_fixed = true;
                                // Check if all words are already fixed but CRC is still bad.
                                for(uint8_t word=STC007Line::WORD_L_SH0;word<=STC007Line::WORD_Q_SH336;word++)
                                {
                                    if((*in_queue)[line_ofs].isWordValid(word)==false)
                                    {
                                        // There are still invalid words.
                                        all_fixed = false;
                                        break;
                                    }
                                }
                                if(all_fixed!=false)
                                {
                                    // All words are fixed, but CRC is still invalid.
                                    // This must be dropout on the CRC word itself.
                                    // Re-calculate line CRC.
                                    (*in_queue)[line_ofs].calcCRC();
                                    // Replace CRC with fixed re-calculated one.
                                    (*in_queue)[line_ofs].setSourceCRC((*in_queue)[line_ofs].getCalculatedCRC());
                                    (*in_queue)[line_ofs].setFixed(STC007Line::WORD_CRCC_SH0);
                                    // Count number of fixed PCM lines.
                                    line_fix_cnt++;
#ifdef DI_EN_DBG_OUT
                                    if(suppress_log==false)
                                    {
                                        QString log_line;
                                        log_line.sprintf("[L2B-007] Replaced CRC word in PCM line, PCM line [%03u-%03u] now has valid CRC [0x%04x] (was [0x%04x])!!!",
                                                         (*in_queue)[line_ofs].frame_number,
                                                         (*in_queue)[line_ofs].line_number,
                                                         (*in_queue)[line_ofs].getSourceCRC(),
                                                         old_src_crc);
                                        qInfo()<<log_line;
                                    }
#endif
                                }
                            }
                        }
                        else
                        {
                            // Propagate back corrections for 16-bit mode.
                            uint16_t new_word, new_bitword;
                            // Save old word content and old CRC for comparison.
                            old_word = (*in_queue)[line_ofs].getWord(word_idx);
                            old_bitword = (*in_queue)[line_ofs].getWord(STC007DataBlock::WORD_Q0);
                            old_src_crc = (*in_queue)[line_ofs].getSourceCRC();
                            old_clc_crc = (*in_queue)[line_ofs].getCalculatedCRC();
                            // Split 16-bit word from data block into 14+2 bit parts.
                            new_word = pcm_block.getWord(word_idx);
                            new_bitword = new_word&STC007DataBlock::F1_S_MASK;      // 2-bit LSB part for S-word in place of Q-word.
                            new_word = (new_word>>STC007DataBlock::F1_WORD_OFS);    // 14-bit MSB part.
                            // Move LSB bits.
                            if(word_idx==STC007DataBlock::WORD_P0)
                            {
                                new_bitword = (new_bitword<<STC007DataBlock::F1_S_P0_OFS);
                                old_bitword = old_bitword&(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_P0_OFS);
                            }
                            else if(word_idx==STC007DataBlock::WORD_R2)
                            {
                                new_bitword = (new_bitword<<STC007DataBlock::F1_S_R2_OFS);
                                old_bitword = old_bitword&(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_R2_OFS);
                            }
                            else if(word_idx==STC007DataBlock::WORD_L2)
                            {
                                new_bitword = (new_bitword<<STC007DataBlock::F1_S_L2_OFS);
                                old_bitword = old_bitword&(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_L2_OFS);
                            }
                            else if(word_idx==STC007DataBlock::WORD_R1)
                            {
                                new_bitword = (new_bitword<<STC007DataBlock::F1_S_R1_OFS);
                                old_bitword = old_bitword&(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_R1_OFS);
                            }
                            else if(word_idx==STC007DataBlock::WORD_L1)
                            {
                                new_bitword = (new_bitword<<STC007DataBlock::F1_S_L1_OFS);
                                old_bitword = old_bitword&(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_L1_OFS);
                            }
                            else if(word_idx==STC007DataBlock::WORD_R0)
                            {
                                new_bitword = (new_bitword<<STC007DataBlock::F1_S_R0_OFS);
                                old_bitword = old_bitword&(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_R0_OFS);
                            }
                            else if(word_idx==STC007DataBlock::WORD_L0)
                            {
                                new_bitword = (new_bitword<<STC007DataBlock::F1_S_L0_OFS);
                                old_bitword = old_bitword&(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_L0_OFS);
                            }
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                QString log_line;
                                log_line.sprintf("[L2B-007] Old word [0x%04x]+[0x%04x], new word [0x%04x]+[0x%04x]",
                                                 old_word, old_bitword, new_word, new_bitword);
                                qInfo()<<log_line;
                            }
#endif
                            // Check if "fixed" word differs from the old value.
                            if(old_word!=new_word)
                            {
                                // Put fixed word back into the PCM line.
                                (*in_queue)[line_ofs].setWord(word_idx, new_word, (*in_queue)[line_ofs].isWordCRCOk(word_idx));
                                // Re-calculate line CRC.
                                (*in_queue)[line_ofs].calcCRC();
                                // Set word as fixed.
                                (*in_queue)[line_ofs].setFixed(word_idx);
                                // Count number of words that were fixed.
                                word_patch_cnt++;
                                // Check if PCM line CRC stayed invalid.
                                if((*in_queue)[line_ofs].isCRCValidIgnoreForced()!=false)
                                {
                                    // CRC became valid!
                                    for(uint8_t word=STC007Line::WORD_L_SH0;word<=STC007Line::WORD_CRCC_SH0;word++)
                                    {
                                        // Set all of the word CRC flags in the line as valid.
                                        (*in_queue)[line_ofs].setFixed(word);
                                    }
                                    // Count number of fixed PCM lines.
                                    line_fix_cnt++;
#ifdef DI_EN_DBG_OUT
                                    if(suppress_log==false)
                                    {
                                        QString log_line;
                                        log_line.sprintf("[L2B-007] Replaced #[%u] word [0x%04x] with [0x%04x] in PCM line, PCM line [%03u-%03u] now has valid CRC [0x%04x] (was [0x%04x])!!!",
                                                         word_idx,
                                                         old_word,
                                                         new_word,
                                                         (*in_queue)[line_ofs].frame_number,
                                                         (*in_queue)[line_ofs].line_number,
                                                         (*in_queue)[line_ofs].getCalculatedCRC(),
                                                         old_clc_crc);
                                        qInfo()<<log_line;
                                    }
#endif
                                }
#ifdef DI_EN_DBG_OUT
                                else
                                {
                                    // CRC stayed invalid.
                                    if(suppress_log==false)
                                    {
                                        QString log_line;
                                        log_line.sprintf("[L2B-007] Replaced #[%u] word [0x%04x] with [0x%04x] in PCM line, bad CRC [0x%04x] remained bad as [0x%04x]",
                                                         word_idx,
                                                         old_word,
                                                         new_word,
                                                         old_src_crc,
                                                         (*in_queue)[line_ofs].getCalculatedCRC());
                                        qInfo()<<log_line;
                                    }
                                }
#endif
                            }
                            // Check if previous correction was successfull.
                            if((*in_queue)[line_ofs].isCRCValidIgnoreForced()==false)
                            {
                                // Line is still invalid.
                                // Update "old CRC".
                                old_src_crc = (*in_queue)[line_ofs].getSourceCRC();
                                // Check if "fixed" S-word differs from the old value.
                                if(old_bitword!=new_bitword)
                                {
                                    // Get old state of S-word.
                                    old_word = old_bitword = (*in_queue)[line_ofs].getWord(STC007DataBlock::WORD_Q0);
                                    // Mask affected bits.
                                    if(word_idx==STC007DataBlock::WORD_P0)
                                    {
                                        old_bitword = old_bitword&(~(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_P0_OFS));
                                    }
                                    else if(word_idx==STC007DataBlock::WORD_R2)
                                    {
                                        old_bitword = old_bitword&(~(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_R2_OFS));
                                    }
                                    else if(word_idx==STC007DataBlock::WORD_L2)
                                    {
                                        old_bitword = old_bitword&(~(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_L2_OFS));
                                    }
                                    else if(word_idx==STC007DataBlock::WORD_R1)
                                    {
                                        old_bitword = old_bitword&(~(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_R1_OFS));
                                    }
                                    else if(word_idx==STC007DataBlock::WORD_L1)
                                    {
                                        old_bitword = old_bitword&(~(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_L1_OFS));
                                    }
                                    else if(word_idx==STC007DataBlock::WORD_R0)
                                    {
                                        old_bitword = old_bitword&(~(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_R0_OFS));
                                    }
                                    else if(word_idx==STC007DataBlock::WORD_L0)
                                    {
                                        old_bitword = old_bitword&(~(STC007DataBlock::F1_S_MASK<<STC007DataBlock::F1_S_L0_OFS));
                                    }
                                    // Put fixed word back into the PCM line.
                                    (*in_queue)[line_ofs].setWord(STC007DataBlock::WORD_Q0, (old_bitword|new_bitword), (*in_queue)[line_ofs].isWordCRCOk(STC007DataBlock::WORD_Q0));
                                    // Re-calculate line CRC.
                                    (*in_queue)[line_ofs].calcCRC();
                                    // Check if PCM line CRC stayed invalid.
                                    if((*in_queue)[line_ofs].isCRCValidIgnoreForced()!=false)
                                    {
                                        // CRC became valid!
                                        for(uint8_t word=STC007Line::WORD_L_SH0;word<=STC007Line::WORD_CRCC_SH0;word++)
                                        {
                                            // Set all of the word CRC flags in the line as valid.
                                            (*in_queue)[line_ofs].setFixed(word);
                                        }
                                        // Count number of fixed PCM lines.
                                        line_fix_cnt++;
#ifdef DI_EN_DBG_OUT
                                        if(suppress_log==false)
                                        {
                                            QString log_line;
                                            log_line.sprintf("[L2B-007] Replaced #[%u] word [0x%04x] with [0x%04x] in PCM line, PCM line [%03u-%03u] now has valid CRC [0x%04x] (was [0x%04x])!!!",
                                                             STC007DataBlock::WORD_Q0,
                                                             old_word,
                                                             (*in_queue)[line_ofs].getWord(STC007DataBlock::WORD_Q0),
                                                             (*in_queue)[line_ofs].frame_number,
                                                             (*in_queue)[line_ofs].line_number,
                                                             (*in_queue)[line_ofs].getCalculatedCRC(),
                                                             old_src_crc);
                                            qInfo()<<log_line;
                                        }
#endif
                                    }
#ifdef DI_EN_DBG_OUT
                                    else
                                    {
                                        // CRC stayed invalid.
                                        if(suppress_log==false)
                                        {
                                            QString log_line;
                                            log_line.sprintf("[L2B-007] Replaced #[%u] word [0x%04x] with [0x%04x] in PCM line, bad CRC [0x%04x] remained bad as [0x%04x]",
                                                             STC007DataBlock::WORD_Q0,
                                                             old_word,
                                                             (*in_queue)[line_ofs].getWord(STC007DataBlock::WORD_Q0),
                                                             old_src_crc,
                                                             (*in_queue)[line_ofs].getCalculatedCRC());
                                            qInfo()<<log_line;
                                        }
                                    }
#endif
                                }
                            }
                        }
                    }
                    else
                    {
                        if((*in_queue)[line_ofs].isCRCValid()!=false)
                        {
                            if(pcm_block.getResolution()==STC007DataBlock::RES_14BIT)
                            {
                                old_word = (*in_queue)[line_ofs].getWord(word_idx);
                                if(old_word!=pcm_block.getWord(word_idx))
                                {
                                    (*in_queue)[line_ofs].setForcedBad();
#ifdef DI_EN_DBG_OUT
                                    if(suppress_log==false)
                                    {
                                        QString log_line;
                                        log_line.sprintf("[L2B-007] Valid word mismatch [0x%04x]<>[0x%04x] in PCM line [%04u:%03u]!",
                                                         old_word,
                                                         (*in_queue)[line_ofs].getWord(word_idx),
                                                         (*in_queue)[line_ofs].frame_number,
                                                         (*in_queue)[line_ofs].line_number);
                                        qInfo()<<log_line;
                                    }
#endif
                                    //qWarning()<<DBG_ANCHOR<<"[L2B-007] CWD propagation valid word mismatch!";
                                }
                            }
                        }
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            if((*in_queue)[line_ofs].isForcedBad()!=false)
                            {
                                qInfo()<<"[L2B-007] PCM line is forced to be BAD, skipping";
                            }
                            else if((*in_queue)[line_ofs].isCRCValidIgnoreForced()!=false)
                            {
                                qInfo()<<"[L2B-007] PCM line has good CRC and doesn't need to be fixed";
                            }
                            else if((*in_queue)[line_ofs].frame_number==frasm_f2.frame_number)
                            {
                                qInfo()<<"[L2B-007] PCM line is from the next frame, that line will be re-filled, skipping";
                            }
                            else
                            {
                                qInfo()<<"[L2B-007] PCM line is filler, that should not be fixed";
                            }
                        }
#endif
                    }
                    // Dump PCM line after patching.
                    //qInfo()<<QString::fromStdString((*in_queue)[line_ofs].dumpContentString());
                    //qInfo()<<"-----------------------------------------------";
                }
            }
        }

        // Go to the next line in the buffer.
        buf_ofs++;
    }

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        if(word_patch_cnt>0)
        {
            qInfo()<<"[L2B-007] Words fixed:"<<word_patch_cnt;
        }
        if(line_fix_cnt>0)
        {
            qInfo()<<"[L2B-007] PCM lines fixed:"<<line_fix_cnt;
        }
    }
#endif

    return line_fix_cnt;
}

//------------------------ Perform predictive BROKEN block detection and Cross-Word Decoding.
void STC007DataStitcher::prescanFrame()
{
    bool suppress_log, next_frame_add;
    uint16_t runs, fix_count, fix_per_run;

    next_frame_add = false;
    suppress_log = ((log_level&LOG_FIELD_ASSEMBLY)==0);

    // Detect BROKEN data blocks and trace down false valid PCM lines.
    //patchBrokenLines(&conv_queue);

    if(enable_CWD!=false)
    {
        // Temporary fill a field from the next frame to complete bottom data blocks.
        next_frame_add = fillNextFieldForCWD();
        fix_count = runs = 0;
        do
        {
            // "Repair" some PCM lines using Cross-Word Decoding.
            fix_per_run = performCWD(&conv_queue);
            if(fix_per_run>0) runs++;
            fix_count += fix_per_run;
        }
        while(fix_per_run!=0);
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(fix_count>0)
            {
                qInfo()<<"[L2B-007] CWD fixed"<<fix_count<<"lines in"<<runs<<"iterations";
            }
        }
#endif
        if(next_frame_add!=false)
        {
            // Remove temporary lines.
            removeNextFieldAfterCWD();
        }

#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        //if(fix_count>0)
        {
            qInfo()<<"[L2B-007] CWD queue:";
            for(uint16_t i=0;i<conv_queue.size();i++)
            {
                qInfo()<<QString::fromStdString(conv_queue[i].dumpContentString());
            }
        }
#endif
    }
}

//------------------------ Set data block sample rate.
void STC007DataStitcher::setBlockSampleRate(STC007DataBlock *in_block)
{
    // Check if sample rate is preset.
    if((preset_sample_rate==PCMSamplePair::SAMPLE_RATE_44100)||(preset_sample_rate==PCMSamplePair::SAMPLE_RATE_44056))
    {
        // Apply preset sample rate.
        in_block->sample_rate = preset_sample_rate;
    }
    else
    {
        // Choose sample rate from video standard.
        if(frasm_f1.video_standard==FrameAsmDescriptor::VID_PAL)
        {
            in_block->sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
        }
        else if(frasm_f1.video_standard==FrameAsmDescriptor::VID_NTSC)
        {
            in_block->sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
        }
        else
        {
            // Video standard is not set, set the default one.
            in_block->sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
        }
    }
}

//------------------------ Output service tag "New file started".
void STC007DataStitcher::outputFileStart()
{
    size_t queue_size;
    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Empty pointer provided, service tag discarded!";
    }
    else
    {
        PCMSamplePair service_pair;
        service_pair.setServNewFile(file_name);
        while(1)
        {
            // Try to put service tag into the queue.
            mtx_samples->lock();
            queue_size = (out_samples->size()+1);
            if(queue_size<(MAX_SAMPLEPAIR_QUEUE_SIZE-1))
            {
                // Put service pair in the output queue.
                out_samples->push_back(service_pair);
                mtx_samples->unlock();
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-007] Service tag 'NEW FILE' written.";
                }
#endif
                break;
            }
            else
            {
                mtx_samples->unlock();
                QThread::msleep(20);
            }
        }
        file_time.start();
    }
}

//------------------------ Output one sample pair from the data block into output queue.
void STC007DataStitcher::outputSamplePair(STC007DataBlock *in_block, uint8_t idx_left, uint8_t idx_right)
{
    int16_t sample_left, sample_right;
    bool block_state, word_left_state, word_right_state;
    bool word_left_fixed, word_right_fixed;
    PCMSamplePair sample_pair;
    // Set emphasis state of the samples.
    sample_pair.setEmphasis(in_block->hasEmphasis());
    // Set sample rate.
    sample_pair.setSampleRate(in_block->sample_rate);
    if(in_block->isDataBroken()==false)
    {
        // Set validity of the whole block and the samples.
        block_state = in_block->isBlockValid();
        if(block_state==false)
        {
            word_left_fixed = word_right_fixed = false;
        }
        else
        {
            word_left_fixed = in_block->isWordLineCRCOk(idx_left);
            word_right_fixed = in_block->isWordLineCRCOk(idx_right);
        }
        word_left_state = in_block->isWordValid(idx_left);
        word_right_state = in_block->isWordValid(idx_right);
    }
    else
    {
        // Data block deemed to be "BROKEN", no data can be taken as valid.
        block_state = false;
        word_left_state = word_right_state = false;
        word_left_fixed = word_right_fixed = false;
    }
    // Set data to [PCMSamplePair] object.
    sample_left = in_block->getSample(idx_left);
    sample_right = in_block->getSample(idx_right);
    // Copy samples into the [PCMSamplePair] object.
    sample_pair.setSamplePair(sample_left, sample_right,
                              block_state, block_state,
                              word_left_state, word_right_state,
                              word_left_fixed, word_right_fixed);
    // Put sample pair in the output queue.
    // [mtx_samples] should be locked already!
    out_samples->push_back(sample_pair);
}

//------------------------ Output PCM data block into output queue (blocking).
void STC007DataStitcher::outputDataBlock(STC007DataBlock *in_block)
{
    size_t queue_size;
    bool size_lock;
    size_lock = false;
    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Empty pointer provided, result discarded!";
    }
    else
    {
        while(1)
        {
            // Try to put processed data block into the queue.
            mtx_samples->lock();
            queue_size = (out_samples->size()+1);
            if(queue_size<(MAX_SAMPLEPAIR_QUEUE_SIZE-3))
            {
                // Output L0+R0 samples.
                outputSamplePair(in_block, STC007DataBlock::WORD_L0, STC007DataBlock::WORD_R0);
                // Output L1+R1 samples.
                outputSamplePair(in_block, STC007DataBlock::WORD_L1, STC007DataBlock::WORD_R1);
                // Output L2+R2 samples.
                outputSamplePair(in_block, STC007DataBlock::WORD_L2, STC007DataBlock::WORD_R2);
                mtx_samples->unlock();

                if(size_lock!=false)
                {
                    size_lock = false;
#ifdef DI_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        qInfo()<<"[L2B-007] Output PCM data blocks queue has some space, continuing...";
                    }
#endif
                }
                break;
            }
            else
            {
                mtx_samples->unlock();
                if(size_lock==false)
                {
                    size_lock = true;
#ifdef DI_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        qInfo()<<"[L2B-007] Output queue is at size limit ("<<(MAX_SAMPLEPAIR_QUEUE_SIZE-3)<<"), waiting...";
                    }
#endif
                }
                QThread::msleep(100);
            }
        }
        // Output data block for visualization.
        emit newBlockProcessed(*in_block);
    }
}

//------------------------ Output service tag "File ended".
void STC007DataStitcher::outputFileStop()
{
    size_t queue_size;
    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Empty pointer provided, service tag discarded!";
    }
    else
    {
        PCMSamplePair service_pair;
        service_pair.setServEndFile();
        while(1)
        {
            // Try to put service tag into the queue.
            mtx_samples->lock();
            queue_size = (out_samples->size()+1);
            if(queue_size<(MAX_SAMPLEPAIR_QUEUE_SIZE-1))
            {
                // Put service pair in the output queue.
                out_samples->push_back(service_pair);
                mtx_samples->unlock();
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-007] Service tag 'FILE END' written.";
                }
#endif
                break;
            }
            else
            {
                mtx_samples->unlock();
                QThread::msleep(20);
            }
        }
        // Reset file name.
        file_name.clear();
        // Report time that file processing took.
        qDebug()<<"[L2B-007] File processed in"<<file_time.elapsed()<<"msec";
    }
}

//------------------------ Perform deinterleave of a frame in [conv_queue].
void STC007DataStitcher::performDeinterleave()
{
    bool already_unsafe;
    std::deque<STC007Line>::iterator buf_scaner;
    STC007DataBlock pcm_block;

    // Set parameters for converter.
    lines_to_block.setInput(&conv_queue);
    lines_to_block.setOutput(&pcm_block);
    lines_to_block.setIgnoreCRC(ignore_CRC);
    lines_to_block.setForceParity(!ignore_CRC);
    lines_to_block.setPCorrection(enable_P_code);
    lines_to_block.setQCorrection(enable_Q_code);
    lines_to_block.setCWDCorrection(enable_CWD);

    // Dump the whole line buffer out (for visualization).
    buf_scaner = conv_queue.begin();
    while(buf_scaner!=conv_queue.end())
    {
        if(((*buf_scaner).frame_number==frasm_f1.frame_number)||((*buf_scaner).frame_number==frasm_f2.frame_number))
        {
            emit newLineProcessed((*buf_scaner));
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                qInfo()<<QString::fromStdString((*buf_scaner).dumpContentString());
            }
#endif
        }
        buf_scaner++;
    }

    // Dump buffered data (one frame) into converter.
    while(conv_queue.size()>STC007DataBlock::MIN_DEINT_DATA)
    {
        // Reset data block structure.
        pcm_block.clear();
        already_unsafe = false;
        // Set parameters for processing.
        lines_to_block.setResMode(getDataBlockResolution(&conv_queue, 0));

        // Fill up data block, performing de-interleaving, convert lines to data blocks.
        lines_to_block.processBlock(0);
        // TODO: determine emphasis for the data block
        //pcm_block.setEmphasis();
        // Set sample rate for data block.
        setBlockSampleRate(&pcm_block);
        // Set sample format.
        pcm_block.setM2Format(mode_m2);

        // Remove first line from the input queue.
        conv_queue.pop_front();

        // Check if data is not pure silence.
        if(pcm_block.isSilent()==false)
        {
            // Check if invalid seam masking is enabled.
            if(mask_seams!=false)
            {
                // Check for field seams.
                if((frasm_f1.inner_padding_ok==false)&&(frasm_f1.inner_silence==false))
                {
                    // Interframe padding was not detected correctly (and not due to silence).
                    // Need to disable error-correction for in-frame field seam, so errors will get masked (interpolated or muted).
                    if((pcm_block.isOnSeam()!=false)&&
                        (pcm_block.getStartFrame()==frasm_f1.frame_number)&&(pcm_block.getStartFrame()==pcm_block.getStopFrame()))
                    {
                        // Current PCM data block was assembled on in-frame field seam.
                        pcm_block.markAsUnsafe();
                        already_unsafe = true;
#ifdef DI_EN_DBG_OUT
                        if(((log_level&LOG_UNSAFE)!=0)&&((log_level&LOG_FIELD_ASSEMBLY)!=0))
                        {
                            QString log_line;
                            log_line.sprintf("[L2B-007] Invalidating block at [%03u/%03u...%03u/%03u] due to bad interframe seam",
                                     pcm_block.getStartFrame(), pcm_block.getStartLine(), pcm_block.getStopFrame(), pcm_block.getStopLine());
                            qInfo()<<log_line;
                        }
#endif
                    }
                }
                if((frasm_f0.outer_padding_ok==false)&&(frasm_f0.outer_silence==false))
                {
                    // Frame-to-frame padding was not detected correctly (and not due to silence).
                    // Need to disable error-correction for inter-frame field seam, so errors will get masked (interpolated or muted).
                    if((pcm_block.getStartFrame()!=pcm_block.getStopFrame())&&
                        (pcm_block.getStartFrame()==frasm_f0.frame_number)&&(pcm_block.getStopFrame()==frasm_f1.frame_number))
                    {
                        // Current PCM data block was assembled on inter-frame field seam.
                        pcm_block.markAsUnsafe();
                        already_unsafe = true;
#ifdef DI_EN_DBG_OUT
                        if(((log_level&LOG_UNSAFE)!=0)&&((log_level&LOG_FIELD_ASSEMBLY)!=0))
                        {
                            QString log_line;
                            log_line.sprintf("[L2B-007] Invalidating block at [%03u/%03u...%03u/%03u] due to bad frame-to-frame seam",
                                     pcm_block.getStartFrame(), pcm_block.getStartLine(), pcm_block.getStopFrame(), pcm_block.getStopLine());
                            qInfo()<<log_line;
                        }
#endif
                    }
                }
            }

            // Check if data block already marked as unsafe.
            if(already_unsafe==false)
            {
                // Check if broken data blocks masking is enabled.
                if((broken_mask_dur>0)&&(broken_countdown==0))
                {
                    // Check for random "BROKEN" data blocks.
                    if(pcm_block.isDataBroken()!=false)
                    {
                        // If broken data block was found - disable P and Q corrections on next lines
                        // to prevent wrong "corrections" resulting in clicks and pops.
                        // Reset error-correction prohibition timer.
                        broken_countdown = broken_mask_dur;
#ifdef DI_EN_DBG_OUT
                        if((log_level&LOG_UNSAFE)!=0)
                        {
                            QString log_line;
                            log_line.sprintf("[L2B-007] Broken block detected at [%03u/%03u...%03u/%03u], disabling error correction for [%u] next lines",
                                 pcm_block.getStartFrame(), pcm_block.getStartLine(), pcm_block.getStopFrame(), pcm_block.getStopLine(), broken_countdown);
                            qInfo()<<log_line;
                        }
#endif
                    }
                }
                // Check for active countdown from random "BROKEN" block.
                if(broken_countdown!=0)
                {
                    // Mark trailing data blocks as unsafe to use error-correction on after "BROKEN" block.
                    pcm_block.markAsUnsafe();
#ifdef DI_EN_DBG_OUT
                    if((log_level&LOG_UNSAFE)!=0)
                    {
                        QString log_line;
                        log_line.sprintf("[L2B-007] Marking block at [%03u...%03u] as 'unsafe' due to broken block above",
                                 pcm_block.getStartLine(), pcm_block.getStopLine());
                        qInfo()<<log_line;
                    }
#endif
                }
            }
        }

        // Check if stats for the block should be reported.
        if(isBlockNoReport(&pcm_block)==false)
        {
            // Report damages and corrections.
            if(pcm_block.isBlockValid()!=false)
            {
                // Data block is valid.
                if(pcm_block.isDataFixedByP()!=false)
                {
                    // Data is fixed by P-correction.
                    frasm_f1.blocks_fix_p++;
                }
                else if(pcm_block.isDataFixedByQ()!=false)
                {
                    // Data is fixed by Q-correction.
                    frasm_f1.blocks_fix_q++;
                }
                if(pcm_block.isDataFixedByCWD()!=false)
                {
                    // Data block is fixed with help of CWD-correction.
                    frasm_f1.blocks_fix_cwd++;
                }
            }
            else
            {
                // Data block is corrupted.
                frasm_f1.blocks_drop++;
                // Samples in data block are corrupted.
                frasm_f1.samples_drop += pcm_block.getErrorsAudioFixed();
                if(pcm_block.isDataBroken()!=false)
                {
                    // Data is BROKEN.
                    frasm_f1.blocks_broken_field++;
                }
            }
        }

        // Countdown for trailing invalid data blocks after "BROKEN" one.
        if(broken_countdown>0)
        {
            broken_countdown--;
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_UNSAFE)!=0)
            {
                if(broken_countdown==0)
                {
                    qInfo()<<"[L2B-007] Broken block countdown ended.";
                }
            }
#endif
        }

#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_BLOCK_DUMP)!=0)
           ||(((log_level&LOG_UNSAFE)!=0)&&(broken_countdown>0)))
        {
            qInfo()<<QString::fromStdString("[L2B-007] "+pcm_block.dumpContentString());
        }
#endif
        // Add compiled data block into output queue.
        outputDataBlock(&pcm_block);
    }
}

//------------------------ Set logging level (using [STC007Deinterleaver] defines).
void STC007DataStitcher::setLogLevel(uint16_t new_log)
{
    log_level = new_log;
}

//------------------------ Preset video standard for stitching.
void STC007DataStitcher::setVideoStandard(uint8_t in_standard)
{
#ifdef DI_EN_DBG_OUT
    if(preset_video_mode!=in_standard)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_standard==FrameAsmDescriptor::VID_NTSC)
            {
                qInfo()<<"[L2B-007] Video standard set to: NTSC.";
            }
            else if(in_standard==FrameAsmDescriptor::VID_PAL)
            {
                qInfo()<<"[L2B-007] Video standard set to: PAL.";
            }
            else if(in_standard==FrameAsmDescriptor::VID_UNKNOWN)
            {
                qInfo()<<"[L2B-007] Automatic video standard detection enabled.";
            }
            else
            {
                qInfo()<<"[L2B-007] Unknown video standard provided, ignored!";
            }
        }
    }
#endif
    if(in_standard<FrameAsmDescriptor::VID_MAX)
    {
        preset_video_mode = in_standard;
    }
}

//------------------------ Preset field order.
void STC007DataStitcher::setFieldOrder(uint8_t in_order)
{
#ifdef DI_EN_DBG_OUT
    if(preset_field_order!=in_order)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_order==FrameAsmDescriptor::ORDER_TFF)
            {
                qInfo()<<"[L2B-007] Field order preset to 'TFF'.";
            }
            else if(in_order==FrameAsmDescriptor::ORDER_BFF)
            {
                qInfo()<<"[L2B-007] Field order preset to 'BFF'.";
            }
            else if(in_order==FrameAsmDescriptor::ORDER_UNK)
            {
                qInfo()<<"[L2B-007] Field order set to 'automatic detection'.";
            }
            else
            {
                qInfo()<<"[L2B-007] Unknown field order provided, ignored!";
            }
        }
    }
#endif
    if(in_order<FrameAsmDescriptor::ORDER_MAX)
    {
        preset_field_order = in_order;
    }
}

//------------------------ Enable/disable P-code error correction.
void STC007DataStitcher::setPCorrection(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(enable_P_code!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-007] P-code ECC set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-007] P-code ECC set to 'enabled'.";
            }
        }
    }
#endif
    enable_P_code = in_set;
}

//------------------------ Enable/disable Q-code error correction.
void STC007DataStitcher::setQCorrection(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(enable_Q_code!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-007] Q-code ECC set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-007] Q-code ECC set to 'enabled'.";
            }
        }
    }
#endif
    enable_Q_code = in_set;
}

//------------------------ Enable/disable CWD error correction.
void STC007DataStitcher::setCWDCorrection(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(enable_CWD!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-007] Cross-Word Decoding set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-007] Cross-Word Decoding set to 'enabled'.";
            }
        }
    }
#endif
    enable_CWD = in_set;
}

//------------------------ Enable/disable M2 sample format (13/16-bits in 14-bit words).
void STC007DataStitcher::setM2SampleFormat(bool in_m2)
{
#ifdef DI_EN_DBG_OUT
    if(mode_m2!=in_m2)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_m2==false)
            {
                qInfo()<<"[L2B-007] M2 sample format set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-007] M2 sample format set to 'enabled'.";
            }
        }
    }
#endif
    mode_m2 = in_m2;
}

//------------------------ Preset audio resolution.
void STC007DataStitcher::setResolutionPreset(uint8_t in_res)
{
#ifdef DI_EN_DBG_OUT
    if(preset_audio_res!=in_res)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_res==SAMPLE_RES_14BIT)
            {
                qInfo()<<"[L2B-007] Resolution preset to '14-bit'.";
            }
            else if(in_res==SAMPLE_RES_16BIT)
            {
                qInfo()<<"[L2B-007] Resolution preset to '16-bit'.";
            }
            else if(in_res==SAMPLE_RES_UNKNOWN)
            {
                qInfo()<<"[L2B-007] Resolution set to 'automatic detection'.";
            }
            else
            {
                qInfo()<<"[L2B-007] Unknown audio resolution provided, ignored!";
            }
        }
    }
#endif
    if(in_res<SAMPLE_RES_MAX)
    {
        preset_audio_res = in_res;
    }
}

//------------------------ Preset audio sample rate.
void STC007DataStitcher::setSampleRatePreset(uint16_t in_srate)
{
#ifdef DI_EN_DBG_OUT
    if(preset_sample_rate!=in_srate)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_srate==PCMSamplePair::SAMPLE_RATE_44056)
            {
                qInfo()<<"[L2B-007] Sample rate preset to '44056 Hz'.";
            }
            else if(in_srate==PCMSamplePair::SAMPLE_RATE_44100)
            {
                qInfo()<<"[L2B-007] Sample rate preset to '44100 Hz'.";
            }
            else if(in_srate==PCMSamplePair::SAMPLE_RATE_AUTO)
            {
                qInfo()<<"[L2B-007] Sample rate set to 'automatic detection'.";
            }
            else
            {
                qInfo()<<"[L2B-007] Unknown sample rate provided!";
            }
        }
    }
#endif
    preset_sample_rate = in_srate;
}

//------------------------ Set fine settings: maximum unchecked data blocks for a seam in 14-bit mode.
void STC007DataStitcher::setFineMaxUnch14(uint8_t in_set)
{
#ifdef DI_EN_DBG_OUT
    if(max_unchecked_14b_blocks!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            qInfo()<<"[L2B-007] Maximum unchecked 14-bit data blocks per seam set to:"<<in_set;
        }
    }
#endif
    max_unchecked_14b_blocks = in_set;
    emit guiUpdFineMaxUnch14(max_unchecked_14b_blocks);
}

//------------------------ Set fine settings: maximum unchecked data blocks for a seam in 16-bit mode.
void STC007DataStitcher::setFineMaxUnch16(uint8_t in_set)
{
#ifdef DI_EN_DBG_OUT
    if(max_unchecked_16b_blocks!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            qInfo()<<"[L2B-007] Maximum unchecked 16-bit data blocks per seam set to:"<<in_set;
        }
    }
#endif
    max_unchecked_16b_blocks = in_set;
    emit guiUpdFineMaxUnch16(max_unchecked_16b_blocks);
}

//------------------------ Set fine settings: usage of ECC on CRC-marked words.
void STC007DataStitcher::setFineUseECC(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(ignore_CRC==in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-007] CRC usage set to 'enabled'.";
            }
            else
            {
                qInfo()<<"[L2B-007] CRC usage set to 'disabled'.";
            }
        }
    }
#endif
    ignore_CRC = !in_set;
    emit guiUpdFineUseECC(!ignore_CRC);
}

//------------------------ Set fine settings: usage of unchecked seams masking.
void STC007DataStitcher::setFineMaskSeams(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(mask_seams!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-007] Masking unchecked field seams set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-007] Masking unchecked field seams set to 'enabled'.";
            }
        }
    }
#endif
    mask_seams = in_set;
    emit guiUpdFineMaskSeams(mask_seams);
}

//------------------------ Set fine settings: number of lines to mask after BROKEN data block.
void STC007DataStitcher::setFineBrokeMask(uint8_t in_set)
{
#ifdef DI_EN_DBG_OUT
    if(broken_mask_dur!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            qInfo()<<"[L2B-007] ECC-disabled lines count after BROKEN data block set to:"<<in_set;
        }
    }
#endif
    broken_mask_dur = in_set;
    emit guiUpdFineBrokeMask(broken_mask_dur);
}

//------------------------ Set fine settings: allow automatic top-line addition.
void STC007DataStitcher::setFineTopLineFix(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(fix_cut_above!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-007] Line insertion from the top of the frames set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-007] Line insertion from the top of the frames set to 'enabled'.";
            }
        }
    }
#endif
    fix_cut_above = in_set;
    emit guiUpdFineTopLineFix(fix_cut_above);
}

//------------------------ Set fine settings to defaults.
void STC007DataStitcher::setDefaultFineSettings()
{
    setFineMaxUnch14(MAX_BURST_UNCH_14BIT);
    setFineMaxUnch16(MAX_BURST_UNCH_16BIT);
    setFineUseECC(true);
    setFineTopLineFix(false);
    setFineMaskSeams(true);
    setFineBrokeMask(UNCH_MASK_DURATION);
}

//------------------------ Get current fine settings.
void STC007DataStitcher::requestCurrentFineSettings()
{
    emit guiUpdFineMaxUnch14(max_unchecked_14b_blocks);
    emit guiUpdFineMaxUnch16(max_unchecked_16b_blocks);
    emit guiUpdFineUseECC(!ignore_CRC);
    emit guiUpdFineTopLineFix(fix_cut_above);
    emit guiUpdFineMaskSeams(mask_seams);
    emit guiUpdFineBrokeMask(broken_mask_dur);
}

//------------------------ Main processing loop.
void STC007DataStitcher::doFrameReassemble()
{
    uint16_t lines_per_frame;
    quint64 time_spent;
    size_t queue_size;
    STC007Line cur_line;

    broken_countdown = 0;
    frasm_f0.video_standard = frasm_f1.video_standard = frasm_f2.video_standard = FrameAsmDescriptor::VID_UNKNOWN;

#ifdef DI_EN_DBG_OUT
    qInfo()<<"[L2B-007] Launched, STC-007 thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
#endif
    // Check working pointers.
    if((in_lines==NULL)||(mtx_lines==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-007] Empty input pointer provided, unable to continue!";
        emit finished();
        return;
    }

    QElapsedTimer time_per_frame;
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
        mtx_lines->lock();
        // Get input queue size.
        queue_size = in_lines->size();
        // Check if there is anything in the input queue and there is place for data.
        if(queue_size>0)
        {
            // Wait for sufficient data in the input queue and detect frame numbers.
            if(waitForTwoFrames()!=false)
            {
                // Two full frames are available.
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-007] -------------------- Detected two frames of data, switching to processing...";
                }
#endif
                time_per_frame.start();

                //log_level |= LOG_UNSAFE;

                // Fast fill two frames into internal buffer.
                fillUntilTwoFrames();

                lines_per_frame = 0;

                // Remove lines from Frame A from input queue.
                cur_line = in_lines->front();
                while(1)
                {
                    // Check input queue size.
                    if(in_lines->size()>0)
                    {
                        // Get first line.
                        cur_line = in_lines->front();
                        // Check frame number.
                        if(cur_line.frame_number<=frasm_f1.frame_number)
                        {
                            // Line from Frame A.
                            lines_per_frame++;
                            // Remove line from the input.
                            in_lines->pop_front();
                        }
                        else
                        {
                            // No more lines from Frame A.
                            break;
                        }
                    }
                    else
                    {
                        // No more lines available.
                        break;
                    }
                }
                // Unlock shared access.
                mtx_lines->unlock();

#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-007] Removed"<<lines_per_frame<<"lines from input queue from frame"<<frasm_f1.frame_number;
                }
#endif
                // Set deinterleaver logging parameters.
                uint8_t ext_di_log_lvl = 0;
                if((log_level&LOG_SETTINGS)!=0)
                {
                    ext_di_log_lvl |= STC007Deinterleaver::LOG_SETTINGS;
                }
                if((log_level&LOG_DEINTERLEAVE)!=0)
                {
                    ext_di_log_lvl |= STC007Deinterleaver::LOG_PROCESS;
                }
                if((log_level&LOG_ERROR_CORR)!=0)
                {
                    ext_di_log_lvl |= STC007Deinterleaver::LOG_ERROR_CORR;
                }
                lines_to_block.setLogLevel(ext_di_log_lvl);

                // Detect trimming for two frames in the buffer and start/end file marks.
                findFramesTrim();

                if(file_start!=false)
                {
                    // Reset internal state.
                    resetState();
                }

                // Lock shared access.
                mtx_lines->lock();
                // Get input queue size.
                queue_size = in_lines->size();
                // Check if there is anything in the input queue and there is place for data.
                if(queue_size>0)
                {
                    // Check if file ended.
                    if(file_end!=false)
                    {
                        lines_per_frame = 0;
                        cur_line = in_lines->front();
                        // Remove lines from dummy Frame B from input queue.
                        while(cur_line.frame_number==frasm_f2.frame_number)
                        {
                            in_lines->pop_front();
                            lines_per_frame++;
                            if(in_lines->size()==0)
                            {
                                break;
                            }
                        }
#ifdef DI_EN_DBG_OUT
                        if((log_level&LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[L2B-007] Removed"<<lines_per_frame<<"lines from input queue from dummy frame"<<frasm_f2.frame_number;
                        }
#endif
                    }
                }
                // Unlock shared access.
                mtx_lines->unlock();

                // Split frame buffer into 4x field buffers.
                splitFramesToFields();

                //QElapsedTimer dbg_timer;
                //dbg_timer.start();

                // Find a way to stitch fields together.
                findFieldStitching();

                // Inform processing chain about new file.
                if(file_start!=false)
                {
                    outputFileStart();
                }

                // Fill up PCM line queue from internal field buffers.
                fillFrameForOutput();

                // Pre-scan frame buffer, perform false-positive PCM line detection and CWD.
                prescanFrame();

                lines_to_block.setLogLevel(ext_di_log_lvl);
                // Perform deinterleaving on one frame.
                performDeinterleave();

                // Report frame assembling parameters.
                emit guiUpdFrameAsm(frasm_f1);

                // Move trim data from Frame B to Frame A.
                frasm_f0 = frasm_f1;
                frasm_f1 = frasm_f2;
                // Reset flags in Frame B.
                frasm_f2.trim_ok = false;
                frasm_f2.inner_padding_ok = false;
                frasm_f2.outer_padding_ok = false;

                // Report time that frame processing took.
                time_spent = time_per_frame.nsecsElapsed();
                time_spent = time_spent/1000;
                emit loopTime(time_spent);

                // Inform processing chain about end of file.
                if(file_end!=false)
                {
                    // Inform processing chain about end of file.
                    outputFileStop();
                    // Reset internal state.
                    resetState();
                }

                // Reset file start/end flags.
                file_start = file_end = false;
            }
            else
            {
                // Not enough data in the input queue.
                // Unlock shared access.
                mtx_lines->unlock();
                // Wait (~a frame) for input to fill up.
                QThread::msleep(25);
            }
        }
        else
        {
            // Input queue is empty.
            // Unlock shared access.
            mtx_lines->unlock();
            // Wait (~a frame) for input to fill up.
            QThread::msleep(50);
        }
    }
    resetState();
    qInfo()<<"[L2B-007] Loop stop.";
    emit finished();
}

//------------------------ Set the flag to break execution loop and exit.
void STC007DataStitcher::stop()
{
#ifdef DI_EN_DBG_OUT
    qInfo()<<"[L2B-007] Received termination request";
#endif
    finish_work = true;
}
