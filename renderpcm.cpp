#include "renderpcm.h"

RenderPCM::RenderPCM(QObject *parent)
{
    Q_UNUSED(parent);

    live_pb = true;
    frame_time_lim = TIME_NTSC;
    frame_number = 0;
    fill_line_num = 0;
    provided_width = 0;
    provided_heigth = 0;

    img_data = new QImage(640, 480, QImage::Format_RGB32);
    img_data->fill(Qt::black);

    pix_data = QPixmap::fromImage(*img_data);

    qInfo()<<"[REN] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

RenderPCM::~RenderPCM()
{
    if(img_data!=NULL)
    {
        delete img_data;
    }
    qInfo()<<"[REN] Renderer destroyed";
}

//------------------------ Reset frame state.
void RenderPCM::resetFrame()
{
    // Reset filling line.
    fill_line_num = 0;
}

//------------------------ Report thread ID.
void RenderPCM::dumpThreadDebug()
{
    qInfo()<<"[REN] Current thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
}

//------------------------ Enable/disable frame pacing for live playback.
void RenderPCM::setLivePlay(bool in_flag)
{
    live_pb = in_flag;
}

//------------------------ Set frame time for live playback.
void RenderPCM::setFrameTime(uint8_t in_standard)
{
    if(in_standard==FrameAsmDescriptor::VID_PAL)
    {
        frame_time_lim = TIME_PAL;
    }
    else
    {
        frame_time_lim = TIME_NTSC;
    }
}

//------------------------ Resize only vertical by video standard.
void RenderPCM::setLineCount(uint8_t in_standard)
{
    if(in_standard==FrameAsmDescriptor::VID_NTSC)
    {
        resizeToFrame(provided_width, (LINES_PER_NTSC_FIELD*2));
    }
    else if(in_standard==FrameAsmDescriptor::VID_PAL)
    {
        resizeToFrame(provided_width, (LINES_PER_PAL_FIELD*2));
    }
    else
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] Unknown video standard provided:"<<in_standard;
#endif
        resizeToFrame(provided_width, 650);
    }
}

//------------------------ Resize canvas to new dimensions.
void RenderPCM::resizeToFrame(uint16_t in_width, uint16_t in_height)
{
    provided_width = in_width;
    provided_heigth = in_height;
    if((img_data->width()!=provided_width)||(img_data->height()!=provided_heigth))
    {
        pix_data.fill(Qt::black);
        // Resize container if required.
        if(img_data!=NULL)
        {
            delete img_data;
        }
        img_data = new QImage(provided_width, provided_heigth, QImage::Format_RGB32);
        qInfo()<<"[REN] Canvas resized to:"<<provided_width<<"x"<<provided_heigth;
    }
}

//------------------------ Prepare to receive new frame with different resolution.
void RenderPCM::startNewFrame(uint16_t in_width, uint16_t in_height)
{
#ifdef VIS_EN_DBG_OUT
    qInfo()<<"[REN] New frame"<<in_width<<"x"<<in_height<<"started";
#endif
    resizeToFrame(in_width, in_height);
    resetFrame();
}

//------------------------ Start new frame for source video.
void RenderPCM::startVideoFrame()
{
    startNewFrame(768, 650);
}

//------------------------ Start new frame for PCM-1 binarized lines.
void RenderPCM::startPCM1Frame()
{
    startNewFrame((PPB_PCM1LINE*PCM1Line::BITS_IN_LINE), (PCM1DataStitcher::LINES_PF*2));
}

//------------------------ Start new frame for PCM-1 binarized sub-lines.
void RenderPCM::startPCM1SubFrame()
{
    startNewFrame((PPB_PCM1SUBLINE*(PCM1Line::BITS_IN_LINE-PCM1Line::BITS_PER_CRC)), (PCM1DataStitcher::LINES_PF*2));
}

//------------------------ Start new frame for PCM-16x0 binarized lines.
void RenderPCM::startPCM1600Frame()
{
    startNewFrame((PPB_PCM1600SUBLINE*PCM16X0SubLine::BITS_IN_LINE), (PCM16X0DataStitcher::LINES_PF*2));
}

//------------------------ Start new frame for STC-007 binarized lines.
void RenderPCM::startSTC007NTSCFrame()
{
    startNewFrame((PPB_STC007LINE*STC007Line::BITS_IN_LINE), (STC007DataStitcher::LINES_PF_NTSC*2));
}

//------------------------ Start new frame for STC-007 binarized lines.
void RenderPCM::startSTC007PALFrame()
{
    startNewFrame((PPB_STC007LINE*STC007Line::BITS_IN_LINE), (STC007DataStitcher::LINES_PF_PAL*2));
}

//------------------------ Start new frame for PCM-1 data blocks.
void RenderPCM::startPCM1DBFrame()
{
    startNewFrame(PPB_PCM1BLK*(WPL_PCM1BLK+3+WPL_PCM1BLK*16+4), (PCM1DataBlock::SAMPLE_CNT/WPL_PCM1BLK)*PCM1DataBlock::INT_BLK_PER_FIELD*2);
}

//------------------------ Start new frame for PCM-16x0 data blocks.
void RenderPCM::startPCM1600DBFrame()
{
    startNewFrame(PPB_PCM1600BLK*(9+PCM16X0SubLine::BITS_PER_WORD*PCM16X0DataBlock::SAMPLE_CNT+8), (PCM16X0DataStitcher::LINES_PF*2));
}

//------------------------ Start new frame for STC-007 data blocks.
void RenderPCM::startSTC007DBFrame()
{
    startNewFrame(PPB_STC007BLK*(6+STC007Line::BITS_PER_F1_WORD*STC007DataBlock::SAMPLE_CNT+7), (STC007DataStitcher::LINES_PF_NTSC*2));
}

//------------------------
void RenderPCM::prepareNewFrame(uint16_t in_frame_no)
{
#ifdef VIS_EN_DBG_OUT
    qInfo()<<"[REN] New frame #"<<in_frame_no<<"incoming";
#endif
    finishNewFrame(in_frame_no);
    resetFrame();
    frame_number = in_frame_no;
}

//------------------------ Convert finished frame and send it.
void RenderPCM::finishNewFrame(uint16_t in_frame_no)
{
#ifdef VIS_EN_DBG_OUT
    qInfo()<<"[REN] Frame"<<in_frame_no<<"is done";
#endif
    qint64 time_spent;
    // Convert assembled image to pixmap.
    pix_data = QPixmap::fromImage(*img_data);
    // Start frame timer if not already.
    if(frame_time.isValid()==false)
    {
        frame_time.start();
    }
    // Report new frame to renderer.
    frame_number = in_frame_no;
    emit newFrame(pix_data, frame_number);
    // Measure time from last frame.
    time_spent = frame_time.elapsed();
    if((live_pb!=false)&&(time_spent<frame_time_lim))
   {
        // Frame pace if required.
        QThread::msleep(frame_time_lim-time_spent);
    }
    // Restart frame timer.
    frame_time.start();
}

//------------------------ Insert next video line into the frame.
void RenderPCM::renderNewLine(VideoLine in_line)
{
    QColor pixel_cl;
    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] Video line overflow:"<<fill_line_num<<">"<<provided_heigth;
