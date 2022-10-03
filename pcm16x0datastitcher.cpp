#include "pcm16x0datastitcher.h"

PCM16X0DataStitcher::PCM16X0DataStitcher(QObject *parent) : QObject(parent)
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

    file_name.clear();

    preset_field_order = FrameAsmDescriptor::ORDER_TFF;
    preset_format = PCM16X0Deinterleaver::FORMAT_SI;

    log_level = 0;
    trim_fill = 0;
    file_start = file_end = format_changed = false;

    enable_P_code = true;
    finish_work = false;

    // Reset internal state.
    resetState();
    // Preset default fine parameters.
    setDefaultFineSettings();
}

//------------------------ Set pointers to shared input data.
void PCM16X0DataStitcher::setInputPointers(std::deque<PCM16X0SubLine> *in_pcmline, QMutex *mtx_pcmline)
{
    if((in_pcmline==NULL)||(mtx_pcmline==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Empty input pointer provided in [PCM16X0DataStitcher::setInputPointers()], unable to apply!";
    }
    else
    {
        in_lines = in_pcmline;
        mtx_lines = mtx_pcmline;
    }
}

//------------------------ Set pointers to shared output data.
void PCM16X0DataStitcher::setOutputPointers(std::deque<PCMSamplePair> *out_pcmsamples, QMutex *mtx_pcmsamples)
{
    if((out_pcmsamples==NULL)||(mtx_pcmsamples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Empty output pointer provided in [PCM16X0DataStitcher::setOutputPointers()], unable to apply!";
    }
    else
    {
        out_samples = out_pcmsamples;
        mtx_samples = mtx_pcmsamples;
    }
}

//------------------------ Reset all stats from the last file, start from scratch.
void PCM16X0DataStitcher::resetState()
{
    // Reset Control Bit history.
    clearCtrlBitStats();
    // Reset good padding history.
    clearPadStats();
    // Clear queues.
    padding_queue.clear();
    conv_queue.clear();
    // Set default assembling parameters.
    f1_srate = PCMSamplePair::SAMPLE_RATE_44056;
    f1_emph = false;
    f1_code = false;
    frasm_f1.clearMisc();

#ifdef DI_EN_DBG_OUT
    //if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-16x0] Internal state is reset";
    }
#endif
}

//------------------------ Wait until one full frame is in the input queue.
bool PCM16X0DataStitcher::waitForOneFrame()
{
    bool frame1_lock;
    std::deque<PCM16X0SubLine>::iterator buf_scaner;

    frame1_lock = false;
    frasm_f1.frame_number = 0;

    // Pick start of the queue.
    buf_scaner = in_lines->begin();

    // Scan the buffer until there is nothing in the input.
    while(buf_scaner!=in_lines->end())
    {
        // Still at the first frame in the buffer.
        if(frame1_lock==false)
        {
            // End of the frame is not detected yet.
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
                    qInfo()<<"[L2B-16x0] Detected end of the Frame, exiting search...";
                }
#endif
                break;
            }
        }
        // Check for the End Of File.
        if((*buf_scaner).isServEndFile()!=false)
        {
#ifdef DI_LOG_BUF_WAIT_VERBOSE
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[L2B-16x0] EOF detected";
            }
#endif
            //break;
        }
        // Go to the next line in the input.
        buf_scaner++;
    }

    return frame1_lock;
}

//------------------------ Fill internal buffer up full frame from input queue.
void PCM16X0DataStitcher::fillUntilFullFrame()
{
    std::deque<PCM16X0SubLine>::iterator buf_scaner;

    // Reset internal buffer fill counter.
    trim_fill = 0;

#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-16x0] Copying full frame from the input queue...";
    }
#endif
    buf_scaner = in_lines->begin();

    // Fill up buffer until there is nothing in the input.
    while(buf_scaner!=in_lines->end())
    {
        // Check frame number.
        if((*buf_scaner).frame_number==frasm_f1.frame_number)
        {
            // Check if there is a service tag for the end of the frame.
            if((*buf_scaner).isServEndFrame()!=false)
            {
#ifdef DI_LOG_BUF_FILL_VERBOSE
                if((log_level&LOG_PROCESS)!=0)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-16x0] Frame end at line %u-%u, fill index: %u", (*buf_scaner).frame_number, (*buf_scaner).line_number, trim_fill);
                    qInfo()<<log_line;
                }
#endif
                // One frame is enough.
                //break;
            }
            else if(trim_fill<BUF_SIZE_TRIM)
            {
                // Copy data into internal buffer.
                trim_buf[trim_fill] = (*buf_scaner);
#ifdef DI_LOG_BUF_FILL_VERBOSE
                if((log_level&LOG_PROCESS)!=0)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-16x0] Added line %u-%u-%u (%u) into buffer at index: %u",
                                     (*buf_scaner).frame_number,
                                     (*buf_scaner).line_number,
                                     (*buf_scaner).line_part,
                                     (*buf_scaner).isServiceLine(),
                                     trim_fill);
                    qInfo()<<log_line;
                }
#endif
                trim_fill++;
            }
            else
            {
                qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error! Line buffer index out of bound in [PCM16X0DataStitcher::fillUntilFullFrame()]! Line skipped!";
                qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Max lines:"<<BUF_SIZE_TRIM;
            }
        }
        // Go to the next line in the input.
        buf_scaner++;
    }
#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-16x0] Filled"<<trim_fill<<"sub-lines";
    }
#endif
}

//------------------------ Detect frame trimming (how many lines to skip from top and bottom of each field).
void PCM16X0DataStitcher::findFrameTrim()
{
    uint16_t line_ind, f1o_good, f1e_good;
    bool f1e_top, f1e_bottom, f1o_top, f1o_bottom;
    bool f1o_skip_bad, f1e_skip_bad;

    file_start = file_end = false;
    f1e_top = f1e_bottom = f1o_top = f1o_bottom = false;
    f1o_skip_bad = f1e_skip_bad = false;
    f1o_good = f1e_good = 0;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-16x0] -------------------- Trim search starting...";
    }
#endif

    // Preset default trim.
    frasm_f1.even_top_data = frasm_f1.even_bottom_data = frasm_f1.odd_top_data = frasm_f1.odd_bottom_data = 0;

    // Pre-scan buffer to count up number of good lines.
    line_ind = 0;
    while(line_ind<trim_fill)
    {
        if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
        {
            // Frame number is ok.
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
                        if(f1e_good>MIN_GOOD_SUBLINES_PF)
                        {
                            f1e_skip_bad = true;
                        }
                    }
                    else
                    {
                        // Line from odd field.
                        f1o_good++;
                        if(f1o_good>MIN_GOOD_SUBLINES_PF)
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
        }
        // Go to next line.
        line_ind++;
    }

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        if(file_start!=false)
        {
            qInfo()<<"[L2B-16x0] New file tag detected in the frame";
        }
        if(file_end!=false)
        {
            qInfo()<<"[L2B-16x0] File end tag detected in the frame";
        }
        if(f1o_skip_bad!=false)
        {
            qInfo()<<"[L2B-16x0]"<<f1o_good<<"lines with good CRC in odd field, aggresive trimming enabled for odd field";
        }
        if(f1e_skip_bad!=false)
        {
            qInfo()<<"[L2B-16x0]"<<f1e_good<<"lines with good CRC in even field, aggresive trimming enabled for even field";
        }
    }
#endif

    // Cycle through the whole buffer.
    // (assuming that buffer starts on the first line of the frame)
    line_ind = 0;
    while(line_ind<trim_fill)
    {
        // Check for service lines in the input.
        if((trim_buf[line_ind].isServiceLine()!=false)&&(trim_buf[line_ind].isServFiller()==false))
        {
            // Go to next line.
            line_ind++;
#ifdef DI_LOG_TRIM_VERBOSE
            if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                qInfo()<<"[L2B-16x0] Skipping service line at"<<trim_buf[line_ind].frame_number<<"/"<<trim_buf[line_ind].line_number;
            }
#endif
            // Skip service lines.
            continue;
        }

#ifdef DI_LOG_TRIM_VERBOSE
        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
        {
            QString log_line;
            log_line.sprintf("[L2B-16x0] Check: at %04u, line: %03u-%03u-%01u, PCM: ",
                             line_ind,
                             trim_buf[line_ind].frame_number,
                             trim_buf[line_ind].line_number,
                             trim_buf[line_ind].line_part);
            if(trim_buf[line_ind].hasBWSet()==false)
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

        // Check frame numbers (that are set in [waitForOneFrame()]).
        if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
        {
            // Frame number is ok.
            // Check field.
            if((trim_buf[line_ind].line_number%2)==0)
            {
                // Line from even field.
                if(((f1e_skip_bad==false)&&(trim_buf[line_ind].hasBWSet()!=false))||
                    ((f1e_skip_bad!=false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false)))
                {
                    // PCM detected in the line.
                    if(f1e_top==false)
                    {
                        // Even top trim was not detected yet.
                        // Check if field starts from left part of the line.
                        if(trim_buf[line_ind].line_part==PCM16X0SubLine::PART_LEFT)
                        {
                            // Set number of the line that PCM is starting.
                            frasm_f1.even_top_data = trim_buf[line_ind].line_number;
                            // Detect only first encounter in the buffer.
                            f1e_top = true;
#ifdef DI_EN_DBG_OUT
                            if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
                            {
                                qInfo()<<"[L2B-16x0] Frame even field new top trim:"<<frasm_f1.even_top_data;
                            }
#endif
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            // TODO: fix non-left field starting
                            qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Found even field start at non-left part of the line! Frame"<<frasm_f1.frame_number;
                        }
#endif
                    }
                    // Check if field ends on the right part of the line.
                    else
                    {
                        if(trim_buf[line_ind].line_part==PCM16X0SubLine::PART_RIGHT)
                        {
                            // Update last line with PCM from the bottom.
                            frasm_f1.even_bottom_data = trim_buf[line_ind].line_number;
                            f1e_bottom = true;
                        }
                    }
                }
            }
            else
            {
                // Line from odd field.
                if(((f1o_skip_bad==false)&&(trim_buf[line_ind].hasBWSet()!=false))||
                    ((f1o_skip_bad!=false)&&(trim_buf[line_ind].isCRCValidIgnoreForced()!=false)))
                {
                    // PCM detected in the line.
                    if(f1o_top==false)
                    {
                        // Odd top trim was not detected yet.
                        // Check if field starts from left part of the line.
                        if(trim_buf[line_ind].line_part==PCM16X0SubLine::PART_LEFT)
                        {
                            // Set number of the line that PCM is starting.
                            frasm_f1.odd_top_data = trim_buf[line_ind].line_number;
                            // Detect only first encounter in the buffer.
                            f1o_top = true;
#ifdef DI_EN_DBG_OUT
                            if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
                            {
                                qInfo()<<"[L2B-16x0] Frame odd field new top trim:"<<frasm_f1.odd_top_data;
                            }
#endif
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            // TODO: fix non-left field starting
                            qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Found odd field start at non-left part of the line! Frame"<<frasm_f1.frame_number;
                        }
#endif
                    }
                    // Check if field ends on the right part of the line.
                    else
                    {
                        if(trim_buf[line_ind].line_part==PCM16X0SubLine::PART_RIGHT)
                        {
                            // Update last line with PCM from the bottom.
                            frasm_f1.odd_bottom_data = trim_buf[line_ind].line_number;
                            f1o_bottom = true;
                        }
                    }

                }
            }
        }

        // Go to next line.
        line_ind++;
    }

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        QString log_line;
        log_line = "[L2B-16x0] Frame ("+QString::number(frasm_f1.frame_number)+") trim (OT, OB, ET, EB): ";
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
    }
#endif
}

//------------------------ Split frame into 2 internal buffers per field.
void PCM16X0DataStitcher::splitFrameToFields()
{
    uint16_t line_ind, line_num;
    uint32_t ref_lvl_odd, ref_lvl_even, ref_lvl_odd_bad, ref_lvl_even_bad;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-16x0] -------------------- Splitting frame into field buffers...";
        qInfo()<<"[L2B-16x0]"<<trim_fill<<"sub-lines in the buffer (inc. service-lines)";
    }
#endif

    line_ind = 0;
    ref_lvl_odd = ref_lvl_even = ref_lvl_odd_bad = ref_lvl_even_bad = 0;

    // Cycle splitting frame buffer into 2 field buffers.
    while(line_ind<trim_fill)
    {
        // Save current line number.
        line_num = trim_buf[line_ind].line_number;
        // Pick lines for the frame.
        if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
        {
            // Fill up the frame, splitting fields.
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
                            trim_buf[line_ind].queue_order = frasm_f1.even_data_lines;
                            frame1_even[frasm_f1.even_data_lines] = trim_buf[line_ind];
                            frasm_f1.even_data_lines++;
                            // Pre-calculate average reference level for all lines.
                            ref_lvl_even_bad += trim_buf[line_ind].ref_level;
                            if(trim_buf[line_ind].isCRCValid()!=false)
                            {
                                // Calculate number of sub-lines with valid CRC in the field.
                                frasm_f1.even_valid_lines++;
                                // Pre-calculate average reference level for valid lines.
                                ref_lvl_even += trim_buf[line_ind].ref_level;
                            }
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
                            {
                                QString log_line;
                                log_line.sprintf("[L2B-16x0] Even field buffer is full, line %u-%u-%u is skipped!",
                                                 trim_buf[line_ind].frame_number,
                                                 trim_buf[line_ind].line_number,
                                                 trim_buf[line_ind].line_part);
                                qInfo()<<log_line;
                            }
                        }
#endif
                    }
                }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                else
                {
                    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
                    {
                        qInfo()<<"[L2B-16x0] Frame no line even field skip";
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
                        trim_buf[line_ind].queue_order = frasm_f1.odd_data_lines;
                        frame1_odd[frasm_f1.odd_data_lines] = trim_buf[line_ind];
                        frasm_f1.odd_data_lines++;
                        // Pre-calculate average reference level for all lines.
                        ref_lvl_odd_bad += trim_buf[line_ind].ref_level;
                        if(trim_buf[line_ind].isCRCValid()!=false)
                        {
                            // Calculate number of sub-lines with valid CRC in the field.
                            frasm_f1.odd_valid_lines++;
                            // Pre-calculate average reference level for valid lines.
                            ref_lvl_odd += trim_buf[line_ind].ref_level;
                        }
                    }
#ifdef DI_EN_DBG_OUT
                    else
                    {
                        if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
                        {
                            QString log_line;
                            log_line.sprintf("[L2B-16x0] Odd field buffer is full, line %u-%u-%u is skipped!",
                                             trim_buf[line_ind].frame_number,
                                             trim_buf[line_ind].line_number,
                                             trim_buf[line_ind].line_part);
                            qInfo()<<log_line;
                        }
                    }
#endif
                }
            }
        }
        // Continue search through the buffer.
        line_ind++;
    }

    // Calculate average reference level per fields.
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
        log_line.sprintf("[L2B-16x0] Odd field sub-lines: %u (%u valid), even field sub-lines: %u (%u valid)",
                         frasm_f1.odd_data_lines,
                         frasm_f1.odd_valid_lines,
                         frasm_f1.even_data_lines,
                         frasm_f1.even_valid_lines);
        qInfo()<<log_line;
        log_line.sprintf("[L2B-007] Average reference level: %03u/%03u (odd/even)", frasm_f1.odd_ref, frasm_f1.even_ref);
        qInfo()<<log_line;
    }
#endif
    if((frasm_f1.odd_data_lines%PCM16X0SubLine::SUBLINES_PER_LINE)!=0)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Odd field sub-lines count error! Not multiple of"<<PCM16X0SubLine::SUBLINES_PER_LINE;
    }
    if((frasm_f1.even_data_lines%PCM16X0SubLine::SUBLINES_PER_LINE)!=0)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Even field sub-lines count error! Not multiple of"<<PCM16X0SubLine::SUBLINES_PER_LINE;
    }
}

//------------------------ Pre-scan field and force invalid CRCs were needed.
void PCM16X0DataStitcher::prescanForFalsePosCRCs(std::vector<PCM16X0SubLine> *field_buf, uint16_t f_size)
{
    bool suppress_log;
    uint8_t part_no;
    PCM16X0SubLine part_0, part_1, part_2;

    suppress_log = ((log_level&(LOG_PROCESS))==0);
    //suppress_log = false;

    part_no = PCM16X0SubLine::PART_LEFT;
    for(uint16_t index=0;index<f_size;index++)
    {
        if(part_no==PCM16X0SubLine::PART_LEFT)
        {
            part_0 = (*field_buf)[index];
            part_0.queue_order = index;
            part_no = PCM16X0SubLine::PART_MIDDLE;
        }
        else if(part_no==PCM16X0SubLine::PART_MIDDLE)
        {
            part_1 = (*field_buf)[index];
            part_1.queue_order = index;
            part_no = PCM16X0SubLine::PART_RIGHT;
        }
        else if(part_no==PCM16X0SubLine::PART_RIGHT)
        {
            part_2 = (*field_buf)[index];
            part_2.queue_order = index;
            part_no = PCM16X0SubLine::PART_LEFT;
            // Verify integrity of the buffer.
            if((part_0.frame_number==part_1.frame_number)&&(part_1.frame_number==part_2.frame_number)
               &&(part_0.line_number==part_1.line_number)&&(part_1.line_number==part_2.line_number))
            {
                // Check if only left part of the line has valid CRC and it contains picked bits.
                // That may indicate wrong data coordinates and false-positive CRC.
                if((part_0.isCRCValid()!=false)&&(part_1.isCRCValid()==false)&&(part_2.isCRCValid()==false)
                   &&(part_0.hasPickedLeft()!=false))
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Frame"<<part_0.frame_number<<"line"<<part_0.line_number<<"only left part in the line is valid and it has picked bits, unsafe, invalidated";
                        qInfo()<<QString::fromStdString((*field_buf)[part_0.queue_order].dumpContentString());
                        qInfo()<<QString::fromStdString((*field_buf)[part_1.queue_order].dumpContentString());
                        qInfo()<<QString::fromStdString((*field_buf)[part_2.queue_order].dumpContentString());
                    }
#endif
                    (*field_buf)[part_0.queue_order].setForcedBad();
                    (*field_buf)[part_1.queue_order].setForcedBad();
                    (*field_buf)[part_2.queue_order].setForcedBad();
                }
                // Check if only right part of the line has valid CRC and it contains picked bits.
                // That may indicate wrong data coordinates and false-positive CRC.
                else if((part_0.isCRCValid()==false)&&(part_1.isCRCValid()==false)&&(part_2.isCRCValid()!=false)
                   &&(part_2.hasPickedRight()!=false))
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Frame"<<part_0.frame_number<<"line"<<part_0.line_number<<"only right part in the line is valid and it has picked bits, unsafe, invalidated";
                        qInfo()<<QString::fromStdString((*field_buf)[part_0.queue_order].dumpContentString());
                        qInfo()<<QString::fromStdString((*field_buf)[part_1.queue_order].dumpContentString());
                        qInfo()<<QString::fromStdString((*field_buf)[part_2.queue_order].dumpContentString());
                    }
#endif
                    (*field_buf)[part_0.queue_order].setForcedBad();
                    (*field_buf)[part_1.queue_order].setForcedBad();
                    (*field_buf)[part_2.queue_order].setForcedBad();
                }
            }
            else
            {
                qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Field data integrity error in [PCM16X0DataStitcher::prescanForFalsePosCRCs()]!";
                qWarning()<<DBG_ANCHOR<<QString::fromStdString((*field_buf)[part_0.queue_order].dumpContentString());
                qWarning()<<DBG_ANCHOR<<QString::fromStdString((*field_buf)[part_1.queue_order].dumpContentString());
                qWarning()<<DBG_ANCHOR<<QString::fromStdString((*field_buf)[part_2.queue_order].dumpContentString());
                break;
            }
        }
    }
}

//------------------------ Cut lines from the top of the field.
void PCM16X0DataStitcher::cutFieldTop(std::vector<PCM16X0SubLine> *field_buf, uint16_t *f_size, uint16_t cut_cnt)
{
    // Switch from line offset to sub-line offset.
    cut_cnt *= PCM16X0SubLine::SUBLINES_PER_LINE;
    // Check if anything should be done.
    if(cut_cnt>0)
    {
        // Refill the field.
        for(size_t i=0;i<(size_t)((*f_size)-cut_cnt);i++)
        {
            // Prevent field buffer out-of-bound.
            if((i+cut_cnt)>=SUBLINES_PF)
            {
                break;
            }
            //qInfo()<<i<<(i+cut_cnt)<<(*f_size);
            if((i+cut_cnt)<(*f_size))
            {
                // Copy data from bottom lines to the top.
                (*field_buf)[i] = (*field_buf)[i+cut_cnt];
            }
            else
            {
                (*field_buf)[i].clear();
            }
        }
        // Correct data size.
        (*f_size) -= cut_cnt;
    }
}

