#include "pcm16x0deinterleaver.h"

PCM16X0Deinterleaver::PCM16X0Deinterleaver()
{
    log_level = 0;
    force_parity_check = false;
    en_p_code = true;
    ignore_crc = false;
    setSIFormat();
    proc_state = STG_CRC_CHECK;
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void PCM16X0Deinterleaver::setLogLevel(uint8_t in_level)
{
    log_level = in_level;
}

//------------------------ Set input deque pointer.
void PCM16X0Deinterleaver::setInput(std::deque<PCM16X0SubLine> *in_line_buffer)
{
    input_deque = in_line_buffer;
    if(input_deque!=NULL)
    {
        // Remove pointer to vector.
        input_vector = NULL;
    }
}

//------------------------ Set input vector pointer.
void PCM16X0Deinterleaver::setInput(std::vector<PCM16X0SubLine> *in_line_buffer)
{
    input_vector = in_line_buffer;
    if(input_vector!=NULL)
    {
        // Remove pointer to deque.
        input_deque = NULL;
    }
}

//------------------------ Set output data block.
void PCM16X0Deinterleaver::setOutput(PCM16X0DataBlock *out_data)
{
    out_data_block = out_data;
}

//------------------------ Enable/disable force parity check (regardless of CRC result).
void PCM16X0Deinterleaver::setForceParity(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(force_parity_check!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-16x0] Parity check override set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-16x0] Parity check override set to 'enabled'.";
            }
        }
    }
#endif
    force_parity_check = flag;
}

//------------------------ Enable/disable P-code correction of data.
void PCM16X0Deinterleaver::setPCorrection(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(en_p_code!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-16x0] P-code error correction set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-16x0] P-code error correction set to 'enabled'.";
            }
        }
    }
#endif
    en_p_code = flag;
}


//------------------------ Enable/disable ignoring CRC in video lines.
void PCM16X0Deinterleaver::setIgnoreCRC(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(ignore_crc!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-16x0] CRC discarding set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-16x0] CRC discarding set to 'enabled'.";
            }
        }
    }
#endif
    ignore_crc = flag;
}

//------------------------ Perform deinterleaving in SI format.
void PCM16X0Deinterleaver::setSIFormat()
{
    ei_format = false;
}

//------------------------ Perform deinterleaving in EI format.
void PCM16X0Deinterleaver::setEIFormat()
{
    ei_format = true;
}

//------------------------ Take buffer of binarized PCM lines, perform deinterleaving,
//------------------------ check for errors and correct those if possible.
uint8_t PCM16X0Deinterleaver::processBlock(uint16_t line_sh, bool even_order)
{
    bool use_vector, suppress_log;
    uint8_t stage_count, bad_ptr, err_total, err_audio, fix_result;
    uint16_t min_data_size, line1_ofs, line2_ofs, line3_ofs;

    use_vector = false;

#ifdef DI_EN_DBG_OUT
    QString log_line;
#endif

    // Check pointers.
    if(out_data_block==NULL)
    {
#ifdef DI_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[DI-16x0] Null output pointer for PCM-16x0 data block provided, exiting...";
#endif
        return DI_RET_NULL_BLOCK;
    }
    if(ei_format==false)
    {
        // Set minimum lines for SI format.
        min_data_size = PCM16X0DataBlock::MIN_DEINT_DATA_SI+line_sh;
    }
    else
    {
        // Set minimum lines for EI format.
        min_data_size = PCM16X0DataBlock::MIN_DEINT_DATA_EI+line_sh;
    }
    if(input_deque==NULL)
    {
        if(input_vector==NULL)
        {
#ifdef DI_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[DI-16x0] Null output pointer for PCM-16x0 line buffer provided, exiting...";
#endif
            return DI_RET_NULL_LINES;
        }
        else
        {
            use_vector = true;
            // Check if there is enough data to process.
            if((input_vector->size())<=(size_t)min_data_size)
            {
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[DI-16x0] Not enough PCM-16x0 lines in buffer:"<<input_vector->size()<<", required:"<<(min_data_size+1);
                }
#endif
                return DI_RET_NO_DATA;
            }
        }
    }
    else
    {
        // Check if there is enough data to process.
        if((input_deque->size())<=(size_t)min_data_size)
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[DI-16x0] Not enough PCM-16x0 lines in buffer:"<<input_deque->size()<<", required:"<<(min_data_size+1);
            }
#endif
            return DI_RET_NO_DATA;
        }
    }

    // Measure processing time for debuging.
