#include "pcm1datastitcher.h"

PCM1DataStitcher::PCM1DataStitcher(QObject *parent) : QObject(parent)
{
    // Reset pointers to input/output queues.
    in_lines = NULL;
    out_samples = NULL;
    mtx_lines = NULL;
    mtx_samples = NULL;

    // Set internal frame and field buffer sizes.
    trim_buf.resize(BUF_SIZE_TRIM);
    frame1_odd.resize(BUF_SIZE_FIELD);
    frame1_even.resize(BUF_SIZE_FIELD);

    file_name.clear();

    preset_field_order = FrameAsmDescriptor::ORDER_TFF;
    preset_odd_offset = preset_even_offset = 0;

    log_level = 0;
    trim_fill = 0;

    auto_offset = true;
    file_start = file_end = false;
    finish_work = false;

    // Reset internal state.
    resetState();
    // Preset default fine parameters.
    setDefaultFineSettings();
}

//------------------------ Set pointers to shared input data.
void PCM1DataStitcher::setInputPointers(std::deque<PCM1Line> *in_pcmline, QMutex *mtx_pcmline)
{
    if((in_pcmline==NULL)||(mtx_pcmline==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-1] Empty input pointer provided, unable to apply!";
    }
    else
    {
        in_lines = in_pcmline;
        mtx_lines = mtx_pcmline;
    }
}

//------------------------ Set pointers to shared output data.
void PCM1DataStitcher::setOutputPointers(std::deque<PCMSamplePair> *out_pcmsamples, QMutex *mtx_pcmsamples)
{
    if((out_pcmsamples==NULL)||(mtx_pcmsamples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-1] Empty output pointer provided, unable to apply!";
    }
    else
    {
        out_samples = out_pcmsamples;
        mtx_samples = mtx_pcmsamples;
    }
}

//------------------------ Reset all stats from the last file, start from scratch.
void PCM1DataStitcher::resetState()
{
    // Clear queue.
    conv_queue.clear();
    // Set default assembling parameters.
    header_present = emphasis_set = false;
    frasm_f1.clearMisc();
#ifdef DI_EN_DBG_OUT
    //if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-1] Internal state is reset";
    }
#endif
}

//------------------------ Wait until one full frame is in the input queue.
bool PCM1DataStitcher::waitForOneFrame()
{
    bool frame1_lock;
    std::deque<PCM1Line>::iterator buf_scaner;

    frame1_lock = false;

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
                    qInfo()<<"[L2B-1] Detected end of the Frame, exiting search...";
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
                qInfo()<<"[L2B-1] EOF detected, frame"<<(*buf_scaner).frame_number<<"line"<<(*buf_scaner).line_number;
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
void PCM1DataStitcher::fillUntilFullFrame()
{
    std::deque<PCM1Line>::iterator buf_scaner;

    // Reset internal buffer fill counter.
    trim_fill = 0;

#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-1] Copying full frame from the input queue...";
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
                    log_line.sprintf("[L2B-1] Frame end at line %u-%u, fill index: %u", (*buf_scaner).frame_number, (*buf_scaner).line_number, trim_fill);
                    qInfo()<<log_line;
                }
#endif
                // One frame is enough.
                break;
            }
            else if(trim_fill<BUF_SIZE_TRIM)
            {
                // Copy data into internal buffer.
                trim_buf[trim_fill] = (*buf_scaner);
#ifdef DI_LOG_BUF_FILL_VERBOSE
                if((log_level&LOG_PROCESS)!=0)
                {
                    QString log_line;
                    log_line.sprintf("[L2B-1] Added line %u-%u (%u) into buffer at index: %u",
                                     (*buf_scaner).frame_number,
                                     (*buf_scaner).line_number,
                                     (*buf_scaner).isServiceLine(),
                                     trim_fill);
                    qInfo()<<log_line;
                }
#endif
                trim_fill++;
            }
            else
            {
                qWarning()<<DBG_ANCHOR<<"[L2B-1] Line buffer index out of bound! Logic error! Line skipped!";
                qWarning()<<DBG_ANCHOR<<"[L2B-1] Max lines:"<<BUF_SIZE_TRIM;
            }
        }
        // Go to the next line in the input.
        buf_scaner++;
    }
#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[L2B-16x0] Filled"<<trim_fill<<"lines";
    }
#endif
}