#endif
        return;
    }

    if(in_line.isDoubleWidth()==false)
    {
        if(in_line.pixel_data.size()<=(size_t)(img_data->width()))
        {
            if(in_line.isEmpty()==false)
            {
                QRgb *pixel_ptr;
                pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);
                for(uint16_t i=0;i<provided_width;i++)
                {
                    if(in_line.colors==vid_preset_t::COLOR_BW)
                    {
                        *pixel_ptr = qRgb(in_line.pixel_data[i], in_line.pixel_data[i], in_line.pixel_data[i]);
                    }
                    else
                    {
                        if(in_line.colors==vid_preset_t::COLOR_R)
                        {
                            pixel_cl = VIS_RED_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_G)
                        {
                            pixel_cl = VIS_GREEN_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_B)
                        {
                            pixel_cl = VIS_BLUE_TINT;
                        }
                        else
                        {
                            pixel_cl = VIS_BIT1_MARK;
                        }
                        pixel_cl.setRed(pixel_cl.red()*in_line.pixel_data[i]/255);
                        pixel_cl.setGreen(pixel_cl.green()*in_line.pixel_data[i]/255);
                        pixel_cl.setBlue(pixel_cl.blue()*in_line.pixel_data[i]/255);
                        *pixel_ptr = pixel_cl.rgb();
                    }
                    pixel_ptr++;
                }
            }
            fill_line_num++;
        }
    }
    else
    {
        if(in_line.pixel_data.size()<=((size_t)(img_data->width())*2))
        {
            if(in_line.isEmpty()==false)
            {
                QRgb *pixel_ptr;
                uint16_t line_width;
                // Get pointer to image line data.
                pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);
                line_width = in_line.pixel_data.size();
                // Copy every other byte (shrinking line by 2).
                for(uint16_t i=0;i<line_width;i=i+2)
                {
                    if(in_line.colors==vid_preset_t::COLOR_BW)
                    {
                        *pixel_ptr = qRgb(in_line.pixel_data[i], in_line.pixel_data[i], in_line.pixel_data[i]);
                    }
                    else
                    {
                        if(in_line.colors==vid_preset_t::COLOR_R)
                        {
                            pixel_cl = VIS_RED_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_G)
                        {
                            pixel_cl = VIS_GREEN_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_B)
                        {
                            pixel_cl = VIS_BLUE_TINT;
                        }
                        else
                        {
                            pixel_cl = VIS_BIT1_MARK;
                        }
                        pixel_cl.setRed(pixel_cl.red()*in_line.pixel_data[i]/255);
                        pixel_cl.setGreen(pixel_cl.green()*in_line.pixel_data[i]/255);
                        pixel_cl.setBlue(pixel_cl.blue()*in_line.pixel_data[i]/255);
                        *pixel_ptr = pixel_cl.rgb();
                    }
                    pixel_ptr++;
                }
            }
            fill_line_num++;
        }
    }
}

//------------------------ Insert next video line into the frame in the order of original frame.
void RenderPCM::renderNewLineInOrder(VideoLine in_line)
{
    uint16_t line_num;
    QColor pixel_cl;
    // [VideoLine] line numer starts from 1.
    line_num = (in_line.line_number-1);
    if(line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] Video line overflow:"<<line_num<<">"<<provided_heigth;
#endif
        return;
    }

    if(in_line.isDoubleWidth()==false)
    {
        if(in_line.pixel_data.size()<=(size_t)(img_data->width()))
        {
            if(in_line.isEmpty()==false)
            {
                QRgb *pixel_ptr;
                pixel_ptr = (QRgb *)img_data->scanLine(line_num);
                for(uint16_t i=0;i<provided_width;i++)
                {
                    if(in_line.colors==vid_preset_t::COLOR_BW)
                    {
                        *pixel_ptr = qRgb(in_line.pixel_data[i], in_line.pixel_data[i], in_line.pixel_data[i]);
                    }
                    else
                    {
                        if(in_line.colors==vid_preset_t::COLOR_R)
                        {
                            pixel_cl = VIS_RED_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_G)
                        {
                            pixel_cl = VIS_GREEN_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_B)
                        {
                            pixel_cl = VIS_BLUE_TINT;
                        }
                        else
                        {
                            pixel_cl = VIS_BIT1_MARK;
                        }
                        pixel_cl.setRed(pixel_cl.red()*in_line.pixel_data[i]/255);
                        pixel_cl.setGreen(pixel_cl.green()*in_line.pixel_data[i]/255);
                        pixel_cl.setBlue(pixel_cl.blue()*in_line.pixel_data[i]/255);
                        *pixel_ptr = pixel_cl.rgb();
                    }
                    pixel_ptr++;
                }
            }
            line_num++;
        }
    }
    else
    {
        if(in_line.pixel_data.size()<=((size_t)(img_data->width())*2))
        {
            if(in_line.isEmpty()==false)
            {
                QRgb *pixel_ptr;
                uint16_t line_width;
                // Get pointer to image line data.
                pixel_ptr = (QRgb *)img_data->scanLine(line_num);
                line_width = in_line.pixel_data.size();
                // Copy every other byte (shrinking line by 2).
                for(uint16_t i=0;i<line_width;i=i+2)
                {
                    if(in_line.colors==vid_preset_t::COLOR_BW)
                    {
                        *pixel_ptr = qRgb(in_line.pixel_data[i], in_line.pixel_data[i], in_line.pixel_data[i]);
                    }
                    else
                    {
                        if(in_line.colors==vid_preset_t::COLOR_R)
                        {
                            pixel_cl = VIS_RED_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_G)
                        {
                            pixel_cl = VIS_GREEN_TINT;
                        }
                        else if(in_line.colors==vid_preset_t::COLOR_B)
                        {
                            pixel_cl = VIS_BLUE_TINT;
                        }
                        else
                        {
                            pixel_cl = VIS_BIT1_MARK;
                        }
                        pixel_cl.setRed(pixel_cl.red()*in_line.pixel_data[i]/255);
                        pixel_cl.setGreen(pixel_cl.green()*in_line.pixel_data[i]/255);
                        pixel_cl.setBlue(pixel_cl.blue()*in_line.pixel_data[i]/255);
                        *pixel_ptr = pixel_cl.rgb();
                    }
                    pixel_ptr++;
                }
            }
            line_num++;
        }
    }
}

//------------------------ Insert next PCM-1 line into the frame.
void RenderPCM::renderNewLine(PCM1Line in_line)
{
    QRgb *pixel_ptr;
    uint8_t pixel_data, pixel_width;
    uint8_t word, word_bit, line_bit;

    pixel_data = 0;
    pixel_width = PPB_PCM1LINE;

    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] PCM-1 line overflow";
