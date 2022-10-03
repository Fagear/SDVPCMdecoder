#include "stc007deinterleaver.h"

// Matrices for fast Q-correction.
static const uint16_t I_MATRIX[STC007Line::BITS_PER_WORD] =        // I matrix (H-mirrored).
{
    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000
};
static const uint16_t TP1_MATRIX[STC007Line::BITS_PER_WORD] =      // T^1 matrix (H-mirrored).
{
    0x2000, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x2080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000
};
static const uint16_t TP2_MATRIX[STC007Line::BITS_PER_WORD] =      // T^2 matrix (H-mirrored).
{
    0x1000, 0x2000, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x1040, 0x2080, 0x0100, 0x0200, 0x0400, 0x0800
};
static const uint16_t TP3_MATRIX[STC007Line::BITS_PER_WORD] =      // T^3 matrix (H-mirrored).
{
    0x0800, 0x1000, 0x2000, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0820, 0x1040, 0x2080, 0x0100, 0x0200, 0x0400
};
static const uint16_t TP4_MATRIX[STC007Line::BITS_PER_WORD] =      // T^4 matrix (H-mirrored).
{
    0x0400, 0x0800, 0x1000, 0x2000, 0x0001, 0x0002, 0x0004, 0x0008, 0x0410, 0x0820, 0x1040, 0x2080, 0x0100, 0x0200
};
static const uint16_t TP5_MATRIX[STC007Line::BITS_PER_WORD] =      // T^5 matrix (H-mirrored).
{
    0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x0001, 0x0002, 0x0004, 0x0208, 0x0410, 0x0820, 0x1040, 0x2080, 0x0100
};
static const uint16_t TP6_MATRIX[STC007Line::BITS_PER_WORD] =      // T^6 matrix (H-mirrored).
{
    0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x0001, 0x0002, 0x0104, 0x0208, 0x0410, 0x0820, 0x1040, 0x2080
};
static const uint16_t TN1_MATRIX[STC007Line::BITS_PER_WORD] =      // T^(-1) matrix (H-mirrored).
{
    0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0101, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x0001
};
static const uint16_t TN2_MATRIX[STC007Line::BITS_PER_WORD] =      // T^(-2) matrix (H-mirrored).
{
    0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0101, 0x0202, 0x0400, 0x0800, 0x1000, 0x2000, 0x0001, 0x0002
};
static const uint16_t TN3_MATRIX[STC007Line::BITS_PER_WORD] =      // T^(-3) matrix (H-mirrored).
{
    0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0101, 0x0202, 0x0404, 0x0800, 0x1000, 0x2000, 0x0001, 0x0002, 0x0004
};
static const uint16_t TN4_MATRIX[STC007Line::BITS_PER_WORD] =      // T^(-4) matrix (H-mirrored).
{
    0x0010, 0x0020, 0x0040, 0x0080, 0x0101, 0x0202, 0x0404, 0x0808, 0x1000, 0x2000, 0x0001, 0x0002, 0x0004, 0x0008
};
static const uint16_t TN5_MATRIX[STC007Line::BITS_PER_WORD] =      // T^(-5) matrix (H-mirrored).
{
    0x0020, 0x0040, 0x0080, 0x0101, 0x0202, 0x0404, 0x0808, 0x1010, 0x2000, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010
};
static const uint16_t TN6_MATRIX[STC007Line::BITS_PER_WORD] =      // T^(-6) matrix (H-mirrored).
{
    0x0040, 0x0080, 0x0101, 0x0202, 0x0404, 0x0808, 0x1010, 0x2020, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020
};
static const uint16_t TP1IN1_MATRIX[STC007Line::BITS_PER_WORD] =   // (T^1+I)^(-1) matrix (H-mirrored).
{
    0x3FFE, 0x3FFC, 0x3FF8, 0x3FF0, 0x3FE0, 0x3FC0, 0x3F80, 0x3F00, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF
};
static const uint16_t TP2IN1_MATRIX[STC007Line::BITS_PER_WORD] =   // (T^2+I)^(-1) matrix (H-mirrored).
{
    0x1554, 0x2AA8, 0x1550, 0x2AA0, 0x1540, 0x2A80, 0x1500, 0x2A00, 0x0155, 0x02AA, 0x0555, 0x0AAA, 0x1555, 0x2AAA
};
static const uint16_t TP3IN1_MATRIX[STC007Line::BITS_PER_WORD] =   // (T^3+I)^(-1) matrix (H-mirrored).
{
    0x1248, 0x2490, 0x0920, 0x1240, 0x2480, 0x0900, 0x1200, 0x2400, 0x1A49, 0x3492, 0x2924, 0x1249, 0x2492, 0x0924
};
static const uint16_t TP4IN1_MATRIX[STC007Line::BITS_PER_WORD] =   // (T^4+I)^(-1) matrix (H-mirrored).
{
    0x0445, 0x088A, 0x1115, 0x222A, 0x0455, 0x08AA, 0x1155, 0x22AA, 0x0111, 0x0222, 0x0444, 0x0888, 0x1111, 0x2222
};
static const uint16_t TP5IN1_MATRIX[STC007Line::BITS_PER_WORD] =   // (T^5+I)^(-1) matrix (H-mirrored).
{
    0x1AD7, 0x35AF, 0x2B5E, 0x16BD, 0x2D7B, 0x1AF7, 0x35EF, 0x2BDE, 0x0D6B, 0x1AD6, 0x35AD, 0x2B5A, 0x16B5, 0x2D6B
};

STC007Deinterleaver::STC007Deinterleaver()
{
    this->clear();
}

//------------------------ Reset all fields to default.
void STC007Deinterleaver::clear()
{
    input_vector = NULL;
    input_deque = NULL;
    out_data_block = NULL;
    log_level = 0;
    data_res_mode = RES_MODE_14BIT_AUTO;
    ignore_crc = false;
    force_parity_check = false;
    en_p_code = true;
    en_q_code = true;
    en_cwd = false;
    proc_state = STG_ERROR_CHECK;
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void STC007Deinterleaver::setLogLevel(uint8_t in_level)
{
    log_level = in_level;
}

//------------------------ Set input deque pointer.
void STC007Deinterleaver::setInput(std::deque<STC007Line> *in_line_buffer)
{
    input_deque = in_line_buffer;
    if(input_deque!=NULL)
    {
        // Remove pointer to vector.
        input_vector = NULL;
    }
}

//------------------------ Set input vector pointer.
void STC007Deinterleaver::setInput(std::vector<STC007Line> *in_line_buffer)
{
    input_vector = in_line_buffer;
    if(input_vector!=NULL)
    {
        // Remove pointer to deque.
        input_deque = NULL;
    }
}

//------------------------ Set output data block.
void STC007Deinterleaver::setOutput(STC007DataBlock *out_data)
{
    out_data_block = out_data;
}

//------------------------ Set resolution detection algorythm (14-bit EIAJ/16-bit PCM-F1).
void STC007Deinterleaver::setResMode(uint8_t in_resolution)
{
    if(in_resolution<RES_MODE_MAX)
    {
#ifdef DI_EN_DBG_OUT
        if(data_res_mode!=in_resolution)
        {
            if((log_level&LOG_SETTINGS)!=0)
            {
                if(in_resolution==RES_MODE_14BIT)
                {
                    qInfo()<<"[DI-007] Resolution mode set to '14-bit strict'.";
                }
                else if(in_resolution==RES_MODE_14BIT_AUTO)
                {
                    qInfo()<<"[DI-007] Resolution mode set to '14-bit auto'.";
                }
                else if(in_resolution==RES_MODE_16BIT_AUTO)
                {
                    qInfo()<<"[DI-007] Resolution mode set to '16-bit auto'.";
                }
                else
                {
                    qInfo()<<"[DI-007] Resolution mode set to '16-bit strict'.";
                }
            }
        }
#endif
        data_res_mode = in_resolution;
    }
}

//------------------------ Enable/disable ignoring CRC in video lines.
void STC007Deinterleaver::setIgnoreCRC(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(ignore_crc!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-007] CRC discarding set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-007] CRC discarding set to 'enabled'.";
            }
        }
    }