//------------------------ Detect frame trimming (how many lines to skip from top and bottom of each field).
void PCM1DataStitcher::findFrameTrim()
{
    uint16_t line_ind, f1o_good, f1e_good;
    bool f1e_top, f1e_bottom, f1o_top, f1o_bottom;
    bool f1o_skip_bad, f1e_skip_bad;
    bool data_started_odd, data_started_even;

    data_started_odd = data_started_even = header_present = emphasis_set = file_start = file_end = false;
    f1e_top = f1e_bottom = f1o_top = f1o_bottom = false;
    f1o_skip_bad = f1e_skip_bad = false;
    f1o_good = f1e_good = 0;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-1] -------------------- Trim search starting...";
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
            // Check for service lines in the input.
            if(trim_buf[line_ind].isServiceLine()==false)
            {
                // Current line is not a service one.
                if(trim_buf[line_ind].isCRCValid()!=false)
                {
                    // Binarization gave good result.
                    if((trim_buf[line_ind].line_number%2)==0)
                    {
                        // Line from even field.
                        data_started_even = true;
                        f1e_good++;
                        if(f1e_good>MIN_GOOD_LINES_PF)
                        {
                            f1e_skip_bad = true;
                        }
                    }
                    else
                    {
                        // Line from odd field.
                        data_started_odd = true;
                        f1o_good++;
                        if(f1o_good>MIN_GOOD_LINES_PF)
                        {
                            f1o_skip_bad = true;
                        }
                    }
                }
            }
            else if(trim_buf[line_ind].isServHeader()!=false)
            {
                // Line contains Service line with Header.
                if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
                {
                    // Frame number is ok.
                    if((trim_buf[line_ind].line_number%2)==0)
                    {
                        // Line from even field.
                        if(data_started_even==false)
                        {
                            // PCM data has not started yet.
                            // Set Header flag.
                            header_present = true;
                        }
                    }
                    else
                    {
                        // Line from odd field.
                        if(data_started_odd==false)
                        {
                            // PCM data has not started yet.
                            // Set Header flag.
                            header_present = true;
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
    // Reset data flags.
    data_started_odd = data_started_even = false;
    // Do backwards search for emphasis line.
    line_ind = trim_fill;
    while(line_ind>0)
    {
        // Go to next line.
        line_ind--;
        if(trim_buf[line_ind].isServiceLine()==false)
        {
            // Current line is not a service one.
            if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
            {
                // Frame number is ok.
                if(trim_buf[line_ind].isCRCValid()!=false)
                {
                    // Binarization gave good result.
                    if((trim_buf[line_ind].line_number%2)==0)
                    {
                        // Line from even field.
                        data_started_even = true;
                        if(data_started_odd!=false)
                        {
                            // Data has started in both fields.
                            break;
                        }
                    }
                    else
                    {
                        // Line from odd field.
                        data_started_odd = true;
                        if(data_started_even!=false)
                        {
                            // Data has started in both fields.
                            break;
                        }
                    }
                }
            }
        }
        else if(trim_buf[line_ind].isServHeader()!=false)
        {
            // Line contains Header.
            if(trim_buf[line_ind].frame_number==frasm_f1.frame_number)
            {
                // Frame number is ok.
                if((trim_buf[line_ind].line_number%2)==0)
                {
                    // Line from even field.
                    if(data_started_even==false)
                    {
                        // PCM data has not started yet.
                        // Set Emphasis flag.
                        emphasis_set = true;
                    }
                }
                else
                {
                    // Line from odd field.
                    if(data_started_odd==false)
                    {
                        // PCM data has not started yet.
                        // Set Emphasis flag.
                        emphasis_set = true;
                    }
                }
            }
        }
    }

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_TRIM)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        if(file_start!=false)
        {
            qInfo()<<"[L2B-1] New file tag detected in the frame";
        }
        if(file_end!=false)
        {
            qInfo()<<"[L2B-1] File end tag detected in the frame";
        }
        if(header_present!=false)
        {
            qInfo()<<"[L2B-1] Header detected in the frame, top lines are starting ones";
        }
        if(emphasis_set!=false)
        {
            qInfo()<<"[L2B-1] Emphasis is set for the frame";
        }
        if(f1o_skip_bad!=false)
        {
            qInfo()<<"[L2B-1]"<<f1o_good<<"lines with good CRC in odd field, aggresive trimming enabled for odd field";
        }
        if(f1e_skip_bad!=false)
        {
            qInfo()<<"[L2B-1]"<<f1e_good<<"lines with good CRC in even field, aggresive trimming enabled for even field";
        }
    }
#endif

    if(auto_offset==false)
    {
        // Set top data offset to preset one.
        f1o_top = f1e_top = true;
        // Odd field offset.
        if(preset_odd_offset>0)
        {
            frasm_f1.odd_top_data = 2*preset_odd_offset+1;
        }
        else
        {
            frasm_f1.odd_top_data = 1;
        }
        // Even field offset.
        if(preset_even_offset>0)
        {
            frasm_f1.even_top_data = 2*preset_even_offset+2;
        }
        else
        {
            frasm_f1.even_top_data = 2;
        }
    }

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
                qInfo()<<"[L2B-1] Skipping service line at"<<trim_buf[line_ind].frame_number<<"/"<<trim_buf[line_ind].line_number;
            }
#endif
            // Skip service lines.
            continue;
        }