#ifdef QT_VERSION
    QElapsedTimer time_per_block;
    time_per_block.start();
#endif

    suppress_log = !(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_ERROR_CORR)!=0));

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[DI-16x0] -------------------- Filling data in 16-bit mode";
    }
#endif
    // Determine block order.
    if(even_order==false)
    {
        out_data_block->setOrderOdd();
    }
    else
    {
        out_data_block->setOrderEven();
    }

    // Determine line offsets according to format.
    if(ei_format==false)
    {
        // SI format.
        line1_ofs = PCM16X0DataBlock::LINE_1_SI_OFS+line_sh;
        line2_ofs = PCM16X0DataBlock::LINE_2_SI_OFS+line_sh;
        line3_ofs = PCM16X0DataBlock::LINE_3_SI_OFS+line_sh;
    }
    else
    {
        // EI format.
        line1_ofs = PCM16X0DataBlock::LINE_1_EI_OFS+line_sh;
        line2_ofs = PCM16X0DataBlock::LINE_2_EI_OFS+line_sh;
        line3_ofs = PCM16X0DataBlock::LINE_3_EI_OFS+line_sh;
    }

    // Fill data block with words from interleaved PCM-lines.
    if(use_vector==false)
    {
        setWordData(&((*input_deque)[line1_ofs]),
                    &((*input_deque)[line2_ofs]),
                    &((*input_deque)[line3_ofs]),
                    out_data_block);
    }
    else
    {
        setWordData(&((*input_vector)[line1_ofs]),
                    &((*input_vector)[line2_ofs]),
                    &((*input_vector)[line3_ofs]),
                    out_data_block);
    }
    // Reset audio state.
    out_data_block->markAsOriginalData();

    // Cycle through sub-blocks.
    for(uint8_t blk=PCM16X0DataBlock::SUBBLK_1;blk<=PCM16X0DataBlock::SUBBLK_3;blk++)
    {
        //qInfo()<<"BLK:"<<blk<<"time:"<<time_per_block.nsecsElapsed()/1000;

        // Set starting condition.
        proc_state = STG_CRC_CHECK;
        // Cycle through stages.
        stage_count = 0;
        // Get error counts for the sub-block.
        err_total = out_data_block->getErrorsTotal(blk);
        err_audio = out_data_block->getErrorsAudio(blk);

        //suppress_log = !(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_ERROR_CORR)!=0));

#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[DI-16x0] Processing sub-block [%01u]...", blk);
            qInfo()<<log_line;
        }
