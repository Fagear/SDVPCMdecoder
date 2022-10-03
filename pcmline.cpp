#include "pcmline.h"

PCMLine::PCMLine()
{
    help_stage = 0;
    this->clear();
}

PCMLine::PCMLine(const PCMLine &in_object)
{
    frame_number = in_object.frame_number;
    line_number = in_object.line_number;
    black_level = in_object.black_level;
    white_level = in_object.white_level;
    ref_low = in_object.ref_low;
    ref_level = in_object.ref_level;
    ref_high = in_object.ref_high;
    coords = in_object.coords;
    process_time = in_object.process_time;
    hysteresis_depth = in_object.hysteresis_depth;
    shift_stage = in_object.shift_stage;
    ref_level_sweeped = in_object.ref_level_sweeped;
    coords_sweeped = in_object.coords_sweeped;
    data_by_ext_tune = in_object.data_by_ext_tune;
    file_path = in_object.file_path;

    help_stage = in_object.help_stage;
    calc_crc = in_object.calc_crc;
    blk_wht_set = in_object.blk_wht_set;
    coords_set = in_object.coords_set;
    forced_bad = in_object.forced_bad;
    service_type = in_object.service_type;

    pixel_start = in_object.pixel_start;
    pixel_stop = in_object.pixel_stop;
    pixel_start_offset = in_object.pixel_start_offset;
    pixel_size_mult = in_object.pixel_size_mult;
    halfpixel_size_mult = in_object.halfpixel_size_mult;
}

PCMLine& PCMLine::operator= (const PCMLine &in_object)
{
    if(this==&in_object) return *this;

    frame_number = in_object.frame_number;
    line_number = in_object.line_number;
    black_level = in_object.black_level;
    white_level = in_object.white_level;
    ref_low = in_object.ref_low;
    ref_level = in_object.ref_level;
    ref_high = in_object.ref_high;
    coords = in_object.coords;
    process_time = in_object.process_time;
    hysteresis_depth = in_object.hysteresis_depth;
    shift_stage = in_object.shift_stage;
    ref_level_sweeped = in_object.ref_level_sweeped;
    coords_sweeped = in_object.coords_sweeped;
    data_by_ext_tune = in_object.data_by_ext_tune;
    file_path = in_object.file_path;

    help_stage = in_object.help_stage;
    calc_crc = in_object.calc_crc;
    blk_wht_set = in_object.blk_wht_set;
    coords_set = in_object.coords_set;
    forced_bad = in_object.forced_bad;
    service_type = in_object.service_type;

    pixel_start = in_object.pixel_start;
    pixel_stop = in_object.pixel_stop;
    pixel_start_offset = in_object.pixel_start_offset;
    pixel_size_mult = in_object.pixel_size_mult;
    halfpixel_size_mult = in_object.halfpixel_size_mult;

    return *this;
}

bool PCMLine::operator!= (const PCMLine &in_object)
{
    if(frame_number!=in_object.frame_number)
    {
        return false;
    }
    else if(line_number!=in_object.line_number)
    {
        return false;
    }
    else if(ref_level!=in_object.ref_level)
    {
        return false;
    }
    else
    {
        return true;
    }
}

//------------------------ Reset all fields to default.
void PCMLine::clear()
{
    frame_number = line_number = 0;
    black_level = white_level = 0;
    ref_low = ref_level = ref_high = 0;
    coords.clear();
    process_time = 0;
    hysteresis_depth = shift_stage = 0;
    ref_level_sweeped = coords_sweeped = data_by_ext_tune = false;
    file_path.clear();

    calc_crc = 0;
    blk_wht_set = coords_set = forced_bad = false;
    service_type = SRVLINE_NO;

    pixel_start = 0;
    pixel_stop = 1;
    pixel_start_offset = 0;
    pixel_size_mult = INT_CALC_MULT;
    halfpixel_size_mult = pixel_size_mult/INT_CALC_PIXEL_DIV;
}

//------------------------ Set service tag "new file opened".
void PCMLine::setServNewFile(std::string path)
{
    // Convert this line into service line.
    this->setServiceLine();
    // Set source file path.
    file_path = path;
    service_type = SRVLINE_NEW_FILE;
}

//------------------------ Set service tag "file ended".
void PCMLine::setServEndFile()
{
    // Convert this line into service line.
    this->setServiceLine();
    // Set service tag.
    service_type = SRVLINE_END_FILE;
}

//------------------------ Set service tag "filler line".
void PCMLine::setServFiller()
{
    // Convert this line into service line.
    this->setServiceLine();
    // Set service tag.
    service_type = SRVLINE_FILLER;
}

//------------------------ Set service tag "field ended".
void PCMLine::setServEndField()
{
    // Convert this line into service line.
    this->setServiceLine();
    // Set service tag.
    service_type = SRVLINE_END_FIELD;
}