#ifdef DI_LOG_TRIM_VERBOSE
        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
        {
            QString log_line;
            log_line.sprintf("[L2B-1] Check: at %04u, line: %03u-%03u, PCM: ",
                             line_ind,
                             trim_buf[line_ind].frame_number,
                             trim_buf[line_ind].line_number);
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
                        // Set number of the line that PCM is starting.
                        frasm_f1.even_top_data = trim_buf[line_ind].line_number;
                        // Detect only first encounter in the buffer.
                        f1e_top = true;
#ifdef DI_EN_DBG_OUT
                        if(((log_level&LOG_TRIM)!=0)&&((log_level&LOG_PROCESS)!=0))
                        {
                            qInfo()<<"[L2B-1] Frame even field new top trim:"<<frasm_f1.even_top_data;
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
                if(((f1o_skip_bad==false)&&(trim_buf[line_ind].hasBWSet()!=false))||
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
                            qInfo()<<"[L2B-1] Frame odd field new top trim:"<<frasm_f1.odd_top_data;
                        }
#endif
                    }
                    // Update last line with PCM from the bottom.
                    frasm_f1.odd_bottom_data = trim_buf[line_ind].line_number;
                    f1o_bottom = true;
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
        log_line = "[L2B-1] Frame ("+QString::number(frasm_f1.frame_number)+") trim (OT, OB, ET, EB): ";
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

//------------------------ Crop one sub-line from the video line.
void PCM1DataStitcher::splitLineToSubline(PCM1Line *in_line, PCM1SubLine *out_sub, uint8_t part)
{
    // Reset sub-line fields.
    out_sub->clear();
    // Copy source frame and line numbers.
    out_sub->frame_number = in_line->frame_number;
    out_sub->line_number = in_line->line_number;
    // Copy BW detection flag.
    out_sub->setBWLevels(in_line->hasBWSet());
    // Set line part.
    out_sub->setLinePart(part);
    if(part==PCM1SubLine::PART_LEFT)
    {
        // Copy data words.
        out_sub->setLeft(in_line->getWord(PCM1Line::WORD_L2));
        out_sub->setRight(in_line->getWord(PCM1Line::WORD_R2));
        // Copy count of picked bits from the leftmost word.
        out_sub->picked_bits_left = in_line->picked_bits_left;
    }
    else if(part==PCM1SubLine::PART_MIDDLE)
    {
        // Copy data words.
        out_sub->setLeft(in_line->getWord(PCM1Line::WORD_L4));
        out_sub->setRight(in_line->getWord(PCM1Line::WORD_R4));
    }
    else if(part==PCM1SubLine::PART_RIGHT)
    {
        // Copy data words.
        out_sub->setLeft(in_line->getWord(PCM1Line::WORD_L6));
        out_sub->setRight(in_line->getWord(PCM1Line::WORD_R6));
    }
    // Copy count of picked bits from the whole line CRC.
    out_sub->picked_bits_right = in_line->picked_bits_right;
    // Copy CRC validity flag.
    out_sub->setCRCValid(in_line->isCRCValid());
}

//------------------------ Split frame into 2 internal buffers per field.
void PCM1DataStitcher::splitFrameToFields()
{
    uint16_t line_ind, line_num;
    uint32_t ref_lvl_odd, ref_lvl_even, ref_lvl_odd_bad, ref_lvl_even_bad;
    PCM1Line current_line;
    PCM1SubLine sub_temp;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-1] -------------------- Splitting frame into field buffers...";
        qInfo()<<"[L2B-1]"<<trim_fill<<"sub-lines in the buffer";
    }
