#include "arvidline.h"

ArVidLine::ArVidLine()
{
    this->clear();
}

ArVidLine::ArVidLine(const ArVidLine &in_object) : PCMLine(in_object)
{
    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    // Copy data words.
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        words[index] = in_object.words[index];
    }
    // Copy pixel coordinates.
    for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
    {
        for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
        {
            pixel_coordinates[stage][bit] = in_object.pixel_coordinates[stage][bit];
        }
    }
}

ArVidLine& ArVidLine::operator= (const ArVidLine &in_object)
{
    if(this==&in_object) return *this;

    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    // Copy data words.
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        words[index] = in_object.words[index];
    }
    // Copy pixel coordinates.
    for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
    {
        for(uint8_t stage=0;stage<PCM_LINE_MAX_PS_STAGES;stage++)
        {
            pixel_coordinates[stage][bit] = in_object.pixel_coordinates[stage][bit];
        }
    }

    return *this;
}

void ArVidLine::clear()
{
    // Clear base class fields.
    PCMLine::clear();
    // Clear own fields.

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
}

//------------------------ Set word with source CRC.
void ArVidLine::setSourceCRC(uint16_t in_crc)
{
    Q_UNUSED(in_crc)
    // TODO
}

//------------------------ Zero out all data words.
void ArVidLine::setSilent()
{
    for(uint8_t i=0;i<WORD_CNT;i++)
    {
        words[i] = 0;
    }
}

//------------------------ Get number of data bits in the source line by the standard.
uint8_t ArVidLine::getBitsPerSourceLine()
{
    return BITS_IN_LINE;
}

//------------------------ Get number of data bits between data coordinates (for PPB calculation).
uint8_t ArVidLine::getBitsBetweenDataCoordinates()
{
    return BITS_PCM_DATA;
}

//------------------------ Get highest bit number for the left part of the line for pixel-shifting.
uint8_t ArVidLine::getLeftShiftZoneBit()
{
    return BITS_LEFT_SHIFT;
}

//------------------------ Get lowest bit number for the right part of the line for pixel-shifting.
uint8_t ArVidLine::getRightShiftZoneBit()
{
    return BITS_RIGHT_SHIFT;
}

//------------------------ Get pre-calculated coordinate of pixel in video line for requested PCM bit number and pixel-shifting stage.
//------------------------ [calcPPB()] MUST be called before any [findVideoPixel()] calls!
uint16_t ArVidLine::getVideoPixelT(uint8_t pcm_bit, uint8_t shift_stage)
{
    return pixel_coordinates[shift_stage][pcm_bit];
}

//------------------------ Re-calculate CRCC for all words in the line.
void ArVidLine::calcCRC()
{
    // TODO
}

//------------------------ Get CRC that was read from the source.
uint16_t ArVidLine::getSourceCRC()
{
    // TODO
    return 0;
}

//------------------------ Get the type of PCM to determine class derived from [PCMLine].
uint8_t ArVidLine::getPCMType()
{
    return TYPE_ARVA;
}

int16_t ArVidLine::getSample(uint8_t index)
{
    Q_UNUSED(index)
    // TODO
    return 0;
}

//------------------------ Is CRC valid (re-calculated CRC is the same as read one)?
bool ArVidLine::isCRCValidIgnoreForced()
{
    // TODO
    return false;
}

//------------------------ Is audio sample near zero value?
bool ArVidLine::isNearSilence(uint8_t index)
{
    Q_UNUSED(index)
    // TODO
    return false;
}

//------------------------ Are audio samples in both channels near zero?
bool ArVidLine::isAlmostSilent()
{
    // TODO
    return false;
}

//------------------------ Are all audio words zeroed?
bool ArVidLine::isSilent()
{
    // TODO
    return false;
}

//------------------------ Convert PCM data to string for debug.
std::string ArVidLine::dumpWordsString()
{
    std::string text_out;
    uint8_t ind, bit_pos;

    // Printing header.
    /*if(hasStartMarker()==false)
    {
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
    }
    else*/
    {
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
    }

    // Printing 14 bit words data.
    for(ind=0;ind<WORD_CNT;ind++)
    {
        text_out += DUMP_WBRL_OK;
        bit_pos = BITS_PER_WORD;
        do
        {
            bit_pos--;
            if((words[ind]&(1<<bit_pos))==0)
            {
                text_out += DUMP_BIT_ZERO;
            }
            else
            {
                text_out += DUMP_BIT_ONE;
            }
        }
        while(bit_pos>0);
        text_out += DUMP_WBRR_OK;
    }

    return text_out;
}

//------------------------ Output full information about the line.
std::string ArVidLine::dumpContentString()
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

        sprintf(c_buf, " PPB[%02u.%03u] ", getPPB(), getPPBfrac());
        text_out += c_buf;

        sprintf(c_buf, "T[%05u]", process_time);
        text_out += c_buf;
    }

    return text_out;
}

//------------------------ Next line in help output.
std::string ArVidLine::helpDumpNext()
{
    std::string text_out;
    // TODO
    return text_out;
}

void ArVidLine::calcCoordinates(uint8_t in_shift)
{
    for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
    {
        pixel_coordinates[in_shift][bit] = PCMLine::getVideoPixelC(bit, in_shift, 0);
    }
}