#endif
        return;
    }

    // Get pointer to image line data.
    pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);

    line_bit = 0;
    // Cycle through all data words.
    for(word=0;word<PCM1Line::WORD_MAX;word++)
    {
        // Determine how many bits there are in the current word.
        if(word==PCM1Line::WORD_CRCC)
        {
            word_bit = PCM1Line::BITS_PER_CRC;
        }
        else
        {
            word_bit = PCM1Line::BITS_PER_WORD;
        }
        // Cycle through all bits in the current word.
        while(word_bit>0)
        {
            // Get burrent bit state from the data word.
            if((in_line.words[word]&(1<<(word_bit-1)))==0)
            {
                pixel_data = 0;
            }
            else
            {
                pixel_data = 1;
            }
            for(uint8_t j=0;j<pixel_width;j++)
            {
                if(pixel_data==0)
                {
                    // Bit state "0".
                    if(in_line.isCRCValid()!=false)
                    {
                        // Data in the line was ok after binarization.
                        *pixel_ptr = VIS_BIT0_BLK;
                        if((line_bit<in_line.picked_bits_left)||
                           (line_bit>(PCM1Line::BITS_PCM_DATA-in_line.picked_bits_right-1)))
                        {
                            // Highlight picked bits.
                            *pixel_ptr = VIS_BIT0_GRN;
                        }
                    }
                    else if(in_line.isForcedBad()!=false)
                    {
                        // Line is forced to be bad.
                        *pixel_ptr = VIS_BIT0_MGN;
                    }
                    else if(in_line.hasBWSet()!=false)
                    {
                        // Line has invalid CRC but B&W levels are detected.
                        *pixel_ptr = VIS_BIT0_YEL;
                    }
                    else
                    {
                        // No BW levels detected.
                        *pixel_ptr = VIS_BIT0_RED;
                    }
                }
                else
                {
                    // Bit state "1".
                    if(in_line.isCRCValid()!=false)
                    {
                        // Data in the line was ok after binarization.
                        *pixel_ptr = VIS_BIT1_GRY;
                        if((line_bit<in_line.picked_bits_left)||
                           (line_bit>(PCM1Line::BITS_PCM_DATA-in_line.picked_bits_right-1)))
                        {
                            // Highlight picked bits.
                            *pixel_ptr = VIS_BIT1_GRN;
                        }
                    }
                    else if(in_line.isForcedBad()!=false)
                    {
                        // Line is forced to be bad.
                        *pixel_ptr = VIS_BIT1_MGN;
                    }
                    else if(in_line.hasBWSet()!=false)
                    {
                        // Line has invalid CRC but B&W levels are detected.
                        *pixel_ptr = VIS_BIT1_YEL;
                    }
                    else
                    {
                        // No BW levels detected.
                        *pixel_ptr = VIS_BIT1_RED;
                    }
                }
                // Advance to next pixel.
                pixel_ptr++;
            }
            // To go the next bit in the line.
            word_bit--;
            line_bit++;
        }
    }
    if(fill_line_num<provided_heigth)
    {
        fill_line_num++;
    }
}

//------------------------ Insert next PCM-1 sub-line into the frame.
void RenderPCM::renderNewLine(PCM1SubLine in_line)
{
    QRgb *pixel_ptr;
    uint8_t pixel_data, pixel_width;
    uint8_t word, word_bit, line_bit;
    uint16_t pixel_offset;

    pixel_data = 0;
    pixel_width = PPB_PCM1SUBLINE;

    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] PCM-1 line overflow";
#endif
        return;
    }
    // Get pointer to image line data.
    pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);

    pixel_offset = in_line.getLinePart();
    if(pixel_offset>2) pixel_offset = 0;
    pixel_offset = pixel_offset*PCM1SubLine::BITS_PCM_DATA;

    // Shift starting pixel according to sub-line.
    for(uint16_t i=0;i<(pixel_offset*pixel_width);i++)
    {
        pixel_ptr++;
    }

    line_bit = 0;
    // Cycle through all data words.
    for(word=0;word<PCM1SubLine::WORD_MAX;word++)
    {
        // Cycle through all bits in the current word.
        word_bit = PCM1SubLine::BITS_PER_WORD;
        while(word_bit>0)
        {
            // Get burrent bit state from the data word.
            if((in_line.getWord(word)&(1<<(word_bit-1)))==0)
            {
                pixel_data = 0;
            }
            else
            {
                pixel_data = 1;
            }
            for(uint8_t j=0;j<pixel_width;j++)
            {
                if(pixel_data==0)
                {
                    // Bit state "0".
                    if(in_line.isCRCValid()!=false)
                    {
                        // Data in the line was ok after binarization.
                        *pixel_ptr = VIS_BIT0_BLK;
                        if((line_bit<in_line.picked_bits_left)&&(in_line.getLinePart()==PCM1SubLine::PART_LEFT))
                        {
                            // Highlight picked bits.
                            *pixel_ptr = VIS_BIT0_GRN;
                        }
                    }
                    else if(in_line.hasBWSet()!=false)
                    {
                        // Line has invalid CRC but B&W levels are detected.
                        *pixel_ptr = VIS_BIT0_YEL;
                    }
                    else
                    {
                        // No BW levels detected.
                        *pixel_ptr = VIS_BIT0_RED;
                    }
                }
                else
                {
                    // Bit state "1".
                    if(in_line.isCRCValid()!=false)
                    {
                        // Data in the line was ok after binarization.
                        *pixel_ptr = VIS_BIT1_GRY;
                        if((line_bit<in_line.picked_bits_left)&&(in_line.getLinePart()==PCM1SubLine::PART_LEFT))
                        {
                            // Highlight picked bits.
                            *pixel_ptr = VIS_BIT1_GRN;
                        }
                    }
                    else if(in_line.hasBWSet()!=false)
                    {
                        // Line has invalid CRC but B&W levels are detected.
                        *pixel_ptr = VIS_BIT1_YEL;
                    }
                    else
                    {
                        // No BW levels detected.
                        *pixel_ptr = VIS_BIT1_RED;
                    }
                }
                // Advance to next pixel.
                pixel_ptr++;
            }
            // Advance to the next bit in the word.
            word_bit--;
            line_bit++;
        }
    }
    if(fill_line_num<provided_heigth)
    {
        if(in_line.getLinePart()==PCM1SubLine::PART_RIGHT)
        {
            // Go to the next line after last part of the source PCM line.
            fill_line_num++;
        }
    }
}

//------------------------ Insert next PCM-16x0 line into the frame.
void RenderPCM::renderNewLine(PCM16X0SubLine in_line)
{
    QRgb *pixel_ptr;
    uint8_t pixel_data, pixel_width;
    uint8_t word, word_bit, line_bit;
    uint16_t pixel_offset;

    /*QString log_line;
    log_line.sprintf("[REN] New line received: %03u-%03u-%03u", in_line.frame_number, in_line.line_number, in_line.line_part);
    qInfo()<<log_line;*/

    pixel_data = 0;
    pixel_width = PPB_PCM1600SUBLINE;

    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] PCM-16x0 line overflow";