#endif

    line_ind = 0;
    ref_lvl_odd = ref_lvl_even = ref_lvl_odd_bad = ref_lvl_even_bad = 0;
    f1_max_line = 0;

    // Cycle splitting frame buffer into 2 field buffers.
    while(line_ind<trim_fill)
    {
        current_line = trim_buf[line_ind];
        // Save current line number.
        line_num = current_line.line_number;
        // Check for stray service line.
        if(current_line.isServiceLine()!=false)
        {
            if(trim_buf[line_ind].isServFiller()==false)
            {
                // Skip stray unfiltered or lost sync service line.
                line_ind++;
                continue;
            }
        }
        // Pick lines for the frame.
        if(current_line.frame_number==frasm_f1.frame_number)
        {
            // Update maximum line number.
            if(f1_max_line<line_num)
            {
                f1_max_line = line_num;
            }
            // Fill up the frame, splitting fields.
            if((line_num%2)==0)
            {
                // Check if lines are available after trimming (not top = bottom = current = 0).
                if((frasm_f1.even_top_data!=frasm_f1.even_bottom_data)||(frasm_f1.even_top_data!=0))
                {
                    // Check frame trimming.
                    if((line_num>=frasm_f1.even_top_data)&&(line_num<=frasm_f1.even_bottom_data))
                    {
                        // Cycle through sub-lines in one line.
                        for(uint8_t sub=0;sub<PCM1SubLine::PART_MAX;sub++)
                        {
                            // Check array index bounds.
                            if(frasm_f1.even_data_lines<BUF_SIZE_FIELD)
                            {
                                // Fill up even field.
                                splitLineToSubline(&current_line, &sub_temp, sub);
                                frame1_even[frasm_f1.even_data_lines] = sub_temp;
                                frasm_f1.even_data_lines++;
                                // Pre-calculate average reference level for all lines.
                                ref_lvl_even_bad += current_line.ref_level;
                                if(current_line.isCRCValid()!=false)
                                {
                                    // Calculate number of sub-lines with valid CRC in the field.
                                    frasm_f1.even_valid_lines++;
                                    // Pre-calculate average reference level for valid lines.
                                    ref_lvl_even += current_line.ref_level;
                                }
                            }
#ifdef DI_EN_DBG_OUT
                            else
                            {
                                if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
                                {
                                    QString log_line;
                                    log_line.sprintf("[L2B-1] Even field buffer is full, line %u-%u is skipped!",
                                                     current_line.frame_number,
                                                     current_line.line_number);
                                    qInfo()<<log_line;
                                }
                            }
#endif
                        }
                    }
                }
#ifdef DI_LOG_NOLINE_SKIP_VERBOSE
                else
                {
                    if(((log_level&LOG_PADDING)!=0)&&((log_level&LOG_PROCESS)!=0))
                    {
                        qInfo()<<"[L2B-1] Frame no line even field skip";
                    }
                }
#endif
            }
            else
            {
                // Check frame trimming.
                if((line_num>=frasm_f1.odd_top_data)&&(line_num<=frasm_f1.odd_bottom_data))
                {
                    // Check for stray header inside the data.
                    if(current_line.isServiceLine()!=false)
                    {
                        // Make this line invalid.
                        current_line.clear();
                    }
                    // Cycle through sub-lines in one line.
                    for(uint8_t sub=0;sub<PCM1SubLine::PART_MAX;sub++)
                    {
                        // Check array index bounds.
                        if(frasm_f1.odd_data_lines<BUF_SIZE_FIELD)
                        {
                            // Fill up odd field.
                            splitLineToSubline(&current_line, &sub_temp, sub);
                            frame1_odd[frasm_f1.odd_data_lines] = sub_temp;
                            frasm_f1.odd_data_lines++;
                            // Pre-calculate average reference level for all lines.
                            ref_lvl_odd_bad += current_line.ref_level;
                            if(current_line.isCRCValid()!=false)
                            {
                                // Calculate number of sub-lines with valid CRC in the field.
                                frasm_f1.odd_valid_lines++;
                                // Pre-calculate average reference level for valid lines.
                                ref_lvl_odd += current_line.ref_level;
                            }
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
                            {
                                QString log_line;
                                log_line.sprintf("[L2B-1] Odd field buffer is full, line %u-%u is skipped!",
                                                 current_line.frame_number,
                                                 current_line.line_number);
                                qInfo()<<log_line;
                            }
                        }
#endif
                    }
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
        log_line.sprintf("[L2B-1] Odd field sub-lines: %u (%u valid), even field sub-lines: %u (%u valid)",
                         frasm_f1.odd_data_lines,
                         frasm_f1.odd_valid_lines,
                         frasm_f1.even_data_lines,
                         frasm_f1.even_valid_lines);
        qInfo()<<log_line;
        log_line.sprintf("[L2B-007] Average reference level: %03u/%03u (odd/even)", frasm_f1.odd_ref, frasm_f1.even_ref);
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Calculate padding for each field.
void PCM1DataStitcher::findFramePadding()
{
#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-1] -------------------- Padding detection starting...";
    }
#endif
    if(auto_offset!=false)
    {
        // Check if data starts with Header.
        if(header_present==false)
        {
            // No Header, assume top part of the frame is cut.
            // Stick trimmed data to the bottom of the field.
            frasm_f1.odd_bottom_padding = 0;
            frasm_f1.even_bottom_padding = 0;
            // Calculate top padding to NTSC standard.
            frasm_f1.odd_top_padding = (SUBLINES_PF-frasm_f1.odd_data_lines)/PCM1Line::SUBLINES_PER_LINE;
            frasm_f1.even_top_padding = (SUBLINES_PF-frasm_f1.even_data_lines)/PCM1Line::SUBLINES_PER_LINE;
        }
        else
        {
            // Header is detected in the frame.
            // Stick data to the top of the field.
            frasm_f1.odd_top_padding = 0;
            frasm_f1.even_top_padding = 0;
            // Calculate top padding to NTSC standard.
            frasm_f1.odd_bottom_padding = (SUBLINES_PF-frasm_f1.odd_data_lines)/PCM1Line::SUBLINES_PER_LINE;
            frasm_f1.even_bottom_padding = (SUBLINES_PF-frasm_f1.even_data_lines)/PCM1Line::SUBLINES_PER_LINE;
        }
    }
    else
    {
        // Calculate odd field top padding.
        if(preset_odd_offset>0)
        {
            // Data if shifted upwards, no need for top padding.
            frasm_f1.odd_top_padding = 0;
        }
        else
        {
            // Data if shifted downwards, convert preset offset to top padding,
            // assuming top of the data not trimmed, but set by frame line number in [findFrameTrim()].
            frasm_f1.odd_top_padding = 0-preset_odd_offset;
        }
        // Calculate even field top padding.
        if(preset_even_offset>0)
        {
            // Data if shifted upwards, no need for top padding.
            frasm_f1.even_top_padding = 0;
        }
        else
        {
            // Data if shifted downwards, convert preset offset to top padding,
            // assuming top of the data not trimmed, but set by frame line number in [findFrameTrim()].
            frasm_f1.even_top_padding = 0-preset_even_offset;
        }
        // Re-calculate bottom data trim to fit within standard number of lines per field.
        frasm_f1.odd_bottom_padding = (frasm_f1.odd_bottom_data-frasm_f1.odd_top_data)/2+1;     // Number of data lines.
        frasm_f1.odd_bottom_padding += frasm_f1.odd_top_padding;                                // Data lines + top padding.
        if(frasm_f1.odd_bottom_padding>LINES_PF)
        {
            frasm_f1.odd_bottom_padding -= LINES_PF;                                        // Calculate how many excessive lines there are.
            frasm_f1.odd_bottom_data -= (frasm_f1.odd_bottom_padding*2);                    // Trim bottom more (skipping every other line due to interlacing).
            frasm_f1.odd_data_lines = (frasm_f1.odd_bottom_data-frasm_f1.odd_top_data)/2+1; // Re-calculate number of useful lines.
            frasm_f1.odd_data_lines *= PCM1Line::SUBLINES_PER_LINE;                         // Convert number of lines to number of sublines.
        }
        // Re-calculate bottom data trim to fit within standard number of lines per field.
        frasm_f1.even_bottom_padding = (frasm_f1.even_bottom_data-frasm_f1.even_top_data)/2+1;  // Number of data lines.
        frasm_f1.even_bottom_padding += frasm_f1.even_top_padding;                              // Data lines + top padding.
        if(frasm_f1.even_bottom_padding>LINES_PF)
        {
            frasm_f1.even_bottom_padding -= LINES_PF;                                       // Calculate how many excessive lines there are.
            frasm_f1.even_bottom_data -= (frasm_f1.even_bottom_padding*2);                  // Trim bottom more (skipping every other line due to interlacing).
            frasm_f1.even_data_lines = (frasm_f1.even_bottom_data-frasm_f1.even_top_data)/2+1;  // Re-calculate number of useful lines.
            frasm_f1.even_data_lines *= PCM1Line::SUBLINES_PER_LINE;                        // Convert number of lines to number of sublines.
        }
        // Calculate bottom padding to NTSC standard.
        frasm_f1.odd_bottom_padding = (SUBLINES_PF-frasm_f1.odd_data_lines)/PCM1Line::SUBLINES_PER_LINE-frasm_f1.odd_top_padding;
        frasm_f1.even_bottom_padding = (SUBLINES_PF-frasm_f1.even_data_lines)/PCM1Line::SUBLINES_PER_LINE-frasm_f1.even_top_padding;
    }

    //qDebug()<<"po"<<frasm_f1.odd_top_data<<frasm_f1.odd_bottom_data<<frasm_f1.odd_data_lines/PCM1Line::SUBLINES_PER_LINE<<frasm_f1.odd_top_padding<<frasm_f1.odd_bottom_padding;
    //qDebug()<<"pe"<<frasm_f1.even_top_data<<frasm_f1.even_bottom_data<<frasm_f1.even_data_lines/PCM1Line::SUBLINES_PER_LINE<<frasm_f1.even_top_padding<<frasm_f1.even_bottom_padding;

    // Preset field order.
    if(preset_field_order==FrameAsmDescriptor::ORDER_BFF)
    {
        frasm_f1.presetBFF();
    }
    else
    {
        frasm_f1.presetTFF();
    }
#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PADDING)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        QString log_line;
        qInfo()<<"[L2B-1] ----------- Padding results:";
        log_line.sprintf("[L2B-1] Frame %u odd field padding: Calc", frasm_f1.frame_number);
        log_line += " (top: "+QString::number(frasm_f1.odd_top_padding);
        log_line += ", lines: "+(QString::number(frasm_f1.odd_data_lines/PCM1Line::SUBLINES_PER_LINE));
        log_line += " (sub-lines: "+QString::number(frasm_f1.odd_data_lines);
        log_line += "), bottom: "+QString::number(frasm_f1.odd_bottom_padding)+")";
        qInfo()<<log_line;
        log_line.sprintf("[L2B-1] Frame %u even field padding: Calc", frasm_f1.frame_number);
        log_line += " (top: "+QString::number(frasm_f1.even_top_padding);
        log_line += ", lines: "+(QString::number(frasm_f1.even_data_lines/PCM1Line::SUBLINES_PER_LINE));
        log_line += " (sub-lines: "+QString::number(frasm_f1.even_data_lines);
        log_line += "), bottom: "+QString::number(frasm_f1.even_bottom_padding)+")";
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Get first line number for first field to fill.
uint16_t PCM1DataStitcher::getFirstFieldLineNum(uint8_t in_order)
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
uint16_t PCM1DataStitcher::getSecondFieldLineNum(uint8_t in_order)
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
uint16_t PCM1DataStitcher::addLinesFromField(std::vector<PCM1SubLine> *field_buf, uint16_t ind_start, uint16_t count, uint16_t *last_line_num)
{
    uint16_t lines_cnt;
    lines_cnt = 0;

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
                (*field_buf)[index].line_number = *last_line_num;
                // Copy PCM line into queue.
                conv_queue.push_back((*field_buf)[index]);
                if((last_line_num!=NULL)&&((*field_buf)[index].getLinePart()==PCM1SubLine::PART_RIGHT))
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
                qInfo()<<"[L2B-1]"<<lines_cnt<<"sub-lines from field ("<<ind_start<<"..."<<(ind_start+count-1)<<"), #("<<min_line_num<<"..."<<max_line_num<<") inserted into queue";
            }
#endif
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if((log_level&LOG_FIELD_ASSEMBLY)!=0)
            {
                qInfo()<<"[L2B-1] No sub-lines from field inserted";
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-1] Array access out of bounds! Total:"<<field_buf->size()<<", start index:"<<ind_start<<", stop index:"<<(ind_start+count);
    }
    return lines_cnt;
}

//------------------------ Fill output line buffer with empty lines.
uint16_t PCM1DataStitcher::addFieldPadding(uint32_t in_frame, uint16_t line_cnt, uint16_t *last_line_num)
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

    PCM1SubLine empty_line;
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
        for(uint8_t part_cnt=0;part_cnt<PCM1SubLine::PART_MAX;part_cnt++)
        {
            // Set line part.
            empty_line.setLinePart(part_cnt);
            // Push padding line into queue.
            conv_queue.push_back(empty_line);
            lines_cnt++;
        }
    }
#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_FIELD_ASSEMBLY)!=0)
    {
        qInfo()<<"[L2B-1]"<<lines_cnt<<"padding sub-lines #("<<min_line_num<<"..."<<max_line_num<<") inserted into queue";
    }
#endif
    return lines_cnt;
}

