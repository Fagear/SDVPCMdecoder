#include "frametrimset.h"

CoordinatePair::CoordinatePair()
{
    this->clear();
}

CoordinatePair::CoordinatePair(const CoordinatePair &in_object)
{
    reference = in_object.reference;
    data_start = in_object.data_start;
    data_stop = in_object.data_stop;
    from_doubled = in_object.from_doubled;
    not_sure = in_object.not_sure;
}

CoordinatePair& CoordinatePair::operator= (const CoordinatePair &in_object)
{
    if(this==&in_object) return *this;

    reference = in_object.reference;
    data_start = in_object.data_start;
    data_stop = in_object.data_stop;
    from_doubled = in_object.from_doubled;
    not_sure = in_object.not_sure;

    return *this;
}

bool CoordinatePair::operator== (const CoordinatePair &in_object)
{
    if((in_object.data_start==data_start)&&(in_object.data_stop==data_stop)&&(in_object.from_doubled==from_doubled))
    {
        return true;
    }
    return false;
}

bool CoordinatePair::operator!= (const CoordinatePair &in_object)
{
    if((in_object.data_start!=data_start)||(in_object.data_stop!=data_stop)||(in_object.from_doubled!=from_doubled))
    {
        return true;
    }
    return false;
}

CoordinatePair& CoordinatePair::operator- (const CoordinatePair &in_object)
{
    if(this==&in_object)
    {
        this->setToZero();
    }
    else
    {
        data_start -= in_object.data_start;
        data_stop -= in_object.data_stop;
    }
    return *this;
}