#endif
        return;
    }

    // Get pointer to image line data.
    pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);

    pixel_offset = in_line.line_part;
    if(pixel_offset>2) pixel_offset = 0;
    pixel_offset = pixel_offset*PCM16X0SubLine::BITS_PCM_DATA;
    if(in_line.line_part==PCM16X0SubLine::PART_RIGHT)
    {
        // Account for service bit.
        pixel_offset++;
    }
    // Shift starting pixel according to sub-line.
    for(uint16_t i=0;i<(pixel_offset*pixel_width);i++)
    {
        pixel_ptr++;
    }

    line_bit = 0;
    // Cycle through all data words.
    for(word=0;word<PCM16X0SubLine::WORD_MAX;word++)
    {
        // Determine how many bits there are in the current word.
        if(word==PCM16X0SubLine::WORD_CRCC)
        {
            word_bit = PCM16X0SubLine::BITS_PER_CRC;
        }
        else
        {
            word_bit = PCM16X0SubLine::BITS_PER_WORD;
        }
        // Cycle through all bits in the current word.
        while(word_bit>0)
        {
            // Get burrent bit state from the data word.
            if((in_line.words[word]&(1<<(word_bit-1)))==0)
            {
                pixel_data = 0;
            }
            else
            {
                pixel_data = 1;
            }
            for(uint8_t j=0;j<pixel_width;j++)
            {
                if(pixel_data==0)
                {
                    // Bit state "0".
                    if(in_line.isCRCValid()!=false)
                    {
                        // Data in the sub-line was ok after binarization.
                        *pixel_ptr = VIS_BIT0_BLK;
                        if((line_bit<in_line.picked_bits_left)||
                           (line_bit>(PCM16X0SubLine::BITS_PCM_DATA-in_line.picked_bits_right-1)))
                        {
                            // Highlight picked bits.
                            *pixel_ptr = VIS_BIT0_GRN;
                        }
                    }
                    else if(in_line.isForcedBad()!=false)
                    {
                        // Sub-line is forced to be bad.
                        *pixel_ptr = VIS_BIT0_MGN;
                    }
                    else if(in_line.hasBWSet()!=false)
                    {
                        // Sub-line has invalid CRC but B&W levels are detected.
                        *pixel_ptr = VIS_BIT0_YEL;
                    }
                    else
                    {
                        // No BW levels detected.
                        *pixel_ptr = VIS_BIT0_RED;
                    }
                }
                else
                {
                    // Bit state "1".
                    if(in_line.isCRCValid()!=false)
                    {
                        // Data in the sub-line was ok after binarization.
                        *pixel_ptr = VIS_BIT1_GRY;
                        if((line_bit<in_line.picked_bits_left)||
                           (line_bit>(PCM16X0SubLine::BITS_PCM_DATA-in_line.picked_bits_right-1)))
                        {
                            *pixel_ptr = VIS_BIT1_GRN;
                        }
                    }
                    else if(in_line.isForcedBad()!=false)
                    {
                        // Sub-line is forced to be bad.
                        *pixel_ptr = VIS_BIT1_MGN;
                    }
                    else if(in_line.hasBWSet()!=false)
                    {
                        // Sub-line has invalid CRC but B&W levels are detected.
                        *pixel_ptr = VIS_BIT1_YEL;
                    }
                    else
                    {
                        // No BW levels detected.
                        *pixel_ptr = VIS_BIT1_RED;
                    }
                }
                // Advance to next pixel.
                pixel_ptr++;
            }
            // To go the next bit in the line.
            word_bit--;
            line_bit++;
        }
        if((in_line.line_part==PCM16X0SubLine::PART_MIDDLE)&&(word==PCM16X0SubLine::WORD_CRCC))
        {
            // Draw service/skew bit.
            for(uint8_t j=0;j<pixel_width;j++)
            {
                if(in_line.hasDataCoordSet()==false)
                {
                    // Failed to detect data coordinates for the sub-line.
                    if(in_line.control_bit==false)
                    {
                        // Control Bit state "0".
                        *pixel_ptr = VIS_BIT0_RED;
                    }
                    else
                    {
                        // Control Bit state "1".
                        *pixel_ptr = VIS_BIT1_RED;
                    }
                }
                else
                {
                    // Data coordinates were detected for the sub-line.
                    if(in_line.control_bit==false)
                    {
                        // Control Bit state "0".
                        *pixel_ptr = VIS_BIT0_BLK;
                    }
                    else
                    {
                        // Control Bit state "1".
                        *pixel_ptr = VIS_BIT1_GRY;
                    }
                }
                // Advance to next pixel.
                pixel_ptr++;
            }
        }
    }
    if(fill_line_num<provided_heigth)
    {
        if(in_line.line_part==PCM16X0SubLine::PART_RIGHT)
        {
            // Go to the next line after last part of the source PCM line.
            fill_line_num++;
        }
    }
}

//------------------------ Insert next STC-007 line into the frame.
void RenderPCM::renderNewLine(STC007Line in_line)
{
    //QElapsedTimer timer;
    //timer.start();
    QRgb *pixel_ptr;
    uint8_t pixel_data, pixel_width;
    uint8_t pixel_mult, word, word_bit;

#ifdef VIS_EN_DBG_OUT
    //qInfo()<<"[REN] New line received at"<<fill_line_num<<"line #"<<in_line.line_number;
#endif

    pixel_data = 0;
    pixel_width = PPB_STC007LINE;
    /*if(in_line.frame_number!=frame_number)
    {
        // Workaround for misplaced frame control signals.
        finishNewFrame(frame_number);
        frame_number = in_line.frame_number;
        resetFrame();
    }*/
    // Check for frame overflow.
    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] STC-007 line overflow:"<<fill_line_num<<">"<<provided_heigth;