//------------------------ Find sub-line offset of the first "0" Control Bit in a field buffer.
int16_t PCM16X0DataStitcher::findZeroControlBitOffset(std::vector<PCM16X0SubLine> *field, uint16_t f_size, bool from_top)
{
    bool suppress_log;
    uint8_t zero_cnt, run_cnt;
    int16_t buf_start_ofs, subline_ofs, zero_ofs;
    std::vector<uint8_t> cnt_stat;
    std::vector<int16_t> ofs_stat;

    // TODO: exclude zeroed bits from the count if those are <2 sub-lines to the limits of the field.
    suppress_log = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));
    run_cnt = zero_cnt = 0;
    zero_ofs = -1;
    cnt_stat.reserve(SI_TRUE_INTERLEAVE);
    ofs_stat.reserve(SI_TRUE_INTERLEAVE);

    // Select search direction.
    if(from_top==false)
    {
        // Search from the bottom of the field.
        buf_start_ofs = f_size;
        // Go from [PART_LEFT] to [PART_MIDDLE].
        buf_start_ofs++;
        // Backwards cycle through sub-lines in the field buffer until Control Bit "0" is found in all interleave blocks.
        while(buf_start_ofs>=PCM16X0SubLine::SUBLINES_PER_LINE)
        {
            // Go to the next line, advancing [PCM16X0SubLine::SUBLINES_PER_LINE] sub-lines backwards.
            buf_start_ofs -= PCM16X0SubLine::SUBLINES_PER_LINE;
            // Scan back through interleave blocks.
            for(uint8_t iblk=0;iblk<PCM16X0DataBlock::INT_BLK_PER_FIELD;iblk++)
            {
                // Calculate subline offset for the interleave block probable Control Bit.
                subline_ofs = buf_start_ofs-(iblk*SI_TRUE_INTERLEAVE);
                // Check if that interleave block is present in the buffer.
                if(subline_ofs>=0)
                {
                    if((*field)[subline_ofs].line_part!=PCM16X0SubLine::PART_MIDDLE)
                    {
                        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error in [findZeroControlBitOffset()], line not on middle subline in field buffer!";
                        zero_cnt = 0;
                        break;
                    }
                    /*qInfo()<<"Offset:"<<QString::number(buf_start_ofs)<<"subline:"<<QString::number(subline_ofs)
                           <<"line:"<<QString::number((*field)[subline_ofs].line_number)<<"count:"<<zero_cnt
                           <<QString::fromStdString((*field)[subline_ofs].dumpWordsString());*/
                    // Check CRC state of the sub-line.
                    if((*field)[subline_ofs].isCRCValid()!=false)
                    {
                        // Check if Control Bit is set to "0" in the sub-line.
                        if((*field)[subline_ofs].control_bit==false)
                        {
                            // Counter number of interleave blocks with "0" bit at this position.
                            zero_cnt++;
                        }
                    }
                }
                else
                {
                    // Offset out of bounds.
                    break;
                }
            }
            // Save stats for the run.
            ofs_stat.push_back((buf_start_ofs-1));      // Move back from [PART_MIDDLE] to [PART_LEFT].
            cnt_stat.push_back(zero_cnt);
            zero_cnt = 0;
            run_cnt++;
            if(run_cnt>(PCM16X0DataBlock::SI_INTERLEAVE_OFS*3/2))
            {
                // More lines that should be enough to find one interleave block has passed.
                break;
            }
        }
    }
    else
    {
        // Search from the top of the field.
        buf_start_ofs = 0;
        // Go from [PART_LEFT] to [PART_MIDDLE].
        buf_start_ofs++;
        // Forwards cycle through sub-lines in the field buffer until Control Bit "0" is found in all interleave blocks.
        while(buf_start_ofs<(f_size-PCM16X0SubLine::SUBLINES_PER_LINE))
        {
            // Go to the next line, advancing [PCM16X0SubLine::SUBLINES_PER_LINE] sub-lines forward.
            buf_start_ofs += PCM16X0SubLine::SUBLINES_PER_LINE;
            // Scan back through interleave blocks.
            for(uint8_t iblk=0;iblk<PCM16X0DataBlock::INT_BLK_PER_FIELD;iblk++)
            {
                // Calculate subline offset for the interleave block probable Control Bit.
                subline_ofs = buf_start_ofs+(iblk*SI_TRUE_INTERLEAVE);
                // Check if that interleave block is present in the buffer.
                if(subline_ofs<f_size)
                {
                    if((*field)[subline_ofs].line_part!=PCM16X0SubLine::PART_MIDDLE)
                    {
                        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error in [findZeroControlBitOffset()], line not on middle subline in field buffer!";
                        zero_cnt = 0;
                        break;
                    }
                    /*qInfo()<<"Offset:"<<QString::number(buf_start_ofs)<<"subline:"<<QString::number(subline_ofs)
                           <<"line:"<<QString::number((*field)[subline_ofs].line_number)<<"count:"<<zero_cnt
                           <<QString::fromStdString((*field)[subline_ofs].dumpWordsString());*/
                    // Check CRC state of the sub-line.
                    if((*field)[subline_ofs].isCRCValid()!=false)
                    {
                        // Check if Control Bit is set to "0" in the sub-line.
                        if((*field)[subline_ofs].control_bit==false)
                        {
                            // Counter number of interleave blocks with "0" bit at this position.
                            zero_cnt++;
                        }
                    }
                }
                else
                {
                    // Offset out of bounds.
                    break;
                }
            }
            // Save stats for the run.
            ofs_stat.push_back((buf_start_ofs-1));      // Move back from [PART_MIDDLE] to [PART_LEFT].
            cnt_stat.push_back(zero_cnt);
            zero_cnt = 0;
            run_cnt++;
            if(run_cnt>(PCM16X0DataBlock::SI_INTERLEAVE_OFS*3/2))
            {
                // More lines that should be enough to find one interleave block has passed.
                break;
            }
        }
    }
    // Cycle through stats.
    zero_cnt = 0;
    buf_start_ofs = 0;
    for(size_t i=0;i<ofs_stat.size();i++)
    {
        // Pick offset with the most number of zeroed bits.
        if(cnt_stat[i]>zero_cnt)
        {
            zero_cnt = cnt_stat[i];
            buf_start_ofs = ofs_stat[i];
        }
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(cnt_stat[i]>0)
            {
                qInfo()<<"[L2B-16x0] At offset"<<ofs_stat[i]<<"found"<<cnt_stat[i]<<"zeroed Control Bits";
            }
        }
#endif
    }

    // Check if there are enough interleave blocks counted with zeroed bit.
    if(zero_cnt>0)
    {
        zero_ofs = buf_start_ofs;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            if(from_top==false)
            {
                log_line.sprintf("[L2B-16x0] Zero CTRL from bottom at line %03u, offset %u, total %u",
                                 (*field)[buf_start_ofs].line_number,
                                 zero_ofs,
                                 f_size);
            }
            else
            {
                log_line.sprintf("[L2B-16x0] Zero CTRL from top at line %03u, offset %u, total %u",
                                 (*field)[buf_start_ofs].line_number,
                                 zero_ofs,
                                 f_size);
            }
            qInfo()<<log_line;
        }
#endif
    }

    return zero_ofs;
}

//------------------------ Estimate interleave block number by sub-line offset of the zeroed Control Bit.
uint8_t PCM16X0DataStitcher::estimateBlockNumber(std::vector<PCM16X0SubLine> *field, uint16_t f_size, int16_t zero_ofs)
{
    bool suppress_log;
    uint8_t out_iblk;

    suppress_log = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    // By default set to the last block.
    out_iblk = (PCM16X0DataBlock::INT_BLK_PER_FIELD-1);

    // Check bound for the offset.
    if(zero_ofs<f_size)
    {
        // Make sure that zeroed Control Bit was found (offset>0).
        if(zero_ofs<0)
        {
            out_iblk = 0;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Zeroed Control Bit was not detected, anchoring data to the top, interleave block 0";
            }
#endif
        }
        else
        {
            PCM16X0SubLine ctrl_line;
            // Pick sub-line with zeroed Control Bit.
            ctrl_line = field->at(zero_ofs);
            // Estimate interleave block number.
            if(ctrl_line.line_number<INT_BLK_LINE_DELIMITER)
            {
                out_iblk = 0;
            }
            else if(ctrl_line.line_number<(INT_BLK_LINE_DELIMITER+(2*PCM16X0DataBlock::SI_INTERLEAVE_OFS)))
            {
                out_iblk = 1;
            }
            else if(ctrl_line.line_number<(INT_BLK_LINE_DELIMITER+(2*(2*PCM16X0DataBlock::SI_INTERLEAVE_OFS))))
            {
                out_iblk = 2;
            }
            else if(ctrl_line.line_number<(INT_BLK_LINE_DELIMITER+(3*(2*PCM16X0DataBlock::SI_INTERLEAVE_OFS))))
            {
                out_iblk = 3;
            }
            else if(ctrl_line.line_number<(INT_BLK_LINE_DELIMITER+(4*(2*PCM16X0DataBlock::SI_INTERLEAVE_OFS))))
            {
                out_iblk = 4;
            }
            else if(ctrl_line.line_number<(INT_BLK_LINE_DELIMITER+(5*(2*PCM16X0DataBlock::SI_INTERLEAVE_OFS))))
            {
                out_iblk = 5;
            }
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Control Bit source line number:"<<ctrl_line.line_number<<"estimated to be from"<<(out_iblk+1)<<"interleave block";
            }
#endif
        }
    }
    else
    {
        qWarning()<<"[L2B-16x0] Offset out-of-bounds in [PCM16X0DataStitcher::estimateBlockNumber()]:"<<zero_ofs<<"of"<<f_size;
    }

    return out_iblk;
}

//------------------------ Try to assemble data blocks with provided padding for SI format, collect stats.
uint8_t PCM16X0DataStitcher::trySIPadding(std::deque<PCM16X0SubLine> *field_data, uint8_t padding, FieldStitchStats *stitch_stats)
{
    bool suppress_log, run_lock, even_block;
    uint8_t intl_blk_index;
    uint16_t line_index, start_subline;
    uint16_t valid_burst_count, silence_burst_count, uncheck_burst_count, brk_burst_count;
    uint16_t top_broken, valid_burst_max, silence_burst_max, uncheck_burst_max, brk_burst_max;
    size_t buf_size;
    PCM16X0DataBlock service_bits;

    suppress_log = !((log_level&LOG_PADDING_BLOCK)!=0);
    //suppress_log = false;

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-16x0] Processing "+QString::number(field_data->size())+" sub-lines, checking padding "+QString::number(padding)+"...";
    }
    if((log_level&LOG_PADDING_LINE)!=0)
    {
        // Dump test lines array.
        for(size_t index=0;index<field_data->size();index++)
        {
            qInfo()<<"[L2B-16x0]"<<QString::fromStdString((*field_data)[index].dumpContentString());
        }
    }
#endif

    // Per interleave block stats.
    std::deque<FieldStitchStats> iblk_stats;
    iblk_stats.resize(PCM16X0DataBlock::INT_BLK_PER_FIELD);
    top_broken = 0;

    // Run deinterleaving and error-detection on padded field.
    // Cycle through interleave blocks in the field.
    buf_size = 0;
    intl_blk_index = 0;
    while(intl_blk_index<PCM16X0DataBlock::INT_BLK_PER_FIELD)
    {
        line_index = 0;
        valid_burst_count = silence_burst_count = uncheck_burst_count = brk_burst_count = 0;
        valid_burst_max = silence_burst_max = uncheck_burst_max = brk_burst_max = 0;
        run_lock = even_block = false;
        // Cycle through lines in single block inside the interleave block.
        while(line_index<PCM16X0DataBlock::SI_INTERLEAVE_OFS)
        {
            // Calculate starting sub-line for the block.
            start_subline = (line_index+(intl_blk_index*SI_TRUE_INTERLEAVE));
            // Fill up block, performing de-interleaving and error-correction.
            if(pad_checker.processBlock(start_subline, even_block)!=PCM16X0Deinterleaver::DI_RET_OK)
            {
                // No data left in the buffer or some other error while de-interleaving.
#ifdef DI_EN_DBG_OUT
                if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
                {
                    qInfo()<<"[L2B-16x0] No more data in the buffer at subline offset"<<start_subline<<"block offset"<<line_index;
                }
#endif
                // Exit line cycle.
                break;
            }
            // At least cycle run once.
            run_lock = true;
            // Put line number inside of interleave block into queue index.
            padding_block.queue_order = line_index;
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
                        qInfo()<<"[L2B-16x0] Updating 'valid' limit from"<<valid_burst_max<<"to"<<valid_burst_count<<"at pass"<<buf_size;
                    }
#endif
                    valid_burst_max = valid_burst_count;
                }
            }
#ifdef DI_EN_DBG_OUT
            if((suppress_log==false)&&((log_level&LOG_PROCESS)!=0))
            {
                if(padding_block.frame_number!=0)
                {
                    qInfo()<<"[L2B-16x0] "+QString::fromStdString(padding_block.dumpContentString());
                }
            }
#endif
            // Is there too much silence?
            // P-check will not have any use on silence.
            if(padding_block.isSilent()!=false)
            {
                // Found block with too much silence.
                silence_burst_count++;
                if(silence_burst_count>=MAX_BURST_SILENCE_SI)
                {
                    // Reset valid block counter.
                    valid_burst_count = 0;
                }
            }
            else
            {
                if(silence_burst_count>silence_burst_max)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Updating 'silence' limit from"<<silence_burst_max<<"to"<<silence_burst_count<<"at pass"<<buf_size;
                    }
#endif
                    // Update longest burst of silence.
                    silence_burst_max = silence_burst_count;
                }
                // Reset counter if burst of silence ended.
                silence_burst_count = 0;
            }
            // Check for "uncheckable" blocks: invalid, with no P, with P-corrected samples.
            // P-correction can "correct" samples from broken data blocks, masking incorrectly assembled blocks.
            if((padding_block.canForceCheck()==false)||(padding_block.isDataFixedByP()!=false))
            {
                // Block can not be checked for validity.
                uncheck_burst_count++;
                if(uncheck_burst_count>MAX_BURST_UNCH_SI)
                {
                    // Reset valid block counter.
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
                        qInfo()<<"[L2B-16x0] Updating 'unchecked' limit from"<<uncheck_burst_max<<"to"<<uncheck_burst_count<<"at pass"<<buf_size;
                    }
#endif
                    // Update longest burst of unchecked blocks.
                    uncheck_burst_max = uncheck_burst_count;
                }
                // Reset counter if burst of unchecked blocks ended.
                uncheck_burst_count = 0;
            }
            // Check for BROKEN data block.
            if(padding_block.isDataBroken()!=false)
            {
                // Data in the block is BROKEN (no CRC marks but parity error).
                // Current padding run is bad.
                brk_burst_count++;
                if(brk_burst_count>=MAX_BURST_BROKEN)
                {
                    // Reset valid block counter.
                    valid_burst_count = 0;
                }
            }
            else
            {
                if(brk_burst_count>brk_burst_max)
                {
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Updating 'BROKEN' limit from"<<brk_burst_max<<"to"<<brk_burst_count<<"at pass"<<buf_size;
                    }
#endif
                    // Update longest burst of BROKEN blocks.
                    brk_burst_max = brk_burst_count;
                }
                // Reset counter if burst of BROKEN blocks ended.
                brk_burst_count = 0;
            }
            buf_size++;

            // TODO: check validity of the line before checking Control Bits.
            // Check if current sub-line contains useful service bits.
            if(line_index<BIT_MAX_OFS)
            {
                if(line_index==BIT_EMPHASIS_OFS)
                {
                    // 1st H of the interleave block.
                    if((*field_data)[start_subline].control_bit==false)
                    {
                        // Emphasis: enabled.
                        service_bits.emphasis = true;
                    }
                    else
                    {
                        // Emphasis: disabled.
                        service_bits.emphasis = false;
                    }
                    service_bits.frame_number = (*field_data)[start_subline].frame_number;
                    service_bits.start_line = (*field_data)[start_subline].line_number;
                    service_bits.start_part = (*field_data)[start_subline].line_part;
                    // Use odd/even flag as padded/non-padded block indicator.
                    if((padding!=0)&&(intl_blk_index==0))
                    {
                        service_bits.setOrderEven();
                    }
                    else
                    {
                        service_bits.setOrderOdd();
                    }
                }
                else if(line_index==BIT_SAMPLERATE_OFS)
                {
                    // 2nd H of the interleave block.
                    if((*field_data)[start_subline].control_bit==false)
                    {
                        // Sampling frequency: 44100 Hz.
                        service_bits.sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
                    }
                    else
                    {
                        // Sampling frequency: 44056 Hz.
                        service_bits.sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
                    }
                }
                else if(line_index==BIT_MODE_OFS)
                {
                    // 3rd H of the interleave block.
                    if((*field_data)[start_subline].control_bit==false)
                    {
                        // Mode: EI format.
                        service_bits.ei_format = true;
                    }
                    else
                    {
                        // Mode: SI format.
                        service_bits.ei_format = false;
                    }
                }
                else if(line_index==BIT_CODE_OFS)
                {
                    // 4th H of the interleave block.
                    if((*field_data)[start_subline].control_bit==false)
                    {
                        // Code.
                        service_bits.code = true;
                    }
                    else
                    {
                        // Audio.
                        service_bits.code = false;
                    }
                    service_bits.stop_line = (*field_data)[start_subline].line_number;
                    service_bits.stop_part = (*field_data)[start_subline].line_part;
                }
            }
            // Alternate order from block to block inside one interleave block.
            even_block = !even_block;
            // Move to the next line in the buffer with the interleave block.
            line_index++;
        }// [line_index] cycle

#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Interleave block "+QString::number(intl_blk_index)+": "+QString::fromStdString(service_bits.dumpServiceBitsString());
        }
#endif
        // Update post-cycle counters if required.
        if(valid_burst_count>valid_burst_max)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Updating 'valid' limit from"<<valid_burst_max<<"to"<<valid_burst_count<<"at pass"<<buf_size;
            }
#endif
            valid_burst_max = valid_burst_count;
        }
        if(silence_burst_count>silence_burst_max)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Updating 'silence' limit from"<<silence_burst_max<<"to"<<silence_burst_count<<"at pass"<<buf_size;
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
                qInfo()<<"[L2B-16x0] Updating 'unchecked' limit from"<<uncheck_burst_max<<"to"<<uncheck_burst_count<<"at pass"<<buf_size;
            }
#endif
            // Update longest burst of unchecked blocks.
            uncheck_burst_max = uncheck_burst_count;
        }
        if(brk_burst_count>brk_burst_max)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Updating 'BROKEN' limit from"<<brk_burst_max<<"to"<<brk_burst_count<<"at pass"<<buf_size;
            }
#endif
            // Update longest burst of BROKEN blocks.
            brk_burst_max = brk_burst_count;
        }

        if(run_lock!=false)
        {
            // Save stats for interleave block.
            iblk_stats[intl_blk_index].index = intl_blk_index;
            iblk_stats[intl_blk_index].valid = valid_burst_max;
            iblk_stats[intl_blk_index].silent = silence_burst_max;
            iblk_stats[intl_blk_index].unchecked = uncheck_burst_max;
            iblk_stats[intl_blk_index].broken = brk_burst_max;
        }

        // Move to the next interleave block in the field.
        intl_blk_index++;
    }// [intl_blk_index] cycle

    // Check if there are more than 2 interleave blocks.
    if(iblk_stats.size()>2)
    {
        // First and last blocks may be unsafe to process,
        // exclude those from stats if there are enough blocks in the list.
        if(iblk_stats.front().index==0)
        {
            iblk_stats.pop_front();
        }
        if(iblk_stats.back().index==6)
        {
            iblk_stats.pop_back();
        }
    }
    for(uint8_t index=0;index<iblk_stats.size();index++)
    {
        if(iblk_stats[index].broken>top_broken)
        {
            top_broken = iblk_stats[index].broken;
        }
    }
    // Fill all broken fields with top value to prevent sort skewing.
    for(uint8_t index=0;index<iblk_stats.size();index++)
    {
        iblk_stats[index].broken = top_broken;
    }

    // Sort vector by valid block count, then by unchecked blocks count.
    std::sort(iblk_stats.begin(), iblk_stats.end());

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString index_line, valid_line, silence_line, unchecked_line, broken_line, tmp;
        for(uint8_t index=0;index<iblk_stats.size();index++)
        {
            tmp.sprintf("%02u", iblk_stats[index].index);
            index_line += "|"+tmp;
            tmp.sprintf("%02x", iblk_stats[index].valid);
            valid_line += "|"+tmp;
            tmp.sprintf("%02x", iblk_stats[index].silent);
            silence_line += "|"+tmp;
            tmp.sprintf("%02x", iblk_stats[index].unchecked);
            unchecked_line += "|"+tmp;
            tmp.sprintf("%02x", iblk_stats[index].broken);
            broken_line += "|"+tmp;
        }
        qInfo()<<"[L2B-16x0] Per interleave block sorted stats:";
        qInfo()<<"[L2B-16x0] i-Block:  "<<index_line;
        qInfo()<<"[L2B-16x0] Valid:    "<<valid_line;
        qInfo()<<"[L2B-16x0] Silent:   "<<silence_line;
        qInfo()<<"[L2B-16x0] Unchecked:"<<unchecked_line;
        qInfo()<<"[L2B-16x0] Broken:   "<<broken_line;
    }