//------------------------ Fill in first field into queue for conversion from field buffer, inserting padding.
void PCM1DataStitcher::fillFirstFieldForOutput()
{
    uint8_t cur_field_order;
    uint16_t last_line, field_1_cnt, field_1_top_pad, field_1_bottom_pad, target_lines_per_field, added_lines_cnt;
    std::vector<PCM1SubLine> *p_field_1;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-1] -------------------- First field reassembling starting...";
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
        field_1_cnt = frasm_f1.odd_data_lines;
        p_field_1 = &frame1_odd;
#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0))
        {
            qInfo()<<"[L2B-1] Field order: TFF";
        }
#endif
    }
    else
    {
        // Field order is set to BFF.
        // First field = even, second = odd.
        field_1_top_pad = frasm_f1.even_top_padding;
        field_1_bottom_pad = frasm_f1.even_bottom_padding;
        field_1_cnt = frasm_f1.even_data_lines;
        p_field_1 = &frame1_even;
#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0))
        {
            qInfo()<<"[L2B-1] Field order: BFF";
        }
#endif
    }

    added_lines_cnt = 0;
    // Reset line number.
    last_line = getFirstFieldLineNum(cur_field_order);
    // Add first field top padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_1_top_pad, &last_line);
    // Add lines from first field (full field).
    added_lines_cnt += addLinesFromField(p_field_1, 0, field_1_cnt, &last_line);
    // Add first field bottom padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_1_bottom_pad, &last_line);