#endif
        return;
    }

    // Get pointer to image line data.
    pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);

    // Draw START marker and PCM data state.
    for(uint8_t i=0;i<STC007Line::BITS_START;i++)
    {
        // START marker bit states "1010".
        if(i==0)
        {
            pixel_data = 1;
        }
        else if(i==1)
        {
            pixel_data = 0;
        }
        else if(i==2)
        {
            pixel_data = 1;
        }
        else if(i==3)
        {
            pixel_data = 0;
        }

        for(uint8_t j=0;j<pixel_width;j++)
        {
            // Draw START marker.
            *pixel_ptr = VIS_BIT0_BLK;
            if(pixel_data==0)
            {
                // Bit state "0".
                if((in_line.isCRCValid()!=false)||(in_line.hasMarkers()!=false))
                {
                    // Line CRC is valid or at least both markers were found.
                    *pixel_ptr = VIS_BIT0_GRY;
                }
            }
            else
            {
                // Bit state "1".
                if((in_line.isCRCValid()!=false)||(in_line.hasMarkers()!=false))
                {
                    // Line CRC is valid or at least both markers were found.
                    *pixel_ptr = VIS_BIT1_GRY;
                }
            }
            pixel_ptr++;
        }
    }
    // Draw PCM data.
    word = 0;
    word_bit = (STC007Line::BITS_PER_WORD-1);
    pixel_mult = (pixel_width-1);
    // Cycle through all bits.
    for(uint16_t i=0;i<(STC007Line::BITS_PCM_DATA*pixel_width);i++)
    {
        // Get burrent bit state from the data word.
        if((in_line.getWord(word)&(1<<word_bit))==0)
        {
            // Bit state "0".
            if(in_line.isWordCRCOk(word)!=false)
            {
                // Data in the word was ok after binarization.
                *pixel_ptr = VIS_BIT0_GRY;
            }
            else if(in_line.isForcedBad()!=false)
            {
                // Line is forced to be bad.
                *pixel_ptr = VIS_BIT0_MGN;
            }
            else if(in_line.isWordValid(word)!=false)
            {
                // Line was marked with bad CRC after binarization but the word is now valid (fixed by CWD).
                *pixel_ptr = VIS_BIT0_BLU;
            }
            else if(in_line.hasMarkers()!=false)
            {
                // Word has invalid CRC, but markers are present.
                *pixel_ptr = VIS_BIT0_YEL;
            }
            else
            {
                // Word has invalid CRC, no markers found in the line.
                *pixel_ptr = VIS_BIT0_RED;
            }
        }
        else
        {
            // Bit state "1".
            if(in_line.isWordCRCOk(word)!=false)
            {
                // Data in the word was ok after binarization.
                *pixel_ptr = VIS_BIT1_GRY;
            }
            else if(in_line.isForcedBad()!=false)
            {
                // Line is forced to be bad.
                *pixel_ptr = VIS_BIT1_MGN;
            }
            else if(in_line.isWordValid(word)!=false)
            {
                // Line was marked with bad CRC after binarization but the word is now valid (fixed by CWD).
                *pixel_ptr = VIS_BIT1_BLU;
            }
            else if(in_line.hasMarkers()!=false)
            {
                // Word has invalid CRC, but markers are present.
                *pixel_ptr = VIS_BIT1_YEL;
            }
            else
            {
                // Word has invalid CRC, no markers found in the line.
                *pixel_ptr = VIS_BIT1_RED;
            }
        }
        // Advance to next pixel.
        pixel_ptr++;
        if(pixel_mult==0)
        {
            // Draw full width of the pixel.
            pixel_mult = pixel_width;
            if(word_bit==0)
            {
                // Al bits in the word are done.
                if(word<STC007Line::WORD_Q_SH336)
                {
                    // Select next 14-bit word.
                    word_bit = (STC007Line::BITS_PER_WORD-1);
                    word++;
                }
                else if(word<STC007Line::WORD_CRCC_SH0)
                {
                    // Select last 16-bit word.
                    word_bit = (STC007Line::BITS_PER_CRC-1);
                    word++;
                }
            }
            else
            {
                word_bit--;
            }
        }
        pixel_mult--;
    }
    // Draw STOP marker and PCM data state.
    for(uint8_t i=0;i<STC007Line::BITS_STOP;i++)
    {
        // STOP marker bit states "01111".
        if(i==0)
        {
            pixel_data = 0;
        }
        else if(i>0)
        {
            pixel_data = 1;
        }
        for(uint8_t j=0;j<pixel_width;j++)
        {
            // Draw STOP marker.
            *pixel_ptr = VIS_BIT0_BLK;
            if(pixel_data==0)
            {
                // Bit state "0".
                if((in_line.isCRCValid()!=false)||(in_line.hasMarkers()!=false))
                {
                    // Line CRC is valid or at least both markers were found.
                    *pixel_ptr = VIS_BIT0_GRY;
                }
            }
            else
            {
                // Bit state "1".
                if(in_line.isCRCValid()==false)
                {
                    if(in_line.hasMarkers()!=false)
                    {
                        // Invalid line CRC but at least both markers were found.
                        *pixel_ptr = VIS_BIT1_GRY;
                    }
                }
                else
                {
                    // Line CRC is valid.
                    *pixel_ptr = VIS_BIT1_MARK;
                }
            }
            pixel_ptr++;
        }
    }
    fill_line_num++;
    //qInfo()<<"line"<<timer.nsecsElapsed();
}

//------------------------ Insert next PCM-1 data block into the frame.
void RenderPCM::renderNewBlock(PCM1DataBlock in_block)
{
    QRgb *pixel_ptr;
    QRgb pixel_data;
    bool pixel_on;
    uint8_t pixel_width, word_start, word_stop, word_bit;
    uint16_t line_limit, word_data;

    pixel_data = 0;
    pixel_width = PPB_PCM1BLK;

    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] PCM-1 block overflow"<<fill_line_num<<provided_heigth;
#endif
        return;
    }

    // Calculate number of visualization lines to draw one data block.
    line_limit = PCM1DataBlock::WORD_CNT/WPL_PCM1BLK;

#ifdef VIS_EN_DBG_OUT
    qInfo()<<"[REN] PCM-1 block"<<in_block.interleave_num<<"from frame"<<in_block.frame_number;
#endif

    // Cycle through drawing lines for one data block.
    for(uint8_t blk_line=0;blk_line<line_limit;blk_line++)
    {
#ifdef VIS_EN_DBG_OUT
        qInfo()<<"[REN] Draw line"<<blk_line<<"fill line"<<fill_line_num<<"from"<<img_data->height();
#endif
        // Get pointer to image line data.
        pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);
        // Calculate word limits.
        word_start = blk_line*WPL_PCM1BLK;
        word_stop = word_start+WPL_PCM1BLK;
        // Draw status bar.
        for(uint8_t i=0;i<(WPL_PCM1BLK+3);i++)
        {
#ifdef VIS_EN_DBG_OUT
            qInfo()<<"[REN] Status bar"<<i;
#endif
            // Preset default color.
            pixel_data = VIS_BIT0_BLK;
            if(i<WPL_PCM1BLK)
            {
                if(i%2==0)
                {
                    // Draw per-word-pair picked bits flags.
                    if((in_block.isShortLength()==false)||((word_start+i)<PCM1DataBlock::WORD_CNT_SHORT))
                    {
                        // Current data block is not short.
                        if(in_block.hasPickedSample(word_start+i)!=false)
                        {
                            // Word at that position has bits picked.
                            pixel_data = VIS_BIT1_BLU;
                        }
                        else if(in_block.hasPickedWord(word_start+i)!=false)
                        {
                            // Word at that position was from line with picked CRC.
                            pixel_data = VIS_BIT0_BLU;
                        }
                    }
                }
                else
                {
                    // Draw per-word-pair validity flags.
                    if((in_block.isShortLength()==false)||((word_start+i)<PCM1DataBlock::WORD_CNT_SHORT))
                    {
                        // Current data block is not short.
                        if(in_block.isWordValid(word_start+i)==false)
                        {
                            // Word at that position has invalid CRC.
                            pixel_data = VIS_BIT1_YEL;
                        }
                    }
                }
            }
            else if(i==WPL_PCM1BLK)
            {
                // Draw overall data block validity flag.
                if(in_block.isBlockValid()==false)
                {
                    // Data block is invalid.
                    pixel_data = VIS_BIT1_RED;
                }
            }
            // Bit #(VIS_PCM1_WPL+1) is skipped, black delimiter.
            else if(i==(WPL_PCM1BLK+2))
            {
                // Draw silence indicator.
                if(in_block.isAlmostSilent()==false)
                {
                    // Samples in the block are not near the silence.
                    pixel_data = VIS_LIM_OK;
                }
                else
                {
                    // Data block has almost silence.
                    pixel_data = VIS_LIM_MARK;
                }
            }
            // Draw full width of bits.
            for(uint8_t j=0;j<pixel_width;j++)
            {
                *pixel_ptr = pixel_data;
                // Advance to next pixel.
                pixel_ptr++;
            }
        }

        // Cycle through all data words.
        for(uint8_t word=word_start;word<word_stop;word++)
        {
            // Cycle through all bits in the current sample.
            word_bit = 16;
            while(word_bit>0)
            {
#ifdef VIS_EN_DBG_OUT
                qInfo()<<"[REN] Word"<<word<<"bit"<<(word_bit-1);
#endif
                // Get burrent bit state from the data word.
                if((in_block.isShortLength()!=false)&&(word>=PCM1DataBlock::WORD_CNT_SHORT))
                {
                    // Current data block is short and word index is out-of-bounds.
                    pixel_data = VIS_BIT0_BLK;
                }
                else
                {
                    word_data = (uint16_t)in_block.getSample(word);
                    //word_data = (word_data>>2);
                    pixel_on = word_data&(1<<(word_bit-1));
                    if(in_block.isWordValid(word)==false)
                    {
                        // Word at this position has invalid CRC.
                        if(pixel_on==false)
                        {
                            // Bit state "0".
                            pixel_data = VIS_BIT0_RED;
                        }
                        else
                        {
                            // Bit state "1".
                            pixel_data = VIS_BIT1_RED;
                        }
                    }
                    else if(in_block.hasPickedSample(word)!=false)
                    {
                        // Word at this position has picked bits.
                        if(pixel_on==false)
                        {
                            // Bit state "0".
                            pixel_data = VIS_BIT0_BLU;
                        }
                        else
                        {
                            // Bit state "1".
                            pixel_data = VIS_BIT1_BLU;
                        }
                    }
                    else
                    {
                        // This word has valid CRC.
                        if(pixel_on==false)
                        {
                            // Bit state "0".
                            pixel_data = VIS_BIT0_BLK;
                        }
                        else
                        {
                            // Bit state "1".
                            pixel_data = VIS_BIT1_GRY;
                        }
                    }
                }
                // Draw full width of bits.
                for(uint8_t j=0;j<pixel_width;j++)
                {
                    *pixel_ptr = pixel_data;
                    // Advance to next pixel.
                    pixel_ptr++;
                }
                word_bit--;
            }
        }

        // Draw block markers and emphasis indicators.
        for(uint8_t i=0;i<4;i++)
        {
            pixel_data = VIS_BIT0_BLK;
            if(i==0)
            {
                // Draw limiter.
                if(in_block.interleave_num%2==0)
                {
                    // Current block interleave number is even.
                    pixel_data = VIS_LIM_OK;
                }
                else
                {
                    // Current block interleave number is odd.
                    pixel_data = VIS_LIM_MARK;
                }
            }
            // Bit #1 is skipped, black delimiter.
            else if(i==2)
            {
                // Draw emphasis indicator.
                if(in_block.hasEmphasis()!=false)
                {
                    // Emphasis is present.
                    pixel_data = VIS_BIT0_GRN;
                }
            }
            // Draw full width of bits.
            for(uint8_t j=0;j<pixel_width;j++)
            {
                *pixel_ptr = pixel_data;
                // Advance to next pixel.
                pixel_ptr++;
            }
        }
        if(fill_line_num<provided_heigth)
        {
            fill_line_num++;
        }
    }
}