#endif

    valid_burst_max = iblk_stats[0].valid;
    silence_burst_max = iblk_stats[0].silent;
    uncheck_burst_max = iblk_stats[0].unchecked;
    brk_burst_max = iblk_stats[0].broken;
    iblk_stats.clear();

    // Output stats for the seam.
    if(stitch_stats!=NULL)
    {
        stitch_stats->index = padding;
        stitch_stats->valid = valid_burst_max;
        stitch_stats->silent = silence_burst_max;
        stitch_stats->unchecked = uncheck_burst_max;
        stitch_stats->broken = brk_burst_max;
    }

    // Check for too many unchecked blocks.
    if(uncheck_burst_max>MAX_BURST_UNCH_SI)
    {
        // Too many unchecked blocks.
        return DS_RET_NO_PAD;
    }
    // Check is any good blocks were found.
    if(valid_burst_max==0)
    {
        // No good blocks.
        return DS_RET_NO_PAD;
    }
    // Check for too much silence.
    if(silence_burst_max>MAX_BURST_SILENCE_SI)
    {
        // Too much silence.
        return DS_RET_SILENCE;
    }
    // Check for BROKEN data blocks.
    if(brk_burst_max>=MAX_BURST_BROKEN)
    {
        // BROKEN data.
        return DS_RET_BROKE;
    }
    // Padding is OK.
    return DS_RET_OK;
}

//------------------------ Detect SI format interleave block coordinates in the field buffer, calculate padding.
//------------------------ Data in SI format in one field is independent from other fields and frames.
uint8_t PCM16X0DataStitcher::findSIPadding(std::vector<PCM16X0SubLine> *field_buf, uint16_t *f_size,
                                         uint16_t *top_padding, uint16_t *bottom_padding)
{
    bool suppress_log, padding_lock;
    uint8_t ext_di_log_lvl, iblk_num, pad, stitch_res;
    uint16_t field_subline_count, min_broken, pad_top, pad_bottom;
    int16_t zero_ofs, last_ofs;
    PCM16X0SubLine empty_line;
    std::array<bool, (MAX_PADDING_SI+1)> pad_results;

    stitch_res = DS_RET_NO_PAD;
    suppress_log = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    // Pre-calculate default padding (moving all data to the bottom).
    (*bottom_padding) = 0;
    (*top_padding) = (SUBLINES_PF-(*f_size))/PCM16X0SubLine::SUBLINES_PER_LINE;

    if(field_buf==NULL)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Null pointer for buffer provided in [PCM16X0DataStitcher::findSIPadding()], exiting...";
        return DS_RET_NO_DATA;
    }
    if(field_buf->size()<(*f_size))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Buffer index out-of-bound in [PCM16X0DataStitcher::findSIPadding()], exiting...";
        return DS_RET_NO_DATA;
    }
    if((*f_size)<MIN_FILL_SUBLINES_PF_SI)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Not enough data in the field buffer in [PCM16X0DataStitcher::findSIPadding()], exiting...";
        }
#endif
        return DS_RET_NO_DATA;
    }

    field_subline_count = pad_top = pad_bottom = 0;
    pad_results.fill(false);

    /*(*top_padding) = 6;
    (*bottom_padding) = 12;
    return DS_RET_OK;*/

    ext_di_log_lvl = 0;
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_DEINTERLEAVE)!=0))
    {
        ext_di_log_lvl |= PCM16X0Deinterleaver::LOG_PROCESS;
    }
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_ERROR_CORR)!=0))
    {
        ext_di_log_lvl |= PCM16X0Deinterleaver::LOG_ERROR_CORR;
    }

    // Save sub-line count of the buffer.
    field_subline_count = (*f_size);
    // Fill test queue with all the data from the field.
    padding_queue.clear();
    for(uint16_t line=0;line<field_subline_count;line++)
    {
        padding_queue.push_back((*field_buf)[line]);    // Filling going forward pushing back is faster than backwards pushing front.
    }

    // Preset frame and line parameters for padding sub-line dummy.
    empty_line.frame_number = (*field_buf)[0].frame_number;
    empty_line.line_number = (*field_buf)[field_subline_count-1].line_number;
    empty_line.queue_order = (*field_buf)[field_subline_count-1].queue_order;
    empty_line.line_part = 0;

    // Fill full field from the bottom with empty sub-lines if required.
    while(padding_queue.size()<SUBLINES_PF)
    {
        if(empty_line.line_part==0)
        {
            // Current sub-line is the first from a new line.
            // Increase line count.
            empty_line.line_number += 2;
        }
        empty_line.queue_order++;
        padding_queue.push_back(empty_line);
        // Go to the next sub-line.
        empty_line.line_part++;
        if(empty_line.line_part>=PCM16X0SubLine::SUBLINES_PER_LINE)
        {
            empty_line.line_part = 0;
        }
        // Calculate how many sub-lines were inserted from below the data from the field.
        pad_bottom++;
    }
#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-16x0] "+QString::number(field_subline_count)+" sub-lines provided for padding, added "+QString::number(pad_bottom)+" empty sub-lines at the bottom.";
    }
#endif

    padding_lock = false;
    // Setup fields for top padding.
    empty_line.line_number = 0;
    empty_line.queue_order = 0;

    // Find any zeroed Control Bits from the top of the buffer (assuming it will be at 2H, Sample rate).
    // There may be none zeroed Control Bits in SI format, so this offset detection will not work.
    zero_ofs = findZeroControlBitOffset(field_buf, field_subline_count, true);
    // Check if found Control Bit is from 1H (Emphasis) and there is zeroed 2H bit (Sample rate).
    if((zero_ofs>=0)&&((zero_ofs+PCM16X0SubLine::SUBLINES_PER_LINE+1)<field_subline_count))
    {
        // Zeroed Control Bit offset was found and there are enough lines in the buffer to go lower.
        // Select next line.
        // Check CRC state of the [PART_MIDDLE] sub-line.
        if((*field_buf)[zero_ofs+PCM16X0SubLine::SUBLINES_PER_LINE+1].isCRCValid()!=false)
        {
            // CRC is ok.
            // Check if Control Bit is set to "0" in the sub-line.
            if((*field_buf)[zero_ofs+PCM16X0SubLine::SUBLINES_PER_LINE+1].control_bit==false)
            {
                // Next line also has zeroed Control Bit, that must be 2H,
                // because 3H bit should be always "1" in SI format.
                // There can not be two adjacent lines with zeroed Control Bits in SI format except 2H and 3H.
                // Move offset one line lower.
                zero_ofs += PCM16X0SubLine::SUBLINES_PER_LINE;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Zero CTRL also at line:"<<(*field_buf)[zero_ofs+PCM16X0SubLine::SUBLINES_PER_LINE+1].line_number<<
                             "offset"<<zero_ofs<<"moved offset to it.";
                }
#endif
            }
        }
    }
    // Guess number of first interleave block in the buffer
    // by position of the zeroed Control Bit in the source frame.
    iblk_num = estimateBlockNumber(field_buf, field_subline_count, zero_ofs);

    // Check if P-code is available for force-checking.
    if(enable_P_code!=false)
    {
        // Setup deinterleaver.
        pad_checker.setInput(&padding_queue);
        pad_checker.setOutput(&padding_block);
        pad_checker.setForceParity(true);
        pad_checker.setLogLevel(ext_di_log_lvl);
        pad_checker.setPCorrection(true);
        pad_checker.setSIFormat();

        // Check previous padding.
        pad = getProbablePadding();
        if(pad!=INVALID_PAD_MARK)
        {
            // Apply that padding.
            for(uint8_t insert=0;insert<pad;insert++)
            {
                for(uint8_t i=PCM16X0SubLine::SUBLINES_PER_LINE;i>0;i--)
                {
                    // Remove last line from the field to balance number of lines in the field.
                    padding_queue.pop_back();
                    // Preset line part index for padding sub-line.
                    empty_line.line_part = (i-1);
                    // Add padding line at the top of the field.
                    padding_queue.push_front(empty_line);
                }
            }
            // Check if padding from stats can be used.
            if(trySIPadding(&padding_queue, pad)==DS_RET_OK)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Previously used padding"<<pad<<"is OK.";

                }
#endif
                padding_lock = true;
                // Update padding stats.
                updatePadStats(pad, true);
                // Save padding.
                pad_top = pad;
                // Cut bottom dummy padding to the size.
                if(pad_bottom>=pad_top)
                {
                    pad_bottom = pad_bottom-pad_top;
                }
                else
                {
                    pad_bottom = 0;
                }
                stitch_res = DS_RET_OK;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Previously used padding"<<pad<<"did not work!";
                }
#endif
                // Re-fill test queue with all the data from the field.
                pad_bottom = 0;
                padding_queue.clear();
                for(uint16_t line=0;line<field_subline_count;line++)
                {
                    padding_queue.push_back((*field_buf)[line]);
                }
                // Preset frame and line parameters for padding sub-line dummy.
                empty_line.frame_number = (*field_buf)[0].frame_number;
                empty_line.line_number = (*field_buf)[field_subline_count-1].line_number;
                empty_line.queue_order = (*field_buf)[field_subline_count-1].queue_order;
                empty_line.line_part = 0;
                // Fill full field from the bottom with empty sub-lines if required.
                while(padding_queue.size()<SUBLINES_PF)
                {
                    if(empty_line.line_part==0)
                    {
                        // Current sub-line is the first from a new line.
                        // Increase line count.
                        empty_line.line_number += 2;
                    }
                    empty_line.queue_order++;
                    padding_queue.push_back(empty_line);
                    // Go to the next sub-line.
                    empty_line.line_part++;
                    if(empty_line.line_part>=PCM16X0SubLine::SUBLINES_PER_LINE)
                    {
                        empty_line.line_part = 0;
                    }
                    // Calculate how many sub-lines were inserted from below the data from the field.
                    pad_bottom++;
                }
            }
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] No stats data for previous paddings available.";
            }
        }
#endif

        // Start full padding sweep only if no valid previous padding was found.
        if(padding_lock==false)
        {
            // Initialize stats data.
            std::vector<FieldStitchStats> stitch_data, min_brk_data;
            stitch_data.resize(MAX_PADDING_SI);
            min_brk_data.reserve(MAX_PADDING_SI);

            // Cycle through top padding within length of one interleave block.
            for(pad=0;pad<MAX_PADDING_SI;pad++)
            {
                // Try to assemble blocks with set top padding and collect stats into [stitch_data].
                trySIPadding(&padding_queue, pad, &stitch_data[pad]);

#ifdef DI_EN_DBG_OUT
                if((suppress_log==false)&&((log_level&LOG_PADDING_BLOCK)!=0))
                {
                    QString log_line;
                    log_line.sprintf("[L2B-16x0] Removing line %u-%u from the bottom to balance padding at the top...",
                                     padding_queue.back().frame_number,
                                     padding_queue.back().line_number);
                    qInfo()<<log_line;
                }
#endif
                // Add one video line of padding at the top and remove last line, shifting data lower.
                // Note that one line contains [PCM16X0SubLine::SUBLINES_PER_LINE] sub-lines.
                for(uint8_t i=PCM16X0SubLine::SUBLINES_PER_LINE;i>0;i--)
                {
                    // Remove last line from the field to balance number of lines in the field.
                    padding_queue.pop_back();
                    // Preset line part index for padding sub-line.
                    empty_line.line_part = (i-1);
                    // Add padding line at the top of the field.
                    padding_queue.push_front(empty_line);
                }
            }

#ifdef DI_EN_DBG_OUT
            if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                QString index_line, valid_line, silence_line, unchecked_line, broken_line, tmp;
                for(pad=0;pad<MAX_PADDING_SI;pad++)
                {
                    tmp.sprintf("%02u", stitch_data[pad].index);
                    index_line += "|"+tmp;
                    tmp.sprintf("%02x", stitch_data[pad].valid);
                    valid_line += "|"+tmp;
                    tmp.sprintf("%02x", stitch_data[pad].silent);
                    silence_line += "|"+tmp;
                    tmp.sprintf("%02x", stitch_data[pad].unchecked);
                    unchecked_line += "|"+tmp;
                    tmp.sprintf("%02x", stitch_data[pad].broken);
                    broken_line += "|"+tmp;
                }
                qInfo()<<"[L2B-16x0] Padding sweep stats for SI format:";
                qInfo()<<"[L2B-16x0] Padding:  "<<index_line;
                qInfo()<<"[L2B-16x0] Valid:    "<<valid_line;
                qInfo()<<"[L2B-16x0] Silent:   "<<silence_line;
                qInfo()<<"[L2B-16x0] Unchecked:"<<unchecked_line;
                qInfo()<<"[L2B-16x0] Broken:   "<<broken_line;
            }
#endif

            // Search for minimum number of broken blocks
            // Sort by number of valid blocks from max to min.
            // Sort by number of unchecked blocks from min to max.
            // Pick first after sort.
            // If its unchecked blocks count is less than allowed - set as detected padding.

            // Find minimum broken blocks within all passes.
            min_broken = stitch_data[0].broken;
            for(pad=0;pad<MAX_PADDING_SI;pad++)
            {
                if(stitch_data[pad].broken<min_broken)
                {
                    min_broken = stitch_data[pad].broken;
                }
            }
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Minimal count of BROKEN blocks:"<<min_broken;
            }
#endif

            // Assemble new vector with minimal broken block counts.
            for(pad=0;pad<MAX_PADDING_SI;pad++)
            {
                if((stitch_data[pad].broken==min_broken)&&(stitch_data[pad].valid>0))
                {
                    min_brk_data.push_back(stitch_data[pad]);
                }
            }

            if(min_brk_data.empty()==false)
            {
                // Sort vector by valid block count, then by unchecked blocks count.
                std::sort(min_brk_data.begin(), min_brk_data.end());

#ifdef DI_EN_DBG_OUT
                if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
                {
                    QString index_line, valid_line, silence_line, unchecked_line, broken_line, tmp;
                    for(pad=0;pad<min_brk_data.size();pad++)
                    {
                        tmp.sprintf("%02u", min_brk_data[pad].index);
                        index_line += "|"+tmp;
                        tmp.sprintf("%02x", min_brk_data[pad].valid);
                        valid_line += "|"+tmp;
                        tmp.sprintf("%02x", min_brk_data[pad].silent);
                        silence_line += "|"+tmp;
                        tmp.sprintf("%02x", min_brk_data[pad].unchecked);
                        unchecked_line += "|"+tmp;
                        tmp.sprintf("%02x", min_brk_data[pad].broken);
                        broken_line += "|"+tmp;
                    }
                    qInfo()<<"[L2B-16x0] Filtered stats:";
                    qInfo()<<"[L2B-16x0] Padding:  "<<index_line;
                    qInfo()<<"[L2B-16x0] Valid:    "<<valid_line;
                    qInfo()<<"[L2B-16x0] Silent:   "<<silence_line;
                    qInfo()<<"[L2B-16x0] Unchecked:"<<unchecked_line;
                    qInfo()<<"[L2B-16x0] Broken:   "<<broken_line;
                }
#endif
                // Check unchecked blocks limit.
                if(min_brk_data[0].unchecked<=MAX_BURST_UNCH_SI)
                {
                    // Check for silence.
                    if(min_brk_data[0].silent<MAX_BURST_SILENCE_SI)
                    {
                        // Not so many silent blocks.
                        // Check number of BROKEN blocks.
                        if(min_broken==0)
                        {
                            if(min_brk_data[0].valid>MIN_VALID_SI)
                            {
                                // No broken blocks, valid padding.
                                stitch_res = DS_RET_OK;
#ifdef DI_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[L2B-16x0] Detected valid padding:"<<min_brk_data[0].index;
                                }
#endif
                            }
                            else
                            {
                                stitch_res = DS_RET_NO_PAD;
#ifdef DI_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[L2B-16x0] Probably valid padding:"<<min_brk_data[0].index<<"but too little valid blocks, failed";
                                }
#endif
                            }
                        }
                        else
                        {
                            // Found some broken blocks, invalid padding.
                            stitch_res = DS_RET_BROKE;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[L2B-16x0] Detected BROKEN padding:"<<min_brk_data[0].index;
                            }
#endif
                        }
                        // Good padding is locked.
                        padding_lock = true;
                        // Save top padding.
                        pad_top = min_brk_data[0].index;
                        // Cut bottom dummy padding to the size.
                        if(pad_bottom>=pad_top)
                        {
                            pad_bottom = pad_bottom-pad_top;
                        }
                        else
                        {
                            pad_bottom = 0;
                        }
                        // Update padding stats.
                        updatePadStats(pad_top, true);
                    }
                    else
                    {
                        // Too many silent blocks.
                        stitch_res = DS_RET_SILENCE;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-16x0] Unable to find padding due to silence";
                        }
#endif
                    }
                }
#ifdef DI_EN_DBG_OUT
                else
                {
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Unable to find padding, too many unchecked blocks";
                    }
                }
#endif
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] No valid padding variants after filtering, padding search failed";
                }
            }
#endif
        }
    }
#ifdef DI_EN_DBG_OUT
    else
    {
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] No P codes, no data for padding detection";
        }
    }
#endif
    padding_queue.clear();
    // Check if valid padding was detected.
    if(padding_lock!=false)
    {
        // Found inter-block padding via stats (valid or broken).
        // Calculate target start of the interleave block.
        last_ofs = iblk_num*PCM16X0DataBlock::SI_INTERLEAVE_OFS;
        if(last_ofs<pad_top)
        {
            // Top padding should be removed and field data cut.
            // Calculate number of lines for the next block.
            last_ofs = (iblk_num+1)*PCM16X0DataBlock::SI_INTERLEAVE_OFS;
            // Calculate how many excess sub-lines are there in the field.
            last_ofs -= pad_top;
            // Remove top padding.
            pad_top = 0;
#ifdef DI_EN_DBG_OUT
            if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                qInfo()<<"[L2B-16x0] Too many lines in the field, top padding removed, field is cut by"<<last_ofs<<"lines at the top";
            }
#endif
            // Cut field from above.
            cutFieldTop(field_buf, f_size, last_ofs);
            // Re-save field length that could be updated in the [cutFieldTop()].
            field_subline_count = (*f_size);
        }
        else if(last_ofs>pad_top)
        {
            // Top padding should be increased.
            // Calculate number of lines for the previous block.
            last_ofs = (iblk_num-1)*PCM16X0DataBlock::SI_INTERLEAVE_OFS;
#ifdef DI_EN_DBG_OUT
            if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                if(last_ofs==0)
                {
                    qInfo()<<"[L2B-16x0] Top padding is spot on to align at the interleave block"<<(iblk_num+1);
                }
                else
                {
                    qInfo()<<"[L2B-16x0] Top padding should be increased by"<<last_ofs<<"lines to align at the interleave block"<<(iblk_num+1);
                }
            }
#endif
            // Add part of the previous empty block to detected padding.
            pad_top += last_ofs;
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                qInfo()<<"[L2B-16x0] Top padding is as it should be for the interleave block";
            }
        }
#endif

        // Convert line padding to sub-line padding.
        pad_top = pad_top*PCM16X0SubLine::SUBLINES_PER_LINE;
        // Calculate how many sub-lines should field data take.
        pad_bottom = SUBLINES_PF-pad_top;
        // Compare to number of sub-lines provided.
        if(pad_bottom>=field_subline_count)
        {
            // No excess sub-lines in the field, calculate bottom sub-lines padding.
            pad_bottom = pad_bottom-field_subline_count;
        }
        else
        {
            // There are excess sub-lines in the field.
            // Calculate how many sub-lines to cut from the bottom of the field.
            pad_bottom = field_subline_count-pad_bottom;
            // Reduce number of sub-lines in the field.
            field_subline_count = field_subline_count-pad_bottom;
#ifdef DI_EN_DBG_OUT
            if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                qInfo()<<"[L2B-16x0] Field overflow! Cutting"<<pad_bottom<<"bottom sub-lines from the data to get"<<field_subline_count<<"sub-lines in the field";
            }
#endif
            // Reset bottom padding.
            pad_bottom = 0;
        }
    }
    else
    {
        // Inter-block padding was not found (too many unchecked or no P-code available).
        // Check if zeroed Control Bit position was found.
        if(zero_ofs>=0)
        {
            // Some zeroed Control Bits were found, assume that were 2H bits.
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Trying to align data vertically by Control Bits...";
            }
#endif
            // Reset padding.
            pad_top = pad_bottom = 0;

            // Calculate target sub-line for Contol Bit (2H).
            last_ofs = PCM16X0SubLine::SUBLINES_PER_LINE+(iblk_num*SI_TRUE_INTERLEAVE);
            // Calculate delta between target and real offset.
            last_ofs = last_ofs-zero_ofs;
            if(last_ofs>0)
            {
                // Put difference into padding.
                pad_top = last_ofs;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Added"<<last_ofs<<"sub-lines for top padding";
                }
#endif
            }
            else if(last_ofs<0)
            {
                // Excess number of lines in the buffer.
                // Remove sign.
                last_ofs = (0-last_ofs);
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Too much data at the top of the field, cutting"<<last_ofs<<"sub-lines from top...";
                }
#endif
                // Cut field from above.
                cutFieldTop(field_buf, f_size, (last_ofs/PCM16X0SubLine::SUBLINES_PER_LINE));
                // Re-save field length that could be updated in the [cutFieldTop()].
                field_subline_count = (*f_size);
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Top padding is as it should be for the interleave block";
                }
            }
