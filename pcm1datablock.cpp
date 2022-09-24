#include "pcm1datablock.h"

PCM1DataBlock::PCM1DataBlock()
{
    clear();
}

PCM1DataBlock::PCM1DataBlock(const PCM1DataBlock &in_object)
{
    frame_number = in_object.frame_number;
    start_line = in_object.start_line;
    stop_line = in_object.stop_line;
    interleave_num = in_object.interleave_num;
    sample_rate = in_object.sample_rate;
    short_blk = in_object.short_blk;
    emphasis = in_object.emphasis;
    process_time = in_object.process_time;
    for(uint8_t i=0;i<WORD_CNT;i++)
    {
        words[i] = in_object.words[i];
        word_crc[i] = in_object.word_crc[i];
        picked_left[i] = in_object.picked_left[i];
        picked_crc[i] = in_object.picked_crc[i];
    }
}

PCM1DataBlock& PCM1DataBlock::operator= (const PCM1DataBlock &in_object)
{
    if(this==&in_object) return *this;

    frame_number = in_object.frame_number;
    start_line = in_object.start_line;
    stop_line = in_object.stop_line;
    interleave_num = in_object.interleave_num;
    sample_rate = in_object.sample_rate;
    short_blk = in_object.short_blk;
    emphasis = in_object.emphasis;
    process_time = in_object.process_time;
    for(uint8_t i=0;i<WORD_CNT;i++)
    {
        words[i] = in_object.words[i];
        word_crc[i] = in_object.word_crc[i];
        picked_left[i] = in_object.picked_left[i];
        picked_crc[i] = in_object.picked_crc[i];
    }

    return *this;
}

//------------------------ Reset object.
void PCM1DataBlock::clear()
{
    frame_number = start_line = stop_line = 0;
    interleave_num = 0;
    sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
    short_blk = emphasis = false;
    process_time = 0;
    for(uint8_t i=0;i<WORD_CNT;i++)
    {
        words[i] = 0;
        words[i] |= PCM1Line::BIT_RANGE_POS;
        word_crc[i] = false;
        picked_left[i] = picked_crc[i] = 0;
    }
}

//------------------------ Copy word into object with its state.
void PCM1DataBlock::setWord(uint8_t index, uint16_t in_word, bool in_valid, bool in_picked_left, bool in_picked_crc)
{
    if(index<getWordCount())
    {
        words[index] = in_word;
        word_crc[index] = in_valid;
        picked_left[index] = in_picked_left;
        picked_crc[index] = in_picked_crc;
    }
}

//------------------------ Set length of the block to normal (184 words).
void PCM1DataBlock::setNormalLength()
{
    short_blk = false;
}

//------------------------ Set length of the block to short (182 words).
void PCM1DataBlock::setShortLength()
{
    short_blk = true;
    // Clear unused words.
    for(uint8_t i=(WORD_CNT_SHORT-1);i<=(WORD_CNT-1);i++)
    {
        words[i] = 0;
        words[i] |= PCM1Line::BIT_RANGE_POS;
        word_crc[i] = false;
        picked_left[i] = picked_crc[i] = 0;
    }
}

//------------------------ Set emphasis state of contained samples.
void PCM1DataBlock::setEmphasis(bool in_set)
{
    emphasis = in_set;
}

//------------------------ Check if data block can be forced to check integrity via ECC codes.
bool PCM1DataBlock::canForceCheck()
{
    // No ECC capabilities in PCM-1.
    return false;
}

