//------------------------ New generation universal bit detector.
uint8_t STC007Binarizer::rawProcess(VideoLine *in_video_line, STC007Line *out_stc007_line)
{
    uint8_t log_level = 0;
    //uint16_t est_ppb = 0;
    std::array<uint16_t, 50> sprd0_data;
    std::array<uint16_t, 50> sprd1_data;
    QElapsedTimer line_timer;
    line_timer.start();

    //log_level |= LOG_PROCESS|LOG_BRIGHT;
    //log_level |= LOG_LINEDUMP;

    if(in_video_line==NULL)
    {
#ifdef QT_VERSION
        qWarning()<<"[LB] Null pointer for video line provided, exiting...";
#endif
        return (uint8_t)LB_RET_NULL_VIDEO;
    }
    if(out_stc007_line==NULL)
    {
#ifdef QT_VERSION
        qWarning()<<"[LB] Null pointer for STC-007 line provided, exiting...";
#endif
        return (uint8_t)LB_RET_NULL_PCM;
    }

#ifdef LB_EN_DBG_OUT
    QString log_line, stats;
#endif

    // Copy VideoLine pointer for internal functions.
    video_line = (VideoLine *)in_video_line;

    // Store video-line parameters in the new PCM-line object.
    out_stc007_line->frame_number = video_line->frame_number;
    out_stc007_line->line_number = video_line->line_number;

    // Check if input line has service tags.
    if(video_line->isServNewFile()==true)
    {
        // Set output line as service line for new input file.
        out_stc007_line->setServNewFile(video_line->file_path);
    }
    else if(video_line->isServEndFile()==true)
    {
        // Set output line as service line for ended video file.
        out_stc007_line->setServEndFile();
    }
    else if(video_line->isServFiller()==true)
    {
        // Set output line as filler line.
        out_stc007_line->setServFiller();
        //out_stc007_line->setSilent();
    }
    // Check if line is 100% empty.
    else if(video_line->isEmpty()==false)
    {
        uint32_t tmp_calc;

        // Get current video line length in pixels.
        line_length = video_line->pixel_data.size();

        // Determine limits within the line for marker search.
        mark_start_max = line_length*digi_set.mark_max_dist;
        mark_start_max = mark_start_max/100;
        mark_end_min = line_length-mark_start_max;

        // Estimate minimum bit length.
        tmp_calc = line_length*STC007Line::INT_CALC_MULT;
        tmp_calc = tmp_calc/140;
        //est_ppb = (uint16_t)((tmp_calc+(STC007Line::INT_CALC_MULT/2))/STC007Line::INT_CALC_MULT);

        // Find WHITE and BLACK levels, find STOP marker.
        if(findBlackWhite(out_stc007_line)==true)
        {
            uint8_t lvl_black, lvl_white, low_ref, high_ref, lvl_ref;
            uint8_t pixel_val = 0;
            uint8_t min_ppb = 0;
            uint8_t max_ppb = 0;
            uint8_t hyst_lvl = 2;
            uint16_t ppb_enc_by0 = 0;
            uint16_t ppb_enc_by1 = 0;
            uint16_t last_transition = 0;
            uint16_t last_bit_width = 0;
            uint16_t est_ppb_by0 = 0;
            uint16_t est_ppb_by1 = 0;
            bool prev_high = false;
            bool dir_up_fix = false;
            bool dir_down_fix = false;

            // Calculate minimum PPB (ArVid Audio: 155 bits per line).
            min_ppb = (line_length*9/10)/155;
            // Calculate maximum PPB (PCM-1: 96 bits per line).
            max_ppb = (line_length/90)+1;
            if(max_ppb>50)
            {
                max_ppb = 50;
            }

            // Store B&W levels.
            lvl_black = out_stc007_line->black_level;
            lvl_white = out_stc007_line->white_level;

            // Calculate reference level.
            lvl_ref = pickCenterRefLevel(lvl_black, lvl_white);

            // Run until 0 and 1 PPBs are equal.
            do
            {
                // Reset data.
                last_transition = ppb_enc_by0 = ppb_enc_by1 = est_ppb_by0 = est_ppb_by1 = 0;
                prev_high = false;
                // Calculate low and high levels for hysteresis.
                low_ref = getLowRef(lvl_ref, hyst_lvl);
                high_ref = getHighRef(lvl_ref, hyst_lvl);
                if(low_ref<=lvl_black)
                {
#ifdef LB_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        log_line.sprintf("[ULB] Low ref. level clipping: %03u", low_ref);
                        qInfo()<<log_line;
                    }
#endif
                    low_ref = lvl_black+1;
                }
                if(high_ref>=lvl_white)
                {
#ifdef LB_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        log_line.sprintf("[ULB] High ref. level clipping: %03u", high_ref);
                        qInfo()<<log_line;
                    }
#endif
                    high_ref = lvl_white-1;
                }
#ifdef LB_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    log_line.sprintf("[ULB] Levels: [%03u|%03u] -> %03u [%03u|%03u]", lvl_black, lvl_white, lvl_ref, low_ref, high_ref);
                    qInfo()<<log_line;
                }
#endif

                // Reset stats data.
                sprd0_data.fill(0); sprd1_data.fill(0);
                // Go through the whole video line.
                for(uint16_t pixel=0;pixel<line_length;pixel++)
                {
                    // Pick the pixel.
                    pixel_val = video_line->pixel_data[pixel];

                    // Perform binarization with hysteresis.
                    if(prev_high==false)
                    {
                        // Previous bit was "0".
                        if(pixel_val>=low_ref)
                        {
                            // Value is more than low reference.
                            // Register "0->1" transition.
                            last_bit_width = pixel - last_transition;
                            // Update last transition coordinate.
                            last_transition = pixel;
                            // Invert last bit state.
                            prev_high = !prev_high;
                            // Check bit width limits.
                            if(last_bit_width<50)
                            {
                                // Update bit 0 width stats.
                                sprd0_data[last_bit_width]++;
                            }
                        }
                    }
                    else
                    {
                        // Previous bit was "1".
                        if(pixel_val<high_ref)
                        {
                            // Value is less than high reference.
                            // Register "1->0" transition.
                            last_bit_width = pixel - last_transition;
                            // Update last transition coordinate.
                            last_transition = pixel;
                            // Invert last bit state.
                            prev_high = !prev_high;
                            // Check bit width limits.
                            if(last_bit_width<50)
                            {
                                // Update bit 1 width stats.
                                sprd1_data[last_bit_width]++;
                            }
                        }
                    }
                }

                // Perform encounters search.
                for(uint16_t ppb=2;ppb<max_ppb;ppb++)
                {
                    // Search for maximum encounters.
                    if(sprd0_data[ppb]>ppb_enc_by0)
                    {
                        // Store current encounter of PPB.
                        ppb_enc_by0 = sprd0_data[ppb];
                        est_ppb_by0 = ppb;
                    }
                    if(sprd1_data[ppb]>ppb_enc_by1)
                    {
                        // Store current encounter of PPB.
                        ppb_enc_by1 = sprd1_data[ppb];
                        est_ppb_by1 = ppb;
                    }
                }
                // PCM-1: 94 bits
                // STC-007: 137 bits
                // ArVid Audio: 155 bits

#ifdef LB_EN_DBG_OUT
                if(((log_level&LOG_BRIGHT)!=0)&&((log_level&LOG_PROCESS)!=0))
                {
                    //qInfo()<<"[ULB] Pixel data:"<<stats;
                    stats.clear();
                    for(uint16_t index=0;index<50;index++)
                    {
                        stats += QString::number(sprd0_data[index])+",";
                    }
                    qInfo()<<"[ULB] Bit 0 width spread:"<<stats<<" ("<<ppb_enc_by0<<"pixels @"<<est_ppb_by0<<"encounters)";
                    stats.clear();
                    for(uint16_t index=0;index<50;index++)
                    {
                        stats += QString::number(sprd1_data[index])+",";
                    }
                    qInfo()<<"[ULB] Bit 1 width spread:"<<stats<<" ("<<ppb_enc_by1<<"pixels @"<<est_ppb_by1<<"encounters)";
                }
#endif

                if(est_ppb_by0==est_ppb_by1)
                {
                    // PPBs are equalized.
                    break;
                }
                else
                {
                    if(est_ppb_by0>est_ppb_by1)
                    {
                        // Reference level is too high.
                        // Check if there was other locked direction.
                        if(dir_up_fix==false)
                        {
                            // Lock this direction.
                            dir_down_fix = true;
                            // Decrease reference level.
                            if(lvl_ref>(lvl_black+hyst_lvl+1))
                            {
                                lvl_ref--;
                            }
                            else
                            {
#ifdef LB_EN_DBG_OUT
                                if((log_level&LOG_PROCESS)!=0)
                                {
                                    qInfo()<<"[ULB] Lowest reference reached, unable to equalize, stopped.";
                                }
#endif
                                break;
                            }
                        }
                        else
                        {
#ifdef LB_EN_DBG_OUT
                            if((log_level&LOG_PROCESS)!=0)
                            {
                                qInfo()<<"[ULB] PPB ratio inverted, unable to equalize, stopped.";
                            }
#endif
                            break;
                        }
                    }
                    else
                    {
                        // Reference level is too low.
                        // Check if there was other locked direction.
                        if(dir_down_fix==false)
                        {
                            // Lock this direction.
                            dir_up_fix = true;
                            // Increase reference level.
                            if(lvl_ref<(lvl_white-hyst_lvl-1))
                            {
                                lvl_ref++;
                            }
                            else
                            {
#ifdef LB_EN_DBG_OUT
                                if((log_level&LOG_PROCESS)!=0)
                                {
                                    qInfo()<<"[ULB] Highest reference reached, unable to equalize, stopped.";
                                }
#endif
                                break;
                            }
                        }
                        else
                        {
#ifdef LB_EN_DBG_OUT
                            if((log_level&LOG_PROCESS)!=0)
                            {
                                qInfo()<<"[ULB] PPB ratio inverted, unable to equalize, stopped.";
                            }
#endif
                            break;
                        }
                    }
                }
            }
            while(1);

            // Perform limited encounters search.
            ppb_enc_by0 = ppb_enc_by1 = est_ppb_by0 = est_ppb_by1 = 0;
            for(uint16_t ppb=min_ppb;ppb<max_ppb;ppb++)
            {
                // Search for maximum encounters.
                if(sprd0_data[ppb]>ppb_enc_by0)
                {
                    // Store current encounter of PPB.
                    ppb_enc_by0 = sprd0_data[ppb];
                    est_ppb_by0 = ppb;
                }
                if(sprd1_data[ppb]>ppb_enc_by1)
                {
                    // Store current encounter of PPB.
                    ppb_enc_by1 = sprd1_data[ppb];
                    est_ppb_by1 = ppb;
                }
            }

            // Pick lowest PPB of the two.
            if(est_ppb_by0==est_ppb_by1)
            {
                ppb_enc_by0 += ppb_enc_by1;
            }
            else if((est_ppb_by0>est_ppb_by1)&&(est_ppb_by1!=0))
            {
                est_ppb_by0 = est_ppb_by1;
                ppb_enc_by0 = ppb_enc_by1;
            }

            // Store resulting reference level.
            out_stc007_line->ref_level = lvl_ref;