#endif
            // Calculate total number of sub-lines for the field.
            last_ofs = pad_top;                 // Add top padding.
            last_ofs += field_subline_count;    // Add PCM data.
            // Calculate delta between target and real count.
            last_ofs = SUBLINES_PF-last_ofs;
            if(last_ofs>0)
            {
                // Put difference into padding.
                pad_bottom = last_ofs;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Added"<<last_ofs<<"sub-lines for bottom padding";
                }
#endif
            }
            else if(last_ofs<0)
            {
                // Excess number of lines in the buffer.
                // Remove sign.
                last_ofs = (0-last_ofs);
                // Trim the field.
                field_subline_count -= last_ofs;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Data didn't fit in the field and was trimmed by"<<last_ofs<<"sub-lines";
                }
#endif
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Bottom padding is as it should be";
                }
            }
#endif
        }
        else
        {
            // Move all data to the bottom of the field,
            // calculate sub-line top padding to a standard.
            pad_bottom = 0;
            pad_top = SUBLINES_PF-field_subline_count;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] No vertical alignment data found, moving data to the bottom of the field, top padding:"<<pad_top;
            }
#endif
        }
    }
    // Export detected padding values, converting from sub-lines to lines.
    (*top_padding) = (pad_top/PCM16X0SubLine::SUBLINES_PER_LINE);
    (*bottom_padding) = (pad_bottom/PCM16X0SubLine::SUBLINES_PER_LINE);
    (*f_size) = field_subline_count;

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        log_line.sprintf("[L2B-16x0] Top padding: %u (%u), bottom padding: %u (%u)",
                         (*top_padding), pad_top, (*bottom_padding), pad_bottom);
        qInfo()<<log_line;
    }
    if((pad_top%PCM16X0SubLine::SUBLINES_PER_LINE)!=0)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Corrupted top padding!";
    }
    if((pad_bottom%PCM16X0SubLine::SUBLINES_PER_LINE)!=0)
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Corrupted bottom padding!";
    }
#endif

    return stitch_res;
}

//------------------------ Try to detect line coordinates of interleave blocks for SI format.
void PCM16X0DataStitcher::findSIDataAlignment()
{
    uint8_t odd_res, even_res;
    uint16_t top_pad, bottom_pad;

    even_res = DS_RET_NO_DATA;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-16x0] -------------------- Padding detection for SI format starting...";
    }
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-16x0] -------------------- Processing odd field...";
    }
#endif

    // DEBUG hook
    /*
    if(frasm_f1.frame_number==282)
    {
        // pcm_04_cut.avi
        qInfo()<<"DBG";
    }*/

    // Preset field order.
    if(preset_field_order==FrameAsmDescriptor::ORDER_BFF)
    {
        frasm_f1.presetBFF();
    }
    else
    {
        frasm_f1.presetTFF();
    }

    // Detect padding of odd field.
    odd_res = findSIPadding(&frame1_odd, &frasm_f1.odd_data_lines, &top_pad, &bottom_pad);
    if(odd_res==DS_RET_OK)
    {
        frasm_f1.padding_ok = true;
        frasm_f1.silence = false;
    }
    else
    {
        frasm_f1.padding_ok = false;
        if(odd_res==DS_RET_SILENCE)
        {
            frasm_f1.silence = true;
        }
        else
        {
            frasm_f1.silence = false;
        }
    }
    // Save top and bottom padding.
    frasm_f1.odd_top_padding = top_pad;
    frasm_f1.odd_bottom_padding = bottom_pad;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-16x0] -------------------- Processing even field...";
    }
#endif
    // Detect padding of even field.
    even_res = findSIPadding(&frame1_even, &frasm_f1.even_data_lines, &top_pad, &bottom_pad);
    if(even_res==DS_RET_OK)
    {
        frasm_f1.padding_ok = true&&frasm_f1.padding_ok;
        frasm_f1.silence = false||frasm_f1.silence;
    }
    else
    {
        frasm_f1.padding_ok = false;
        if(odd_res==DS_RET_SILENCE)
        {
            frasm_f1.silence = true;
        }
        else
        {
            frasm_f1.silence = false||frasm_f1.silence;
        }
    }
    // Save top and bottom padding.
    frasm_f1.even_top_padding = top_pad;
    frasm_f1.even_bottom_padding = bottom_pad;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        QString log_line;
        qInfo()<<"[L2B-16x0] ----------- Padding results:";
        log_line.sprintf("[L2B-16x0] Frame %u odd field padding: ", frasm_f1.frame_number);
        if(odd_res==DS_RET_OK)
        {
            log_line += "OK";
        }
        else if(odd_res==DS_RET_SILENCE)
        {
            log_line += "Silence";
        }
        else
        {
            log_line += "Fail";
        }
        log_line += " (top: "+QString::number(frasm_f1.odd_top_padding);
        log_line += ", lines: "+(QString::number(frasm_f1.odd_data_lines/PCM16X0SubLine::SUBLINES_PER_LINE));
        log_line += " (sub-lines: "+QString::number(frasm_f1.odd_data_lines);
        log_line += "), bottom: "+QString::number(frasm_f1.odd_bottom_padding)+")";
        qInfo()<<log_line;
        log_line.sprintf("[L2B-16x0] Frame %u even field padding: ", frasm_f1.frame_number);
        if(even_res==DS_RET_OK)
        {
            log_line += "OK";
        }
        else if(even_res==DS_RET_SILENCE)
        {
            log_line += "Silence";
        }
        else
        {
            log_line += "Fail";
        }
        log_line += " (top: "+QString::number(frasm_f1.even_top_padding);
        log_line += ", lines: "+(QString::number(frasm_f1.even_data_lines/PCM16X0SubLine::SUBLINES_PER_LINE));
        log_line += " (sub-lines: "+QString::number(frasm_f1.even_data_lines);
        log_line += "), bottom: "+QString::number(frasm_f1.even_bottom_padding)+")";
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Try to stitch EI format fields with provided sub-line padding, collect stats.
uint8_t PCM16X0DataStitcher::tryEIPadding(uint16_t padding, FieldStitchStats *stitch_stats)
{
    bool suppress_log, even_block, run_lock;
    uint16_t valid_burst_count, silence_burst_count, uncheck_burst_count, brk_burst_count;
    uint16_t valid_burst_max, silence_burst_max, uncheck_burst_max, brk_burst_max;
    size_t buf_size;

    suppress_log = !((log_level&LOG_PADDING_BLOCK)!=0);

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING_LINE)!=0)||((log_level&LOG_PADDING_BLOCK)!=0))
    {
        qInfo()<<"[L2B-16x0] Processing"<<padding_queue.size()<<"sub-lines, checking padding ="<<padding;
    }
#endif

    // Get final buffer size.
    buf_size = padding_queue.size();
    if(buf_size<EI_TRUE_INTERLEAVE)
    {
#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_PADDING_LINE)!=0)||((log_level&LOG_PADDING_BLOCK)!=0))
        {
            qInfo()<<"[L2B-16x0] Not enough lines to perform padding "<<buf_size;
        }
#endif
        return DS_RET_NO_DATA;
    }

#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_PADDING_LINE)!=0)
    {
        // Dump test lines array.
        for(size_t index=0;index<padding_queue.size();index++)
        {
            qInfo()<<"[L2B-16x0]"<<QString::fromStdString(padding_queue[index].dumpContentString());
        }
    }
#endif

    buf_size = 0;
    even_block = run_lock = false;
    valid_burst_count = silence_burst_count = uncheck_burst_count = brk_burst_count = 0;
    valid_burst_max = silence_burst_max = uncheck_burst_max = brk_burst_max = 0;

    // Run deinterleaving and error-detection on combined and padded buffer.
    while(((LINES_PF*2*2)+buf_size+1)<padding_queue.size())
    {
        // Fill up data block, performing de-interleaving and error-correction.
        if(pad_checker.processBlock(buf_size, even_block)!=PCM16X0Deinterleaver::DI_RET_OK)
        {
            // No data left in the buffer or some other error while de-interleaving.
#ifdef DI_EN_DBG_OUT
            if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                qInfo()<<"[L2B-16x0] No more data in the buffer at line offset"<<buf_size<<"buffer size:"<<padding_queue.size();
            }
#endif
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
            // Found block with checkable and valid data.
            valid_burst_count++;
        }
        else
        {
            // Current block doesn't have any checkable data.
            if(valid_burst_count>valid_burst_max)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Updating 'valid' limit from"<<valid_burst_max<<"to"<<valid_burst_count<<"at pass"<<buf_size;
                }
#endif
                // Update counter for maximum valid blocks in a row.
                valid_burst_max = valid_burst_count;
            }
        }
#ifdef DI_EN_DBG_OUT
        if((suppress_log==false)&&((log_level&LOG_PROCESS)!=0))
        {
            qInfo()<<"[L2B-16x0]"<<QString::fromStdString(padding_block.dumpContentString());
        }
#endif
        // Is there too much silence?
        // P-check will not have any use on silence.
        if(padding_block.isSilent()!=false)
        {
            // Found block with too much silence.
            silence_burst_count++;
            if(silence_burst_count>=MAX_BURST_SILENCE_EI)
            {
                // Reset valid block counter.
                valid_burst_count = 0;
            }
        }
        else
        {
            // Current data block is not silent.
            if(silence_burst_count>silence_burst_max)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Updating 'silence' limit from"<<silence_burst_max<<"to"<<silence_burst_count<<"at pass"<<buf_size;
                }
#endif
                // Update longest burst of silence.
                silence_burst_max = silence_burst_count;
            }
            // Reset counter if burst of silence ended.
            silence_burst_count = 0;
        }
        // Check for "uncheckable" blocks: invalid, with no P, with P-corrected samples.
        // P-correction can "correct" samples from broken data blocks, masking incorrectly assembled blocks.
        if((padding_block.canForceCheck()==false)||(padding_block.isDataFixedByP()!=false))
        {
            // Block can not be checked for validity.
            uncheck_burst_count++;
            if(uncheck_burst_count>MAX_BURST_UNCH_EI)
            {
                // Reset valid block counter.
                valid_burst_count = 0;
            }
        }
        else
        {
            // Current data block can be checked for validity.
            if(uncheck_burst_count>uncheck_burst_max)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Updating 'unchecked' limit from"<<uncheck_burst_max<<"to"<<uncheck_burst_count<<"at pass"<<buf_size;
                }
#endif
                // Update longest burst of unchecked blocks.
                uncheck_burst_max = uncheck_burst_count;
            }
            // Reset counter if burst of unchecked blocks ended.
            uncheck_burst_count = 0;
        }
        // Check for BROKEN data block.
        if(padding_block.isDataBroken()!=false)
        {
            // Data in the block is BROKEN (no CRC marks but parity error).
            // Current padding run is bad.
            brk_burst_count++;
            if(brk_burst_count>=MAX_BURST_BROKEN)
            {
                // Reset valid block counter.
                valid_burst_count = 0;
            }
        }
        else
        {
            // Current data block is not broken.
            if(brk_burst_count>brk_burst_max)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Updating 'BROKEN' limit from"<<brk_burst_max<<"to"<<brk_burst_count<<"at pass"<<buf_size;
                }
#endif
                // Update longest burst of BROKEN blocks.
                brk_burst_max = brk_burst_count;
            }
            // Reset counter if burst of BROKEN blocks ended.
            brk_burst_count = 0;
        }
        // Shift one line down in the buffer.
        buf_size++;
        // Alternate order from block to block inside one interleave block.
        even_block = !even_block;
    }
    // Update post-cycle counters if required.
    if(valid_burst_count>valid_burst_max)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Updating 'valid' limit from"<<valid_burst_max<<"to"<<valid_burst_count<<"at pass"<<buf_size;
        }
#endif
        valid_burst_max = valid_burst_count;
    }
    if(silence_burst_count>silence_burst_max)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Updating 'silence' limit from"<<silence_burst_max<<"to"<<silence_burst_count<<"at pass"<<buf_size;
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
            qInfo()<<"[L2B-16x0] Updating 'unchecked' limit from"<<uncheck_burst_max<<"to"<<uncheck_burst_count<<"at pass"<<buf_size;
        }
#endif
        // Update longest burst of unchecked blocks.
        uncheck_burst_max = uncheck_burst_count;
    }
    if(brk_burst_count>brk_burst_max)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Updating 'BROKEN' limit from"<<brk_burst_max<<"to"<<brk_burst_count<<"at pass"<<buf_size;
        }
#endif
        // Update longest burst of BROKEN blocks.
        brk_burst_max = brk_burst_count;
    }

    // Output stats for the seam.
    if((stitch_stats!=NULL)&&(run_lock!=false))
    {
        stitch_stats->index = padding;
        stitch_stats->valid = valid_burst_max;
        stitch_stats->silent = silence_burst_max;
        stitch_stats->unchecked = uncheck_burst_max;
        stitch_stats->broken = brk_burst_max;
    }

    // Check for too many unchecked blocks.
    if(uncheck_burst_max>MAX_BURST_UNCH_EI)
    {
        // Too many unchecked blocks.
        return DS_RET_NO_PAD;
    }
    // Check is any good blocks were found.
    if(valid_burst_max==0)
    {
        // No good blocks.
        return DS_RET_NO_PAD;
    }
    // Check for too much silence.
    if(silence_burst_max>MAX_BURST_SILENCE_EI)
    {
        // Too much silence.
        return DS_RET_SILENCE;
    }
    // Check for BROKEN data blocks.
    if(brk_burst_max>=MAX_BURST_BROKEN)
    {
        // BROKEN data.
        return DS_RET_BROKE;
    }
    // Padding is OK.
    return DS_RET_OK;
}

//------------------------ Find sub-line padding to properly (without parity errors) stitch two fields for EI format.
uint8_t PCM16X0DataStitcher::findEIPadding(uint8_t field_order)
{
    bool suppress_log, padding_lock;
    uint8_t ext_di_log_lvl, stitch_res, field_padding;
    uint16_t pad, min_broken;
    uint16_t last_pad_line;
    PCM16X0SubLine empty_line;

    QElapsedTimer dbg_timer;

    stitch_res = DS_RET_NO_PAD;
    field_padding = 0;
    suppress_log = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    std::vector<PCM16X0SubLine> *field1;
    std::vector<PCM16X0SubLine> *field2;
    uint16_t f1_size, f2_size;

    if(field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        field1 = &frame1_odd;
        field2 = &frame1_even;
        f1_size = frasm_f1.odd_data_lines;
        f2_size = frasm_f1.even_data_lines;
    }
    else
    {
        field1 = &frame1_even;
        field2 = &frame1_odd;
        f1_size = frasm_f1.even_data_lines;
        f2_size = frasm_f1.odd_data_lines;
    }

    // Calculate default padding.
    // Set default padding, anchoring data to the bottom of the field.
    frasm_f1.odd_bottom_padding = 0;
    frasm_f1.even_bottom_padding = 0;
    // Assume default padding according to NTSC standards.
    frasm_f1.odd_top_padding = (SUBLINES_PF-frasm_f1.odd_data_lines)/PCM16X0SubLine::SUBLINES_PER_LINE-frasm_f1.odd_bottom_padding;
    frasm_f1.even_top_padding = (SUBLINES_PF-frasm_f1.even_data_lines)/PCM16X0SubLine::SUBLINES_PER_LINE-frasm_f1.even_bottom_padding;

    ext_di_log_lvl = 0;
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_DEINTERLEAVE)!=0))
    {
        ext_di_log_lvl |= PCM16X0Deinterleaver::LOG_PROCESS;
    }
    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_ERROR_CORR)!=0))
    {
        ext_di_log_lvl |= PCM16X0Deinterleaver::LOG_ERROR_CORR;
    }

    dbg_timer.start();

    padding_lock = false;

    // Check if P-code available.
    if(enable_P_code!=false)
    {
        // Dump two fields sequently into one array, inserting padding in process.
        padding_queue.clear();
        // Copy data from field 1.
        for(uint16_t index=0;index<f1_size;index++)
        {
            // Fill from the top of [field1] to the bottom.
            padding_queue.push_back((*field1)[index]);
        }
        // Save frame parameters to carry those into padding.
        empty_line.line_number = (*field1)[f1_size-1].line_number;
        empty_line.frame_number = (*field1)[f1_size-1].frame_number;
        // Save last padding position.
        last_pad_line = padding_queue.size();

        // Setup deinterleaver.
        pad_checker.setInput(&padding_queue);
        pad_checker.setOutput(&padding_block);
        pad_checker.setLogLevel(ext_di_log_lvl);
        pad_checker.setIgnoreCRC(ignore_CRC);
        pad_checker.setForceParity(true);
        pad_checker.setPCorrection(true);
        pad_checker.setEIFormat();

        // Initialize stats data.
        std::vector<FieldStitchStats> stitch_data, min_brk_data;
        stitch_data.resize(MAX_PADDING_EI);
        min_brk_data.reserve(MAX_PADDING_EI);

        // Detect padding between fields, verifying parity while assembling data blocks with padding between fields.
        // Padding cycle.
        for(pad=0;pad<MAX_PADDING_EI;pad++)
        {
            //qInfo()<<"pad"<<pad<<dbg_timer.nsecsElapsed()/1000;

            // Refill sub-lines from field 2.
            for(uint16_t index=0;index<f2_size;index++)
            {
                // Fill from the top of [field2] to the bottom.
                padding_queue.push_back((*field2)[index]);
            }

            // Try to stitch fields with set padding and collect stats into [stitch_data].
            tryEIPadding(pad, &stitch_data[pad]);

            // Remove sub-lines up to last padded.
            while(padding_queue.size()>last_pad_line)
            {
                padding_queue.pop_back();
            }
            // Add one more padding line.
            empty_line.line_number += 2;    // Simulate interlaced line numbering.
            for(uint8_t part=0;part<PCM16X0SubLine::SUBLINES_PER_LINE;part++)
            {
                empty_line.line_part = part;
                padding_queue.push_back(empty_line);
            }
            // Save new padding position.
            last_pad_line = padding_queue.size();
        }

#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
        {
            QString index_line, valid_line, silence_line, unchecked_line, broken_line, tmp;
            for(pad=0;pad<MAX_PADDING_EI;pad++)
            {
                tmp.sprintf("%03u", stitch_data[pad].index);
                index_line += "|"+tmp;
                tmp.sprintf("%03x", stitch_data[pad].valid);
                valid_line += "|"+tmp;
                tmp.sprintf("%03x", stitch_data[pad].silent);
                silence_line += "|"+tmp;
                tmp.sprintf("%03x", stitch_data[pad].unchecked);
                unchecked_line += "|"+tmp;
                tmp.sprintf("%03x", stitch_data[pad].broken);
                broken_line += "|"+tmp;
            }
            qInfo()<<"[L2B-16x0] Padding sweep stats for EI format:";
            qInfo()<<"[L2B-16x0] Padding:  "<<index_line;
            qInfo()<<"[L2B-16x0] Valid:    "<<valid_line;
            qInfo()<<"[L2B-16x0] Silent:   "<<silence_line;
            qInfo()<<"[L2B-16x0] Unchecked:"<<unchecked_line;
            qInfo()<<"[L2B-16x0] Broken:   "<<broken_line;
        }
#endif

        // Search for minimum number of broken blocks
        // Sort by number of valid blocks from max to min.
        // Sort by number of unchecked blocks from min to max.
        // Pick first after sort.
        // If its unchecked blocks count is less than allowed - set as detected padding.
        // Find minimum broken blocks within all passes.
        min_broken = stitch_data[0].broken;
        for(pad=0;pad<MAX_PADDING_EI;pad++)
        {
            if(stitch_data[pad].broken<min_broken)
            {
                min_broken = stitch_data[pad].broken;
            }
        }
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Minimal count of BROKEN blocks:"<<min_broken;
        }
#endif

        // Assemble new vector with minimal broken block counts.
        for(pad=0;pad<MAX_PADDING_EI;pad++)
        {
            if((stitch_data[pad].broken==min_broken)&&(stitch_data[pad].valid)>0)
            {
                min_brk_data.push_back(stitch_data[pad]);
            }
        }

        if(min_brk_data.empty()==false)
        {
            // Sort vector by valid block count, then by unchecked blocks count.
            std::sort(min_brk_data.begin(), min_brk_data.end());

#ifdef DI_EN_DBG_OUT
            if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
            {
                QString index_line, valid_line, silence_line, unchecked_line, broken_line, tmp;
                for(pad=0;pad<min_brk_data.size();pad++)
                {
                    tmp.sprintf("%03u", min_brk_data[pad].index);
                    index_line += "|"+tmp;
                    tmp.sprintf("%03x", min_brk_data[pad].valid);
                    valid_line += "|"+tmp;
                    tmp.sprintf("%03x", min_brk_data[pad].silent);
                    silence_line += "|"+tmp;
                    tmp.sprintf("%03x", min_brk_data[pad].unchecked);
                    unchecked_line += "|"+tmp;
                    tmp.sprintf("%03x", min_brk_data[pad].broken);
                    broken_line += "|"+tmp;
                }
                qInfo()<<"[L2B-16x0] Filtered stats:";
                qInfo()<<"[L2B-16x0] Padding:  "<<index_line;
                qInfo()<<"[L2B-16x0] Valid:    "<<valid_line;
                qInfo()<<"[L2B-16x0] Silent:   "<<silence_line;
                qInfo()<<"[L2B-16x0] Unchecked:"<<unchecked_line;
                qInfo()<<"[L2B-16x0] Broken:   "<<broken_line;
            }
#endif

            // Check unchecked blocks limit.
            if(min_brk_data[0].unchecked<=MAX_BURST_UNCH_EI)
            {
                // Check for silence.
                if(min_brk_data[0].silent<MAX_BURST_SILENCE_EI)
                {
                    // Not so many silent blocks.
                    // Check number of BROKEN blocks.
                    if(min_broken==0)
                    {
                        if(min_brk_data[0].valid>MIN_VALID_EI)
                        {
                            // No broken blocks, valid padding.
                            stitch_res = DS_RET_OK;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[L2B-16x0] Detected valid padding:"<<min_brk_data[0].index;
                            }
#endif
                        }
                        else
                        {
                            stitch_res = DS_RET_NO_PAD;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[L2B-16x0] Probably valid padding:"<<min_brk_data[0].index<<"but too little valid blocks, failed";
                            }
#endif
                        }
                    }
                    else
                    {
                        // Found some broken blocks, invalid padding.
                        stitch_res = DS_RET_BROKE;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-16x0] Detected BROKEN padding:"<<min_brk_data[0].index;
                        }
