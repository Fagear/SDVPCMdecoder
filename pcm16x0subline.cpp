#include "pcm16x0subline.h"

PCM16X0SubLine::PCM16X0SubLine()
{
    this->clear();
}

PCM16X0SubLine::PCM16X0SubLine(const PCM16X0SubLine &in_object) : PCMLine(in_object)
{
    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    control_bit = in_object.control_bit;
    line_part = in_object.line_part;
    picked_bits_left = in_object.picked_bits_left;
    picked_bits_right = in_object.picked_bits_right;
    queue_order = in_object.queue_order;
    // Copy pixel coordinates.
    for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
    {
        for(uint8_t bit=0;bit<BITS_IN_LINE;bit++)
        {
            pixel_coordinates[stage][bit] = in_object.pixel_coordinates[stage][bit];
        }
    }
    // Copy data words.
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        words[index] = in_object.words[index];
    }
}

PCM16X0SubLine& PCM16X0SubLine::operator= (const PCM16X0SubLine &in_object)
{
    if(this==&in_object) return *this;

    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    control_bit = in_object.control_bit;
    line_part = in_object.line_part;
    picked_bits_left = in_object.picked_bits_left;
    picked_bits_right = in_object.picked_bits_right;
    queue_order = in_object.queue_order;
    // Copy pixel coordinates.
    for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
    {
        for(uint8_t bit=0;bit<BITS_IN_LINE;bit++)
        {
            pixel_coordinates[stage][bit] = in_object.pixel_coordinates[stage][bit];
        }
    }
    // Copy data words.
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        words[index] = in_object.words[index];
    }

    return *this;
}

//------------------------ Reset all fields to default.
void PCM16X0SubLine::clear()
{
    // Clear base class fields.
    PCMLine::clear();
    // Clear own fields.
    control_bit = true;
    line_part = PART_LEFT;
    picked_bits_left = picked_bits_right = 0;
    queue_order = 0;
    // Reset pixel coordinates.
    for(uint8_t bit=0;bit<BITS_IN_LINE;bit++)
    {
        for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
        {
            pixel_coordinates[stage][bit] = 0;
        }
    }
    // Reset data words.
    setSilent();
    // Force CRC to be bad until good binarization.
    //calcCRC();
    calc_crc = CRC_SILENT;  // Pre-calculated CRC for silenced line.
    setInvalidCRC();        // Set "read" CRC to inverted one.
}

//------------------------ Set word with source CRC.
void PCM16X0SubLine::setSourceCRC(uint16_t in_crc)
{
    words[WORD_CRCC] = in_crc;
}

//------------------------ Zero out all data words.
void PCM16X0SubLine::setSilent()
{
    for(uint8_t i=WORD_R1P1L1;i<=WORD_R3P3L3;i++)
    {
        words[i] = 0;
    }
    calcCRC();
}

//------------------------ Copy 16-bit word into the object.
void PCM16X0SubLine::setWord(uint8_t index, uint16_t in_word)
{
    if(index<WORD_CNT)
    {
        if(index==WORD_CRCC)
        {
            words[index] = in_word&CRC_WORD_MASK;
        }
        else
        {
            words[index] = in_word&DATA_WORD_MASK;
        }
    }
}

//------------------------ Get number of data bits in the source line by the standard.
uint8_t PCM16X0SubLine::getBitsPerSourceLine()
{
    return BITS_IN_LINE;
}

//------------------------ Get number of data bits between data coordinates (for PPB calculation).
uint8_t PCM16X0SubLine::getBitsBetweenDataCoordinates()
{
    return BITS_IN_LINE;
}

//------------------------ Get highest bit number for the left part of the line for pixel-shifting.
uint8_t PCM16X0SubLine::getLeftShiftZoneBit()
{
    return BITS_LEFT_SHIFT;
}

//------------------------ Get lowest bit number for the right part of the line for pixel-shifting.
uint8_t PCM16X0SubLine::getRightShiftZoneBit()
{
    return BITS_RIGHT_SHIFT;
}

//------------------------ Get pre-calculated coordinate of pixel in video line for requested PCM bit number and pixel-shifting stage.
//------------------------ [calcPPB()] MUST be called before any [findVideoPixel()] calls!
uint16_t PCM16X0SubLine::getVideoPixelByTable(uint8_t pcm_bit, uint8_t shift_stage)
{
    return pixel_coordinates[shift_stage][pcm_bit];
}

