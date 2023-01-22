#include "videoline.h"

VideoLine::VideoLine()
{
    this->clear();
}

VideoLine::VideoLine(const VideoLine &in_object)
{
    frame_number = in_object.frame_number;
    line_number = in_object.line_number;
    colors = in_object.colors;
    pixel_data = in_object.pixel_data;
    file_path = in_object.file_path;
    process_time = in_object.process_time;
    scan_done = in_object.scan_done;
    empty = in_object.empty;
    doubled = in_object.doubled;
    service_type = in_object.service_type;
}

VideoLine& VideoLine::operator= (const VideoLine &in_object)
{
    if(this==&in_object) return *this;

    frame_number = in_object.frame_number;
    line_number = in_object.line_number;
    colors = in_object.colors;
    pixel_data = in_object.pixel_data;
    file_path = in_object.file_path;
    process_time = in_object.process_time;
    scan_done = in_object.scan_done;
    empty = in_object.empty;
    doubled = in_object.doubled;
    service_type = in_object.service_type;

    return *this;
}

//------------------------ Reset object.
void VideoLine::clear()
{
    frame_number = 0;
    line_number = 0;
    colors = vid_preset_t::COLOR_BW;
    pixel_data.reserve(1024);
    file_path.clear();
    process_time = 0;
    scan_done = false;
    empty = false;
    doubled = false;
    setServNo();
}

//------------------------ Set length of the line (in pixels).
void VideoLine::setLength(uint16_t in_length)
{
    pixel_data.resize(in_length);
}

//------------------------ Set pixel brightness with boundary check.
void VideoLine::setBrighness(uint16_t pixel_index, uint8_t data)
{
    if(pixel_index<pixel_data.size())
    {
        pixel_data[pixel_index] = data;
    }
}

//------------------------ Set flag "line is empty" for fast skipping for padded lines.
void VideoLine::setEmpty(bool flag)
{
    empty = flag;
    pixel_data.clear();
}

//------------------------ Set flag "double width" to aid binarization.
void VideoLine::setDoubleWidth(bool flag)
{
    doubled = flag;
}

//------------------------ Set service flag to none.
void VideoLine::setServNo()
{
    service_type = SRVLINE_NO;
}

//------------------------ Set service flag "new file opened".
void VideoLine::setServNewFile(std::string path)
{
    file_path = path;
    service_type = SRVLINE_NEW_FILE;
    setEmpty(true);
}

//------------------------ Set service flag "file ended".
void VideoLine::setServEndFile()
{
    service_type = SRVLINE_END_FILE;
    setEmpty(true);
}

//------------------------ Set service flag "filler line".
void VideoLine::setServFiller()
{
    service_type = SRVLINE_FILLER;
    setEmpty(true);
}

//------------------------ Set service flag "field ended".
void VideoLine::setServEndField()
{
    service_type = SRVLINE_END_FIELD;
    setEmpty(true);
}

//------------------------ Set service flag "frame ended".
void VideoLine::setServEndFrame()
{
    service_type = SRVLINE_END_FRAME;
    setEmpty(true);
}

//------------------------ Check if line is set to "empty".
bool VideoLine::isEmpty()
{
    return empty;
}

//------------------------ Check if line is set to "doubled".
bool VideoLine::isDoubleWidth()
{
    return doubled;
}

//------------------------ Check if line has any service tag.
bool VideoLine::isServiceLine()
{
    if(service_type==SRVLINE_NO)
    {
        return false;
    }
    return true;
}

//------------------------ Check if line has service tag "new file opened".
bool VideoLine::isServNewFile()
{
    if(service_type==SRVLINE_NEW_FILE)
    {
        return true;
    }
    return false;
}

//------------------------ Check if line has service tag "file ended".
bool VideoLine::isServEndFile()
{
    if(service_type==SRVLINE_END_FILE)
    {
        return true;
    }
    return false;
}

//------------------------ Check if line has service tag "filler line".
bool VideoLine::isServFiller()
{
    if(service_type==SRVLINE_FILLER)
    {
        return true;
    }
    return false;
}

//------------------------ Check if line has service tag "field ended".
bool VideoLine::isServEndField()
{
    if(service_type==SRVLINE_END_FIELD)
    {
        return true;
    }
    return false;
}

//------------------------ Check if line has service tag "frame ended".
bool VideoLine::isServEndFrame()
{
    if(service_type==SRVLINE_END_FRAME)
    {
        return true;
    }
    return false;
}