#endif
                    }
                    // Good padding is locked.
                    padding_lock = true;
                    // Save inter-frame padding.
                    field_padding = min_brk_data[0].index;
                    // Update padding stats.
                    updatePadStats(field_padding, true);
                }
                else
                {
                    // Too many silent blocks.
                    stitch_res = DS_RET_SILENCE;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Unable to find padding due to silence";
                    }
#endif
                }
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Unable to find padding, too many unchecked blocks";
                }
            }
#endif
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] No valid padding variants after filtering, padding search failed!";
            }
        }
#endif
    }
#ifdef DI_EN_DBG_OUT
    else
    {
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] No P codes, no data for inter-frame padding detection";
        }
    }
#endif
    padding_queue.clear();

    // Check if padding between field is found.
    if(padding_lock!=false)
    {
        // Check if pre-calculated padding matches detected field padding.
        if(field_order==FrameAsmDescriptor::ORDER_TFF)
        {
            // TFF field order.
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Detected inter-frame padding"<<field_padding<<"with TFF field order";
            }
#endif
            // Store padding as bottom for first field (odd for TFF).
            frasm_f1.odd_bottom_padding = field_padding;
            // Remove top padding for second field (even for TFF).
            frasm_f1.even_top_padding = 0;
        }
        else
        {
            // BFF field order.
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Detected inter-frame padding"<<field_padding<<"with BFF field order";
            }
#endif
            // Store padding as bottom for first field (even for TFF).
            frasm_f1.even_bottom_padding = field_padding;
            // Remove top padding for second field (odd for TFF).
            frasm_f1.odd_top_padding = 0;
        }
    }

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        if((stitch_res==DS_RET_OK)||(stitch_res==DS_RET_SILENCE))
        {
            QString log_line;
            log_line.sprintf("[L2B-16x0] Inter-frame padding: %u", field_padding);
            qInfo()<<log_line;
        }
    }
#endif

    return stitch_res;
}

//------------------------ Re-calculate and recombine field paddings for EI format after inter-frame padding was detected.
void PCM16X0DataStitcher::conditionEIFramePadding(std::vector<PCM16X0SubLine> *field1, std::vector<PCM16X0SubLine> *field2, uint16_t *f1_size, uint16_t *f2_size,
                                                  uint16_t *f1_top_pad, uint16_t *f1_bottom_pad, uint16_t *f2_top_pad, uint16_t *f2_bottom_pad)
{
    bool suppress_log, pos_lock;
    uint8_t iblk_num;
    uint16_t inter_frame_pad;
    int16_t zero_ofs, last_ofs;

    pos_lock = false;
    suppress_log = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    // Save inter-frame padding.
    inter_frame_pad = (*f1_bottom_pad);
    // Detect location of Control Bits (3rd H with EI format marker should be "0") in the second field.
    zero_ofs = findZeroControlBitOffset(field2, (*f2_size), false);
    if(zero_ofs>=0)
    {
        // "0" Control Bit was found. Probably it's the 3rd (MODE) one.
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Second field zeroed Control Bit offset:"<<(zero_ofs/PCM16X0SubLine::SUBLINES_PER_LINE)<<"("<<zero_ofs<<")";
        }
#endif
        pos_lock = true;
        // Calculate frame data padding from the bottom of the frame (should be stored in bottom padding of the second field).
        // Try to guess number of first interleave block in the buffer
        // by position of the zeroed Control Bit in the source frame.
        iblk_num = estimateBlockNumber(field2, *f2_size, zero_ofs);
        // Calculate how many sub-lines after Contol Bit from the last interleave block is in the buffer.
        zero_ofs = (*f2_size)-zero_ofs;
        // Calculate delta between target number of sub-lines from Contol Bit (3H) to the end of the interleave block and buffer.
        last_ofs = (PCM16X0DataBlock::SI_INTERLEAVE_OFS-2)*PCM16X0SubLine::SUBLINES_PER_LINE-zero_ofs;
        // Equlize buffer to full interleave block length.
        if(last_ofs<0)
        {
            // Excess number of lines in the buffer.
            // Remove sign.
            last_ofs = (0-last_ofs);
            // Trim field.
            (*f2_size) -= last_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Last block in the buffer was"<<last_ofs<<"sub-lines longer, trimmed";
            }
#endif
        }
        else if(last_ofs>0)
        {
            // Put difference into padding.
            (*f2_bottom_pad) += (last_ofs/PCM16X0SubLine::SUBLINES_PER_LINE);
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Last block in the buffer was"<<last_ofs<<"sub-lines shorter, added bottom padding of"<<(*f2_bottom_pad)<<"lines";
            }
#endif
        }
        // Calculate sub-line offset of the last sub-line of the block.
        last_ofs = (PCM16X0DataBlock::INT_BLK_PER_FIELD-iblk_num-1)*SI_TRUE_INTERLEAVE;
        (*f2_bottom_pad) += (last_ofs/PCM16X0SubLine::SUBLINES_PER_LINE);
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(last_ofs>0)
            {
                qInfo()<<"[L2B-16x0] Adding"<<last_ofs<<"sub-lines at the bottom to fill the second field";
            }
            qInfo()<<"[L2B-16x0] Calculated bottom padding for second field:"<<(*f2_bottom_pad)<<"lines";
        }
#endif
        // Calculate top padding.
        last_ofs = LINES_PF-(*f2_size)/PCM16X0SubLine::SUBLINES_PER_LINE;   // Subtract PCM data lines.
        last_ofs -= (*f2_bottom_pad);                                       // Subtract calculated bottom padding.
        if(last_ofs<0)
        {
            // Data doesn't fit in the second field with those Control Bit coordinates!
            // Remove sign.
            last_ofs = (0-last_ofs);
            /*if(last_ofs==1)
            {
                // Maybe that zeroed control bit was Audio/Data (4th H), shift downwards one line.
                (*f2_bottom_pad) -= 1;
                // Just fits without additional padding.
                last_ofs = 0;   // Will be put in [(*f2_top_pad)] later.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Too many lines for second field, shifting one line down assuming zero Data Control Bit (4H)...";
                }
#endif
            }
            else*/
            {
                // Should never happen!
                // But if it did that means that top of the frame was cropped,
                // that caused shift of the last interleave block up.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Data doesn't fit by"<<last_ofs<<"lines into second field, miscalculated Control Bit offset!";
                }
#endif
                // Calculate by how many interleave blocks calculation was missed.
                zero_ofs = ((last_ofs/PCM16X0DataBlock::SI_INTERLEAVE_OFS)+1);
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Control Bit offset seems to be off by"<<zero_ofs<<"interleave blocks";
                }
#endif
                zero_ofs = zero_ofs*PCM16X0DataBlock::SI_INTERLEAVE_OFS;
                // Calculate new bottom padding to shift data to the bottom.
                last_ofs = (*f2_bottom_pad)-zero_ofs;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Reducing bottom padding by"<<zero_ofs<<"lines";
                }
#endif
                if(last_ofs<0)
                {
                    // TODO Logic error 3
                    qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error 3! Bottom padding correction failed with"<<last_ofs<<"offset at frame"<<frasm_f1.frame_number;
                    (*f2_top_pad) = (*f2_bottom_pad) = 0;
                    pos_lock = false;
                }
                else
                {
                    // Save re-calculated bottom padding for second field.
                    (*f2_bottom_pad) = last_ofs;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Re-calculated bottom padding for second field:"<<(*f2_bottom_pad)<<"lines";
                    }
#endif
                    // Re-calculate top padding.
                    last_ofs = LINES_PF-(*f2_size)/PCM16X0SubLine::SUBLINES_PER_LINE;   // Subtract PCM data lines.
                    last_ofs -= (*f2_bottom_pad);                                       // Subtract calculated bottom padding.
                }
            }
        }
        // Re-check calculated top padding (might be corrected if it was negative).
        if(last_ofs>inter_frame_pad)
        {
            // Calculated top padding for the second field is more than the whole inter-frame padding!
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated second field top padding of"<<last_ofs<<"lines is more than inter-frame padding of"<<inter_frame_pad<<"lines! Resetting second field padding...";
            }
#endif
            // Try to compensate for probably wrong Control Bit position.
            if((last_ofs-inter_frame_pad)<2)
            {
                (*f2_top_pad) = inter_frame_pad;
                (*f2_bottom_pad) += (last_ofs-inter_frame_pad);
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Shifting second field up by"<<(last_ofs-inter_frame_pad)<<"lines.";
                    qInfo()<<"[L2B-16x0] Re-calculated bottom padding for second field:"<<(*f2_bottom_pad)<<"lines.";
                    qInfo()<<"[L2B-16x0] Calculated top padding for first field:"<<(*f2_top_pad)<<"lines.";
                }
#endif
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Resetting second field padding...";
                }
#endif
                // Reset top padding for the second field to preserve detected field padding.
                (*f2_top_pad) = (*f2_bottom_pad) = 0;
                pos_lock = false;
            }
        }
        else if(pos_lock!=false)
        {
            // Save padding for the top of the second field.
            (*f2_top_pad) = last_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated top padding for second field:"<<(*f2_top_pad)<<"lines";
            }
#endif
        }
    }
#ifdef DI_EN_DBG_OUT
    else
    {
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Unable to detect position of Control Bits in the second field!";
        }
    }
#endif
    // Check if data position was locked in the second field.
    if(pos_lock!=false)
    {
        // Top and bottom paddings for the second field are done,
        // inter-frame padding is detected.
        // Calculate how many lines of inter-frame padding should be left in the first field.
        zero_ofs = inter_frame_pad-(*f2_top_pad);
        (*f1_bottom_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Calculated bottom padding for first field:"<<(*f1_bottom_pad)<<"lines";
            if(((*f1_bottom_pad)>0)&&((*f2_top_pad)>0))
            {
                qInfo()<<"[L2B-16x0] Inter-frame padding of"<<inter_frame_pad<<"lines is split into"<<(*f1_bottom_pad)<<"+"<<(*f2_top_pad);
            }
        }
#endif
        // Calculate padding for the top of the first field (the last one).
        zero_ofs = ((*f1_size)+(*f2_size))/PCM16X0SubLine::SUBLINES_PER_LINE;   // Total number of lines with PCM data from both fields.
        zero_ofs += (*f1_bottom_pad)+(*f2_top_pad);                     // Plus number of lines to stitch fields together.
        zero_ofs += (*f2_bottom_pad);                                   // Plus calculated padding for second field.
        zero_ofs = (2*LINES_PF)-zero_ofs;             // Delta from standard number of lines per frame.
        if(zero_ofs<0)
        {
            // Data doesn't fit in a frame!
            // Scrap padding for the second field.
            (*f1_top_pad) = (*f1_bottom_pad) = (*f2_top_pad) = (*f2_bottom_pad) = 0;
            pos_lock = false;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Too many lines for the frame! All padding is reset.";
            }
#endif
        }
        else
        {
            // Save padding for the top of the first field.
            (*f1_top_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated top padding for first field:"<<(*f1_top_pad)<<"lines";
            }
#endif
        }
    }
    // Check if there was no luck with locking data position by second field.
    if(pos_lock==false)
    {
        // Detect location of Control Bits (3rd H with EI format marker should be "0") in the first field.
        zero_ofs = findZeroControlBitOffset(field1, (*f1_size), false);
        if(zero_ofs>=0)
        {
            // "0" Control Bit was found.
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] First field control bit offset:"<<(zero_ofs/PCM16X0SubLine::SUBLINES_PER_LINE)<<"("<<zero_ofs<<")";
            }
#endif
            pos_lock = true;
            uint8_t iblk_cnt;
            // Calculate number of interleave blocks with Control Bits in the buffer.
            iblk_cnt = zero_ofs/SI_TRUE_INTERLEAVE;      // /105
            zero_ofs -= (iblk_cnt*SI_TRUE_INTERLEAVE);   // Sub-lines offset of the first zeroed Control Bit in the interleave block.
            zero_ofs = (SUBLINES_PF+(2*PCM16X0SubLine::SUBLINES_PER_LINE))-zero_ofs;      // Number of sub-lines to add from the top.
            // Convert from sub-lines to lines.
            zero_ofs = (zero_ofs/PCM16X0SubLine::SUBLINES_PER_LINE);
            // Save first field top padding.
            (*f1_top_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated top padding for first field:"<<(*f1_top_pad)<<"lines";
            }
#endif
            // Calculate bottom padding for first field.
            zero_ofs = LINES_PF-(*f1_top_pad);
            zero_ofs -= (*f1_size)/PCM16X0SubLine::SUBLINES_PER_LINE;
            if(zero_ofs<0)
            {
                qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error 1! First field bottom padding calculated to be"<<zero_ofs<<"at frame"<<frasm_f1.frame_number;
                pos_lock = false;
                // TODO Logic error 1
            }
            else
            {
                // Save bottom padding for first field.
                (*f1_bottom_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Calculated bottom padding for first field:"<<(*f1_bottom_pad)<<"lines";
                }
#endif
                // Calculate how many lines of inter-frame padding should be moved to the second field.
                zero_ofs = inter_frame_pad-(*f1_bottom_pad);
                if(zero_ofs<0)
                {
                    qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error 2! Second field top padding calculated to be"<<zero_ofs<<"at frame"<<frasm_f1.frame_number;
                    pos_lock = false;
                    // TODO Logic error 2
                }
                else
                {
                    // Save top padding for second field.
                    (*f2_top_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Calculated top padding for second field:"<<(*f2_top_pad);
                        if(((*f1_bottom_pad)>0)&&((*f2_top_pad)>0))
                        {
                            qInfo()<<"[L2B-16x0] Inter-frame padding of"<<inter_frame_pad<<"lines is split into"<<(*f1_bottom_pad)<<"+"<<(*f2_top_pad);
                        }
                    }
#endif
                    // Calculate bottom padding for the second field.
                    zero_ofs = (*f2_size)/PCM16X0SubLine::SUBLINES_PER_LINE;    // Number of lines with PCM data in second field.
                    zero_ofs += (*f2_top_pad);                          // Plus top padding of the second field, calculated above from padding.
                    zero_ofs = LINES_PF-zero_ofs;     // Delta from standard number of lines per field.
                    if(zero_ofs<0)
                    {
                        // Data still doesn't fit in the frame!
                        // Remove bottom padding for second field.
                        (*f2_bottom_pad) = 0;
                        // Trim data in the second field.
                        (*f2_size) -= ((0-zero_ofs)*PCM16X0SubLine::SUBLINES_PER_LINE);
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-16x0] Data didn't fit in the second field and was trimmed by"<<(0-zero_ofs)<<"lines";
                        }
#endif
                    }
                    else
                    {
                        // Save padding for the bottom of the second field.
                        (*f2_bottom_pad) = zero_ofs;
                    }
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] Calculated bottom padding for second field:"<<(*f2_bottom_pad)<<"lines";
                    }
#endif
                }
            }
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Unable to detect position of Control Bits in the first field!";
            }
        }
#endif
    }
    // Check if data position was not locked by first frame either.
    if(pos_lock==false)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] No data position was set. Dividing inter-frame padding equally...";
        }
#endif
        // Divide inter-frame padding ~equally between two fields.
        zero_ofs = inter_frame_pad/2;
        (*f2_top_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Calculated top padding for second field:"<<(*f2_top_pad)<<"lines";
        }
#endif
        // Calculate bottom padding for the first field.
        zero_ofs = inter_frame_pad*PCM16X0SubLine::SUBLINES_PER_LINE;
        zero_ofs -= ((*f2_top_pad)*PCM16X0SubLine::SUBLINES_PER_LINE);
        (*f1_bottom_pad) = zero_ofs/PCM16X0SubLine::SUBLINES_PER_LINE;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Calculated bottom padding for first field:"<<(*f1_bottom_pad)<<"lines";
        }
#endif
        // Calculate top padding for the first field.
        zero_ofs = (*f1_size)/PCM16X0SubLine::SUBLINES_PER_LINE;    // Number of lines with PCM data in first field.
        zero_ofs += (*f1_bottom_pad);                       // Plus bottom padding of the first field, calculated above.
        zero_ofs = LINES_PF-zero_ofs;     // Delta from standard number of lines per field.
        if(zero_ofs<0)
        {
            // Data still doesn't fit in the frame!
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Too many lines for the first field! All padding is reset.";
            }
#endif
            // Drop top padding for the first field.
            (*f1_top_pad) = 0;
            // Calculate bottom padding for the first field.
            zero_ofs = (*f1_size)/PCM16X0SubLine::SUBLINES_PER_LINE;    // Number of lines with PCM data in first field.
            zero_ofs = LINES_PF-zero_ofs;     // Delta from standard number of lines per field.
            (*f1_bottom_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated bottom padding for first field:"<<(*f1_bottom_pad)<<"lines";
            }
#endif
            // Calculate how many lines of inter-frame padding should be moved to the second field.
            zero_ofs = inter_frame_pad-(*f1_bottom_pad);
            (*f2_top_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated top padding for second field:"<<(*f2_top_pad)<<"lines";
            }
#endif
        }
        else
        {
            // Save padding for the top of the first field.
            (*f1_top_pad) = zero_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated top padding for first field:"<<(*f1_top_pad)<<"lines";
            }
#endif
        }
        // Calculate bottom padding for the second field.
        zero_ofs = (*f2_size)/PCM16X0SubLine::SUBLINES_PER_LINE;    // Number of lines with PCM data in second field.
        zero_ofs += (*f2_top_pad);                          // Plus top padding of the second field, calculated above.
        zero_ofs = LINES_PF-zero_ofs;     // Delta from standard number of lines per field.
        if(zero_ofs<0)
        {
            // Data still doesn't fit in the frame!
            // Remove bottom padding for second field.
            (*f2_bottom_pad) = 0;
            // Trim data in the second field.
            (*f2_size) -= ((0-zero_ofs)*PCM16X0SubLine::SUBLINES_PER_LINE);
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Data didn't fit in the second field and was trimmed by"<<(0-zero_ofs)<<"lines";
            }
#endif
        }
        else
        {
            // Save padding for the bottom of the second field.
            (*f2_bottom_pad) = zero_ofs;
        }
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Calculated bottom padding for second field:"<<(*f2_bottom_pad)<<"lines";
        }
#endif
    }
}

//------------------------ Try to locate PCM data inside the field/frame by Control Bit locations for EI format.
uint8_t PCM16X0DataStitcher::findEIDataAlignment(std::vector<PCM16X0SubLine> *field, uint16_t *f_size, uint16_t *top_pad, uint16_t *bottom_pad)
{
    bool suppress_log;
    uint8_t iblk_num;
    int16_t zero_ofs, last_ofs;

    suppress_log = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    // Try to detect 3th Control Bit (that should be zero) location in the lowest interleave blokck in the buffer.
    zero_ofs = findZeroControlBitOffset(field, *f_size, false);
    if(zero_ofs>=0)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Zeroed Control Bit offset:"<<(zero_ofs/PCM16X0SubLine::SUBLINES_PER_LINE)<<"("<<zero_ofs<<")";
        }
#endif
        // Reset padding.
        (*top_pad) = (*bottom_pad) = 0;
        // Try to guess number of first interleave block in the buffer
        // by position of the zeroed Control Bit in the source frame.
        iblk_num = estimateBlockNumber(field, *f_size, zero_ofs);
        // Calculate how many sub-lines after Contol Bit from the last interleave block is in the buffer.
        zero_ofs = (*f_size)-zero_ofs;
        // Calculate delta between target number from Contol Bit (3H) to the end of the interleave block and buffer.
        last_ofs = (PCM16X0DataBlock::SI_INTERLEAVE_OFS-2)*PCM16X0SubLine::SUBLINES_PER_LINE-zero_ofs;
        // Equlize buffer to full interleave block length.
        if(last_ofs<0)
        {
            // Excess number of lines in the buffer.
            // Remove sign.
            last_ofs = (0-last_ofs);
            // Trim field.
            (*f_size) -= last_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Last block in the buffer was"<<last_ofs<<"sub-lines longer, trimmed";
            }