#endif
    ignore_crc = flag;
}

//------------------------ Enable/disable force parity check (regardless of CRC result).
void STC007Deinterleaver::setForceParity(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(force_parity_check!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-007] Parity check override set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-007] Parity check override set to 'enabled'.";
            }
        }
    }
#endif
    force_parity_check = flag;
}

//------------------------ Enable/disable P-code correction of data.
void STC007Deinterleaver::setPCorrection(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(en_p_code!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-007] P-code error correction set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-007] P-code error correction set to 'enabled'.";
            }
        }
    }
#endif
    en_p_code = flag;
    if(en_p_code==false)
    {
        setQCorrection(false);
        setCWDCorrection(false);
    }
}

//------------------------ Enable/disable Q-code correction of data.
void STC007Deinterleaver::setQCorrection(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(en_q_code!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-007] Q-code error correction set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-007] Q-code error correction set to 'enabled'.";
            }
        }
    }
#endif
    en_q_code = flag;
    if(en_q_code!=false)
    {
        setPCorrection(true);
    }
}

//------------------------ Enable/disable CWD correction of data.
void STC007Deinterleaver::setCWDCorrection(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(en_cwd!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-007] CWD error correction set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-007] CWD error correction set to 'enabled'.";
            }
        }
    }
#endif
    en_cwd = flag;
}

//------------------------ Take buffer of binarized PCM lines, perform deinterleaving,
//------------------------ check for errors and correct those if possible.
uint8_t STC007Deinterleaver::processBlock(uint16_t line_shift)
{
    bool use_vector, suppress_log;
    uint8_t run_audio_res, stage_count, fill_passes, all_crc_errs, aud_crc_errs, first_bad, second_bad, fix_result;

    use_vector = false;

    // Check pointers.
    if(out_data_block==NULL)
    {
#ifdef DI_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[DI-007] Null input pointer for STC-007 data block provided in [STC007Deinterleaver::processBlock()], exiting...";
#endif
        return DI_RET_NULL_BLOCK;
    }
    if(input_deque==NULL)
    {
        if(input_vector==NULL)
        {
#ifdef DI_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[DI-007] Null output pointer for STC-007 line buffer provided in [STC007Deinterleaver::processBlock()], exiting...";
#endif
            return DI_RET_NULL_LINES;
        }
        else
        {
            use_vector = true;
            // Check if there is enough data to process.
            if((input_vector->size())<=(size_t)(STC007DataBlock::MIN_DEINT_DATA+line_shift))
            {
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[DI-007] Not enough STC-007 lines in buffer:"<<input_vector->size()<<", required:"<<(STC007DataBlock::MIN_DEINT_DATA+line_shift+1);
                }
#endif
                return DI_RET_NO_DATA;
            }
        }
    }
    else
    {
        // Check if there is enough data to process.
        if((input_deque->size())<=(size_t)(STC007DataBlock::MIN_DEINT_DATA+line_shift))
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[DI-007] Not enough STC-007 lines in buffer:"<<input_deque->size()<<", required:"<<(STC007DataBlock::MIN_DEINT_DATA+line_shift+1);
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

    // Set primary audio resolution and try count.
    if(data_res_mode==RES_MODE_14BIT)
    {
        // 14 bit strict.
        run_audio_res = STC007DataBlock::RES_14BIT;
        // Allow only one pass.
        fill_passes = MAX_PASSES;
    }
    else if(data_res_mode==RES_MODE_14BIT_AUTO)
    {
        // 14 bit loose, start from 14-bit mode.
        run_audio_res = STC007DataBlock::RES_14BIT;
        // Allow 14-16-14 bit passes to determine what's best.
        fill_passes = 0;
    }
    else if(data_res_mode==RES_MODE_16BIT_AUTO)
    {
        // 16 bit loose, start from 16-bit mode.
        run_audio_res = STC007DataBlock::RES_16BIT;
        // Allow 16-14-16 bit passes to determine what's best.
        fill_passes = 0;
    }
    else
    {
        // 16 bit strict.
        run_audio_res = STC007DataBlock::RES_16BIT;
        // Allow only one pass.
        fill_passes = MAX_PASSES;
    }

    // Preset "no index" value for error locators.
    first_bad = second_bad = NO_ERR_INDEX;
    all_crc_errs = aud_crc_errs = 0;
    proc_state = STG_DATA_FILL;

    // Cycle through stages.
    stage_count = 0;
    do
    {
        // Count loops.
        stage_count++;

/*#ifdef DI_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[DI-007]"<<QString::fromStdString(out_data_block->textDump());
        }
#endif
*/
        //qDebug()<<"[DI-007] State #"<<proc_state<<", stage"<<stage_count;

        // Select processing mode.
        if(proc_state==STG_DATA_FILL)
        {
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                if(run_audio_res==STC007DataBlock::RES_16BIT)
                {
                    qInfo()<<"[DI-007] -------------------- Filling data in 16-bit mode, try"<<fill_passes;
                }
                else
                {
                    qInfo()<<"[DI-007] -------------------- Filling data in 14-bit mode, try"<<fill_passes;
                }
            }
#endif
            out_data_block->clear();
            // Fill data block with words from interleaved PCM-lines.
            if(use_vector==false)
            {
                setWordData(&((*input_deque)[STC007DataBlock::LINE_L0+line_shift]), &((*input_deque)[STC007DataBlock::LINE_R0+line_shift]),
                            &((*input_deque)[STC007DataBlock::LINE_L1+line_shift]), &((*input_deque)[STC007DataBlock::LINE_R1+line_shift]),
                            &((*input_deque)[STC007DataBlock::LINE_L2+line_shift]), &((*input_deque)[STC007DataBlock::LINE_R2+line_shift]),
                            &((*input_deque)[STC007DataBlock::LINE_P0+line_shift]), &((*input_deque)[STC007DataBlock::LINE_Q0+line_shift]),
                            out_data_block, run_audio_res);
            }
            else
            {
                setWordData(&((*input_vector)[STC007DataBlock::LINE_L0+line_shift]), &((*input_vector)[STC007DataBlock::LINE_R0+line_shift]),
                            &((*input_vector)[STC007DataBlock::LINE_L1+line_shift]), &((*input_vector)[STC007DataBlock::LINE_R1+line_shift]),
                            &((*input_vector)[STC007DataBlock::LINE_L2+line_shift]), &((*input_vector)[STC007DataBlock::LINE_R2+line_shift]),
                            &((*input_vector)[STC007DataBlock::LINE_P0+line_shift]), &((*input_vector)[STC007DataBlock::LINE_Q0+line_shift]),
                            out_data_block, run_audio_res);
            }
            // Reset audio state.
            out_data_block->markAsOriginalData();
            // Count number of fills.
            fill_passes++;
            // Go check CRC.
            proc_state = STG_ERROR_CHECK;
        }
        else if(proc_state==STG_ERROR_CHECK)
        {
            bool skip_processing;
            uint16_t p_code, q_code;

            skip_processing = false;
            // Preset "no index" value for error locators.
            first_bad = NO_ERR_INDEX;
            second_bad = NO_ERR_INDEX;
            // Reset "bad samples" counter.
            all_crc_errs = aud_crc_errs = 0;
            if(run_audio_res==STC007DataBlock::RES_14BIT)
            {
                if(out_data_block->getErrorsTotalSource()==out_data_block->getErrorsAudioSource())
                {
                    // Both ECC words are intact (all errors in the audio part).
                    // Calculate P-word from actual audio samples.
                    p_code = calcPcode(out_data_block);
                    // Calculate Q-word for 14-bit mode.
                    q_code = calcQcode(out_data_block);
                    // Check if P&Q codes are the same in the data block, indicating no damage.
                    if((p_code==out_data_block->getWord(STC007DataBlock::WORD_P0))&&
                       (q_code==out_data_block->getWord(STC007DataBlock::WORD_Q0))&&
                       (out_data_block->getErrorsAudioSource()>2))
                    {
                        // No need to count and trace anything.
                        skip_processing = true;
                    }
                }
            }
            /*else
            {
                // No Q-word for 16-bit mode.
                if((p_code==out_data_block->getWord(STC007DataBlock::WORD_P0))&&
                   (out_data_block->getErrorsTotalSource()>0))
                {
                    // No need to count and trace anything.
                    skip_processing = true;
                }
            }*/

            if(skip_processing==false)
            {
                // Search for audio samples with bad CRC.
                for(uint8_t index=STC007DataBlock::WORD_L0;index<=STC007DataBlock::WORD_R2;index++)
                {
                    if(out_data_block->isWordLineCRCOk(index)==false)
                    {
                        // Count number of errors in audio samples.
                        if(first_bad==NO_ERR_INDEX)
                        {
                            // Save index of first error in audio data.
                            first_bad = index;
                        }
                        else if(second_bad==NO_ERR_INDEX)
                        {
                            // Save index of second error in audio data.
                            second_bad = index;
                            break;
                        }
                    }
                }
                // Get number of words with bad CRC.
                aud_crc_errs = out_data_block->getErrorsAudioSource();
                all_crc_errs = out_data_block->getErrorsTotalSource();
                proc_state = STG_TASK_SELECTION;
            }
            else
            {
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] All words match up and good while indicated"<<out_data_block->getErrorsTotalSource()<<"errors";
                }
#endif
                // Mark all words as valid.
                for(uint8_t index=STC007DataBlock::WORD_L0;index<=STC007DataBlock::WORD_Q0;index++)
                {
                    out_data_block->setValid(index);
                }
                proc_state = STG_DATA_OK;
            }
        }
        else if(proc_state==STG_TASK_SELECTION)
        {
            // Preset bad result.
            proc_state = STG_BAD_BLOCK;

            // Check number of bad words.
            if(all_crc_errs<=2)
            {
                // 2 or less errors, that can be handled by P and Q correction.
                if(aud_crc_errs==0)
                {
                    // No audio CRC errors in the data block.
                    if(force_parity_check==false)
                    {
                        // Force parity check disabled.
                        // All audio samples are fine - data is valid.
                        // (don't care about any errors in P&Q words)
                        proc_state = STG_DATA_OK;
                    }
                    else if(en_p_code!=false)
                    {
                        // P-code correction is allowed.
                        // Override and force parity check.
                        proc_state = STG_P_CORR;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-007] No audio CRC errors, but parity check is forced, checking with P-code...";
                        }
#endif
                    }
                    else
                    {
                        // Force parity check enabled, but P-code checks are disabled (assume Q-code disabled as well).
                        // Assume all audio samples are fine - data is valid.
                        // (don't care about any errors in P/Q words)
                        proc_state = STG_NO_CHECK;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-007] No audio CRC errors, parity check is forced, but P-code is disabled.";
                        }