#endif
        do
        {
            // Count loops.
            stage_count++;

            //qInfo()<<"Run:"<<stage_count<<"stage:"<<proc_state<<"time:"<<time_per_block.nsecsElapsed()/1000;

            // Select processing mode.
            if(proc_state==STG_CRC_CHECK)
            {
                // Check total number of errors.
                if(err_total>1)
                {
                    // Impossible to correct or check with that number of errors.
                    proc_state = STG_BAD_BLOCK;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[DI-16x0] More than one (%01u) word is CRC-marked, that can not be corrected!", err_total);
                        qInfo()<<log_line;
                    }
#endif
                    /*if(err_total==2)
                    {
                        uint16_t synd_p;
                        synd_p = calcSyndromeP(out_data_block, blk);
                        if(synd_p==0)
                        {
                            if((out_data_block->getWord(blk, PCM16X0DataBlock::WORD_L)!=0)&&
                               (out_data_block->getWord(blk, PCM16X0DataBlock::WORD_R)!=0)&&
                               (out_data_block->getWord(blk, PCM16X0DataBlock::WORD_P)!=0))
                            {
                                log_line.sprintf("[TEST] Block [%04u:%03u:%01u-%01u] may be ok (%04x[%u] + %04x[%u] = %04x[%u])",
                                                 out_data_block->frame_number,
                                                 out_data_block->start_line,
                                                 out_data_block->start_part,
                                                 blk,
                                                 out_data_block->getWord(blk, PCM16X0DataBlock::WORD_L),
                                                 out_data_block->isWordValid(blk, PCM16X0DataBlock::WORD_L),
                                                 out_data_block->getWord(blk, PCM16X0DataBlock::WORD_R),
                                                 out_data_block->isWordValid(blk, PCM16X0DataBlock::WORD_R),
                                                 out_data_block->getWord(blk, PCM16X0DataBlock::WORD_P),
                                                 out_data_block->isWordValid(blk, PCM16X0DataBlock::WORD_P));
                                qInfo()<<log_line;
                            }
                        }
                    }*/
                }
                else
                {
                    // Total errors in the sub-block: 1 or 0.
                    // Check if P-code correction is allowed.
                    if(en_p_code!=false)
                    {
                        // P-code correction is allowed.
                        // Check if parity check is forced.
                        if(force_parity_check!=false)
                        {
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                if(err_total==0)
                                {
                                    qInfo()<<"[DI-16x0] No audio CRC errors, but parity check is forced, checking with P-code...";
                                }
                                else
                                {
                                    qInfo()<<"[DI-16x0] One audio sample is CRC-marked, trying to correct with P-code...";
                                }
                            }
#endif
                            // Go straight to P-correction.
                            proc_state = STG_P_CORR;
                        }
                        else
                        {
                            // Check for any errors in the sub-block.
                            if(err_total>0)
                            {
                                // There is ONE error.
                                // Check if it is in the audio or in the P-code.
                                if(err_audio>0)
                                {
                                    // Error is in audio sample, need to do P-correction.
                                    proc_state = STG_P_CORR;
#ifdef DI_EN_DBG_OUT
                                    if(suppress_log==false)
                                    {
                                        qInfo()<<"[DI-16x0] One audio sample is CRC-marked, trying to correct with P-code...";
                                    }
#endif
                                }
                                else
                                {
                                    // No errors in audio, everything is fine.
                                    // (don't care about error in P word if any)
                                    proc_state = STG_DATA_OK;
                                }
                            }
                            else
                            {
                                // No errors, everything is fine.
                                proc_state = STG_DATA_OK;
                            }
                        }
                    }
                    else
                    {
                        // P-code correction is disabled.
                        // Check for any errors in the sub-block.
                        if(err_audio>0)
                        {
                            // There is an error, but P-code is not available, bad block.
                            proc_state = STG_BAD_BLOCK;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-16x0] One audio sample is CRC-marked, but P-code is disabled, unable to fix an error!";
                            }
#endif
                        }
                        else if(force_parity_check!=false)
                        {
                            // Force parity check enabled, but P-code checks are disabled.
                            // No errors in audio, everything is fine.
                            proc_state = STG_NO_CHECK;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-16x0] No audio CRC errors, parity check is forced, but P-code is disabled.";
                            }
#endif
                        }
                        else
                        {
                            // No errors in audio, everything is fine.
                            proc_state = STG_DATA_OK;
                        }
                    }
                }
            }
            else if(proc_state==STG_P_CORR)
            {
                // Preset "no index" value for error locator.
                bad_ptr = NO_ERR_INDEX;

                // Line priority: LINE1 -> LINE3 -> LINE2 (P).
                if(out_data_block->isWordCRCOk(blk, PCM16X0DataBlock::WORD_L)==false)
                {
                    bad_ptr = PCM16X0DataBlock::WORD_L;
                }
                else if(out_data_block->isWordCRCOk(blk, PCM16X0DataBlock::WORD_R)==false)
                {
                    bad_ptr = PCM16X0DataBlock::WORD_R;
                }
                else if(out_data_block->isWordCRCOk(blk, PCM16X0DataBlock::WORD_P)==false)
                {
                    bad_ptr = PCM16X0DataBlock::WORD_P;
                }
                // Check if error pointer set to parity word.
                if(bad_ptr!=PCM16X0DataBlock::WORD_P)
                {
                    // P-code word is available.
                    // Try to check parity with with P-code.
                    fix_result = fixByP(out_data_block, blk, bad_ptr);
                    if(fix_result==FIX_BROKEN)
                    {
                        // Audio data is broken.
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-16x0] Found BROKEN data";
                        }
#endif
                        // Check if sub-block has picked bits in leftmost sample.
                        if(out_data_block->getPickedAudioSamples(blk)>1)
                        {
                            // Two or more audio samples are marked as bit-picked, can not determine which can be false-picked, mark the whole block as BAD.
                            out_data_block->markAsBad(blk, PCM16X0DataBlock::LINE_1);
                            out_data_block->markAsBad(blk, PCM16X0DataBlock::LINE_3);
                            proc_state = STG_BAD_BLOCK;
#ifdef DI_EN_DBG_OUT
                            //suppress_log = false;
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-16x0] BROKEN, but both audio samples have picked bits, marking block as BAD";
                            }
