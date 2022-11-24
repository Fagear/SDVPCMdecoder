#include "stc007datablock.h"

STC007DataBlock::STC007DataBlock()
{
    this->clear();
}

STC007DataBlock::STC007DataBlock(const STC007DataBlock &in_object)
{
    sample_rate = in_object.sample_rate;
    emphasis = in_object.emphasis;
    process_time = in_object.process_time;
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        w_frame[index] = in_object.w_frame[index];
        w_line[index] = in_object.w_line[index];
        words[index] = in_object.words[index];
        line_crc[index] = in_object.line_crc[index];
        cwd_fixed[index] = in_object.cwd_fixed[index];
        word_valid[index] = in_object.word_valid[index];
    }
    cwd_applied = in_object.cwd_applied;
    m2_format = in_object.m2_format;
    resolution = in_object.resolution;
    audio_state = in_object.audio_state;
}

STC007DataBlock& STC007DataBlock::operator= (const STC007DataBlock &in_object)
{
    if(this==&in_object) return *this;

    sample_rate = in_object.sample_rate;
    emphasis = in_object.emphasis;
    process_time = in_object.process_time;
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        w_frame[index] = in_object.w_frame[index];
        w_line[index] = in_object.w_line[index];
        words[index] = in_object.words[index];
        line_crc[index] = in_object.line_crc[index];
        cwd_fixed[index] = in_object.cwd_fixed[index];
        word_valid[index] = in_object.word_valid[index];
    }
    cwd_applied = in_object.cwd_applied;
    m2_format = in_object.m2_format;
    resolution = in_object.resolution;
    audio_state = in_object.audio_state;

    return *this;
}

//------------------------ Reset object.
void STC007DataBlock::clear()
{
    sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
    emphasis = false;
    process_time = 0;
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        w_frame[index] = 0;
        w_line[index] = 0;
        words[index] = 0;
        line_crc[index] = cwd_fixed[index] = word_valid[index] = false;
    }
    cwd_applied = false;
    m2_format = false;
    resolution = RES_14BIT;
    audio_state = AUD_ORIG;
}

//------------------------ Set resolution of audio.
void STC007DataBlock::setResolution(uint8_t in_res)
{
    if(in_res<RES_MAX)
    {
        resolution = in_res;
    }
}

//------------------------ Copy word into object with its state.
void STC007DataBlock::setWord(uint8_t index, uint16_t in_word, bool is_line_valid, bool is_cwd_fixed)
{
    if(index<WORD_CNT)
    {
        words[index] = in_word;
        line_crc[index] = word_valid[index] = is_line_valid;
        cwd_fixed[index] = is_cwd_fixed;
    }
}

//------------------------ Set source frame and line number for the word.
void STC007DataBlock::setSource(uint8_t index, uint32_t frame, uint16_t line)
{
    if(index<WORD_CNT)
    {
        w_frame[index] = frame;
        w_line[index] = line;
    }
}

//------------------------ Set word as fixed by error correction.
void STC007DataBlock::setFixed(uint8_t index)
{
    if(index<WORD_CNT)
    {
        word_valid[index] = true;
    }
}

//------------------------ Set word as valid from the source (with no error correction).
void STC007DataBlock::setValid(uint8_t index)
{
    if(index<WORD_CNT)
    {
        //line_crc[index] = true;   // Enabling this breaks CWD
        word_valid[index] = true;
        cwd_fixed[index] = false;
    }
}

//------------------------ Set state of audio data in the data block.
void STC007DataBlock::setAudioState(uint8_t in_state)
{
    if(in_state<AUD_MAX)
    {
        audio_state = in_state;
    }
}

//------------------------ Set emphasis state of contained samples.
void STC007DataBlock::setEmphasis(bool in_set)
{
    emphasis = in_set;
}

//------------------------ Set sample format (normal/M2).
void STC007DataBlock::setM2Format(bool in_set)
{
    m2_format = in_set;
}

//------------------------ Mark data words as "original state".
void STC007DataBlock::markAsOriginalData()
{
    audio_state = AUD_ORIG;
}