#endif
                    }
                }
                else if(aud_crc_errs==1)
                {
                    // There is one corrupted audio sample. It can be fixed with P-code (or Q-code).
                    if(en_p_code!=false)
                    {
                        // P-code correction is allowed.
                        // Go to P-code correction stage.
                        proc_state = STG_P_CORR;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-007] One audio sample #"<<first_bad<<"is CRC-marked, trying to correct with P-code...";
                        }
#endif
                    }
#ifdef DI_EN_DBG_OUT
                    else
                    {
                        // P-code correction is disabled (and Q-code as well).
                        // Unable to do anything with an error.
                        // Bad state was preset at the start.
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-007] One audio sample #"<<first_bad<<"is CRC-marked, but P-code is disabled, unable to fix an error!";
                        }
                    }
#endif
                }
                else if(aud_crc_errs==2)
                {
                    // There are two corrupted audio samples.
                    if(run_audio_res==STC007DataBlock::RES_14BIT)
                    {
                        // 14-bit mode (STC-007/STC-008).
                        if(en_q_code!=false)
                        {
                            // Q-code correction is allowed.
                            // Go to Q-code correction stage.
                            proc_state = STG_Q_CORR;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-007] Two audio samples are CRC-marked, trying to correct with Q-code...";
                            }
#endif
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            // Q-code correction is disabled.
                            // Unable to do anything with an error.
                            // Bad state was preset at the start.
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-007] Two audio samples are CRC-marked, but Q-code is disabled, unable to fix errors!";
                            }
                        }
#endif
                    }
                    else
                    {
                        // 16-bit mode (PCM-F1).
                        // There is no Q-code available in 16-bit mode.
                        if((en_cwd!=false)&&(out_data_block->isDataFixedByCWD()==false))
                        {
                            // Try to apply data from CWD.
                            proc_state = STG_CWD_CORR;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-007] Two audio samples are CRC-marked, no Q-code (16-bit mode), trying CWD...";
                            }
#endif
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            // No fix.
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-007] Two audio samples are CRC-marked, no Q-code (16-bit mode), unable to fix the data!";
                            }
                        }
#endif
                    }
                }
            }
            else
            {
                // More than two corrupted words.
                // It can not be restored to original state by default corrections.
                if((en_cwd!=false)&&(out_data_block->cwd_applied==false))
                {
                    // Try to apply data from CWD.
                    proc_state = STG_CWD_CORR;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] More than two samples are CRC-marked, trying CWD...";
                    }
#endif
                }
#ifdef DI_EN_DBG_OUT
                else
                {
                    // Dropout detected.
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] More than two words are CRC-marked, that can not be corrected!";
                    }
                }
#endif
            }
        }
        else if(proc_state==STG_CWD_CORR)
        {
            // Try to "fix" data with Cross-Word Decoding if pre-deinterleave was performed.
            uint8_t fix_count;
            fix_count = 0;
            // Preset bad result.
            proc_state = STG_BAD_BLOCK;
            // Scan words for already pre-fixed words during pre-scan.
            for(uint8_t index=STC007DataBlock::WORD_L0;index<=STC007DataBlock::WORD_Q0;index++)
            {
                if((out_data_block->isWordCWDFixed(index)!=false)/*&&(out_data_block->isWordValid(index)==false)*/)
                {
                    // Data word has flag that this word was fixed in pre-scan.
                    out_data_block->setFixed(index);
                    out_data_block->markAsFixedByCWD();
                    fix_count++;

                    // DEBUG
                    //suppress_log = false;

#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] Sample #"<<index<<"is fixed during CWD pre-scan, updating valid flag...";
                    }
#endif
                }
            }
            // Check number of CWD corrections.
            if(fix_count!=0)
            {
                // Reset "bad samples" counter.
                first_bad = second_bad = NO_ERR_INDEX;
                all_crc_errs = aud_crc_errs = 0;
                // Search for audio samples with bad CRC.
                for(uint8_t index=STC007DataBlock::WORD_L0;index<=STC007DataBlock::WORD_Q0;index++)
                {
                    if(out_data_block->isWordValid(index)==false)
                    {
                        // Calculate number of words that still have errors.
                        all_crc_errs++;
                    }
                    if(index<=STC007DataBlock::WORD_R2)
                    {
                        if(out_data_block->isWordValid(index)==false)
                        {
                            // Count number of errors in audio samples.
                            aud_crc_errs++;
                            if(first_bad==NO_ERR_INDEX)
                            {
                                // Save index of first error in audio data.
                                first_bad = index;
                            }
                            else if(second_bad==NO_ERR_INDEX)
                            {
                                // Save index of second error in audio data.
                                second_bad = index;
                            }
                        }
                    }
                }
                // Repeat process.
                proc_state = STG_TASK_SELECTION;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007]"<<fix_count<<"samples fixed with CWD, retrying standard ECC...";
                }
#endif
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Cross-Word Decoding didn't help, unable to fix errors!";
                }
            }
