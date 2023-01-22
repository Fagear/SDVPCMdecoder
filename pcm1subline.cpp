#include "pcm1subline.h"

PCM1SubLine::PCM1SubLine()
{
    clear();
}

PCM1SubLine::PCM1SubLine(const PCM1SubLine &in_object)
{
    // Copy misc. field.
    frame_number = in_object.frame_number;
    line_number = in_object.line_number;
    picked_bits_left = in_object.picked_bits_left;
    picked_bits_right = in_object.picked_bits_right;
    line_part = in_object.line_part;
    bw_set = in_object.bw_set;
    crc = in_object.crc;
    // Copy data words.
    for(uint8_t i=0;i<WORD_CNT;i++)
    {
        words[i] = in_object.words[i];
    }
}

PCM1SubLine& PCM1SubLine::operator= (const PCM1SubLine &in_object)
{
    if(this==&in_object) return *this;

    // Copy misc. field.
    frame_number = in_object.frame_number;
    line_number = in_object.line_number;
    picked_bits_left = in_object.picked_bits_left;
    picked_bits_right = in_object.picked_bits_right;
    line_part = in_object.line_part;
    bw_set = in_object.bw_set;
    crc = in_object.crc;
    // Copy data words.
    for(uint8_t i=0;i<WORD_CNT;i++)
    {
        words[i] = in_object.words[i];
    }

    return *this;
}

//------------------------ Reset object.
void PCM1SubLine::clear()
{
    frame_number = line_number = 0;
    picked_bits_left = picked_bits_right = 0;
    setLinePart(PART_LEFT);
    setBWLevels(false);
    setCRCValid(false);
    setSilent();
}

//------------------------ Zero out all data words.
void PCM1SubLine::setSilent()
{
    for(uint8_t i=WORD_L;i<=WORD_R;i++)
    {
        words[i] = PCM1Line::BIT_RANGE_POS;
    }
}

//------------------------ Copy the word into the object.
void PCM1SubLine::setWord(uint8_t index, uint16_t in_word)
{
    if(index<WORD_CNT)
    {
        words[index] = in_word&DATA_WORD_MASK;
    }
}

//------------------------ Copy word for the left channel into object.
void PCM1SubLine::setLeft(uint16_t in_word)
{
    setWord(WORD_L, in_word);
}

//------------------------ Copy word for the right channel into object.
void PCM1SubLine::setRight(uint16_t in_word)
{
    setWord(WORD_R, in_word);
}

//------------------------ Set part of the source video line.
void PCM1SubLine::setLinePart(uint8_t index)
{
    if(index<PART_MAX)
    {
        line_part = index;
    }
}

//------------------------ Set presence of BLACK and WHITE levels.
void PCM1SubLine::setBWLevels(bool bw_ok)
{
    bw_set = bw_ok;
}

//------------------------ Set validity of the words.
void PCM1SubLine::setCRCValid(bool crc_ok)
{
    crc = crc_ok;
}

//------------------------ Were BLACK and WHITE levels set for the line?
bool PCM1SubLine::hasBWSet()
{
    return bw_set;
}

//------------------------ Were the leftmost word's bits picked during binarization?
bool PCM1SubLine::hasPickedLeft()
{
    if(picked_bits_left>0)
    {
        return true;
    }
    return false;
}

//------------------------ Were the rightmost word's bits picked during binarization?
bool PCM1SubLine::hasPickedRight()
{
    if(picked_bits_right>0)
    {
        return true;
    }
    return false;
}

//------------------------ Check if words are marked as valid.
bool PCM1SubLine::isCRCValid()
{
    return crc;
}

//------------------------ Get word data by its index.
uint16_t PCM1SubLine::getWord(uint8_t index)
{
    if(index<WORD_CNT)
    {
        return words[index];
    }
    return 0;
}

//------------------------ Get left sample word.
uint16_t PCM1SubLine::getLeft()
{
    return getWord(WORD_L);
}

//------------------------ Get right sample word.
uint16_t PCM1SubLine::getRight()
{
    return getWord(WORD_R);
}

//------------------------ Get part of the source video line.
uint8_t PCM1SubLine::getLinePart()
{
    return line_part;
}

//------------------------ Convert PCM data to string for debug.
std::string PCM1SubLine::dumpWordsString()
{
    std::string text_out;
    uint8_t ind, bit_pos;

    // Printing 13 bit words data.
    for(ind=WORD_L;ind<=WORD_R;ind++)
    {
        bit_pos = BITS_PER_WORD;
        do
        {
            bit_pos--;
            if((ind==WORD_L)&&(bit_pos>=(BITS_PER_WORD-picked_bits_left)))
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
    }

    return text_out;
}

//------------------------ Output full information about the line.
std::string PCM1SubLine::dumpContentString()
{
    std::string text_out;
    char c_buf[256];

    sprintf(c_buf, "F[%03u] L[%03u] P[%01u] ", (unsigned int)frame_number, line_number, line_part);
    text_out += c_buf;

    text_out += dumpWordsString();

    if(isCRCValid()==false)
    {
        text_out += " BD CRC!";
    }
    else
    {
        text_out += " CRC OK";
    }

    return text_out;
}