//------------------------ Mark data block as fixed by Cross-Word Decoding.
void STC007DataBlock::markAsFixedByCWD()
{
    // Set the mark that normal ECC can't deal with amount of errors and some were cleared with CWD.
    cwd_applied = true;
}

//------------------------ Mark data block as fixed by P-code.
void STC007DataBlock::markAsFixedByP()
{
    audio_state = AUD_FIX_P;
}

//------------------------ Mark data block as fixed by Q-code.
void STC007DataBlock::markAsFixedByQ()
{
    audio_state = AUD_FIX_Q;
}

//------------------------ Mark fixed words as bad again to prevent clicks on possible incorrect "corrections".
void STC007DataBlock::markAsUnsafe()
{
    if(audio_state==AUD_BROKEN)
    {
        return;
    }
    uint8_t ind_limit;
    // Determine word limit.
    if(resolution==RES_16BIT)
    {
        // Up to P word for 16-bit mode.
        ind_limit = WORD_P0;
    }
    else
    {
        // Up to Q word for 14-bit mode.
        ind_limit = WORD_Q0;
    }
    // Revert words validity to "before error-correction" state,
    // so damaged words that could be incorrectly "fixed" due to misassembling will be interpolated later.
    for(uint8_t index=WORD_L0;index<=ind_limit;index++)
    {
        // Revert word validity to "before error-correction" state.
        word_valid[index] = line_crc[index];
        line_crc[index] = cwd_fixed[index] = false;
    }
    audio_state = AUD_ORIG;
    cwd_applied = false;
#ifdef DB_EN_DBG_OUT
    QString log_line;
    log_line.sprintf("[DB] Marked INVALID [%03u/%03u...%03u/%03u]", start_frame, start_line, stop_frame, stop_line);
    qInfo()<<log_line;
#endif
}

//------------------------ Mark all words as bad.
void STC007DataBlock::markAsBroken()
{
    uint8_t ind_limit;
    // Determine word limit.
    if(resolution==RES_16BIT)
    {
        ind_limit = WORD_P0;
    }
    else
    {
        ind_limit = WORD_Q0;
    }
    // Mark all words.
    for(uint8_t index=WORD_L0;index<=ind_limit;index++)
    {
        word_valid[index] = line_crc[index] = cwd_fixed[index] = false;
    }
    audio_state = AUD_BROKEN;
    cwd_applied = false;
#ifdef DB_EN_DBG_OUT
    QString log_line;
    log_line.sprintf("[DB] Marked BROKEN [%03u/%03u...%03u/%03u]", start_frame, start_line, stop_frame, stop_line);
    qInfo()<<log_line;
#endif
}

//------------------------ Remove flag that word as fixed by Cross-Word Decoding.
void STC007DataBlock::clearWordFixedByCWD(uint8_t index)
{
    if(index<WORD_CNT)
    {
        cwd_fixed[index] = false;
    }
}

//------------------------ Remove flag that data block as fixed by Cross-Word Decoding.
void STC007DataBlock::clearFixedByCWD()
{
    cwd_applied = false;
}

//------------------------ Check if data block can be forced to check integrity via P and Q codes.
bool STC007DataBlock::canForceCheck()
{
    if(isDataBroken()==false)
    {
        if(resolution==RES_14BIT)
        {
            // 14-bit mode.
            if(getErrorsTotalCWD()<=1)
            {
                // If only one word is damaged, it can be any - P or Q will always be available.
                return true;
            }
        }
        else
        {
            // 16-bit mode.
            // Parity check can only be available if there are no errors.
            if(getErrorsTotalCWD()==0)
            {
                // No errors at all.
                return true;
            }
        }
    }
    // Forced ECC check can not be done by default.
    return false;
}

//------------------------ Check if line containing the word is marked after binarization with CRC as "not damaged".
bool STC007DataBlock::isWordLineCRCOk(uint8_t index)
{
    if(index<WORD_CNT)
    {
        return line_crc[index];
    }
    return false;
}

//------------------------ Check if the word is fixed with CWD after binarization.
bool STC007DataBlock::isWordCWDFixed(uint8_t index)
{
    if(index<WORD_CNT)
    {
        return cwd_fixed[index];
    }
    return false;
}