#endif
        }
        else if(proc_state==STG_P_CORR)
        {
            // Try to check parity with with P-code.
            // Preset bad result.
            proc_state = STG_BAD_BLOCK;
            // Check if P-word is not damaged.
            if(out_data_block->isWordValid(STC007DataBlock::WORD_P0)!=false)
            {
                // Fix audio sample with parity if required (audio resolution is already preset).
                fix_result = fixByP(out_data_block, first_bad);
                if(fix_result==FIX_BROKEN)
                {
                    // Audio data is BROKEN.
                    // Bad state was preset at the start.
                    out_data_block->markAsBroken();
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] Broken data detected!";
                    }
#endif
                }
                else
                {
                    // Audio is not BROKEN (yet).
                    proc_state = STG_DATA_OK;
                    // Clear "fixed by CWD" flag if present.
                    out_data_block->clearWordFixedByCWD(first_bad);
                    if(fix_result==FIX_DONE)
                    {
                        // Audio data is fixed with P-code.
                        out_data_block->markAsFixedByP();
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-007] Successfully fixed the error with P-code";
                        }
#endif
                    }
                    else if(fix_result==FIX_NOT_NEED)
                    {
                        // There was no actual error at the pointer.
                        /*if(first_bad<STC007DataBlock::WORD_P0)
                        {
                            out_data_block->markAsFixedByP();
                        }*/
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-007] No P-code error detected";
                        }
#endif
                    }
                    // Check if resolution set to 14-bit and Q-code is available.
                    if((run_audio_res==STC007DataBlock::RES_14BIT)&&                            // Audio resolution is 14-bit, Q-code present.
                       (en_q_code!=false))                                                      // Usage of Q-code is allowed.
                    {
                        // Check if Q-word is valid.
                        if(out_data_block->isWordValid(STC007DataBlock::WORD_Q0)!=false)
                        {
                            // Check if force check is required.
                            if(force_parity_check!=false)                                       // Forced check is enabled.
                            {
                                uint16_t synd_q;
                                // Calculate syndrome for Q-code (all audio words and P-code are intact as check showed).
                                synd_q = calcSyndromeQ(out_data_block);
                                if(synd_q!=0)
                                {
#ifdef DI_EN_DBG_OUT
                                    if(suppress_log==false)
                                    {
                                        qInfo()<<"[DI-007] But Q-code check resulted in error! Broken data detected!";
                                    }
#endif
                                    // Broken data by Q-code.
                                    proc_state = STG_BAD_BLOCK;
                                    out_data_block->markAsBroken();
                                }
#ifdef DI_EN_DBG_OUT
                                else
                                {
                                    if(suppress_log==false)
                                    {
                                        qInfo()<<"[DI-007] No Q-code error detected";
                                    }
                                }
#endif
                            }
                        }
                        else
                        {
                            // Q-code is damaged, fix it as well as soon as all other samples are fine.
                            uint16_t q_code;
                            q_code = calcQcode(out_data_block);
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                QString log_line;
                                log_line.sprintf("[DI-007] Q-word is damaged, patching it: 0x%04u -> 0x%04u",
                                                 out_data_block->getWord(STC007DataBlock::WORD_Q0), q_code);
                                qInfo()<<log_line;
                            }
#endif
                            out_data_block->setWord(STC007DataBlock::WORD_Q0, q_code, out_data_block->isWordLineCRCOk(STC007DataBlock::WORD_Q0), false);
                            out_data_block->setFixed(STC007DataBlock::WORD_Q0);
                        }
                    }
                }
            }
            else
            {
                // Can not check parity because P-code word itself is damaged.
                if(run_audio_res==STC007DataBlock::RES_14BIT)
                {
                    // Q-code is available (14-bit mode).
                    if(en_q_code!=false)
                    {
                        // Q-code correction is allowed.
                        // Switch to Q-code mode.
                        proc_state = STG_Q_CORR;
#ifdef DI_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[DI-007] P-code word is bad, switching to Q-code check";
                        }
#endif
                    }
                    else
                    {
                        // Q-code correction is disabled.
                        if(aud_crc_errs==0)
                        {
                            // No CRC marks (just forced check).
                            // Can not check the data.
                            proc_state = STG_NO_CHECK;
#ifdef DI_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-007] P-code word is bad, but Q-code is disabled, unable to verify parity";
                            }
#endif
                        }
#ifdef DI_EN_DBG_OUT
                        else
                        {
                            // There is at least one audio error, P-code is also bad and Q-code is disabled.
                            // Unable to do anything with an error.
                            // Bad state was preset at the start.
                            if(suppress_log==false)
                            {
                                qInfo()<<"[DI-007] P-code word is bad, but Q-code is disabled, unable to fix an error!";
                            }
                        }
#endif
                    }
                }
                else if(aud_crc_errs==0)
                {
                    // No Q-code available (16-bit mode) and no CRC marks (just forced check).
                    // Can not check the data.
                    proc_state = STG_NO_CHECK;
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] P-code word is bad, unable to verify parity";
                    }
#endif
                }
#ifdef DI_EN_DBG_OUT
                else
                {
                    // No Q-code (16-bit mode) and one audio CRC mark, but bad P-code.
                    // Data can not be corrected.
                    // Bad state was preset at the start.
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] P-code word is bad, no Q-code, can not fix the data";
                    }
                }
#endif
            }
        }
        else if(proc_state==STG_Q_CORR)
        {
            // Preset bad result.
            proc_state = STG_BAD_BLOCK;
            // Try to correct errors with Q-code (only for 14-bit mode).
            if(out_data_block->isWordValid(STC007DataBlock::WORD_Q0)!=false)
            {
                // Fix two samples with Q and P codes.
                fix_result = fixByQ(out_data_block, first_bad, second_bad);
                // Workaround for Q-fix for P-code.
                // Error markers point only on audio samples.
                // Set second marker on P-code for CWD marking if needed.
                if(out_data_block->isWordLineCRCOk(STC007DataBlock::WORD_P0)==false)
                {
                    second_bad = STC007DataBlock::WORD_P0;
                }
                if(fix_result==FIX_DONE)
                {
                    // Audio data is corrected.
                    proc_state = STG_DATA_OK;
                    // Clear "fixed by CWD" flags if present.
                    out_data_block->clearWordFixedByCWD(first_bad);
                    out_data_block->clearWordFixedByCWD(second_bad);
                    // Mark data block as fixed by Q-code.
                    out_data_block->markAsFixedByQ();
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] Successfully fixed errors with Q-code";
                    }
#endif
                }
                else if(fix_result==FIX_NOT_NEED)
                {
                    // No actual errors at pointers, data is ok.
                    proc_state = STG_DATA_OK;
                    // Clear "fixed by CWD" flags if present.
                    out_data_block->clearWordFixedByCWD(first_bad);
                    out_data_block->clearWordFixedByCWD(second_bad);
                    /*if(first_bad<STC007DataBlock::WORD_P0)
                    {
                        // Mark data block as fixed by Q-code if it was not just forced check.
                        out_data_block->markAsFixedByQ();
                    }*/
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] ECC has not detected any errors";
                    }
#endif
                }
                else if(fix_result==FIX_SWITCH_P)
                {
                    // Need to switch to correction with P-code.
                    proc_state = STG_P_CORR;
                }
                else if(fix_result==FIX_BROKEN)
                {
                    // Audio data is broken.
                    // Bad state was preset at the start.
                    out_data_block->markAsBroken();
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] Broken data detected!";
                    }
