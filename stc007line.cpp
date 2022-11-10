#include "stc007line.h"

STC007Line::STC007Line()
{
    this->clear();
}

STC007Line::STC007Line(const STC007Line &in_object) : PCMLine(in_object)
{
    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    mark_st_stage = in_object.mark_st_stage;
    mark_ed_stage = in_object.mark_ed_stage;
    marker_start_bg_coord = in_object.marker_start_bg_coord;
    marker_start_ed_coord = in_object.marker_start_ed_coord;
    marker_stop_ed_coord = in_object.marker_stop_ed_coord;
    // Copy data words.
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        words[index] = in_object.words[index];
        word_crc[index] = in_object.word_crc[index];
        word_valid[index] = in_object.word_valid[index];
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

STC007Line& STC007Line::operator= (const STC007Line &in_object)
{
    if(this==&in_object) return *this;

    // Copy base class fields.
    PCMLine::operator =(in_object);
    // Copy own fields.
    mark_st_stage = in_object.mark_st_stage;
    mark_ed_stage = in_object.mark_ed_stage;
    marker_start_bg_coord = in_object.marker_start_bg_coord;
    marker_start_ed_coord = in_object.marker_start_ed_coord;
    marker_stop_ed_coord = in_object.marker_stop_ed_coord;
    // Copy data words.
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        words[index] = in_object.words[index];
        word_crc[index] = in_object.word_crc[index];
        word_valid[index] = in_object.word_valid[index];
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

//------------------------ Reset all fields to default.
void STC007Line::clear()
{
    // Clear base class fields.
    PCMLine::clear();
    // Clear own fields.
    mark_st_stage = MARK_ST_START;
    mark_ed_stage = MARK_ED_START;
    marker_start_bg_coord = marker_start_ed_coord = marker_stop_ed_coord = 0;
    // Reset data words.
    setSilent();
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        // Reset per-word flags used for CWD.
        word_crc[index] = false;
        word_valid[index] = false;
    }
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

//------------------------ Set service flag "PCM control block".
void STC007Line::setServCtrlBlk()
{
    uint16_t id_word, address1_word, address2_word, ctrl_flags;
    uint16_t line;
    uint32_t frame;
    // Store Control Block data before clearing the line.
    id_word = words[WORD_CB_ID];
    address1_word = words[WORD_CB_ADDR1];
    address2_word = words[WORD_CB_ADDR2];
    ctrl_flags = words[WORD_CB_CTRL];
    // Save frame and line number.
    frame = frame_number;
    line = line_number;
    // Clear all fields.
    clear();
    // Restore frame and line number.
    frame_number = frame;
    line_number = line;
    // Restore Control Block data.
    words[WORD_CB_ID] = id_word;
    words[WORD_CB_ADDR1] = address1_word;
    words[WORD_CB_ADDR2] = address2_word;
    words[WORD_CB_CTRL] = ctrl_flags;
    // Update CRC.
    calcCRC();
    setSourceCRC(getCalculatedCRC());
    // Set service tag.
    service_type = SRVLINE_CTRL_BLOCK;
}

//------------------------ Set word with source CRC.
void STC007Line::setSourceCRC(uint16_t in_crc)
{
    words[WORD_CRCC_SH0] = in_crc;
}

//------------------------ Zero out all data words.
void STC007Line::setSilent()
{
    for(uint8_t i=WORD_L_SH0;i<=WORD_Q_SH336;i++)
    {
        words[i] = 0;
    }
    calcCRC();
}

//------------------------ Copy 14-bit word into object with its state.
void STC007Line::setWord(uint8_t index, uint16_t in_word, bool in_valid)
{
    if(index<WORD_CNT)
    {
        words[index] = in_word&0x3FFF;
        word_crc[index] = word_valid[index] = in_valid;
    }
}

//------------------------ Set word as fixed (no CRC error).
void STC007Line::setFixed(uint8_t index)
{
    if(index<WORD_CNT)
    {
        word_valid[index] = true;
    }
}

//------------------------ Force set marker detection stages to "markers exist" to bypass checks.
void STC007Line::forceMarkersOk()
{
    mark_st_stage = MARK_ST_BOT_2;
    mark_ed_stage = MARK_ED_LEN_OK;
}

//------------------------ Apply state of the line CRC to per-word state.
void STC007Line::applyCRCStatePerWord()
{
    for(uint8_t index=0;index<WORD_CNT;index++)
    {
        word_crc[index] = word_valid[index] = isCRCValid();
    }
}

//------------------------ Get number of data bits in the source line by the standard.
uint8_t STC007Line::getBitsPerSourceLine()
{
    return BITS_IN_LINE;
}

//------------------------ Get number of data bits between data coordinates (for PPB calculation).
uint8_t STC007Line::getBitsBetweenDataCoordinates()
{
    // Data coordinates capture 2nd, 3rd, 4th bits of START marker and 1 empty bit before STOP marker.
    return (3+BITS_PCM_DATA+1);
}

//------------------------ Get highest bit number for the left part of the line for pixel-shifting.
uint8_t STC007Line::getLeftShiftZoneBit()
{
    return BITS_LEFT_SHIFT;
}

//------------------------ Get lowest bit number for the right part of the line for pixel-shifting.
uint8_t STC007Line::getRightShiftZoneBit()
{
    return BITS_RIGHT_SHIFT;
}

//------------------------ Get pre-calculated coordinate of pixel in video line for requested PCM bit number and pixel-shifting stage.
//------------------------ [calcPPB()] MUST be called before any [findVideoPixel()] calls!
uint16_t STC007Line::getVideoPixelT(uint8_t pcm_bit, uint8_t shift_stage)
{
    return pixel_coordinates[shift_stage][pcm_bit];
}

//------------------------ Re-calculate CRCC for all words in the line.
void STC007Line::calcCRC()
{
    CRC16_init(&calc_crc);
    for(uint8_t i=WORD_L_SH0;i<=WORD_Q_SH336;i++)
    {
        calc_crc = getCalcCRC16(calc_crc, words[i], BITS_PER_WORD);
    }
#ifdef STC_LINE_EN_DBG_OUT
    QString log_line;
    log_line.sprintf("[STCL] Calculating CRC... %04x", calc_crc);
    qInfo()<<log_line;
#endif
}

//------------------------ Get CRC that was read from the source.
uint16_t STC007Line::getSourceCRC()
{
    return words[WORD_CRCC_SH0];
}

//------------------------ Get the type of PCM to determine class derived from [PCMLine];
uint8_t STC007Line::getPCMType()
{
    return TYPE_STC007;
}

//------------------------ Convert one 14-bit word to a 16-bit sample.
int16_t STC007Line::getSample(uint8_t index)
{
    uint16_t data_word;

    if(index<WORD_P_SH288)
    {
        data_word = words[index];
        // Convert 14-bit word to a 16-bit word.
        data_word = (data_word<<2);
    }
    else
    {
        // Index out-of-bounds.
        data_word = 0;
    }
    return (int16_t)data_word;
}

//------------------------ Get ID part of Control Block.
//------------------------ Negative result means an error.
int16_t STC007Line::getCtrlID()
{
    if(isServCtrlBlk()!=false)
    {
        return (words[WORD_CB_ID]&0x3FFF);
    }
    return -1;
}

//------------------------ Get Index code from Address part of Control Block.
//------------------------ Negative result means an error.
int8_t STC007Line::getCtrlIndex()
{
    if(isServCtrlBlk()!=false)
    {
        uint16_t tmp_ctrl;
        tmp_ctrl = words[WORD_CB_ADDR1];
        tmp_ctrl = (tmp_ctrl>>8);
        tmp_ctrl = tmp_ctrl&0x3F;
        return (int8_t)tmp_ctrl;
    }
    return -1;
}

//------------------------ Get Index code from Address part of Control Block.
//------------------------ Negative result means an error.
int8_t STC007Line::getCtrlHour()
{
    if(isServCtrlBlk()!=false)
    {
        uint16_t tmp_ctrl;
        tmp_ctrl = words[WORD_CB_ADDR1];
        tmp_ctrl = (tmp_ctrl>>4);
        tmp_ctrl = tmp_ctrl&0x0F;
        return (int8_t)tmp_ctrl;
    }
    return -1;
}

//------------------------ Get Index code from Address part of Control Block.
//------------------------ Negative result means an error.
int8_t STC007Line::getCtrlMinute()
{
    if(isServCtrlBlk()!=false)
    {
        uint16_t tmp_ctrl;
        tmp_ctrl = words[WORD_CB_ADDR2];
        tmp_ctrl = (tmp_ctrl>>12);
        tmp_ctrl = tmp_ctrl&0x03;
        tmp_ctrl += ((words[WORD_CB_ADDR1]&0x0F)<<2);
        return (int8_t)tmp_ctrl;
    }
    return -1;
}

//------------------------ Get Index code from Address part of Control Block.
//------------------------ Negative result means an error.
int8_t STC007Line::getCtrlSecond()
{
    if(isServCtrlBlk()!=false)
    {
        uint16_t tmp_ctrl;
        tmp_ctrl = words[WORD_CB_ADDR2];
        tmp_ctrl = (tmp_ctrl>>6);
        tmp_ctrl = tmp_ctrl&0x3F;
        return (int8_t)tmp_ctrl;
    }
    return -1;
}

//------------------------ Get Index code from Address part of Control Block.
//------------------------ Negative result means an error.
int8_t STC007Line::getCtrlFieldCode()
{
    if(isServCtrlBlk()!=false)
    {
        uint16_t tmp_ctrl;
        tmp_ctrl = words[WORD_CB_ADDR2];
        tmp_ctrl = tmp_ctrl&0x3F;
        return (int8_t)tmp_ctrl;
    }
    return -1;
}

//------------------------ Was PCM START-marker found in the line?
bool STC007Line::hasStartMarker()
{
    if(mark_st_stage==MARK_ST_BOT_2)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Was PCM STOP-marker found in the line?
bool STC007Line::hasStopMarker()
{
    if(mark_ed_stage==MARK_ED_LEN_OK)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Were both PCM markers found in the line?
bool STC007Line::hasMarkers()
{
    if((mark_st_stage==MARK_ST_BOT_2)&&(mark_ed_stage==MARK_ED_LEN_OK))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Does provided line have the same words?
bool STC007Line::hasSameWords(STC007Line *in_line)
{
    bool equal;
    equal = true;
    if(in_line==NULL)
    {
        return false;
    }
    else
    {
        for(uint8_t index=WORD_L_SH0;index<=WORD_Q_SH336;index++)
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

//------------------------ Does this line contain control signal block (and no audio data)?
bool STC007Line::hasControlBlock()
{
    if((words[WORD_CB_CUE1]==0x3333)&&(words[WORD_CB_CUE2]==0x0CCC)
        &&(words[WORD_CB_CUE3]==0x3333)&&(words[WORD_CB_CUE4]==0x0CCC))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Is CRC valid (re-calculated CRC is the same as read one)?
bool STC007Line::isCRCValidIgnoreForced()
{
    if(getCalculatedCRC()==getSourceCRC())
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Prohibition of digital dubbing.
bool STC007Line::isCtrlCopyProhibited()
{
    if(isServCtrlBlk()!=false)
    {
        if((words[WORD_CB_CTRL]&CTRL_COPY_MASK)!=0)
        {
            return true;
        }
    }
    return false;
}

//------------------------ Presence of P-word.
bool STC007Line::isCtrlEnabledP()
{
    if(isServCtrlBlk()!=false)
    {
        if((words[WORD_CB_CTRL]&CTRL_EN_P_MASK)==0)
        {
            return true;
        }
    }
    return false;
}

//------------------------ Presence of Q-word (14/16-bit mode).
bool STC007Line::isCtrlEnabledQ()
{
    if(isServCtrlBlk()!=false)
    {
        if((words[WORD_CB_CTRL]&CTRL_EN_Q_MASK)==0)
        {
            return true;    // 14-bit mode
        }
    }
    return false;   // 16-bit mode (PCM-F1 format)
}

//------------------------ Pre-emphasis of audio.
bool STC007Line::isCtrlEnabledEmphasis()
{
    if(isServCtrlBlk()!=false)
    {
        if((words[WORD_CB_CTRL]&CTRL_EMPH_MASK)==0)
        {
            return true;
        }
    }
    return false;
}

//------------------------ Is audio sample near zero value?
bool STC007Line::isNearSilence(uint8_t index)
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
bool STC007Line::isAlmostSilent()
{
    bool silent;
    silent = false;
    // Check if both channels are close to silence.
    if(((isNearSilence(WORD_L_SH0)!=false)||(isNearSilence(WORD_L_SH95)!=false)||(isNearSilence(WORD_L_SH190)!=false))&&
        ((isNearSilence(WORD_R_SH48)!=false)||(isNearSilence(WORD_R_SH143)!=false)||(isNearSilence(WORD_R_SH238)!=false)))
    {
        silent = true;
    }
    return silent;
}

//------------------------ Are all audio words zeroed?
bool STC007Line::isSilent()
{
    bool zero;
    zero = true;
    for(uint8_t index=WORD_L_SH0;index<=WORD_R_SH238;index++)
    {
        if(getSample(index)!=0)
        {
            zero = false;
            break;
        }
    }
    return zero;
}

//------------------------ Check if the line was fixed by CWD.
bool STC007Line::isFixedByCWD()
{
    if(isCRCValid()!=false)
    {
        for(uint8_t index=WORD_L_SH0;index<=WORD_Q_SH336;index++)
        {
            if((word_crc[index]==false)&&(word_valid[index]!=false))
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------ Check if line has service tag "PCM control block".
bool STC007Line::isServCtrlBlk()
{
    if(service_type==SRVLINE_CTRL_BLOCK)
    {
        return true;
    }
    return false;
}

//------------------------ Check if word is marked after binarization with CRC as "not damaged".
bool STC007Line::isWordCRCOk(uint8_t index)
{
    if(index<WORD_CNT)
    {
        if(forced_bad==false)
        {
            return word_crc[index];
        }
        return false;
    }
    return false;
}

//------------------------ Check if word is safe to playback (not damaged or fixed by ECC).
bool STC007Line::isWordValid(uint8_t index)
{
    if(index<WORD_CNT)
    {
        if(forced_bad==false)
        {
            return word_valid[index];
        }
        return false;
    }
    return false;
}

//------------------------ Get one word.
uint16_t STC007Line::getWord(uint8_t index)
{
    if(index<WORD_CNT)
    {
        return words[index];
    }
    return 0;
}

//------------------------ Convert PCM data to string for debug.
std::string STC007Line::dumpWordsString()
{
    std::string text_out;
    uint8_t ind, bit_pos;
    // Printing header.
    if(hasStartMarker()==false)
    {
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
    }
    else
    {
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ZERO;
    }

    // Printing 14 bit words data.
    for(ind=WORD_L_SH0;ind<=WORD_Q_SH336;ind++)
    {
        if(isWordCRCOk(ind)==false)
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
            if((words[ind]&(1<<bit_pos))==0)
            {
                if(isWordValid(ind)==false)
                {
                    text_out += DUMP_BIT_ZERO_BAD;
                }
                else
                {
                    text_out += DUMP_BIT_ZERO;
                }
            }
            else
            {
                if(isWordValid(ind)==false)
                {
                    text_out += DUMP_BIT_ONE_BAD;
                }
                else
                {
                    text_out += DUMP_BIT_ONE;
                }
            }
        }
        while(bit_pos>0);
        if(isWordCRCOk(ind)==false)
        {
            text_out += DUMP_WBRR_BAD;
        }
        else
        {
            text_out += DUMP_WBRR_OK;
        }
    }
    // Printing 16 bit CRCC data.
    if(isWordCRCOk(WORD_CRCC_SH0)==false)
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
        if((getSourceCRC()&(1<<bit_pos))==0)
        {
            if(isWordValid(WORD_CRCC_SH0)==false)
            {
                text_out += DUMP_BIT_ZERO_BAD;
            }
            else
            {
                text_out += DUMP_BIT_ZERO;
            }
        }
        else
        {
            if(isWordValid(WORD_CRCC_SH0)==false)
            {
                text_out += DUMP_BIT_ONE_BAD;
            }
            else
            {
                text_out += DUMP_BIT_ONE;
            }
        }
    }
    while(bit_pos>0);
    if(isWordCRCOk(WORD_CRCC_SH0)==false)
    {
        text_out += DUMP_WBRR_BAD;
    }
    else
    {
        text_out += DUMP_WBRR_OK;
    }
    // Printing footer.
    if(hasStopMarker()==false)
    {
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ZERO;
    }
    else
    {
        text_out += DUMP_BIT_ZERO;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ONE;
        text_out += DUMP_BIT_ONE;
    }

    return text_out;
}

//------------------------ Output full information about the line.
std::string STC007Line::dumpContentString()
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
        else if(isServCtrlBlk()!=false)
        {
            sprintf(c_buf, "F[%03u] SERVICE LINE: Control Block, ID=[%05d], Index=[%02d], Hour=[%02d], Min.=[%02d], Sec.=[%02d], Field=[%02d], CTRL=[0x%04X]: ",
                    (unsigned int)frame_number,
                    getCtrlID(),
                    getCtrlIndex(),
                    getCtrlHour(),
                    getCtrlMinute(),
                    getCtrlSecond(),
                    getCtrlFieldCode(),
                    words[WORD_CB_CTRL]);
            text_out += c_buf;
            if(isCtrlEnabledP()==false)
            {
                text_out += "P-code: OFF, ";
            }
            else
            {
                text_out += "P-code: ON,  ";
            }
            if(isCtrlEnabledQ()==false)
            {
                text_out += "Q-code: OFF, ";
            }
            else
            {
                text_out += "Q-code: ON,  ";
            }
            if(isCtrlEnabledEmphasis()==false)
            {
                text_out += "Emph.: OFF, ";
            }
            else
            {
                text_out += "Emph.: ON,  ";
            }
            if(isCtrlCopyProhibited()==false)
            {
                text_out += "Copy: ALLOW";
            }
            else
            {
                text_out += "Copy: DENY ";
            }
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
            sprintf(c_buf, "R>[%03u|%03u|%03u] S", ref_low, ref_level, ref_high);
        }
        else if(isDataBySkip()!=false)
        {
            sprintf(c_buf, "R=[%03u|%03u|%03u] S", ref_low, ref_level, ref_high);
        }
        else
        {
            sprintf(c_buf, "R?[%03u|%03u|%03u] S", ref_low, ref_level, ref_high);
        }
        text_out += c_buf;
        if(hasStartMarker()==false)
        {
            sprintf(c_buf, "%01u", mark_st_stage);
            text_out += c_buf;
        }
        else
        {
            text_out += "+";
        }
        if((isDataBySkip()!=false)&&(hasStartMarker()!=false))
        {
            sprintf(c_buf, "[%02d|", marker_start_bg_coord);
        }
        else
        {
            sprintf(c_buf, "[%02d:", marker_start_bg_coord);
        }
        text_out += c_buf;
        if(coords.areValid()==false)
        {
            text_out += "NA] ";
        }
        else
        {
            sprintf(c_buf, "%02d] ", coords.data_start);
            text_out += c_buf;
        }
        text_out += "["+dumpWordsString()+"] E";
        if(hasStopMarker()==false)
        {
            sprintf(c_buf, "%01d", mark_ed_stage);
            text_out += c_buf;
        }
        else
        {
            text_out += "+";
        }
        if(coords.areValid()==false)
        {
            text_out += "[ N/A";
        }
        else
        {
            sprintf(c_buf, "[%04d", coords.data_stop);
            text_out += c_buf;
        }
        if((isDataBySkip()!=false)&&(hasStopMarker()!=false))
        {
            sprintf(c_buf, "|%04d] ", marker_stop_ed_coord);
        }
        else
        {
            sprintf(c_buf, ":%04d] ", marker_stop_ed_coord);
        }
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
            if(isForcedBad()!=false)
            {
                sprintf(c_buf, "BD CRC! [0x%04X] (forced)", getCalculatedCRC());
                text_out += c_buf;
            }
            else if(isFixedByCWD()!=false)
            {
                sprintf(c_buf, "FIXED   [0x%04X]", getCalculatedCRC());
                text_out += c_buf;
            }
            else
            {
                sprintf(c_buf, "CRC OK  [0x%04X]", getCalculatedCRC());
                text_out += c_buf;
            }
        }
        else if(hasMarkers()!=false)
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
std::string STC007Line::helpDumpNext()
{
    std::string text_out;
    if(help_stage<STC_LINE_HELP_SIZE)
    {
        text_out = STC007_HELP_LIST[help_stage];
        help_stage++;
    }
    else
    {
        text_out.clear();
    }
    return text_out;
}

//------------------------ Pre-calculate pixel coordinates list for given pixel shift stage.
void STC007Line::calcCoordinates(uint8_t in_shift)
{
    for(uint8_t bit=0;bit<BITS_PCM_DATA;bit++)
    {
        pixel_coordinates[in_shift][bit] = PCMLine::getVideoPixelC(bit, in_shift, 3);
    }
}