//------------------------ Check if word is safe to playback (not damaged or fixed by ECC).
bool STC007DataBlock::isWordValid(uint8_t index)
{
    if(index<WORD_CNT)
    {
        return word_valid[index];
    }
    return false;
}

//------------------------ Check if data block is valid.
bool STC007DataBlock::isBlockValid()
{
    if(getErrorsAudioFixed()>0)
    {
        return false;
    }
    return true;
}

//------------------------ Check if some audio words in data block were repaired using Cross-Word Decoding.
bool STC007DataBlock::isAudioAlteredByCWD()
{
    for(uint8_t index=WORD_L0;index<=WORD_R2;index++)
    {
        if(isWordCWDFixed(index)!=false)
        {
            return true;
        }
    }
    return false;
}

//------------------------ Check if some words in data block were repaired using Cross-Word Decoding.
bool STC007DataBlock::isDataAlteredByCWD()
{
    for(uint8_t index=WORD_L0;index<=WORD_Q0;index++)
    {
        if(isWordCWDFixed(index)!=false)
        {
            return true;
        }
    }
    return false;
}

//------------------------ Check if data block was repaired using Cross-Word Decoding.
bool STC007DataBlock::isDataFixedByCWD()
{
    if(cwd_applied!=false)
    {
        return isDataAlteredByCWD();
    }
    return false;
}

//------------------------ Check if data block was repaired by P-code.
bool STC007DataBlock::isDataFixedByP()
{
    if(audio_state==AUD_FIX_P)
    {
        return true;
    }
    return false;
}

//------------------------ Check if data block was repaired by Q-code.
bool STC007DataBlock::isDataFixedByQ()
{
    if(audio_state==AUD_FIX_Q)
    {
        return true;
    }
    return false;
}