#ifdef DI_EN_DBG_OUT
        if((target_lines_per_field!=0)&&(target_lines_per_field!=added_lines_cnt))
        {
            qWarning()<<"[L2B-1] Addded"<<added_lines_cnt<<"sub-lines per field (WRONG COUNT, should be"<<target_lines_per_field<<")";
        }
        else if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FIELD_ASSEMBLY)!=0))
        {
            qInfo()<<"[L2B-1] Addded"<<added_lines_cnt<<"sub-lines per field";
        }
#endif
}

//------------------------ Fill in second field into queue for conversion from field buffer, inserting padding.
void PCM1DataStitcher::fillSecondFieldForOutput()
{
    uint8_t cur_field_order;
    uint16_t last_line, field_2_cnt, field_2_top_pad, field_2_bottom_pad, target_lines_per_field, added_lines_cnt;
    std::vector<PCM1SubLine> *p_field_2;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[L2B-1] -------------------- Second field reassembling starting...";
    }
#endif

    cur_field_order = frasm_f1.field_order;
    target_lines_per_field = SUBLINES_PF;

    // Select field data to add lines from.
    if(cur_field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        // Field order is set to TFF.
        // First field = odd, second = even.
        field_2_top_pad = frasm_f1.even_top_padding;
        field_2_bottom_pad = frasm_f1.even_bottom_padding;
        field_2_cnt = frasm_f1.even_data_lines;
        p_field_2 = &frame1_even;
#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0))
        {
            qInfo()<<"[L2B-1] Field order: TFF";
        }
#endif
    }
    else
    {
        // Field order is set to BFF.
        // First field = even, second = odd.
        field_2_top_pad = frasm_f1.odd_top_padding;
        field_2_bottom_pad = frasm_f1.odd_bottom_padding;
        field_2_cnt = frasm_f1.odd_data_lines;
        p_field_2 = &frame1_odd;
#ifdef DI_EN_DBG_OUT
        if(((log_level&LOG_FIELD_ASSEMBLY)!=0)||((log_level&LOG_PROCESS)!=0))
        {
            qInfo()<<"[L2B-1] Field order: BFF";
        }
#endif
    }

    added_lines_cnt = 0;
    // Reset line number.
    last_line = getSecondFieldLineNum(cur_field_order);
    // Add second field top padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_2_top_pad, &last_line);
    // Add lines from second field (full field).
    added_lines_cnt += addLinesFromField(p_field_2, 0, field_2_cnt, &last_line);
    // Add second field bottom padding.
    added_lines_cnt += addFieldPadding(frasm_f1.frame_number, field_2_bottom_pad, &last_line);

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FIELD_ASSEMBLY)!=0))
    {
        if((target_lines_per_field!=0)&&(target_lines_per_field!=added_lines_cnt))
        {
            qInfo()<<"[L2B-1] Addded"<<added_lines_cnt<<"sub-lines per field (WRONG COUNT, should be"<<target_lines_per_field<<")";
        }
        else
        {
            qInfo()<<"[L2B-1] Addded"<<added_lines_cnt<<"sub-lines per field";
        }
    }
#endif
}

//------------------------ Set data block sample rate.
void PCM1DataStitcher::setBlockSampleRate(PCM1DataBlock *in_block)
{
    // TODO: add support for sample rate preset.
    in_block->sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
    frasm_f1.odd_sample_rate = frasm_f1.even_sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
}