//------------------------ Re-calculate CRCC for all words in the line.
void PCM16X0SubLine::calcCRC()
{
    CRC16_init(&calc_crc);
    for(uint8_t i=WORD_R1P1L1;i<=WORD_R3P3L3;i++)
    {
        calc_crc = getCalcCRC16(calc_crc, words[i], BITS_PER_WORD);
    }
#ifdef PCM16X0_LINE_EN_DBG_OUT
    QString log_line;
    log_line.sprintf("[PCM16X0L] Calculating CRC... %04x", calc_crc);
    qInfo()<<log_line;
#endif
}

//------------------------ Get CRC that was read from the source.
uint16_t PCM16X0SubLine::getSourceCRC()
{
    return words[WORD_CRCC];
}

//------------------------ Get the type of PCM to determine class derived from [PCMLine].
uint8_t PCM16X0SubLine::getPCMType()
{
    return TYPE_PCM16X0;
}

//------------------------ Get one word.
uint16_t PCM16X0SubLine::getWord(uint8_t index)
{
    if(index<WORD_CNT)
    {
        return words[index];
    }
    return 0;
}

//------------------------ Convert one 16-bit word to a 16-bit sample.
int16_t PCM16X0SubLine::getSample(uint8_t index)
{
    if(index<=WORD_R3P3L3)
    {
        return (int16_t)words[index];
    }
    // Index out-of-bounds.
    return 0;
}

//------------------------ Does provided line have the same words?
bool PCM16X0SubLine::hasSameWords(PCM16X0SubLine *in_line)
{
    if(in_line==NULL)
    {
        return false;
    }
    else
    {
        for(uint8_t index=WORD_R1P1L1;index<=WORD_R3P3L3;index++)
        {
            if(words[index]!=in_line->words[index])
            {
                return false;
            }
        }
        return true;
    }
}

//------------------------ Does this line contain words with picked bits?
bool PCM16X0SubLine::hasPickedWords()
{
    if((picked_bits_left!=0)||(picked_bits_right!=0))
    {
        return true;
    }
    return false;
}

//------------------------ Were the leftmost word's bits picked during binarization?
bool PCM16X0SubLine::hasPickedLeft()
{
    if(picked_bits_left!=0)
    {
        return true;
    }
    return false;
}

//------------------------ Were the rightmost word's bits picked during binarization?
bool PCM16X0SubLine::hasPickedRight()
{
    if(picked_bits_right!=0)
    {
        return true;
    }
    return false;
}

//------------------------ Is CRC valid (re-calculated CRC is the same as read one)?
bool PCM16X0SubLine::isCRCValidIgnoreForced()
{
    if(getCalculatedCRC()==getSourceCRC())
    {
        return true;
    }
    return false;
}