#endif
                        }
                        else if(out_data_block->getPickedAudioSamples(blk)==1)
                        {
                            // One audio sample is marked as bit-picked.
                            // Check if parity is bit-picked.
                            if(out_data_block->hasPickedParity(blk)!=false)
                            {
                                // Parity is marked as bit-picked.
                                out_data_block->markAsBad(blk, PCM16X0DataBlock::LINE_1);
                                out_data_block->markAsBad(blk, PCM16X0DataBlock::LINE_3);
                                proc_state = STG_BAD_BLOCK;
#ifdef DI_EN_DBG_OUT
                                //suppress_log = false;
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[DI-16x0] BROKEN, one audio sample has picked bits, but parity is also bit-picked, marking block as BAD";
                                }
#endif
                            }
                            else
                            {
                                // Parity is not bit-picked.
                                // Check which one of the audio-samples is bit-picked (only can happen with [SUBBLK_1]).
                                if(out_data_block->hasPickedLeft(PCM16X0DataBlock::LINE_1)!=false)
                                {
                                    // [LINE_1] is picked.
#ifdef DI_EN_DBG_OUT
                                    //suppress_log = false;
                                    if(suppress_log==false)
                                    {
                                        qInfo()<<"[DI-16x0] BROKEN, audio sample at LINE_1 has picked bits, trying to recover...";
                                    }
#endif
                                    // Mark "valid" bit-picked sample as bad.
                                    out_data_block->markAsBad(blk, PCM16X0DataBlock::LINE_1);
                                    // Try to correct bad sample on the next run.
                                    proc_state = STG_P_CORR;
                                }
                                else if(out_data_block->hasPickedLeft(PCM16X0DataBlock::LINE_3)!=false)
                                {
                                    // [LINE_3] is picked.
#ifdef DI_EN_DBG_OUT
                                    //suppress_log = false;
                                    if(suppress_log==false)
                                    {
                                        qInfo()<<"[DI-16x0] BROKEN, audio sample at LINE_3 has picked bits, trying to recover...";
                                    }
#endif
                                    // Mark "valid" bit-picked sample as bad.
                                    out_data_block->markAsBad(blk, PCM16X0DataBlock::LINE_3);
                                    // Try to correct bad sample on the next run.
                                    proc_state = STG_P_CORR;
                                }
                                else
                                {
                                    // Logic error, should never end up here.
#ifdef DI_EN_DBG_OUT
                                    qWarning()<<DBG_ANCHOR<<"[DI-16x0] BROKEN, failed to recover it";
#endif
                                    proc_state = STG_BAD_BLOCK;
                                    out_data_block->markAsBroken();
                                }
                            }
                        }
                        else
                        {
                            // No bit-picked audio samples.
                            // Check if parity is bit-picked.
                            if(out_data_block->hasPickedParity(blk)!=false)
                            {
                                // Parity is marked as bit-picked.
#ifdef DI_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[DI-16x0] BROKEN, audio samples are not picked, parity has picked bits, unable to verify parity";
                                }
#endif
                                // Deem parity word as invalid.
                                out_data_block->markAsBad(blk, PCM16X0DataBlock::LINE_2);
                                // Unable to force-check parity.
                                proc_state = STG_NO_CHECK;
                            }
                            else
                            {
                                // Parity is not bit-picked.
#ifdef DI_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[DI-16x0] Actually BROKEN data detected!";
                                }
#endif
                                // No words are bit-picked and still BROKEN.
                                proc_state = STG_BAD_BLOCK;
                                out_data_block->markAsBroken(blk);
                            }
                        }
                    }
                    else if(fix_result==FIX_NOT_NEED)
                    {
                        // Audio data is ok.
                        proc_state = STG_DATA_OK;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-16x0] No parity error detected";
                        }
#endif
                    }
                    else
                    {
                        // Audio data is fixed.
                        proc_state = STG_DATA_OK;
                        out_data_block->markAsFixedByP(blk);
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-16x0] Successfully fixed the error with P-code";
                        }
#endif
                        /*if(suppress_log==false)
                        {
                            qInfo()<<QString::fromStdString(out_data_block->dumpContentString());
                        }*/
                    }
                }
                else
                {
                    // P-code word is NOT available.
                    // Can not check the data.
                    proc_state = STG_NO_CHECK;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-16x0] P-code word is bad, unable to verify parity";
                    }
#endif
                }
            }
            else if(proc_state==STG_BAD_BLOCK)
            {
                // Errors in audio samples can not be fixed.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-16x0] ["+QString::number(out_data_block->frame_number)+
                             "|"+QString::number(out_data_block->start_line)+
                             "-"+QString::number(out_data_block->start_part)+
                             ":"+QString::number(out_data_block->stop_line)+
                             "-"+QString::number(out_data_block->stop_part)+"] "+
                             "Audio data can not be fixed, dropout detected!";
                }