#endif
                }
                // In other cases bad state was preset at the start.
            }
            else
            {
                // Can not check data because Q-code word is damaged.
                if(first_bad==NO_ERR_INDEX)
                {
                    // No CRC marks were provided (just forced check).
                    // Unable to check the data.
                    proc_state = STG_NO_CHECK;
                    // Re-calculate and fill in P and Q words.
                    uint16_t ecc_word;
                    ecc_word = calcPcode(out_data_block);
                    out_data_block->setWord(STC007DataBlock::WORD_P0, ecc_word, false, out_data_block->isWordCWDFixed(STC007DataBlock::WORD_P0));
                    out_data_block->setFixed(STC007DataBlock::WORD_P0);
                    ecc_word = calcQcode(out_data_block);
                    out_data_block->setWord(STC007DataBlock::WORD_Q0, ecc_word, false, out_data_block->isWordCWDFixed(STC007DataBlock::WORD_Q0));
                    out_data_block->setFixed(STC007DataBlock::WORD_Q0);
#ifdef DI_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] Q-code word is bad, unable to check audio data, re-calculated P and Q";
                    }
#endif
                }
#ifdef DI_EN_DBG_OUT
                else
                {
                    // CRC marks are present.
                    // No hope here.
                    // Bad state was preset at the start.
                    if(suppress_log==false)
                    {
                        qInfo()<<"[DI-007] Q-code word is bad, can not fix the errors";
                    }
                }
#endif
            }
        }
        else if(proc_state==STG_BAD_BLOCK)
        {
            // Errors in audio samples can not be fixed.
            out_data_block->clearFixedByCWD();
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[DI-007] Audio data can not be fixed, dropout detected!";
            }
#endif
            if(fill_passes>=MAX_PASSES)
            {
                // Exit stage cycle.
                break;
            }
            else
            {
                // Try to switch audio resolution and fill again.
                if(run_audio_res==STC007DataBlock::RES_16BIT)
                {
                    run_audio_res = STC007DataBlock::RES_14BIT;
                }
                else
                {
                    run_audio_res = STC007DataBlock::RES_16BIT;
                }
                proc_state = STG_DATA_FILL;
            }
        }
        else if(proc_state==STG_NO_CHECK)
        {
            // No CRC errors in audio words, but unable to force-check integrity due to bad P/Q-code.
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[DI-007] Audio samples are not damaged but can not be verified!";
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
                qInfo()<<"[DI-007] All audio samples are good";
                if(out_data_block->isDataFixedByCWD()!=false)
                {
                    qInfo()<<"[DI-007] Fixed by CWD";
                }
            }
#endif
            // Exit stage cycle.
            break;
        }
        else
        {
#ifdef QT_VERSION
            qWarning()<<DBG_ANCHOR<<"[DI-007] Impossible state detected in [STC007Deinterleaver::processBlock()], breaking...";
#endif
            // Exit stage cycle.
            break;
        }

        // Check for looping.
        if(stage_count>(STG_CONVERT_MAX*MAX_PASSES))
        {
#ifdef QT_VERSION
            qWarning()<<DBG_ANCHOR<<"[DI-007] Inf. loop detected in [STC007Deinterleaver::processBlock()], breaking...";
#endif
            // Exit stage cycle.
            break;
        }
    }
    while(1);   // Stages cycle.

    // Store time that processing took.
    quint64 block_time;
    block_time = 0;
#ifdef QT_VERSION
    block_time = time_per_block.nsecsElapsed();
#endif
    out_data_block->process_time = (uint32_t)(block_time/1000);

    return DI_RET_OK;
}