//------------------------ Set service tag "frame ended".
void PCMLine::setServEndFrame()
{
    // Convert this line into service line.
    this->setServiceLine();
    // Set service tag.
    service_type = SRVLINE_END_FRAME;
}

//------------------------Set state of "from doubled-width source".
void PCMLine::setFromDoubledState(bool in_flag)
{
    coords.setDoubledState(in_flag);
}

//------------------------ Set state of BLACK and WHITE levels.
void PCMLine::setBWLevelsState(bool in_flag)
{
    blk_wht_set = in_flag;
}

//------------------------ Set state of data coordinates.
void PCMLine::setDataCoordinatesState(bool in_flag)
{
    coords_set = in_flag;
}

//------------------------ Set state of "data by reference sweep" flag.
void PCMLine::setSweepedReference(bool in_flag)
{
    ref_level_sweeped = in_flag;
}

//------------------------ Set state of "data by coordinates sweep" flag.
void PCMLine::setSweepedCoordinates(bool in_flag)
{
    coords_sweeped = in_flag;
}

//------------------------ Set invalid CRC in the line.
void PCMLine::setInvalidCRC()
{
    // Set stored in a word CRC to inverse of a calculated one.
    setSourceCRC(~calc_crc);
}

//------------------------ Set "forced to be invalid" flag.
void PCMLine::setForcedBad()
{
    forced_bad = true;
}

//------------------------ Set pixel count of the source [VideoLine].
void PCMLine::setSourcePixels(uint16_t in_start, uint16_t in_stop)
{
    if(in_stop>in_start)
    {
        if(getBitsBetweenDataCoordinates()<=(in_stop-in_start))
        {
            // Set pixel limits from the source video line.
            pixel_start = in_start;
            pixel_stop = in_stop;
        }
    }
}

//------------------------ Calculate and store int-multiplied PPB (pixel-per-bit) value (from [BITS_IN_LINE] bits span).
//------------------------ Pre-calculate coordinates table.
//------------------------ MUST be called after data coordinates are set and before any [findVideoPixel()] calls!
void PCMLine::calcPPB(CoordinatePair in_coords)
{
    // Calculate PPB for provided video line length.
    setPPB(in_coords);
    // Pre-calculate pixel coordinates for PCM line object.
    for(uint8_t i=0;i<PCM_LINE_MAX_PS_STAGES;i++)
    {
        calcCoordinates(i);
    }
}

//------------------------ Get PPB (pixep per bit) integer value.
uint8_t PCMLine::getPPB()
{
    return pixel_size_mult/INT_CALC_MULT;
}

//------------------------ Get PPB (pixep per bit) fraction value (integer after point).
uint8_t PCMLine::getPPBfrac()
{
    return pixel_size_mult%INT_CALC_MULT;
}

//------------------------ Get calculated coordinate of pixel in video line for requested PCM bit number.
//------------------------ Apply non-uniform (different for begining, center and end of the line) pixel-shifting to coordinates.
//------------------------ [setPPB()] or [calcPPB()] MUST be called before any [findVideoPixel()] calls!
uint16_t PCMLine::getVideoPixelC(uint8_t pcm_bit, uint8_t in_shift, uint8_t bit_ofs)
{
    int32_t video_pixel;
    int8_t bg_shift, ed_shift;

    // Apply constant bit offset.
    pcm_bit += bit_ofs;

    // Limit bit position.
    if(pcm_bit>=getBitsPerSourceLine())
    {
        pcm_bit = getBitsPerSourceLine()-1;
    }

    // Pixel = bit * pixels-per-bit (PPB).
    video_pixel = ((pcm_bit*pixel_size_mult)+halfpixel_size_mult);
#ifdef LB_ROUNDED_DIVS
    video_pixel = (video_pixel + INT_CALC_MULT/2)/INT_CALC_MULT;
#else
    video_pixel = video_pixel/INT_CALC_MULT;
#endif
    // Apply pixel offset to usefull data start.
    video_pixel = video_pixel+pixel_start_offset;

    // Get pixel-shift offsets.
    bg_shift = PIX_SH_BG_TBL[in_shift];
    ed_shift = PIX_SH_ED_TBL[in_shift];

    // Check if shift if uniform.
    if(bg_shift==ed_shift)
    {
        // Apply uniform pixel shift.
        video_pixel += bg_shift;
    }
    else
    {
        // Shift is non-uniform.
        // Check bit relative position.
        if(pcm_bit<getLeftShiftZoneBit())
        {
            // Left side (beginning) of the line.
            video_pixel += bg_shift;
        }
        else if(pcm_bit>getRightShiftZoneBit())
        {
            // Right side (ending) of the line.
            video_pixel += ed_shift;
        }
        // Middle part should not move with non-uniform shift.
    }

    // Correct if out of bounds.
    if(video_pixel<pixel_start)
    {
        video_pixel = pixel_start;
    }
    else if(video_pixel>=pixel_stop)
    {
        video_pixel = pixel_stop-1;
    }

    return (uint16_t)video_pixel;
}