#endif

        }
        else if(last_ofs>0)
        {
            // Put difference into padding.
            (*bottom_pad) += (last_ofs/PCM16X0SubLine::SUBLINES_PER_LINE);
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Last block in the buffer was"<<last_ofs<<"sub-lines shorter, added bottom padding of"<<(*bottom_pad)<<"lines";
            }
#endif
        }
        // Calculate sub-line offset of the last sub-line of the block.
        last_ofs = (PCM16X0DataBlock::INT_BLK_PER_FIELD-iblk_num-1)*SI_TRUE_INTERLEAVE;
        (*bottom_pad) += (last_ofs/PCM16X0SubLine::SUBLINES_PER_LINE);
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(last_ofs>0)
            {
                qInfo()<<"[L2B-16x0] Adding"<<last_ofs<<"sub-lines at the bottom to fill the field";
            }
            qInfo()<<"[L2B-16x0] Calculated bottom padding:"<<(*bottom_pad);
        }
#endif
        // Calculate top padding.
        last_ofs = LINES_PF-(*f_size)/PCM16X0SubLine::SUBLINES_PER_LINE;      // Subtract PCM data lines.
        last_ofs -= (*bottom_pad);                                                      // Subtract calculated bottom padding.
        if(last_ofs<0)
        {
            // Remove sign.
            last_ofs = (0-last_ofs);
            // Negative top offset, probably some noise at the top of the field.
            if((last_ofs<PCM16X0DataBlock::SI_INTERLEAVE_OFS)&&(last_ofs<(*f_size)))
            {
                // Error is less than interleave block.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[L2B-16x0] Top of the field was cut by"<<last_ofs<<"lines";
                }
#endif
                // Cut part of the data at the top of the field.
                cutFieldTop(field, f_size, last_ofs);
                return DS_RET_OK;
            }
            else
            {
                // Error is too big.
                qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error in [PCM16X0DataStitcher::findEIDataAlignment()]! Negative offet"<<last_ofs<<"at frame"<<frasm_f1.frame_number;
                return DS_RET_NO_PAD;
            }
        }
        else
        {
            // Save top padding.
            (*top_pad) += last_ofs;
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[L2B-16x0] Calculated top padding:"<<(*top_pad);
            }
#endif
            return DS_RET_OK;
        }
    }
    else
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Control Bit location not found, unable to align data";
        }
#endif
        return DS_RET_NO_PAD;
    }
}

//------------------------ Detect inter-frame and per-field paddings for EI format.
uint8_t PCM16X0DataStitcher::findEIFrameStitching()
{
    bool suppress_log, suppress_anylog;
    uint8_t f_res;
    uint8_t proc_state, stage_count;

    //QElapsedTimer dbg_timer;

    suppress_log = !(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0));
    suppress_anylog = !(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0));

    // Preset field order.
    if(preset_field_order==FrameAsmDescriptor::ORDER_BFF)
    {
        frasm_f1.presetBFF();
    }
    else
    {
        frasm_f1.presetTFF();
    }

    // Set first stage.
    proc_state = STG_TRY_PREVIOUS;
    //proc_state = STG_FULL_PREPARE;

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-16x0] -------------------- Padding detection for EI format starting...";
    }
#endif

    // Now let's try to detect padding.
    stage_count = 0;
    do
    {
        // Count loops.
        stage_count++;
        //qInfo()<<"Run"<<stage_count<<"stage"<<proc_state<<"time"<<dbg_timer.nsecsElapsed()/1000;
//------------------------ Try to perform the same stitching as on frame before.
        if(proc_state==STG_TRY_PREVIOUS)
        {
            // Check trim data.
            /*if((frasm_f0.odd_top_data==frasm_f1.odd_top_data)&&(frasm_f0.even_top_data==frasm_f1.even_top_data)
                &&(frasm_f0.odd_bottom_data==frasm_f1.odd_bottom_data)&&(frasm_f0.even_bottom_data==frasm_f1.even_bottom_data)
                &&(frasm_f0.inner_padding_ok!=false)&&(frasm_f0.outer_padding_ok!=false))*/
            {
                // TODO: save stats of previous frames and try to apply it.
                // Check previous padding.
                f_res = getProbablePadding();
                if(f_res!=INVALID_PAD_MARK)
                {
                    // Apply that padding.
                    uint8_t inter_pad;
                    uint16_t field1_line_cnt, field2_line_cnt;
                    PCM16X0SubLine empty_line;
                    std::vector<PCM16X0SubLine> *field1_ptr, *field2_ptr;

                    inter_pad = f_res;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-16x0] Trying previous by stat padding"<<inter_pad<<"lines";
                    }
#endif
                    padding_queue.clear();
                    // Determine field order.
                    if(frasm_f1.isOrderTFF()!=false)
                    {
                        field1_line_cnt = frasm_f1.odd_data_lines;
                        field2_line_cnt = frasm_f1.even_data_lines;
                        field1_ptr = &frame1_odd;
                        field2_ptr = &frame1_even;
                    }
                    else
                    {
                        field1_line_cnt = frasm_f1.even_data_lines;
                        field2_line_cnt = frasm_f1.odd_data_lines;
                        field1_ptr = &frame1_even;
                        field2_ptr = &frame1_odd;
                    }
                    // Copy data from [field 1].
                    for(uint16_t index=0;index<field1_line_cnt;index++)
                    {
                        // Fill from the top of [field1] to the bottom.
                        padding_queue.push_back((*field1_ptr)[index]);
                    }
                    // Save frame parameters to carry those into padding.
                    empty_line.line_number = (*field1_ptr)[field1_line_cnt-1].line_number;
                    empty_line.frame_number = (*field1_ptr)[field1_line_cnt-1].frame_number;
                    // Add padding.
                    for(uint8_t padding=0;padding<inter_pad;padding++)
                    {
                        // Add one more padding line.
                        empty_line.line_number += 2;    // Simulate interlaced line numbering.
                        for(uint8_t part=0;part<PCM16X0SubLine::SUBLINES_PER_LINE;part++)
                        {
                            empty_line.line_part = part;
                            padding_queue.push_back(empty_line);
                        }
                    }
                    // Copy data from [field 2].
                    for(uint16_t index=0;index<field2_line_cnt;index++)
                    {
                        // Fill from the top of [field2] to the bottom.
                        padding_queue.push_back((*field2_ptr)[index]);
                    }
                    // Try stitch fields with provided padding.
                    f_res = tryEIPadding(inter_pad);
                    if(f_res==DS_RET_OK)
                    {
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-16x0] Previously used padding of"<<inter_pad<<"lines is OK.";
                        }
#endif
                        updatePadStats(inter_pad, true);
                        if(frasm_f1.isOrderTFF()!=false)
                        {
                            // Store padding as bottom for first field (odd for TFF).
                            frasm_f1.odd_bottom_padding = inter_pad;
                            // Remove top padding for second field (even for TFF).
                            frasm_f1.even_top_padding = 0;
                            frasm_f1.silence = false;
                            proc_state = STG_ALIGN_TFF;
                        }
                        else
                        {
                            // Store padding as bottom for first field (even for TFF).
                            frasm_f1.even_bottom_padding = inter_pad;
                            // Remove top padding for second field (odd for TFF).
                            frasm_f1.odd_top_padding = 0;
                            frasm_f1.silence = false;
                            proc_state = STG_ALIGN_BFF;
                        }
                    }
                    else
                    {
                        // Previous padding failed.
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[L2B-16x0] Previously used padding of"<<inter_pad<<"lines did not work!";
                        }
#endif
                        // Go to full padding sweep.
                        proc_state = STG_FULL_PREPARE;
                    }
                }
                else
                {
                    // No padding stats available.
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[L2B-16x0] No stats data for previous paddings available.";
                    }
#endif
                    // Go to full padding sweep.
                    proc_state = STG_FULL_PREPARE;
                }
            }
        }
//------------------------ Processing current frame padding.
        else if(proc_state==STG_FULL_PREPARE)
        {
            // Reset padding.
            frasm_f1.odd_top_padding = frasm_f1.odd_bottom_padding = frasm_f1.even_top_padding = frasm_f1.even_bottom_padding = 0;
            // Check if there is enough data in fields of current frame.
            // Parity lines take ~82 lines in a field.
            // Data lines take ~164 lines in a field.
            if(((frasm_f1.odd_data_lines<MIN_FILL_SUBLINES_PF_EI)&&(frasm_f1.even_data_lines<MIN_FILL_SUBLINES_PF_EI))||
               ((frasm_f1.odd_data_lines+frasm_f1.even_data_lines)<(2*MIN_FILL_SUBLINES_PF_EI)))
            {
                // Not enough usefull lines, it will be impossible to compile data blocks.
                // Unable to detect field padding.
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-16x0] Not enough usefull lines in current frame, padding unknown, skipping...";
                }
#endif
                // Inter-frame padding can not be detected due to not enough PCM lines.
                // Try to align data by Control Bit location.
                proc_state = STG_FB_CTRL_EST;
            }
            else
            {
                // There are enough lines in Frame A.
                if(frasm_f1.isOrderTFF()!=false)
                {
                    proc_state = STG_INTERPAD_TFF;
                }
                else
                {
                    proc_state = STG_INTERPAD_BFF;
                }
            }
        }
        else if(proc_state==STG_INTERPAD_TFF)
        {
            // Try to detect current frame field padding, assuming TFF order.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-16x0] Try to detect field padding assuming TFF...";
            }
#endif
            // Check if there is enough data in a field.
            if(frasm_f1.odd_data_lines<MIN_FILL_SUBLINES_PF_EI)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-16x0] Not enough usefull lines in odd field, inter-frame padding unknown, skipping...";
                }
#endif
                // Inter-frame padding can not be detected due to not enough PCM lines.
                // Try to align data by Control Bit location.
                proc_state = STG_FB_CTRL_EST;
            }
            else
            {
                // Get inter-frame padding result and store it in [frasm_f1.odd_bottom_padding] for TFF.
                f_res = findEIPadding(FrameAsmDescriptor::ORDER_TFF);
                // Preset "no silence" for padding.
                frasm_f1.silence = false;
                // Preset bad padding.
                frasm_f1.padding_ok = false;
                if(f_res==DS_RET_OK)
                {
                    // Padding detected successfully.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-16x0] Field order: TFF, inter-frame padding:"<<frasm_f1.odd_bottom_padding;
                    }
#endif
                    // Align data keeping inter-frame padding.
                    proc_state = STG_ALIGN_TFF;
                }
                else if(f_res==DS_RET_SILENCE)
                {
                    // Can not detect padding due to silence, need to try later.
                    frasm_f1.silence = true;
                    frasm_f1.odd_bottom_padding = 0;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-16x0] Unable to detect inter-frame padding and field order due to silence";
                    }
#endif
                    // Inter-frame padding can not be detected due to silence.
                    // Try to align data by Control Bit location.
                    proc_state = STG_FB_CTRL_EST;
                }
                else
                {
                    // Inter-frame padding attempt failed.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-16x0] Unable to detect inter-frame padding, fallback to locating data position via Control Bits";
                    }
#endif
                    frasm_f1.odd_bottom_padding = 0;
                    // Try to align data by Control Bit location.
                    proc_state = STG_FB_CTRL_EST;
                }
            }
        }
        else if(proc_state==STG_INTERPAD_BFF)
        {
            // Try to detect current frame field padding, assuming BFF order.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-16x0] Try to detect field padding assuming BFF...";
            }
#endif
            if(frasm_f1.even_data_lines<MIN_FILL_SUBLINES_PF_EI)
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-16x0] Not enough usefull lines in even field, inter-frame padding unknown, skipping...";
                }
#endif
                // Inter-frame padding can not be detected due to not enough PCM lines.
                // Try to align data by Control Bit location.
                proc_state = STG_FB_CTRL_EST;
            }
            else
            {
                // Get inter-frame padding result and store it in [frasm_f1.even_bottom_padding] for BFF.
                f_res = findEIPadding(FrameAsmDescriptor::ORDER_BFF);
                // Preset "no silence" for padding.
                frasm_f1.silence = false;
                // Preset bad padding.
                frasm_f1.padding_ok = false;
                if(f_res==DS_RET_OK)
                {
                    // Padding detected successfully.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-16x0] Field order: BFF, inter-frame padding:"<<frasm_f1.even_bottom_padding;
                    }
#endif
                    // Align data keeping inter-frame padding.
                    proc_state = STG_ALIGN_BFF;
                }
                else if(f_res==DS_RET_SILENCE)
                {
                    // Can not detect padding due to silence, need to try later.
                    frasm_f1.silence = true;
                    frasm_f1.even_bottom_padding = 0;
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-16x0] Unable to detect inter-frame padding and field order due to silence";
                    }
#endif
                    // Inter-frame padding can not be detected due to silence.
                    // Try to align data by Control Bit location.
                    proc_state = STG_FB_CTRL_EST;
                }
                else
                {
                    // Inter-frame padding attempt failed.
#ifdef DI_EN_DBG_OUT
                    if(suppress_anylog==false)
                    {
                        qInfo()<<"[L2B-16x0] Unable to detect inter-frame padding, fallback to locating data position via Control Bits";
                    }
#endif
                    frasm_f1.even_bottom_padding = 0;
                    // Try to align data by Control Bit location.
                    proc_state = STG_FB_CTRL_EST;
                }
            }
        }
        else if(proc_state==STG_ALIGN_TFF)
        {
            // Inter-field padding was detected.
            // Align data to the frame borders, keeping inter-frame padding.
            conditionEIFramePadding(&frame1_odd, &frame1_even, &frasm_f1.odd_data_lines, &frasm_f1.even_data_lines,
                                    &frasm_f1.odd_top_padding, &frasm_f1.odd_bottom_padding, &frasm_f1.even_top_padding, &frasm_f1.even_bottom_padding);
            frasm_f1.padding_ok = true;
            // Padding is done.
            proc_state = STG_PAD_OK;
        }
        else if(proc_state==STG_ALIGN_BFF)
        {
            // Inter-field padding was detected.
            // Align data to the frame borders, keeping inter-frame padding.
            conditionEIFramePadding(&frame1_even, &frame1_odd, &frasm_f1.even_data_lines, &frasm_f1.odd_data_lines,
                                    &frasm_f1.even_top_padding, &frasm_f1.even_bottom_padding, &frasm_f1.odd_top_padding, &frasm_f1.odd_bottom_padding);
            frasm_f1.padding_ok = true;
            // Padding is done.
            proc_state = STG_PAD_OK;
        }
        else if(proc_state==STG_FB_CTRL_EST)
        {
            // Preset good result.
            proc_state = STG_PAD_OK;
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-16x0] Performing data location search for odd field...";
            }
#endif
            // Find padding for one field.
            if(findEIDataAlignment(&frame1_odd, &frasm_f1.odd_data_lines, &frasm_f1.odd_top_padding, &frasm_f1.odd_bottom_padding)!=DS_RET_OK)
            {
                // Unable to align data by Control Bits.
                // Calculate default padding.
                frasm_f1.odd_bottom_padding = 0;
                frasm_f1.odd_top_padding = (SUBLINES_PF-frasm_f1.odd_data_lines)/PCM16X0SubLine::SUBLINES_PER_LINE;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-16x0] Unable to align PCM data, anchoring it to the bottom of the field, calculated top padding:"<<frasm_f1.odd_top_padding;
                }
#endif
                proc_state = STG_PAD_NO_GOOD;
            }
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-16x0] Performing data location search for even field...";
            }
#endif
            // Find padding for another field.
            if(findEIDataAlignment(&frame1_even, &frasm_f1.even_data_lines, &frasm_f1.even_top_padding, &frasm_f1.even_bottom_padding)!=DS_RET_OK)
            {
                // Unable to align data by Control Bits.
                // Calculate default padding.
                frasm_f1.even_bottom_padding = 0;
                frasm_f1.even_top_padding = (SUBLINES_PF-frasm_f1.even_data_lines)/PCM16X0SubLine::SUBLINES_PER_LINE;
#ifdef DI_EN_DBG_OUT
                if(suppress_anylog==false)
                {
                    qInfo()<<"[L2B-16x0] Unable to align PCM data, anchoring it to the bottom of the field, calculated top padding:"<<frasm_f1.even_top_padding;
                }
#endif
                proc_state = STG_PAD_NO_GOOD;
            }
        }
//------------------------ General states.
        else if(proc_state==STG_PAD_OK)
        {
            // Field order and paddings are found successfully.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-16x0] Padding detected successfully.";
            }
#endif
            //qDebug()<<"[DI] Stage"<<stage_count<<"time:"<<dbg_timer.nsecsElapsed();
            // Exit stage cycle.
            break;
        }
        else if(proc_state==STG_PAD_SILENCE)
        {
            // Padding can not be found on silence.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-16x0] Unable to detect inter-frame padding due to silence!";
            }
#endif
            //qDebug()<<"[DI] Stage"<<stage_count<<"time:"<<dbg_timer.nsecsElapsed();
            // Exit stage cycle.
            break;
        }
        else if(proc_state==STG_PAD_NO_GOOD)
        {
            // Padding was not found.
#ifdef DI_EN_DBG_OUT
            if(suppress_anylog==false)
            {
                qInfo()<<"[L2B-16x0] Unable to detect valid inter-frame padding!";
            }
#endif
            //qDebug()<<"[DI] Stage"<<stage_count<<"time:"<<dbg_timer.nsecsElapsed();
            // Exit stage cycle.
            break;
        }
        //qDebug()<<"[DI] Stage"<<stage_count<<"time:"<<dbg_timer.nsecsElapsed();

        // Check for looping.
        if(stage_count>STG_PAD_MAX)
        {
#ifdef DI_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Inf. loop detected in [PCM16X0DataStitcher::findEIFrameStitching()], breaking...";
#endif
            return DS_RET_NO_PAD;
        }
    }
    while(1);   // Stages cycle.

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        qInfo()<<"[L2B-16x0] ----------- Padding results:";
        log_line = "[L2B-16x0] Frame ("+QString::number(frasm_f1.frame_number)+") inter-frame padding: ";
        if(frasm_f1.padding_ok!=false)
        {
            log_line += "OK";
        }
        else if(frasm_f1.silence!=false)
        {
            log_line += "Silence";
        }
        else
        {
            log_line += "Fail";
        }
        if(frasm_f1.isOrderTFF()!=false)
        {
            log_line += " ("+QString::number(frasm_f1.odd_top_padding)+"-";
            log_line += QString::number(frasm_f1.odd_bottom_padding)+"-";
            log_line += QString::number(frasm_f1.even_top_padding)+"-";
            log_line += QString::number(frasm_f1.even_bottom_padding)+"), field order: ";

            log_line += "TFF";
            if(frasm_f1.isOrderPreset()!=false)
            {
                log_line += " (preset)";
            }
        }
        else if(frasm_f1.isOrderBFF()!=false)
        {
            log_line += " ("+QString::number(frasm_f1.even_top_padding)+"-";
            log_line += QString::number(frasm_f1.even_bottom_padding)+"-";
            log_line += QString::number(frasm_f1.odd_top_padding)+"-";
            log_line += QString::number(frasm_f1.odd_bottom_padding)+"), field order: ";

            log_line += "BFF";
            if(frasm_f1.isOrderPreset()!=false)
            {
                log_line += " (preset)";
            }
        }
        else
        {
            log_line += ", field order: Unknown";
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

//------------------------ Clear Control Bits history.
void PCM16X0DataStitcher::clearCtrlBitStats()
{
    stats_emph.fill(EMPH_UNKNOWN);
    stats_code.fill(CONTENT_UNKNOWN);
    stats_srate.fill(PCMSamplePair::SAMPLE_RATE_UNKNOWN);
}

//------------------------ Add new Control Bit array to the list, remove oldest one.
void PCM16X0DataStitcher::updateCtrlBitStats(PCM16X0DataBlock *int_block_flags)
{
    if(int_block_flags!=NULL)
    {
        if(int_block_flags->isOrderOdd()!=false)
        {
            // Control Bits were not detected.
            stats_emph.push(EMPH_UNKNOWN);
            stats_code.push(CONTENT_UNKNOWN);
            stats_srate.push(PCMSamplePair::SAMPLE_RATE_UNKNOWN);
        }
        else
        {
            // Control Bits were successfully found.
            if(int_block_flags->hasEmphasis()==false)
            {
                stats_emph.push(EMPH_OFF);
            }
            else
            {
                stats_emph.push(EMPH_ON);
            }
            if(int_block_flags->hasCode()==false)
            {
                stats_code.push(CONTENT_AUDIO);
            }
            else
            {
                stats_code.push(CONTENT_CODE);
            }
            if(int_block_flags->sample_rate==PCMSamplePair::SAMPLE_RATE_44100)
            {
                stats_srate.push(PCMSamplePair::SAMPLE_RATE_44100);
            }
            else
            {
                stats_srate.push(PCMSamplePair::SAMPLE_RATE_44056);
            }
        }
    }
}

//------------------------ Get most probable emphasis bit state from stats.
bool PCM16X0DataStitcher::getProbableEmphasesBit()
{
    uint8_t cnt_off, cnt_on;

    cnt_off = cnt_on = 0;
    // Count bit states.
    for(uint8_t index=0;index<STATS_DEPTH;index++)
    {
        if(stats_emph[index]==EMPH_OFF)
        {
            cnt_off++;
        }
        else if(stats_emph[index]==EMPH_ON)
        {
            cnt_on++;
        }
    }
    // Determine bit state by statistics.
    if((cnt_off>0)||(cnt_on>0))
    {
        if(cnt_off<cnt_on)
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-16x0] Emphasis state by stats is ON (OFF: %02u, ON: %02u)", cnt_off, cnt_on);
                qInfo()<<log_line;
            }
#endif
            return false;       // 1H = 0 (emphasis on)
        }
        else
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-16x0] Emphasis state by stats is OFF (OFF: %02u, ON: %02u)", cnt_off, cnt_on);
                qInfo()<<log_line;
            }