//---------------------- Set data and CRC state for all words in the data block.
void STC007Deinterleaver::setWordData(STC007Line *line_L0, STC007Line *line_R0,
                                      STC007Line *line_L1, STC007Line *line_R1,
                                      STC007Line *line_L2, STC007Line *line_R2,
                                      STC007Line *line_P0, STC007Line *line_Q0,
                                      STC007DataBlock *out_data_block,
                                      uint8_t in_resolution)
{
    bool word1_crc_ok, word2_crc_ok, word3_crc_ok, word4_crc_ok, word5_crc_ok, word6_crc_ok, word7_crc_ok, word8_crc_ok;
    bool sword1_crc_ok, sword2_crc_ok, sword3_crc_ok, sword4_crc_ok, sword5_crc_ok, sword6_crc_ok, sword7_crc_ok;

    // Check if CRC should be ignored.

    word1_crc_ok = word2_crc_ok = word3_crc_ok = word4_crc_ok = word5_crc_ok = word6_crc_ok = word7_crc_ok = word8_crc_ok = true;
    if(ignore_crc==false)
    {
        // Take CRC states for the words.
        word1_crc_ok = line_L0->isWordCRCOk(STC007Line::WORD_L_SH0);
        word2_crc_ok = line_R0->isWordCRCOk(STC007Line::WORD_R_SH48);
        word3_crc_ok = line_L1->isWordCRCOk(STC007Line::WORD_L_SH95);
        word4_crc_ok = line_R1->isWordCRCOk(STC007Line::WORD_R_SH143);
        word5_crc_ok = line_L2->isWordCRCOk(STC007Line::WORD_L_SH190);
        word6_crc_ok = line_R2->isWordCRCOk(STC007Line::WORD_R_SH238);
        word7_crc_ok = line_P0->isWordCRCOk(STC007Line::WORD_P_SH288);
        word8_crc_ok = line_Q0->isWordCRCOk(STC007Line::WORD_Q_SH336);
    }

    // Check provided resolution.
    if(in_resolution==STC007DataBlock::RES_14BIT)
    {
        // 14-bit mode (STC-007).
        // Copy L0.
        out_data_block->setWord(STC007DataBlock::WORD_L0, line_L0->getWord(STC007Line::WORD_L_SH0), word1_crc_ok, line_L0->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_L0, line_L0->frame_number, line_L0->line_number);
        // Copy R0.
        out_data_block->setWord(STC007DataBlock::WORD_R0, line_R0->getWord(STC007Line::WORD_R_SH48), word2_crc_ok, line_R0->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_R0, line_R0->frame_number, line_R0->line_number);
        // Copy L1.
        out_data_block->setWord(STC007DataBlock::WORD_L1, line_L1->getWord(STC007Line::WORD_L_SH95), word3_crc_ok, line_L1->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_L1, line_L1->frame_number, line_L1->line_number);
        // Copy R1.
        out_data_block->setWord(STC007DataBlock::WORD_R1, line_R1->getWord(STC007Line::WORD_R_SH143), word4_crc_ok, line_R1->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_R1, line_R1->frame_number, line_R1->line_number);
        // Copy L2.
        out_data_block->setWord(STC007DataBlock::WORD_L2, line_L2->getWord(STC007Line::WORD_L_SH190), word5_crc_ok, line_L2->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_L2, line_L2->frame_number, line_L2->line_number);
        // Copy R2.
        out_data_block->setWord(STC007DataBlock::WORD_R2, line_R2->getWord(STC007Line::WORD_R_SH238), word6_crc_ok, line_R2->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_R2, line_R2->frame_number, line_R2->line_number);
        // Copy P0.
        out_data_block->setWord(STC007DataBlock::WORD_P0, line_P0->getWord(STC007Line::WORD_P_SH288), word7_crc_ok, line_P0->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_P0, line_P0->frame_number, line_P0->line_number);
        // Copy Q0.
        out_data_block->setWord(STC007DataBlock::WORD_Q0, line_Q0->getWord(STC007Line::WORD_Q_SH336), word8_crc_ok, line_Q0->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_Q0, line_Q0->frame_number, line_Q0->line_number);
    }
    else
    {
        // 16-bit mode (PCM-F1).
        bool word_valid;
        uint16_t s_word, f1_word;

        sword1_crc_ok = sword2_crc_ok = sword3_crc_ok = sword4_crc_ok = sword5_crc_ok = sword6_crc_ok = sword7_crc_ok = true;
        // Check if CRC should be ignored.
        if(ignore_crc==false)
        {
            // Take CRC states for the Q/S-words.
            sword1_crc_ok = line_L0->isWordCRCOk(STC007Line::WORD_Q_SH336);
            sword2_crc_ok = line_R0->isWordCRCOk(STC007Line::WORD_Q_SH336);
            sword3_crc_ok = line_L1->isWordCRCOk(STC007Line::WORD_Q_SH336);
            sword4_crc_ok = line_R1->isWordCRCOk(STC007Line::WORD_Q_SH336);
            sword5_crc_ok = line_L2->isWordCRCOk(STC007Line::WORD_Q_SH336);
            sword6_crc_ok = line_R2->isWordCRCOk(STC007Line::WORD_Q_SH336);
            sword7_crc_ok = line_P0->isWordCRCOk(STC007Line::WORD_Q_SH336);
        }

        // Copy L0.
        // Copy data from interleaved lines while adding two LSB's from S-words.
        f1_word = (line_L0->getWord(STC007Line::WORD_L_SH0)<<STC007DataBlock::F1_WORD_OFS);     // Move 14-bit word towards MSBs,
        s_word = (line_L0->getWord(STC007Line::WORD_Q_SH336)>>STC007DataBlock::F1_S_L0_OFS)&STC007DataBlock::F1_S_MASK;     // Add 2 LSB bits from S-word in the same line.
        // Calculate CRC validity from two words in the same line.
        word_valid = (word1_crc_ok&sword1_crc_ok);
        out_data_block->setWord(STC007DataBlock::WORD_L0, (f1_word+s_word), word_valid, line_L0->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_L0, line_L0->frame_number, line_L0->line_number);
        // Copy R0.
        f1_word = (line_R0->getWord(STC007Line::WORD_R_SH48)<<STC007DataBlock::F1_WORD_OFS);
        s_word = (line_R0->getWord(STC007Line::WORD_Q_SH336)>>STC007DataBlock::F1_S_R0_OFS)&STC007DataBlock::F1_S_MASK;
        word_valid = (word2_crc_ok&sword2_crc_ok);
        out_data_block->setWord(STC007DataBlock::WORD_R0, (f1_word+s_word), word_valid, line_R0->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_R0, line_R0->frame_number, line_R0->line_number);
        // Copy L1.
        f1_word = (line_L1->getWord(STC007Line::WORD_L_SH95)<<STC007DataBlock::F1_WORD_OFS);
        s_word = (line_L1->getWord(STC007Line::WORD_Q_SH336)>>STC007DataBlock::F1_S_L1_OFS)&STC007DataBlock::F1_S_MASK;
        word_valid = (word3_crc_ok&sword3_crc_ok);
        out_data_block->setWord(STC007DataBlock::WORD_L1, (f1_word+s_word), word_valid, line_L1->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_L1, line_L1->frame_number, line_L1->line_number);
        // Copy R1.
        f1_word = (line_R1->getWord(STC007Line::WORD_R_SH143)<<STC007DataBlock::F1_WORD_OFS);
        s_word = (line_R1->getWord(STC007Line::WORD_Q_SH336)>>STC007DataBlock::F1_S_R1_OFS)&STC007DataBlock::F1_S_MASK;
        word_valid = (word4_crc_ok&sword4_crc_ok);
        out_data_block->setWord(STC007DataBlock::WORD_R1, (f1_word+s_word), word_valid, line_R1->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_R1, line_R1->frame_number, line_R1->line_number);
        // Copy L2.
        f1_word = (line_L2->getWord(STC007Line::WORD_L_SH190)<<STC007DataBlock::F1_WORD_OFS);
        s_word = (line_L2->getWord(STC007Line::WORD_Q_SH336)>>STC007DataBlock::F1_S_L2_OFS)&STC007DataBlock::F1_S_MASK;
        word_valid = (word5_crc_ok&sword5_crc_ok);
        out_data_block->setWord(STC007DataBlock::WORD_L2, (f1_word+s_word), word_valid, line_L2->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_L2, line_L2->frame_number, line_L2->line_number);
        // Copy R2.
        f1_word = (line_R2->getWord(STC007Line::WORD_R_SH238)<<STC007DataBlock::F1_WORD_OFS);
        s_word = (line_R2->getWord(STC007Line::WORD_Q_SH336)>>STC007DataBlock::F1_S_R2_OFS)&STC007DataBlock::F1_S_MASK;
        word_valid = (word6_crc_ok&sword6_crc_ok);
        out_data_block->setWord(STC007DataBlock::WORD_R2, (f1_word+s_word), word_valid, line_R2->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_R2, line_R2->frame_number, line_R2->line_number);
        // Copy P0.
        f1_word = (line_P0->getWord(STC007Line::WORD_P_SH288)<<STC007DataBlock::F1_WORD_OFS);
        s_word = (line_P0->getWord(STC007Line::WORD_Q_SH336)>>STC007DataBlock::F1_S_P0_OFS)&STC007DataBlock::F1_S_MASK;
        word_valid = (word7_crc_ok&sword7_crc_ok);
        out_data_block->setWord(STC007DataBlock::WORD_P0, (f1_word+s_word), word_valid, line_P0->isFixedByCWD());
        out_data_block->setSource(STC007DataBlock::WORD_P0, line_P0->frame_number, line_P0->line_number);
        // Zero out unused Q-word in data block structure (Q/S word is spread into 16-bit PCM words), so it will not count as damaged.
        out_data_block->setWord(STC007DataBlock::WORD_Q0, 0, true, false);
        out_data_block->setSource(STC007DataBlock::WORD_Q0, line_Q0->frame_number, line_Q0->line_number);
    }
    // Set samples resolution.
    out_data_block->setResolution(in_resolution);
#ifdef DI_EN_DBG_OUT
    if((log_level&LOG_ERROR_CORR)!=0)
    {
        QString log_line;
        log_line.sprintf("[DI-007] Added data from lines [%03u-%03u][%u|%u]-%03u[%u|%u]-%03u[%u|%u]-%03u[%u|%u]-%03u[%u|%u]-%03u[%u|%u]-%03u[%u|%u]-[%03u-%03u][%u|%u]",
                         line_L0->frame_number, line_L0->line_number, line_L0->isCRCValid(), line_L0->isFixedByCWD(),
                         line_R0->line_number, line_R0->isCRCValid(), line_R0->isFixedByCWD(),
                         line_L1->line_number, line_L1->isCRCValid(), line_L1->isFixedByCWD(),
                         line_R1->line_number, line_R1->isCRCValid(), line_R1->isFixedByCWD(),
                         line_L2->line_number, line_L2->isCRCValid(), line_L2->isFixedByCWD(),
                         line_R2->line_number, line_R2->isCRCValid(), line_R2->isFixedByCWD(),
                         line_P0->line_number, line_P0->isCRCValid(), line_P0->isFixedByCWD(),
                         line_Q0->frame_number, line_Q0->line_number, line_Q0->isCRCValid(), line_Q0->isFixedByCWD());
        qInfo()<<log_line;
    }
#endif
}

//------------------------ Calculate P-code.
uint16_t STC007Deinterleaver::calcPcode(STC007DataBlock *data_block)
{
    uint16_t p_code;
    p_code = data_block->getWord(STC007DataBlock::WORD_L0)^data_block->getWord(STC007DataBlock::WORD_R0)^
             data_block->getWord(STC007DataBlock::WORD_L1)^data_block->getWord(STC007DataBlock::WORD_R1)^
             data_block->getWord(STC007DataBlock::WORD_L2)^data_block->getWord(STC007DataBlock::WORD_R2);
    return p_code;
}

//------------------------ Calculate Q-code.
uint16_t STC007Deinterleaver::calcQcode(STC007DataBlock *data_block)
{
    uint16_t q_code;
    q_code = multMatrix((uint16_t *)TP6_MATRIX, data_block->getWord(STC007DataBlock::WORD_L0));
    q_code = q_code^multMatrix((uint16_t *)TP5_MATRIX, data_block->getWord(STC007DataBlock::WORD_R0));
    q_code = q_code^multMatrix((uint16_t *)TP4_MATRIX, data_block->getWord(STC007DataBlock::WORD_L1));
    q_code = q_code^multMatrix((uint16_t *)TP3_MATRIX, data_block->getWord(STC007DataBlock::WORD_R1));
    q_code = q_code^multMatrix((uint16_t *)TP2_MATRIX, data_block->getWord(STC007DataBlock::WORD_L2));
    q_code = q_code^multMatrix((uint16_t *)TP1_MATRIX, data_block->getWord(STC007DataBlock::WORD_R2));
    return q_code;
}