//------------------------ Insert next PCM-16x0 data block into the frame.
void RenderPCM::renderNewBlock(PCM16X0DataBlock in_block)
{
    QRgb *pixel_ptr;
    QRgb pixel_data;
    bool pixel_on;
    uint8_t pixel_width, word_bit;
    uint16_t word_data;

    pixel_data = 0;
    pixel_width = PPB_PCM1600BLK;

    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] PCM-16x0 block overflow";
#endif
        return;
    }

    // Get pointer to image line data.
    pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);

    // Draw status bar.
    for(uint8_t i=0;i<9;i++)
    {
        pixel_data = VIS_BIT0_BLK;
        if(i==0)
        {
            // Draw left sub-block picked bits flag.
            if(in_block.hasPickedSample(PCM16X0DataBlock::SUBBLK_1)!=false)
            {
                // Sub-block has samples fixed with Bit Picker.
                pixel_data = VIS_BIT1_BLU;
            }
            else if(in_block.hasPickedWord(PCM16X0DataBlock::SUBBLK_1)!=false)
            {
                // Sub-block has words fixed with Bit Picker.
                pixel_data = VIS_BIT0_BLU;
            }
        }
        else if(i==1)
        {
            // Draw left sub-block validity flag.
            if(in_block.isDataFixedByP(PCM16X0DataBlock::SUBBLK_1)!=false)
            {
                // Sub-block was fixed with P-code.
                pixel_data = VIS_BIT1_GRN;
            }
        }
        else if(i==2)
        {
            // Draw left sub-block picked bits flag.
            if(in_block.hasPickedWord(PCM16X0DataBlock::SUBBLK_2)!=false)
            {
                // Sub-block has words fixed with Bit Picker.
                pixel_data = VIS_BIT0_BLU;
            }

        }
        else if(i==3)
        {
            // Draw middle sub-block validity flag.
            if(in_block.isDataFixedByP(PCM16X0DataBlock::SUBBLK_2)!=false)
            {
                // Sub-block was fixed with P-code.
                pixel_data = VIS_BIT1_GRN;
            }
        }
        else if(i==4)
        {
            // Draw left sub-block picked bits flag.
            if(in_block.hasPickedWord(PCM16X0DataBlock::SUBBLK_3)!=false)
            {
                // Sub-block has words fixed with Bit Picker.
                pixel_data = VIS_BIT0_BLU;
            }
        }
        else if(i==5)
        {
            // Draw right sub-block validity flag.
            if(in_block.isDataFixedByP(PCM16X0DataBlock::SUBBLK_3)!=false)
            {
                // Sub-block was fixed with P-code.
                pixel_data = VIS_BIT1_GRN;
            }
        }
        else if(i==6)
        {
            // Draw whole data block validity flag.
            if(in_block.isBlockValid()==false)
            {
                // Data block is invalid.
                pixel_data = VIS_BIT1_RED;
            }
        }
        // Bit #7 is skipped, black delimiter.
        else if(i==8)
        {
            // Draw silence indicator.
            if(in_block.isAlmostSilent()==false)
            {
                // Samples in the block are not near the silence.
                pixel_data = VIS_LIM_OK;
            }
            else
            {
                // Data block has almost silence.
                pixel_data = VIS_LIM_MARK;
            }
        }
        // Draw full width of bits.
        for(uint8_t j=0;j<pixel_width;j++)
        {
            *pixel_ptr = pixel_data;
            pixel_ptr++;
        }
    }

    // Cycle through all sub-blocks.
    for(uint8_t blk=PCM16X0DataBlock::SUBBLK_1;blk<=PCM16X0DataBlock::SUBBLK_3;blk++)
    {
        // Cycle through all data words in the sub-block.
        for(uint8_t word=PCM16X0DataBlock::WORD_L;word<=PCM16X0DataBlock::WORD_R;word++)
        {
            word_data = (uint16_t)in_block.getSample(blk, word);
            // Cycle through all bits in the current word.
            word_bit = PCM16X0SubLine::BITS_PER_WORD;
            while(word_bit>0)
            {
                // Get burrent bit state from the data word.
                pixel_on = word_data&(1<<(word_bit-1));
                // Check the state of the data block.
                if(in_block.isBlockValid()==false)
                {
                    // Data block is invalid.
                    if(in_block.isDataBroken(blk)==false)
                    {
                        // Sub-block is not broken.
                        if(in_block.isWordCRCOk(blk, word)==false)
                        {
                            // Word at this position has invalid CRC.
                            if(pixel_on==false)
                            {
                                // Bit state "0".
                                pixel_data = VIS_BIT0_RED;
                            }
                            else
                            {
                                // Bit state "1".
                                pixel_data = VIS_BIT1_RED;
                            }
                        }
                        else if(in_block.hasPickedSample(blk, word)!=false)
                        {
                            // This sample had valid CRC with help of Bit Picker.
                            if(pixel_on==false)
                            {
                                // Bit state "0".
                                pixel_data = VIS_BIT0_BLU;
                            }
                            else
                            {
                                // Bit state "1".
                                pixel_data = VIS_BIT1_BLU;
                            }
                        }
                        else
                        {
                            // This word has valid CRC.
                            if(pixel_on==false)
                            {
                                // Bit state "0".
                                pixel_data = VIS_BIT0_BLK;
                            }
                            else
                            {
                                // Bit state "1".
                                pixel_data = VIS_BIT1_GRY;
                            }
                        }
                    }
                    else
                    {
                        // Sub-block has BROKEN data.
                        if(in_block.isWordValid(blk, word)==false)
                        {
                            // This word is unusable.
                            if(pixel_on==false)
                            {
                                // Bit state "0".
                                pixel_data = VIS_BIT0_MGN;
                            }
                            else
                            {
                                // Bit state "1".
                                pixel_data = VIS_BIT1_MGN;
                            }
                        }
                        else
                        {
                            // This word has valid CRC.
                            if(pixel_on==false)
                            {
                                // Bit state "0".
                                pixel_data = VIS_BIT0_BLK;
                            }
                            else
                            {
                                // Bit state "1".
                                pixel_data = VIS_BIT1_GRY;
                            }
                        }
                    }
                }
                else if(in_block.isDataFixedByP(blk)!=false)
                {
                    // Sub-block was fixed with P-code.
                    if(in_block.isWordCRCOk(blk, word)==false)
                    {
                        // Word at this position had invalid CRC, now fixed.
                        if(pixel_on==false)
                        {
                            // Bit state "0".
                            pixel_data = VIS_BIT0_GRN;
                        }
                        else
                        {
                            // Bit state "1".
                            pixel_data = VIS_BIT1_GRN;
                        }
                    }
                    else if(in_block.hasPickedSample(blk, word)!=false)
                    {
                        // This sample had valid CRC with help of Bit Picker.
                        if(pixel_on==false)
                        {
                            // Bit state "0".
                            pixel_data = VIS_BIT0_BLU;
                        }
                        else
                        {
                            // Bit state "1".
                            pixel_data = VIS_BIT1_BLU;
                        }
                    }
                    else
                    {
                        // This word had valid CRC after binarization.
                        if(pixel_on==false)
                        {
                            // Bit state "0".
                            pixel_data = VIS_BIT0_BLK;
                        }
                        else
                        {
                            // Bit state "1".
                            pixel_data = VIS_BIT1_GRY;
                        }
                    }
                }
                else if(in_block.isWordValid(blk, word)==false)
                {
                    // This word is invalid.
                    if(pixel_on==false)
                    {
                        // Bit state "0".
                        pixel_data = VIS_BIT0_RED;
                    }
                    else
                    {
                        // Bit state "1".
                        pixel_data = VIS_BIT1_RED;
                    }
                }
                else if(in_block.hasPickedSample(blk, word)!=false)
                {
                    // This sample had valid CRC with help of Bit Picker.
                    if(pixel_on==false)
                    {
                        // Bit state "0".
                        pixel_data = VIS_BIT0_BLU;
                    }
                    else
                    {
                        // Bit state "1".
                        pixel_data = VIS_BIT1_BLU;
                    }
                }
                else
                {
                    // This word has valid CRC.
                    if(pixel_on==false)
                    {
                        // Bit state "0".
                        pixel_data = VIS_BIT0_BLK;
                    }
                    else
                    {
                        // Bit state "1".
                        pixel_data = VIS_BIT1_GRY;
                    }
                }
                // Draw full width of bits.
                for(uint8_t j=0;j<pixel_width;j++)
                {
                    *pixel_ptr = pixel_data;
                    // Advance to next pixel.
                    pixel_ptr++;
                }
                word_bit--;
            }
        }
    }

    // Draw emphasis and BROKEN indicator.
    for(uint8_t i=0;i<8;i++)
    {
        pixel_data = VIS_BIT0_BLK;
        if(i==0)
        {
            // Draw limiter.
            pixel_data = VIS_LIM_OK;
        }
        // Bit #1 is skipped, black delimiter.
        else if(i==2)
        {
            // Draw format indicator.
            if(in_block.isInEIFormat()!=false)
            {
                // Data is in EI format.
                pixel_data = VIS_BIT1_BLU;
            }
        }
        else if(i==3)
        {
            // Draw emphasis indicator.
            if(in_block.hasEmphasis()!=false)
            {
                // Emphasis is present.
                pixel_data = VIS_BIT0_GRN;
            }
        }
        // Bit #4 is skipped, black delimiter.
        else if((i>=5)&&(i<=6))
        {
            // Draw BROKEN indicator.
            if(in_block.isDataBroken()!=false)
            {
                // Data in the block is BROKEN.
                pixel_data = VIS_BIT1_MGN;
            }
        }
        // Draw full width of bits.
        for(uint8_t j=0;j<pixel_width;j++)
        {
            *pixel_ptr = pixel_data;
            // Advance to next pixel.
            pixel_ptr++;
        }
    }
    if(fill_line_num<provided_heigth)
    {
        fill_line_num++;
    }
}

