#include "pcm16x0datablock.h"

PCM16X0DataBlock::PCM16X0DataBlock()
{
    this->clear();
}

PCM16X0DataBlock::PCM16X0DataBlock(const PCM16X0DataBlock &in_object)
{
    frame_number = in_object.frame_number;
    start_line = in_object.start_line;
    stop_line = in_object.stop_line;
    start_part = in_object.start_part;
    stop_part = in_object.stop_part;
    queue_order = in_object.queue_order;
    sample_rate = in_object.sample_rate;
    emphasis = in_object.emphasis;
    ei_format = in_object.ei_format;
    code = in_object.code;
    process_time = in_object.process_time;
    for(uint8_t block=0;block<SUBBLK_CNT;block++)
    {
        audio_state[block] = in_object.audio_state[block];
        for(uint8_t index=0;index<LINE_CNT;index++)
        {
            words[block][index] = in_object.words[block][index];
            word_crc[block][index] = in_object.word_crc[block][index];
            word_valid[block][index] = in_object.word_valid[block][index];
        }
    }
    for(uint8_t index=0;index<LINE_CNT;index++)
    {
        picked_left[index] = in_object.picked_left[index];
        picked_crc[index] = in_object.picked_crc[index];
    }
    order_even = in_object.order_even;
}

PCM16X0DataBlock& PCM16X0DataBlock::operator= (const PCM16X0DataBlock &in_object)
{
    if(this==&in_object) return *this;

    frame_number = in_object.frame_number;
    start_line = in_object.start_line;
    stop_line = in_object.stop_line;
    start_part = in_object.start_part;
    stop_part = in_object.stop_part;
    queue_order = in_object.queue_order;
    sample_rate = in_object.sample_rate;
    emphasis = in_object.emphasis;
    ei_format = in_object.ei_format;
    code = in_object.code;
    process_time = in_object.process_time;
    for(uint8_t block=0;block<SUBBLK_CNT;block++)
    {
        audio_state[block] = in_object.audio_state[block];
        for(uint8_t index=0;index<LINE_CNT;index++)
        {
            words[block][index] = in_object.words[block][index];
            word_crc[block][index] = in_object.word_crc[block][index];
            word_valid[block][index] = in_object.word_valid[block][index];
        }
    }
    for(uint8_t index=0;index<LINE_CNT;index++)
    {
        picked_left[index] = in_object.picked_left[index];
        picked_crc[index] = in_object.picked_crc[index];
    }
    order_even = in_object.order_even;

    return *this;
}

//------------------------ Reset object.
void PCM16X0DataBlock::clear()
{
    frame_number = 0;
    start_line = stop_line = 0;
    start_part = stop_part = PCM16X0SubLine::PART_LEFT;
    queue_order = 0;
    sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
    emphasis = false;
    ei_format = false;
    code = false;
    process_time = 0;
    for(uint8_t block=0;block<SUBBLK_CNT;block++)
    {
        audio_state[block] = AUD_ORIG;
        for(uint8_t index=0;index<LINE_CNT;index++)
        {
            words[block][index] = 0;
            word_crc[block][index] = false;
            word_valid[block][index] = false;
        }
    }
    for(uint8_t index=0;index<LINE_CNT;index++)
    {
        picked_left[index] = false;
        picked_crc[index] = false;
    }
    order_even = false;
}

//------------------------ Copy word into object with its state.
void PCM16X0DataBlock::setWord(uint8_t blk, uint8_t line, uint16_t in_word, bool in_valid, bool in_picked_left, bool in_picked_crc)
{
    if((blk<SUBBLK_CNT)&&(line<LINE_CNT))
    {
        words[blk][line] = in_word;
        word_crc[blk][line] = in_valid;
        word_valid[blk][line] = in_valid;
        if(blk==SUBBLK_1)
        {
            // Picked bits in the samples can only be in [SUBBLK_1].
            picked_left[line] = in_picked_left;
            // Check CRC pick status only on left sub-block, it should be the same for all sub-blocks.
            picked_crc[line] = in_picked_crc;
        }
    }
}

//------------------------ Set this block odd in the order.
void PCM16X0DataBlock::setOrderOdd()
{
    order_even = false;
}

//------------------------ Set this block even in the order.
void PCM16X0DataBlock::setOrderEven()
{
    order_even = true;
}