#endif
            return true;        // 1H = 1 (emphasis off)
        }
    }
    else
    {
#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_FIELD_ASSEMBLY)!=0)
        {
            QString log_line;
            log_line.sprintf("[L2B-16x0] Emphasis state not found by stats, defaulting to OFF");
            qInfo()<<log_line;
        }
#endif
        return true;        // 1H = 1 (emphasis off)
    }
}

//------------------------ Get most probable code bit state from stats.
bool PCM16X0DataStitcher::getProbableCodeBit()
{
    uint8_t cnt_code, cnt_audio;

    cnt_code = cnt_audio = 0;
    // Count bit states.
    for(uint8_t index=0;index<STATS_DEPTH;index++)
    {
        if(stats_code[index]==CONTENT_CODE)
        {
            cnt_code++;
        }
        else if(stats_code[index]==CONTENT_AUDIO)
        {
            cnt_audio++;
        }
    }
    // Determine bit state by statistics.
    if((cnt_code>0)||(cnt_audio>0))
    {
        if(cnt_code<cnt_audio)
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-16x0] Content by stats is AUDIO (CODE: %02u, AUDIO: %02u)", cnt_code, cnt_audio);
                qInfo()<<log_line;
            }
#endif
            return true;        // 4H = 1 (audio)
        }
        else
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-16x0] Content by stats is CODE (CODE: %02u, AUDIO: %02u)", cnt_code, cnt_audio);
                qInfo()<<log_line;
            }
#endif
            return false;       // 4H = 0 (code)
        }
    }
    else
    {
#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_FIELD_ASSEMBLY)!=0)
        {
            QString log_line;
            log_line.sprintf("[L2B-16x0] Content not found by stats, defaulting to AUDIO");
            qInfo()<<log_line;
        }
#endif
        return true;        // 4H = 1 (audio)
    }
}

//------------------------ Get most probable sample rate from stats.
uint16_t PCM16X0DataStitcher::getProbableSampleRate()
{
    uint8_t cnt_44056, cnt_44100;

    cnt_44056 = cnt_44100 = 0;
    // Count bit states.
    for(uint8_t index=0;index<STATS_DEPTH;index++)
    {
        if(stats_srate[index]==PCMSamplePair::SAMPLE_RATE_44056)
        {
            cnt_44056++;
        }
        else if(stats_srate[index]==PCMSamplePair::SAMPLE_RATE_44100)
        {
            cnt_44100++;
        }
    }
    // Determine bit state by statistics.
    if((cnt_44056>0)||(cnt_44100>0))
    {
        if(cnt_44056<cnt_44100)
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-16x0] Sample rate by stats is 44100 (44056: %02u, 44100: %02u)", cnt_44056, cnt_44100);
                qInfo()<<log_line;
            }
#endif
            return PCMSamplePair::SAMPLE_RATE_44100;    // 2H = 0 (44100)
        }
        else
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                QString log_line;
                log_line.sprintf("[L2B-16x0] Sample rate by stats is 44056 (44056: %02u, 44100: %02u)", cnt_44056, cnt_44100);
                qInfo()<<log_line;
            }
#endif
            return PCMSamplePair::SAMPLE_RATE_44056;    // 2H = 1 (44056)
        }
    }
    else
    {
#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_FIELD_ASSEMBLY)!=0)
        {
            QString log_line;
            log_line.sprintf("[L2B-16x0] Sample rate not found by stats, defaulting to 44056");
            qInfo()<<log_line;
        }
#endif
        return PCMSamplePair::SAMPLE_RATE_44056;    // 2H = 1 (44056)
    }
}

//------------------------ Clear padding history.
void PCM16X0DataStitcher::clearPadStats()
{
    stats_padding.fill(INVALID_PAD_MARK);
}

//------------------------ Add new padding to the list, remove oldest one.
void PCM16X0DataStitcher::updatePadStats(uint8_t new_pad, bool valid)
{
    if(valid==false)
    {
        stats_padding.push(INVALID_PAD_MARK);
    }
    else
    {
        stats_padding.push(new_pad);
    }
}

//------------------------ Get most probable padding from stats.
uint8_t PCM16X0DataStitcher::getProbablePadding()
{
    uint8_t index, max_cnt, max_idx;
    std::array<uint8_t, MAX_PADDING_EI> pad_cnt;
    pad_cnt.fill(0);
    max_cnt = 0;
    max_idx = INVALID_PAD_MARK;

    //return max_idx;

    // Scan all padding history.
    for(index=0;index<STATS_DEPTH;index++)
    {
        // Check if index contains valid padding.
        if(stats_padding[index]!=INVALID_PAD_MARK)
        {
            // Increase count for this padding.
            pad_cnt[stats_padding[index]]++;
            max_cnt++;
            //qDebug()<<"[HST]"<<index<<padding_stats[index]<<pad_cnt[padding_stats[index]];
        }
        /*else
        {
            qDebug()<<"[HST]"<<index<<padding_stats[index];
        }*/
    }

    // Check if at least one valid padding was found.
    if(max_cnt>0)
    {
        max_cnt = 0;
        // Search for maximum count for single padding.
        for(index=0;index<MAX_PADDING_EI;index++)
        {
            // Check if current count is more than maximum on record.
            if(pad_cnt[index]>max_cnt)
            {
                // Update maximum.
                max_cnt = pad_cnt[index];
                // Save padding.
                max_idx = index;
            }
            //qDebug()<<"[CNT]"<<index<<pad_cnt[index]<<max_cnt;
        }
#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
        {
            QString log_line;
            log_line.sprintf("[L2B-16x0] Padding by stats (%02u encounters) is %02u", max_cnt, max_idx);
            qInfo()<<log_line;
        }
#endif
    }

    return max_idx;
}

//------------------------ Get first line number for first field to fill.
uint16_t PCM16X0DataStitcher::getFirstFieldLineNum(uint8_t in_order)
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
uint16_t PCM16X0DataStitcher::getSecondFieldLineNum(uint8_t in_order)
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
uint16_t PCM16X0DataStitcher::addLinesFromField(std::vector<PCM16X0SubLine> *field_buf, uint16_t ind_start, uint16_t count,
                                                uint16_t *last_q_order, uint16_t *last_line_num)
{
    uint16_t lines_cnt;
    lines_cnt = 0;
    PCM16X0SubLine copy_line;

#ifdef DI_EN_DBG_OUT
    uint16_t min_line_num, max_line_num;
    max_line_num = 0;
    min_line_num = 0xFFFF;
#endif

    // Check array limits.
    if((field_buf->size()>=ind_start)&&(field_buf->size()>=(ind_start+count)))
    {
        // Check if there is anything to copy.
        if(count!=0)
        {
            for(uint16_t index=ind_start;index<(ind_start+count);index++)
            {
                // Copy PCM line into queue.
                copy_line = (*field_buf)[index];
                copy_line.queue_order = (*last_q_order);
                conv_queue.push_back(copy_line);
                (*last_q_order) = (*last_q_order)+1;
                if((last_line_num!=NULL)&&((*field_buf)[index].line_part==PCM16X0SubLine::PART_LEFT))
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
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                qInfo()<<"[L2B-16x0]"<<lines_cnt<<"sub-lines from field ("<<ind_start<<"..."<<(ind_start+count-1)<<"), #("<<min_line_num<<"..."<<max_line_num<<") inserted into queue";
            }
#endif
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                qInfo()<<"[L2B-16x0] No sub-lines from field inserted";
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Array access out of bounds in [PCM16X0DataStitcher::addLinesFromField()]! Total:"<<field_buf->size()<<", start index:"<<ind_start<<", stop index:"<<(ind_start+count);
    }
    return lines_cnt;
}

//------------------------ Fill output line buffer with empty lines.
uint16_t PCM16X0DataStitcher::addFieldPadding(uint32_t in_frame, uint16_t line_cnt,
                                              uint16_t *last_q_order, uint16_t *last_line_num)
{
    uint16_t lines_cnt;
    lines_cnt = 0;

    if(line_cnt==0)
    {
        return lines_cnt;
    }

#ifdef DI_EN_DBG_OUT
    uint16_t min_line_num, max_line_num;
    max_line_num = 0;
    min_line_num = 0xFFFF;
#endif

    PCM16X0SubLine empty_line;
    for(uint16_t index=0;index<line_cnt;index++)
    {
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
        for(uint8_t part_cnt=0;part_cnt<PCM16X0SubLine::PART_MAX;part_cnt++)
        {
            // Set line part.
            empty_line.line_part = part_cnt;
            // Push padding line into queue.
            empty_line.queue_order = (*last_q_order);
            (*last_q_order) = (*last_q_order)+1;
            conv_queue.push_back(empty_line);
            lines_cnt++;
        }
    }
#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_FIELD_ASSEMBLY)!=0)
    {
        qInfo()<<"[L2B-16x0]"<<lines_cnt<<"padding sub-lines #("<<min_line_num<<"..."<<max_line_num<<") inserted into queue";
    }
#endif
    return lines_cnt;
}

//------------------------ Fill in one frame into queue for conversion from field buffers, inserting padding.
void PCM16X0DataStitcher::fillFrameForOutput()
{
    bool suppress_log;
    uint8_t cur_field_order;
    uint16_t last_line, queue_ord, field_1_cnt, field_2_cnt, field_1_top_pad, field_1_bottom_pad, field_2_top_pad, field_2_bottom_pad, target_lines_per_field;
    uint16_t added_lines_cnt, frame_lines_cnt;
    std::vector<PCM16X0SubLine> *p_field_1, *p_field_2;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FIELD_ASSEMBLY)!=0));

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[L2B-16x0] -------------------- Frame reassembling starting...";
    }
#endif

    cur_field_order = frasm_f1.field_order;
    target_lines_per_field = SUBLINES_PF;

    // Select field data to add lines from.
    if(cur_field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        // Field order is set to TFF.
        // First field = odd, second = even.
        field_1_top_pad = frasm_f1.odd_top_padding;
        field_1_bottom_pad = frasm_f1.odd_bottom_padding;
        field_2_top_pad = frasm_f1.even_top_padding;
        field_2_bottom_pad = frasm_f1.even_bottom_padding;
        field_1_cnt = frasm_f1.odd_data_lines;
        field_2_cnt = frasm_f1.even_data_lines;
        p_field_1 = &frame1_odd;
        p_field_2 = &frame1_even;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Field order: TFF";
        }
#endif
    }
    else
    {
        // Field order is set to BFF.
        // First field = even, second = odd.
        field_1_top_pad = frasm_f1.even_top_padding;
        field_1_bottom_pad = frasm_f1.even_bottom_padding;
        field_2_top_pad = frasm_f1.odd_top_padding;
        field_2_bottom_pad = frasm_f1.odd_bottom_padding;
        field_1_cnt = frasm_f1.even_data_lines;
        field_2_cnt = frasm_f1.odd_data_lines;
        p_field_1 = &frame1_even;
        p_field_2 = &frame1_odd;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Field order: BFF";
        }
#endif
    }

    frame_lines_cnt = added_lines_cnt = 0;
    // Reset line number.
    queue_ord = 1;
    last_line = getFirstFieldLineNum(cur_field_order);
    // Add first field top padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_1_top_pad, &queue_ord, &last_line);
    // Add lines from first field (full field).
    added_lines_cnt += addLinesFromField(p_field_1, 0, field_1_cnt, &queue_ord, &last_line);
    // Add first field bottom padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_1_bottom_pad, &queue_ord, &last_line);
    if(added_lines_cnt<target_lines_per_field)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Missed"<<(target_lines_per_field-added_lines_cnt)<<"sub-lines, adding...";
        }
#endif
        added_lines_cnt += addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-added_lines_cnt), &queue_ord, &last_line);
    }

    frame_lines_cnt += added_lines_cnt;
    added_lines_cnt = 0;

    // Reset line number.
    queue_ord = 1;
    last_line = getSecondFieldLineNum(cur_field_order);
    // Add second field top padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_2_top_pad, &queue_ord, &last_line);
    // Add lines from second field (full field).
    added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &queue_ord, &last_line);
    // Add second field bottom padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_2_bottom_pad, &queue_ord, &last_line);
    if(added_lines_cnt<target_lines_per_field)
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Missed"<<(target_lines_per_field-added_lines_cnt)<<"sub-lines, adding...";
        }
#endif
        added_lines_cnt += addFieldPadding(frasm_f1.frame_number, (target_lines_per_field-added_lines_cnt), &queue_ord, &last_line);
    }
    frame_lines_cnt += added_lines_cnt;

#ifdef DI_EN_DBG_OUT
    if((target_lines_per_field!=0)&&((target_lines_per_field*2)!=frame_lines_cnt))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Addded"<<frame_lines_cnt<<"sub-lines per frame"<<frasm_f1.frame_number<<"(WRONG COUNT, should be"<<(target_lines_per_field*2)<<")";
    }
    else if(suppress_log==false)
    {
        qInfo()<<"[L2B-16x0] Addded"<<frame_lines_cnt<<"sub-lines per frame";
    }
#endif

    PCM16X0DataBlock ctrl_stats;
    // Gather frame stats on Control Bits and put result into [PCM16X0DataBlock] fields.
    collectCtrlBitStats(&ctrl_stats);
    // Update Control Bits history.
    updateCtrlBitStats(&ctrl_stats);

    if(ctrl_stats.isOrderOdd()==false)
    {
        // Set Control Bits for the frame as detected.
        f1_srate = ctrl_stats.sample_rate;
        f1_emph = ctrl_stats.emphasis;
        f1_code = ctrl_stats.code;
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Control Bits are set as detected in frame data";
        }
#endif
    }
    else
    {
        // Get Control Bits by stats.
        f1_srate = getProbableSampleRate();
        f1_emph = getProbableEmphasesBit();
        f1_code = getProbableCodeBit();
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[L2B-16x0] Control Bits are set by stats";
        }
#endif
    }
}

//------------------------ Collect Control Bit statistics for the frame.
bool PCM16X0DataStitcher::collectCtrlBitStats(PCM16X0DataBlock *int_block_flags)
{
    uint8_t emph, rate, mode, code, emph_cnt, rate_cnt, mode_cnt, code_cnt;
    uint16_t subline_cnt;

    emph = rate = mode = code = emph_cnt = rate_cnt = mode_cnt = code_cnt = 0;

    if(conv_queue.size()>=SUBLINES_PF)
    {
        // Cycle through interleave blocks.
        for(uint8_t iblk=0;iblk<(PCM16X0DataBlock::INT_BLK_PER_FIELD*2);iblk++)
        {
            // Gather statistics for service bits in the field.
            subline_cnt = iblk*SI_TRUE_INTERLEAVE+1;     // Pick start of the interleave block, shift to [PART_MIDDLE].
            if(conv_queue[subline_cnt+BIT_EMPHASIS_OFS].isCRCValid()!=false)
            {
                emph_cnt++;
                if(conv_queue[subline_cnt+BIT_EMPHASIS_OFS].control_bit==false)
                {
                    emph++;
                }
            }
            if(conv_queue[subline_cnt+BIT_SAMPLERATE_OFS].isCRCValid()!=false)
            {
                rate_cnt++;
                if(conv_queue[subline_cnt+BIT_SAMPLERATE_OFS].control_bit==false)
                {
                    rate++;
                }
            }
            if(conv_queue[subline_cnt+BIT_MODE_OFS].isCRCValid()!=false)
            {
                mode_cnt++;
                if(conv_queue[subline_cnt+BIT_MODE_OFS].control_bit==false)
                {
                    mode++;
                }
            }
            if(conv_queue[subline_cnt+BIT_CODE_OFS].isCRCValid()!=false)
            {
                code_cnt++;
                if(conv_queue[subline_cnt+BIT_CODE_OFS].control_bit==false)
                {
                    code++;
                }
            }
        }
    }
    else
    {
        if(int_block_flags!=NULL)
        {
            // Re-purpose order flag as Control Bit validity flag.
            // Bits are invalid.
            int_block_flags->setOrderOdd();
        }
        return false;
    }

    if(int_block_flags!=NULL)
    {
        // Set most probable service bits for padded interleave blocks to output.
        if(emph>emph_cnt/2)
        {
            // Emphasis: enabled.
            int_block_flags->emphasis = true;
        }
        else
        {
            // Emphasis: disabled.
            int_block_flags->emphasis = false;
        }
        if(rate>rate_cnt/2)
        {
            // Sampling frequency: 44100 Hz.
            int_block_flags->sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
        }
        else
        {
            // Sampling frequency: 44056 Hz.
            int_block_flags->sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
        }
        if(mode>mode_cnt/2)
        {
            // Mode: EI format.
            int_block_flags->ei_format = true;
        }
        else
        {
            // Mode: SI format.
            int_block_flags->ei_format = false;
        }
        if(code>code_cnt/2)
        {
            // Code.
            int_block_flags->code = true;
        }
        else
        {
            // Audio.
            int_block_flags->code = false;
        }
        // Re-purpose order flag as Control Bit validity flag.
        if((emph_cnt>=2)&&(rate_cnt>=2)&&(code_cnt>=2))
        {
            // Bits are valid.
            int_block_flags->setOrderEven();
        }
        else
        {
            int_block_flags->setOrderOdd();
        }

#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            QString log_line, tmp_line;
            log_line = "[L2B-16x0] Control Bit configuration by frame content: ";
            if(int_block_flags->hasEmphasis()==false)
            {
                tmp_line.sprintf("EMPH: OFF (%01u/%01u), ", emph, emph_cnt);
            }
            else
            {
                tmp_line.sprintf("EMPH: ON  (%01u/%01u), ", emph, emph_cnt);
            }
            log_line += tmp_line;
            if(int_block_flags->sample_rate==PCMSamplePair::SAMPLE_RATE_44100)
            {
                tmp_line.sprintf("SR: 44100 (%01u/%01u), ", rate, rate_cnt);
            }
            else
            {
                tmp_line.sprintf("SR: 44056 (%01u/%01u), ", rate, rate_cnt);
            }
            log_line += tmp_line;
            if(int_block_flags->isInEIFormat()==false)
            {
                tmp_line.sprintf("FMT: SI (%01u/%01u), ", mode, mode_cnt);
            }
            else
            {
                tmp_line.sprintf("FMT: EI (%01u/%01u), ", mode, mode_cnt);
            }
            log_line += tmp_line;
            if(int_block_flags->hasCode()==false)
            {
                tmp_line.sprintf("DATA: AUDIO (%01u/%01u)", code, code_cnt);
            }
            else
            {
                tmp_line.sprintf("DATA: CODE  (%01u/%01u)", code, code_cnt);
            }
            log_line += tmp_line;
            qInfo()<<log_line;
        }
#endif
    }

    if((emph_cnt>=2)&&(rate_cnt>=2)&&(code_cnt>=2))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Output service tag "New file started".