#endif
                // Exit stage cycle.
                break;
            }
            else if(proc_state==STG_NO_CHECK)
            {
                // No CRC errors in audio words, but unable to force-check integrity due to unavailable P-code.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {


                    qInfo()<<"[DI-16x0] ["+QString::number(out_data_block->frame_number)+
                             "|"+QString::number(out_data_block->start_line)+
                             "-"+QString::number(out_data_block->start_part)+
                             ":"+QString::number(out_data_block->stop_line)+
                             "-"+QString::number(out_data_block->stop_part)+"] "+
                             "Audio samples are not damaged but can not be verified!";
                }
#endif
                // Exit stage cycle.
                break;
            }
            else if(proc_state==STG_DATA_OK)
            {
                // Audio samples are intact.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-16x0] ["+QString::number(out_data_block->frame_number)+
                             "|"+QString::number(out_data_block->start_line)+
                             "-"+QString::number(out_data_block->start_part)+
                             ":"+QString::number(out_data_block->stop_line)+
                             "-"+QString::number(out_data_block->stop_part)+"] "+
                             "All audio samples are good";
                }
#endif
                // Exit stage cycle.
                break;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[DI-16x0] Impossible state detected, breaking...";
#endif
                // Exit stage cycle.
                break;
            }

            // Check for looping.
            if(stage_count>STG_CONVERT_MAX)
            {
#ifdef DI_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[DI-16x0] Inf. loop detected, breaking...";
#endif
                // Exit stage cycle.
                break;
            }
        }
        while(1);   // Stages cycle.
    }

    // Store time that processing took.
    quint64 block_time;
    block_time = 0;
#ifdef QT_VERSION
    block_time = time_per_block.nsecsElapsed();
#endif
    out_data_block->process_time = (uint32_t)(block_time/1000);

    return DI_RET_OK;
}

//------------------------ Set data and CRC state for all words in the data block.
void PCM16X0Deinterleaver::setWordData(PCM16X0SubLine *line1, PCM16X0SubLine *line2, PCM16X0SubLine *line3, PCM16X0DataBlock *out_data_block)
{
    bool subline1_crc_ok, subline2_crc_ok, subline3_crc_ok;

    // Preset that CRCs are ok.
    subline1_crc_ok = subline2_crc_ok = subline3_crc_ok = true;
    // Check if CRC should be ignored.
    if(ignore_crc==false)
    {
        // Word CRCs must be checked.
        // Take CRC states from the sub-lines.
        subline1_crc_ok = line1->isCRCValid();
        subline2_crc_ok = line2->isCRCValid();
        subline3_crc_ok = line3->isCRCValid();
    }
    else
    {
        // CRC skip is allowed.
        // Check word statuses by subline state (filler or not).
        subline1_crc_ok = line1->coords.areValid()&&line1->hasBWSet();
        subline2_crc_ok = line2->coords.areValid()&&line2->hasBWSet();
        subline3_crc_ok = line3->coords.areValid()&&line3->hasBWSet();
    }

    // Line 1.
    // Copy R1/L1.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_1, PCM16X0DataBlock::LINE_1, line1->words[PCM16X0SubLine::WORD_R1P1L1],
                            subline1_crc_ok, line1->hasPickedLeft(), line1->hasPickedRight());
    // Copy L2/R2.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_2, PCM16X0DataBlock::LINE_1, line1->words[PCM16X0SubLine::WORD_L2P2R2],
                            subline1_crc_ok, false, line1->hasPickedRight());
    // Copy R3/L3.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_3, PCM16X0DataBlock::LINE_1, line1->words[PCM16X0SubLine::WORD_R3P3L3],
                            subline1_crc_ok, false, line1->hasPickedRight());
    // Line 2.
    // Copy P1.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_1, PCM16X0DataBlock::LINE_2, line2->words[PCM16X0SubLine::WORD_R1P1L1],
                            subline2_crc_ok, line2->hasPickedLeft(), line2->hasPickedRight());
    // Copy P2.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_2, PCM16X0DataBlock::LINE_2, line2->words[PCM16X0SubLine::WORD_L2P2R2],
                            subline2_crc_ok, false, line2->hasPickedRight());
    // Copy P3.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_3, PCM16X0DataBlock::LINE_2, line2->words[PCM16X0SubLine::WORD_R3P3L3],
                            subline2_crc_ok, false, line2->hasPickedRight());
    // Line 3.
    // Copy L1/R1.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_1, PCM16X0DataBlock::LINE_3, line3->words[PCM16X0SubLine::WORD_R1P1L1],
                            subline3_crc_ok, line3->hasPickedLeft(), line3->hasPickedRight());
    // Copy R2/L2.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_2, PCM16X0DataBlock::LINE_3, line3->words[PCM16X0SubLine::WORD_L2P2R2],
                            subline3_crc_ok, false, line3->hasPickedRight());
    // Copy L3/R3.
    out_data_block->setWord(PCM16X0DataBlock::SUBBLK_3, PCM16X0DataBlock::LINE_3, line3->words[PCM16X0SubLine::WORD_R3P3L3],
                            subline3_crc_ok, false, line3->hasPickedRight());
    // Save frames/lines info.
    out_data_block->frame_number = line1->frame_number;
    out_data_block->start_line = line1->line_number;
    out_data_block->start_part = line1->line_part;
    out_data_block->stop_line = line3->line_number;
    out_data_block->stop_part = line3->line_part;
    out_data_block->queue_order = line3->queue_order;