//------------------------ Calculate syndrome for P-code (parity).
uint16_t STC007Deinterleaver::calcSyndromeP(STC007DataBlock *data_block)
{
    uint16_t syndrome;
    syndrome = calcPcode(data_block)^data_block->getWord(STC007DataBlock::WORD_P0);
    return syndrome;
}

//------------------------ Calculate syndrome for Q-code (B-adjacent).
uint16_t STC007Deinterleaver::calcSyndromeQ(STC007DataBlock *data_block)
{
    uint16_t syndrome;
    syndrome = calcQcode(data_block)^data_block->getWord(STC007DataBlock::WORD_Q0);
    return syndrome;
}

//------------------------ Recalculate P-word and make it valid.
void STC007Deinterleaver::recalcP(STC007DataBlock *data_block)
{
    bool suppress_log;
    uint16_t old_p, p_code;

    suppress_log = !((log_level&LOG_ERROR_CORR)!=0);

    // Save old word.
    old_p = data_block->getWord(STC007DataBlock::WORD_P0);
    // Calculate new value for P-code.
    p_code = calcPcode(data_block);
    if(old_p!=p_code)
    {
        // Store new P-word.
        data_block->setWord(STC007DataBlock::WORD_P0, p_code, data_block->isWordLineCRCOk(STC007DataBlock::WORD_P0), false);
        // Mark P-word as fixed.
        data_block->setFixed(STC007DataBlock::WORD_P0);
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[DI-007] P-code updated 0x%04x -> 0x%04x and now valid", old_p, data_block->getWord(STC007DataBlock::WORD_P0));
            qInfo()<<log_line;
        }
#endif
    }
    else
    {
        // Mark P-word as valid.
        data_block->setValid(STC007DataBlock::WORD_P0);
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[DI-007] P-code doesn't need fixing, updated to be valid";
        }
#endif
    }
}

//------------------------ Calculate syndrome for P-code and fix an error.
uint8_t STC007Deinterleaver::fixByP(STC007DataBlock *data_block, uint8_t first_bad)
{
    bool suppress_log;
    uint16_t check, fix_word;

    data_block->markAsOriginalData();

    suppress_log = !((log_level&LOG_ERROR_CORR)!=0);

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        log_line = "[DI-007] Starting P-correction, bad index: ";
        if(first_bad==NO_ERR_INDEX)
        {
            log_line += "not set";
        }
        else
        {
            log_line += QString::number(first_bad);
        }
        qInfo()<<log_line;
    }
#endif

    // Calculate syndrome for P-code.
    check = calcSyndromeP(data_block);
    // Check if audio data needs to be fixed.
    if(check==0)
    {
        // Audio data is ok.
        if(first_bad!=NO_ERR_INDEX)
        {
            // Set sample as "ok" if it was marked with CRC.
            data_block->setValid(first_bad);
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[DI-007] Zero parity syndrome, the samples are ok. No error at"<<first_bad;
            }
#endif
        }
#ifdef DI_EN_DBG_OUT
        else
        {
            // No error pointer was set.
            if(suppress_log==false)
            {
                qInfo()<<"[DI-007] Zero parity syndrome, the samples are ok.";
            }
        }
#endif
        return FIX_NOT_NEED;
    }
    else if(first_bad==NO_ERR_INDEX)
    {
        // Error was detected, but there is no bad word pointer.
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[DI-007] Parity check failed (0x%04x) while no CRC markers were set, broken block!", check);
            qInfo()<<log_line;
        }
#endif
        // Data is broken.
        return FIX_BROKEN;
    }
    else
    {
        // Fix an error in the sample (flip errored bits in the word).
        fix_word = check^data_block->getWord(first_bad);
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[DI-007] Syndrome for P-code: 0x%04x, bad sample at [%01u], fix: 0x%04x -> 0x%04x",
                             check, first_bad, data_block->getWord(first_bad), fix_word);
            qInfo()<<log_line;
        }
#endif
        // Replace damaged word marked as non-valid, bad CRC.
        data_block->setWord(first_bad, fix_word, false, data_block->isWordValid(first_bad));
        // Mark as fixed.
        data_block->setFixed(first_bad);
        return FIX_DONE;
    }
}

//------------------------ Calculate syndromes for P-code (parity) and Q-code (b-adjacent code),
//------------------------ fix damaged samples.
uint8_t STC007Deinterleaver::fixByQ(STC007DataBlock *data_block, uint8_t first_bad, uint8_t second_bad)
{
    bool suppress_log, fix_found;
    uint16_t synd_p, synd_q, error_first, error_second, fix_word1, fix_word2;

    synd_p = 0;
    fix_found = false;
    data_block->markAsOriginalData();

    suppress_log = !((log_level&LOG_ERROR_CORR)!=0);

    // Check if second marker was not provided due to search only by audio samples.
    if(second_bad==NO_ERR_INDEX)
    {
        // Check if that can be P-code word.
        if(data_block->isWordValid(STC007DataBlock::WORD_P0)==false)
        {
            // Point second (empty) marker to P-code word.
            second_bad = STC007DataBlock::WORD_P0;
        }
    }

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        QString log_line;
        log_line = "[DI-007] Starting Q-correction, first bad index: ";
        if(first_bad==NO_ERR_INDEX)
        {
            log_line += "not set";
        }
        else
        {
            log_line += QString::number(first_bad);
        }
        log_line += ", second bad index: ";
        if(second_bad==NO_ERR_INDEX)
        {
            log_line += "not set";
        }
        else
        {
            log_line += QString::number(second_bad);
        }
        qInfo()<<log_line;
    }
#endif

    // Calculate syndrome for Q-code.
    synd_q = calcSyndromeQ(data_block);

    // Check if P-code is available.
    if(second_bad==STC007DataBlock::WORD_P0)
    {
        // Second corrupted word is P.
        if(synd_q==0)
        {
            // No error found in other words.
            if(first_bad!=NO_ERR_INDEX)
            {
                // Audio data is ok.
                data_block->setValid(first_bad);
                //data_block->setFixed(first_bad);
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] No ECC error detected, the audio samples are ok. No error at"<<first_bad;
                }
#endif
            }
#ifdef DI_EN_DBG_OUT
            else
            {
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] No ECC error detected, the audio samples are ok.";
                }
            }
#endif
            // Fix P-word.
            recalcP(data_block);
            return FIX_NOT_NEED;
        }
    }
    else
    {
        // Both damaged words are audio data, check both syndromes.
        // Calculate syndrome for P.
        synd_p = calcSyndromeP(data_block);
        if((synd_p==0)&&(synd_q==0))
        {
            // Audio data is ok.
            data_block->setValid(first_bad);
            data_block->setValid(second_bad);
#ifdef DI_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[DI-007] No ECC error detected, the audio samples are ok";
            }
#endif
            return FIX_NOT_NEED;
        }
    }

    // Check is there are two bad audio samples and P-code (3 errors actually).
    //if(data_block->getErrorsTotal()>2)
    if((second_bad!=STC007DataBlock::WORD_P0)&&(data_block->isWordValid(STC007DataBlock::WORD_P0)==false))
    {
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[DI-007] There are more than 2 marked words, can not fix those errors";
        }
#endif
        // Data is bad.
        return FIX_NA;
    }

    // Check for correct first marker.
    if(first_bad==NO_ERR_INDEX)
    {
        // Error was detected, but there are no bad word pointers.
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[DI-007] ECC check failed (0x%04x, 0x%04x) while no CRC markers were set, broken block!", synd_p, synd_q);
            qInfo()<<log_line;
        }