void PCM16X0DataStitcher::outputFileStart()
{
    size_t queue_size;
    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Empty pointer provided in [PCM16X0DataStitcher::outputFileStart()], service tag discarded!";
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
                    qInfo()<<"[L2B-16x0] Service tag 'NEW FILE' written.";
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

//------------------------ Output PCM data block into output queue (blocking).
void PCM16X0DataStitcher::outputDataBlock(PCM16X0DataBlock *in_block)
{
    size_t queue_size;
    bool size_lock;
    size_lock = false;

    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Empty pointer provided in [PCM16X0DataStitcher::outputDataBlock()], result discarded!";
    }
    else
    {
        bool block_state, word_left_state, word_right_state;
        PCMSamplePair sample_pair;
        while(1)
        {
            // Try to put processed data block into the queue.
            mtx_samples->lock();
            queue_size = (out_samples->size()+1);
            if(queue_size<(MAX_SAMPLEPAIR_QUEUE_SIZE-3))
            {
                // Set emphasis state of the samples.
                sample_pair.setEmphasis(in_block->hasEmphasis());
                // Set sample rate.
                sample_pair.setSampleRate(in_block->sample_rate);
                // Output L0+R0 samples.
                if(in_block->isDataBroken(PCM16X0DataBlock::SUBBLK_1)==false)
                {
                    // Set validity of the whole block and the samples.
                    block_state = in_block->isBlockValid();
                    word_left_state = in_block->isWordValid(PCM16X0DataBlock::SUBBLK_1, PCM16X0DataBlock::WORD_L);
                    word_right_state = in_block->isWordValid(PCM16X0DataBlock::SUBBLK_1, PCM16X0DataBlock::WORD_R);
                }
                else
                {
                    // Data block deemed to be "broken", no data can be taken as valid.
                    block_state = false;
                    word_left_state = word_right_state = false;
                }
                // Set data to [PCMSamplePair] object.
                sample_pair.setSamplePair(in_block->getSample(PCM16X0DataBlock::SUBBLK_1, PCM16X0DataBlock::WORD_L),
                                          in_block->getSample(PCM16X0DataBlock::SUBBLK_1, PCM16X0DataBlock::WORD_R),
                                          block_state, block_state, word_left_state, word_right_state);
                // Put sample pair in the output queue.
                out_samples->push_back(sample_pair);
                // Output L1+R1 samples.
                if(in_block->isDataBroken(PCM16X0DataBlock::SUBBLK_2)==false)
                {
                    block_state = in_block->isBlockValid();
                    word_left_state = in_block->isWordValid(PCM16X0DataBlock::SUBBLK_2, PCM16X0DataBlock::WORD_L);
                    word_right_state = in_block->isWordValid(PCM16X0DataBlock::SUBBLK_2, PCM16X0DataBlock::WORD_R);
                }
                else
                {
                    block_state = false;
                    word_left_state = word_right_state = false;
                }
                sample_pair.setSamplePair(in_block->getSample(PCM16X0DataBlock::SUBBLK_2, PCM16X0DataBlock::WORD_L),
                                          in_block->getSample(PCM16X0DataBlock::SUBBLK_2, PCM16X0DataBlock::WORD_R),
                                          block_state, block_state, word_left_state, word_right_state);
                out_samples->push_back(sample_pair);
                // Output L2+R2 samples.
                if(in_block->isDataBroken(PCM16X0DataBlock::SUBBLK_3)==false)
                {
                    block_state = in_block->isBlockValid();
                    word_left_state = in_block->isWordValid(PCM16X0DataBlock::SUBBLK_3, PCM16X0DataBlock::WORD_L);
                    word_right_state = in_block->isWordValid(PCM16X0DataBlock::SUBBLK_3, PCM16X0DataBlock::WORD_R);
                }
                else
                {
                    block_state = false;
                    word_left_state = word_right_state = false;
                }
                sample_pair.setSamplePair(in_block->getSample(PCM16X0DataBlock::SUBBLK_3, PCM16X0DataBlock::WORD_L),
                                          in_block->getSample(PCM16X0DataBlock::SUBBLK_3, PCM16X0DataBlock::WORD_R),
                                          block_state, block_state, word_left_state, word_right_state);
                out_samples->push_back(sample_pair);
                mtx_samples->unlock();
                if(size_lock!=false)
                {
                    size_lock = false;
#ifdef DI_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        qInfo()<<"[L2B-16x0] Output PCM data blocks queue has some space, continuing...";
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
                        qInfo()<<"[L2B-16x0] Output queue is at size limit ("<<(MAX_SAMPLEPAIR_QUEUE_SIZE-3)<<"), waiting...";
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
void PCM16X0DataStitcher::outputFileStop()
{
    size_t queue_size;
    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Empty pointer provided in [PCM16X0DataStitcher::outputFileStop()], service tag discarded!";
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
                    qInfo()<<"[L2B-16x0] Service tag 'FILE END' written.";
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
        // Reset filename.
        file_name.clear();
        // Report time that file processing took.
        qDebug()<<"[L2B-16x0] File processed in"<<file_time.elapsed()<<"msec";
    }
}

//------------------------ Perform deinterleave of a frame in [conv_queue].
void PCM16X0DataStitcher::performDeinterleave(uint8_t int_format)
{
    bool even_order/*, already_invalid*/;
    uint8_t int_block_cnt, broken_countdown;
    uint16_t line_in_block, frame_lim, interleave_lim, valid_cnt;
    std::deque<PCM16X0SubLine>::iterator buf_scaner;
    PCM16X0DataBlock pcm_block;

    // Set parameters for converter.
    lines_to_block.setInput(&conv_queue);
    lines_to_block.setOutput(&pcm_block);
    lines_to_block.setIgnoreCRC(ignore_CRC);
    lines_to_block.setForceParity(!ignore_CRC);
    lines_to_block.setPCorrection(enable_P_code);

    if(int_format==PCM16X0Deinterleaver::FORMAT_EI)
    {
        // Set limits for EI format.
        frame_lim = (EI_TRUE_INTERLEAVE*PCM16X0DataBlock::LINE_CNT);
        interleave_lim = PCM16X0DataBlock::EI_INTERLEAVE_OFS;
        frasm_f1.ei_format = true;
        lines_to_block.setEIFormat();
    }
    else
    {
        // Set limits for SI format.
        frame_lim = SI_TRUE_INTERLEAVE;
        interleave_lim = PCM16X0DataBlock::SI_INTERLEAVE_OFS;
        frasm_f1.ei_format = false;
        lines_to_block.setSIFormat();
    }

    // Dump the whole line buffer out (for visualization).
    buf_scaner = conv_queue.begin();
    while(buf_scaner!=conv_queue.end())
    {
        if((*buf_scaner).frame_number==frasm_f1.frame_number)
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

    valid_cnt = int_block_cnt = broken_countdown = 0;
    // Dump buffered data (one frame) into converter.
    while(conv_queue.size()>=frame_lim)
    {
        // Set odd sample order for start.
        even_order = false;
        line_in_block = 0;
        for(uint16_t i=0;i<interleave_lim;i++)
        {
            // Reset data block structure.
            pcm_block.clear();
            //already_invalid = false;
            // Fill up data block, performing de-interleaving, convert lines to data blocks.
            lines_to_block.processBlock(i, even_order);
            // Put line number inside of interleave block into queue index.
            pcm_block.queue_order = line_in_block;

            // Set service bits.
            pcm_block.sample_rate = f1_srate;
            pcm_block.setEmphasis(f1_emph);
            pcm_block.code = f1_code;
            if(int_format==PCM16X0Deinterleaver::FORMAT_EI)
            {
                // EI format.
                pcm_block.ei_format = true;
            }
            else
            {
                // SI format.
                pcm_block.ei_format = false;
            }

            // Check if data is not pure silence.
            if(pcm_block.isSilent()==false)
            {
                // Check if data block is valid and data was not fixed.
                if((pcm_block.isBlockValid()!=false)/*&&(pcm_block.isDataFixed()==false)*/)
                if((pcm_block.isBlockValid()!=false)&&(pcm_block.hasPickedWord(PCM16X0DataBlock::SUBBLK_1)==false))
                {
                    valid_cnt++;
                }
                // Check if invalid seam masking is enabled.
                if(mask_seams!=false)
                {
                    // Check if stitching was successfull.
                    if((frasm_f1.padding_ok==false)&&(frasm_f1.silence==false))
                    {
                        // Padding was not detected correctly (and not due to silence).
                        if(valid_cnt<3)
                        {
                            // Not enough valid lines from the start of the frame encountered.
                            pcm_block.markAsUnsafe();
                            //already_invalid = true;
#ifdef DI_EN_DBG_OUT
                            if((log_level&LOG_PROCESS)!=0)
                            {
                                QString log_line;
                                log_line.sprintf("[L2B-16x0] Not enough valid blocks from the start of the frame, block [%03u:%03u-%01u] marked as unsafe",
                                     pcm_block.frame_number, pcm_block.start_line, pcm_block.start_part);
                                qInfo()<<log_line;
                            }
#endif
                        }
                    }
                }
                // Check if broken data blocks masking is enabled.
                if(broken_mask_dur>0)
                {
                    // Check for random "BROKEN" data blocks.
                    if(pcm_block.isDataBroken()!=false)
                    {
                        // If broken data block was found - disable P corrections on next lines
                        // to prevent noise.
                        // Reset error-correction prohibition timer.
                        broken_countdown = broken_mask_dur;
#ifdef DI_EN_DBG_OUT
                        if((log_level&LOG_PROCESS)!=0)
                        {
                            QString log_line;
                            log_line.sprintf("[L2B-16x0] Broken block detected at [%03u:%03u-%01u], disabling error correction for %u next lines",
                                 pcm_block.frame_number, pcm_block.start_line, pcm_block.start_part, broken_countdown);
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
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        QString log_line;
                        log_line.sprintf("[L2B-16x0] Marking block at [%03u:%03u-%01u] as 'unsafe' due to broken block above",
                                 pcm_block.frame_number, pcm_block.start_line, pcm_block.start_part);
                        qInfo()<<log_line;
                    }
#endif
                }
            }

            if(pcm_block.isBlockValid()==false)
            {
                // Data block is corrupted.
                frasm_f1.blocks_drop++;
                // Samples in data block are corrupted.
                frasm_f1.samples_drop += pcm_block.getErrorsFixedAudio();
                if(pcm_block.isDataBroken()!=false)
                {
                    // Broken data block detected.
                    frasm_f1.blocks_broken++;
#ifdef DI_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        QString log_line;
                        log_line.sprintf("[L2B-16x0] Broken block from lines [%03u:%03u-%01u...%03u-%01u]!",
                                         pcm_block.frame_number, pcm_block.start_line, pcm_block.start_part,
                                         pcm_block.stop_line, pcm_block.stop_part);
                        qInfo()<<log_line;
                    }
#endif
                }
            }
            else
            {
                // Report P-corrections per sub-block if any.
                if(pcm_block.isDataFixedByP(PCM16X0DataBlock::SUBBLK_1)!=false)
                {
                    // Data is fixed by P-correction.
                    frasm_f1.blocks_fix_p++;
                }
                if(pcm_block.isDataFixedByP(PCM16X0DataBlock::SUBBLK_2)!=false)
                {
                    // Data is fixed by P-correction.
                    frasm_f1.blocks_fix_p++;
                }
                if(pcm_block.isDataFixedByP(PCM16X0DataBlock::SUBBLK_3)!=false)
                {
                    // Data is fixed by P-correction.
                    frasm_f1.blocks_fix_p++;
                }
            }

            // Countdown for trailing invalid data blocks after "BROKEN" one.
            if(broken_countdown>0)
            {
                broken_countdown--;
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    if(broken_countdown==0)
                    {
                        qInfo()<<"[L2B-16x0] Broken block countdown ended.";
                    }
                }
#endif
            }
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_BLOCK_DUMP)!=0)
            {
                qInfo()<<QString::fromStdString("[L2B-16x0] "+pcm_block.dumpContentString());
            }
#endif
            // Add compiled data block into output queue.
            outputDataBlock(&pcm_block);
            // Alternate between order of words (L/R) in each block.
            even_order = !even_order;
            line_in_block++;
        }
        // Remove processed lines from the queue.
        for(uint16_t i=0;i<frame_lim;i++)
        {
            if(conv_queue.size()>0)
            {
                conv_queue.pop_front();
            }
            else
            {
                qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Logic error in [PCM16X0DataStitcher::performDeinterleave()]! Lines per interleave block mismatch at"<<i;
            }
        }
        // Go to next interleave block.
        int_block_cnt++;
    }
}

//------------------------ Set logging level (using [PCM16X0Deinterleaver] defines).
void PCM16X0DataStitcher::setLogLevel(uint16_t new_log)
{
    log_level = new_log;
}

//------------------------ Set PCM-1630 mode/format.
void PCM16X0DataStitcher::setFormat(uint8_t in_set)
{
    if(preset_format!=in_set)
    {
#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==PCM16X0Deinterleaver::FORMAT_EI)
            {
                qInfo()<<"[L2B-16x0] PCM-1630 mode set to 'EI format'.";
            }
            else if(in_set==PCM16X0Deinterleaver::FORMAT_SI)
            {
                qInfo()<<"[L2B-16x0] PCM-1630 mode set to 'SI format'.";
            }
            else if(in_set==PCM16X0Deinterleaver::FORMAT_AUTO)
            {
                qInfo()<<"[L2B-16x0] PCM-1630 mode set to 'automatic detection'.";
            }
            else
            {
                qInfo()<<"[L2B-16x0] Unknown PCM-1630 mode provided, ignored!";
            }
        }
#endif
    }
    if(in_set<PCM16X0Deinterleaver::FORMAT_MAX)
    {
        if(preset_format!=in_set)
        {
            format_changed = true;
        }
        preset_format = in_set;
    }
}

//------------------------ Preset field order.
void PCM16X0DataStitcher::setFieldOrder(uint8_t in_order)
{
#ifdef DI_EN_DBG_OUT
    if(preset_field_order!=in_order)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_order==FrameAsmDescriptor::ORDER_TFF)
            {
                qInfo()<<"[L2B-16x0] Field order preset to 'TFF'.";
            }
            else if(in_order==FrameAsmDescriptor::ORDER_BFF)
            {
                qInfo()<<"[L2B-16x0] Field order preset to 'BFF'.";
            }
            else
            {
                qInfo()<<"[L2B-16x0] Unknown field order provided, ignored!";
            }
        }
    }
#endif
    if((in_order==FrameAsmDescriptor::ORDER_TFF)||(in_order==FrameAsmDescriptor::ORDER_BFF))
    {
        preset_field_order = in_order;
    }
}

//------------------------ Enable/disable P-code error correction.
void PCM16X0DataStitcher::setPCorrection(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(enable_P_code!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-16x0] P-code ECC set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-16x0] P-code ECC set to 'enabled'.";
            }
        }
    }
#endif
    enable_P_code = in_set;
}

//------------------------ Preset audio sample rate.
void PCM16X0DataStitcher::setSampleRatePreset(uint16_t in_srate)
{
    uint16_t sample_rate_preset;
    sample_rate_preset = PCMSamplePair::SAMPLE_RATE_UNKNOWN;
#ifdef DI_EN_DBG_OUT
    if(sample_rate_preset!=in_srate)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_srate==PCMSamplePair::SAMPLE_RATE_44056)
            {
                qInfo()<<"[L2B-16x0] Sample rate preset to '44056 Hz'.";
            }
            else if(in_srate==PCMSamplePair::SAMPLE_RATE_44100)
            {
                qInfo()<<"[L2B-16x0] Sample rate preset to '44100 Hz'.";
            }
            else if(in_srate==PCMSamplePair::SAMPLE_RATE_AUTO)
            {
                qInfo()<<"[L2B-16x0] Sample rate set to 'automatic detection'.";
            }
            else
            {
                qInfo()<<"[L2B-16x0] Unknown sample rate provided, ignored!";
            }
        }
    }
#endif
    // TODO: add support for sample rate preset.
    sample_rate_preset = in_srate;
}

//------------------------ Set fine settings: usage of ECC on CRC-marked words.
void PCM16X0DataStitcher::setFineUseECC(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(ignore_CRC==in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-16x0] ECC usage set to 'enabled'.";
            }
            else
            {
                qInfo()<<"[L2B-16x0] ECC usage set to 'disabled'.";
            }
        }
    }
#endif
    ignore_CRC = !in_set;
    emit guiUpdFineUseECC(!ignore_CRC);
}

//------------------------ Set fine settings: usage of unchecked seams masking.
void PCM16X0DataStitcher::setFineMaskSeams(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(mask_seams!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-16x0] Masking unchecked field seams set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[L2B-16x0] Masking unchecked field seams set to 'enabled'.";
            }
        }
    }
#endif
    mask_seams = in_set;
    emit guiUpdFineMaskSeams(mask_seams);
}

//------------------------ Set fine settings: number of lines to mask after BROKEN data block.
void PCM16X0DataStitcher::setFineBrokeMask(uint8_t in_set)
{
#ifdef DI_EN_DBG_OUT
    if(broken_mask_dur!=in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            qInfo()<<"[L2B-16x0] ECC-disabled lines count after BROKEN data block set to:"<<in_set;
        }
    }
#endif
    broken_mask_dur = in_set;
    emit guiUpdFineBrokeMask(broken_mask_dur);
}

//------------------------ Set fine settings to defaults.
void PCM16X0DataStitcher::setDefaultFineSettings()
{
    setFineUseECC(true);
    setFineMaskSeams(true);
    setFineBrokeMask(UNCH_MASK_DURATION);
}

//------------------------ Get current fine settings.
void PCM16X0DataStitcher::requestCurrentFineSettings()
{
    emit guiUpdFineUseECC(!ignore_CRC);
    emit guiUpdFineMaskSeams(mask_seams);
    emit guiUpdFineBrokeMask(broken_mask_dur);
}

//------------------------ Main processing loop.
void PCM16X0DataStitcher::doFrameReassemble()
{
    uint16_t sublines_per_frame;
    quint64 time_spent;
    size_t queue_size;
    PCM16X0SubLine cur_line;

#ifdef DI_EN_DBG_OUT
    qInfo()<<"[L2B-16x0] Launched, PCM-16x0 thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
#endif
    // Check working pointers.
    if((in_lines==NULL)||(mtx_lines==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-16x0] Empty pointer provided in [PCM16X0DataStitcher::doFrameReassemble()], unable to continue!";
        emit finished();
        return;
    }
    QElapsedTimer time_per_frame;
    // Inf. loop in a thread.
    while(finish_work==false)
    {
        // Process Qt events.
        QApplication::processEvents();
        //qDebug()<<"[DBG] Start loop";
        if(format_changed!=false)
        {
            resetState();
            format_changed = false;
        }

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
            // Wait for sufficient data in the input queue and detect frame number.
            if(waitForOneFrame()!=false)
            {
                // One full frame is available.
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-16x0] -------------------- Detected full frame of data, switching to processing...";
                }
#endif
                time_per_frame.start();

                // Fast fill frame into internal buffer.
                fillUntilFullFrame();

                sublines_per_frame = 0;

                // Remove lines from Frame A from input queue.
                cur_line = in_lines->front();
                while(1)
                {
                    // Check input queue size.
                    if(in_lines->size()>0)
                    {
                        // Get first sub-line.
                        cur_line = in_lines->front();
                        // Check frame number.
                        if(cur_line.frame_number==frasm_f1.frame_number)
                        {
                            // Sub-line from Frame A.
                            sublines_per_frame++;
                            // Remove sub-line from the input.
                            in_lines->pop_front();
                        }
                        else
                        {
                            // No more sub-lines from Frame A.
                            break;
                        }
                    }
                    else
                    {
                        // No more sub-lines available.
                        break;
                    }
                }
                // Unlock shared access.
                mtx_lines->unlock();

#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-16x0] Removed"<<sublines_per_frame<<"sub-lines from input queue from frame"<<frasm_f1.frame_number;
                }
#endif
                // Set deinterleaver logging parameters.
                uint8_t ext_di_log_lvl = 0;
                if((log_level&LOG_SETTINGS)!=0)
                {
                    ext_di_log_lvl |= PCM16X0Deinterleaver::LOG_SETTINGS;
                }
                if((log_level&LOG_DEINTERLEAVE)!=0)
                {
                    ext_di_log_lvl |= PCM16X0Deinterleaver::LOG_PROCESS;
                }
                if((log_level&LOG_ERROR_CORR)!=0)
                {
                    ext_di_log_lvl |= PCM16X0Deinterleaver::LOG_ERROR_CORR;
                }
                lines_to_block.setLogLevel(ext_di_log_lvl);

                // Detect trimming for the frame in the buffer and start/end file marks.
                findFrameTrim();

                if(file_start!=false)
                {
                    // Reset internal state.
                    resetState();
                }

                // Check if file ended.
                if(file_end==false)
                {
                    // Split frame buffer into 2x field buffers.
                    splitFrameToFields();

                    // Invalidate false-positive CRCs.
                    prescanForFalsePosCRCs(&frame1_odd, frasm_f1.odd_data_lines);
                    prescanForFalsePosCRCs(&frame1_even, frasm_f1.even_data_lines);

                    if(preset_format!=PCM16X0Deinterleaver::FORMAT_EI)
                    {
                        // Find coordinates of interleave blocks in fields for SI format.
                        findSIDataAlignment();
                    }
                    else
                    {
                        // Find interframe padding and line offset of interleave block for EI format.
                        findEIFrameStitching();
                    }

                    // Inform processing chain about new file.
                    if(file_start!=false)
                    {
                        outputFileStart();
                    }

                    // Fill up PCM line queue from internal field buffers.
                    fillFrameForOutput();

                    // Perform deinterleaving on one frame.
                    performDeinterleave(preset_format);

                    // Convert from sub-lines to lines before reporting.
                    frasm_f1.odd_data_lines = frasm_f1.odd_data_lines/PCM16X0SubLine::SUBLINES_PER_LINE;
                    frasm_f1.even_data_lines = frasm_f1.even_data_lines/PCM16X0SubLine::SUBLINES_PER_LINE;
                    frasm_f1.odd_valid_lines = frasm_f1.odd_valid_lines/PCM16X0SubLine::SUBLINES_PER_LINE;
                    frasm_f1.even_valid_lines = frasm_f1.even_valid_lines/PCM16X0SubLine::SUBLINES_PER_LINE;
                    frasm_f1.odd_std_lines = frasm_f1.even_std_lines = LINES_PF;
                    // Report frame assembling parameters.
                    emit guiUpdFrameAsm(frasm_f1);

                    // Report time that frame processing took.
                    time_spent = time_per_frame.nsecsElapsed();
                    time_spent = time_spent/1000;
                    emit loopTime(time_spent);
                }
                else
                {
                    // Inform processing chain about end of file.
                    outputFileStop();
                    // Reset internal state.
                    resetState();
                }

                // Reset trim data.
                frasm_f1.clear();
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
    qInfo()<<"[L2B-16x0] Loop stop.";
    emit finished();
}

//------------------------ Set the flag to break execution loop and exit.
void PCM16X0DataStitcher::stop()
{
#ifdef DI_EN_DBG_OUT
    qInfo()<<"[L2B-16x0] Received termination request";
#endif
    finish_work = true;
}