#ifdef DI_EN_DBG_OUT
    if(((log_level&LOG_PROCESS)!=0)&&((log_level&LOG_ERROR_CORR)!=0))
    {
        QString log_line;
        log_line.sprintf("[DI-16x0] Added data from lines %03u[P%01u]-%03u[P%01u]-%03u[P%01u]",
                        line1->line_number,
                        line1->line_part,
                        line2->line_number,
                        line2->line_part,
                        line3->line_number,
                        line3->line_part);
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Calculate syndrome for P-code (parity).
uint16_t PCM16X0Deinterleaver::calcSyndromeP(PCM16X0DataBlock *data_block, uint8_t blk)
{
    uint16_t syndrome/* = 0xDEAD*/;
    syndrome = data_block->getWord(blk, PCM16X0DataBlock::WORD_L)
               ^data_block->getWord(blk, PCM16X0DataBlock::WORD_R)
               ^data_block->getWord(blk, PCM16X0DataBlock::WORD_P);
    return syndrome;
}

//------------------------ Calculate syndrome for P-code and fix an error.
uint8_t PCM16X0Deinterleaver::fixByP(PCM16X0DataBlock *data_block, uint8_t blk, uint8_t bad_ptr)
{
    bool suppress_log;
    uint16_t check, fix_word;

#ifdef DI_EN_DBG_OUT
    QString log_line;
#endif

    suppress_log = !((log_level&LOG_ERROR_CORR)!=0);

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line = "[DI-16x0] Starting P-correction, bad index: ";
        if(bad_ptr==NO_ERR_INDEX)
        {
            log_line += "not set";
        }
        else
        {
            log_line += QString::number(bad_ptr);
        }
        qInfo()<<log_line;
    }
#endif

    // Calculate syndrome for P-code.
    check = calcSyndromeP(data_block, blk);
    // Check if audio data needs to be fixed.
    if(check==0)
    {
        // Audio data is ok.
        if(bad_ptr!=NO_ERR_INDEX)
        {
            // Set sample as "ok" if it was marked with CRC.
            data_block->fixWord(blk, bad_ptr, data_block->getWord(blk, bad_ptr));
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[DI-16x0] Zero parity syndrome, the samples are ok. No error at"<<bad_ptr;
            }
#endif
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            if(suppress_log==false)
            {
                qInfo()<<"[DI-16x0] Zero parity syndrome, the samples are ok.";
            }
        }
#endif
        return FIX_NOT_NEED;
    }
    else if(bad_ptr==NO_ERR_INDEX)
    {
        // Error was detected, but there is no bad word pointer.
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[DI-16x0] Parity check failed (0x%04x) while no CRC markers were set, broken block!", check);
            qInfo()<<log_line;
        }
#endif
        // Data is broken.
        return FIX_BROKEN;
    }
    else
    {
        // Fix an error in the sample (flip errored bits in the word).
        fix_word = check^data_block->getWord(blk, bad_ptr);
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[DI-16x0] Syndrome for P-code: 0x%04x, bad sample at [%01u], fix: 0x%04x -> 0x%04x",
                             check, bad_ptr, data_block->getWord(blk, bad_ptr), fix_word);
            qInfo()<<log_line;
        }
#endif
        // Replace damaged word.
        data_block->fixWord(blk, bad_ptr, fix_word);
        return FIX_DONE;
    }
}