//------------------------ Check if data block was repaired.
bool STC007DataBlock::isDataFixed()
{
    for(uint8_t index=WORD_L0;index<=WORD_Q0;index++)
    {
        if(line_crc[index]==false)
        {
            if(word_valid[index]!=false)
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------ Check if data block is broken (probably frame miss or bad frame stitching).
bool STC007DataBlock::isDataBroken()
{
    if(audio_state==AUD_BROKEN)
    {
        return true;
    }
    return false;
}

//------------------------ Check if data block contains 14-bit audio samples.
bool STC007DataBlock::isData14bit()
{
    if(resolution==RES_14BIT)
    {
        return true;
    }
    return false;
}

//------------------------ Check if data block contains 16-bit audio samples.
bool STC007DataBlock::isData16bit()
{
    if(resolution==RES_16BIT)
    {
        return true;
    }
    return false;
}

//------------------------ Is audio sample near zero value?
bool STC007DataBlock::isNearSilence(uint8_t index)
{
    int16_t audio_sample;
    audio_sample = getSample(index);
    if((resolution==RES_16BIT)||(m2_format!=false))
    {
        // Allow 2 LSBs wiggle room.
        if(audio_sample>=(int16_t)(1<<2))
        {
            return false;
        }
        if(audio_sample<(0-(int16_t)(1<<2)))
        {
            return false;
        }
    }
    else
    {
        // Allow 2 LSBs (2 LSBs in 14-bit word become 4 LSBs in 16-bit state) wiggle room.
        if(audio_sample>=(int16_t)(1<<4))
        {
            return false;
        }
        if(audio_sample<(0-(int16_t)(1<<4)))
        {
            return false;
        }
    }
    return true;
}

//------------------------ Are most audio samples are near "0"?
//------------------------ (for avoiding mis-calculation of resolution and ECC)
bool STC007DataBlock::isAlmostSilent()
{
    bool silent;
    silent = false;
    // Check if both channels are close to silence.
    if(((isNearSilence(WORD_L0)!=false)||(isNearSilence(WORD_L1)!=false)||(isNearSilence(WORD_L2)!=false))&&
        ((isNearSilence(WORD_R0)!=false)||(isNearSilence(WORD_R1)!=false)||(isNearSilence(WORD_R2)!=false)))
    {
        silent = true;
    }
    return silent;
}

//------------------------ Are all audio samples = 0?
//------------------------ (parity and ECC words do not count)
bool STC007DataBlock::isSilent()
{
    bool silent;
    silent = true;
    for(uint8_t index=WORD_L0;index<=WORD_R2;index++)
    {
        if(getSample(index)!=0)
        {
            silent = false;
            break;
        }
    }
    return silent;
}

//------------------------ Is block assembled on field seam?
bool STC007DataBlock::isOnSeam()
{
    if(getStartLine()>getStopLine())
    {
        return true;
    }
    return false;
}

//------------------------ Do samples in the block need de-emphasis for playback?
bool STC007DataBlock::hasEmphasis()
{
    return emphasis;
}

//------------------------ Get processed word.
uint16_t STC007DataBlock::getWord(uint8_t index)
{
    if(index<WORD_CNT)
    {
        return words[index];
    }
    return 0;
}

//------------------------ Get audio sample according to its resolution (convert to 16 bits if needed).
int16_t STC007DataBlock::getSample(uint8_t index)
{
    if(index<=WORD_R2)
    {
        if(m2_format==false)
        {
            // Normal sample format.
            if(resolution==RES_16BIT)
            {
                // 16-bit Sony PCM-F1 mode.
                return (int16_t)(words[index]);
            }
            else
            {
                // 14-bit STC-007 mode, converting to 16-bits.
                return (int16_t)(words[index]<<2);
            }
        }
        else
        {
            // M2 sample format.
            bool is_positive;
            uint16_t data_word;

            data_word = words[index];

            // Check 14th bit for range (R bit).
            if((data_word&STC007Line::BIT_M2_RANGE_POS)==0)
            {
                // Higher range.
                // Move 3 bits left (VALUE x8) to fill up 16-bit MSBs.
                data_word = (data_word<<3);
            }
            else
            {
                // Lower range.
                // Pick sign of the value.
                is_positive = ((data_word&STC007Line::BIT_M2_SIGN_POS)==0);
                // Remove range bit.
                data_word = data_word&(~STC007Line::BIT_M2_RANGE_POS);
                if(is_positive==false)
                {
                    // Fill all MSBs following negative sign.
                    data_word|=(1<<15)|(1<<14)|(1<<13);
                }
            }
            /*if((int16_t)data_word==-4096)
            {
                qDebug()<<data_word;
            }*/
            // Return the word converted in 16-bit sample.
            return (int16_t)data_word;
        }
    }
    return 0;
}

//------------------------ Get resolution of audio samples.
uint8_t STC007DataBlock::getResolution()
{
    return resolution;
}

//------------------------ Get what type of fix was applied to audio data.
uint8_t STC007DataBlock::getAudioState()
{
    return audio_state;
}

//------------------------ Get error count for audio samples (before corrections).
uint8_t STC007DataBlock::getErrorsAudioSource()
{
    uint8_t crc_errs;
    crc_errs = 0;
    // Search for audio samples with bad CRC.
    for(uint8_t index=WORD_L0;index<=WORD_R2;index++)
    {
        if(line_crc[index]==false)
        {
            crc_errs++;
        }
    }
    return crc_errs;
}

//------------------------ Get error count for audio samples (after CWD, before ECC).
uint8_t STC007DataBlock::getErrorsAudioCWD()
{
    uint8_t crc_errs;
    crc_errs = 0;
    // Search for audio samples with bad CRC.
    for(uint8_t index=WORD_L0;index<=WORD_R2;index++)
    {
        if((line_crc[index]==false)&&(cwd_fixed[index]==false))
        {
            crc_errs++;
        }
    }
    return crc_errs;
}

//------------------------ Get error count for audio samples (after corrections).
uint8_t STC007DataBlock::getErrorsAudioFixed()
{
    uint8_t crc_errs;
    crc_errs = 0;
    // Search for audio samples with bad CRC after correction.
    for(uint8_t index=WORD_L0;index<=WORD_R2;index++)
    {
        if(word_valid[index]==false)
        {
            crc_errs++;
        }
    }
    return crc_errs;
}

//------------------------ Get total error count for all words (before correction).
uint8_t STC007DataBlock::getErrorsTotalSource()
{
    uint8_t crc_errs;
    crc_errs = 0;

    // Determine word limit.
    uint8_t ind_limit;
    if(resolution==RES_16BIT)
    {
        ind_limit = WORD_P0;
    }
    else
    {
        ind_limit = WORD_Q0;
    }

    // Search for words with bad CRC.
    for(uint8_t index=WORD_L0;index<=ind_limit;index++)
    {
        if(line_crc[index]==false)
        {
            crc_errs++;
        }
    }
    return crc_errs;
}

//------------------------ Get total error count for all words (after CWD, before ECC).
uint8_t STC007DataBlock::getErrorsTotalCWD()
{
    uint8_t crc_errs;
    crc_errs = 0;

    // Determine word limit.
    uint8_t ind_limit;
    if(resolution==RES_16BIT)
    {
        ind_limit = WORD_P0;
    }
    else
    {
        ind_limit = WORD_Q0;
    }

    // Search for words with bad CRC.
    for(uint8_t index=WORD_L0;index<=ind_limit;index++)
    {
        if((line_crc[index]==false)&&(cwd_fixed[index]==false))
        {
            crc_errs++;
        }
    }
    return crc_errs;
}

//------------------------ Get total error count for all words (before correction).
uint8_t STC007DataBlock::getErrorsTotalFixed()
{
    uint8_t crc_errs;
    crc_errs = 0;

    // Determine word limit.
    uint8_t ind_limit;
    if(resolution==RES_16BIT)
    {
        ind_limit = WORD_P0;
    }
    else
    {
        ind_limit = WORD_Q0;
    }

    // Search for words with bad CRC.
    for(uint8_t index=WORD_L0;index<=ind_limit;index++)
    {
        if(word_valid[index]==false)
        {
            crc_errs++;
        }
    }
    return crc_errs;
}

//------------------------ Get number of source frame of the line with [WORD_L0] word.
uint32_t STC007DataBlock::getStartFrame()
{
    return w_frame[WORD_L0];
}

//------------------------ Get number of source frame of the line with [WORD_Q0] word.
uint32_t STC007DataBlock::getStopFrame()
{
    return w_frame[WORD_Q0];
}

//------------------------ Get number of source line with [WORD_L0] word.
uint16_t STC007DataBlock::getStartLine()
{
    return w_line[WORD_L0];
}

//------------------------ Get number of source line with [WORD_Q0] word.
uint16_t STC007DataBlock::getStopLine()
{
    return w_line[WORD_Q0];
}

//------------------------ Convert PCM data (14-bit) to string for debug.
std::string STC007DataBlock::dumpWordsString14bit()
{
    uint8_t zero_char, one_char, bit_pos;
    std::string text_out;

    for(uint8_t index=WORD_L0;index<=WORD_Q0;index++)
    {
        if(cwd_fixed[index]!=false)
        {
            text_out += DUMP_WBRL_CWD_FIX;
        }
        else if(line_crc[index]==false)
        {
            text_out += DUMP_WBRL_BAD;
        }
        else
        {
            text_out += DUMP_WBRL_OK;
        }

        if(word_valid[index]==false)
        {
            zero_char = DUMP_BIT_ZERO_BAD;
            one_char = DUMP_BIT_ONE_BAD;
        }
        else
        {
            zero_char = DUMP_BIT_ZERO;
            one_char = DUMP_BIT_ONE;
        }

        bit_pos = STC007Line::BITS_PER_WORD;
        do
        {
            bit_pos--;
            if((words[index]&(1<<bit_pos))==0)
            {
                text_out += zero_char;
            }
            else
            {
                text_out += one_char;
            }
        }
        while(bit_pos>0);

        if(cwd_fixed[index]!=false)
        {
            text_out += DUMP_WBRR_CWD_FIX;
        }
        else if(line_crc[index]==false)
        {
            text_out += DUMP_WBRR_BAD;
        }
        else
        {
            text_out += DUMP_WBRR_OK;
        }
        //text_out += ' ';
    }
    return text_out;
}

//------------------------ Convert PCM data (16-bit) to string for debug.
std::string STC007DataBlock::dumpWordsString16bit()
{
    uint8_t zero_char, one_char, bit_pos;
    std::string text_out;

    for(uint8_t index=WORD_L0;index<=WORD_P0;index++)
    {
        if(cwd_fixed[index]!=false)
        {
            text_out += DUMP_WBRL_CWD_FIX;
        }
        else if(line_crc[index]==false)
        {
            text_out += DUMP_WBRL_BAD;
        }
        else
        {
            text_out += DUMP_WBRL_OK;
        }

        if(word_valid[index]==false)
        {
            zero_char = DUMP_BIT_ZERO_BAD;
            one_char = DUMP_BIT_ONE_BAD;
        }
        else
        {
            zero_char = DUMP_BIT_ZERO;
            one_char = DUMP_BIT_ONE;
        }

        bit_pos = STC007Line::BITS_PER_F1_WORD;
        do
        {
            bit_pos--;
            if((words[index]&(1<<bit_pos))==0)
            {
                text_out += zero_char;
            }
            else
            {
                text_out += one_char;
            }
        }
        while(bit_pos>0);

        if(cwd_fixed[index]!=false)
        {
            text_out += DUMP_WBRR_CWD_FIX;
        }
        else if(line_crc[index]==false)
        {
            text_out += DUMP_WBRR_BAD;
        }
        else
        {
            text_out += DUMP_WBRR_OK;
        }
        //text_out += ' ';
    }
    return text_out;
}

//------------------------ Output full information about the data block.
std::string STC007DataBlock::dumpContentString()
{
    std::string text_out;
    char c_buf[32];

    text_out = "STC-007 block: ";
    sprintf(c_buf, "B[%03u-%03u] E[%03u-%03u] ", getStartFrame(), getStartLine(), getStopFrame(), getStopLine());
    text_out += c_buf;

    if(resolution==RES_16BIT)
    {
        text_out += "16-BIT "+dumpWordsString16bit();
    }
    else
    {
        text_out += "14-BIT "+dumpWordsString14bit();
    }

    sprintf(c_buf, " S[%05u] C[", sample_rate);
    text_out += c_buf;

    if(hasEmphasis()==false)
    {
        text_out += "-";
    }
    else
    {
        text_out += "D";
    }
    if(isOnSeam()==false)
    {
        text_out += "-";
    }
    else
    {
        text_out += "O";
    }
    text_out += "] ";

    sprintf(c_buf, "EA[%01u|%01u|%01u] ET[%01u|%01u|%01u] T[%04u] ",
            getErrorsAudioSource(), getErrorsAudioCWD(), getErrorsAudioFixed(),
            getErrorsTotalSource(), getErrorsTotalCWD(), getErrorsTotalFixed(),
            process_time);
    text_out += c_buf;

    if(isSilent()!=false)
    {
        text_out += "FS ";
    }
    else if(isAlmostSilent()!=false)
    {
        text_out += "AS ";
    }
    else
    {
        text_out += "NS ";
    }


    if(canForceCheck()==false)
    {
        sprintf(c_buf, "UNCH  ");
    }
    else
    {
        sprintf(c_buf, "CHECK ");
    }
    text_out += c_buf;

    if(isBlockValid()==false)
    {
        sprintf(c_buf, "INVAL  ");
    }
    else
    {
        sprintf(c_buf, "VALID  ");
    }
    text_out += c_buf;

    if(audio_state==AUD_ORIG)
    {
        if(isDataFixedByCWD()!=false)
        {
            sprintf(c_buf, "O_CWD");
        }
        else if(isDataFixed()!=false)
        {
            sprintf(c_buf, "FIXED");
        }
        else
        {
            sprintf(c_buf, "ORIG ");
        }
    }
    else if(audio_state==AUD_FIX_P)
    {
        if(isDataFixedByCWD()==false)
        {
            sprintf(c_buf, "P_FIX");
        }
        else
        {
            sprintf(c_buf, "P_CWD");
        }
    }
    else if(audio_state==AUD_FIX_Q)
    {
        if(isDataFixedByCWD()==false)
        {
            sprintf(c_buf, "Q_FIX");
        }
        else
        {
            sprintf(c_buf, "Q_CWD");
        }
    }
    else
    {
        sprintf(c_buf, "BROKE");
    }
    text_out += c_buf;

    return text_out;
}