//------------------------ Set state of audio data in the data block.
void PCM16X0DataBlock::setAudioState(uint8_t blk, uint8_t in_state)
{
    if((blk<SUBBLK_CNT)&&(in_state<AUD_MAX))
    {
        audio_state[blk] = in_state;
    }
}

//------------------------ Set emphasis state of contained samples.
void PCM16X0DataBlock::setEmphasis(bool in_set)
{
    emphasis = in_set;
}

//------------------------ Set word as fixed by error correction.
void PCM16X0DataBlock::fixWord(uint8_t blk, uint8_t word, uint16_t in_word)
{
    if((blk<SUBBLK_CNT)&&(word<WORD_CNT))
    {
        word = getWordToLine(blk, word);
        words[blk][word] = in_word;
        word_valid[blk][word] = true;
    }
}

//------------------------ Mark data words as "original state".
void PCM16X0DataBlock::markAsOriginalData(uint8_t blk)
{
    if(blk<SUBBLK_CNT)
    {
        audio_state[blk] = AUD_ORIG;
    }
    else
    {
        for(uint8_t i=0;i<SUBBLK_CNT;i++)
        {
            audio_state[i] = AUD_ORIG;
        }
    }
}

//------------------------ Mark data block as fixed by P-code.
void PCM16X0DataBlock::markAsFixedByP(uint8_t blk)
{
    if(blk<SUBBLK_CNT)
    {
        audio_state[blk] = AUD_FIX_P;
    }
}

//------------------------ Mark fixed words as bad again to prevent clicks on possible incorrect "corrections".
void PCM16X0DataBlock::markAsUnsafe()
{
    bool full_bad;
    //qInfo()<<QString::fromStdString("[BEFORE] "+dumpContentString());
    for(uint8_t i=0;i<SUBBLK_CNT;i++)
    {
        full_bad = false;
        if((word_crc[i][LINE_2]==false)&&(getErrorsAudio(i)>0))
        {
            // Sub-block has some errors marked with CRC, but error-correction word is bad,
            // so data block was not fixed or checked for possible BROKEN state, thus to prevent future use of not-damaged words in the block
            // due to its probable misassembling, invalidate all words.
            full_bad = true;
        }

        // Check if data block is not set as "BROKEN" already.
        if(audio_state[i]!=AUD_BROKEN)
        {
            //word_crc[i][LINE_1] = false;
            //word_crc[i][LINE_3] = false;
            if(full_bad==false)
            {
                // Revert word validity to "before error-correction" state,
                word_valid[i][LINE_1] = word_crc[i][LINE_1];
                word_valid[i][LINE_3] = word_crc[i][LINE_3];
            }
            else
            {
                // Shut down the whole data block.
                word_valid[i][LINE_1] = false;
                word_valid[i][LINE_3] = false;
            }
            audio_state[i] = AUD_ORIG;
#ifdef DB_EN_DBG_OUT
            QString log_line;
            log_line.sprintf("[DB] Marked INVALID [%03u:%03u-%01u...%03u-%01u:%01u]",
                             frame_number, start_line, start_part, stop_line, stop_part, i);
            qInfo()<<log_line;
#endif
        }
    }
    //qInfo()<<QString::fromStdString("[AFTER]  "+dumpContentString());
}

//------------------------ Mark all words as bad.
void PCM16X0DataBlock::markAsBroken(uint8_t blk)
{
    if(blk<SUBBLK_CNT)
    {
        // Mark all words.
        for(uint8_t index=LINE_1;index<=LINE_3;index++)
        {
            word_valid[blk][index] = false;
            word_crc[blk][index] = false;
        }
        audio_state[blk] = AUD_BROKEN;
#ifdef DB_EN_DBG_OUT
        QString log_line;
        log_line.sprintf("[DB] Marked BROKEN [%03u:%03u-%01u...%03u-%01u:%01u]",
                         frame_number, start_line, start_part, stop_line, stop_part, blk);
        qInfo()<<log_line;
#endif
    }
    else
    {
        for(uint8_t i=0;i<SUBBLK_CNT;i++)
        {
            // Mark all words.
            for(uint8_t index=LINE_1;index<=LINE_3;index++)
            {
                word_valid[i][index] = false;
                word_crc[i][index] = false;
            }
            audio_state[i] = AUD_BROKEN;
        }
#ifdef DB_EN_DBG_OUT
        QString log_line;
        log_line.sprintf("[DB] Marked BROKEN [%03u:%03u-%01u...%03u-%01u]",
                         frame_number, start_line, start_part, stop_line, stop_part);
        qInfo()<<log_line;
#endif
    }
}