//------------------------ Does the word in the data block contained picked bits?
bool PCM1DataBlock::hasPickedWord(uint8_t word)
{
    if(word<getWordCount())
    {
        // Check for picked bits at the left of the sample and for picked CRC on the source line.
        if((picked_left[word]!=false)||(picked_crc[word]!=false))
        {
            return true;
        }
    }
    else
    {
        // Default path: check the whole data block.
        for(word=0;word<WORD_CNT;word++)
        {
            if((picked_left[word]!=false)||(picked_crc[word]!=false))
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------ Does the sample in the data block contained picked bits?
bool PCM1DataBlock::hasPickedSample(uint8_t word)
{
    if(word<getWordCount())
    {
        // Check for picked bits at the left of the sample and for picked CRC on the source line.
        if(picked_left[word]!=false)
        {
            return true;
        }
    }
    else
    {
        // Default path: check the whole data block.
        for(word=0;word<WORD_CNT;word++)
        {
            if(picked_left[word]!=false)
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------ Check if word is marked with CRC as "not damaged".
bool PCM1DataBlock::isWordCRCOk(uint8_t index)
{
    if(index<getWordCount())
    {
        return word_crc[index];
    }
    else
    {
        return false;
    }
}

//------------------------ Check if word is safe to playback (not damaged or fixed).
bool PCM1DataBlock::isWordValid(uint8_t index)
{
    if(index<getWordCount())
    {
        return word_crc[index];
    }
    else
    {
        return false;
    }
}

//------------------------ Check if data block is valid.
bool PCM1DataBlock::isBlockValid()
{
    bool valid;
    valid = true;
    if(getErrorsAudio()>0)
    {
        valid = false;
    }
    return valid;
}

//------------------------ Is audio sample near zero value?
bool PCM1DataBlock::isNearSilence(uint8_t index)
{
    int16_t audio_sample;
    audio_sample = getSample(index);
    // Allow 2 LSBs (2 LSBs in 14-bit word become 4 LSBs in 16-bit state) wiggle room.
    if(audio_sample>=(int16_t)(1<<4))
    {
        return false;
    }
    if(audio_sample<(0-(int16_t)(1<<4)))
    {
        return false;
    }
    return true;
}

//------------------------ Are most audio samples are near "0"?
bool PCM1DataBlock::isAlmostSilent()
{
    bool silent;
    uint8_t word_lim;
    silent = true;
    word_lim = getWordCount();
    for(uint8_t index=WORD_FIRST;index<word_lim;index++)
    {
        if(isNearSilence(index)==false)
        {
            silent = false;
            break;
        }
    }
    return silent;
}

//------------------------ Are all audio samples = 0?
bool PCM1DataBlock::isSilent()
{
    bool silent;
    uint8_t word_lim;
    silent = true;
    word_lim = getWordCount();
    for(uint8_t index=WORD_FIRST;index<word_lim;index++)
    {
        if(getSample(index)!=0)
        {
            silent = false;
            break;
        }
    }
    return silent;
}

//------------------------ Does this block has normal length?
bool PCM1DataBlock::isNormalLength()
{
    return !short_blk;
}

//------------------------ Does this block has short length?
bool PCM1DataBlock::isShortLength()
{
    return short_blk;
}

//------------------------ Do samples in the block need de-emphasis for playback?
bool PCM1DataBlock::hasEmphasis()
{
    return emphasis;
}

//------------------------ Get maximum word count according to block length.
uint8_t PCM1DataBlock::getWordCount()
{
    if(short_blk==false)
    {
        return WORD_CNT;
    }
    else
    {
        return WORD_CNT_SHORT;
    }
}

//------------------------ Get processed word.
uint16_t PCM1DataBlock::getWord(uint8_t index)
{
    if(index<getWordCount())
    {
        return words[index];
    }
    else
    {
        return 0;
    }
}

//------------------------ Get audio sample.
int16_t PCM1DataBlock::getSample(uint8_t index)
{
    if(index<getWordCount())
    {
        bool is_positive;
        uint16_t data_word;

        data_word = words[index];

        // Check 13th bit for range (R bit).
        if((data_word&PCM1Line::BIT_RANGE_POS)==0)
        {
            // Higher range.
            // Move 4 bits left to fill up 16-bit MSBs.
            data_word = (data_word<<4);
        }
        else
        {
            // Lower range.
            // Pick sign of the value.
            is_positive = ((data_word&PCM1Line::BIT_SIGN_POS)==0);
            // Remove range bit.
            data_word = data_word&(~PCM1Line::BIT_RANGE_POS);
            // Move 2 bits left, leaving 2 MSBs empty.
            data_word = (data_word<<2);
            if(is_positive==false)
            {
                // Fill all MSBs following negative sign.
                data_word|=(1<<15)|(1<<14);
            }
        }
        // Return the word converted in 16-bit sample.
        return (int16_t)data_word;
    }
    else
    {
        // Index out-of-bounds.
        return 0;
    }
}

//------------------------ Get error count for audio samples.
uint8_t PCM1DataBlock::getErrorsAudio()
{
    uint8_t crc_errs;
    crc_errs = 0;
    // Search for audio samples with bad CRC.
    for(uint8_t index=WORD_FIRST;index<getWordCount();index++)
    {
        if(word_crc[index]==false)
        {
            crc_errs++;
        }
    }
    return crc_errs;
}

//------------------------ Get total error count for all words.
uint8_t PCM1DataBlock::getErrorsTotal()
{
    return getErrorsAudio();
}

//------------------------ Convert PCM data to string for debug.
std::string PCM1DataBlock::dumpWordsString(uint8_t line_index)
{
    bool index_lock;
    uint8_t line_pos, line_cnt, zero_char, one_char, bit_pos;
    std::string text_out;

    index_lock = false;
    line_pos = line_cnt = 0;
    // Cycle through all words.
    for(uint16_t wrd=0;wrd<getWordCount();wrd++)
    {
        // Determine opening bracket and bit chars appearance according to source CRC.
        if(word_crc[wrd]==false)
        {
            text_out += DUMP_WBRL_BAD;
            zero_char = DUMP_BIT_ZERO_BAD;
            one_char = DUMP_BIT_ONE_BAD;
        }
        else
        {
            text_out += DUMP_WBRL_OK;
            zero_char = DUMP_BIT_ZERO;
            one_char = DUMP_BIT_ONE;
        }

        // Cycle through bits of the word.
        bit_pos = PCM1Line::BITS_PER_WORD;
        do
        {
            bit_pos--;
            if((words[wrd]&(1<<bit_pos))==0)
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
        if(word_crc[wrd]==false)
        {
            text_out += DUMP_WBRR_BAD;
        }
        else
        {
            text_out += DUMP_WBRR_OK;
        }

        line_pos++;
        if(line_pos>=DUMP_SPLIT)
        {
            if(line_cnt==line_index)
            {
                index_lock = true;
            }
            else
            {
                text_out.clear();
            }
            line_cnt++;
            if(index_lock!=false)
            {
                break;
            }
            line_pos = 0;
        }
    }
    return text_out;
}

//------------------------ Convert PCM data to string for debug.
std::string PCM1DataBlock::dumpSamplesString(uint8_t line_index)
{
    bool index_lock;
    uint8_t line_pos, line_cnt, zero_char, one_char, bit_pos;
    uint16_t sample_val;
    std::string text_out;

    index_lock = false;
    line_pos = line_cnt = 0;
    // Cycle through all words.
    for(uint16_t wrd=0;wrd<getWordCount();wrd++)
    {
        // Determine opening bracket and bit chars appearance according to source CRC.
        if(word_crc[wrd]==false)
        {
            text_out += DUMP_WBRL_BAD;
            zero_char = DUMP_BIT_ZERO_BAD;
            one_char = DUMP_BIT_ONE_BAD;
        }
        else
        {
            text_out += DUMP_WBRL_OK;
            zero_char = DUMP_BIT_ZERO;
            one_char = DUMP_BIT_ONE;
        }

        // Cycle through bits of the word.
        sample_val = (uint16_t)getSample(wrd);
        bit_pos = 16;
        do
        {
            bit_pos--;
            if((sample_val&(1<<bit_pos))==0)
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
        if(word_crc[wrd]==false)
        {
            text_out += DUMP_WBRR_BAD;
        }
        else
        {
            text_out += DUMP_WBRR_OK;
        }

        line_pos++;
        if(line_pos>=DUMP_SPLIT)
        {
            if(line_cnt==line_index)
            {
                index_lock = true;
            }
            else
            {
                text_out.clear();
            }
            line_cnt++;
            if(index_lock!=false)
            {
                break;
            }
            line_pos = 0;
        }
    }
    return text_out;
}

//------------------------ Output full information about the data block.
std::string PCM1DataBlock::dumpInfoString()
{
    std::string text_out;
    char c_buf[32];

    text_out = "PCM-1 interleave block: ";
    sprintf(c_buf, "B[%03u-%03u] E[%03u-%03u] I[%01u] 13-BIT ",
            frame_number, start_line, frame_number, stop_line, interleave_num);
    text_out += c_buf;

    if(isShortLength()==false)
    {
        text_out += "L[NORMAL]";
    }
    else
    {
        text_out += "L[SHORT] ";
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

    sprintf(c_buf, "] EA[%01u] T[%04u] ", getErrorsAudio(), process_time);
    text_out += c_buf;

    if(isSilent()!=false)
    {
        text_out += "FS";
    }
    else if(isAlmostSilent()!=false)
    {
        text_out += "AS";
    }
    else
    {
        text_out += "NS";
    }

    return text_out;
}
