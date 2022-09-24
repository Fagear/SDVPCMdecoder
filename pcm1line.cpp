#include "pcm1line.h"

PCM1Line::PCM1Line()
{
    this->clear();
}

PCM1Line::PCM1Line(const PCM1Line &in_object) : PCMLine(in_object)
{
    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    picked_bits_left = in_object.picked_bits_left;
    picked_bits_right = in_object.picked_bits_right;
    // Copy data words.
    for(uint8_t index=0;index<WORD_MAX;index++)
    {
        words[index] = in_object.words[index];
    }
    // Copy pixel coordinates.
    for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
    {
        for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
        {
            pixel_coordinates[stage][bit] = in_object.pixel_coordinates[stage][bit];
        }
    }
}

PCM1Line& PCM1Line::operator= (const PCM1Line &in_object)
{
    if(this==&in_object) return *this;

    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    picked_bits_left = in_object.picked_bits_left;
    picked_bits_right = in_object.picked_bits_right;
    // Copy data words.
    for(uint8_t index=0;index<WORD_MAX;index++)
    {
        words[index] = in_object.words[index];
    }
    // Copy pixel coordinates.
    for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
    {
        for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
        {
            pixel_coordinates[stage][bit] = in_object.pixel_coordinates[stage][bit];
        }
    }

    return *this;
}

//------------------------ Reset all fields to default.
void PCM1Line::clear()
{
    // Clear base class fields.
    PCMLine::clear();
    // Clear own fields.
    picked_bits_left = picked_bits_right = 0;
    // Reset data words.
    setSilent();
    // Reset pixel coordinates.
    for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
    {
        for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
        {
            pixel_coordinates[stage][bit] = 0;
        }
    }
    // Force CRC to be bad until good binarization.
    //calcCRC();
    calc_crc = CRC_SILENT;  // Pre-calculated CRC for silenced line.
    setInvalidCRC();        // Set "read" CRC to inverted one.
}

//------------------------ Set service flag "header/footer line".
void PCM1Line::setServHeader()
{
    // Convert this line into service line.
    PCMLine::setServiceLine();
    // Set service tag.
    service_type = SRVLINE_HEADER_LINE;
}

//------------------------ Set word with source CRC.
void PCM1Line::setSourceCRC(uint16_t in_crc)
{
    words[WORD_CRCC] = in_crc;
}

//------------------------ Zero out all data words.
void PCM1Line::setSilent()
{
    for(uint8_t i=WORD_L2;i<=WORD_R6;i++)
    {
        words[i] = 0;
        words[i] |= BIT_RANGE_POS;
    }
    calcCRC();
}

//------------------------ Get number of data bits in the source line by the standard.
uint8_t PCM1Line::getBitsPerSourceLine()
{
    return BITS_IN_LINE;
}

//------------------------ Get number of data bits between data coordinates (for PPB calculation).
uint8_t PCM1Line::getBitsBetweenDataCoordinates()
{
    return BITS_PCM_DATA;
}

//------------------------ Get highest bit number for the left part of the line for pixel-shifting.
uint8_t PCM1Line::getLeftShiftZoneBit()
{
    return BITS_LEFT_SHIFT;
}

//------------------------ Get lowest bit number for the right part of the line for pixel-shifting.
uint8_t PCM1Line::getRightShiftZoneBit()
{
    return BITS_RIGHT_SHIFT;
}

//------------------------ Get pre-calculated coordinate of pixel in video line for requested PCM bit number and pixel-shifting stage.
//------------------------ [calcPPB()] MUST be called before any [findVideoPixel()] calls!
uint16_t PCM1Line::getVideoPixelT(uint8_t pcm_bit, uint8_t shift_stage)
{
    return pixel_coordinates[shift_stage][pcm_bit];
}

//------------------------ Re-calculate CRCC for all words in the line.
void PCM1Line::calcCRC()
{
    CRC16_init(&calc_crc);
    for(uint8_t i=WORD_L2;i<=WORD_R6;i++)
    {
        calc_crc = getCalcCRC16(calc_crc, ~words[i], BITS_PER_WORD);
    }
    calc_crc = ~calc_crc;
#ifdef PCM1_LINE_EN_DBG_OUT
    QString log_line;
    log_line.sprintf("[PCM1L] Calculating CRC... %04x", calc_crc);
    qInfo()<<log_line;
#endif
}

//------------------------ Get CRC that was read from the source.
uint16_t PCM1Line::getSourceCRC()
{
    return words[WORD_CRCC];
}

//------------------------ Get the type of PCM to determine class derived from [PCMLine];
uint8_t PCM1Line::getPCMType()
{
    return TYPE_PCM1;
}