#endif
        // Data is broken.
        return FIX_BROKEN;
    }
    else if(second_bad==NO_ERR_INDEX)
    {
        // Error was detected, but there is only one bad word pointer.
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[DI-007] Data corruption is detected, but found only one CRC markers, switching to P-code correction";
        }
#endif
        // Switch to P-code correction.
        return FIX_SWITCH_P;
    }
    else
    {
        // Error was detected, error markers are set, proceed with correction.
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[DI-007] Syndromes for P-code and Q-code: 0x%04x, 0x%04x", synd_p, synd_q);
            qInfo()<<log_line;
        }
#endif
        // Reset error locators.
        error_first = 0;
        error_second = 0;
        // Select branch by defective sample positions.
        if(first_bad==STC007DataBlock::WORD_L0)
        {
            if(second_bad==STC007DataBlock::WORD_R0)
            {
                // Branch #1: L0-R0.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN5_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP1IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #1 (L0-R0)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_L1)
            {
                // Branch #7: L0-L1.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN4_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP2IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #7 (L0-L1)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_R1)
            {
                // Branch #12: L0-R1.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN3_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP3IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #12 (L0-R1)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_L2)
            {
                // Branch #16: L0-L2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN2_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP4IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #16 (L0-L2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_R2)
            {
                // Branch #19: L0-R2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN1_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP5IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #19 (L0-R2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_P0)
            {
                // Branch #21: L0-P0.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN6_MATRIX, synd_q);
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #21 (L0-P0)";
                }
#endif
            }
        }
        else if(first_bad==STC007DataBlock::WORD_R0)
        {
            if(second_bad==STC007DataBlock::WORD_L1)
            {
                // Branch #2: R0-L1.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN4_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP1IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #2 (R0-L1)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_R1)
            {
                // Branch #8: R0-R1.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN3_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP2IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #8 (R0-R1)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_L2)
            {
                // Branch #13: R0-L2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN2_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP3IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #13 (R0-L2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_R2)
            {
                // Branch #17: R0-R2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN1_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP4IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #17 (R0-R2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_P0)
            {
                // Branch #20: R0-P0.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN5_MATRIX, synd_q);
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #20 (R0-P0)";
                }
#endif
            }
        }
        else if(first_bad==STC007DataBlock::WORD_L1)
        {
            if(second_bad==STC007DataBlock::WORD_R1)
            {
                // Branch #3: L1-R1.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN3_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP1IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #3 (L1-R1)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_L2)
            {
                // Branch #9: L1-L2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN2_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP2IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #9 (L1-L2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_R2)
            {
                // Branch #14: L1-R2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN1_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP3IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #14 (L1-R2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_P0)
            {
                // Branch #18: L1-P0.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN4_MATRIX, synd_q);
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #18 (L1-P0)";
                }
#endif
            }
        }
        else if(first_bad==STC007DataBlock::WORD_R1)
        {
            if(second_bad==STC007DataBlock::WORD_L2)
            {
                // Branch #4: R1-L2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN2_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP1IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #4 (R1-L2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_R2)
            {
                // Branch #10: R1-R2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN1_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP2IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #10 (R1-R2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_P0)
            {
                // Branch #15: R1-P0.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN3_MATRIX, synd_q);
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #15 (R1-P0)";
                }
#endif
            }
        }
        else if(first_bad==STC007DataBlock::WORD_L2)
        {
            if(second_bad==STC007DataBlock::WORD_R2)
            {
                // Branch #5: L2-R2.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN1_MATRIX, synd_q);
                error_first = error_first^synd_p;
                error_first = multMatrix((uint16_t *)TP1IN1_MATRIX, error_first);
                // Calculate error locator 2.
                error_second = error_first^synd_p;
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #5 (L2-R2)";
                }
#endif
            }
            else if(second_bad==STC007DataBlock::WORD_P0)
            {
                // Branch #11: L2-P0.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN2_MATRIX, synd_q);
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #11 (L2-P0)";
                }
#endif
            }
        }
        else if(first_bad==STC007DataBlock::WORD_R2)
        {
            if(second_bad==STC007DataBlock::WORD_P0)
            {
                // Branch #6: R2-P0.
                // Calculate error locator 1.
                error_first = multMatrix((uint16_t *)TN1_MATRIX, synd_q);
                fix_found = true;
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[DI-007] Q-code branch #6 (R2-P0)";
                }
#endif
            }
        }
        // Check if error locators were found.
        if(fix_found!=false)
        {
            // Error locators were found.
            uint16_t old_word1, old_word2;
            // Calculate fix mask.
            old_word1 = data_block->getWord(first_bad);
            fix_word1 = old_word1^error_first;
            if(error_first!=0)
            {
                // Fix first word.
                data_block->setWord(first_bad, fix_word1, false, data_block->isWordCWDFixed(first_bad));    // Replace damaged words marked as non-valid, bad CRC.
                data_block->setFixed(first_bad);                                                            // Mark as valid.
            }
            else
            {
                // Nothing to fix, word was valid from the source.
                data_block->setValid(first_bad);
            }
            // Check special case for P-word (that only can be in the second place).
            old_word2 = data_block->getWord(second_bad);
            if(second_bad==STC007DataBlock::WORD_P0)
            {
                error_second = old_word2^calcPcode(data_block);
            }
            // Calculate fix mask.
            fix_word2 = old_word2^error_second;
            if(error_second!=0)
            {
                // Fix second word.
                data_block->setWord(second_bad, fix_word2, false, data_block->isWordCWDFixed(second_bad));
                data_block->setFixed(second_bad);
            }
            else
            {
                // Nothing to fix, word was valid from the source.
                data_block->setValid(second_bad);
            }
            if((error_first==0)&&(error_second==0))
            {
                // There was nothing to fix, all words were fine.
                return FIX_NOT_NEED;
            }
            else
            {
                // Audio data is corrected.
#ifdef DI_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString log_line;
                    log_line.sprintf("[DI-007] Bad samples at [%01u] and [%01u], fix: 0x%04x^0x%04x -> 0x%04x, 0x%04x^0x%04x -> 0x%04x",
                                     first_bad, second_bad,
                                     old_word1, error_first, fix_word1,
                                     old_word2, error_second, fix_word2);
                    qInfo()<<log_line;
                }
#endif
                return FIX_DONE;
            }
        }
        else
        {
            // Error locator was not found. Probably bad branching due to incorrect input.
#ifdef QT_VERSION
            qWarning()<<DBG_ANCHOR<<"[DI-007] Unexpected branch in [STC007Deinterleaver::fixByQ()]. Logic error, data block is broken!";
#endif
            // Data is broken.
            return FIX_BROKEN;
        }
    }
}

//------------------------ Perform MATRIX x VECTOR multiplication.
//------------------------ Note that matrix has to be horizontally mirrored.
uint16_t STC007Deinterleaver::multMatrix(uint16_t *matrix, uint16_t vector)
{
    uint16_t res_vec;
    uint16_t temp_matrix[STC007Line::BITS_PER_WORD];  // Matrix container for Q-code calculations.
    res_vec = 0;
    for(uint8_t bit=0;bit<STC007Line::BITS_PER_WORD;bit++)
    {
        // Perform modulo-2 multiplication on input matrix.
        temp_matrix[bit] = matrix[bit]&vector;
        // XOR all bits in the line;
        if(bitXOR(temp_matrix[bit])!=0)
        {
            // Put result into output vector.
            res_vec |= (1<<bit);
        }
    }
    return res_vec;
}

//------------------------ Perform XOR on all bits in the word.
uint8_t STC007Deinterleaver::bitXOR(uint16_t vector)
{
    uint8_t res_bit;
    res_bit = 0;
    // Cycle through all bits in the word.
    for(uint8_t bit=0;bit<STC007Line::BITS_PER_WORD;bit++)
    {
        // XOR if bit is set.
        if((vector&0x0001)!=0)
        {
            res_bit = res_bit^0x01;
        }
        // Shift bits.
        vector = (vector>>1);
    }
    return res_bit;
}