//------------------------ Get re-calculated CRC for the data.
uint16_t PCMLine::getCalculatedCRC()
{
    return calc_crc;
}

//------------------------ Does this PCM line is of a type that uses markers to locate data?
bool PCMLine::canUseMarkers()
{
    if(getPCMType()==TYPE_STC007)
    {
        return true;
    }
    return false;
}

//------------------------ Were black and white levels set for the line?
bool PCMLine::hasBWSet()
{
    return blk_wht_set;
}

//------------------------ Were data coordinates set for the line?
bool PCMLine::hasDataCoordSet()
{
    return coords_set;
}

//------------------------ Was the line source doubled width?
bool PCMLine::isSourceDoubleWidth()
{
    return coords.isSourceDoubleWidth();
}

//------------------------ Is the line forced to be invalid?
bool PCMLine::isForcedBad()
{
    return forced_bad;
}

//------------------------ Was re-calculated CRC the same as the read one and was not forced to be a bad one?
bool PCMLine::isCRCValid()
{
    if((isForcedBad()==false)&&(isCRCValidIgnoreForced()!=false))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Was PCM data decoded with use of reference level sweep?
bool PCMLine::isDataByRefSweep()
{
    return ref_level_sweeped;
}

//------------------------ Was PCM data decoded with use of data coordinates sweep?
bool PCMLine::isDataByCoordSweep()
{
    return coords_sweeped;
}

//------------------------ Was PCM data decoded with external parameters from previous lines?
bool PCMLine::isDataBySkip()
{
    return data_by_ext_tune;
}

//------------------------ Check if line has any service tag.
bool PCMLine::isServiceLine()
{
    if(service_type==SRVLINE_NO)
    {
        return false;
    }
    else
    {
        return true;
    }
}

//------------------------ Check if line has service tag "new file opened".
bool PCMLine::isServNewFile()
{
    if(service_type==SRVLINE_NEW_FILE)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Check if line has service tag "file ended".
bool PCMLine::isServEndFile()
{
    if(service_type==SRVLINE_END_FILE)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Is this a filler line (from padding frame)?
bool PCMLine::isServFiller()
{
    if(service_type==SRVLINE_FILLER)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Check if line has service tag "field ended".
bool PCMLine::isServEndField()
{
    if(service_type==SRVLINE_END_FIELD)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Check if line has service tag "frame ended".
bool PCMLine::isServEndFrame()
{
    if(service_type==SRVLINE_END_FRAME)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Restart help output.
void PCMLine::helpDumpRestart()
{
    help_stage = 0;
}

//------------------------ Initialize CRC16 value.
void PCMLine::CRC16_init(uint16_t *CRC_data)
{
    (*CRC_data) = CRC_INIT;
}

//------------------------ Update CRC16 data by calculation.
uint16_t PCMLine::getCalcCRC16(uint16_t CRC_data, uint16_t in_data, uint8_t bit_cnt)
{
    // Cycle through all bits in the word (may be any number of bits from 1 to 16).
    for(uint8_t i=0;i<bit_cnt;i++)
    {
        // Check state of CRC's MSB.
        if((CRC_data&CRC_MAX_BIT)==0)
        {
            CRC_data = (CRC_data<<1);
            if((in_data&(1<<(bit_cnt-1)))!=0)
            {
                CRC_data = CRC_data^CRC_POLY;
            }
        }
        else
        {
            CRC_data = (CRC_data<<1);
            if((in_data&(1<<(bit_cnt-1)))==0)
            {
                CRC_data = CRC_data^CRC_POLY;
            }
        }
        // Shift input word once to MSB.
        in_data = (in_data<<1);
    }
    return CRC_data;
}

//------------------------ Convert PCM line into service tag.
void PCMLine::setServiceLine()
{
    uint16_t line;
    uint32_t frame;
    // Temporary save frame and line number.
    frame = frame_number;
    line = line_number;
    // Clear all fields.
    clear();
    // Restore frame and line number.
    frame_number = frame;
    line_number = line;
}

//------------------------ Calculate and store int-multiplied PPB (pixel-per-bit) value
//------------------------ from provided video line length (in pixels) and bit count between data coordinates.
void PCMLine::setPPB(CoordinatePair in_coords)
{
    uint8_t bit_count;
    // Get bit count for the PCM line.
    bit_count = getBitsBetweenDataCoordinates();
    // Calculate number of pixels with useful data in the video line.
    pixel_size_mult = in_coords.data_stop-in_coords.data_start;
    // PPB = pixels/bits.
    pixel_size_mult = (pixel_size_mult*INT_CALC_MULT + bit_count/2)/bit_count;
    // Store pixel offset of the first bit.
    pixel_start_offset = in_coords.data_start;
    // Calculate half of the pixel for bit center detection.
    halfpixel_size_mult = (pixel_size_mult + INT_CALC_PIXEL_DIV/2)/INT_CALC_PIXEL_DIV;
}