//------------------------ Output service tag "New file started".
void PCM1DataStitcher::outputFileStart()
{
    size_t queue_size;
    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-1] Empty pointer provided, service tag discarded!";
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
                    qInfo()<<"[L2B-1] Service tag 'NEW FILE' written.";
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
void PCM1DataStitcher::outputDataBlock(PCM1DataBlock *in_block)
{
    size_t queue_size;
    bool size_lock;
    size_lock = false;

    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-1] Empty pointer provided, result discarded!";
    }
    else
    {
        PCMSamplePair sample_pair;
        for(uint8_t wrd=0;wrd<in_block->getWordCount();wrd+=2)
        {
            while(1)
            {
                // Try to put processed words into the queue.
                mtx_samples->lock();
                queue_size = (out_samples->size()+1);
                if(queue_size<(MAX_SAMPLEPAIR_QUEUE_SIZE-1))
                {
                    // Set emphasis state of the samples.
                    sample_pair.setEmphasis(in_block->hasEmphasis());
                    // Set sample rate.
                    sample_pair.setSampleRate(in_block->sample_rate);
                    // Output L+R samples.
                    sample_pair.setSample(PCMSamplePair::CH_LEFT, in_block->getSample(wrd), in_block->isBlockValid(), in_block->isWordValid(wrd), false);
                    sample_pair.setSample(PCMSamplePair::CH_RIGHT, in_block->getSample(wrd+1), in_block->isBlockValid(), in_block->isWordValid(wrd+1), false);
                    // Put sample pair in the output queue.
                    out_samples->push_back(sample_pair);
                    mtx_samples->unlock();
                    if(size_lock!=false)
                    {
                        size_lock = false;
#ifdef DI_EN_DBG_OUT
                        if((log_level&LOG_PROCESS)!=0)
                        {
                            qInfo()<<"[L2B-1] Output PCM data blocks queue has some space, continuing...";
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
                            qInfo()<<"[L2B-1] Output queue is at size limit ("<<(MAX_SAMPLEPAIR_QUEUE_SIZE-1)<<"), waiting...";
                        }
#endif
                    }
                    QThread::msleep(100);
                }
            }
        }
        // Output data block for visualization.
        emit newBlockProcessed(*in_block);
    }
}

//------------------------ Output service tag "File ended".
void PCM1DataStitcher::outputFileStop()
{
    size_t queue_size;
    if((out_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-1] Empty pointer provided, service tag discarded!";
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
                    qInfo()<<"[L2B-1] Service tag 'FILE END' written.";
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
        qDebug()<<"[L2B-1] File processed in"<<file_time.elapsed()<<"msec";
    }
}

//------------------------ Perform deinterleave of one field in [conv_queue].
void PCM1DataStitcher::performDeinterleave()
{
    std::deque<PCM1SubLine>::iterator buf_scaner;
    PCM1DataBlock pcm_block;

    // Set parameters for converter.
    lines_to_block.setInput(&conv_queue);
    lines_to_block.setOutput(&pcm_block);
    lines_to_block.setIgnoreCRC(ignore_CRC);

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

    // Cycle through all interleave blocks.
    for(uint8_t iblk=0;iblk<PCM1DataBlock::INT_BLK_PER_FIELD;iblk++)
    {
        // Reset data block structure.
        pcm_block.clear();
        // Fill up data block, performing de-interleaving, convert lines to data blocks.
        lines_to_block.processBlock(iblk);
        frasm_f1.blocks_total++;
        // Set emphasis for the block from the frame emphasis flag.
        pcm_block.setEmphasis(emphasis_set);
        frasm_f1.odd_emphasis = frasm_f1.even_emphasis = emphasis_set;
        // Set sample rate for data block.
        setBlockSampleRate(&pcm_block);
        // Check compiled data block for CRC errors.
        if(pcm_block.isBlockValid()==false)
        {
            // Data block is corrupted.
            frasm_f1.blocks_drop++;
            // Samples in data block are corrupted.
            frasm_f1.samples_drop += pcm_block.getErrorsAudio();
        }

        // TODO: implement [blocks_fix_bp] count

#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_BLOCK_DUMP)!=0)
        {
            qInfo()<<QString::fromStdString("[L2B-1] "+pcm_block.dumpInfoString());
            for(uint8_t i=0;i<(PCM1DataBlock::WORD_CNT/PCM1DataBlock::DUMP_SPLIT);i++)
            {
                qInfo()<<QString::fromStdString("[L2B-1] Words: "+pcm_block.dumpWordsString(i));
            }
            for(uint8_t i=0;i<(PCM1DataBlock::WORD_CNT/PCM1DataBlock::DUMP_SPLIT);i++)
            {
                qInfo()<<QString::fromStdString("[L2B-1] Samples: "+pcm_block.dumpSamplesString(i));
            }
        }
#endif
        // Output processed data block.
        outputDataBlock(&pcm_block);
    }
}

//------------------------ Set logging level (using [PCM1Deinterleaver] defines).
void PCM1DataStitcher::setLogLevel(uint16_t new_log)
{
    log_level = new_log;
}

//------------------------ Preset field order.
void PCM1DataStitcher::setFieldOrder(uint8_t in_order)
{
#ifdef DI_EN_DBG_OUT
    if(preset_field_order!=in_order)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_order==FrameAsmDescriptor::ORDER_TFF)
            {
                qInfo()<<"[L2B-1] Field order preset to 'TFF'.";
            }
            else if(in_order==FrameAsmDescriptor::ORDER_BFF)
            {
                qInfo()<<"[L2B-1] Field order preset to 'BFF'.";
            }
            else
            {
                qInfo()<<"[L2B-1] Unknown field order provided, ignored!";
            }
        }
    }
#endif
    if((in_order==FrameAsmDescriptor::ORDER_TFF)||(in_order==FrameAsmDescriptor::ORDER_BFF))
    {
        preset_field_order = in_order;
    }
}