//------------------------ Is audio sample near zero value?
bool PCM16X0SubLine::isNearSilence(uint8_t index)
{
    int16_t audio_sample;
    audio_sample = getSample(index);
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

//------------------------ Are audio samples in both channels near zero?
bool PCM16X0SubLine::isAlmostSilent()
{
    // Check if both channels are close to silence.
    if(((isNearSilence(WORD_R1P1L1)!=false)&&(isNearSilence(WORD_L2P2R2)!=false))||
        ((isNearSilence(WORD_L2P2R2)!=false)&&(isNearSilence(WORD_R3P3L3)!=false)))
    {
        return true;
    }
    return false;
}

//------------------------ Are all audio words zeroed?
bool PCM16X0SubLine::isSilent()
{
    for(uint8_t index=WORD_R1P1L1;index<=WORD_R3P3L3;index++)
    {
        if(getSample(index)!=0)
        {
            return false;
        }
    }
    return true;
}

//------------------------ Were the word's bits picked during binarization?
bool PCM16X0SubLine::isPicked(uint8_t word)
{
    if(word<WORD_CNT)
    {
        if((picked_bits_left!=0)&&(word==WORD_R1P1L1))
        {
            // Left word was picked.
            return true;
        }
        else if((picked_bits_right!=0)&&(word==WORD_CRCC))
        {
            // Right word was picked.
            return true;
        }
    }
    return false;
}

//------------------------ Convert PCM data to string for debug.
std::string PCM16X0SubLine::dumpWordsString()
{
    std::string text_out;
    uint8_t ind, bit_pos;

    text_out = DUMP_WBRL_OK;
    if(control_bit==false)
    {
        text_out += DUMP_BIT_ZERO;
    }
    else
    {
        text_out += DUMP_BIT_ONE;
    }
    text_out += DUMP_WBRR_OK;

    // Printing 16 bit words data.
    for(ind=WORD_R1P1L1;ind<=WORD_R3P3L3;ind++)
    {
        if(isCRCValid()==false)
        {
            text_out += DUMP_WBRL_BAD;
        }
        else
        {
            text_out += DUMP_WBRL_OK;
        }
        bit_pos = BITS_PER_WORD;
        do
        {
            bit_pos--;
            if(isCRCValid()==false)
            {
                if((words[ind]&(1<<bit_pos))==0)
                {
                    text_out += DUMP_BIT_ZERO_BAD;
                }
                else
                {
                    text_out += DUMP_BIT_ONE_BAD;
                }
            }
            else if((ind==WORD_R1P1L1)&&(bit_pos>=(BITS_PER_WORD-picked_bits_left)))
            {
                if((words[ind]&(1<<bit_pos))==0)
                {
                    text_out += DUMP_BIT_ZERO_BAD;
                }
                else
                {
                    text_out += DUMP_BIT_ONE_BAD;
                }
            }
            else
            {
                if((words[ind]&(1<<bit_pos))==0)
                {
                    text_out += DUMP_BIT_ZERO;
                }
                else
                {
                    text_out += DUMP_BIT_ONE;
                }
            }
        }
        while(bit_pos>0);
        if(isCRCValid()==false)
        {
            text_out += DUMP_WBRR_BAD;
        }
        else
        {
            text_out += DUMP_WBRR_OK;
        }
    }
    // Printing 16 bit CRCC data.
    if(isCRCValid()==false)
    {
        text_out += DUMP_WBRL_BAD;
    }
    else
    {
        text_out += DUMP_WBRL_OK;
    }
    bit_pos = BITS_PER_CRC;
    do
    {
        bit_pos--;
        if(isCRCValid()==false)
        {
            if((getSourceCRC()&(1<<bit_pos))==0)
            {
                text_out += DUMP_BIT_ZERO_BAD;
            }
            else
            {
                text_out += DUMP_BIT_ONE_BAD;
            }
        }
        else if(bit_pos<picked_bits_right)
        {
            if((getSourceCRC()&(1<<bit_pos))==0)
            {
                text_out += DUMP_BIT_ZERO_BAD;
            }
            else
            {
                text_out += DUMP_BIT_ONE_BAD;
            }
        }
        else
        {
            if((getSourceCRC()&(1<<bit_pos))==0)
            {
                text_out += DUMP_BIT_ZERO;
            }
            else
            {
                text_out += DUMP_BIT_ONE;
            }
        }
    }
    while(bit_pos>0);
    if(isCRCValid()==false)
    {
        text_out += DUMP_WBRR_BAD;
    }
    else
    {
        text_out += DUMP_WBRR_OK;
    }

    return text_out;
}

//------------------------ Output full information about the line.
std::string PCM16X0SubLine::dumpContentString()
{
    std::string text_out;
    char c_buf[256];

    if(isServiceLine()!=false)
    {
        if(isServNewFile()!=false)
        {
            sprintf(c_buf, "F[%03u] L[%03u] SERVICE LINE: next lines are from new file: %s", (unsigned int)frame_number, line_number, file_path.substr(0, 192).c_str());
            text_out += c_buf;
        }
        else if(isServEndFile()!=false)
        {
            sprintf(c_buf, "F[%03u] L[%03u] SERVICE LINE: previous lines were last in the file", (unsigned int)frame_number, line_number);
            text_out += c_buf;
        }
        else if(isServFiller()!=false)
        {
            sprintf(c_buf, "F[%03u] L[%03u] SERVICE LINE: line from a filler frame", (unsigned int)frame_number, line_number);
            text_out += c_buf;
        }
        else if(isServEndField()!=false)
        {
            sprintf(c_buf, "F[%03u] L[%03u] SERVICE LINE: field of a frame ended", (unsigned int)frame_number, line_number);
            text_out += c_buf;
        }
        else if(isServEndFrame()!=false)
        {
            sprintf(c_buf, "F[%03u] L[%03u] SERVICE LINE: frame ended", (unsigned int)frame_number, line_number);
            text_out += c_buf;
        }
    }
    else
    {
        sprintf(c_buf, "F[%03u] L[%03u] P[%01u] ", (unsigned int)frame_number, line_number, line_part);
        text_out += c_buf;

        if(hasBWSet()==false)
        {
            sprintf(c_buf, "Y[%03u?%03u] ", black_level, white_level);
            text_out += c_buf;
        }
        else
        {
            sprintf(c_buf, "Y[%03u|%03u] ", black_level, white_level);
            text_out += c_buf;
        }

        if(isDataByRefSweep()!=false)
        {
            sprintf(c_buf, "R>[%03u|%03u|%03u] ", ref_low, ref_level, ref_high);
        }
        else if(isDataBySkip()!=false)
        {
            sprintf(c_buf, "R=[%03u|%03u|%03u] ", ref_low, ref_level, ref_high);
        }
        else
        {
            sprintf(c_buf, "R?[%03u|%03u|%03u] ", ref_low, ref_level, ref_high);
        }
        text_out += c_buf;
        if(isDataByCoordSweep()!=false)
        {
            sprintf(c_buf, "D[%03d:%04d] ", coords.data_start, coords.data_stop);
            text_out += c_buf;
        }
        else if(coords.areValid()!=false)
        {
            sprintf(c_buf, "D[%03d|%04d] ", coords.data_start, coords.data_stop);
            text_out += c_buf;
        }
        else
        {
            text_out += "D[N/A| N/A] ";
        }

        text_out += dumpWordsString();

        text_out += " BP[";
        if(picked_bits_left>0)
        {
            sprintf(c_buf, "%01u/", picked_bits_left);
            text_out += c_buf;
        }
        else
        {
            text_out += "-/";
        }
        if(picked_bits_right>0)
        {
            sprintf(c_buf, "%01u] ", picked_bits_right);
            text_out += c_buf;
        }
        else
        {
            text_out += "-] ";
        }
        sprintf(c_buf, "Q[%03u] ", queue_order);
        text_out += c_buf;
        sprintf(c_buf, "PPB[%02u.%03u] ", getPPB(), getPPBfrac());
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
        sprintf(c_buf, "HD[%02u] ", hysteresis_depth);
        text_out += c_buf;
        sprintf(c_buf, "SS[%01u] ", shift_stage);
        text_out += c_buf;
        sprintf(c_buf, "T[%05u] ", process_time);
        text_out += c_buf;

        if(isCRCValidIgnoreForced()!=false)
        {
            if(isForcedBad()==false)
            {
                sprintf(c_buf, "CRC OK  [0x%04X]", getCalculatedCRC());
                text_out += c_buf;
            }
            else
            {
                sprintf(c_buf, "BD CRC! [0x%04X] (forced)", getCalculatedCRC());
                text_out += c_buf;
            }
        }
        else if(hasBWSet()!=false)
        {
            sprintf(c_buf, "BD CRC! [0x%04X!=0x%04X]", getCalculatedCRC(), getSourceCRC());
            text_out += c_buf;
        }
        else
        {
            text_out += "No PCM!";
        }
    }
    return text_out;
}

//------------------------ Next line in help output.
std::string PCM16X0SubLine::helpDumpNext()
{
    std::string text_out;
    if(help_stage<PCM16X0_LINE_HELP_SIZE)
    {
        text_out = PCM16X0_HELP_LIST[help_stage];
        help_stage++;
    }
    else
    {
        text_out.clear();
    }
    return text_out;
}

//------------------------ Pre-calculate pixel coordinates list for given pixel shift stage.
void PCM16X0SubLine::calcCoordinates(uint8_t in_shift)
{
    for(uint8_t bit=0;bit<BITS_IN_LINE;bit++)
    {
        pixel_coordinates[in_shift][bit] = PCMLine::getVideoPixeBylCalc(bit, in_shift);
    }
}