#ifdef LB_EN_DBG_OUT
            if(((log_level&LOG_LINEDUMP)!=0)||((log_level&LOG_PROCESS)!=0))
            {
                if(est_ppb_by0==0)
                {
                    qInfo()<<"[ULB] No PPB was detected!";
                }
                else
                {
                    uint16_t est_pixels;
                    est_pixels = line_length/est_ppb_by0;
                    qInfo()<<"[ULB] Estimated PPB:"<<est_ppb_by0<<"with"<<ppb_enc_by0<<"encounters at"<<lvl_ref<<"reference level (~"<<est_pixels<<"bits per line)";
                    if(est_pixels>145)
                    {
                        qInfo()<<"[ULB] Data may contain ArVid Audio";
                    }
                    else if(est_pixels>110)
                    {
                        qInfo()<<"[ULB] Data may contain STC-007 PCM audio";
                    }
                    else
                    {
                        qInfo()<<"[ULB] Data may contain PCM-1 audio";
                    }
                }
            }
#endif
        }
        else
        {
#ifdef LB_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[ULB] Unable to detect black or white levels.";
            }
#endif
        }
    }
    else
    {
        out_stc007_line->setSilent();
#ifdef LB_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[ULB] Line marked as 'empty', skipping all processing...";
        }
#endif
    }
    qInfo()<<"[ULB] Processed by"<<line_timer.nsecsElapsed();
    return (uint8_t)LB_RET_OK;
}