//------------------------ Preset auto line offset.
void PCM1DataStitcher::setAutoLineOffset(bool in_auto)
{
#ifdef DI_EN_DBG_OUT
    if(auto_offset!=in_auto)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_auto==false)
            {
                qInfo()<<"[L2B-1] Auto line offset preset to 'disabled'";
            }
            else
            {
                qInfo()<<"[L2B-1] Auto line offset preset to 'enabled'";
            }
        }
    }
#endif
    auto_offset = in_auto;
}

//------------------------ Preset odd line offset from the top.
void PCM1DataStitcher::setOddLineOffset(int8_t in_ofs)
{
#ifdef DI_EN_DBG_OUT
    if(preset_odd_offset!=in_ofs)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            qInfo()<<"[L2B-1] Off line offset preset to"<<in_ofs;
        }
    }
#endif
    preset_odd_offset = in_ofs;
}

//------------------------ Preset even line offset from the top.
void PCM1DataStitcher::setEvenLineOffset(int8_t in_ofs)
{
#ifdef DI_EN_DBG_OUT
    if(preset_even_offset!=in_ofs)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            qInfo()<<"[L2B-1] Even line offset preset to"<<in_ofs;
        }
    }
#endif
    preset_even_offset = in_ofs;
}

//------------------------ Set fine settings: usage of ECC on CRC-marked words.
void PCM1DataStitcher::setFineUseECC(bool in_set)
{
#ifdef DI_EN_DBG_OUT
    if(ignore_CRC==in_set)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(in_set==false)
            {
                qInfo()<<"[L2B-1] ECC usage set to 'enabled'.";
            }
            else
            {
                qInfo()<<"[L2B-1] ECC usage set to 'disabled'.";
            }
        }
    }
#endif
    ignore_CRC = !in_set;
    emit guiUpdFineUseECC(!ignore_CRC);
}

//------------------------ Set fine settings to defaults.
void PCM1DataStitcher::setDefaultFineSettings()
{
    setFineUseECC(true);
}

//------------------------ Get current fine settings.
void PCM1DataStitcher::requestCurrentFineSettings()
{
    emit guiUpdFineUseECC(!ignore_CRC);
}

//------------------------ Main processing loop.
void PCM1DataStitcher::doFrameReassemble()
{
    uint16_t lines_per_frame;
    quint64 time_spent;
    size_t queue_size;
    PCM1Line cur_line;

#ifdef DI_EN_DBG_OUT
    qInfo()<<"[L2B-1] Launched, PCM-1 thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
#endif
    // Check working pointers.
    if((in_lines==NULL)||(mtx_lines==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[L2B-1] Empty pointer provided, unable to continue!";
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
            // Wait for sufficient data in the input queue and detect frame number.
            if(waitForOneFrame()!=false)
            {
                // One full frame is available.
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[L2B-1] -------------------- Detected full frame of data, switching to processing...";
                }
#endif
                time_per_frame.start();

                // Fast fill frame into internal buffer.
                fillUntilFullFrame();

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
                    qInfo()<<"[L2B-1] Removed"<<lines_per_frame<<"lines from input queue from frame"<<frasm_f1.frame_number;
                }
#endif
                // Set deinterleaver logging parameters.
                uint8_t ext_di_log_lvl = 0;
                if((log_level&LOG_SETTINGS)!=0)
                {
                    ext_di_log_lvl |= PCM1Deinterleaver::LOG_SETTINGS;
                }
                if((log_level&LOG_DEINTERLEAVE)!=0)
                {
                    ext_di_log_lvl |= PCM1Deinterleaver::LOG_PROCESS;
                }
                if((log_level&LOG_ERROR_CORR)!=0)
                {
                    ext_di_log_lvl |= PCM1Deinterleaver::LOG_ERROR_CORR;
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

                    // Calculate frame padding.
                    findFramePadding();

                    // Inform processing chain about new file.
                    if(file_start!=false)
                    {
                        outputFileStart();
                    }

                    // Fill up PCM line queue from internal field buffer.
                    fillFirstFieldForOutput();
                    // Perform deinterleaving on first field.
                    performDeinterleave();
                    // Clear conversion queue.
                    conv_queue.clear();

                    // Fill up PCM line queue from internal field buffer.
                    fillSecondFieldForOutput();
                    // Perform deinterleaving on second field.
                    performDeinterleave();
                    // Clear conversion queue.
                    conv_queue.clear();

                    // Convert from sub-lines to lines before reporting.
                    frasm_f1.odd_data_lines = frasm_f1.odd_data_lines/PCM1Line::SUBLINES_PER_LINE;
                    frasm_f1.even_data_lines = frasm_f1.even_data_lines/PCM1Line::SUBLINES_PER_LINE;
                    frasm_f1.odd_valid_lines = frasm_f1.odd_valid_lines/PCM1Line::SUBLINES_PER_LINE;
                    frasm_f1.even_valid_lines = frasm_f1.even_valid_lines/PCM1Line::SUBLINES_PER_LINE;
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
    qInfo()<<"[L2B-1] Loop stop.";
    emit finished();
}

//------------------------ Set the flag to break execution loop and exit.
void PCM1DataStitcher::stop()
{
#ifdef DI_EN_DBG_OUT
    qInfo()<<"[L2B-1] Received termination request";
#endif
    finish_work = true;
}