//------------------------ Mark word as bad.
void PCM16X0DataBlock::markAsBad(uint8_t blk, uint8_t line)
{
    if((blk<SUBBLK_CNT)&&(line<LINE_CNT))
    {
        word_crc[blk][line] = false;
        word_valid[blk][line] = false;
        picked_left[line] = false;
    }
}

//------------------------ Check if data block can be forced to check integrity via P codes.
bool PCM16X0DataBlock::canForceCheck()
{
    bool check_available;
    // Forced ECC check can not be done by default.
    check_available = false;
    // Check if data is BROKEN.
    if(isDataBroken()==false)
    {
        // Check number of errors in the data block.
        if(getErrorsTotal()==0)
        {
            // No errors at all.
            check_available = true;
        }
    }
    return check_available;
}

//------------------------ Has left sample picked (during binarization) bits?
bool PCM16X0DataBlock::hasPickedLeft(uint8_t line)
{
    if(line<LINE_CNT)
    {
        return picked_left[line];
    }
    return false;
}

//------------------------ Has CRC picked (during binarization) bits?
bool PCM16X0DataBlock::hasPickedCRC(uint8_t line)
{
    if(line<LINE_CNT)
    {
        return picked_crc[line];
    }
    return false;
}

//------------------------ Does the word in the sub-block contained picked bits?
bool PCM16X0DataBlock::hasPickedWord(uint8_t blk, uint8_t word)
{
    if(word<WORD_CNT)
    {
        // Convert word index into line index.
        word = getWordToLine(blk, word);
        // Check words in the sub-block.
        if(blk==SUBBLK_1)
        {
            // Only left sub-block can have picked bits in the samples.
            if((hasPickedLeft(word)!=false)||(hasPickedCRC(word)!=false))
            {
                return true;
            }
        }
        else
        {
            if(hasPickedCRC(word)!=false)
            {
                return true;
            }
        }
    }
    else
    {
        // Default path: check the whole sub-block.
        if(blk==SUBBLK_1)
        {
            // Only left sub-block can have picked bits in the samples.
            if((hasPickedLeft(LINE_1)!=false)||(hasPickedCRC(LINE_1)!=false)
              ||(hasPickedLeft(LINE_2)!=false)||(hasPickedCRC(LINE_2)!=false)
              ||(hasPickedLeft(LINE_3)!=false)||(hasPickedCRC(LINE_3)!=false))
            {
                return true;
            }
        }
        else
        {
            if((hasPickedCRC(LINE_1)!=false)||(hasPickedCRC(LINE_3)!=false))
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------ Does the sample in the sub-block contained picked bits?
bool PCM16X0DataBlock::hasPickedSample(uint8_t blk, uint8_t word)
{
    if(word<WORD_P)
    {
        // Convert word index into line index.
        word = getWordToLine(blk, word);
        if(blk==SUBBLK_1)
        {
            if(hasPickedLeft(word)!=false)
            {
                return true;
            }
        }
    }
    else
    {
        // Default path: check the whole sub-block.
        if(blk==SUBBLK_1)
        {
            if((hasPickedLeft(LINE_1)!=false)||(hasPickedLeft(LINE_3)!=false))
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------ Has this sub-block picked parity?
bool PCM16X0DataBlock::hasPickedParity(uint8_t blk)
{
    if(blk<SUBBLK_CNT)
    {
        if(blk==SUBBLK_1)
        {
            if(hasPickedLeft(LINE_2)!=false)
            {
                return true;
            }
        }
        if(hasPickedCRC(LINE_2)!=false)
        {
            return true;
        }
    }
    return false;
}


//------------------------ Count number of audio samples in [SUBBLK_1] with picked bits.
uint8_t PCM16X0DataBlock::getPickedAudioSamples(uint8_t blk)
{
    uint8_t picked_samples;
    picked_samples = 0;
    if(blk==SUBBLK_1)
    {
        // Samples with picked bits can only be in left sub-block.
        if(hasPickedSample(SUBBLK_1, WORD_L)!=false)
        {
            picked_samples++;
        }
        if(hasPickedSample(SUBBLK_1, WORD_R)!=false)
        {
            picked_samples++;
        }
    }
    return picked_samples;
}

//------------------------ Check if word is marked with CRC as "not damaged".
bool PCM16X0DataBlock::isWordCRCOk(uint8_t blk, uint8_t word)
{
    if((blk<SUBBLK_CNT)&&(word<WORD_CNT))
    {
        return word_crc[blk][getWordToLine(blk, word)];
    }
    return false;
}

//------------------------ Check if word is safe to playback (not damaged or fixed).
bool PCM16X0DataBlock::isWordValid(uint8_t blk, uint8_t word)
{
    if((blk<SUBBLK_CNT)&&(word<WORD_CNT))
    {
        return word_valid[blk][getWordToLine(blk, word)];
    }
    return false;
}

//------------------------ Is this block odd in the order?
bool PCM16X0DataBlock::isOrderOdd()
{
    return !order_even;
}

//------------------------ Is this block even in the order?
bool PCM16X0DataBlock::isOrderEven()
{
    return order_even;
}

//------------------------ Check if data block is valid.
bool PCM16X0DataBlock::isBlockValid(uint8_t blk)
{
    bool valid;
    valid = true;
    if(getErrorsFixedAudio(blk)>0)
    {
        valid = false;
    }
    return valid;
}

//------------------------ Check if data block was repaired by P-code.
bool PCM16X0DataBlock::isDataFixedByP(uint8_t blk)
{
    bool data_fixed;
    data_fixed = false;
    if(blk<SUBBLK_CNT)
    {
        if(audio_state[blk]==AUD_FIX_P)
        {
            data_fixed = true;
        }
    }
    else
    {
        for(uint8_t i=0;i<SUBBLK_CNT;i++)
        {
            data_fixed = (data_fixed||(audio_state[i]==AUD_FIX_P));
        }
    }
    return data_fixed;
}

//------------------------ Check if data block was repaired.
bool PCM16X0DataBlock::isDataFixed(uint8_t blk)
{
    return isDataFixedByP(blk);
}

//------------------------ Check if data block is broken.
bool PCM16X0DataBlock::isDataBroken(uint8_t blk)
{
    bool data_broken;
    data_broken = false;
    if(blk<SUBBLK_CNT)
    {
        if(audio_state[blk]==AUD_BROKEN)
        {
            data_broken = true;
        }
    }
    else
    {
        for(uint8_t i=0;i<SUBBLK_CNT;i++)
        {
            data_broken = (data_broken||(audio_state[i]==AUD_BROKEN));
        }
    }
    return data_broken;
}

//------------------------ Is audio sample near zero value?
bool PCM16X0DataBlock::isNearSilence(uint8_t blk, uint8_t word)
{
    int16_t audio_sample;
    audio_sample = getSample(blk, word);
    // Allow 2 LSBs wiggle room.
    if(audio_sample>=(int16_t)(1<<2))
    {
        return false;
    }
    if(audio_sample<(0-(int16_t)(1<<2)))
    {
        return false;
    }
    return true;
}

//------------------------ Are most audio samples are near "0"?
bool PCM16X0DataBlock::isAlmostSilent()
{
    bool silent;
    silent = false;
    // Check if both channels are close to silence.
    if(((isNearSilence(SUBBLK_1, WORD_L)!=false)&&(isNearSilence(SUBBLK_1, WORD_R)!=false))
        ||((isNearSilence(SUBBLK_2, WORD_L)!=false)&&(isNearSilence(SUBBLK_2, WORD_R)!=false))
        ||((isNearSilence(SUBBLK_3, WORD_L)!=false)&&(isNearSilence(SUBBLK_3, WORD_R)!=false)))
    {
        silent = true;
    }
    return silent;
}

//------------------------ Are all audio samples = 0?
//------------------------ (parity and ECC words do not count)
bool PCM16X0DataBlock::isSilent()
{
    bool silent;
    silent = false;
    if((getSample(SUBBLK_1, WORD_L)==0)&&(getSample(SUBBLK_1, WORD_R)==0)
        &&(getSample(SUBBLK_2, WORD_L)==0)&&(getSample(SUBBLK_2, WORD_R)==0)
        &&(getSample(SUBBLK_3, WORD_L)==0)&&(getSample(SUBBLK_3, WORD_R)==0))
    {
        silent = true;
    }
    return silent;
}

//------------------------ Is block in EI format (PCM-1630) instead of SI format?
bool PCM16X0DataBlock::isInEIFormat()
{
    return ei_format;
}

//------------------------ Do samples in the block need de-emphasis for playback?
bool PCM16X0DataBlock::hasEmphasis()
{
    return emphasis;
}

//------------------------ Does block contain CODE instead of AUDIO?
bool PCM16X0DataBlock::hasCode()
{
    return code;
}

//------------------------ Get processed word.
uint16_t PCM16X0DataBlock::getWord(uint8_t blk, uint8_t word)
{
    if((blk<SUBBLK_CNT)&&(word<WORD_CNT))
    {
        return words[blk][getWordToLine(blk, word)];
    }
    return 0;
}

//------------------------ Get audio sample.
int16_t PCM16X0DataBlock::getSample(uint8_t blk, uint8_t word)
{
    if((blk<SUBBLK_CNT)&&((word==WORD_L)||(word==WORD_R)))
    {
        return (int16_t)(words[blk][getWordToLine(blk, word)]);
    }
    return 0;
}

//------------------------ Get what type of fix was applied to audio data.
uint8_t PCM16X0DataBlock::getAudioState(uint8_t blk)
{
    if(blk<SUBBLK_CNT)
    {
        return audio_state[blk];
    }
    return AUD_BROKEN;
}

//------------------------ Get error count for audio samples (before correction).
uint8_t PCM16X0DataBlock::getErrorsAudio(uint8_t blk)
{
    uint8_t crc_errs;
    crc_errs = 0;
    // Search for audio samples with bad CRC.
    if(blk<SUBBLK_CNT)
    {
        // Count for selected sub-block.
        if(word_crc[blk][LINE_1]==false)
        {
            crc_errs++;
        }
        if(word_crc[blk][LINE_3]==false)
        {
            crc_errs++;
        }
    }
    else
    {
        // Count for all sub-blocks.
        for(uint8_t index=SUBBLK_1;index<=SUBBLK_3;index++)
        {
            if(word_crc[index][LINE_1]==false)
            {
                crc_errs++;
            }
            if(word_crc[index][LINE_3]==false)
            {
                crc_errs++;
            }
        }
    }
    return crc_errs;
}

//------------------------ Get error count for audio samples (after correction).
uint8_t PCM16X0DataBlock::getErrorsFixedAudio(uint8_t blk)
{
    uint8_t crc_errs;
    crc_errs = 0;
    // Search for audio samples with bad CRC after correction.
    if(blk<SUBBLK_CNT)
    {
        // Count for selected sub-block.
        if(word_valid[blk][LINE_1]==false)
        {
            crc_errs++;
        }
        if(word_valid[blk][LINE_3]==false)
        {
            crc_errs++;
        }
    }
    else
    {
        // Count for all sub-blocks.
        for(uint8_t index=SUBBLK_1;index<=SUBBLK_3;index++)
        {
            if(word_valid[index][LINE_1]==false)
            {
                crc_errs++;
            }
            if(word_valid[index][LINE_3]==false)
            {
                crc_errs++;
            }
        }
    }
    return crc_errs;
}

//------------------------ Get total error count for all words (before correction).
uint8_t PCM16X0DataBlock::getErrorsTotal(uint8_t blk)
{
    uint8_t crc_errs;
    crc_errs = 0;
    // Search for words with bad CRC.
    if(blk<SUBBLK_CNT)
    {
        // Count for selected sub-block.
        for(uint8_t line=LINE_1;line<=LINE_3;line++)
        {
            if(word_crc[blk][line]==false)
            {
                crc_errs++;
            }
        }
    }
    else
    {
        // Count for all sub-blocks.
        for(uint8_t index=SUBBLK_1;index<=SUBBLK_3;index++)
        {
            for(uint8_t line=LINE_1;line<=LINE_3;line++)
            {
                if(word_crc[index][line]==false)
                {
                    crc_errs++;
                }
            }
        }
    }
    return crc_errs;
}

//------------------------ Convert PCM data (16-bit) to string for debug.
std::string PCM16X0DataBlock::dumpWordsString()
{
    uint8_t zero_char, one_char, bit_pos;
    std::string text_out;

    // Cycle through sub-blocks.
    for(uint8_t blk=SUBBLK_1;blk<=SUBBLK_3;blk++)
    {
        // Cycle through words in the sub-block.
        for(uint8_t word=0;word<WORD_CNT;word++)
        {
            // Determine opening bracket appearance according to source CRC.
            if(word_crc[blk][getWordToLine(blk, word)]==false)
            {
                text_out += DUMP_WBRL_BAD;
            }
            else
            {
                text_out += DUMP_WBRL_OK;
            }

            // Determine bit chars appearance according to word validity.
            if(word_valid[blk][getWordToLine(blk, word)]==false)
            {
                zero_char = DUMP_BIT_ZERO_BAD;
                one_char = DUMP_BIT_ONE_BAD;
            }
            else
            {
                zero_char = DUMP_BIT_ZERO;
                one_char = DUMP_BIT_ONE;
            }

            // Cycle through bits of the word.
            bit_pos = PCM16X0SubLine::BITS_PER_WORD;
            do
            {
                bit_pos--;
                if((words[blk][getWordToLine(blk, word)]&(1<<bit_pos))==0)
                {
                    text_out += zero_char;
                }
                else
                {
                    text_out += one_char;
                }
            }
            while(bit_pos>0);

            // Determine closing bracket appearance according to source CRC.
            if(word_crc[blk][getWordToLine(blk, word)]==false)
            {
                text_out += DUMP_WBRR_BAD;
            }
            else
            {
                text_out += DUMP_WBRR_OK;
            }

            // Parity word is the last in the structure.
            if(word==WORD_P)
            {
                // Check state of the audio in the sub-block.
                if(audio_state[blk]==AUD_ORIG)
                {
                    text_out += "[ORIG] ";
                }
                else if(audio_state[blk]==AUD_FIX_P)
                {
                    text_out += "[P_FIX]";
                }
                else if(audio_state[blk]==AUD_BROKEN)
                {
                    text_out += "[BROKE]";
                }
                else
                {
                    text_out += "[?????]";
                }
            }
        }
        text_out += ' ';
    }
    return text_out;
}

//------------------------ Output full information about the data block.
std::string PCM16X0DataBlock::dumpContentString()
{
    std::string text_out;
    char c_buf[32];

    text_out = "PCM-16x0 block: ";
    sprintf(c_buf, "B[%03u-%03u-%01u] E[%03u-%03u-%01u] Q[%03u-",
            frame_number, start_line, start_part, frame_number, stop_line, stop_part, queue_order);
    text_out += c_buf;

    if(isOrderEven()==false)
    {
        text_out += "O] ";
    }
    else
    {
        text_out += "E] ";
    }

    text_out += "16-BIT "+dumpWordsString();

    sprintf(c_buf, "S[%05u] C[", sample_rate);
    text_out += c_buf;

    if(isInEIFormat()==false)
    {
        text_out += "S";
    }
    else
    {
        text_out += "E";
    }
    if(hasEmphasis()==false)
    {
        text_out += "-";
    }
    else
    {
        text_out += "D";
    }
    if(hasCode()==false)
    {
        text_out += "-";
    }
    else
    {
        text_out += "C";
    }
    text_out += "] ";

    sprintf(c_buf, "EA[%01u] ET[%01u] T[%04u] ", getErrorsAudio(), getErrorsTotal(), process_time);
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

    return text_out;
}

//------------------------ Output service bits information from the data block.
std::string PCM16X0DataBlock::dumpServiceBitsString()
{
    std::string text_out;
    char c_buf[32];

    text_out = "PCM-16x0 service bits: ";
    if(hasEmphasis()==false)
    {
        text_out += "[EPMHASIS: OFF]";
    }
    else
    {
        text_out += " [EPMHASIS: ON]";
    }
    sprintf(c_buf, " [%05u Hz]", sample_rate);
    text_out += c_buf;
    if(isInEIFormat()==false)
    {
        text_out += " [MODE: SI]";
    }
    else
    {
        text_out += " [MODE: EI]";
    }
    if(hasCode()==false)
    {
        text_out += " [AUDIO]";
    }
    else
    {
        text_out += " [CODE] ";
    }
    return text_out;
}

//------------------------ Translate word coordinate into line coordinate.
uint8_t PCM16X0DataBlock::getWordToLine(uint8_t blk, uint8_t word)
{
    uint8_t line;
    if(order_even==false)
    {
        // Odd order, first block in the interleave block and every other (3, 5, 7...).
        if(blk==SUBBLK_1)
        {
            // Sub-block 1 (left).
            if(word==WORD_L)
            {
                // L1
                line = LINE_3;
            }
            else if(word==WORD_R)
            {
                // R1
                line = LINE_1;
            }
            else
            {
                // P1
                line = LINE_2;
            }
        }
        else if(blk==SUBBLK_2)
        {
            // Sub-block 2 (middle).
            if(word==WORD_L)
            {
                // L2
                line = LINE_1;
            }
            else if(word==WORD_R)
            {
                // R2
                line = LINE_3;
            }
            else
            {
                // P2
                line = LINE_2;
            }
        }
        else
        {
            // Sub-block 3 (right).
            if(word==WORD_L)
            {
                // L3
                line = LINE_3;
            }
            else if(word==WORD_R)
            {
                // R3
                line = LINE_1;
            }
            else
            {
                // P3
                line = LINE_2;
            }
        }
    }
    else
    {
        // Even order, second block in the interleave block and every other (4, 6, 8...).
        if(blk==SUBBLK_1)
        {
            // Sub-block 1 (left).
            if(word==WORD_L)
            {
                // L1
                line = LINE_1;
            }
            else if(word==WORD_R)
            {
                // R1
                line = LINE_3;
            }
            else
            {
                // P1
                line = LINE_2;
            }
        }
        else if(blk==SUBBLK_2)
        {
            // Sub-block 2 (middle).
            if(word==WORD_L)
            {
                // L2
                line = LINE_3;
            }
            else if(word==WORD_R)
            {
                // R2
                line = LINE_1;
            }
            else
            {
                // P2
                line = LINE_2;
            }
        }
        else
        {
            // Sub-block 3 (right).
            if(word==WORD_L)
            {
                // L3
                line = LINE_1;
            }
            else if(word==WORD_R)
            {
                // R3
                line = LINE_3;
            }
            else
            {
                // P3
                line = LINE_2;
            }
        }
    }
    return line;
}

//------------------------ Translate word coordinate into line coordinate.
uint8_t PCM16X0DataBlock::getLineToWord(uint8_t blk, uint8_t line)
{
    uint8_t word;
    if(order_even==false)
    {
        // Odd order, first block in the interleave block and every other (3, 5, 7...).
        if(blk==SUBBLK_1)
        {
            // Sub-block 1 (left).
            if(line==LINE_1)
            {
                word = WORD_R;
            }
            else if(line==LINE_3)
            {
                word = WORD_L;
            }
            else
            {
                word = WORD_P;
            }
        }
        else if(blk==SUBBLK_2)
        {
            // Sub-block 2 (middle).
            if(line==LINE_1)
            {
                word = WORD_L;
            }
            else if(line==LINE_3)
            {
                word = WORD_R;
            }
            else
            {
                word = WORD_P;
            }
        }
        else
        {
            // Sub-block 3 (right).
            if(line==LINE_1)
            {
                word = WORD_R;
            }
            else if(line==LINE_3)
            {
                word = WORD_L;
            }
            else
            {
                word = WORD_P;
            }
        }
    }
    else
    {
        // Even order, second block in the interleave block and every other (4, 6, 8...).
        if(blk==SUBBLK_1)
        {
            // Sub-block 1 (left).
            if(line==LINE_1)
            {
                word = WORD_L;
            }
            else if(line==LINE_3)
            {
                word = WORD_R;
            }
            else
            {
                word = WORD_P;
            }
        }
        else if(blk==SUBBLK_2)
        {
            // Sub-block 2 (middle).
            if(line==LINE_1)
            {
                word = WORD_R;
            }
            else if(line==LINE_3)
            {
                word = WORD_L;
            }
            else
            {
                word = WORD_P;
            }
        }
        else
        {
            // Sub-block 3 (right).
            if(line==LINE_1)
            {
                word = WORD_L;
            }
            else if(line==LINE_3)
            {
                word = WORD_R;
            }
            else
            {
                word = WORD_P;
            }
        }
    }
    return word;
}