//------------------------ Insert next STC-007 data block into the frame.
void RenderPCM::renderNewBlock(STC007DataBlock in_block)
{
    QRgb *pixel_ptr;
    QRgb pixel_data;
    uint8_t pixel_width;
    uint8_t pixel_mult, word, byte_bit;
    uint16_t word_data;

    pixel_data = 0;
    pixel_width = PPB_STC007BLK;

    if(fill_line_num>=provided_heigth)
    {
#ifdef VIS_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[REN] STC-007 data block overflow";
#endif
        return;
    }

    // Get pointer to image line data.
    pixel_ptr = (QRgb *)img_data->scanLine(fill_line_num);

    // Draw status bar.
    for(uint8_t i=0;i<6;i++)
    {
        // Set bits by default as black.
        pixel_data = VIS_BIT0_BLK;
        if(i==0)
        {
            // Flag for P-correction.
            if(in_block.isDataFixedByP()!=false)
            {
                pixel_data = VIS_BIT1_GRN;
            }
        }
        else if(i==1)
        {
            // Flag for Q-correction.
            if(in_block.isDataFixedByQ()!=false)
            {
                pixel_data = VIS_BIT1_YEL;
            }
        }
        else if(i==2)
        {
            // Flag for CWD-correction.
            if(in_block.isBlockValid()==false)
            {
                // Data block is invalid.
                bool cwd_used;
                cwd_used = false;
                for(uint8_t index=STC007DataBlock::WORD_L0;index<=STC007DataBlock::WORD_R2;index++)
                {
                    if(in_block.isWordCWDFixed(index)!=false)
                    {
                        cwd_used = true;
                        break;
                    }
                }
                if(cwd_used!=false)
                {
                    // Indicate that words were fixed with CWD.
                    pixel_data = VIS_BIT0_BLU;
                }
            }
            else
            {
                // Data block is valid.
                if(in_block.isDataFixedByCWD()!=false)
                {
                    pixel_data = VIS_BIT1_BLU;
                }
            }
        }
        else if(i==3)
        {
            // Flag for data block validity.
            if(in_block.isBlockValid()==false)
            {
                pixel_data = VIS_BIT1_RED;
            }
        }
        // Bit #4 is skipped, black delimiter.
        else if(i==5)
        {
            // Indicating near silence in the data limiter.
            if(in_block.isAlmostSilent()==false)
            {
                pixel_data = VIS_LIM_OK;
            }
            else
            {
                pixel_data = VIS_LIM_MARK;
            }
        }
        // Draw full width of bits.
        for(uint8_t j=0;j<pixel_width;j++)
        {
            *pixel_ptr = pixel_data;
            pixel_ptr++;
        }
    }
    // Draw PCM data.
    word = 0;
    byte_bit = (STC007Line::BITS_PER_F1_WORD-1);
    pixel_mult = (pixel_width-1);
    // Cycle through all bits.
    for(uint16_t i=0;i<(STC007DataBlock::WORD_P0*STC007Line::BITS_PER_F1_WORD*pixel_width);i++)
    {
        // Get burrent bit state from the data word.
        word_data = in_block.getSample(word);
        if((word_data&(1<<byte_bit))==0)
        {
            // Bit state "0".
            pixel_data = VIS_BIT0_BLK;
            if(in_block.isBlockValid()==false)
            {
                // Data block is invalid (has too many errors for ECC).
                if(in_block.isDataBroken()==false)
                {
                    // Data in the block is NOT broken.
                    if(in_block.isWordCWDFixed(word)!=false)
                    {
                        // This exact word in a block was fixed with CWD.
                        pixel_data = VIS_BIT0_BLU;
                    }
                    else if(in_block.isWordValid(word)==false)
                    {
                        // This exact word in a block was CRC-marked as BAD and was not fixed.
                        pixel_data = VIS_BIT0_RED;
                    }
                }
                else
                {
                    // Data in the block is BROKEN.
                    if(in_block.isWordLineCRCOk(word)==false)
                    {
                        pixel_data = VIS_BIT0_MGN;
                    }
                }
            }
            else if(in_block.isDataFixedByQ()!=false)
            {
                // Data block was fixed using Q-correction.
                if(in_block.isWordCWDFixed(word)!=false)
                {
                    // This exact word in a block was fixed with CWD.
                    pixel_data = VIS_BIT0_BLU;
                }
                else if(in_block.isWordLineCRCOk(word)==false)
                {
                    // This exact word in a block was fixed (had bad CRC before ECC).
                    pixel_data = VIS_BIT0_YEL;
                }

            }
            else if(in_block.isDataFixedByP()!=false)
            {
                // Data block was fixed using P-correction.
                if(in_block.isWordCWDFixed(word)!=false)
                {
                    // This exact word in a block was fixed with CWD.
                    pixel_data = VIS_BIT0_BLU;
                }
                else if(in_block.isWordLineCRCOk(word)==false)
                {
                    // This exact word in a block was fixed (had bad CRC before ECC).
                    pixel_data = VIS_BIT0_GRN;
                }

            }
            else if(in_block.isWordCWDFixed(word)!=false)
            {
                // This exact word in a block was fixed with CWD.
                pixel_data = VIS_BIT0_BLU;
            }
            else if(in_block.isWordValid(word)==false)
            {
                pixel_data = VIS_BIT0_RED;
            }
        }
        else
        {
            // Bit state "1".
            pixel_data = VIS_BIT1_GRY;
            if(in_block.isBlockValid()==false)
            {
                // Data block is invalid (has too many errors for ECC).
                if(in_block.isDataBroken()==false)
                {
                    // Data in the block is NOT broken.
                    if(in_block.isWordCWDFixed(word)!=false)
                    {
                        // This exact word in a block was fixed with CWD.
                        pixel_data = VIS_BIT1_BLU;
                    }
                    else if(in_block.isWordValid(word)==false)
                    {
                        // This exact word in a block was CRC-marked as BAD and was not fixed.
                        pixel_data = VIS_BIT1_RED;
                    }
                }
                else
                {
                    // Data in the block is BROKEN.
                    if(in_block.isWordLineCRCOk(word)==false)
                    {
                        pixel_data = VIS_BIT1_MGN;
                    }
                }
            }
            else if(in_block.isDataFixedByQ()!=false)
            {
                // Data block was fixed using Q-correction.
                if(in_block.isWordCWDFixed(word)!=false)
                {
                    // This exact word in a block was fixed with CWD.
                    pixel_data = VIS_BIT1_BLU;
                }
                else if(in_block.isWordLineCRCOk(word)==false)
                {
                    // This exact word in a block was fixed (had bad CRC before ECC).
                    pixel_data = VIS_BIT1_YEL;
                }

            }
            else if(in_block.isDataFixedByP()!=false)
            {
                // Data block was fixed using P-correction.
                if(in_block.isWordCWDFixed(word)!=false)
                {
                    // This exact word in a block was fixed with CWD.
                    pixel_data = VIS_BIT1_BLU;
                }
                else if(in_block.isWordLineCRCOk(word)==false)
                {
                    // This exact word in a block was fixed (had bad CRC before ECC).
                    pixel_data = VIS_BIT1_GRN;
                }
            }
            else if(in_block.isWordCWDFixed(word)!=false)
            {
                // This exact word in a block was fixed with CWD.
                pixel_data = VIS_BIT1_BLU;
            }
            else if(in_block.isWordValid(word)==false)
            {
                pixel_data = VIS_BIT1_RED;
            }
        }
        // Advance to next pixel.
        *pixel_ptr = pixel_data;
        pixel_ptr++;
        if(pixel_mult==0)
        {
            pixel_mult = pixel_width;
            if(byte_bit==0)
            {
                if(word<STC007DataBlock::WORD_CNT)
                {
                    byte_bit = (STC007Line::BITS_PER_F1_WORD-1);
                    word++;
                }
            }
            else
            {
                byte_bit--;
            }
        }
        pixel_mult--;
    }

    // Draw seam, emphasis and BROKEN indicators.
    for(uint8_t i=0;i<7;i++)
    {
        pixel_data = VIS_BIT0_BLK;
        if(i==0)
        {
            // Draw seam indicator.
            if(in_block.isOnSeam()==false)
            {
                pixel_data = VIS_LIM_OK;
            }
            else
            {
                pixel_data = VIS_LIM_MARK;
            }
        }
        else if(i==2)
        {
            // Draw emphasis indicator.
            if(in_block.hasEmphasis()==false)
            {
                pixel_data = VIS_BIT0_BLK;
            }
            else
            {
                pixel_data = VIS_BIT0_GRN;
            }
        }
        else if((i>=4)&&(i<=5))
        {
            // Draw BROKEN indicator.
            if(in_block.isDataBroken()==false)
            {
                pixel_data = VIS_BIT0_BLK;
            }
            else
            {
                pixel_data = VIS_BIT1_MGN;
            }
        }
        // Draw full width of the pixel.
        for(uint8_t j=0;j<pixel_width;j++)
        {
            *pixel_ptr = pixel_data;
            pixel_ptr++;
        }
    }
    if(fill_line_num<provided_heigth)
    {
        fill_line_num++;
    }
}