//------------------------ Convert one 13-bit word to a 16-bit sample.
int16_t PCM1Line::getSample(uint8_t index)
{
    bool is_positive;
    uint16_t data_word;

    if(index<WORD_CRCC)
    {
        data_word = words[index];
        // Check 13th bit for range (R bit).
        if((data_word&BIT_RANGE_POS)==0)
        {
            // Higher range.
            // Move 4 bits left to fill up 16-bit MSBs.
            data_word = (data_word<<4);
        }
        else
        {
            // Lower range.
            // Pick sign of the value.
            is_positive = ((data_word&BIT_SIGN_POS)==0);
            // Remove range bit.
            data_word = data_word&(~BIT_RANGE_POS);
            // Move 2 bits left, leaving 2 MSBs empty.
            data_word = (data_word<<2);
            if(is_positive==false)
            {
                // Fill all MSBs following negative sign.
                data_word|=(1<<15)|(1<<14);
            }
        }
    }
    else
    {
        // Index out-of-bounds.
        data_word = 0;
    }
    return (int16_t)data_word;
}

//------------------------ Does provided line have the same words?
bool PCM1Line::hasSameWords(PCM1Line *in_line)
{
    bool equal;
    equal = true;
    if(in_line==NULL)
    {
        return false;
    }
    else
    {
        for(uint8_t index=0;index<WORD_CRCC;index++)
        {
            if(words[index]!=in_line->words[index])
            {
                equal = false;
                break;
            }
        }
        return equal;
    }
}

//------------------------ Does this line contain words with picked bits?
bool PCM1Line::hasPickedWords()
{
    if((picked_bits_left!=0)||(picked_bits_right!=0))
    {
        return true;
    }
    return false;
}

//------------------------ Were the leftmost word's bits picked during binarization?
bool PCM1Line::hasPickedLeft()
{
    if(picked_bits_left!=0)
    {
        return true;
    }
    return false;
}

//------------------------ Were the rightmost word's bits picked during binarization?
bool PCM1Line::hasPickedRight()
{
    if(picked_bits_right!=0)
    {
        return true;
    }
    return false;
}

//------------------------ Does this line contain header line (and no audio data)?
bool PCM1Line::hasHeader()
{
    if((words[WORD_L2]==0x0666)&&(words[WORD_R2]==0x0CCC)&&(words[WORD_L4]==0x1999)
       &&(words[WORD_R4]==0x1333)&&(words[WORD_L6]==0x0666)&&(words[WORD_R6]==0x0CCC)
       &&(getSourceCRC()==0xCCCC))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Is CRC valid (re-calculated CRC is the same as read one) or does the line contain the header (PCM-1 special case)?
bool PCM1Line::isCRCValidIgnoreForced()
{
    if((getCalculatedCRC()==getSourceCRC())||(hasHeader()!=false))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Is audio sample near zero value?
bool PCM1Line::isNearSilence(uint8_t index)
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

//------------------------ Are audio samples in both channels near zero?
bool PCM1Line::isAlmostSilent()
{
    bool silent;
    silent = false;
    // Check if both channels are close to silence.
    if(((isNearSilence(WORD_L2)!=false)||(isNearSilence(WORD_L4)!=false)||(isNearSilence(WORD_L6)!=false))&&
        ((isNearSilence(WORD_R2)!=false)||(isNearSilence(WORD_R4)!=false)||(isNearSilence(WORD_R6)!=false)))
    {
        silent = true;
    }

    return silent;
}

//------------------------ Are all audio words zeroed?
bool PCM1Line::isSilent()
{
    bool zero;
    zero = true;
    for(uint8_t index=WORD_L2;index<=WORD_R6;index++)
    {
        if(getSample(index)!=0)
        {
            zero = false;
            break;
        }
    }
    return zero;
}

//------------------------ Check if line has service tag "header/footer line".
bool PCM1Line::isServHeader()
{
    if(service_type==SRVLINE_HEADER_LINE)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Convert PCM data to string for debug.
std::string PCM1Line::dumpWordsString()
{
    std::string text_out;
    uint8_t ind, bit_pos;

    // Printing 13 bit words data.
    for(ind=WORD_L2;ind<=WORD_R6;ind++)
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
            else if((ind==WORD_L2)&&(bit_pos>=(BITS_PER_WORD-picked_bits_left)))
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
std::string PCM1Line::dumpContentString()
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
        else if(isServHeader()!=false)
        {
            sprintf(c_buf, "F[%03u] L[%03u] SERVICE LINE: PCM-1 header/footer line", (unsigned int)frame_number, line_number);
            text_out += c_buf;
        }
    }
    else
    {
        sprintf(c_buf, "F[%03u] L[%03u] ", (unsigned int)frame_number, line_number);
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
        }
        else
        {
            sprintf(c_buf, "D[%03d|%04d] ", coords.data_start, coords.data_stop);
        }
        text_out += c_buf;
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
        sprintf(c_buf, "PPB[%02u.%03u] ", getPPB(), getPPBfrac());
        text_out += c_buf;
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
std::string PCM1Line::helpDumpNext()
{
    std::string text_out;
    if(help_stage<PCM1_LINE_HELP_SIZE)
    {
        text_out = PCM1_HELP_LIST[help_stage];
        help_stage++;
    }
    else
    {
        text_out.clear();
    }
    return text_out;
}

//------------------------ Pre-calculate pixel coordinates list for given pixel shift stage.
void PCM1Line::calcCoordinates(uint8_t in_shift)
{
    for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
    {
        pixel_coordinates[in_shift][bit] = PCMLine::getVideoPixelC(bit, in_shift);
    }
}