//------------------------ Compare operator to support std::sort().
bool CoordinatePair::operator< (const CoordinatePair &in_object)
{
    // 1st, sort by [data_start] field from min to max.
    if(data_start<in_object.data_start)
    {
        return true;
    }
    else if(data_start==in_object.data_start)
    {
        // 2nd, sort by [data_stop] field from max to min.
        if(data_stop>in_object.data_stop)
        {
            return true;
        }
        else if(data_stop==in_object.data_stop)
        {
            // 3rd, sort by [reference] field from max to min.
            if(reference<in_object.reference)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

//------------------------ Reset all fields.
void CoordinatePair::clear()
{
    reference = 0;
    data_start = NO_COORD_LEFT;
    data_stop = NO_COORD_RIGHT;
    from_doubled = not_sure = false;
}

//------------------------ Set data coordinates with minimal integrity check.
bool CoordinatePair::setCoordinates(int16_t in_start, int16_t in_stop)
{
    if(in_stop>in_start)
    {
        data_start = in_start;
        data_stop = in_stop;
        return true;
    }
    return false;
}

//------------------------ Set both coordinates to "0".
void CoordinatePair::setToZero()
{
    data_start = data_stop = 0;
    from_doubled = not_sure = false;
}

//------------------------ Set double-width flag.
void CoordinatePair::setDoubledState(bool in_flag)
{
    from_doubled = in_flag;
}

//------------------------ Substract both coordinates.
void CoordinatePair::substract(int16_t in_start, int16_t in_stop)
{
    data_start -= in_start;
    data_stop -= in_stop;
}

//------------------------ Get difference (delta) from last to first coordinate.
int16_t CoordinatePair::getDelta()
{
    return (data_stop-data_start);
}

//------------------------ Are data coordinates doubled?
bool CoordinatePair::isSourceDoubleWidth()
{
    return from_doubled;
}

//------------------------ Are both data coordinates set to valid values?
bool CoordinatePair::areValid()
{
    return ((data_start!=NO_COORD_LEFT)&&(data_stop!=NO_COORD_RIGHT)&&(data_start<data_stop));
}

//------------------------ Are both data coordinates set to "0".
bool CoordinatePair::areZeroed()
{
    return ((data_start==0)&&(data_stop==0));
}

//------------------------ Does coordinate delta exceed threshold? (used only for coordinates delta calculations)
bool CoordinatePair::hasDeltaWarning(uint8_t in_delta)
{
    return ((data_start<=-in_delta)||(data_start>=in_delta)
            ||(data_stop<=-in_delta)||(data_stop>=in_delta));
}



//------------------------ Frame binarization information.
FrameBinDescriptor::FrameBinDescriptor()
{
    this->clear();
}

FrameBinDescriptor::FrameBinDescriptor(const FrameBinDescriptor &in_object)
{
    frame_id = in_object.frame_id;
    line_length = in_object.line_length;
    lines_odd = in_object.lines_odd;
    lines_even = in_object.lines_even;
    lines_pcm_odd = in_object.lines_pcm_odd;
    lines_pcm_even = in_object.lines_pcm_even;
    lines_bad_odd = in_object.lines_bad_odd;
    lines_bad_even = in_object.lines_bad_even;
    lines_dup_odd = in_object.lines_dup_odd;
    lines_dup_even = in_object.lines_dup_even;
    data_coord = in_object.data_coord;
    time_odd = in_object.time_odd;
    time_even = in_object.time_even;
}

FrameBinDescriptor& FrameBinDescriptor::operator= (const FrameBinDescriptor &in_object)
{
    if(this==&in_object) return *this;

    frame_id = in_object.frame_id;
    line_length = in_object.line_length;
    lines_odd = in_object.lines_odd;
    lines_even = in_object.lines_even;
    lines_pcm_odd = in_object.lines_pcm_odd;
    lines_pcm_even = in_object.lines_pcm_even;
    lines_bad_odd = in_object.lines_bad_odd;
    lines_bad_even = in_object.lines_bad_even;
    lines_dup_odd = in_object.lines_dup_odd;
    lines_dup_even = in_object.lines_dup_even;
    data_coord = in_object.data_coord;
    time_odd = in_object.time_odd;
    time_even = in_object.time_even;

    return *this;
}

//------------------------ Reset all fields (for reuse).
void FrameBinDescriptor::clear()
{
    frame_id = 0;
    line_length = lines_odd = lines_even = lines_pcm_odd = lines_pcm_even = lines_bad_odd = lines_bad_even = lines_dup_odd = lines_dup_even = 0;
    data_coord.clear();
    time_odd = time_even = 0;
}

//------------------------ Get total number of lines of video in the frame.
uint16_t FrameBinDescriptor::totalLines()
{
    return (lines_odd+lines_even);
}

//------------------------ Get total number of lines with PCM data in the frame.
uint16_t FrameBinDescriptor::totalWithPCM()
{
    return (lines_pcm_odd+lines_pcm_even);
}

//------------------------ Get total number of lines with damaged lines in the frame.
uint16_t FrameBinDescriptor::totalWithBadCRCs()
{
    return (lines_bad_odd+lines_bad_even);
}

//------------------------ Get total number of duplicated lines in the frame.
uint16_t FrameBinDescriptor::totalDuplicated()
{
    return (lines_dup_odd+lines_dup_even);
}

//------------------------ Get total processing time of the frame.
uint32_t FrameBinDescriptor::totalProcessTime()
{
    return (time_odd+time_even);
}



//------------------------ Frame assembling attempt statistics.
FieldStitchStats::FieldStitchStats()
{
    this->clear();
}

FieldStitchStats::FieldStitchStats(const FieldStitchStats &in_object)
{
    index = in_object.index;
    valid = in_object.valid;
    silent = in_object.silent;
    unchecked = in_object.unchecked;
    broken = in_object.broken;
}

FieldStitchStats& FieldStitchStats::operator= (const FieldStitchStats &in_object)
{
    if(this==&in_object) return *this;

    index = in_object.index;
    valid = in_object.valid;
    silent = in_object.silent;
    unchecked = in_object.unchecked;
    broken = in_object.broken;

    return *this;
}

bool FieldStitchStats::operator!= (const FieldStitchStats &in_object)
{
    if((in_object.index!=index)||(in_object.valid!=valid)||(in_object.silent!=silent)||(in_object.unchecked!=unchecked)||(in_object.broken!=broken))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool FieldStitchStats::operator== (const FieldStitchStats &in_object)
{
    if((in_object.index==index)&&(in_object.valid==valid)&&(in_object.silent==silent)&&(in_object.unchecked==unchecked)&&(in_object.broken==broken))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Compare operator to support std::sort().
bool FieldStitchStats::operator< (const FieldStitchStats &in_object)
{
    // 1th, sort by [broken] field from min to max.
    if(broken<in_object.broken)
    {
        return true;
    }
    else if(broken==in_object.broken)
    {
        // 2st, sort by [valid] field from max to min.
        if(valid>in_object.valid)
        {
            return true;
        }
        else if(valid==in_object.valid)
        {
            // 3rd, sort by [unchecked] field from min to max.
            if(unchecked<in_object.unchecked)
            {
                return true;
            }
            else if(unchecked==in_object.unchecked)
            {
                // 4th, sort by [silent] field from min to max.
                if(silent<in_object.silent)
                {
                    return true;
                }
                else if(silent==in_object.silent)
                {
                    // 5th, sort by [index] field from min to max.
                    if(index<in_object.index)
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

//------------------------ Reset all fields.
void FieldStitchStats::clear()
{
    index = valid = 0;
    silent = unchecked = broken = 0xFF;
}



//------------------------ Frame assembling information.
FrameAsmDescriptor::FrameAsmDescriptor()
{
    this->clear();
}

FrameAsmDescriptor::FrameAsmDescriptor(const FrameAsmDescriptor &in_object)
{
    frame_number = in_object.frame_number;
    odd_std_lines = in_object.odd_std_lines;
    even_std_lines = in_object.even_std_lines;
    odd_data_lines = in_object.odd_data_lines;
    even_data_lines = in_object.even_data_lines;
    odd_valid_lines = in_object.odd_valid_lines;
    even_valid_lines = in_object.even_valid_lines;
    odd_top_data = in_object.odd_top_data;
    odd_bottom_data = in_object.odd_bottom_data;
    even_top_data = in_object.even_top_data;
    even_bottom_data = in_object.even_bottom_data;
    odd_sample_rate = in_object.odd_sample_rate;
    even_sample_rate = in_object.even_sample_rate;
    field_order = in_object.field_order;
    odd_emphasis = in_object.odd_emphasis;
    even_emphasis = in_object.even_emphasis;
    odd_ref = in_object.odd_ref;
    even_ref = in_object.even_ref;
    blocks_total = in_object.blocks_total;
    blocks_drop = in_object.blocks_drop;
    samples_drop = in_object.samples_drop;
    drawn = in_object.drawn;

    order_preset = in_object.order_preset;
    order_guessed = in_object.order_guessed;
    service_type = in_object.service_type;
    file_path = in_object.file_path;
}

FrameAsmDescriptor& FrameAsmDescriptor::operator= (const FrameAsmDescriptor &in_object)
{
    if(this==&in_object) return *this;

    frame_number = in_object.frame_number;
    odd_std_lines = in_object.odd_std_lines;
    even_std_lines = in_object.even_std_lines;
    odd_data_lines = in_object.odd_data_lines;
    even_data_lines = in_object.even_data_lines;
    odd_valid_lines = in_object.odd_valid_lines;
    even_valid_lines = in_object.even_valid_lines;
    odd_top_data = in_object.odd_top_data;
    odd_bottom_data = in_object.odd_bottom_data;
    even_top_data = in_object.even_top_data;
    even_bottom_data = in_object.even_bottom_data;
    odd_sample_rate = in_object.odd_sample_rate;
    even_sample_rate = in_object.even_sample_rate;
    field_order = in_object.field_order;
    odd_emphasis = in_object.odd_emphasis;
    even_emphasis = in_object.even_emphasis;
    odd_ref = in_object.odd_ref;
    even_ref = in_object.even_ref;
    blocks_total = in_object.blocks_total;
    blocks_drop = in_object.blocks_drop;
    samples_drop = in_object.samples_drop;
    drawn = in_object.drawn;

    order_preset = in_object.order_preset;
    order_guessed = in_object.order_guessed;
    service_type = in_object.service_type;
    file_path = in_object.file_path;

    return *this;
}

//------------------------ Reset all fields (for reuse).
void FrameAsmDescriptor::clear()
{
    frame_number = 0;
    odd_top_data = 0;
    odd_bottom_data = 0xFFFF;
    even_top_data = 0;
    even_bottom_data = 0xFFFF;
    this->clearMisc();
}

//------------------------ Partial reset.
void FrameAsmDescriptor::clearMisc()
{
    // Don't clear frame number and trimming.
    odd_std_lines = even_std_lines = odd_data_lines = even_data_lines = odd_valid_lines = even_valid_lines = 0;
    odd_sample_rate = even_sample_rate = 0;
    field_order = ORDER_UNK;
    odd_emphasis = even_emphasis = false;
    order_preset = order_guessed = false;
    drawn = false;
    service_type = SRV_NO;
    file_path.clear();
    this->clearAsmStats();
}

//------------------------ Reset only assembly statistics.
void FrameAsmDescriptor::clearAsmStats()
{
    odd_ref = even_ref = 0;
    blocks_total = blocks_drop = samples_drop = 0;
}

//------------------------
void FrameAsmDescriptor::setServNewFile(std::string path)
{
    // Convert this line into service line.
    this->setServiceTag();
    // Set source file path.
    file_path = path;
    service_type = SRV_NEW_FILE;
}

//------------------------
void FrameAsmDescriptor::setServEndFile()
{
    // Convert this line into service line.
    this->setServiceTag();
    service_type = SRV_END_FILE;
}

//------------------------ Clear "preset" flag.
void FrameAsmDescriptor::presetOrderClear()
{
    order_preset = false;
}

//------------------------ Set field order to TFF with "preset" flag.
void FrameAsmDescriptor::presetTFF()
{
    order_preset = true;
    order_guessed = false;
    field_order = ORDER_TFF;
}

//------------------------ Set field order to BFF with "preset" flag.
void FrameAsmDescriptor::presetBFF()
{
    order_preset = true;
    order_guessed = false;
    field_order = ORDER_BFF;
}

//------------------------ Reset field order (if "preset" flag is cleared).
void FrameAsmDescriptor::setOrderUnknown()
{
    if(order_preset==false)
    {
        field_order = ORDER_UNK;
        order_guessed = false;
    }
}

//------------------------ Set field order to TFF (if "preset" flag is cleared).
void FrameAsmDescriptor::setOrderTFF()
{
    if(order_preset==false)
    {
        field_order = ORDER_TFF;
    }
}

//------------------------ Set field order to BFF (if "preset" flag is cleared).
void FrameAsmDescriptor::setOrderBFF()
{
    if(order_preset==false)
    {
        field_order = ORDER_BFF;
    }
}

//------------------------ Set "guessed" flag.
void FrameAsmDescriptor::setOrderGuessed(bool in_flag)
{
    if(order_preset==false)
    {
        order_guessed = in_flag;
    }
}

//------------------------ Get file name for [SRV_NEW_FILE] service tag.
std::string FrameAsmDescriptor::getServFileName()
{
    return file_path;
}

//------------------------ Check if descriptor has any service tag.
bool FrameAsmDescriptor::hasServiceTag()
{
    if(service_type==SRV_NO)
    {
        return false;
    }
    return true;
}

//------------------------ Check if descriptor has service tag "new file opened".
bool FrameAsmDescriptor::isServNewFile()
{
    if(service_type==SRV_NEW_FILE)
    {
        return true;
    }
    return false;
}

//------------------------ Check if descriptor has service tag "file ended".
bool FrameAsmDescriptor::isServEndFile()
{
    if(service_type==SRV_END_FILE)
    {
        return true;
    }
    return false;
}

//------------------------ Check if field order is set.
bool FrameAsmDescriptor::isOrderSet()
{
    if((field_order==ORDER_TFF)||(field_order==ORDER_BFF))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Check if field order is preset.
bool FrameAsmDescriptor::isOrderPreset()
{
    return order_preset;
}

//------------------------ Check if field order is guessed.
bool FrameAsmDescriptor::isOrderGuessed()
{
    return order_guessed;
}

//------------------------ Check if field order is set to BFF.
bool FrameAsmDescriptor::isOrderBFF()
{
    if(field_order==ORDER_BFF)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Check if field order is set to TFF.
bool FrameAsmDescriptor::isOrderTFF()
{
    if(field_order==ORDER_TFF)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Return frame average reference level between two fields.
uint8_t FrameAsmDescriptor::getAvgRef()
{
    uint16_t temp_avg;
    if((odd_ref==0)&&(even_ref==0))
    {
        // No reference level data.
        temp_avg = 0;
    }
    else if(odd_ref==0)
    {
        // No reference level for odd field, keep data from the even field.
        temp_avg = even_ref;
    }
    else if(even_ref==0)
    {
        // No reference level for even field, keep data from the odd field.
        temp_avg = odd_ref;
    }
    else
    {
        // Average data from both fields.
        temp_avg = odd_ref + even_ref;
        temp_avg = temp_avg/2;
    }
    return (uint8_t)temp_avg;
}

//------------------------ Convert into service tag.
void FrameAsmDescriptor::setServiceTag()
{
    uint32_t frame_tmp;
    // Temporary save frame number.
    frame_tmp = frame_number;
    // Clear all fields.
    clear();
    // Restore frame number.
    frame_number = frame_tmp;
}


//------------------------ Frame assembling information for PCM-1.
FrameAsmPCM1::FrameAsmPCM1()
{
    this->clear();
}

FrameAsmPCM1::FrameAsmPCM1(const FrameAsmPCM1 &in_object) : FrameAsmDescriptor(in_object)
{
    // Copy base class fields.
    FrameAsmDescriptor::operator =(in_object);
    // Copy own fields.
    odd_top_padding = in_object.odd_top_padding;
    odd_bottom_padding = in_object.odd_bottom_padding;
    even_top_padding = in_object.even_top_padding;
    even_bottom_padding = in_object.even_bottom_padding;
    blocks_fix_bp = in_object.blocks_fix_bp;
}

FrameAsmPCM1& FrameAsmPCM1::operator= (const FrameAsmPCM1 &in_object)
{
    if(this==&in_object) return *this;

    // Copy base class fields.
    FrameAsmDescriptor::operator =(in_object);
    // Copy own fields.
    odd_top_padding = in_object.odd_top_padding;
    odd_bottom_padding = in_object.odd_bottom_padding;
    even_top_padding = in_object.even_top_padding;
    even_bottom_padding = in_object.even_bottom_padding;
    blocks_fix_bp = in_object.blocks_fix_bp;

    return *this;
}

//------------------------ Reset all fields.
void FrameAsmPCM1::clear()
{
    // Clear base class fields.
    FrameAsmDescriptor::clear();
    // Clear own fields.
    clearMisc();
}

//------------------------ Partial reset.
void FrameAsmPCM1::clearMisc()
{
    // Clear base class fields.
    FrameAsmDescriptor::clearMisc();
    // Clear own fields.
    odd_top_padding = odd_bottom_padding = even_top_padding = even_bottom_padding = 0;
    this->clearAsmStats();
}

//------------------------ Reset only assembly statistics.
void FrameAsmPCM1::clearAsmStats()
{
    // Clear base class fields.
    FrameAsmDescriptor::clearAsmStats();
    // Clear own fields.
    blocks_fix_bp = 0;
}



//------------------------ Frame assembling information for PCM-16x0.
FrameAsmPCM16x0::FrameAsmPCM16x0()
{
    this->clear();
}

FrameAsmPCM16x0::FrameAsmPCM16x0(const FrameAsmPCM16x0 &in_object) : FrameAsmDescriptor(in_object)
{
    // Copy base class fields.
    FrameAsmDescriptor::operator =(in_object);
    // Copy own fields.
    odd_top_padding = in_object.odd_top_padding;
    odd_bottom_padding = in_object.odd_bottom_padding;
    even_top_padding = in_object.even_top_padding;
    even_bottom_padding = in_object.even_bottom_padding;
    silence = in_object.silence;
    padding_ok = in_object.padding_ok;
    ei_format = in_object.ei_format;
    blocks_broken = in_object.blocks_broken;
    blocks_fix_bp = in_object.blocks_fix_bp;
    blocks_fix_p = in_object.blocks_fix_p;
    blocks_fix_cwd = in_object.blocks_fix_cwd;
}

FrameAsmPCM16x0& FrameAsmPCM16x0::operator= (const FrameAsmPCM16x0 &in_object)
{
    if(this==&in_object) return *this;

    // Copy base class fields.
    FrameAsmDescriptor::operator =(in_object);
    // Copy own fields.
    odd_top_padding = in_object.odd_top_padding;
    odd_bottom_padding = in_object.odd_bottom_padding;
    even_top_padding = in_object.even_top_padding;
    even_bottom_padding = in_object.even_bottom_padding;
    silence = in_object.silence;
    padding_ok = in_object.padding_ok;
    ei_format = in_object.ei_format;
    blocks_broken = in_object.blocks_broken;
    blocks_fix_bp = in_object.blocks_fix_bp;
    blocks_fix_p = in_object.blocks_fix_p;
    blocks_fix_cwd = in_object.blocks_fix_cwd;

    return *this;
}

//------------------------ Reset all fields.
void FrameAsmPCM16x0::clear()
{
    // Clear base class fields.
    FrameAsmDescriptor::clear();
    // Clear own fields.
    clearMisc();
}

//------------------------ Partial reset.
void FrameAsmPCM16x0::clearMisc()
{
    // Clear base class fields.
    FrameAsmDescriptor::clearMisc();
    // Clear own fields.
    odd_top_padding = odd_bottom_padding = even_top_padding = even_bottom_padding = 0;
    silence = true;
    padding_ok = false;
    ei_format = false;
    this->clearAsmStats();
}

//------------------------ Reset only assembly statistics.
void FrameAsmPCM16x0::clearAsmStats()
{
    // Clear base class fields.
    FrameAsmDescriptor::clearAsmStats();
    // Clear own fields.
    blocks_broken = blocks_fix_bp = blocks_fix_p = blocks_fix_cwd = 0;
}



//------------------------ Frame assembling information for STC-007.
FrameAsmSTC007::FrameAsmSTC007()
{
    this->clear();
}

FrameAsmSTC007::FrameAsmSTC007(const FrameAsmSTC007 &in_object) : FrameAsmDescriptor(in_object)
{
    // Copy base class fields.
    FrameAsmDescriptor::operator =(in_object);
    // Copy own fields.
    video_standard = in_object.video_standard;
    tff_cnt = in_object.tff_cnt;
    bff_cnt = in_object.bff_cnt;
    odd_resolution = in_object.odd_resolution;
    even_resolution = in_object.even_resolution;
    inner_padding = in_object.inner_padding;
    outer_padding = in_object.outer_padding;
    trim_ok = in_object.trim_ok;
    inner_padding_ok = in_object.inner_padding_ok;
    outer_padding_ok = in_object.outer_padding_ok;
    inner_silence = in_object.inner_silence;
    outer_silence = in_object.outer_silence;
    vid_std_preset = in_object.vid_std_preset;
    vid_std_guessed = in_object.vid_std_guessed;
    blocks_broken_field = in_object.blocks_broken_field;
    blocks_broken_seam = in_object.blocks_broken_seam;
    blocks_fix_p = in_object.blocks_fix_p;
    blocks_fix_q = in_object.blocks_fix_q;
    blocks_fix_cwd = in_object.blocks_fix_cwd;
    ctrl_index = in_object.ctrl_index;
    ctrl_hour = in_object.ctrl_hour;
    ctrl_minute = in_object.ctrl_minute;
    ctrl_second = in_object.ctrl_second;
    ctrl_field = in_object.ctrl_field;
}

FrameAsmSTC007& FrameAsmSTC007::operator= (const FrameAsmSTC007 &in_object)
{
    if(this==&in_object) return *this;

    // Copy base class fields.
    FrameAsmDescriptor::operator =(in_object);
    // Copy own fields.
    video_standard = in_object.video_standard;
    tff_cnt = in_object.tff_cnt;
    bff_cnt = in_object.bff_cnt;
    odd_resolution = in_object.odd_resolution;
    even_resolution = in_object.even_resolution;
    inner_padding = in_object.inner_padding;
    outer_padding = in_object.outer_padding;
    trim_ok = in_object.trim_ok;
    inner_padding_ok = in_object.inner_padding_ok;
    outer_padding_ok = in_object.outer_padding_ok;
    inner_silence = in_object.inner_silence;
    outer_silence = in_object.outer_silence;
    vid_std_preset = in_object.vid_std_preset;
    vid_std_guessed = in_object.vid_std_guessed;
    blocks_broken_field = in_object.blocks_broken_field;
    blocks_broken_seam = in_object.blocks_broken_seam;
    blocks_fix_p = in_object.blocks_fix_p;
    blocks_fix_q = in_object.blocks_fix_q;
    blocks_fix_cwd = in_object.blocks_fix_cwd;
    ctrl_index = in_object.ctrl_index;
    ctrl_hour = in_object.ctrl_hour;
    ctrl_minute = in_object.ctrl_minute;
    ctrl_second = in_object.ctrl_second;
    ctrl_field = in_object.ctrl_field;

    return *this;
}

//------------------------ Reset all fields.
void FrameAsmSTC007::clear()
{
    // Clear base class fields.
    FrameAsmDescriptor::clear();
    // Clear own fields.
    clearMisc();
}

//------------------------ Partial reset.
void FrameAsmSTC007::clearMisc()
{
    // Clear base class fields.
    FrameAsmDescriptor::clearMisc();
    // Clear own fields.
    video_standard = VID_UNKNOWN;
    tff_cnt = bff_cnt = 0;
    odd_resolution = even_resolution = 0;
    inner_padding = outer_padding = 0;
    trim_ok = false;
    inner_padding_ok = outer_padding_ok = false;
    inner_silence = outer_silence = true;
    vid_std_preset = vid_std_guessed = false;
    ctrl_index = ctrl_hour = ctrl_minute = ctrl_second = ctrl_field = -1;
    this->clearAsmStats();
}

//------------------------ Reset only assembly statistics.
void FrameAsmSTC007::clearAsmStats()
{
    // Clear base class fields.
    FrameAsmDescriptor::clearAsmStats();
    // Clear own fields.
    blocks_broken_field = blocks_broken_seam = blocks_fix_p = blocks_fix_q = blocks_fix_cwd = 0;
}

//------------------------ Update video standard if it is not preset.
void FrameAsmSTC007::updateVidStdSoft(uint8_t in_std)
{
    if(vid_std_preset==false)
    {
        if(in_std<VID_MAX)
        {
            video_standard = in_std;
        }
    }
}

//------------------------ Is address data set from Control Block?
bool FrameAsmSTC007::isAddressSet()
{
    return ((ctrl_index!=-1)||(ctrl_hour!=-1)||(ctrl_minute!=-1)||(ctrl_second!=-1)||(ctrl_field!=-1));
}
