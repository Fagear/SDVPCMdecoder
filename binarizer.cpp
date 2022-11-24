#include "binarizer.h"

Binarizer::Binarizer()
{
    // Set default preset.
    setFineSettings(getDefaultFineSettings());

    video_line = NULL;
    out_pcm_line = NULL;
    log_level = 0;
    in_def_black = in_def_reference = in_def_white = 0;
    in_def_coord.clear();
    in_max_hysteresis_depth = HYST_DEPTH_SAFE;
    in_max_shift_stages = SHIFT_STAGES_MIN;
    do_coord_search = true;
    do_start_mark_sweep = true;
    do_ref_lvl_sweep = false;
    force_bit_picker = false;

    proc_state = STG_REF_FIND;
    setMode(MODE_FAST);
    setLinePartMode(FULL_LINE);
    hysteresis_depth_lim = HYST_DEPTH_MIN;
    shift_stages_lim = SHIFT_STAGES_MIN;
    line_length = 0;
    scan_start = scan_end = 0;
    mark_start_max = 0;
    mark_end_min = 0xFFFF;
    estimated_ppb = 0;
    was_BW_scanned = false;

    resetCRCStats(shift_crcs, SHIFT_STAGES_MAX+1);
    resetCRCStats(hyst_crcs, HYST_DEPTH_MAX+1);
    resetCRCStats(crc_stats, MAX_COLL_CRCS+1);
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void Binarizer::setLogLevel(uint8_t in_level)
{
    log_level = in_level;
}

//------------------------ Set input video line pointer.
void Binarizer::setSource(VideoLine *in_ptr)
{
    video_line = in_ptr;
}

//------------------------ Set output PCM line pointer.
void Binarizer::setOutput(PCMLine *out_ptr)
{
    out_pcm_line = out_ptr;
}

//------------------------ Set mode (speed/quality) of binarization.
void Binarizer::setMode(uint8_t in_mode)
{
#ifdef LB_EN_DBG_OUT
    bool print_out = false;
    if(bin_mode!=in_mode)
    {
        print_out = true;
    }
#endif
    if(in_mode==MODE_DRAFT)
    {
        bin_mode = in_mode;
        in_max_hysteresis_depth = HYST_DEPTH_SAFE;
        in_max_shift_stages = SHIFT_STAGES_MIN;
    }
    else if(in_mode==MODE_FAST)
    {
        bin_mode = in_mode;
        in_max_hysteresis_depth = 7;
        in_max_shift_stages = SHIFT_STAGES_SAFE;
    }
    else if(in_mode==MODE_INSANE)
    {
        bin_mode = in_mode;
        in_max_hysteresis_depth = HYST_DEPTH_MAX;
        in_max_shift_stages = SHIFT_STAGES_MAX;
    }
    else
    {
        bin_mode = MODE_NORMAL;
        in_max_hysteresis_depth = HYST_DEPTH_SAFE;
        in_max_shift_stages = SHIFT_STAGES_SAFE;   // More shifts are prone to false positive CRCs and clicks.
    }
#ifdef LB_EN_DBG_OUT
    if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
    {
        if(print_out!=false)
        {
            if(bin_mode==MODE_DRAFT)
            {
                qInfo()<<"[LB] Binarization mode set to 'draft' (level sweep: off, hysteresis sweep depth:"<<in_max_hysteresis_depth<<", pixel-shift:"<<in_max_shift_stages<<"stages).";
            }
            else if(bin_mode==MODE_FAST)
            {
                qInfo()<<"[LB] Binarization mode set to 'fast' (level sweep: off, hysteresis sweep depth:"<<in_max_hysteresis_depth<<", pixel-shift:"<<in_max_shift_stages<<"stages).";
            }
            else if(bin_mode==MODE_INSANE)
            {
                qInfo()<<"[LB] Binarization mode set to 'insane' (level sweep: on, hysteresis sweep depth:"<<in_max_hysteresis_depth<<", pixel-shift:"<<in_max_shift_stages<<"stages).";
            }
            else
            {
                qInfo()<<"[LB] Binarization mode set to 'normal' (level sweep: on, hysteresis sweep depth:"<<in_max_hysteresis_depth<<", pixel-shift:"<<in_max_shift_stages<<"stages).";
            }
        }
    }
#endif
}

//------------------------ Set source line part mode.
void Binarizer::setLinePartMode(uint8_t in_mode)
{
#ifdef LB_EN_DBG_OUT
    if(line_part_mode!=in_mode)
    {
        if(in_mode<PART_MAX)
        {
            if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
            {
                if(in_mode==PART_PCM16X0_LEFT)
                {
                    qInfo()<<"[LB] Line part mode set to 'PCM-16x0 left third'";
                }
                else if(in_mode==PART_PCM16X0_MIDDLE)
                {
                    qInfo()<<"[LB] Line part mode set to 'PCM-16x0 middle third'";
                }
                else if(in_mode==PART_PCM16X0_RIGHT)
                {
                    qInfo()<<"[LB] Line part mode set to 'PCM-16x0 right third'";
                }
                else
                {
                    qInfo()<<"[LB] Line part mode set to 'full video line'";
                }
            }
        }
    }
#endif
    if(in_mode<PART_MAX)
    {
        // Set value from the input.
        line_part_mode = in_mode;
    }
}

//------------------------ Enable/disable data coordinates search.
void Binarizer::setCoordinatesSearch(bool in_flag)
{
#ifdef LB_EN_DBG_OUT
    if(do_coord_search!=in_flag)
    {
        if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
        {
            if(in_flag==false)
            {
                qInfo()<<"[LB] Coordinates search override is disabled.";
            }
            else
            {
                qInfo()<<"[LB] Coordinates search override is enabled.";
            }
        }
    }
#endif
    // Set value from the input.
    do_coord_search = in_flag;
}

//------------------------Set default BLACK and WHITE levels (0 = disable).
void Binarizer::setBWLevels(uint8_t in_black, uint8_t in_white)
{
    // Check BW validity.
    if((in_black<in_white)&&(in_black<digi_set.max_black_lvl)&&(in_white>digi_set.min_white_lvl)&&(in_white!=0))
    {
#ifdef LB_EN_DBG_OUT
        if((in_def_black!=in_black)||(in_def_white!=in_white))
        {
            if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
            {
                QString log_line;
                log_line.sprintf("[LB] Default BLACK and WHITE levels set to: [%03d|%03d]", in_black, in_white);
                qInfo()<<log_line;
            }
        }
#endif
        in_def_black = in_black;
        in_def_white = in_white;
    }
    else
    {
        // Set defaults (disable preset BW levels).
        in_def_black = in_def_white = 0;
#ifdef LB_EN_DBG_OUT
        if((in_def_black!=in_black)||(in_def_white!=in_white))
        {
            if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
            {
                qInfo()<<"[LB] Default BLACK and WHITE levels are disabled.";
            }
        }
#endif
    }
}

//------------------------ Set default reference level (0 = disable).
void Binarizer::setReferenceLevel(uint8_t in_ref)
{
#ifdef LB_EN_DBG_OUT
    if(in_def_reference!=in_ref)
    {
        if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
        {
            if(in_ref==0)
            {
                qInfo()<<"[LB] Default reference level is disabled.";
            }
            else
            {
                QString log_line;
                log_line.sprintf("[LB] Default reference level set to: [%03d]", in_ref);
                qInfo()<<log_line;
            }
        }
    }
#endif
    // Set value from the input.
    in_def_reference = in_ref;
}

//------------------------ Set default pixel coordinates for data (0 = disable).
void Binarizer::setDataCoordinates(int16_t in_data_start, int16_t in_data_stop)
{
    CoordinatePair tmp_coord;
    // Check coordinates' validity.
    if((in_data_start<in_data_stop)&&(in_data_stop!=0)&&(in_data_start!=CoordinatePair::NO_COORD_LEFT)&&(in_data_stop!=CoordinatePair::NO_COORD_RIGHT))
    {
        // Set values from the input.
        tmp_coord.setCoordinates(in_data_start, in_data_stop);
    }
#ifdef LB_EN_DBG_OUT
    else
    {
        if(((log_level&LOG_PROCESS)!=0)&&((log_level&LOG_SETTINGS)!=0))
        {
            qInfo()<<"[LB] Default data pixel coordinates are disabled.";
        }
    }
#endif
    setDataCoordinates(tmp_coord);
}

//------------------------ Set default pixel coordinates for data (0 = disable).
void Binarizer::setDataCoordinates(CoordinatePair in_data_coord)
{
    // Check coordinates' validity.
    if(in_data_coord.areValid()!=false)
    {
        // Set values from the input.
        in_def_coord = in_data_coord;
#ifdef LB_EN_DBG_OUT
        if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
        {
            QString log_line;
            log_line.sprintf("[LB] Default data pixel coordinates set to: [%03d:%04d]", in_data_coord.data_start, in_data_coord.data_stop);
            qInfo()<<log_line;
        }
#endif
    }
    else
    {
        // Set defaults (disable preset coordinates).
        in_def_coord.clear();
#ifdef LB_EN_DBG_OUT
        if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
        {
            qInfo()<<"[LB] Default data pixel coordinates are disabled.";
        }
#endif
    }
}

//------------------------ Set default B&W, reference level and pixel coordinates for data (NULL = disable).
void Binarizer::setGoodParameters(PCMLine *in_pcm_line)
{
    if(in_pcm_line==NULL)
    {
        // Disable default reference level and data coordinates.
        setReferenceLevel();
        setDataCoordinates();
        setBWLevels();
    }
    else if(in_pcm_line->isCRCValidIgnoreForced()!=false)
    {
        // Set both default reference level and data coordinates from the input.
        setReferenceLevel(in_pcm_line->ref_level);
        setDataCoordinates(in_pcm_line->coords);
        setBWLevels(in_pcm_line->black_level, in_pcm_line->white_level);
#ifdef LB_EN_DBG_OUT
        if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_SETTINGS)!=0))
        {
            QString log_line;
            log_line.sprintf("[LB] Default parameters are updated: [%03d:%04d]@[%03u]", in_pcm_line->coords.data_start, in_pcm_line->coords.data_stop, in_pcm_line->ref_level);
            qInfo()<<log_line;
        }
#endif
    }
}

//------------------------ Get defaults for internal fine binarization settings.
bin_preset_t Binarizer::getDefaultFineSettings()
{
    // All field are preset to defaults in [bin_preset_t] class constructor.
    bin_preset_t def_set;
    return def_set;
}

//------------------------ Get internal fine binarization settings.
bin_preset_t Binarizer::getCurrentFineSettings()
{
    return digi_set;
}

//------------------------ Set internal fine binarization settings.
void Binarizer::setFineSettings(bin_preset_t in_set)
{
    digi_set = in_set;
#ifdef LB_EN_DBG_OUT
    if((log_level&LOG_SETTINGS)!=0)
    {
        qInfo()<<"[LB] Fine binarization settings updated";
    }
#endif
}

//------------------------ Are BLACK and WHITE levels preset from outside?
bool Binarizer::areBWLevelsPreset()
{
    if((in_def_white>digi_set.min_white_lvl)&&(in_def_black<digi_set.max_black_lvl))
    {
        // Check if reference level also provided.
        if(isRefLevelPreset()!=false)
        {
            // Check if provided ref. level is inside BW levels.
            if((in_def_reference<=in_def_black)||(in_def_reference>=in_def_white))
            {
                return false;
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Is reference level preset from outside?
bool Binarizer::isRefLevelPreset()
{
    if(in_def_reference>=digi_set.min_ref_lvl)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Main processing.
//------------------------ Take video line [VideoLine], perform AGC, TBC, binarization.
//------------------------ Output PCM data line [PCMLine].
uint8_t Binarizer::processLine()
{
    bool suppress_log;
    uint8_t stage_count;
    uint32_t tmp_calc;
    CoordinatePair forced_coords;

    // Measure processing time for debuging.
#ifdef QT_VERSION
    uint64_t line_time;
    uint64_t time_prep, time_in_all, time_in_level, time_ref_find, time_ref_sweep, time_read;
    Q_UNUSED(time_prep);
    line_time = 0;
    time_prep = time_in_all = time_in_level = time_ref_find = time_ref_sweep = time_read = 0;
    QElapsedTimer line_timer;
    line_timer.start();
#endif

#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif

    if(video_line==NULL)
    {
#ifdef LB_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[LB] Null pointer for video line provided, exiting...";
#endif
        return (uint8_t)LB_RET_NULL_VIDEO;
    }
    if(out_pcm_line==NULL)
    {
#ifdef LB_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[LB] Null pointer for PCM line provided, exiting...";
#endif
        return (uint8_t)LB_RET_NULL_PCM;
    }

    // Prepare PCM line for binarization.
    // Clear internal line structure according to PCM type.
    if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
    {
        // PCM-1.
        PCM1Line *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<PCM1Line *>(out_pcm_line);
        temp_pcm_ptr->clear();
    }
    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
    {
        // PCM-1600/1610/1630.
        PCM16X0SubLine *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<PCM16X0SubLine *>(out_pcm_line);
        temp_pcm_ptr->clear();
        // Copy line part assignment.
        if(line_part_mode==PART_PCM16X0_LEFT)
        {
            // Left part.
            temp_pcm_ptr->line_part = PCM16X0SubLine::PART_LEFT;
        }
        else if(line_part_mode==PART_PCM16X0_MIDDLE)
        {
            // Middle part.
            temp_pcm_ptr->line_part = PCM16X0SubLine::PART_MIDDLE;
        }
        else if(line_part_mode==PART_PCM16X0_RIGHT)
        {
            // Right part.
            temp_pcm_ptr->line_part = PCM16X0SubLine::PART_RIGHT;
        }
    }
    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
    {
        // STC-007/PCM-F1.
        STC007Line *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<STC007Line *>(out_pcm_line);
        temp_pcm_ptr->clear();
    }
    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_ARVA)
    {
        // ArVid Audio.
        ArVidLine *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<ArVidLine *>(out_pcm_line);
        temp_pcm_ptr->clear();
    }
    else
    {
        // Unsupported PCM type.
        out_pcm_line->clear();
    }

    // Store video line origins in the new PCM line object.
    out_pcm_line->frame_number = video_line->frame_number;
    out_pcm_line->line_number = video_line->line_number;

    suppress_log = !((log_level&LOG_PROCESS)!=0);

    // Check if input line has service tags.
    if(video_line->isServiceLine()!=false)
    {
        // Video line contains service tag.
        if(video_line->isServNewFile()!=false)
        {
            // Set output line as service line for new input file.
            out_pcm_line->setServNewFile(video_line->file_path);
        }
        else if(video_line->isServEndFile()!=false)
        {
            // Set output line as service line for ended video file.
            out_pcm_line->setServEndFile();
        }
        else if(video_line->isServFiller()!=false)
        {
            // Set output line as filler line.
            out_pcm_line->setServFiller();
        }
        else if(video_line->isServEndField()!=false)
        {
            // Set output line as service line for ended field.
            out_pcm_line->setServEndField();
        }
        else if(video_line->isServEndFrame()!=false)
        {
            // Set output line as service line for ended frame.
            out_pcm_line->setServEndFrame();
        }
        // Can not determine if [SRVLINE_HEADER_LINE] or [SRVLINE_CTRL_BLOCK] are applicable before binarization.
    }
    // Check if source line is 100% empty.
    else if(video_line->isEmpty()==false)
    {
        // Calculate line length, limits for PCM START and PCM STOP markers search, estimate PPB (Pixels Per Bit) value.
        // Save current video line length in pixels.
        line_length = (uint16_t)video_line->pixel_data.size();
        out_pcm_line->setFromDoubledState(video_line->isDoubleWidth());
        // Set line scan limits.
        scan_start = 0;
        scan_end = (line_length-1);
        out_pcm_line->setSourcePixels(scan_start, scan_end);

        // Check if there no less pixels than bits in PCM line.
        if(line_length<out_pcm_line->getBitsPerSourceLine())
        {
#ifdef LB_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[LB] Line is too short ("<<line_length<<") for PCM data, exiting...";
            qWarning()<<DBG_ANCHOR<<"[LB] Minimal size:"<<out_pcm_line->getBitsPerSourceLine();
#endif
            return (uint8_t)LB_RET_SHORT_LINE;
        }

        // Calculate limits for PCM START and PCM STOP markers search.
        if(out_pcm_line->canUseMarkers()==false)
        {
            // Current PCM line does not support markers.
            mark_start_max = 0;
            mark_end_min = 0xFFFF;
#ifdef LB_LOG_MARKER_VERBOSE
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Scan region: [%03u:%04u]", scan_start, scan_end);
                qInfo()<<log_line;
            }
#endif
        }
        else
        {
            // Determine limits within the line for marker search.
            mark_start_max = line_length*digi_set.mark_max_dist;
            mark_start_max = mark_start_max/100;
            mark_end_min = scan_end-mark_start_max;
            mark_start_max = scan_start+mark_start_max;
#ifdef LB_LOG_MARKER_VERBOSE
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Scan regions: [%03u:%03u], [%04u:%04u]",
                                 scan_start, mark_start_max, mark_end_min, scan_end);
                qInfo()<<log_line;
            }
#endif
        }

        // Integer calculation magic.
        tmp_calc = line_length*PCMLine::INT_CALC_MULT;
        // Take all valueable bits in video line.
        tmp_calc = tmp_calc/out_pcm_line->getBitsPerSourceLine();
        // Estimate minimum bit length (PPB = Pixels Per Bit).
        estimated_ppb = (uint16_t)((tmp_calc+(PCMLine::INT_CALC_MULT/2))/PCMLine::INT_CALC_MULT);

        // Set preliminary data coordinates at the boundaries of scan range.
        out_pcm_line->coords.setCoordinates(scan_start, scan_end);
        forced_coords.clear();
        if(digi_set.en_force_coords!=false)
        {
            // Calculate forced data coordinates.
            forced_coords.data_start = (int16_t)scan_start+digi_set.horiz_coords.data_start;
            forced_coords.data_stop = (int16_t)scan_end-digi_set.horiz_coords.data_stop;
            if(video_line->isDoubleWidth()!=false)
            {
                forced_coords.data_start += digi_set.horiz_coords.data_start;
                forced_coords.data_stop -= digi_set.horiz_coords.data_stop;
            }
            //qDebug()<<scan_start<<forced_coords.data_start<<forced_coords.data_stop<<scan_end;
            if(forced_coords.areValid()!=false)
            {
                // Apply forced coordinates.
                out_pcm_line->coords = forced_coords;
                out_pcm_line->setDataCoordinatesState(true);
            }
        }

#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString sprint_line;
            log_line = "[LB] -------------------- Processing line ";
            sprint_line.sprintf("[%03u:%03u], size: [%03u] pixels",
                             out_pcm_line->frame_number, out_pcm_line->line_number, video_line->pixel_data.size());
            log_line += sprint_line;
            if(out_pcm_line->isSourceDoubleWidth()!=false)
            {
                log_line += " (line is resized to double the length)";
            }
            if(forced_coords.areValid()==false)
            {
                sprint_line.sprintf(", scan area: [%03d:%04d], est. PPB: [%u]",
                                    out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop, estimated_ppb);
            }
            else
            {
                sprint_line.sprintf(", forced coordinated: [%03d:%04d], est. PPB: [%u]",
                                    out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop, estimated_ppb);
            }
            log_line += sprint_line;
            qInfo()<<log_line;
            if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
            {
                qInfo()<<"[LB] PCM type set to PCM-1 (Sony Standard B)";
            }
            else if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
            {
                qInfo()<<"[LB] PCM type set to PCM-16x0 (Sony Standard A)";
            }
            else if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
            {
                qInfo()<<"[LB] PCM type set to STC-007/Sony PCM-F1";
            }
            else if(out_pcm_line->getPCMType()==PCMLine::TYPE_ARVA)
            {
                qInfo()<<"[LB] PCM type set to ArVid Audio";
            }
            else
            {
                qWarning()<<DBG_ANCHOR<<"[LB] Unknown PCM type is set!";
            }
        }
#endif

        // Preset the first stage for state machine (reference level calculation by default).
        proc_state = STG_REF_FIND;

        // BLACK and WHITE level detection was not yet run for the line.
        was_BW_scanned = false;

        // Check for provided B&W levels.
        if(areBWLevelsPreset()!=false)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Black and white levels are provided: [%02u|%03u]", in_def_black, in_def_white);
                qInfo()<<log_line;
            }
#endif
            // Set provided B&W levels for output PCM line.
            out_pcm_line->black_level = in_def_black;
            out_pcm_line->white_level = in_def_white;
            out_pcm_line->setBWLevelsState(true);
        }
        // Check for provided coordinates and reference level (data for speeding up the conversion).
        if(isRefLevelPreset()!=false)
        {
            // Reference level is preset from outside.
            // Check for preset data coordinates.
            if(in_def_coord.areValid()!=false)
            {
                // Data coordinates also preset from outside.
                // Set the state for all possible presets.
                proc_state = STG_INPUT_ALL;
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Reference level is provided: [%03u]", in_def_reference);
                    qInfo()<<log_line;
                    log_line.sprintf("[LB] Data coordinates are provided: [%03d:%04d]", in_def_coord.data_start, in_def_coord.data_stop);
                    qInfo()<<log_line;
                }
#endif
            }
            else
            {
                // Set the state to indicate that only reference level is provided.
                proc_state = STG_INPUT_LEVEL;
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Reference level is provided: [%03u]", in_def_reference);
                    qInfo()<<log_line;
                }
#endif
            }
        }
        // Set hysteresis and pixel-shifting limits from set mode.
        hysteresis_depth_lim = in_max_hysteresis_depth;
        shift_stages_lim = in_max_shift_stages;

#ifdef QT_VERSION
        time_prep = line_timer.nsecsElapsed();
#endif

        // Cycle through stages.
        stage_count = 0;
        do
        {
            // Count loops.
            stage_count++;

            // DEBUG hook.
            /*if((out_pcm_line->frame_number==176)&&(out_pcm_line->line_number==419))
            {
                qInfo()<<"";
            }*/

            // Select processing mode.
            if(proc_state==STG_INPUT_ALL)
            {
                // Data coordinates and reference level are preset.
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] ---------- Parameters are set from the input, bypassing coordinates and reference level calculation...";
                }
#endif
                // Check if B&W levels were preset for the line.
                if(out_pcm_line->hasBWSet()==false)
                {
                    // Find WHITE and BLACK levels for hysteresis calculations or [readPCMdata()] will fail.
                    findBlackWhite();
                }
                // Check if forced coordinates are preset.
                if(forced_coords.areValid()==false)
                {
                    // No forced coordinates.
                    // Preset data coordinates from input.
                    out_pcm_line->coords = in_def_coord;
                }
                // Preset reference level from input.
                out_pcm_line->ref_level = in_def_reference;
                // Reference level sweep is disabled for this pass.
                out_pcm_line->setSweepedReference(false);
                // Check if valid B&W levels were found at all.
                if(out_pcm_line->hasBWSet()==false)
                {
                    // No valid B&W levels.
                    proc_state = STG_NO_GOOD;
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] No valid BLACK and WHITE levels were found.";
                    }
#endif
                }
                else
                {
                    // Check if detected B&W levels and provided ref level fit.
                    if((in_def_reference>=out_pcm_line->white_level)||(in_def_reference<=out_pcm_line->black_level))
                    {
                        // Forcing full processing.
                        proc_state = STG_REF_FIND;
#ifdef LB_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[LB] Reference level [%03u] is outside of B&W levels [%03u:%03u]!",
                                             in_def_reference,
                                             out_pcm_line->black_level,
                                             out_pcm_line->white_level);
                            qInfo()<<log_line;
                        }
#endif
                    }
                    else
                    {
                        // Provided ref level fits between B&W levels.
                        bool force_level_find;
                        force_level_find = false;
                        // Check if coordinates search is allowed.
                        if((do_coord_search!=false)&&(forced_coords.areValid()==false))
                        {
                            // Coordinates search is allowed.
                            if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                            {
                                // STC-007 processing.
                                STC007Line *temp_stc;
                                temp_stc = static_cast<STC007Line *>(out_pcm_line);
                                // Check if valid data without markers is allowed.
                                if(digi_set.en_good_no_marker==false)
                                {
                                    // Binarization without markers for STC-007 is not allowed.
                                    // Force find markers for the line.
                                    findSTC007Coordinates(temp_stc);
                                    // Revert to provided coordinates after those were overwritten by search.
                                    out_pcm_line->coords = in_def_coord;
                                    if(temp_stc->hasMarkers()==false)
                                    {
                                        // At least one marker was NOT found.
                                        force_level_find = true;
#ifdef LB_EN_DBG_OUT
                                        if(suppress_log==false)
                                        {
                                            qInfo()<<"[LB] Both markers should be present for valid CRC, but at least one is missing";
                                        }
#endif
                                    }
                                }
                            }
                        }
                        // Try to read PCM data with provided parameters.
                        readPCMdata(out_pcm_line);
                        // Check the result.
                        if(out_pcm_line->isCRCValid()!=false)
                        {
                            // Data is valid.
                            if(force_level_find==false)
                            {
                                // Set the flag that data was decoded using external parameters.
                                out_pcm_line->data_by_ext_tune = true;
                                // Data is OK, go to exit.
                                proc_state = STG_DATA_OK;
#ifdef LB_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                                    {
                                        STC007Line *temp_stc;
                                        temp_stc = static_cast<STC007Line *>(out_pcm_line);
                                        if(temp_stc->hasMarkers()==false)
                                        {
                                            qInfo()<<"[LB] Managed to get VALID data with preset coordinates while no markers detected.";
                                        }
                                    }
                                }
#endif
                            }
                            else
                            {
                                // Both markers are set to be required for STC-007.
                                // Fast binarization gave valid CRC, but at least one marker is absent.
                                // Forcing full processing.
                                proc_state = STG_REF_FIND;
                            }
                        }
                        else
                        {
                            // Check PCM format to determine next step.
                            if((out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)&&(forced_coords.areValid()==false))
                            {
                                // No valid data, let's try to find PCM data with only preset reference level.
                                // Skip that step is coordinates are forced.
                                proc_state = STG_INPUT_LEVEL;
                            }
                            else
                            {
                                // No valid data, start full paramater detection.
                                // For PCM-1 and PCM-1600 no need to try to detect only new coordinates (without new reference level),
                                // because coordinates detection is much more heavy load than for STC-007 (no markers).
                                // It will take twice as long still if data coordinates search will fail
                                // and than will be repeated with new reference level.
                                proc_state = STG_REF_FIND;
                            }
#ifdef LB_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[LB] No valid data found with provided data coordinates and reference level.";
                            }
#endif
                        }
                    }
                }
#ifdef QT_VERSION
                time_in_all = line_timer.nsecsElapsed();
#endif
            }
            else if(proc_state==STG_INPUT_LEVEL)
            {
                // Only reference level is preset, need to find data coordinates.
                // Check if B&W levels were already found.
                if(was_BW_scanned==false)
                {
                    // Find WHITE and BLACK levels for hysteresis calculations or [readPCMdata()] will fail.
                    findBlackWhite();
                }
                if(forced_coords.areValid()==false)
                {
                    // Reset data coordinates.
                    out_pcm_line->coords.setCoordinates(scan_start, scan_end);
                }
                // Preset reference level from input.
                out_pcm_line->ref_level = in_def_reference;
                // Reference level sweep is disabled for this pass.
                out_pcm_line->setSweepedReference(false);
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] ---------- Reference level is set from the input, bypassing reference level calculation...";
                }
#endif
                // Check if valid B&W levels were found at all.
                if(out_pcm_line->hasBWSet()==false)
                {
                    // No valid B&W levels.
                    proc_state = STG_NO_GOOD;
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] No valid BLACK and WHITE levels were found.";
                    }
#endif
                }
                else
                {
                    // Valid B&W levels are set.
                    // Preset "go full processing" mode by default.
                    proc_state = STG_REF_FIND;
                    // Check PCM format.
                    // PCM-1 and PCM-16x0 go straight to full detection stage.
                    if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                    {
                        // STC-007 processing.
                        // Check if detected B&W levels and provided ref level fit.
                        if((in_def_reference<out_pcm_line->white_level)&&(in_def_reference>out_pcm_line->black_level))
                        {
                            // Provided ref level fits between B&W levels.
                            STC007Line *temp_stc;
                            temp_stc = static_cast<STC007Line *>(out_pcm_line);
                            // Check if data coordinates search is enabled.
                            if(do_coord_search==false)
                            {
                                if(in_def_coord.areValid()==false)
                                {
                                    // Estimate coordinates.
                                    out_pcm_line->coords.setCoordinates((scan_start+estimated_ppb), (scan_end-(4*estimated_ppb)));
                                }
                                else
                                {
                                    // Set external coordinates.
                                    out_pcm_line->coords = in_def_coord;
                                }
                            }
                            else
                            {
                                // Find coordinates for PCM START and STOP markers ([findPCMmarkers()] resets all data coordinates).
                                findSTC007Coordinates(temp_stc);
                            }
                            // Check if markers were found at all.
                            if(temp_stc->hasMarkers()!=false)
                            {
                                // All markers were found.
                                // Check if data coordinates were preset and stayed the same.
                                if((in_def_coord.areValid()==false)||(temp_stc->coords!=in_def_coord))
                                {
                                    // Markers were found, try to decode data (with the same hysteresis/pixel-shifting preset as on previous stages).
                                    readPCMdata(out_pcm_line);
                                    if(out_pcm_line->isCRCValid()!=false)
                                    {
                                        // Set the flag that data was decoded using external parameters.
                                        out_pcm_line->data_by_ext_tune = true;
                                        // Data is OK, go to exit.
                                        proc_state = STG_DATA_OK;
                                    }
#ifdef LB_EN_DBG_OUT
                                    else
                                    {
                                        // No valid data, let's try to find reference level (mode already preset).
                                        if(suppress_log==false)
                                        {
                                            qInfo()<<"[LB] No valid data found with provided reference level.";
                                        }
                                    }
#endif
                                }
#ifdef LB_EN_DBG_OUT
                                else
                                {
                                    // No reason to re-read data with the same data coordinates as already done in [STG_INPUT_ALL] step with bad result.
                                    // Go to full processing (mode already preset).
                                    if(suppress_log==false)
                                    {
                                        qInfo()<<"[LB] Data coordinates didn't change, no reason to re-read data with those preset coordinates...";
                                    }
                                }
#endif
                            }
#ifdef LB_EN_DBG_OUT
                            else
                            {
                                // Markers (at least or one of those) are not found.
                                // No reason to try fast-decoding anything, go to full processing (mode already preset).
                                if(suppress_log==false)
                                {
                                    qInfo()<<"[LB] Markers were not found with provided reference level.";
                                }
                            }
#endif
                        }
#ifdef LB_EN_DBG_OUT
                        else
                        {
                            if(suppress_log==false)
                            {
                                log_line.sprintf("[LB] Reference level [%03u] is outside of B&W levels [%03u:%03u]!",
                                                 in_def_reference,
                                                 out_pcm_line->black_level,
                                                 out_pcm_line->white_level);
                                qInfo()<<log_line;
                            }
                        }
#endif
                    }
                }
#ifdef QT_VERSION
                time_in_level = line_timer.nsecsElapsed();
#endif
            }
            else if(proc_state==STG_REF_FIND)
            {
                // Nothing is preset, do the whole process by itself.
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] ---------- Reference level detection starting...";
                }
#endif
                // Check if [findBlackWhite()] was already called for this line.
                // (ignoring external preset BW flag)
                if(was_BW_scanned==false)
                {
                    // Find WHITE and BLACK levels for reference and hysteresis calculations.
                    findBlackWhite();
                }
                // Check if valid B&W levels were found at all.
                if(out_pcm_line->hasBWSet()==false)
                {
                    // No valid BLACK or WHITE level - no PCM in the video line.
                    proc_state = STG_NO_GOOD;
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] No valid BLACK and WHITE levels were found.";
                    }
#endif
                }
                else
                {
                    // Disable reference level sweep by default.
                    do_ref_lvl_sweep = false;
                    if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
                    {
                        if(bin_mode==MODE_INSANE)
                        {
                            // Allow reference sweep only in the most hardcore mode.
                            do_ref_lvl_sweep = true;
                        }
                    }
                    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                    {
                        if(bin_mode==MODE_INSANE)
                        {
                            // Allow reference sweep only in the most hardcore mode.
                            do_ref_lvl_sweep = true;
                        }
                    }
                    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                    {
                        if((bin_mode==MODE_NORMAL)||(bin_mode==MODE_INSANE))
                        {
                            // Allow reference sweep in two "slow" modes.
                            do_ref_lvl_sweep = true;
                        }
                    }
                    // If allowed - enable reference level sweep.
                    if(do_ref_lvl_sweep!=false)
                    {
                        // Go to full reference level sweep.
                        proc_state = STG_REF_SWEEP_RUN;
                    }
                    else
                    {
                        // Calculate reference level if sweep is disabled.
                        // Preset minimal stages of hysteresis and pixel-shifting.
                        hysteresis_depth_lim = HYST_DEPTH_SAFE;
                        shift_stages_lim = SHIFT_STAGES_MIN;
                        // Preset next step to final data decoding.
                        proc_state = STG_READ_PCM;
                        // Fast and simple pick between BLACK and WHITE.
                        out_pcm_line->ref_level = pickCenterRefLevel(out_pcm_line->black_level, out_pcm_line->white_level);
                        // Check PCM format.
                        if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
                        {
                            // Mode: PCM-1.
                            // Markers are unavailable for current PCM type.
                            PCM1Line *temp_pcm1;
                            temp_pcm1 = static_cast<PCM1Line *>(out_pcm_line);
#ifdef LB_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                log_line.sprintf("[LB] Calculated reference level: [%03u]", out_pcm_line->ref_level);
                                qInfo()<<log_line;
                            }
#endif
                            // Select fallback data coordinates.
                            if(forced_coords.areValid()!=false)
                            {
                                // Set forced coordinates.
                                out_pcm_line->coords = forced_coords;
                                out_pcm_line->setDataCoordinatesState(true);
                            }
                            else
                            {
                                if(in_def_coord.areValid()==false)
                                {
                                    // No coordinates are preset externally.
                                    // Set default coordinates as full video line.
                                    out_pcm_line->coords.setCoordinates(scan_start, scan_end);
                                }
                                else
                                {
                                    // Set external coordinates.
                                    out_pcm_line->coords = in_def_coord;
                                }
                                // Check if data coordinates search is enabled.
                                if((digi_set.en_coord_search!=false)&&(do_coord_search!=false))
                                {
                                    // Try to find data coordinates.
                                    findPCM1Coordinates(temp_pcm1, in_def_coord);
                                }
#ifdef LB_EN_DBG_OUT
                                else
                                {
                                    if(suppress_log==false)
                                    {
                                        if(digi_set.en_coord_search==false)
                                        {
                                            qInfo()<<"[LB] PCM-1 data search is disabled";
                                        }
                                        if(do_coord_search==false)
                                        {
                                            qInfo()<<"[LB] Data coordinates search is now allowed for the line";
                                        }
                                        if(forced_coords.areValid()!=false)
                                        {
                                            log_line.sprintf("[LB] Presetting data coordinates to forced ones: [%03d:%04d]", out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop);
                                        }
                                        else if(in_def_coord.areValid()==false)
                                        {
                                            log_line.sprintf("[LB] Presetting data coordinates by scan limits: [%03d:%04d]", out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop);
                                        }
                                        else
                                        {
                                            log_line.sprintf("[LB] Presetting data coordinates by history: [%03d:%04d]", out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop);
                                        }
                                        qInfo()<<log_line;
                                    }
                                }
#endif
                            }

                            // Check if valid data coordinates were found.
                            if(temp_pcm1->hasDataCoordSet()==false)
                            {
                                // Limit processing on possible failure.
                                hysteresis_depth_lim = HYST_DEPTH_SAFE;
                                shift_stages_lim = SHIFT_STAGES_MIN;
                            }
                            else
                            {
                                // Set maximum stages per preset mode.
                                hysteresis_depth_lim = in_max_hysteresis_depth;
                                shift_stages_lim = in_max_shift_stages;
                            }
                        }
                        else if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                        {
                            // Mode: PCM-16x0.
                            // Markers are unavailable for current PCM type.
                            PCM16X0SubLine *temp_pcm16x0;
                            temp_pcm16x0 = static_cast<PCM16X0SubLine *>(out_pcm_line);
#ifdef LB_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                log_line.sprintf("[LB] Calculated reference level: [%03u]", out_pcm_line->ref_level);
                                qInfo()<<log_line;
                            }
#endif
                            // Select fallback data coordinates.
                            if(forced_coords.areValid()!=false)
                            {
                                // Set forced coordinates.
                                out_pcm_line->coords = forced_coords;
                                out_pcm_line->setDataCoordinatesState(true);
                            }
                            else
                            {
                                if(in_def_coord.areValid()==false)
                                {
                                    // No coordinates are preset externally.
                                    // Set default coordinates as full video line.
                                    out_pcm_line->coords.setCoordinates(scan_start, scan_end);
                                }
                                else
                                {
                                    // Set external coordinates.
                                    out_pcm_line->coords = in_def_coord;
                                }
                                // Check if data coordinates search is enabled.
                                if((digi_set.en_coord_search!=false)&&(do_coord_search!=false))
                                {
                                    // Perform coordinate search (only once per video line).
                                    findPCM16X0Coordinates(temp_pcm16x0, in_def_coord);
                                }
#ifdef LB_EN_DBG_OUT
                                else
                                {
                                    if(suppress_log==false)
                                    {
                                        if(digi_set.en_coord_search==false)
                                        {
                                            qInfo()<<"[LB] PCM-16x0 data search is disabled";
                                        }
                                        if(do_coord_search==false)
                                        {
                                            qInfo()<<"[LB] Data coordinates search is now allowed for the line";
                                        }
                                        if(forced_coords.areValid()!=false)
                                        {
                                            log_line.sprintf("[LB] Presetting data coordinates to forced ones: [%03d:%04d]", out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop);
                                        }
                                        else if(in_def_coord.areValid()==false)
                                        {
                                            log_line.sprintf("[LB] Presetting data coordinates by scan limits: [%03d:%04d]", out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop);
                                        }
                                        else
                                        {
                                            log_line.sprintf("[LB] Presetting data coordinates by history: [%03d:%04d]", out_pcm_line->coords.data_start, out_pcm_line->coords.data_stop);
                                        }
                                        qInfo()<<log_line;
                                    }
                                }
#endif
                            }

                            // Check if valid data coordinates were found.
                            if(temp_pcm16x0->hasDataCoordSet()==false)
                            {
                                // Limit processing on possible failure.
                                if(bin_mode==MODE_DRAFT)
                                {
                                    hysteresis_depth_lim = 2;
                                    shift_stages_lim = SHIFT_STAGES_MIN;
                                }
                                else
                                {
                                    hysteresis_depth_lim = HYST_DEPTH_SAFE;
                                    shift_stages_lim = SHIFT_STAGES_SAFE;
                                }
                            }
                            else
                            {
                                // Set maximum stages per preset mode.
                                hysteresis_depth_lim = in_max_hysteresis_depth;
                                shift_stages_lim = SHIFT_STAGES_SAFE;
                            }
                        }
                        else if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                        {
                            // Mode: STC-007/PCM-F1.
                            // Markers are available for current PCM type.
                            STC007Line *temp_stc;
                            temp_stc = static_cast<STC007Line *>(out_pcm_line);
#ifdef LB_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                log_line.sprintf("[LB] Calculated reference level: [%03u]", out_pcm_line->ref_level);
                                qInfo()<<log_line;
                            }
#endif
                            if(forced_coords.areValid()!=false)
                            {
                                // Set forced coordinates.
                                //out_pcm_line->coords.setCoordinates(forced_coords.data_start+estimated_ppb, forced_coords.data_stop-(4*estimated_ppb));
                                out_pcm_line->coords = forced_coords;
                                out_pcm_line->setDataCoordinatesState(true);
                            }
                            else
                            {
                                // Check if data coordinates search is enabled.
                                if(do_coord_search==false)
                                {
                                    if(in_def_coord.areValid()==false)
                                    {
                                        // Estimate coordinates.
                                        out_pcm_line->coords.setCoordinates((scan_start+estimated_ppb), (scan_end-(4*estimated_ppb)));
                                    }
                                    else
                                    {
                                        // Set external coordinates.
                                        out_pcm_line->coords = in_def_coord;
                                    }
                                }
                                else
                                {
                                    // Try to find PCM markers in the line.
                                    findSTC007Coordinates(temp_stc);
                                }
                                if(temp_stc->hasMarkers()==false)
                                {
                                    // Limit processing on possible failure.
                                    hysteresis_depth_lim = HYST_DEPTH_SAFE;
                                    shift_stages_lim = SHIFT_STAGES_MIN;
                                }
                                else
                                {
                                    // Markers were found, there should be PCM data.
                                    // Set maximum stages per preset mode for reading.
                                    hysteresis_depth_lim = in_max_hysteresis_depth;
                                    shift_stages_lim = in_max_shift_stages;
                                }
                            }
                        }
#ifdef LB_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[LB] Calculated ref. level: [%03u]", out_pcm_line->ref_level);
                            qInfo()<<log_line;
                        }
#endif
                    }
                }
#ifdef QT_VERSION
                time_ref_find = line_timer.nsecsElapsed();
#endif
            }
            else if(proc_state==STG_REF_SWEEP_RUN)
            {
                // Do reference level sweep.
                calcRefLevelBySweep(out_pcm_line);
                // Proceed with final data decoding.
                proc_state = STG_READ_PCM;
#ifdef QT_VERSION
                time_ref_sweep = line_timer.nsecsElapsed();
#endif
            }
            else if(proc_state==STG_READ_PCM)
            {
                // This stage works only after reference level and all data coordinates are set.
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] ---------- Final data read starting...";
                }
                if((suppress_log==false)||((log_level&LOG_BRIGHT)!=0))
                {
                    qInfo()<<"[LB] ---------- Reference level:"<<out_pcm_line->ref_level;
                }
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] ---------- Hysteresis depth (max.):"<<hysteresis_depth_lim;
                    qInfo()<<"[LB] ---------- Pixel-shift stages (max.):"<<shift_stages_lim;
                }
                if((log_level&LOG_RAWBIN)!=0)
                {
                    {
                        uint8_t pixel_val;
                        uint16_t pixel_hop;
                        log_line.clear();
                        if(out_pcm_line->isSourceDoubleWidth()==false)
                        {
                            pixel_hop = 1;
                        }
                        else
                        {
                            pixel_hop = 2;
                        }
                        for(uint16_t pixel=scan_start;pixel<=scan_end;pixel+=pixel_hop)
                        {
                            pixel_val = getPixelBrightness(pixel);
                            if(pixel_val>=out_pcm_line->ref_level)
                            {
                                log_line += "#";
                            }
                            else
                            {
                                log_line += "-";
                            }
                        }
                        qInfo()<<"[LB] Binarized line data:";
                        qInfo()<<log_line;
                    }
                }
#endif
                if(forced_coords.areValid()!=false)
                {
                    // Disable binarization helpers for forced coordinates.
                    hysteresis_depth_lim = HYST_DEPTH_SAFE;
                    shift_stages_lim = SHIFT_STAGES_MIN;
                }
                // Reading PCM data with calculated parameters.
                if(out_pcm_line->hasDataCoordSet()!=false)
                {
                    readPCMdata(out_pcm_line);
                }
                // Check if result is any good.
                if(out_pcm_line->isCRCValid()!=false)
                {
                    // Data read OK, work is done.
                    proc_state = STG_DATA_OK;
                }
                // Check if there is a good result.
                if(proc_state!=STG_DATA_OK)
                {
                    // No valid PCM data found.
                    // Check if data coordinates were provided.
                    if((in_def_coord.areValid()!=false)&&(forced_coords.areValid()==false)&&(do_ref_lvl_sweep==false)
                       &&(out_pcm_line->isForcedBad()==false)&&(out_pcm_line->hasDataCoordSet()==false))
                    {
                        // Data coordinates are provided externally.
                        // Check if provided data coordinates differ from current ones.
                        if(out_pcm_line->coords!=in_def_coord)
                        {
                            // Current data coordinates differ from preset ones, but no good CRC with current coordinates.
                            // Preset coordinates from input.
                            out_pcm_line->coords = in_def_coord;
                            if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                            {
                                STC007Line *temp_stc;
                                temp_stc = static_cast<STC007Line *>(out_pcm_line);
                                temp_stc->marker_start_bg_coord = 0;
                                temp_stc->marker_start_ed_coord = 0;
                                temp_stc->marker_stop_ed_coord = 0;
                            }
#ifdef LB_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[LB] Unable to get valid data with detected coordinates, reverting to provided coordinates.";
                            }
#endif
                            // Re-read data with 'external' coordinates.
                            readPCMdata(out_pcm_line);
                            // Check if data was found.
                            if(out_pcm_line->isCRCValid()!=false)
                            {
                                // Data read OK, work is done.
                                proc_state = STG_DATA_OK;
                            }
                        }
                        /*else
                        {
                            // Try default coordinates.
                            out_pcm_line->coords.setCoordinates(scan_start, scan_end);
#ifdef LB_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[LB] Unable to get valid data with preset coordinates, reverting to default coordinates.";
                            }
#endif
                            // Re-read data with default coordinates.
                            readPCMdata(out_pcm_line);
                            // Check if data was found.
                            if(out_pcm_line->isCRCValid()!=false)
                            {
                                // Data read OK, work is done.
                                proc_state = STG_DATA_OK;
                            }
                        }*/
                    }
                    // Check if there is a good result, finally.
                    if(proc_state!=STG_DATA_OK)
                    {
                        proc_state = STG_NO_GOOD;
                    }
                }
#ifdef QT_VERSION
                time_read = line_timer.nsecsElapsed();
#endif
            }
            else if(proc_state==STG_DATA_OK)
            {
                // Check if good CRC is not allowed for the line.
                if(out_pcm_line->isForcedBad()!=false)
                {
                    // Processing yelded good CRC, but the line if forced to be bad.
                    proc_state = STG_NO_GOOD;
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] ---------- Valid PCM data was found, but line is forced BAD.";
                    }
#endif
                }
                else
                {
                    // PCM data was decoded successfully.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] ---------- Valid PCM data was found, work is done.";
                    }
#endif
                    if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
                    {
                        PCM1Line *temp_pcm_ptr;
                        temp_pcm_ptr = static_cast<PCM1Line *>(out_pcm_line);
                        // Check for presence of Header/Footer line in PCM-1 PCM.
                        if(temp_pcm_ptr->hasHeader()!=false)
                        {
                            // Force it as a valid header service line.
                            temp_pcm_ptr->setServHeader();
                        }
                    }
                    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                    {
                        PCM16X0SubLine *temp_pcm_ptr;
                        temp_pcm_ptr = static_cast<PCM16X0SubLine *>(out_pcm_line);
                        // Force set good data coordinates if data was found without scanning for those.
                        temp_pcm_ptr->setDataCoordinatesState(true);
                    }
                    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                    {
                        STC007Line *temp_pcm_ptr;
                        temp_pcm_ptr = static_cast<STC007Line *>(out_pcm_line);
                        // Check if valid binarization without markers is allowed.
                        if(digi_set.en_good_no_marker==false)
                        {
                            if(temp_pcm_ptr->hasMarkers()==false)
                            {
#ifdef LB_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    if(temp_pcm_ptr->hasStartMarker()!=false)
                                    {
                                        qInfo()<<"[LB] 'Valid' data without STOP marker, forcing BAD CRC";
                                    }
                                    else if(temp_pcm_ptr->hasStopMarker()!=false)
                                    {
                                        qInfo()<<"[LB] 'Valid' data without START marker, forcing BAD CRC";
                                    }
                                    else
                                    {
                                        qInfo()<<"[LB] 'Valid' data without both markers, forcing BAD CRC";
                                    }
                                }
#endif
                                // Don't allow good CRC without markers.
                                temp_pcm_ptr->setForcedBad();
                                proc_state = STG_NO_GOOD;
                                continue;
                            }
                        }
                        // Propagate CRC state to all word statuses.
                        temp_pcm_ptr->applyCRCStatePerWord();
                        // Check for presence of Control Block in STC-007 PCM line.
                        if(temp_pcm_ptr->hasControlBlock()!=false)
                        {
                            // Force it as a valid service line with Control Block.
                            temp_pcm_ptr->setServCtrlBlk();
                        }
                    }
                    // Transfer double width flag.
                    out_pcm_line->setFromDoubledState(video_line->isDoubleWidth());
                    // Exit stage cycle.
                    break;
                }
            }
            else if(proc_state==STG_NO_GOOD)
            {
                // All efforts went to trash - no PCM with valid CRC was found.
                // Check if CRC is BAD as it should be at this stage.
                if(out_pcm_line->isCRCValid()!=false)
                {
                    // Ensure BAD CRC if it was missed.
                    // For example, first fast run produced good CRC, than full-processing was forced and aborted early at B&W search.
                    out_pcm_line->setInvalidCRC();
#ifdef LB_EN_DBG_OUT
                    //if(suppress_log==false)
                    {
                        qInfo()<<"[LB] Missed CRC that should be BAD, fixing...";
                    }
#endif
                }
                if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                {
                    STC007Line *temp_pcm_ptr;
                    temp_pcm_ptr = static_cast<STC007Line *>(out_pcm_line);
                    // Propagate CRC state to all word statuses.
                    temp_pcm_ptr->applyCRCStatePerWord();
                    if((forced_coords.areValid()!=false)&&(temp_pcm_ptr->hasBWSet()!=false))
                    {
                        // Force marker presence is coordinates are forced and no valid CRC.
                        temp_pcm_ptr->forceMarkersOk();
                    }
                }
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    if(out_pcm_line->isServiceLine()==false)
                    {
                        qInfo()<<"[LB] ---------- No valid PCM data was found in the line.";
                    }
                }
#endif
                // Transfer double width flag.
                out_pcm_line->setFromDoubledState(video_line->isDoubleWidth());
                // Exit stage cycle.
                break;
            }
            else
            {
#ifdef LB_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[LB] -------------------- Impossible state detected, breaking...";
#endif
                break;
            }

            // Check for looping.
            if(stage_count>STG_MAX)
            {
#ifdef LB_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[LB] -------------------- Inf. loop detected, breaking...";
#endif
                break;
            }
        }
        while(1);   // Stages cycle.
    }
    else
    {
        // Empty video line provided.
        out_pcm_line->setSilent();
        out_pcm_line->setInvalidCRC();
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[LB] Line marked as 'empty', skipping all processing...";
        }
#endif
    }

    // Store time that processing took.
#ifdef QT_VERSION
    line_time = line_timer.nsecsElapsed();
    out_pcm_line->process_time = line_time/1000;

    /*
    log_line = "[LB-TIME] PREP:   IN_ALL: IN_REF: REF_F:  REF_S:  READ:   TOTAL:";
    qDebug()<<log_line;
    log_line.sprintf("[LB-TIME] %05u   %05u   %05u   %05u   %05u   %05u   %05u",
                     (unsigned int)time_prep,
                     (unsigned int)time_in_all,
                     (unsigned int)time_in_level,
                     (unsigned int)time_ref_find,
                     (unsigned int)time_ref_sweep,
                     (unsigned int)time_read,
                     (unsigned int)line_time);
    qDebug()<<log_line;*/
#else
    out_pcm_line->process_time = 0;
#endif

    return (uint8_t)LB_RET_OK;
}

//------------------------ Debug wrapper for getting value from pixel vector.
#ifdef LB_EN_PIXEL_DBG
uint8_t Binarizer::getPixelBrightness(uint16_t x_coord)
{
    if(video_line==NULL)
    {
        qWarning()<<DBG_ANCHOR<<"[LB] Video line has NULL pointer!";
        return 0;
    }
    else
    {
        if(x_coord<=scan_end)
        {
            if(x_coord<=(video_line->pixel_data.size()))
            {
                // Normal result.
                return video_line->pixel_data.at(x_coord);
            }
            else
            {
                // Pixel coordinate out of bounds.
                qWarning()<<DBG_ANCHOR<<"[LB] Out-of-bound pixel coordinate! ("<<x_coord<<") while max per line is"<<(video_line->pixel_data.size());
                return 0;
            }
        }
        else
        {
            // Pixel coordinate out of bounds.
            qWarning()<<DBG_ANCHOR<<"[LB] Out-of-bound pixel coordinate! ("<<x_coord<<") while max per scan is"<<scan_end;
            return 0;
        }
    }
}
#endif

//------------------------ Fill up regular array of 16-bit integers.
void Binarizer::fillArray(uint16_t *in_arr, uint16_t count, uint16_t value)
{
    for(uint16_t i=0;i<count;i++)
    {
        in_arr[i] = value;
    }
}

//------------------------ Reset CRC statistics.
void Binarizer::resetCRCStats(crc_handler_t *crc_array, uint16_t count, uint8_t *valid_cnt)
{
    // Clear CRC array.
    for(uint16_t i=0;i<count;i++)
    {
        crc_array[i].result = 0;
        crc_array[i].data_start = crc_array[i].data_stop = 0;
        crc_array[i].crc = 0;
        crc_array[i].hyst_dph = crc_array[i].shift_stg = 0x0f;
    }
    if(valid_cnt!=NULL)
    {
        // Reset counter if provided.
        (*valid_cnt) = 0;
    }
}

//------------------------ Update CRC statistics.
void Binarizer::updateCRCStats(crc_handler_t *crc_array, crc_handler_t in_crc, uint8_t *valid_cnt)
{
    bool crc_found;
    // Clear "CRC already in the list" flag.
    crc_found = false;
    if((*valid_cnt)>=MAX_COLL_CRCS)
    {
        (*valid_cnt) = (MAX_COLL_CRCS-1);
    }
    // Try to find provided CRC in the list.
    // (start from [1] because [0] will hold most frequent CRC later)
    for(uint8_t i=1;i<=(*valid_cnt);i++)
    {
        if(crc_array[i].crc==in_crc.crc)
        {
            // This CRC is already in the list, increase its encounters.
            crc_array[i].result++;
            crc_found = true;
            break;
        }
    }
    // Check if CRC match detected.
    if(crc_found==false)
    {
        // No such CRC in the list yet.
        // Increase number of valid CRCs.
        (*valid_cnt)++;
        if((*valid_cnt)<MAX_COLL_CRCS)
        {
            // Not too many different CRCs.
            // Add new CRC to the list.
            crc_array[(*valid_cnt)].crc = in_crc.crc;
            crc_array[(*valid_cnt)].hyst_dph = in_crc.hyst_dph;
            crc_array[(*valid_cnt)].shift_stg = in_crc.shift_stg;
            crc_array[(*valid_cnt)].result++;
        }
    }
}

//------------------------ Get most frequent CRC data.
void Binarizer::findMostFrequentCRC(crc_handler_t *crc_array, uint8_t *valid_cnt, bool skip_equal, bool no_log)
{
    bool suppress_log;

    suppress_log = !((log_level&LOG_PROCESS)!=0);

    // Reset placeholder for most frequent CRC.
    crc_array[0].result = 0;        // How many times most frequent CRC is encountered
    crc_array[0].data_start = 0;    // Its index within [crc_array] array
    crc_array[0].data_stop = 0;
    crc_array[0].hyst_dph = 0;      // Hysteresis depth on which CRC was captured.
    crc_array[0].shift_stg = 0;     // Pixel-shifting stage on which CRC was captured.
    if((*valid_cnt)>=MAX_COLL_CRCS)
    {
        (*valid_cnt) = (MAX_COLL_CRCS-1);
    }
    // Determine most frequent CRC.
    for(uint8_t i=1;i<=(*valid_cnt);i++)
    {
        if(crc_array[i].result>crc_array[0].result)
        {
            // Update most frequent CRC.
            crc_array[0].result = crc_array[i].result;
            crc_array[0].crc = crc_array[i].crc;
            crc_array[0].hyst_dph = crc_array[i].hyst_dph;
            crc_array[0].shift_stg = crc_array[i].shift_stg;
            crc_array[0].data_start = i;
        }
    }
    // Check if should invalidate lines that have many good reference levels with different equal-count CRCs.
    if(skip_equal!=false)
    {
        // Check if other CRCs are less frequent.
        for(uint8_t i=1;i<=(*valid_cnt);i++)
        {
            // Check if current CRC is not the selected most frequent.
            if(crc_array[0].data_start!=i)
            {
                // Check for encounter difference.
                // Make sure that most frequent CRC is significatly more frequent than any other ones.
                if(crc_array[0].result<=(2*crc_array[i].result))
                {
                    // One of other CRCs is too close to most frequent.
                    // Set most frequent CRC as invalid.
                    crc_array[0].result = 0;
                    crc_array[0].hyst_dph = 0;
                    crc_array[0].shift_stg = 0;
#ifdef LB_EN_DBG_OUT
                    if((suppress_log==false)&&(no_log==false))
                    {
                        qInfo()<<"[LB] Impossible to determine which is most frequent CRC: all CRCs invalidated!";
                    }
#endif
                    break;
                }
            }
        }
    }
#ifdef LB_EN_DBG_OUT
    if((*valid_cnt)>1)
    {
        if(((suppress_log==false)||((log_level&LOG_REF_SWEEP)!=0))&&(no_log==false))
        {
            qInfo()<<"[LB] More than one ("<<(*valid_cnt)<<") 'good' CRC found! Collision detected.";
            for(uint8_t i=1;i<=(*valid_cnt);i++)
            {
                if(crc_array[i].result>0)
                {
                    if(crc_array[0].crc==crc_array[i].crc)
                    {
                        if(crc_array[0].result==0)
                        {
                            qInfo()<<"[LB] CRC"<<QString::number(crc_array[i].crc, 16).prepend("0x")<<"count:"<<crc_array[i].result<<"(failed)";
                        }
                        else
                        {
                            qInfo()<<"[LB] CRC"<<QString::number(crc_array[i].crc, 16).prepend("0x")<<"count:"<<crc_array[i].result<<"(selected)";
                        }
                    }
                    else
                    {
                        qInfo()<<"[LB] CRC"<<QString::number(crc_array[i].crc, 16).prepend("0x")<<"count:"<<crc_array[i].result;
                    }
                }
                else
                {
                    break;
                }
            }
            qInfo()<<"[LB] CRCs found:"<<(*valid_cnt);
        }
    }
#endif
    // Most frequent one was found.
    // Check if it was invalidated.
    if(crc_array[0].result==0)
    {
        (*valid_cnt) = 0;
    }
}

//------------------------ Invalidate not frequent enough CRCs.
void Binarizer::invalidateNonFrequentCRCs(crc_handler_t *crc_array, uint8_t low_level, uint8_t high_level, uint8_t valid_cnt, uint16_t target_crc, bool no_log)
{
    bool suppress_log;
    uint8_t index;

    suppress_log = !((log_level&LOG_PROCESS)!=0);
    suppress_log = suppress_log||no_log;

#ifdef LB_EN_DBG_OUT
    /*if(suppress_log==false)
    {
        if(valid_cnt==0)
        {
            qInfo()<<"[LB] Invalidating all CRCs between indexes"<<low_level<<"to"<<high_level<<"...";
        }
        else
        {
            qInfo()<<"[LB] Invalidating not frequent enough CRCs between indexes"<<low_level<<"to"<<high_level<<"...";
        }
    }*/
#endif

    // Invalidate not frequent enough CRCs.
    index = high_level;
    while(index>=low_level)
    {
        // Search for CRCs that are still marked as valid.
        if(crc_array[index].result==REF_CRC_OK)
        {
            // Check if all CRCs were invalidated (most frequent one was not frequent enough)
            // or if CRC on current reference level is not most frequent one.
            if((valid_cnt==0)||(crc_array[index].crc!=target_crc))
            {
                // Make this not a valid CRC.
                crc_array[index].result = REF_CRC_COLL;
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    QString log_line;
                    log_line.sprintf("[LB] At index [%u] invalidated CRC [0x%04X] (not most frequent, most frequent: [0x%04X])",
                                     index,
                                     crc_array[index].crc,
                                     target_crc);
                    qInfo()<<log_line;
                }
#endif
            }
        }
        if(index==low_level) break;
        index--;
    }
}

//------------------------ Pick target after CRC sweep.
uint8_t Binarizer::pickLevelByCRCStats(crc_handler_t *crcs, uint8_t *ref_result, uint8_t low_lvl, uint8_t high_lvl,
                                          uint8_t target_result, uint8_t max_hyst, uint8_t max_shift)
{
    bool suppress_log, good_ref_det, range_lock, second_start_lock;
    uint8_t index, low_depth, low_shift;
    uint8_t low_ref, high_ref, tst_low_ref, tst_high_ref, picked_ref;

    if(ref_result==NULL) return SPAN_NOT_FOUND;
#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !((log_level&LOG_PROCESS)!=0);
    //suppress_log = false;

    good_ref_det = range_lock = second_start_lock = false;
    low_ref = high_ref = tst_low_ref = tst_high_ref = 0;
    low_depth = low_shift = 0xFF;

    // Scan for the lowest shifting stage and hysteresis depth for target result.
    index = high_lvl;
    while(index>=low_lvl)
    {
        // Check for target result and lower then provided maximums.
        if((crcs[index].result==target_result)&&(crcs[index].hyst_dph<=max_hyst)&&(crcs[index].shift_stg<=max_shift))
        {
            // At least one target result found.
            good_ref_det = true;
            if(crcs[index].hyst_dph<low_depth)
            {
                // Store new lowest hysteresis depth.
                low_depth = crcs[index].hyst_dph;
                // Overwrite associated shifting stage.
                low_shift = crcs[index].shift_stg;
                // Save new target region start.
                high_ref = index;
            }
            else if(crcs[index].hyst_dph==low_depth)
            {
                // Found matching lowest hysteresis depth, check if pixel stage is lower.
                if(crcs[index].shift_stg<low_shift)
                {
                    // Store new lowest shifting stage.
                    low_shift = crcs[index].shift_stg;
                    // Save new target region start.
                    high_ref = index;
                }
            }
        }
        if(index==low_lvl) break;
        index--;
    }
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        if(good_ref_det==false)
        {
            log_line.sprintf("[LB] Target result was not found!");
        }
        else
        {
            log_line.sprintf("[LB] Target result found starting at index [%u], lowest depth [%u], lowest shift [%u]",
                             high_ref, low_depth, low_shift);
        }
        qInfo()<<log_line;
    }
#endif
    // Check if target is found.
    if(good_ref_det==false)
    {
        // No target found, exit.
        return SPAN_NOT_FOUND;
    }
    else
    {
        // Proceed with search from the start of target region.
        index = high_ref;
        while(index>=low_lvl)
        {
            // Search for target result and with minimal found hysteresis and shift.
            if((crcs[index].result==target_result)&&(crcs[index].hyst_dph==low_depth)&&(crcs[index].shift_stg==low_shift))
            {
                // Target region continues.
                if(range_lock==false)
                {
                    // Update lowest index.
                    low_ref = index;
                }
                else
                {
                    if(second_start_lock==false)
                    {
                        // Update new high index for probable another region.
                        tst_high_ref = index;
                        second_start_lock = true;
                    }
                    // Update low index for probable another region.
                    tst_low_ref = index;
                }
            }
            else
            {
                // Target region ended.
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    if(range_lock==false)
                    {
                        log_line.sprintf("[LB] Found target region at indexes [%u|%u]",
                                         low_ref, high_ref);
                        qInfo()<<log_line;
                    }
                }
#endif
                // Lock found region.
                range_lock = true;
                if(second_start_lock!=false)
                {
                    // Another region finished.
                    second_start_lock = false;
                    // Compare its length to previously locked one.
                    if((tst_high_ref-tst_low_ref)>=(high_ref-low_ref))
                    {
#ifdef LB_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[LB] Found another target region at indexes [%u|%u], indexes updated from [%u|%u]",
                                             tst_low_ref, tst_high_ref, low_ref, high_ref);
                            qInfo()<<log_line;
                        }
#endif
                        // Update locked region to the new one.
                        low_ref = tst_low_ref;
                        high_ref = tst_high_ref;
                    }
                }
            }
            if(index==low_lvl) break;
            index--;
        }
        // Take the middle of the found region.
        picked_ref = (high_ref-low_ref);
        picked_ref = picked_ref/2;
        picked_ref = low_ref+picked_ref;
        // Good reference value is picked, return it.
        (*ref_result) = picked_ref;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Index selected: %u (CRC: 0x%x) from [%u|%u], with hysteresis depth %u and pixel shift %u",
                             picked_ref, crcs[picked_ref].crc, low_ref, high_ref, low_depth, low_shift);
            qInfo()<<log_line;
        }
#endif
        return SPAN_OK;
    }
}

//------------------------ Pick target after CRC sweep (speed optimized).
uint8_t Binarizer::pickLevelByCRCStatsOpt(crc_handler_t *crcs, uint8_t *ref_result, uint8_t low_lvl, uint8_t high_lvl,
                                          uint8_t target_result, uint8_t max_hyst, uint8_t max_shift)
{
    bool suppress_log, range_lock, good_ref_det;
    uint8_t index, hold_cnt, same_cnt, low_depth, low_shift, high_shift;
    uint8_t low_ref, high_ref, picked_ref;

    if(ref_result==NULL) return SPAN_NOT_FOUND;
#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !((log_level&LOG_PROCESS)!=0);

    //QElapsedTimer dbg_timer;
    //dbg_timer.start();

    range_lock = good_ref_det = false;
    low_shift = high_shift = 0;
    low_ref = high_ref = 0;

    // Scan for biggest range of target CRCs after stats scan.
    index = high_lvl;
    while(index>=low_lvl)
    {
        // Check if CRC at current reference level produced desired result.
        if((crcs[index].result==target_result)&&(crcs[index].hyst_dph<=max_hyst)&&(crcs[index].shift_stg<=max_shift))
        {
            // Current index is at target.
            // Check if target range is already started.
            if(good_ref_det==false)
            {
                // At least one target result found - lock the range.
                good_ref_det = true;
                // Set coordinates to the start of the region.
                low_ref = high_ref = index;
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Target range started at index [%u]", index);
                    qInfo()<<log_line;
                }
#endif
            }
            else
            {
                // Update lower coordinate in the range.
                low_ref = index;
                // Check if it is the last in the search range.
                if(low_ref==low_lvl)
                {
                    // Update coordinates.
                    low_shift = low_ref;
                    high_shift = high_ref;
                    range_lock = true;
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Target range finished at index [%u], updated search coordinates to [%u|%u].",
                                         index, low_shift, high_shift);
                        qInfo()<<log_line;
                    }
#endif
                }
            }
        }
        else
        {
            // Current index is NOT at target.
            // Check if target range was started.
            if(good_ref_det!=false)
            {
                // Check if there was a shorter range before.
                if((high_ref-low_ref+1)>=(high_shift-low_shift+1))
                {
                    // Save bigger span coordinates.
                    low_shift = low_ref;
                    high_shift = high_ref;
                    range_lock = true;
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Target range finished at index [%u], updated search coordinates.", (index+1));
                        qInfo()<<log_line;
                    }
#endif
                }
#ifdef LB_EN_DBG_OUT
                else
                {
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Target range finished at index [%u], it was smaller than previous at [%u|%u], ignored",
                                         (index+1), low_shift, high_shift);
                        qInfo()<<log_line;
                    }
                }
#endif

                // Unlock the range.
                good_ref_det = false;
            }
        }

        if(index==low_lvl) break;
        index--;
    }

    // If target range was found, replace scan coordinates from input with coordinates of the range.
    if(range_lock!=false)
    {
        high_lvl = high_shift;
        low_lvl = low_shift;
    }
    good_ref_det = false;
    hold_cnt = low_shift = high_shift = 0;
    low_depth = low_shift = 255;
    same_cnt = MIN_VALID_CRCS;
    low_ref = high_ref = picked_ref = digi_set.max_ref_lvl;

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Picking from range [%u|%u]...", low_lvl, high_lvl);
        qInfo()<<log_line;
    }
#endif

    // Cycle through set range to find target value.
    index = high_lvl;
    while(index>=low_lvl)
    {
        // Check if CRC at current reference level produced desired result.
        if((crcs[index].result==target_result)&&(crcs[index].hyst_dph<=max_hyst)&&(crcs[index].shift_stg<=max_shift))
        {
            // At least one good result found - lock it.
            good_ref_det = true;
            // Check if hysteresis depth is less than before.
            if(low_depth>crcs[index].hyst_dph)
            {
                // Update lower depth.
                low_depth = crcs[index].hyst_dph;
                // Rewrite "lowest" shift stage for that depth.
                low_shift = crcs[index].shift_stg;
                // Save index for that reference level.
                low_ref = high_ref = index;
                // Reset holding counter to make skip over next temporary higher depth possible.
                hold_cnt = MIN_VALID_CRCS;
                //qInfo()<<"[RAS] Lvl"<<index<<", depth updated to"<<low_depth;
            }
            // Check if the same hysteresis depth is encountered.
            else if(low_depth==crcs[index].hyst_dph)
            {
                //qInfo()<<"[RAS] Lvl"<<index<<", depth"<<low_depth<<", shift"<<low_shift;
                // Check if shift stage is now lower.
                if(low_shift>crcs[index].shift_stg)
                {
                    // Update lower shift stage.
                    low_shift = crcs[index].shift_stg;
                    // Update index for that reference level.
                    low_ref = high_ref = index;
                    // Reset timeout counter for the same "depth/shift stage" combination.
                    same_cnt = MIN_VALID_CRCS;
                    hold_cnt = MIN_VALID_CRCS;
                    //qInfo()<<"[RAS] Lvl"<<index<<", depth"<<low_depth<<", shift updated to"<<low_shift;
                }
                // Check if the same shift stage is encountered as well.
                else if(low_shift==crcs[index].shift_stg)
                {
                    // Update index for that reference level.
                    low_ref = index;
                    // Count down timeout for the same "depth/shift stage" combination.
                    same_cnt--;
                    //qInfo()<<"[RAS] Lvl"<<index<<", depth"<<low_depth<<", shift"<<low_shift<<", cnt"<<same_cnt;
                    if(same_cnt==0)
                    {
                        // Reset depth hold counter as well, no need to hold it afterwards.
                        hold_cnt = 0;
                        // "Depth/shift stage" combo is stable for some time and not getting worse or better, skip waiting, good enough.
                        break;
                    }
                }
                else
                {
                    // If shift stage became worse - count it as another "depth holding" iteration.
                    // Count down depth holding.
                    hold_cnt--;
                    //qInfo()<<"[RAS] Lvl"<<index<<", depth"<<low_depth<<", shift"<<low_shift<<", hold"<<hold_cnt;
                    if(hold_cnt==0)
                    {
                        // Hysteresis depth is stable for some time, not getting worse or better, skip waiting, good enough.
                        break;
                    }
                }
            }
            else
            {
                // Count down depth holding, while waiting for depth to go lower.
                hold_cnt--;
                //qInfo()<<"[RAS] Lvl"<<index<<", depth"<<low_depth<<", shift"<<low_shift<<", HOLD"<<hold_cnt;
                if(hold_cnt==0)
                {
                    // Hysteresis depth is stable for some time, not getting worse or better, skip waiting, good enough.
                    break;
                }
            }
        }
        if(index==low_lvl) break;
        index--;
    };

    //qDebug()<<"Pick time:"<<dbg_timer.nsecsElapsed()/1000;

    if(good_ref_det!=false)
    {
        // Take the middle of the span.
        picked_ref = (high_ref-low_ref);
        picked_ref = picked_ref/2;
        picked_ref = low_ref+picked_ref;
        // Good reference value is picked, return it.
        (*ref_result) = picked_ref;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Index selected: %u (CRC: 0x%x), with hysteresis depth %u and pixel shift %u",
                             picked_ref, crcs[picked_ref].crc, low_depth, low_shift);
            qInfo()<<log_line;
        }
#endif
        return SPAN_OK;
    }
    else
    {
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[LB] No index found, no value selected";
        }
#endif
        return SPAN_NOT_FOUND;
    }
}

//------------------------ Print CRC stats to log.
void Binarizer::textDumpCRCSweep(crc_handler_t *crc_array, uint8_t low_level, uint8_t high_level, uint16_t target1, uint16_t target2)
{
#ifdef LB_EN_DBG_OUT
    QString stats, depth, shifts;
    for(uint16_t index=low_level;index<=high_level;index++)
    {
        if(crc_array[index].result==REF_CRC_OK)
        {
            if(index==target1)
            {
                stats += "#";
            }
            else if(index==target2)
            {
                stats += "^";
            }
            else
            {
                stats += "|";
            }
            depth += QString::number(crc_array[index].hyst_dph, 16);
            shifts += QString::number(crc_array[index].shift_stg, 16);
        }
        else if(crc_array[index].result==REF_CRC_COLL)
        {
            stats += "@";
            depth += QString::number(crc_array[index].hyst_dph, 16);
            shifts += QString::number(crc_array[index].shift_stg, 16);
        }
        else if(crc_array[index].result==REF_BAD_CRC)
        {
            if(index==target2)
            {
                stats += "^";
            }
            else
            {
                stats += "-";
            }
            depth += QString::number(crc_array[index].hyst_dph, 16);
            shifts += QString::number(crc_array[index].shift_stg, 16);
        }
        else
        {
            if(index==target2)
            {
                stats += "^";
            }
            else
            {
                stats += ".";
            }
            depth += ".";
            shifts += ".";
        }
    }
    qInfo()<<"[LB] [.] = no data, [-] = bad, [|] = valid, [@] = invalidated, [#] = selected, [^] - alt";
    qInfo()<<"[LB] Sweep "<<stats;
    qInfo()<<"[LB] Depth "<<depth;
    qInfo()<<"[LB] Shifts"<<shifts;
#endif
}

//------------------------ Find number of encounters of the most frequent brightness.
uint16_t Binarizer::getMostFrequentBrightnessCount(const uint16_t *brght_sprd)
{
    uint8_t brt_lev;
    uint16_t highest_freq;
    brt_lev = 255;
    highest_freq = 0;
    // Search for highest non-zero value.
    while(1)
    {
        if(brght_sprd[brt_lev]>highest_freq)
        {
            // Store maximum for useful data.
            highest_freq = brght_sprd[brt_lev];
        }
        if(brt_lev==0) break;
        brt_lev--;
    }
    return highest_freq;
}

//------------------------ Find lowest non-zero value in the array.
uint8_t Binarizer::getUsefullLowLevel(const uint16_t *brght_sprd)
{
    bool filtered_found;
    uint8_t brt_lev, lowest_lev;
    uint16_t min_freq;

    filtered_found = false;
    lowest_lev = brt_lev = 0;
    // Get most frequent brightness encounters.
    min_freq = getMostFrequentBrightnessCount(brght_sprd);
    // Calculate limit to cut noise.
    min_freq = min_freq/64;
    // Search for lowest non-zero value.
    while(brt_lev<digi_set.max_black_lvl)
    {
        if(brght_sprd[brt_lev]>min_freq)
        {
            // Store minimum for useful data.
            lowest_lev = brt_lev;
            filtered_found = true;
            // Search is done.
            break;
        }
        brt_lev++;
    }
    if(filtered_found==false)
    {
        // No result in the allowed range of brightnesses.
        // Repeat search without filtering.
        while(brt_lev<digi_set.max_black_lvl)
        {
            if(brght_sprd[brt_lev]>0)
            {
                // Store minimum for useful data.
                lowest_lev = brt_lev;
                // Search is done.
                break;
            }
            brt_lev++;
        }
    }
    return lowest_lev;
}

//------------------------ Find highest non-zero value in the array.
uint8_t Binarizer::getUsefullHighLevel(const uint16_t *brght_sprd)
{
    bool filtered_found;
    uint8_t brt_lev, highest_lev;
    uint16_t min_freq;

    filtered_found = false;
    highest_lev = brt_lev = 255;
    // Get most frequent brightness encounters.
    min_freq = getMostFrequentBrightnessCount(brght_sprd);
    // Calculate limit to cut noise.
    min_freq = min_freq/64;
    // Search for highest non-zero value.
    while(brt_lev>=digi_set.min_white_lvl)
    {
        if(brght_sprd[brt_lev]>min_freq)
        {
            // Store maximum for useful data.
            highest_lev = brt_lev;
            // Search is done.
            break;
        }
        brt_lev--;
    }
    if(filtered_found==false)
    {
        // No result in the allowed range of brightnesses.
        // Repeat search without filtering.
        while(brt_lev>=digi_set.min_white_lvl)
        {
            if(brght_sprd[brt_lev]>0)
            {
                // Store maximum for useful data.
                highest_lev = brt_lev;
                // Search is done.
                break;
            }
            brt_lev--;
        }
    }
    return highest_lev;
}

//------------------------ Find WHITE and BLACK levels for [PCM1Line].
void Binarizer::findPCM1BW(uint16_t *brght_sprd)
{
    uint16_t pixel_limit;
    uint16_t search_lim;
    uint32_t temp_calc;
    bool suppress_log;

#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !((log_level&LOG_PROCESS)!=0);

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[LB] ---------- B&W levels search started (for PCM-1)...";
    }
#endif

    // Calculate available length of source line.
    pixel_limit = scan_end - scan_start;
    // Calculate margin from the right edge of the line.
    temp_calc = pixel_limit/32;
    search_lim = scan_end-(uint16_t)temp_calc;
    // Calculate margin from the left edge of the line.
    temp_calc = pixel_limit/8;
    pixel_limit = scan_start+(uint16_t)temp_calc;
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Scanning video line pixels [%03u:%04u] for brightness levels...",
                         pixel_limit, search_lim);
        qInfo()<<log_line;
    }
#endif
    // Calculate spread of brightness for the video line.
    for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }
}

//------------------------ Find WHITE and BLACK levels for [PCM16X0SubLine].
void Binarizer::findPCM16X0BW(uint16_t *brght_sprd)
{
    uint16_t pixel_limit;
    uint16_t search_lim;
    uint32_t temp_calc;
    bool suppress_log;

#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !((log_level&LOG_PROCESS)!=0);

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[LB] ---------- B&W levels search started (for PCM-16x0)...";
    }
#endif

    // Calculate available length of source line.
    pixel_limit = scan_end - scan_start;
    // PCM-16x0 silent pattern has many black pixels some white ones at three CRC areas.
    // Limit scan area to prevent BLACK level saturating encounters.
    // Calculate width of CRC area and some.
    temp_calc = pixel_limit/8;
    // Calculate offset to the left CRC area.
    pixel_limit = pixel_limit/5;
    // Offset CRC area.
    search_lim = pixel_limit+(uint16_t)temp_calc;
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Scanning left part of video line pixels [%03u:%04u] for brightness levels...",
                         pixel_limit, search_lim);
        qInfo()<<log_line;
    }
#endif
    // Calculate spread of brightness for the first CRC area.
    for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }
    // Calculate offset to the middle CRC area.
    pixel_limit = (uint16_t)temp_calc*4;
    pixel_limit += (uint16_t)temp_calc/2;
    // Offset CRC area.
    search_lim = pixel_limit+(uint16_t)temp_calc;
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Scanning middle part of video line pixels [%03u:%04u] for brightness levels...",
                         pixel_limit, search_lim);
        qInfo()<<log_line;
    }
#endif
    // Calculate spread of brightness for the first CRC area.
    for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }
    // Calculate available length of source line.
    pixel_limit = scan_end - scan_start;
    // Calculate margin from the left edge of the line.
    search_lim = scan_end-pixel_limit/64;
    pixel_limit = search_lim-(uint16_t)temp_calc;
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Scanning right part of video line pixels [%03u:%04u] for brightness levels...",
                         pixel_limit, search_lim);
        qInfo()<<log_line;
    }
#endif
    // Calculate spread of brightness for the first CRC area.
    for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }
}

//------------------------ Find WHITE and BLACK levels for [STC007Line].
void Binarizer::findSTC007BW(uint16_t *brght_sprd)
{
    uint8_t pixel_val, brt_lev, marker_detect_stage;
    uint8_t br_black, br_mark_white, useful_low, useful_high;
    uint8_t low_scan_limit, high_scan_limit, range_limit;
    uint8_t bin_level, bin_low, bin_high;
    uint16_t pixel, pixel_limit;
    uint16_t mark_ed_bit_start,         // Pixel coordinate of the start of the "1111" STOP bits and of the end of the "0" STOP bit.
             mark_ed_bit_end;           // Pixel coordinate of the end of the "1111" STOP bits.
    uint16_t white_lvl_count, search_lim;
    uint32_t temp_calc;
    bool suppress_log, white_level_detected;

#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !((log_level&LOG_PROCESS)!=0);

    // Pre-calculate rough brightness spread (can contain black borders and other garbage on a frame perimeter).
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[LB] ---------- B&W levels search started (for STC-007)...";
    }
#endif

    // Calculate brightness spread for the line and find WHITE level for the STOP marker.
    // Set the coordinate limit for scan.
    search_lim = scan_start + estimated_ppb*10;        // Calculate spread of brightness for the left side of the video line (containing PCM START marker).
#ifdef LB_LOG_BW_VERBOSE
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Scanning left side [%03u:%03u] pixels for brightness levels...",
                         scan_start, search_lim);
        qInfo()<<log_line;
    }
#endif
    for(uint16_t pixel=scan_start;pixel<search_lim;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }
    // Calculate spread of brightness for the right side of the video line (containing PCM STOP marker).
    // Set the coordinate limit for scan.
    search_lim = scan_end - estimated_ppb*20;
#ifdef LB_LOG_BW_VERBOSE
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Scanning right side [%04u:%04u] pixels for brightness levels...",
                         search_lim, scan_end);
        qInfo()<<log_line;
    }
#endif
    for(uint16_t pixel=search_lim;pixel<=scan_end;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }
#ifdef LB_EN_DBG_OUT
    if(((log_level&LOG_BRIGHT)!=0)/*&&(suppress_log==false)*/)
    {
        QString stats;
        for(uint16_t index=0;index<256;index++)
        {
            stats += QString::number(brght_sprd[index]);
            if(index!=255)
            {
                stats += ",";
            }
        }
        qInfo()<<"[LB] Borders brighness spread:"<<stats;
    }
#endif

    // Find span of non-zero brightness encounters.
    useful_low = low_scan_limit = br_black = getUsefullLowLevel(brght_sprd);
    useful_high = high_scan_limit = br_mark_white = getUsefullHighLevel(brght_sprd);
    // Calculate range of brightnesses.
    range_limit = high_scan_limit-low_scan_limit;
    // Calculte limits for WHITE peak search.
    high_scan_limit -= (range_limit/4);
    // Calculate delta distance limits.
    bin_high = range_limit/8;       // Re-purpose variables that are not needed now.

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Useful borders brightness data at: [%03u:%03u]", useful_low, useful_high);
        qInfo()<<log_line;
        log_line.sprintf("[LB] WHITE peak search limits: [%03u:%03u]", high_scan_limit, useful_high);
        qInfo()<<log_line;
    }
#endif
#ifdef LB_LOG_BW_VERBOSE
    if(suppress_log==false)
    {
        qInfo()<<"[LB] Searching WHITE peak...";
    }
#endif
    // Find "white" level (via most frequent high-value).
    brt_lev = useful_high;
    white_lvl_count = 0;
    white_level_detected = false;
    // Perform encounters search from "white" downwards.
    while(brt_lev>=high_scan_limit)
    {
        // Search for maximum encounters in high brightness.
        if(brght_sprd[brt_lev]>white_lvl_count)
        {
            // Store current encounters of WHITE level.
            white_lvl_count = brght_sprd[brt_lev];
            // Store new WHITE level (as coordinate of peak encounters).
            br_mark_white = brt_lev;
            // Store "WHITE level found" flag.
            white_level_detected = true;
        }
#ifdef LB_LOG_BW_VERBOSE
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Step: %03u (%03u), enc.: %03u pcs. at %03u, det.: %u",
                             brt_lev, brght_sprd[brt_lev], white_lvl_count, br_mark_white, white_level_detected?1:0);
            qInfo()<<log_line;
        }
#endif
        if(white_level_detected!=false)
        {
            // If white level was found in process, check for current distance from that peak.
            if((br_mark_white-brt_lev)>=bin_high)
            {
                // No need to search so far from found peak.
#ifdef LB_LOG_BW_VERBOSE
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Too far from WHITE peak (%03u->%03u), stopping search...",
                                     br_mark_white, brt_lev);
                    qInfo()<<log_line;
                }
#endif
                break;
            }
        }
        brt_lev--;
    }
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        if(white_level_detected==false)
        {
            log_line.sprintf("[LB] WHITE level was NOT detected!");
            qInfo()<<log_line;
        }
        else
        {
            log_line.sprintf("[LB] WHITE level detected as [%03u] with [%03u] encounters", br_mark_white, white_lvl_count);
            qInfo()<<log_line;
        }
    }
    else if((log_level&LOG_BRIGHT)!=0)
    {
        log_line.sprintf("[LB] WHITE level for STOP marker: [%03u] with [%03u] encounters", br_mark_white, white_lvl_count);
        qInfo()<<log_line;
    }
#endif
    // Refill brightness statistics to fallback to.
    // Calculate available length of source line.
    pixel_limit = scan_end - scan_start;
    // Calculate margin from sides of the line (to exclude markers).
    temp_calc = pixel_limit/8;
    // Calculate stats gathering range.
    pixel_limit = scan_start + (uint16_t)temp_calc;
    search_lim = scan_end - (uint16_t)temp_calc;
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Trying to gather brightness stats data between [%03u:%03u]...",
                         pixel_limit, search_lim);
        qInfo()<<log_line;
    }
#endif
    // Calculate spread of brightness for the video line without marker areas.
    fillArray(brght_sprd, 256);
    for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }

    // Detect presence and coordinates of PCM STOP marker,
    // if found, recalculate brightness spread for CRC part of the line, excluding white line.
    marker_detect_stage = STC007Line::MARK_ED_START;
    mark_ed_bit_start = mark_ed_bit_end = 0;
    // Check if white level was detected (if not - markers can not be found).
    if(white_level_detected!=false)
    {
        // Define rough reference level for white detection.
        bin_level = pickCenterRefLevel(useful_low, br_mark_white);
        bin_high = bin_low = bin_level;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Rough ref. level: %03u (B&W)", bin_level);
            qInfo()<<log_line;
        }
#endif
        // Calculate minimum pixel number for the marker search.
        if(mark_end_min>(estimated_ppb*6))
        {
            pixel_limit = mark_end_min-estimated_ppb*6;
        }
        else
        {
            pixel_limit = 0;
        }
        pixel = scan_end;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Searching STOP marker, detection in range [%04u...%04u], full marker in [%04u...%04u]",
                             pixel, mark_end_min, pixel, pixel_limit);
            qInfo()<<log_line;
        }
#endif
        // Find position of PCM ending marker through backwards search.
        while(pixel>pixel_limit)
        {
            // Get pixel brightness level (8 bit grayscale).
            pixel_val = getPixelBrightness(pixel);
#ifdef LB_LOG_BW_VERBOSE
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Pixel [%04u] val.: [%03u], ref.: [%03u|%03u]",
                                 pixel, pixel_val, bin_low, bin_high);
                qInfo()<<log_line;
            }
#endif
            if(marker_detect_stage==STC007Line::MARK_ED_START)
            {
                // Check additional limit on start of the STOP marker.
                if(pixel<mark_end_min)
                {
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB-%03u] No STOP maker started within limits [%03u:%03u], search stopped!", bin_level, scan_end, mark_end_min);
                        qInfo()<<log_line;
                    }
#endif
                    // Stop the search.
                    break;
                }
                // Detect /\ transition to "1" marker bit.
                if(pixel_val>=bin_low)
                {
                    // Save pixel coordinate of the end of STOP marker (4-bit wide).
                    mark_ed_bit_end = pixel+1;
                    // Update STOP marker detection stage.
                    marker_detect_stage=STC007Line::MARK_ED_TOP;
#ifdef LB_LOG_BW_VERBOSE
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] Got 0->1 transition from the end of STOP-marker";
                    }
#endif
                }
            }
            else if(marker_detect_stage==STC007Line::MARK_ED_TOP)
            {
                // Detect \/ transition to "0" marker bit.
                if(pixel_val<bin_high)
                {
                    // Save pixel coordinate of the beginning of STOP marker (4-bit wide).
                    mark_ed_bit_start = pixel+1;
                    // Update STOP marker detection stage.
                    marker_detect_stage = STC007Line::MARK_ED_BOT;
                    // Check if "1" bit length is more than twice of estimated PPB.
                    if((mark_ed_bit_end-mark_ed_bit_start)>=(estimated_ppb*2))
                    {
                        // Update STOP marker detection stage.
                        marker_detect_stage = STC007Line::MARK_ED_LEN_OK;
#ifdef LB_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[LB] STOP-marker found at [%03u:%03u]", mark_ed_bit_start, mark_ed_bit_end);
                            qInfo()<<log_line;
                        }
#endif
#ifdef LB_LOG_BW_VERBOSE
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[LB] STOP-marker length is ok (%03u>=%03u)", (mark_ed_bit_end-mark_ed_bit_start), (estimated_ppb*2));
                            qInfo()<<log_line;
                        }
#endif
                        // Finish STOP marker search.
                        break;
                    }
                    else
                    {
                        // Wrong "1" bit length, probably noise - reset state machine.
                        marker_detect_stage = STC007Line::MARK_ED_START;
#ifdef LB_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[LB] Erroneous STOP-marker at [%03u:%03u], restarting...", mark_ed_bit_start, mark_ed_bit_end);
                            qInfo()<<log_line;
                        }
#endif
#ifdef LB_LOG_BW_VERBOSE
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[LB] Insufficient STOP-marker length (%03u<%03u)", (mark_ed_bit_end-mark_ed_bit_start), (estimated_ppb*2));
                            qInfo()<<log_line;
                        }
#endif
                    }
                }
            }
            // Go to previous pixel in the line.
            pixel--;
        }
        STC007Line *temp_stc;
        temp_stc = static_cast<STC007Line *>(out_pcm_line);
        // Store final state.
        temp_stc->mark_ed_stage = marker_detect_stage;
        // Save coordinates.
        temp_stc->coords.data_stop = mark_ed_bit_start;
        temp_stc->marker_stop_ed_coord = mark_ed_bit_end;
        // Check if PCM ending marker was found.
        if(temp_stc->hasStopMarker()!=false)
        {
            uint16_t sprd_cnt;
            // Set the limited distance (covering CRC, ECC and data area) from STOP-marker for brightness spread re-calc.
            // ~64 PCM bits from STOP marker.
            search_lim = estimated_ppb*64;
            // Calculate negative coordinate offset from the STOP marker.
            if(search_lim>mark_ed_bit_start)
            {
                search_lim = mark_start_max;
            }
            else
            {
                search_lim = mark_ed_bit_start-search_lim;
            }
            // Reset brightness spread data.
            fillArray(brght_sprd, 256);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Re-gathering brightness stats data between [%04u:%04u]...",
                                 search_lim, (mark_ed_bit_start-1));
                qInfo()<<log_line;
            }
#endif
            sprd_cnt = 0;
            // Re-calculate spread of brighness for the end of the line (excluding black border and garbage).
            for(uint16_t pixel=(mark_ed_bit_start-1);pixel>search_lim;pixel--)
            {
                brght_sprd[getPixelBrightness(pixel)]++;
                sprd_cnt++;
            }
            if(sprd_cnt<32)
            {
                pixel_limit = scan_end - scan_start;
                pixel_limit = pixel_limit/8;
                search_lim = scan_end - pixel_limit;
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Re-gathering brightness stats data between [%04u:%04u]...",
                                     pixel_limit, search_lim);
                    qInfo()<<log_line;
                }
#endif
                for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
                {
                    brght_sprd[getPixelBrightness(pixel)]++;
                }
            }
        }
#ifdef LB_EN_DBG_OUT
        else
        {
            if(suppress_log==false)
            {
                qInfo()<<"[LB] STOP marker was NOT found!";
            }
        }
#endif
    }
}

//------------------------ Find WHITE and BLACK levels for [ArVidLine].
void Binarizer::findArVidBW(uint16_t *brght_sprd)
{
    uint16_t pixel_limit;
    uint16_t search_lim;
    uint32_t temp_calc;
    bool suppress_log;

#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !((log_level&LOG_PROCESS)!=0);

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[LB] ---------- B&W levels search started (for ArVid Audio)...";
    }
#endif

    // Calculate available length of source line.
    pixel_limit = scan_end - scan_start;
    // Calculate margin from the right edge of the line.
    temp_calc = pixel_limit/4;
    search_lim = scan_start+(uint16_t)temp_calc;
    // Calculate margin from the left edge of the line.
    temp_calc = pixel_limit/32;
    pixel_limit = scan_start+(uint16_t)temp_calc;
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Scanning video line pixels [%03u:%04u] for brightness levels...",
                         pixel_limit, search_lim);
        qInfo()<<log_line;
    }
#endif
    // Calculate spread of brightness for the video line.
    for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
    {
        brght_sprd[getPixelBrightness(pixel)]++;
    }
}

//------------------------ Find WHITE and BLACK levels.
bool Binarizer::findBlackWhite()
{
    uint8_t brt_lev;
    uint8_t br_black, br_white, br_mark_white, useful_low, useful_high;
    uint8_t low_scan_limit, high_scan_limit, range_limit;
    uint8_t bin_low, bin_high;
    uint16_t black_lvl_count, white_lvl_count, search_lim;
    uint16_t pixel_limit;
    uint32_t temp_calc;
    uint16_t brightness_spread[256];    // Brightness spread statistics.
    bool suppress_log, black_level_detected, white_level_detected;

    // Reset final BLACK and WHITE values.
    br_black = 0;
    br_white = br_mark_white = 255;
    // Reset brightness spread data.
    fillArray(brightness_spread, 256);
#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !((log_level&LOG_PROCESS)!=0);

    // Gather pixel brightness statistics.
    // Select PCM type specific search algorythm.
    if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
    {
        findPCM1BW(brightness_spread);
    }
    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
    {
        findPCM16X0BW(brightness_spread);
    }
    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
    {
        findSTC007BW(brightness_spread);
    }
    else if(out_pcm_line->getPCMType()==PCMLine::TYPE_ARVA)
    {
        findArVidBW(brightness_spread);
    }
    else
    {
        // Unspecified PCM type.
        // Calculate available length of source line.
        pixel_limit = scan_end - scan_start;
        // Calculate margins from the edges of the line.
        temp_calc = pixel_limit/16;
        pixel_limit = scan_start+(uint16_t)temp_calc;
        search_lim = scan_end-(uint16_t)temp_calc;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Scanning video line pixels [%03u:%04u] for brightness levels...",
                             pixel_limit, search_lim);
            qInfo()<<log_line;
        }
#endif
        // Calculate spread of brightness for the video line.
        for(uint16_t pixel=pixel_limit;pixel<search_lim;pixel++)
        {
            brightness_spread[getPixelBrightness(pixel)]++;
        }
    }

    // Find BLACK level and update WHITE level from brighness spread data.
#ifdef LB_EN_DBG_OUT
    if((log_level&LOG_BRIGHT)!=0)
    {
        log_line.sprintf("[LB] Line [%03u][%03u] final brighness spread: ", out_pcm_line->frame_number, out_pcm_line->line_number);
        for(uint16_t index=0;index<256;index++)
        {
            log_line += QString::number(brightness_spread[index]);
            if(index!=255)
            {
                log_line += ",";
            }
        }
        qInfo()<<log_line;
    }
#endif

    // Find span of non-zero brightness encounters.
    useful_low = low_scan_limit = br_black = getUsefullLowLevel(brightness_spread);
    useful_high = high_scan_limit = br_white = getUsefullHighLevel(brightness_spread);
    // Calculate range of brightnesses.
    range_limit = high_scan_limit-low_scan_limit;
    // Calculte limits for BLACK and WHITE peak searches.
    //low_scan_limit += (range_limit/4);
    //high_scan_limit -= (range_limit/2);
    low_scan_limit += (range_limit/3);
    high_scan_limit -= (range_limit/3);
    // Calculate delta distance limits.
    temp_calc = range_limit;
    temp_calc = temp_calc*10/100;
    bin_low = (uint8_t)temp_calc;       // Re-purpose variables that are not needed now.
    temp_calc = range_limit;
    temp_calc = temp_calc*12/100;
    bin_high = (uint8_t)temp_calc;      // Re-purpose variables that are not needed now.
    // Calculate lowest possible encounter count.
    search_lim = getMostFrequentBrightnessCount(brightness_spread);
    search_lim = search_lim/64;         // Re-purpose variables that are not needed now.

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Most frequent brightness encounters: [%03u], encounter limit: [%03u]", getMostFrequentBrightnessCount(brightness_spread), search_lim);
        qInfo()<<log_line;
        log_line.sprintf("[LB] Useful brightness data at: [%03u:%03u]", useful_low, useful_high);
        qInfo()<<log_line;
        log_line.sprintf("[LB] Peak search limits: [%03u:%03u], [%03u:%03u]", useful_low, low_scan_limit, high_scan_limit, useful_high);
        qInfo()<<log_line;
        log_line.sprintf("[LB] Maximum distances from peaks: [%03u], [%03u]", bin_low, bin_high);
        qInfo()<<log_line;
    }
#endif
#ifdef LB_LOG_BW_VERBOSE
    if(suppress_log==false)
    {
        qInfo()<<"[LB] Searching BLACK peak...";
    }
#endif
    // Find BLACK level (via most frequent low-value).
    brt_lev = useful_low;
    black_lvl_count = 0;
    black_level_detected = false;
    // Perform encounters search from BLACK upwards.
    while(brt_lev<=low_scan_limit)
    {
        // Search for maximum encounters in low brightness.
        if(brightness_spread[brt_lev]>black_lvl_count)
        {
            // Store current encounters of BLACK level.
            black_lvl_count = brightness_spread[brt_lev];
            // Check for frequent enough encounter (noise prevention).
            if(black_lvl_count>search_lim)
            {
                // Store new BLACK level (as coordinate of peak encounters).
                br_black = brt_lev;
                // Store "BLACK level found" flag.
                black_level_detected = true;
            }
        }
#ifdef LB_LOG_BW_VERBOSE
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Step: %03u (%03u), enc.: %03u pcs. at %03u, det.: %u",
                             brt_lev, brightness_spread[brt_lev], black_lvl_count, br_black, black_level_detected?1:0);
            qInfo()<<log_line;
        }
#endif
        if(black_level_detected!=false)
        {
            // If black level was found, check for current distance from that peak.
            if((brt_lev-br_black)>=bin_low)
            {
                // No need to search so far from found peak.
#ifdef LB_LOG_BW_VERBOSE
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Too far from BLACK peak (%03u->%03u), stopping search...",
                                     br_black, brt_lev);
                    qInfo()<<log_line;
                }
#endif
                break;
            }
        }
        brt_lev++;
    }
#ifdef LB_LOG_BW_VERBOSE
    if(suppress_log==false)
    {
        qInfo()<<"[LB] Searching WHITE peak...";
    }
#endif
    // Find WHITE level (via most frequent high-value).
    brt_lev = useful_high;
    white_lvl_count = 0;
    white_level_detected = false;
    // Check if BLACK level was found: no need to search for WHITE if there is no BLACK.
    if(black_level_detected!=false)
    {
        // Perform encounters search from WHITE downwards.
        while(brt_lev>=high_scan_limit)
        {
            // Check for contrast limits.
            if(brt_lev<(br_black+digi_set.min_contrast))
            {
                // Contrast is too low.
#ifdef LB_LOG_BW_VERBOSE
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Contrast is too low (%03u<->%03u), stopping search...",
                                     br_black, brt_lev);
                    qInfo()<<log_line;
                }
#endif
                break;
            }
            // Search for maximum encounters in high brightness.
            if(brightness_spread[brt_lev]>white_lvl_count)
            {
                // Store current encounters of WHITE level.
                white_lvl_count = brightness_spread[brt_lev];
                // Check for frequent enough encounter (noise prevention).
                if(white_lvl_count>search_lim)
                {
                    // Store new WHITE level (as coordinate of peak encounters).
                    br_white = brt_lev;
                    // Store "WHITE level found" flag.
                    white_level_detected = true;
                }
            }
#ifdef LB_LOG_BW_VERBOSE
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Step: %03u (%03u), enc.: %03u pcs. at %03u, det.: %u",
                                 brt_lev, brightness_spread[brt_lev], white_lvl_count, br_white, white_level_detected?1:0);
                qInfo()<<log_line;
            }
#endif
            if(white_level_detected!=false)
            {
                // If white level was found in process, check for current distance from that peak.
                if((br_white-brt_lev)>=bin_high)
                {
                    // No need to search so far from found peak.
#ifdef LB_LOG_BW_VERBOSE
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Too far from WHITE peak (%03u->%03u), stopping search...",
                                         br_white, brt_lev);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
            brt_lev--;
        }
    }

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        if(black_level_detected==false)
        {
            log_line.sprintf("[LB] BLACK level was NOT detected!");
            qInfo()<<log_line;
        }
        else
        {
            log_line.sprintf("[LB] BLACK level detected as [%03u] with [%03u] encounters", br_black, black_lvl_count);
            qInfo()<<log_line;
        }
        if(white_level_detected==false)
        {
            log_line.sprintf("[LB] WHITE level was NOT detected!");
            qInfo()<<log_line;
        }
        else
        {
            log_line.sprintf("[LB] WHITE level detected as [%03u] with [%03u] encounters", br_white, white_lvl_count);
            qInfo()<<log_line;
        }
    }
#endif

    // Check for limits.
    if((black_level_detected!=false)&&(white_level_detected!=false))
    {
        bool invalidate;
        invalidate = false;
        if(br_white<br_black)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Black level is less than white! Invalidating levels.";
            }
#endif
            invalidate = true;
        }
        else if((br_white-br_black)<digi_set.min_contrast)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Too low contrast! Invalidating levels.";
            }
#endif
            invalidate = true;
        }
        else if((do_ref_lvl_sweep!=false)&&((br_white-br_black)<digi_set.min_valid_crcs))
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Black level is too close to white! Invalidating levels.";
            }
#endif
            invalidate = true;
        }
        else if(br_black>digi_set.max_black_lvl)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Black level is too high! Invalidating levels.";
            }
#endif
            invalidate = true;
        }
        else if(br_white<digi_set.min_white_lvl)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] White level is too low! Invalidating levels.";
            }
#endif
            invalidate = true;
        }
        // Check if BLACK and WHITE levels should be invalidated.
        if(invalidate!=false)
        {
            // Reset B&W detection.
            black_level_detected = white_level_detected = false;
            br_black = useful_low;
            br_white = useful_high;
        }
    }

#ifdef LB_EN_DBG_OUT
    if((suppress_log==false)||((log_level&LOG_BRIGHT)!=0))
    {
        log_line.sprintf("[LB] B&W final: BLACK: [%03u] (ok: %u), WHITE: [%03u] (ok: %u)",
                         br_black, black_level_detected?1:0, br_white, white_level_detected?1:0);
        qInfo()<<log_line;
    }
#endif
    // BLACK and WHITE scan completed.
    was_BW_scanned = true;

    // Store detected levels.
    out_pcm_line->black_level = br_black;
    out_pcm_line->white_level = br_white;
    if((black_level_detected==false)||(white_level_detected==false))
    {
        out_pcm_line->setBWLevelsState(false);
        return false;
    }
    else
    {
        out_pcm_line->setBWLevelsState(true);
        return true;
    }
}

//------------------------ Get value of "low level" for hysteresis (for "0" -> "1" transition).
uint8_t Binarizer::getLowLevel(uint8_t in_lvl, uint8_t diff)
{
    if(in_lvl>diff)
    {
        in_lvl -= diff;
    }
    else
    {
        in_lvl = 1;
    }
    return in_lvl;
}

//------------------------ Get value of "high level" for hysteresis (for "1" -> "0" transition).
uint8_t Binarizer::getHighLevel(uint8_t in_lvl, uint8_t diff)
{
    if(in_lvl<(255-diff))
    {
        in_lvl += diff;
    }
    else
    {
        in_lvl = 254;
    }
    return in_lvl;
}

//------------------------ Pick reference level between BLACK and WHITE levels (put in limits if required).
uint8_t Binarizer::pickCenterRefLevel(uint8_t lvl_black, uint8_t lvl_white)
{
    uint8_t br_delta, res_lvl;
    // Check contrast and calculate reference brightness level by picking value in between BLACK and WHITE.
    // Get delta between "white" and "black".
    br_delta = lvl_white-lvl_black;
    // Check for contrast.
    if(br_delta>=digi_set.min_contrast)
    {
        // Contrast is Ok.
        // Calculate PCM reference level.
        br_delta = br_delta/2;
        // Shift reference level by black level.
        res_lvl = br_delta+lvl_black;
        // Limit reference level.
        if(res_lvl<digi_set.min_ref_lvl)
        {
            res_lvl = digi_set.min_ref_lvl;
        }
        else if(res_lvl>digi_set.max_ref_lvl)
        {
            res_lvl = digi_set.max_ref_lvl;
        }
    }
    else
    {
#ifdef LB_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[LB] Contrast is too low!";
        }
#endif
        // Contrast is too low, set reference beyond the range of brightnesses.
        if(lvl_white<digi_set.max_ref_lvl)
        {
            res_lvl = digi_set.max_ref_lvl;
        }
        else
        {
            res_lvl = digi_set.min_ref_lvl;
        }
    }

    return res_lvl;
}

//------------------------ Perform reference level sweep from one level to another (multithreadable).
void Binarizer::sweepRefLevel(PCMLine *pcm_line, crc_handler_t *crc_res)
{
    bool suppress_log, skip_bin;
    uint8_t low_lvl, high_lvl, read_result;
    uint16_t ref_index;
    PCM1Line temp_pcm1;
    PCM16X0SubLine temp_pcm16x0;
    STC007Line temp_stc;
    PCMLine *dummy_line;
    CoordinatePair forced_coords;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&((log_level&LOG_REF_SWEEP)!=0));

    if(pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
    {
        dummy_line = static_cast<PCMLine *>(&temp_pcm1);
    }
    else if(pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
    {
        if(temp_pcm16x0.line_part!=PCM16X0SubLine::PART_LEFT)
        {
            return;
        }
        dummy_line = static_cast<PCMLine *>(&temp_pcm16x0);
    }
    else if(pcm_line->getPCMType()==PCMLine::TYPE_STC007)
    {
        dummy_line = static_cast<PCMLine *>(&temp_stc);
    }
    else
    {
        return;
    }

    // Check if data coordinates are forced.
    if(digi_set.en_force_coords!=false)
    {
        // Calculate forced data coordinates.
        forced_coords.data_start = (int16_t)scan_start+digi_set.horiz_coords.data_start;
        forced_coords.data_stop = (int16_t)scan_end-digi_set.horiz_coords.data_stop;
        if(video_line->isDoubleWidth()!=false)
        {
            forced_coords.data_start += digi_set.horiz_coords.data_start;
            forced_coords.data_stop -= digi_set.horiz_coords.data_stop;
        }
    }

    // Take scan span from B&W levels.
    low_lvl = pcm_line->black_level;
    high_lvl = pcm_line->white_level;

    // Make reference not to interfere with B&W levels.
    low_lvl += 1;
    high_lvl -= 1;

    if(digi_set.min_ref_lvl>low_lvl)
    {
        low_lvl = digi_set.min_ref_lvl;
    }
    if(digi_set.max_ref_lvl<high_lvl)
    {
        high_lvl = digi_set.max_ref_lvl;
    }

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_REF_SWEEP)!=0))
    {
        log_line.sprintf("[LB] Reference level sweep: [%03u...%03u]", low_lvl, high_lvl);
        qInfo()<<log_line;
    }
#endif

    // Cycle through provided range of brightnesses to get CRC result stats.
    ref_index = high_lvl;
    while(ref_index>=low_lvl)
    {
        // Reset line container.
        dummy_line->clear();
        // Set pixel limits.
        dummy_line->setSourcePixels(0, (video_line->pixel_data.size()-1));
        dummy_line->setFromDoubledState(video_line->isDoubleWidth());
        // Preset B&W levels to allow hysteresis calculation.
        dummy_line->black_level = low_lvl;
        dummy_line->white_level = high_lvl;
        // Preset current reference level.
        dummy_line->ref_level = ref_index;
        // Check if data coordinates are forced.
        if(forced_coords.areValid()==false)
        {
            // Check if data coordinates are preset.
            if(in_def_coord.areValid()!=false)
            {
                // There are externally provided coordinates.
                // Try provided coordinates first, because there is a possibility
                // that current data coordinates will not be found due to marker corruption.
                skip_bin = false;
                if(pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                {
                    // Check if all markers must be present for valid CRC.
                    if(digi_set.en_good_no_marker!=false)
                    {
                        // Both markers are required to produce valid CRC.
                        // Find coordinates for PCM START and STOP markers with current reference level.
                        findSTC007Coordinates(&temp_stc, suppress_log);
                        if(temp_stc.hasMarkers()==false)
                        {
                            // At least one marker was not found.
                            skip_bin = true;
                        }
                    }
                }
                // Check if fast run is allowed.
                if(skip_bin!=false)
                {
                    // Both markers are present or are not required.
#ifdef LB_EN_DBG_OUT
                    if((log_level&LOG_REF_SWEEP)!=0)
                    {
                        log_line.sprintf("[LB] External coordinates for first-try run provided: [%03u|%04u]", in_def_coord.data_start, in_def_coord.data_stop);
                        qInfo()<<log_line;
                    }
#endif
                    // Preset data coordinates.
                    dummy_line->coords = in_def_coord;
                    // Try to read data with preset coordinates
                    // (even if markers are damaged and can not be found)
                    readPCMdata(dummy_line, 0, suppress_log);
                }
            }
        }
        // Check if preset coordinates produced a valid CRC.
        if(dummy_line->isCRCValid()==false)
        {
#ifdef LB_EN_DBG_OUT
            if((log_level&LOG_REF_SWEEP)!=0)
            {
                qInfo()<<"[LB] Searching for data coordinates...";
            }
#endif
            // Check if data coordinates are forced.
            if(forced_coords.areValid()==false)
            {
                // No forced coordinates are set.
                // Perform data coordinates search.
                if(pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
                {
                    // Brute-force sweep data coordinates until those fit.
                    findPCM1Coordinates(&temp_pcm1, in_def_coord);
                }
                else if(pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
                {
                    // Reset coordinate scan flag.
                    video_line->scan_done = false;
                    // Brute-force sweep data coordinates until those fit.
                    findPCM16X0Coordinates(&temp_pcm16x0, in_def_coord);
                }
                else if(pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                {
                    // Find coordinates for PCM START and STOP markers with current reference level.
                    findSTC007Coordinates(&temp_stc, suppress_log);
                }
            }
            else
            {
                // Data coordinates are forced, don't perform the search.
                // Apply forced coordinates.
                dummy_line->coords = forced_coords;
                dummy_line->setDataCoordinatesState(true);
            }
            if(dummy_line->hasDataCoordSet()!=false)
            {
#ifdef LB_EN_DBG_OUT
                if((log_level&(LOG_REF_SWEEP))!=0)
                {
                    qInfo()<<"[LB] Data coordinates ok, starting binarization...";
                }
#endif
                // Binarize and read PCM data, calculate CRC.
                readPCMdata(dummy_line, 0, suppress_log);
            }
        }
        // Increase hysteresis depth in case of picked bits in the line
        // to make this run less desirable for selection from the stats.
        if(pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
        {
            if((temp_pcm1.hasPickedLeft()!=false)&&(temp_pcm1.hasPickedRight()!=false))
            {
                // Picked bits on both sides of the line: probable worst case.
                dummy_line->hysteresis_depth += (HYST_DEPTH_MAX+3);
            }
            else if(temp_pcm1.hasPickedRight()!=false)
            {
                // Picked bits only in CRC: can be more easily picked than a data word.
                dummy_line->hysteresis_depth += (HYST_DEPTH_MAX+2);
            }
            else if(temp_pcm1.hasPickedLeft()!=false)
            {
                // Picked bits only in the leftmost data word: the least possibly harmful variant, still not preferred.
                dummy_line->hysteresis_depth += (HYST_DEPTH_MAX+1);
            }
        }
        else if(pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
        {
            if(temp_pcm16x0.hasPickedRight()!=false)
            {
                // Picked bits only in CRC: can be more easily picked than a data word.
                dummy_line->hysteresis_depth += (HYST_DEPTH_MAX+2);
            }
            else if(temp_pcm16x0.hasPickedLeft()!=false)
            {
                // Picked bits only in the leftmost data word: the least possibly harmful variant, still not preferred.
                dummy_line->hysteresis_depth += (HYST_DEPTH_MAX+1);
            }
        }
        // Clamp hysteresis depth.
        if(dummy_line->hysteresis_depth>0x0F)
        {
            dummy_line->hysteresis_depth = 0x0F;
        }
        // Preset "no valid CRC" result.
        read_result = REF_NO_PCM;
        // Check and store final decode result in stats data.
        if((dummy_line->isCRCValid()!=false)&&(dummy_line->coords.areValid()!=false))
        {
            read_result = REF_CRC_OK;
            crc_res[ref_index].result = read_result;
            crc_res[ref_index].data_start = dummy_line->coords.data_start;
            crc_res[ref_index].data_stop = dummy_line->coords.data_stop;
            crc_res[ref_index].hyst_dph = dummy_line->hysteresis_depth;
            crc_res[ref_index].shift_stg = dummy_line->shift_stage;
            crc_res[ref_index].crc = dummy_line->getCalculatedCRC();
        }
        else if(dummy_line->hasDataCoordSet()!=false)
        {
            read_result = REF_BAD_CRC;
            crc_res[ref_index].result = read_result;
            crc_res[ref_index].data_start = dummy_line->coords.data_start;
            crc_res[ref_index].data_stop = dummy_line->coords.data_stop;
            crc_res[ref_index].hyst_dph = dummy_line->hysteresis_depth;
            crc_res[ref_index].shift_stg = dummy_line->shift_stage;
            crc_res[ref_index].crc = dummy_line->getCalculatedCRC();
        }
#ifdef LB_EN_DBG_OUT
        //if(suppress_log==false)
        if((log_level&(LOG_REF_SWEEP))!=0)
        {
            if(read_result==REF_CRC_OK)
            {
                log_line.sprintf("[LB-%03u] Valid PCM with CRC [0x%04x] with [%u] ref. hysteresis, [%u] pixel shifting stage, [%03d:%04d] data coordinates",
                                 ref_index,
                                 dummy_line->getCalculatedCRC(),
                                 dummy_line->hysteresis_depth,
                                 dummy_line->shift_stage,
                                 dummy_line->coords.data_start,
                                 dummy_line->coords.data_stop);
            }
            else
            {
                log_line.sprintf("[LB-%03u] No valid PCM found", ref_index);
            }
            qInfo()<<log_line;
        }
#endif
        ref_index--;
    }
}

//------------------------ Calculate reference level by sweeping it from BLACK to WHITE
//------------------------ gather stats and get the most usable result.
void Binarizer::calcRefLevelBySweep(PCMLine *pcm_line)
{
    bool suppress_log;
    uint8_t fast_ref, bin_level, valid_crc_cnt, span_res/*, thread_cnt, lvls_per_thread*/;
    crc_handler_t scan_sweep_crcs[256];                 // CRC data for reference level sweep stats.
    CoordinatePair forced_coords;

    suppress_log = !((log_level&LOG_PROCESS)!=0);

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if(suppress_log==false)
    {
        qInfo()<<"[LB] ---------- Full reference level sweep starting...";
    }
#endif

    // Fast pre-calculate average reference level for fallback and logs.
    fast_ref = pickCenterRefLevel(pcm_line->black_level, pcm_line->white_level);

    if(pcm_line->getPCMType()==PCMLine::TYPE_STC007)
    {
        // Set maximum shift-stages per preset mode.
        hysteresis_depth_lim = in_max_hysteresis_depth;
        shift_stages_lim = in_max_shift_stages;
    }
    else
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = SHIFT_STAGES_SAFE;
    }

    if(digi_set.en_force_coords!=false)
    {
        // Calculate forced data coordinates.
        forced_coords.data_start = (int16_t)scan_start+digi_set.horiz_coords.data_start;
        forced_coords.data_stop = (int16_t)scan_end-digi_set.horiz_coords.data_stop;
        if(video_line->isDoubleWidth()!=false)
        {
            forced_coords.data_start += digi_set.horiz_coords.data_start;
            forced_coords.data_stop -= digi_set.horiz_coords.data_stop;
        }
    }

    // Reset resulting reference level array.
    resetCRCStats(scan_sweep_crcs, 256);

#if defined(_OPENMP)
    #pragma omp parallel
#endif
    // TODO: perform multi-threaded sweep
    // Perform reference level sweep and gather statistics in [scan_sweep_crcs].
    sweepRefLevel(pcm_line, scan_sweep_crcs);

    // Preset "good CRC region" detection to "none".
    span_res = SPAN_NOT_FOUND;

    // Reset CRC stats for reference level sweep.
    resetCRCStats(crc_stats, (MAX_COLL_CRCS+1), &valid_crc_cnt);
    crc_stats[0].hyst_dph = 0;
    crc_stats[0].shift_stg = 0;

    // Assemble list of "good" CRCs (including collisions).
    // Cycle from WHITE level to BLACK level (all checked reference levels).
    for(bin_level=(pcm_line->white_level-1);bin_level>(pcm_line->black_level);bin_level--)
    {
        // Check for good binarization result for current ref level.
        if(scan_sweep_crcs[bin_level].result==REF_CRC_OK)
        {
            // Update CRC stats list.
            updateCRCStats(crc_stats, scan_sweep_crcs[bin_level], &valid_crc_cnt);
        }
    }

    // Check if any valid CRCs were found.
    if(valid_crc_cnt>0)
    {
        // Find the most frequent one (or invalidate all of CRCs if failed).
        findMostFrequentCRC(crc_stats, &valid_crc_cnt, true, ((log_level&LOG_REF_SWEEP)==0));

#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(valid_crc_cnt==0)
            {
                qInfo()<<"[LB] No valid CRC left for ref. level sweep";
            }
        }
#endif

        // Invalidate not frequent enough CRCs.
        invalidateNonFrequentCRCs(scan_sweep_crcs, (pcm_line->black_level+1), (pcm_line->white_level-1), valid_crc_cnt, crc_stats[0].crc);

        // Re-check if there is a valid CRC.
        if(valid_crc_cnt>0)
        {
            // Check CRC stats run for number of valid CRCs in the line.
            if(crc_stats[0].result<digi_set.min_valid_crcs)
            {
#ifdef LB_EN_DBG_OUT
                if((suppress_log==false)||((log_level&LOG_REF_SWEEP)!=0))
                {
                    log_line.sprintf("[LB] Too narrow region (%u CRCs), skipping...", crc_stats[0].result);
                    qInfo()<<log_line;
                }
#endif
                // Too little valid CRCs.
                span_res = SPAN_TOO_NARROW;
            }
            else
            {
                // Find and pick reference level with smallest hyst/shift combo from "good CRC" region.
                span_res = pickLevelByCRCStats(scan_sweep_crcs, &(pcm_line->ref_level), (pcm_line->black_level+1), (pcm_line->white_level-1),
                                               REF_CRC_OK, 0x0F);
            }
        }
    }

    // Check if "good reference level" region was found.
    if(span_res==SPAN_OK)
    {
        crc_handler_t target_settings;
        target_settings = scan_sweep_crcs[pcm_line->ref_level];
        // Set flag that reference level was sweeped.
        pcm_line->setSweepedReference(true);
        // Copy data coordinates from CRC stats into the PCM line.
        pcm_line->coords.setCoordinates(target_settings.data_start, target_settings.data_stop);
        pcm_line->setDataCoordinatesState(true);
        if((pcm_line->getPCMType()==PCMLine::TYPE_STC007)&&(forced_coords.areValid()==false))
        {
            STC007Line *temp_stc;
            temp_stc = static_cast<STC007Line *>(pcm_line);
            // Set marker statuses.
            findSTC007Coordinates(temp_stc, suppress_log);
        }
        // Limit final [readPCMdata()] stages to that of the picked level.
        hysteresis_depth_lim = target_settings.hyst_dph;
        if(hysteresis_depth_lim>HYST_DEPTH_MAX)
        {
            hysteresis_depth_lim = HYST_DEPTH_MAX;
        }
        shift_stages_lim = target_settings.shift_stg;
#ifdef LB_EN_DBG_OUT
        if((suppress_log==false)||((log_level&LOG_REF_SWEEP)!=0))
        {
            textDumpCRCSweep(scan_sweep_crcs, 0, 255, pcm_line->ref_level, fast_ref);
            log_line.sprintf("[LB] Good reference: [%03u] with hyst.: [%u], shift st.: [%u], data coord.: [%03d|%04d]",
                             pcm_line->ref_level, hysteresis_depth_lim, shift_stages_lim,
                             pcm_line->coords.data_start, pcm_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
    }
    else
    {
        // No good reference level was found.
#ifdef LB_EN_DBG_OUT
        if((suppress_log==false)||((log_level&LOG_REF_SWEEP)!=0))
        {
            textDumpCRCSweep(scan_sweep_crcs, 0, 255, 0xFFFF, fast_ref);
        }
        if(suppress_log==false)
        {
            qInfo()<<"[LB] No good reference value was found during sweep.";
        }
#endif
        // Fallback to other value.
        if(span_res==SPAN_TOO_NARROW)
        {
            // There was a "good" region, but it is too narrow.
            // Pick level from "too narrow" region.
            span_res = pickLevelByCRCStatsOpt(scan_sweep_crcs, &(pcm_line->ref_level),
                                             (pcm_line->black_level+1), (pcm_line->white_level-1),
                                             (uint8_t)REF_CRC_OK, hysteresis_depth_lim, shift_stages_lim);
            // Make sure the line will not get "good CRC".
            pcm_line->setForcedBad();
        }
        else
        {

            // Make sure that binarization will give bad CRC.
            if(pcm_line->canUseMarkers()!=false)
            {
                // Pick one from "bad CRC" region (but with PCM markers).
                span_res = pickLevelByCRCStats(scan_sweep_crcs, &(pcm_line->ref_level),
                                              (pcm_line->black_level+1), (pcm_line->white_level-1),
                                              (uint8_t)REF_BAD_CRC, 0xFF, 0xFF);
            }
            else
            {
                // Pick one from "No PCM" region.
                span_res = pickLevelByCRCStats(scan_sweep_crcs, &(pcm_line->ref_level),
                                              (pcm_line->black_level+1), (pcm_line->white_level-1),
                                              (uint8_t)REF_NO_PCM, 0xFF, 0xFF);
            }
        }
        // Check if target "bad" span was found.
        if(span_res==SPAN_OK)
        {
            // Find CRC data for picked reference level.
            crc_handler_t target_settings;
            target_settings = scan_sweep_crcs[pcm_line->ref_level];
            // Copy data coordinates from CRC stats into the PCM line.
            pcm_line->coords.setCoordinates(target_settings.data_start, target_settings.data_stop);
            pcm_line->setDataCoordinatesState(true);
            if((pcm_line->getPCMType()==PCMLine::TYPE_STC007)&&(forced_coords.areValid()==false))
            {
                STC007Line *temp_stc;
                temp_stc = static_cast<STC007Line *>(pcm_line);
                // Set marker statuses.
                findSTC007Coordinates(temp_stc, suppress_log);
            }
            /*for(uint16_t index=0;index<256;index++)
            {
                if(index==pcm_line->ref_level)
                {
                    if(pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                    {
                        STC007Line *temp_stc;
                        temp_stc = static_cast<STC007Line *>(pcm_line);
                        // Set marker statuses.
                        findSTC007Markers(temp_stc, suppress_log);
                    }
                    // Copy data coordinates from CRC stats into the PCM line.
                    pcm_line->coords.data_start = scan_sweep_crcs[index].data_start;
                    pcm_line->coords.data_stop = scan_sweep_crcs[index].data_stop;
                    pcm_line->setDataCoordinatesState(true);
                    // Limit final read stages to that of the picked level.
                    hysteresis_depth_lim = scan_sweep_crcs[index].hyst_dph;
                    shift_stages_lim = scan_sweep_crcs[index].shift_stg;
                    // Search is done.
                    break;
                }
            }*/
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Fallback to level with at least something:"<<pcm_line->ref_level;
            }
#endif
        }
        // Check for provided working reference level.
        else if(isRefLevelPreset()!=false)
        {
            // Copy provided value from good line.
            pcm_line->ref_level = in_def_reference;
            if(in_def_coord.areValid()!=false)
            {
                // Set data coordinates as preset from the input, if available.
                pcm_line->coords = in_def_coord;
            }
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Fallback to external reference:"<<pcm_line->ref_level;
            }
#endif
        }
        else
        {
            // Set reference as calculated estimate.
            pcm_line->ref_level = fast_ref;
            if(in_def_coord.areValid()==false)
            {
                if(pcm_line->getPCMType()==PCMLine::TYPE_STC007)
                {
                    pcm_line->coords.setCoordinates((scan_start+estimated_ppb), (scan_end-(4*estimated_ppb)));
                }
                else
                {
                    pcm_line->coords.setCoordinates(scan_start, scan_end);
                }
            }
            else
            {
                // Set data coordinates as preset from the input, if available.
                pcm_line->coords = in_def_coord;
            }
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Fallback to calculated reference:"<<pcm_line->ref_level;
            }
#endif
        }
        // Limit furter processing to save on time.
        hysteresis_depth_lim = HYST_DEPTH_MIN;
        shift_stages_lim = SHIFT_STAGES_MIN;
#ifdef LB_EN_DBG_OUT
        if((suppress_log==false)||((log_level&LOG_REF_SWEEP)!=0))
        {
            textDumpCRCSweep(scan_sweep_crcs, 0, pcm_line->white_level, 0xFFFF, pcm_line->ref_level);
            log_line.sprintf("[LB] Invalid reference: [%03u] with hyst.: [%u], shift st.: [%u], data coord.: [%03d|%04d]",
                         pcm_line->ref_level, hysteresis_depth_lim, shift_stages_lim,
                         pcm_line->coords.data_start, pcm_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
    }
}

//------------------------ Try to find data coordinates for PCM-1 data in the line.
uint8_t Binarizer::searchPCM1Data(PCM1Line *pcm_line, CoordinatePair data_loc)
{
    bool suppress_log;
    uint8_t right_ofs, left_ofs;
    uint8_t stat_left_idx, stat_right_idx, valid_left_crcs, valid_right_crcs;
    //uint8_t step_min, step_max;
    uint16_t scan_step, step_span;
    CoordinatePair left_coord, right_coord;
    crc_handler_t scan_left_res[PCM1_SEARCH_STEP_CNT];
    crc_handler_t scan_right_res[PCM1_SEARCH_STEP_CNT];
    crc_handler_t scan_left_crcs[MAX_COLL_CRCS];
    crc_handler_t scan_right_crcs[MAX_COLL_CRCS];

    right_ofs = left_ofs = 0xFF;
#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = ((log_level&LOG_COORD)==0);
    //suppress_log = false;

    stat_left_idx = 2;
    while(stat_left_idx>0)
    {
        // Calculate PPB for the line with provided rough data coordinates.
        pcm_line->calcPPB(data_loc);

        // Calculate pixel coordinate shift step.
        scan_step = pcm_line->getPPB();
        if(scan_step>=PCM1_SEARCH_STEP_DIV)
        {
            // Set step as quarter of PPB if PPB is big enough.
            scan_step = scan_step/PCM1_SEARCH_STEP_DIV;
        }
        else
        {
            // Set minimum step = one pixel.
            scan_step = 1;
        }
        // Calculate maximum delta from provided coordinates.
        step_span = scan_step*PCM1_SEARCH_MAX_OFS;
        // Calculate limits for data search.
        left_coord.data_start = data_loc.data_start-step_span;
        left_coord.data_stop = data_loc.data_start+step_span;
        right_coord.data_start = data_loc.data_stop-step_span;
        right_coord.data_stop = data_loc.data_stop+step_span;
        // Make sure that default line coordinates stay in range.
        // This prevents data coordinates runaway when false positive leads coordinates
        // far from starting point (source line limits).
        if(((left_coord.data_start<scan_start)&&(left_coord.data_stop<scan_start))
           ||((left_coord.data_start>scan_start)&&(left_coord.data_stop>scan_start))
           ||((right_coord.data_start<scan_end)&&(right_coord.data_stop<scan_end))
           ||((right_coord.data_start>scan_end)&&(right_coord.data_stop>scan_end)))
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Line [%03u][%03u] starting coordinates [%03d...%03d|%04d...%04d] exclude default ones, reset to [%03d|%04d]",
                                 video_line->frame_number, video_line->line_number,
                                 left_coord.data_start, left_coord.data_stop, right_coord.data_start, right_coord.data_stop,
                                 scan_start, scan_end);
                qInfo()<<log_line;
            }
#endif
            // Reset data coordinates to default range.
            data_loc.data_start = scan_start;
            data_loc.data_stop = scan_end;
        }
        else
        {
            // Provided coordinates are fine.
            break;
        }
        // Fail-safe anti-lock countdown.
        stat_left_idx--;
    }

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Line [%03u-%03u] left/right start coordinates: [%03d/%04d], PPB: [%02u]",
                         video_line->frame_number, video_line->line_number,
                         data_loc.data_start, data_loc.data_stop, pcm_line->getPPB());
        qInfo()<<log_line;
        log_line.sprintf("[LB] Left search span [%03d|%03d], right search span [%04d|%04d], [%u] steps by [%02u] pixels",
                         left_coord.data_start,
                         left_coord.data_stop,
                         right_coord.data_start,
                         right_coord.data_stop,
                         PCM1_SEARCH_STEP_CNT,
                         scan_step);
        qInfo()<<log_line;
    }
#endif

    // Force calculation of number of cut bits even on valid CRCs.
    force_bit_picker = true;
    // Override pixel shift and hysteresis depth according to binarization mode.
    // Tests indicate that minimal wiggle room for the search gives more valid CRCs (and faster).
    if(bin_mode==MODE_DRAFT)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = 0;
    }
    else if(bin_mode==MODE_FAST)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = 0;
    }
    else if(bin_mode==MODE_NORMAL)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = SHIFT_STAGES_SAFE;
    }
    else if(bin_mode==MODE_INSANE)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = SHIFT_STAGES_SAFE;
    }

    // Reset pixel offset stats.
    resetCRCStats(scan_left_res, PCM1_SEARCH_STEP_CNT);
    resetCRCStats(scan_left_crcs, MAX_COLL_CRCS, &valid_left_crcs);
    stat_left_idx = 0;
    // Cycle through start offset.
    for(int16_t start_ofs=left_coord.data_start;start_ofs<=left_coord.data_stop;start_ofs+=scan_step)
    {
        // Reset stats for right coordinate scan.
        resetCRCStats(scan_right_res, PCM1_SEARCH_STEP_CNT);
        resetCRCStats(scan_right_crcs, MAX_COLL_CRCS, &valid_right_crcs);
        stat_right_idx = 0;
        // Cycle through stop offset.
        for(int16_t stop_ofs=right_coord.data_stop;stop_ofs>=right_coord.data_start;stop_ofs-=scan_step)
        {
#ifdef LB_EN_DBG_OUT
            /*if(suppress_log==false)
            {
                log_line.sprintf("[LB] Pass for coordinates [%03d|%03d]...", start_ofs, stop_ofs);
                qInfo()<<log_line;
            }*/
            //qInfo()<<stat_left_idx<<start_ofs<<stat_right_idx<<stop_ofs<<scan_left_res[28].result;
#endif
            // Set test data coordinates.
            pcm_line->coords.setCoordinates(start_ofs, stop_ofs);
            // Perform binarization.
            readPCMdata(pcm_line, 0, true);
            // Save stats for right coordinate.
            scan_right_res[stat_right_idx].crc = pcm_line->getSourceCRC();          // Read CRC from the line
            scan_right_res[stat_right_idx].hyst_dph = pcm_line->hysteresis_depth;   // On which hysteresis depth it was read
            scan_right_res[stat_right_idx].shift_stg = pcm_line->shift_stage;       // With which pixel-shift stage it was read
            scan_right_res[stat_right_idx].data_start = start_ofs;                  // Current left data coordinate
            scan_right_res[stat_right_idx].data_stop = stop_ofs;                    // Current right date coordinate
            scan_right_res[stat_right_idx].result = REF_BAD_CRC;                    // Preset result as bad.
            // Up hysteresis depth if bit picker was active to make this entry less desirable in stats selection.
            if((pcm_line->hasPickedLeft()!=false)&&(pcm_line->hasPickedRight()!=false))
            {
                // Picked bits on both sides of the line: probable worst case.
                scan_right_res[stat_right_idx].hyst_dph = 0x0E;
            }
            else if(pcm_line->hasPickedRight()!=false)
            {
                // Picked bits only in CRC: can be easily picked than data word.
                scan_right_res[stat_right_idx].hyst_dph = 0x0D;
            }
            else if(pcm_line->hasPickedLeft()!=false)
            {
                // Picked bits only in the leftmost data word: the least possibly harmful variant, still not preferred.
                scan_right_res[stat_right_idx].hyst_dph = 0x0C;
            }

            // Check CRC result.
            if(pcm_line->isCRCValid()!=false)
            {
                // CRC was actually good.
                scan_right_res[stat_right_idx].result = REF_CRC_OK;
                // Gather valid CRC stats.
                updateCRCStats(scan_right_crcs, scan_right_res[stat_right_idx], &valid_right_crcs);
#ifdef LB_EN_DBG_OUT
                /*if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Good CRC from coord. [%03d|%03d]", start_ofs, stop_ofs);
                    qInfo()<<log_line;
                }*/
#endif
            }
#ifdef LB_EN_DBG_OUT
            /*else
            {
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Bad CRC from coord. [%03d|%03d]", start_ofs, stop_ofs);
                    qInfo()<<log_line;
                }
            }*/
#endif
            //qInfo()<<QString::fromStdString(pcm_line->dumpWordsString());
            stat_right_idx++;
            if(stat_right_idx>=PCM1_SEARCH_STEP_CNT)
            {
                qWarning()<<DBG_ANCHOR<<"[LB] Logic error! r_idx ="<<stat_right_idx<<"r_ofs ="<<stop_ofs;
                break;
            }
        }

        // Check if any valid CRCs for found during right coordinate scan.
        if(valid_right_crcs>0)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                if(valid_right_crcs==1)
                {
                    log_line.sprintf("[LB] Found 1 valid CRC (0x%04x) during right coordinate scan with left coordinate at %03d (scan index [%03u])",
                                     scan_right_crcs[1].crc, start_ofs, stat_left_idx);
                }
                else
                {
                    log_line.sprintf("[LB] Found %u valid CRCs during right coordinate scan with left coordinate at %03d (scan index [%03u])",
                                     valid_right_crcs, start_ofs, stat_left_idx);
                }
                qInfo()<<log_line;
            }
#endif
            // Get optimal offset for right coordinate.
            findMostFrequentCRC(scan_right_crcs, &valid_right_crcs, true, suppress_log);

#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                if(valid_right_crcs==0)
                {
                    qInfo()<<"[LB] No valid CRCs after filtering remained...";
                }
            }
#endif
            // Invalidate not frequent enough CRCs.
            invalidateNonFrequentCRCs(scan_right_res, 0, (PCM1_SEARCH_STEP_CNT-1), valid_right_crcs, scan_right_crcs[0].crc, true);
            // Re-check if valid CRCs remained after CRC filtering.
            if(valid_right_crcs>0)
            {
                // There is a valid CRC in the list.
                if(pickLevelByCRCStats(scan_right_res, &right_ofs, 0, (PCM1_SEARCH_STEP_CNT-1), REF_CRC_OK, 0x0F)!=SPAN_OK)
                {
                    valid_right_crcs = 0;
                }
            }
        }
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(valid_right_crcs>0)
            {
                qInfo()<<"[LB] Right coordinate sweep with left coordinate at"<<start_ofs;
                textDumpCRCSweep(scan_right_res, 0, (PCM1_SEARCH_STEP_CNT-1), right_ofs);
                qInfo()<<"[LB] Picked right coordinate:"<<scan_right_res[right_ofs].data_stop;
            }
            else
            {
                log_line.sprintf("[LB] No valid right coordinates after sweep with left coordinate at %03d (scan index [%03u])",
                                 start_ofs, stat_left_idx);
                qInfo()<<log_line;
            }
        }
#endif
        // Check if any valid CRCs remaining after right coordinate scan.
        if(valid_right_crcs>0)
        {
            // Save good CRC data for left coordinate.
            scan_left_res[stat_left_idx].result = REF_CRC_OK;
            scan_left_res[stat_left_idx].crc = scan_right_res[right_ofs].crc;
            scan_left_res[stat_left_idx].hyst_dph = scan_right_res[right_ofs].hyst_dph;
            scan_left_res[stat_left_idx].shift_stg = scan_right_res[right_ofs].shift_stg;
            scan_left_res[stat_left_idx].data_start = scan_right_res[right_ofs].data_start;
            scan_left_res[stat_left_idx].data_stop = scan_right_res[right_ofs].data_stop;
            for(uint8_t index=0;index<scan_right_crcs[0].result;index++)
            {
                // Add same CRC as many times as it was encountered during right coordinate scan.
                updateCRCStats(scan_left_crcs, scan_right_crcs[0], &valid_left_crcs);
            }
        }
        else
        {
            // No valid CRCs after full right coordinate sweep.
            scan_left_res[stat_left_idx].result = REF_BAD_CRC;
            scan_left_res[stat_left_idx].crc = 0;
            scan_left_res[stat_left_idx].hyst_dph = HYST_DEPTH_MAX;         // Set value to maximum to make this entry bad in CRC stats
            scan_left_res[stat_left_idx].shift_stg = SHIFT_STAGES_MAX;      // Set value to maximum to make this entry bad in CRC stats
        }
        stat_left_idx++;
        if(stat_left_idx>=PCM1_SEARCH_STEP_CNT)
        {
            qWarning()<<DBG_ANCHOR<<"[LB] Logic error! l_idx ="<<stat_left_idx<<"l_ofs ="<<start_ofs;
            break;
        }
    }
    // Disable forced Bit Picker for normal operation.
    force_bit_picker = false;

    // Check if any valid CRCs for found during left coordinate scan.
    if(valid_left_crcs>0)
    {
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(valid_left_crcs==1)
            {
                log_line.sprintf("[LB] Found 1 valid CRC (0x%04x) during left coordinate scan", scan_left_crcs[1].crc);
            }
            else
            {
                log_line.sprintf("[LB] Found %u valid CRCs during left coordinate scan", valid_left_crcs);
            }
            qInfo()<<log_line;
        }
#endif
        // Get optimal offset for left coordinate.
        findMostFrequentCRC(scan_left_crcs, &valid_left_crcs, true, suppress_log);

#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(valid_left_crcs==0)
            {
                qInfo()<<"[LB] No valid CRCs after filtering remained...";
            }
        }
#endif
        // Invalidate not frequent enough CRCs.
        invalidateNonFrequentCRCs(scan_left_res, 0, (PCM1_SEARCH_STEP_CNT-1), valid_left_crcs, scan_left_crcs[0].crc, true);
        // Re-check if valid CRCs remained after CRC filtering.
        if(valid_left_crcs>0)
        {
            // Pick optimal offset if anything was found.
            if(pickLevelByCRCStats(scan_left_res, &left_ofs, 0, (PCM1_SEARCH_STEP_CNT-1), REF_CRC_OK, 0x0F)!=SPAN_OK)
            {
                valid_left_crcs = 0;
            }
        }
    }

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[LB] Frame"<<pcm_line->frame_number<<"line"<<pcm_line->line_number<<"left coordinate sweep:";
        textDumpCRCSweep(scan_left_res, 0, (PCM1_SEARCH_STEP_CNT-1), left_ofs);
        if(valid_left_crcs>0)
        {
            qInfo()<<"[LB] Picked left coordinate:"<<scan_left_res[left_ofs].data_start<<"picked right coordinate:"<<scan_left_res[left_ofs].data_stop;
        }
    }
#endif

    // Check if any valid CRCs remaining after left coordinate scan.
    if(valid_left_crcs>0)
    {
        // Found some coordinates.
        // Set new coordinates.
        pcm_line->coords.data_start = scan_left_res[left_ofs].data_start;
        pcm_line->coords.data_stop = scan_left_res[left_ofs].data_stop;
        pcm_line->setDataCoordinatesState(true);
        pcm_line->setSweepedCoordinates(true);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Final data coordinates: [%03d:%04d]",
                             pcm_line->coords.data_start, pcm_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
        return LB_RET_OK;
    }
    else
    {
        // Nothing useful was found.
        // Set default coordinates.
        pcm_line->coords = data_loc;
        pcm_line->setSweepedCoordinates(false);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] No data coordinates found, revert to preset [%03d:%04d]",
                             pcm_line->coords.data_start, pcm_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
        return LB_RET_NO_COORD;
    }
}

//------------------------ Try to find data coordinates for PCM-16x0 data in the line.
uint8_t Binarizer::searchPCM16X0Data(PCM16X0SubLine *pcm_line, CoordinatePair data_loc)
{
    bool suppress_log, lock_right, lock_left, lock_min, lock_max;
    uint8_t right_ofs, left_ofs;
    uint8_t step_min, step_max;
    uint8_t stat_left_idx, stat_right_idx;
    uint8_t valid_crcs, valid_left_crcs, valid_right_crcs, valid_right_p0_crcs, valid_right_p1_crcs, valid_right_p2_crcs;
    uint16_t scan_step, step_span;
    CoordinatePair left_coord, right_coord;
    crc_handler_t scan_left_res[PCM16X0_SEARCH_STEP_CNT];
    crc_handler_t scan_left_crcs[MAX_COLL_CRCS];
    crc_handler_t scan_right_res[PCM16X0_SEARCH_STEP_CNT];
    crc_handler_t scan_right_p0_res[PCM16X0_SEARCH_STEP_CNT];
    crc_handler_t scan_right_p1_res[PCM16X0_SEARCH_STEP_CNT];
    crc_handler_t scan_right_p2_res[PCM16X0_SEARCH_STEP_CNT];
    crc_handler_t scan_right_crcs[MAX_COLL_CRCS];
    crc_handler_t scan_right_p0_crcs[MAX_COLL_CRCS];
    crc_handler_t scan_right_p1_crcs[MAX_COLL_CRCS];
    crc_handler_t scan_right_p2_crcs[MAX_COLL_CRCS];

    valid_crcs = 0;
    lock_right = lock_left = false;
    right_ofs = left_ofs = 0xFF;
#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = ((log_level&LOG_COORD)==0);
    //suppress_log = false;

    step_max = 2;
    while(step_max>0)
    {
        // Calculate PPB for the line with provided rough data coordinates.
        pcm_line->calcPPB(data_loc);

        // Calculate pixel coordinate shift step.
        scan_step = pcm_line->getPPB();
        if(scan_step>=PCM16X0_SEARCH_STEP_DIV)
        {
            // Set step as quarter of PPB if PPB is big enough.
            scan_step = scan_step/PCM16X0_SEARCH_STEP_DIV;
        }
        else
        {
            // Set minimum step = one pixel of the source.
            scan_step = 1;
        }
        // Calculate maximum delta from provided coordinates.
        step_span = scan_step*PCM16X0_SEARCH_MAX_OFS;
        // Calculate limits for data search.
        left_coord.data_start = data_loc.data_start-step_span;
        left_coord.data_stop = data_loc.data_start+step_span;
        right_coord.data_start = data_loc.data_stop-step_span;
        right_coord.data_stop = data_loc.data_stop+step_span;

        // Make sure that default line coordinates stay in range.
        // This prevents data coordinates runaway when false positive leads coordinates
        // far from starting point (source line limits).
        if(((left_coord.data_start<scan_start)&&(left_coord.data_stop<scan_start))
           ||((left_coord.data_start>scan_start)&&(left_coord.data_stop>scan_start))
           ||((right_coord.data_start<scan_end)&&(right_coord.data_stop<scan_end))
           ||((right_coord.data_start>scan_end)&&(right_coord.data_stop>scan_end)))
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Line [%03u][%03u] starting coordinates [%03d...%03d|%04d...%04d] exclude default ones, reset to [%03d|%04d]",
                                 video_line->frame_number, video_line->line_number,
                                 left_coord.data_start, left_coord.data_stop, right_coord.data_start, right_coord.data_stop,
                                 scan_start, scan_end);
                qInfo()<<log_line;
            }
#endif
            // Reset data coordinates to default range.
            data_loc.data_start = scan_start;
            data_loc.data_stop = scan_end;
        }
        else
        {
            // Provided coordinates are fine.
            break;
        }
        // Fail-safe anti-lock countdown.
        step_max--;
    }

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[LB] Line [%03u][%03u] left/right starting coordinates: [%03d/%04d], PPB: [%02u]",
                         video_line->frame_number, video_line->line_number,
                         data_loc.data_start, data_loc.data_stop, pcm_line->getPPB());
        qInfo()<<log_line;
        log_line.sprintf("[LB] Left search span [%03d|%03d], right search span [%04d|%04d], [%u] steps by [%02u] pixels",
                         left_coord.data_start,
                         left_coord.data_stop,
                         right_coord.data_start,
                         right_coord.data_stop,
                         PCM16X0_SEARCH_STEP_CNT,
                         scan_step);
        qInfo()<<log_line;
    }
#endif

    // Force calculation of number of cut bits even on valid CRCs.
    force_bit_picker = true;
    // Override pixel shift and hysteresis depth according to binarization mode.
    // Tests indicate that minimal wiggle room for the search gives more valid CRCs (and faster).
    if(bin_mode==MODE_DRAFT)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = 0;
    }
    else if(bin_mode==MODE_FAST)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = 0;
    }
    else if(bin_mode==MODE_NORMAL)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = SHIFT_STAGES_SAFE;
    }
    else if(bin_mode==MODE_INSANE)
    {
        hysteresis_depth_lim = 0;
        shift_stages_lim = SHIFT_STAGES_SAFE;
    }

    // Reset pixel offset stats.
    resetCRCStats(scan_left_res, PCM16X0_SEARCH_STEP_CNT);
    resetCRCStats(scan_left_crcs, MAX_COLL_CRCS, &valid_left_crcs);
    stat_left_idx = 0;
    // Cycle through start offset.
    for(int16_t start_ofs=left_coord.data_start;start_ofs<=left_coord.data_stop;start_ofs+=scan_step)
    {
        // Reset stats for right coordinate scan.
        resetCRCStats(scan_right_res, PCM16X0_SEARCH_STEP_CNT);
        resetCRCStats(scan_right_p0_res, PCM16X0_SEARCH_STEP_CNT);
        resetCRCStats(scan_right_p1_res, PCM16X0_SEARCH_STEP_CNT);
        resetCRCStats(scan_right_p2_res, PCM16X0_SEARCH_STEP_CNT);
        resetCRCStats(scan_right_crcs, MAX_COLL_CRCS, &valid_right_crcs);
        resetCRCStats(scan_right_p0_crcs, MAX_COLL_CRCS, &valid_right_p0_crcs);
        resetCRCStats(scan_right_p1_crcs, MAX_COLL_CRCS, &valid_right_p1_crcs);
        resetCRCStats(scan_right_p2_crcs, MAX_COLL_CRCS, &valid_right_p2_crcs);
        stat_right_idx = 0;
        lock_right = false;
        lock_min = lock_max = false;
        step_min = 0;
        step_max = PCM16X0_SEARCH_STEP_CNT;
        // Cycle through stop offset.
        for(int16_t stop_ofs=right_coord.data_stop;stop_ofs>=right_coord.data_start;stop_ofs-=scan_step)
        {
#ifdef LB_EN_DBG_OUT
            /*if(suppress_log==false)
            {
                log_line.sprintf("[LB] Pass for coordinates [%03d|%03d]...", start_ofs, stop_ofs);
                qInfo()<<log_line;
            }*/
            //qInfo()<<stat_left_idx<<start_ofs<<stat_right_idx<<stop_ofs<<scan_left_res[28].result;
#endif
            // Set test data coordinates.
            pcm_line->coords.setCoordinates(start_ofs, stop_ofs);
            // Set line part mode for the left part of the line.
            line_part_mode = PART_PCM16X0_LEFT;
            // Perform binarization.
            readPCMdata(pcm_line, 0, true);
            // Save stats for right coordinate.
            scan_right_p0_res[stat_right_idx].crc = pcm_line->getSourceCRC();           // Read CRC from the line
            scan_right_p0_res[stat_right_idx].hyst_dph = pcm_line->hysteresis_depth;    // On which hysteresis depth it was read
            scan_right_p0_res[stat_right_idx].shift_stg = pcm_line->shift_stage;        // With which pixel-shift stage it was read
            scan_right_p0_res[stat_right_idx].data_start = start_ofs;                   // Current left data coordinate
            scan_right_p0_res[stat_right_idx].data_stop = stop_ofs;                     // Current right date coordinate
            scan_right_p0_res[stat_right_idx].result = REF_BAD_CRC;                     // Preset result as bad.
            // Up hysteresis depth if bit picker was active to make this entry less desirable in stats selection.
            if(pcm_line->hasPickedLeft()!=false)
            {
                // Picked bits only in the leftmost data word.
                //scan_right_p0_res[stat_right_idx].hyst_dph += 0x06;
                scan_right_p0_res[stat_right_idx].hyst_dph += 0x02;
                if(scan_right_p0_res[stat_right_idx].hyst_dph>0x0F)
                {
                    scan_right_p0_res[stat_right_idx].hyst_dph = 0x0F;
                }
            }
            // Check CRC result.
            if(pcm_line->isCRCValid()!=false)
            {
                // CRC was actually good.
                scan_right_p0_res[stat_right_idx].result = REF_CRC_OK;
                // Gather valid CRC stats.
                updateCRCStats(scan_right_p0_crcs, scan_right_p0_res[stat_right_idx], &valid_right_p0_crcs);
                // Update search limits for valid CRCs.
                if(lock_min==false)
                {
                    step_min = stat_right_idx;
                    lock_min = true;
                }
                step_max = stat_right_idx;
            }
            // Set line part mode for the center of the line.
            line_part_mode = PART_PCM16X0_MIDDLE;
            // Perform binarization.
            readPCMdata(pcm_line, 0, true);
            // Save stats for right coordinate.
            scan_right_p1_res[stat_right_idx].crc = pcm_line->getSourceCRC();           // Read CRC from the line
            scan_right_p1_res[stat_right_idx].hyst_dph = pcm_line->hysteresis_depth;    // On which hysteresis depth it was read
            scan_right_p1_res[stat_right_idx].shift_stg = pcm_line->shift_stage;        // With which pixel-shift stage it was read
            scan_right_p1_res[stat_right_idx].data_start = start_ofs;                   // Current left data coordinate
            scan_right_p1_res[stat_right_idx].data_stop = stop_ofs;                     // Current right date coordinate
            scan_right_p1_res[stat_right_idx].result = REF_BAD_CRC;                     // Preset result as bad.
            // Check CRC result.
            if(pcm_line->isCRCValid()!=false)
            {
                // CRC was actually good.
                scan_right_p1_res[stat_right_idx].result = REF_CRC_OK;
                // Gather valid CRC stats.
                updateCRCStats(scan_right_p1_crcs, scan_right_p1_res[stat_right_idx], &valid_right_p1_crcs);
                // Update search limits for valid CRCs.
                if(lock_min==false)
                {
                    step_min = stat_right_idx;
                    lock_min = true;
                }
                step_max = stat_right_idx;
            }
            // Set line part mode for the right part of the line.
            line_part_mode = PART_PCM16X0_RIGHT;
            // Perform binarization.
            readPCMdata(pcm_line, 0, true);
            // Save stats for right coordinate.
            scan_right_p2_res[stat_right_idx].crc = pcm_line->getSourceCRC();           // Read CRC from the line
            scan_right_p2_res[stat_right_idx].hyst_dph = pcm_line->hysteresis_depth;    // On which hysteresis depth it was read
            scan_right_p2_res[stat_right_idx].shift_stg = pcm_line->shift_stage;        // With which pixel-shift stage it was read
            scan_right_p2_res[stat_right_idx].data_start = start_ofs;                   // Current left data coordinate
            scan_right_p2_res[stat_right_idx].data_stop = stop_ofs;                     // Current right date coordinate
            scan_right_p2_res[stat_right_idx].result = REF_BAD_CRC;                     // Preset result as bad.
            // Up hysteresis depth if bit picker was active to make this entry less desirable in stats selection.
            if(pcm_line->hasPickedRight()!=false)
            {
                // Picked bits only in CRC: can be easily picked than data word.
                //scan_right_p2_res[stat_right_idx].hyst_dph += 0x09;
                scan_right_p2_res[stat_right_idx].hyst_dph += 0x03;
                if(scan_right_p2_res[stat_right_idx].hyst_dph>0x0F)
                {
                    scan_right_p2_res[stat_right_idx].hyst_dph = 0x0F;
                }
            }
            // Check CRC result.
            if(pcm_line->isCRCValid()!=false)
            {
                // CRC was actually good.
                scan_right_p2_res[stat_right_idx].result = REF_CRC_OK;
                // Gather valid CRC stats.
                updateCRCStats(scan_right_p2_crcs, scan_right_p2_res[stat_right_idx], &valid_right_p2_crcs);
                // Update search limits for valid CRCs.
                if(lock_min==false)
                {
                    step_min = stat_right_idx;
                    lock_min = true;
                }
                step_max = stat_right_idx;
            }

            // Check if valid CRCs were detected earlier and now nothing left.
            if((lock_right!=false)
               &&(scan_right_p0_res[stat_right_idx].result!=REF_CRC_OK)
               &&(scan_right_p1_res[stat_right_idx].result!=REF_CRC_OK)
               &&(scan_right_p2_res[stat_right_idx].result!=REF_CRC_OK))
            {
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] At this step full line is INVALID and right coordinate lock is active, stopping search";
                }
#endif
                // Don't waste resources.
                break;
            }
            // Check if any valid CRC is found on this run.
            if((lock_right==false)
               &&(scan_right_p0_res[stat_right_idx].result==REF_CRC_OK)
               &&(scan_right_p1_res[stat_right_idx].result==REF_CRC_OK)
               &&(scan_right_p2_res[stat_right_idx].result==REF_CRC_OK))
            {
#ifdef LB_EN_DBG_OUT
                if((suppress_log==false)&&(lock_right==false))
                {
                    qInfo()<<"[LB] Full line has valid CRCs, right coordinate lock is enabled";
                }
#endif
                // Lock presence of all valid CRCs on right coordinate sweep.
                lock_right = true;
            }

            //qInfo()<<QString::fromStdString(pcm_line->dumpWordsString());
            stat_right_idx++;
            if(stat_right_idx>=PCM16X0_SEARCH_STEP_CNT)
            {
                qWarning()<<DBG_ANCHOR<<"[LB] Logic error! r_idx ="<<stat_right_idx<<"r_ofs ="<<stop_ofs;
                break;
            }
        }
        // Check if any valid CRCs for left part were found during right coordinate scan.
        if(valid_right_p0_crcs>0)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Found %u valid CRCs during (left part) right coordinate scan with left coordinate at %03d (scan index [%03u])",
                                 valid_right_p0_crcs, start_ofs, stat_left_idx);
                qInfo()<<log_line;
            }
#endif
            // Get optimal offset for right coordinate.
            findMostFrequentCRC(scan_right_p0_crcs, &valid_right_p0_crcs, true, suppress_log);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                if(valid_right_p0_crcs==0)
                {
                    qInfo()<<"[LB] No valid CRCs after filtering remained...";
                }
            }
#endif
            // Invalidate not frequent enough CRCs.
            invalidateNonFrequentCRCs(scan_right_p0_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), valid_right_p0_crcs, scan_right_p0_crcs[0].crc, true);
        }
        // Check if any valid CRCs for middle part were found during right coordinate scan.
        if(valid_right_p1_crcs>0)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Found %u valid CRCs during (middle part) right coordinate scan with left coordinate at %03d (scan index [%03u])",
                                 valid_right_p1_crcs, start_ofs, stat_left_idx);
                qInfo()<<log_line;
            }
#endif
            // Get optimal offset for right coordinate.
            findMostFrequentCRC(scan_right_p1_crcs, &valid_right_p1_crcs, true, suppress_log);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                if(valid_right_p1_crcs==0)
                {
                    qInfo()<<"[LB] No valid CRCs after filtering remained...";
                }
            }
#endif
            // Invalidate not frequent enough CRCs.
            invalidateNonFrequentCRCs(scan_right_p1_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), valid_right_p1_crcs, scan_right_p1_crcs[0].crc, true);
        }
        // Check if any valid CRCs for right part were found during right coordinate scan.
        if(valid_right_p2_crcs>0)
        {
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[LB] Found %u valid CRCs during (right part) right coordinate scan with left coordinate at %03d (scan index [%03u])",
                                 valid_right_p2_crcs, start_ofs, stat_left_idx);
                qInfo()<<log_line;
            }
#endif
            // Get optimal offset for right coordinate.
            findMostFrequentCRC(scan_right_p2_crcs, &valid_right_p2_crcs, true, suppress_log);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                if(valid_right_p2_crcs==0)
                {
                    qInfo()<<"[LB] No valid CRCs after filtering remained...";
                }
            }
#endif
            // Invalidate not frequent enough CRCs.
            invalidateNonFrequentCRCs(scan_right_p2_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), valid_right_p2_crcs, scan_right_p2_crcs[0].crc, true);
        }
        if(step_max>=PCM16X0_SEARCH_STEP_CNT)
        {
            step_max = (PCM16X0_SEARCH_STEP_CNT-1);
        }
        // Combine three sweeps into one, prioritizing middle part.
        for(stat_right_idx=step_min;stat_right_idx<=step_max;stat_right_idx++)
        {
            valid_crcs = 0;
            // Check middle part for valid CRC.
            if(scan_right_p1_res[stat_right_idx].result==REF_CRC_OK)
            {
                // Middle part of the line on the run has valid CRC.
                valid_crcs++;
                // Set valid CRC flag.
                scan_right_res[stat_right_idx].result = REF_CRC_OK;
                // Set dummy CRC to pass through left coordinate stats.
                scan_right_res[stat_right_idx].crc = PCM16X0SubLine::CRC_SILENT;
                // Copy fields from middle part data.
                scan_right_res[stat_right_idx].hyst_dph = scan_right_p1_res[stat_right_idx].hyst_dph;
                scan_right_res[stat_right_idx].shift_stg = scan_right_p1_res[stat_right_idx].shift_stg;
                scan_right_res[stat_right_idx].data_start = scan_right_p1_res[stat_right_idx].data_start;
                scan_right_res[stat_right_idx].data_stop = scan_right_p1_res[stat_right_idx].data_stop;
                // Check if right part also has valid CRC.
                if(scan_right_p2_res[stat_right_idx].result==REF_CRC_OK)
                {
                    // Right part CRC is valid.
                    valid_crcs++;
                    // Summ depths.
                    scan_right_res[stat_right_idx].hyst_dph += scan_right_p2_res[stat_right_idx].hyst_dph;
                    // Check if right part has higher shifting stage.
                    if(scan_right_p2_res[stat_right_idx].shift_stg>scan_right_res[stat_right_idx].shift_stg)
                    {
                        // Update shifting stage to one from the right part.
                        scan_right_res[stat_right_idx].shift_stg = scan_right_p2_res[stat_right_idx].shift_stg;
                    }
                }
                else
                {
                    // Right part CRC is INVALID.
                    // Increase hysteresis depth to avoid this run later in CRC processing (make it less desirable).
                    scan_right_res[stat_right_idx].hyst_dph += HYST_DEPTH_SAFE;
                    //scan_right_res[stat_right_idx].shift_stg += 2;
                }
                // Check if left part also has valid CRC.
                if(scan_right_p0_res[stat_right_idx].result==REF_CRC_OK)
                {
                    // Left part CRC is valid.
                    valid_crcs++;
                    // Summ depths.
                    scan_right_res[stat_right_idx].hyst_dph += scan_right_p0_res[stat_right_idx].hyst_dph;
                    // Check if left part has higher shifting stage.
                    if(scan_right_p0_res[stat_right_idx].shift_stg>scan_right_res[stat_right_idx].shift_stg)
                    {
                        // Update shifting stage to one from the left part.
                        scan_right_res[stat_right_idx].shift_stg = scan_right_p0_res[stat_right_idx].shift_stg;
                    }
                }
                else
                {
                    // Left part CRC is INVALID.
                    // Increase hysteresis depth to avoid this run later in CRC processing (make it less desirable).
                    scan_right_res[stat_right_idx].hyst_dph += HYST_DEPTH_SAFE;
                }
                if(scan_right_res[stat_right_idx].hyst_dph>0x0F)
                {
                    // Put hysteresis depth to the limit.
                    scan_right_res[stat_right_idx].hyst_dph = 0x0F;
                }
                // Gather valid CRC stats.
                updateCRCStats(scan_right_crcs, scan_right_res[stat_right_idx], &valid_right_crcs);
            }
            // If middle part is not good, check for both left AND right parts to have VALID CRC.
            else if((scan_right_p0_res[stat_right_idx].result==REF_CRC_OK)&&(scan_right_p2_res[stat_right_idx].result==REF_CRC_OK))
            {
                // Middle part of the line on the run has INVALID CRC, but other parts have valid CRC.
                valid_crcs = 2;
                // Set valid CRC flag.
                scan_right_res[stat_right_idx].result = REF_CRC_OK;
                // Set dummy CRC to pass through left coordinate stats.
                scan_right_res[stat_right_idx].crc = PCM16X0SubLine::CRC_SILENT;
                // Copy fields from right part data as it tends to have more narrow valid spans.
                scan_right_res[stat_right_idx].hyst_dph = scan_right_p2_res[stat_right_idx].hyst_dph;
                scan_right_res[stat_right_idx].shift_stg = scan_right_p2_res[stat_right_idx].shift_stg;
                scan_right_res[stat_right_idx].data_start = scan_right_p2_res[stat_right_idx].data_start;
                scan_right_res[stat_right_idx].data_stop = scan_right_p2_res[stat_right_idx].data_stop;
                // Check if left part has higher hysteresis depth.
                if(scan_right_p0_res[stat_right_idx].hyst_dph>scan_right_res[stat_right_idx].hyst_dph)
                {
                    // Update hysteresis depth and shifting stage to ones from the left part.
                    scan_right_res[stat_right_idx].hyst_dph = scan_right_p0_res[stat_right_idx].hyst_dph;
                    scan_right_res[stat_right_idx].shift_stg = scan_right_p0_res[stat_right_idx].shift_stg;
                }
                // Check if left part has the same hysteresis depth as the right part.
                else if(scan_right_p0_res[stat_right_idx].hyst_dph==scan_right_res[stat_right_idx].hyst_dph)
                {
                    // Check if left part has higher shifting stage.
                    if(scan_right_p0_res[stat_right_idx].shift_stg>scan_right_res[stat_right_idx].shift_stg)
                    {
                        // Update shifting stage to one from the left part.
                        scan_right_res[stat_right_idx].shift_stg = scan_right_p0_res[stat_right_idx].shift_stg;
                    }
                }
                // Middle part has INVALID CRC, increase hysteresis depth to avoid this run later in CRC processing (make it less desirable).
                scan_right_res[stat_right_idx].hyst_dph += HYST_DEPTH_SAFE;
                if(scan_right_res[stat_right_idx].hyst_dph>0x0F)
                {
                    // Put hysteresis depth to the limit.
                    scan_right_res[stat_right_idx].hyst_dph = 0x0F;
                }
                // Gather valid CRC stats.
                updateCRCStats(scan_right_crcs, scan_right_res[stat_right_idx], &valid_right_crcs);
            }
            else
            {
                scan_right_res[stat_right_idx].result = REF_BAD_CRC;
            }
            // Check if all parts of the line have valid CRC.
            if(valid_crcs==PCM16X0SubLine::SUBLINES_PER_LINE)
            {
#ifdef LB_EN_DBG_OUT
                if((suppress_log==false)&&(lock_left==false))
                {
                    qInfo()<<"[LB] Full line has valid CRCs, left coordinate lock is enabled";
                }
#endif
                // There are all valid parts of the line for current left coordinate.
                lock_left = true;
            }
        }
        // Check if any valid CRCs were found.
        if(valid_right_crcs>0)
        {
            // There is a valid CRC in the list.
            if(pickLevelByCRCStats(scan_right_res, &right_ofs, step_min, step_max, REF_CRC_OK, 0x0F)!=SPAN_OK)
            {
                valid_right_crcs = 0;
            }
        }
        // Re-check if any valid CRCs remained.
        if(valid_right_crcs==0)
        {
            resetCRCStats(scan_right_res, PCM16X0_SEARCH_STEP_CNT);
            resetCRCStats(scan_right_crcs, MAX_COLL_CRCS, &valid_right_crcs);
            // Perform single-part scan, combining three sweeps into one, not targeting middle part.
            for(stat_right_idx=step_min;stat_right_idx<=step_max;stat_right_idx++)
            {
                // Assume that there are no valid CRCs in the middle part and no lines where left AND right part have valid CRCs.
                valid_crcs = 0;
                // Check only right part.
                if(scan_right_p2_res[stat_right_idx].result==REF_CRC_OK)
                {
                    valid_crcs++;
                    // Set valid CRC flag.
                    scan_right_res[stat_right_idx].result = REF_CRC_OK;
                    // Set dummy CRC to pass through left coordinate stats.
                    scan_right_res[stat_right_idx].crc = PCM16X0SubLine::CRC_SILENT;
                    // Copy fields from left part data.
                    scan_right_res[stat_right_idx].hyst_dph = scan_right_p2_res[stat_right_idx].hyst_dph;
                    scan_right_res[stat_right_idx].shift_stg = scan_right_p2_res[stat_right_idx].shift_stg;
                    scan_right_res[stat_right_idx].data_start = scan_right_p2_res[stat_right_idx].data_start;
                    scan_right_res[stat_right_idx].data_stop = scan_right_p2_res[stat_right_idx].data_stop;
                    // Only right part has valid CRC, increase hysteresis depth to avoid this run later in CRC processing (make it less desirable).
                    scan_right_res[stat_right_idx].hyst_dph += HYST_DEPTH_MAX;
                    if(scan_right_res[stat_right_idx].hyst_dph>0x0F)
                    {
                        // Put hysteresis depth to the limit.
                        scan_right_res[stat_right_idx].hyst_dph = 0x0F;
                    }
                    // Gather valid CRC stats.
                    updateCRCStats(scan_right_crcs, scan_right_res[stat_right_idx], &valid_right_crcs);
                }
                // Check only left part.
                else if(scan_right_p0_res[stat_right_idx].result==REF_CRC_OK)
                {
                    valid_crcs++;
                    // Set valid CRC flag.
                    scan_right_res[stat_right_idx].result = REF_CRC_OK;
                    // Set dummy CRC to pass through left coordinate stats.
                    scan_right_res[stat_right_idx].crc = PCM16X0SubLine::CRC_SILENT;
                    // Copy fields from left part data.
                    scan_right_res[stat_right_idx].hyst_dph = scan_right_p0_res[stat_right_idx].hyst_dph;
                    scan_right_res[stat_right_idx].shift_stg = scan_right_p0_res[stat_right_idx].shift_stg;
                    scan_right_res[stat_right_idx].data_start = scan_right_p0_res[stat_right_idx].data_start;
                    scan_right_res[stat_right_idx].data_stop = scan_right_p0_res[stat_right_idx].data_stop;
                    // Only left part has valid CRC, increase hysteresis depth to avoid this run later in CRC processing (make it less desirable).
                    scan_right_res[stat_right_idx].hyst_dph += 2*HYST_DEPTH_SAFE;
                    if(scan_right_res[stat_right_idx].hyst_dph>0x0F)
                    {
                        // Put hysteresis depth to the limit.
                        scan_right_res[stat_right_idx].hyst_dph = 0x0F;
                    }
                    // Gather valid CRC stats.
                    updateCRCStats(scan_right_crcs, scan_right_res[stat_right_idx], &valid_right_crcs);
                }
                else
                {
                    scan_right_res[stat_right_idx].result = REF_BAD_CRC;
                }
            }
            // Check if any valid CRCs were found.
            if(valid_right_crcs>0)
            {
                // There is a valid CRC in the list.
                if(pickLevelByCRCStats(scan_right_res, &right_ofs, step_min, step_max, REF_CRC_OK, 0x0F)!=SPAN_OK)
                {
                    valid_right_crcs = 0;
                }
            }
        }

#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if((valid_right_p0_crcs>0)||(valid_right_p1_crcs>0)||(valid_right_p2_crcs>0))
            {
                qInfo()<<"[LB] Left part right coordinate sweep with left coordinate at"<<start_ofs;
                textDumpCRCSweep(scan_right_p0_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), right_ofs);
                qInfo()<<"[LB] Middle part right coordinate sweep with left coordinate at"<<start_ofs;
                textDumpCRCSweep(scan_right_p1_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), right_ofs);
                qInfo()<<"[LB] Right part right coordinate sweep with left coordinate at"<<start_ofs;
                textDumpCRCSweep(scan_right_p2_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), right_ofs);
                qInfo()<<"[LB] Combined right coordinate sweep with left coordinate at"<<start_ofs;
                textDumpCRCSweep(scan_right_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), right_ofs);
                if(valid_right_crcs>0)
                {
                    qInfo()<<"[LB] Picked right coordinate:"<<scan_right_res[right_ofs].data_stop;
                }
                else
                {
                    qInfo()<<"[LB] Unable to find valid right coordinate!";
                }
            }
            if(valid_right_p0_crcs==0)
            {
                log_line.sprintf("[LB] No left part valid right coordinates after sweep with left coordinate at %03d (scan index [%03u])",
                                 start_ofs, stat_left_idx);
                qInfo()<<log_line;
            }
            if(valid_right_p1_crcs==0)
            {
                log_line.sprintf("[LB] No middle part valid right coordinates after sweep with left coordinate at %03d (scan index [%03u])",
                                 start_ofs, stat_left_idx);
                qInfo()<<log_line;
            }
            if(valid_right_p2_crcs==0)
            {
                log_line.sprintf("[LB] No right part valid right coordinates after sweep with left coordinate at %03d (scan index [%03u])",
                                 start_ofs, stat_left_idx);
                qInfo()<<log_line;
            }
        }
#endif
        // Check if any valid CRCs remaining after right coordinate scan.
        if(valid_right_crcs>0)
        {
            // Save good CRC data for left coordinate.
            scan_left_res[stat_left_idx].result = REF_CRC_OK;
            scan_left_res[stat_left_idx].crc = scan_right_res[right_ofs].crc;
            scan_left_res[stat_left_idx].hyst_dph = scan_right_res[right_ofs].hyst_dph;
            scan_left_res[stat_left_idx].shift_stg = scan_right_res[right_ofs].shift_stg;
            scan_left_res[stat_left_idx].data_start = scan_right_res[right_ofs].data_start;
            scan_left_res[stat_left_idx].data_stop = scan_right_res[right_ofs].data_stop;
            updateCRCStats(scan_left_crcs, scan_right_res[right_ofs], &valid_left_crcs);
            // Check if there was left coordinate before that had all parts of the line valid.
            if(lock_left!=false)
            {
                valid_crcs = 0;
                // Count number of valid parts of the line at current run.
                if(scan_right_p0_res[right_ofs].result==REF_CRC_OK) valid_crcs++;
                if(scan_right_p1_res[right_ofs].result==REF_CRC_OK) valid_crcs++;
                if(scan_right_p2_res[right_ofs].result==REF_CRC_OK) valid_crcs++;
                if(valid_crcs<2)
                {
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] Current left coordinate run has invalid parts while left coordinate lock is active, stopping";
                    }
#endif
                    // No need to search more, full valid line was obtained and not it is not.
                    break;
                }
            }
        }
        stat_left_idx++;
        if(stat_left_idx>=PCM16X0_SEARCH_STEP_CNT)
        {
            qWarning()<<DBG_ANCHOR<<"[LB] Logic error! l_idx ="<<stat_left_idx<<"l_ofs ="<<start_ofs;
            break;
        }
    }
    // Disable forced Bit Picker for normal operation.
    force_bit_picker = false;

    // Check if any valid CRCs for found during left coordinate scan.
    if(valid_left_crcs>0)
    {
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Found %u valid CRCs during left coordinate scan", valid_left_crcs);
            qInfo()<<log_line;
        }
#endif
        // Get optimal offset for left coordinate.
        findMostFrequentCRC(scan_left_crcs, &valid_left_crcs, false, suppress_log);

#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(valid_left_crcs==0)
            {
                qInfo()<<"[LB] No valid CRCs after filtering remained...";
            }
        }
#endif
        // Invalidate not frequent enough CRCs.
        invalidateNonFrequentCRCs(scan_left_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), valid_left_crcs, scan_left_crcs[0].crc, true);
    }

    // Re-check if valid CRCs remained after CRC filtering.
    if(valid_left_crcs>0)
    {
        // Pick optimal offset if anything was found.
        if(pickLevelByCRCStats(scan_left_res, &left_ofs, 0, (PCM16X0_SEARCH_STEP_CNT-1), REF_CRC_OK, 0x0F)!=SPAN_OK)
        {
            valid_left_crcs = 0;
        }
    }

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[LB] Frame"<<pcm_line->frame_number<<"line"<<pcm_line->line_number<<"left coordinate sweep:";
        textDumpCRCSweep(scan_left_res, 0, (PCM16X0_SEARCH_STEP_CNT-1), left_ofs);
        if(valid_left_crcs>0)
        {
            qInfo()<<"[LB] Picked left coordinate:"<<scan_left_res[left_ofs].data_start<<"picked right coordinate:"<<scan_left_res[left_ofs].data_stop;
        }
    }
#endif

    // Check if any valid CRCs remaining after left coordinate scan.
    if(valid_left_crcs>0)
    {
        // Found some coordinates.
        // Set new coordinates.
        pcm_line->coords.data_start = scan_left_res[left_ofs].data_start;
        pcm_line->coords.data_stop = scan_left_res[left_ofs].data_stop;
        pcm_line->setDataCoordinatesState(true);
        pcm_line->setSweepedCoordinates(true);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Final data coordinates: [%03d:%04d]",
                             pcm_line->coords.data_start, pcm_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
        return LB_RET_OK;
    }
    else
    {
        // Nothing useful was found.
        // Set default coordinates.
        pcm_line->coords = data_loc;
        pcm_line->setSweepedCoordinates(false);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] No data coordinates found, revert to preset [%03d:%04d]",
                             pcm_line->coords.data_start, pcm_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
        return LB_RET_NO_COORD;
    }
}

//------------------------ Try to find PCM data coordinates for STC-007/PCM-F1 with provided hysteresis for reference level.
//------------------------ Find START (1010) and STOP (01111) line markers and calculate data coordinates from there.
void Binarizer::searchSTC007Markers(STC007Line *stc_line, uint8_t hyst_lvl, bool no_log)
{
    bool suppress_log;
    uint8_t marker_detect_stage, pixel_val;
    uint8_t bin_level, bin_low, bin_high;
    uint16_t pixel, pixel_limit;
    uint16_t mark_st_1bit_start,    // Pixel coordinate of the start of the 1st "1" START bits.
             mark_st_1bit_end,      // Pixel coordinate of the end of the 1st "1" START bits and of the start of the 1st "0" START bits.
             mark_st_3bit_start,    // Pixel coordinate of the start of the 2nd "1" START bits and of the end of the 1st "0" START bits.
             mark_st_3bit_end,      // Pixel coordinate of the end of the 2nd "1" START bits and of the start of the 2nd "0" START bits.
             mark_ed_bit_start,     // Pixel coordinate of the start of the "1111" STOP bits and of the end of the "0" STOP bit.
             mark_ed_bit_end;       // Pixel coordinate of the end of the "1111" STOP bits.

    // Perform marker binarization and find coordinates of PCM START marker.
    marker_detect_stage = STC007Line::MARK_ST_START;
    mark_st_1bit_start = mark_st_1bit_end = mark_st_3bit_start = mark_st_3bit_end = 0;

    suppress_log = !((log_level&LOG_COORD)!=0);

    // Get binarization levels.
    bin_level = (stc_line->ref_level);
    // Calculate low and high levels for hysteresis.
    bin_low = getLowLevel(bin_level, hyst_lvl);
    if(bin_low<digi_set.min_ref_lvl)
    {
        bin_low = digi_set.min_ref_lvl;
    }
    bin_high = bin_level;   //bin_high = getHighLevel(bin_level, hyst_lvl);

    // Calculate maximum pixel number for the marker search (limit for 1-st bit + 4 bits x PPB + margin).
    pixel_limit = mark_start_max+estimated_ppb*5;
    // Clip the limit if necessary.
    if(pixel_limit>line_length)
    {
        pixel_limit = line_length;
    }
    pixel = scan_start;
#ifdef LB_EN_DBG_OUT
    QString log_line;
    if(((suppress_log==false)||((log_level&LOG_PROCESS)!=0))&&(no_log==false))
    {
        qInfo()<<"[LB] ---------- STC-007 marker search starting...";
        log_line.sprintf("[LB] Searching START marker, detection in range [%03u...%03u], full marker in [%03u...%03u]",
                         scan_start, mark_start_max, scan_start, pixel_limit);
        qInfo()<<log_line;
        log_line.sprintf("[LB] Ref. levels for marker search: [%03u|%03u]", bin_low, bin_high);
        qInfo()<<log_line;
    }
#endif
    // Forward bit search from left to right.
    while(pixel<pixel_limit)
    {
        // Get pixel brightness level (8 bit grayscale).
        pixel_val = getPixelBrightness(pixel);
#ifdef LB_LOG_MARKER_VERBOSE
        if((suppress_log==false)&&(no_log==false))
        {
            log_line.sprintf("[LB] Pixel %04u val.: %03u, ref.: %03u|%03u",
                             pixel, pixel_val, bin_low, bin_high);
            qInfo()<<log_line;
        }
#endif
        if(marker_detect_stage==STC007Line::MARK_ST_START)
        {
            // Check smaller limit for the first START bit only.
            if(pixel>mark_start_max)
            {
#ifdef LB_EN_DBG_OUT
                if((suppress_log==false)&&(no_log==false))
                {
                    log_line.sprintf("[LB-%03u] No START-bit[1] found within limits [%03u:%03u], search stopped!", bin_level, scan_start, mark_start_max);
                    qInfo()<<log_line;
                }
#endif
                // Stop the search.
                break;
            }
            // Detect /\ transition to first "1" marker bit.
            if(pixel_val>=bin_low)
            {
                // Save pixel coordinate of the beginning of 1st START bit.
                mark_st_1bit_start = pixel;
                // Update START marker detection stage.
                marker_detect_stage = STC007Line::MARK_ST_TOP_1;
#ifdef LB_LOG_MARKER_VERBOSE
                if((suppress_log==false)&&(no_log==false))
                {
                    qInfo()<<"[LB] Got 0->1 transition of ongoing START-bit[1]";
                }
#endif
            }
        }
        else if(marker_detect_stage==STC007Line::MARK_ST_TOP_1)
        {
            // Detect \/ transition to first "0" marker bit.
            if(pixel_val<bin_low)
            {
                // Save pixel coordinate of the end of 1st START bit.
                mark_st_1bit_end = pixel;
                // Update START marker detection stage.
                marker_detect_stage = STC007Line::MARK_ST_BOT_1;
#ifdef LB_EN_DBG_OUT
                if((suppress_log==false)&&(no_log==false))
                {
                    log_line.sprintf("[LB-%03u] START-bit[1] found at [%03u:%03u]", bin_level, mark_st_1bit_start, mark_st_1bit_end);
                    qInfo()<<log_line;
                }
#endif
            }
        }
        else if(marker_detect_stage==STC007Line::MARK_ST_BOT_1)
        {
            // Detect /\ transition to second "1" marker bit.
            if(pixel_val>=bin_high)
            {
                // Save pixel coordinate of the beginning of 3rd START bit.
                mark_st_3bit_start = pixel;
                // Check if detected "0 bit" complies with required length.
                if(((mark_st_3bit_start-mark_st_1bit_end)>(estimated_ppb*2))||((mark_st_3bit_start-mark_st_1bit_end)<(estimated_ppb/2)))
                {
                    // Reset search if length is inadequate.
                    marker_detect_stage = STC007Line::MARK_ST_START;
#ifdef LB_EN_DBG_OUT
                    if((suppress_log==false)&&(no_log==false))
                    {
                        log_line.sprintf("[LB-%03u] Erroneous START-bit[2] at [%03u:%03u], restarting...", bin_level, mark_st_1bit_end, mark_st_3bit_start);
                        qInfo()<<log_line;
                    }
#endif
                }
                else
                {
                    // Update START marker detection stage.
                    marker_detect_stage = STC007Line::MARK_ST_TOP_2;
#ifdef LB_EN_DBG_OUT
                    if((suppress_log==false)&&(no_log==false))
                    {
                        log_line.sprintf("[LB-%03u] START-bit[2] found at [%03u:%03u]", bin_level, mark_st_1bit_end, mark_st_3bit_start);
                        qInfo()<<log_line;
                    }
#endif
                }
            }
        }
        else if(marker_detect_stage==STC007Line::MARK_ST_TOP_2)
        {
            // Detect \/ transition to second "0" marker bit.
            if(pixel_val<bin_high)
            {
                // Save pixel coordinate of the end of 3rd START bit.
                mark_st_3bit_end = pixel;
                // Check if detected "1 bit" complies with required length.
                if(((mark_st_3bit_end-mark_st_3bit_start)>(estimated_ppb*2))||((mark_st_3bit_end-mark_st_3bit_start)<(estimated_ppb/2)))
                {
                    // Reset search if length is inadequate.
                    marker_detect_stage = STC007Line::MARK_ST_START;
#ifdef LB_EN_DBG_OUT
                    if((suppress_log==false)&&(no_log==false))
                    {
                        log_line.sprintf("[LB-%03u] Erroneous START-bit[3] at [%03u:%03u], restarting...", bin_level, mark_st_3bit_start, mark_st_3bit_end);
                        qInfo()<<log_line;
                    }
#endif
                }
                else
                {
                    // Update START marker detection stage.
                    marker_detect_stage = STC007Line::MARK_ST_BOT_2;
#ifdef LB_EN_DBG_OUT
                    if((suppress_log==false)&&(no_log==false))
                    {
                        log_line.sprintf("[LB-%03u] START-bit[3] found at [%03u:%03u]", bin_level, mark_st_3bit_start, mark_st_3bit_end);
                        qInfo()<<log_line;
                    }
#endif
                    // Stop START marker search.
                    break;
                }
            }
        }
        // Go to next pixel in the line.
        pixel++;
    }

    // Store final state.
    stc_line->mark_st_stage = marker_detect_stage;
    // Save marker start coordinate.
    stc_line->marker_start_bg_coord = mark_st_1bit_start;
    stc_line->marker_start_ed_coord = mark_st_3bit_end;
#ifdef LB_EN_DBG_OUT
    if((suppress_log==false)&&(no_log==false))
    {
        if(stc_line->hasStartMarker()==false)
        {
            log_line.sprintf("[LB-%03u] START marker was NOT found!", bin_level);
            qInfo()<<log_line;
        }
    }
#endif

    // Refine coordinates of PCM STOP marker with new reference level.
    // Update STOP marker detection stage and coordinates.
    marker_detect_stage = STC007Line::MARK_ED_START;
    mark_ed_bit_start = mark_ed_bit_end = 0;
    // Perform PCM STOP-marker refinement (after reference adjustment) only if PCM START-marker was detected.
    if(stc_line->hasStartMarker())
    {
        bin_low = bin_level;
        // Calculate minimum pixel number for the marker search.
        if(mark_end_min>(estimated_ppb*6))
        {
            pixel_limit = mark_end_min-estimated_ppb*6;
        }
        else
        {
            pixel_limit = 0;
        }
        pixel = scan_end;
#ifdef LB_EN_DBG_OUT
        if((suppress_log==false)&&(no_log==false))
        {
            log_line.sprintf("[LB] Searching STOP marker, detection in range [%04u...%04u], full marker in [%04u...%04u]",
                             pixel, mark_end_min, pixel, pixel_limit);
            qInfo()<<log_line;
        }
#endif
        // Backwards bit search from right to left.
        while(pixel>pixel_limit)
        {
            // Get pixel brightness level (8 bit grayscale).
            pixel_val = getPixelBrightness(pixel);
#ifdef LB_LOG_MARKER_VERBOSE
            if((suppress_log==false)&&(no_log==false))
            {
                log_line.sprintf("[LB] Pixel %04u val.: %03u, ref.: %03u|%03u",
                                 pixel, pixel_val, bin_low, bin_high);
                qInfo()<<log_line;
            }
#endif
            if(marker_detect_stage==STC007Line::MARK_ED_START)
            {
                // Check additional limit on start of the STOP marker.
                if(pixel<mark_end_min)
                {
#ifdef LB_EN_DBG_OUT
                    if((suppress_log==false)&&(no_log==false))
                    {
                        log_line.sprintf("[LB-%03u] No STOP maker started within limits [%03u:%03u], search stopped!", bin_level, scan_end, mark_end_min);
                        qInfo()<<log_line;
                    }
#endif
                    // Stop the search.
                    break;
                }
                // Detect /\ transition to "1" marker bit.
                if(pixel_val>=bin_low)
                {
                    // Save pixel coordinate of the end of STOP marker (4-bit wide).
                    mark_ed_bit_end = (pixel+1);
                    // Update STOP marker detection stage.
                    marker_detect_stage=STC007Line::MARK_ED_TOP;
#ifdef LB_LOG_MARKER_VERBOSE
                    if((suppress_log==false)&&(no_log==false))
                    {
                        qInfo()<<"[LB] Got 0->1 transition from the end of STOP-marker";
                    }
#endif
                }
            }
            else if(marker_detect_stage==STC007Line::MARK_ED_TOP)
            {
                // Detect \/ transition to "0" marker bit.
                if(pixel_val<bin_high)
                {
                    // Save pixel coordinate of the start of STOP marker (4-bit wide).
                    mark_ed_bit_start = (pixel+1);
                    // Update STOP marker detection stage.
                    marker_detect_stage = STC007Line::MARK_ED_BOT;
                    // Check if "1" bit length is of correct length.
                    if(((mark_ed_bit_end-mark_ed_bit_start)>=(estimated_ppb*2))&&((mark_ed_bit_end-mark_ed_bit_start)<=(estimated_ppb*5)))
                    //if((mark_ed_bit_end-mark_ed_bit_start)>=(estimated_ppb*2))
                    {
                        // Update STOP marker detection stage.
                        marker_detect_stage = STC007Line::MARK_ED_LEN_OK;
#ifdef LB_EN_DBG_OUT
                        if((suppress_log==false)&&(no_log==false))
                        {
                            log_line.sprintf("[LB-%03u] STOP-marker updated to be at [%03u:%03u]", bin_level, mark_ed_bit_start, mark_ed_bit_end);
                            qInfo()<<log_line;
                        }
#endif
                        // Finish STOP marker search.
                        break;
                    }
                    else
                    {
                        // Wrong "1" bit length, probably noise - reset state machine.
                        marker_detect_stage = STC007Line::MARK_ED_START;
#ifdef LB_EN_DBG_OUT
                        if((suppress_log==false)&&(no_log==false))
                        {
                            log_line.sprintf("[LB-%03u] Erroneous STOP-marker at [%03u:%03u], restarting...", bin_level, mark_ed_bit_start, mark_ed_bit_end);
                            qInfo()<<log_line;
                        }
#endif
                    }
                }
            }
            // Go to previous pixel in the line.
            pixel--;
        }
        // Store final state.
        stc_line->mark_ed_stage = marker_detect_stage;
    }
    // Save data coordinates.
    stc_line->coords.setCoordinates(mark_st_1bit_end, mark_ed_bit_start);
    // Save marker stop coordinate.
    stc_line->marker_stop_ed_coord = mark_ed_bit_end;
    // Set data coordinates state.
    stc_line->setDataCoordinatesState(stc_line->hasMarkers());
}

//------------------------ Try to find PCM data coordinates for PCM-1.
//------------------------ Sweep beginning and ending data coordinates,
//------------------------ taking provided data coordinates as center point,
//------------------------ trying to get valid CRCs on all sub-lines.
bool Binarizer::findPCM1Coordinates(PCM1Line *pcm1_line, CoordinatePair coord_history)
{
    bool suppress_log;
    bool search_state;
    uint8_t scan_state;
    uint8_t in_hyst_depth, in_shift_stages;
    CoordinatePair data_coord;
    uint16_t line_margin;

    suppress_log = ((log_level&LOG_COORD)==0);

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[LB] ---------- Starting search for PCM-1 data...";
        if((digi_set.left_bit_pick==0)&&(digi_set.right_bit_pick==0))
        {
            qInfo()<<"[LB] Bit Picker is disabled";
        }
        else
        {
            qInfo()<<"[LB] Bit Picker is enabled";
        }
    }
#endif

    //pcm1_line->coords.setCoordinates(-15, 1430);

    // Calculate limits for line scanning.
    line_margin = scan_end-scan_start;
    //line_margin = line_margin/16;
    line_margin = line_margin/16;

    // Determine starting search coordinates.
    if(coord_history.areValid()!=false)
    {
        // Set starting coordinates to averaged history coordinates.
        data_coord = coord_history;
    }
    else
    {
        // Take left scan limit as starting point.
        data_coord.data_start = scan_start;
        // Get state of the first pixel.
        search_state = (getPixelBrightness(data_coord.data_start)>pcm1_line->ref_level);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[LB] Reference level:"<<pcm1_line->ref_level;
            if(search_state==false)
            {
                log_line.sprintf("[LB] Searching for 0->1 transition from %03d to %03d...", data_coord.data_start, (scan_start+line_margin));
            }
            else
            {
                log_line.sprintf("[LB] Searching for 1->0 transition from %03d to %03d...", data_coord.data_start, (scan_start+line_margin));
            }
            qInfo()<<log_line;
        }
        if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
        {
            log_line.sprintf("[LB] Left side transition search limits: [%03d:%03d]", data_coord.data_start, (scan_start+line_margin));
            qInfo()<<log_line;
        }
#endif
        // Search for the first bit transition in the line.
        for(uint16_t pixel=scan_start;pixel<(scan_start+line_margin);pixel++)
        {
            if(search_state==false)
            {
                // First pixel is "0".
                if(getPixelBrightness(pixel)>pcm1_line->ref_level)
                {
                    // 0 -> 1 transition detected.
                    data_coord.data_start = (pixel-1);      // First run should always pass through, because its the starting value.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 0->1 transition for left coordinate at [%03d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
            else
            {
                // First pixel is "1".
                if(getPixelBrightness(pixel)<pcm1_line->ref_level)
                {
                    // 1 -> 0 transition detected.
                    data_coord.data_start = (pixel-1);      // First run should always pass through, because its the starting value.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 1->0 transition for left coordinate at [%03d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
        }
        // Take right scan limit as ending point.
        data_coord.data_stop = scan_end;
        // Get state of the last pixel.
        search_state = (getPixelBrightness(data_coord.data_stop)>pcm1_line->ref_level);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(search_state==false)
            {
                log_line.sprintf("[LB] Searching for 0->1 transition from %04d to %04d...", data_coord.data_stop, (scan_end-line_margin));
            }
            else
            {
                log_line.sprintf("[LB] Searching for 1->0 transition from %04d to %04d...", data_coord.data_stop, (scan_end-line_margin));
            }
            qInfo()<<log_line;
        }
        if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
        {
            log_line.sprintf("[LB] Right side transition search limits: [%04d:%04d]", data_coord.data_stop, (scan_end-line_margin));
            qInfo()<<log_line;
        }
#endif
        // Search for the last bit transition in the line.
        for(uint16_t pixel=scan_end;pixel>(scan_end-line_margin);pixel--)
        {
            if(search_state==false)
            {
                // First pixel is "0".
                if(getPixelBrightness(pixel)>pcm1_line->ref_level)
                {
                    // 0 -> 1 transition detected.
                    data_coord.data_stop = (pixel+1);   // First run should always pass through, because its the starting value.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 0->1 transition for right coordinate at [%04d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
            else
            {
                // First pixel is "1".
                if(getPixelBrightness(pixel)<pcm1_line->ref_level)
                {
                    // 1 -> 0 transition detected.
                    data_coord.data_stop = (pixel+1);
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 1->0 transition for right coordinate at [%04d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
        }
    }

    // Preset "coordinates were not found" state.
    search_state = false;
#ifdef LB_EN_DBG_OUT
    if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
    {
        log_line.sprintf("[LB] Rough data scan interval: [%03d:%04d]", data_coord.data_start, data_coord.data_stop);
        qInfo()<<log_line;
    }
#endif

    // Save binarization options.
    in_hyst_depth = hysteresis_depth_lim;
    in_shift_stages = shift_stages_lim;

    // Sweep left and right offsets.
    scan_state = searchPCM1Data(pcm1_line, data_coord);
    if(scan_state==LB_RET_OK)
    {
        // Valid coordinates and data were found.
        search_state = true;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Good coordinates [%03d:%04d]", pcm1_line->coords.data_start, pcm1_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
    }

    // Restore binarization options.
    hysteresis_depth_lim = in_hyst_depth;
    shift_stages_lim = in_shift_stages;

#ifdef LB_EN_DBG_OUT
    if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
    {
        log_line.sprintf("[LB] Using data coordinates: [%03d:%04d]", pcm1_line->coords.data_start, pcm1_line->coords.data_stop);
        qInfo()<<log_line;
    }
#endif

    // Mark source video line as already scanned.
    video_line->scan_done = true;

    return search_state;
}

//------------------------ Try to find PCM data coordinates for PCM-16x0.
//------------------------ Sweep beginning and ending data coordinates,
//------------------------ taking provided data coordinates as center point,
//------------------------ trying to get valid CRCs on all sub-lines.
bool Binarizer::findPCM16X0Coordinates(PCM16X0SubLine *pcm16x0_line, CoordinatePair coord_history)
{
    bool suppress_log;
    bool search_state;
    uint8_t scan_state;
    uint8_t in_line_mode, in_hyst_depth, in_shift_stages;
    CoordinatePair data_coord;
    uint16_t line_margin;

    suppress_log = ((log_level&LOG_COORD)==0);

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
    {
        qInfo()<<"[LB] ---------- Starting search for PCM-16x0 data...";
        if((digi_set.left_bit_pick==0)&&(digi_set.right_bit_pick==0))
        {
            qInfo()<<"[LB] Bit Picker is disabled";
        }
        else
        {
            qInfo()<<"[LB] Bit Picker is enabled";
        }
    }
#endif

    if(video_line->scan_done!=false)
    {
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[LB] Source line was already scanned for coordinate, skipping...";
        }
#endif
        return false;
    }

    // Calculate limits for line scanning.
    line_margin = scan_end-scan_start;
    line_margin = line_margin/40;

    // Determine starting search coordinates.
    if(coord_history.areValid()!=false)
    {
        // Set starting coordinates to averaged history coordinates.
        data_coord = coord_history;
    }
    else
    {
        // Take left scan limit as starting point.
        data_coord.data_start = scan_start;
        // Get state of the first pixel.
        search_state = (getPixelBrightness(data_coord.data_start)>pcm16x0_line->ref_level);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[LB] Reference level:"<<pcm16x0_line->ref_level;
            if(search_state==false)
            {
                log_line.sprintf("[LB] Searching for 0->1 transition from %03d to %03d...", data_coord.data_start, (scan_start+line_margin));
            }
            else
            {
                log_line.sprintf("[LB] Searching for 1->0 transition from %03d to %03d...", data_coord.data_start, (scan_start+line_margin));
            }
            qInfo()<<log_line;
        }
        if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
        {
            log_line.sprintf("[LB] Left side data search limits: [%03d:%03d]", data_coord.data_start, (scan_start+line_margin));
            qInfo()<<log_line;
        }
#endif
        // Search for the first bit transition in the line.
        for(uint16_t pixel=scan_start;pixel<(scan_start+line_margin);pixel++)
        {
            if(search_state==false)
            {
                // First pixel is "0".
                if(getPixelBrightness(pixel)>pcm16x0_line->ref_level)
                {
                    // 0 -> 1 transition detected.
                    // TODO: maybe remove "-1"
                    data_coord.data_start = (pixel-1);     // First run should always pass through, because its the starting value.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 0->1 transition for left coordinate at [%03d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
            else
            {
                // First pixel is "1".
                if(getPixelBrightness(pixel)<pcm16x0_line->ref_level)
                {
                    // 1 -> 0 transition detected.
                    data_coord.data_start = (pixel-1);     // First run should always pass through, because its the starting value.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 1->0 transition for left coordinate at [%03d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
        }
        // Take right scan limit as ending point.
        data_coord.data_stop = scan_end;
        // Get state of the last pixel.
        search_state = (getPixelBrightness(data_coord.data_stop)>pcm16x0_line->ref_level);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            if(search_state==false)
            {
                log_line.sprintf("[LB] Searching for 0->1 transition from %04d to %04d...", data_coord.data_stop, (scan_end-line_margin));
            }
            else
            {
                log_line.sprintf("[LB] Searching for 1->0 transition from %04d to %04d...", data_coord.data_stop, (scan_end-line_margin));
            }
            qInfo()<<log_line;
        }
        if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
        {
            log_line.sprintf("[LB] Right side data search limits: [%04d:%04d]", data_coord.data_stop, (scan_end-line_margin));
            qInfo()<<log_line;
        }
#endif
        // Search for the last bit transition in the line.
        for(uint16_t pixel=scan_end;pixel>(scan_end-line_margin);pixel--)
        {
            if(search_state==false)
            {
                // First pixel is "0".
                if(getPixelBrightness(pixel)>pcm16x0_line->ref_level)
                {
                    // 0 -> 1 transition detected.
                    data_coord.data_stop = (pixel+1);    // First run should always pass through, because its the starting value.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 0->1 transition for right coordinate at [%04d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
            else
            {
                // First pixel is "1".
                if(getPixelBrightness(pixel)<pcm16x0_line->ref_level)
                {
                    // 1 -> 0 transition detected.
                    data_coord.data_stop = (pixel+1);    // First run should always pass through, because its the starting value.
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Found 1->0 transition for right coordinate at [%04d]", pixel);
                        qInfo()<<log_line;
                    }
#endif
                    break;
                }
            }
        }
    }

    // Preset "coordinates were not found" state.
    search_state = false;
#ifdef LB_EN_DBG_OUT
    if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
    {
        log_line.sprintf("[LB] Rough data scan interval: [%03d:%04d]", data_coord.data_start, data_coord.data_stop);
        qInfo()<<log_line;
    }
#endif

    // Save binarization options.
    in_line_mode = line_part_mode;
    in_hyst_depth = hysteresis_depth_lim;
    in_shift_stages = shift_stages_lim;

    // Sweep left and right offsets.
    scan_state = searchPCM16X0Data(pcm16x0_line, data_coord);
    if(scan_state==LB_RET_OK)
    {
        // Valid coordinates and data were found.
        search_state = true;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Good coordinates [%03d:%04d]", pcm16x0_line->coords.data_start, pcm16x0_line->coords.data_stop);
            qInfo()<<log_line;
        }
#endif
    }

    // Restore binarization options.
    line_part_mode = in_line_mode;
    hysteresis_depth_lim = in_hyst_depth;
    shift_stages_lim = in_shift_stages;

#ifdef LB_EN_DBG_OUT
    if((suppress_log==false)||((log_level&LOG_PROCESS)!=0))
    {
        log_line.sprintf("[LB] Using data coordinates: [%03d:%04d]", pcm16x0_line->coords.data_start, pcm16x0_line->coords.data_stop);
        qInfo()<<log_line;
    }
#endif

    // Mark source video line as already scanned.
    video_line->scan_done = true;

    return search_state;
}

//------------------------ Try to find PCM data coordinates for STC-007/PCM-F1.
//------------------------ Coordinates are derived from START and STOP markers' coordinates.
//------------------------ This function also performs reference level sweep for START marker if allowed.
void Binarizer::findSTC007Coordinates(STC007Line *stc_line, bool no_log)
{
    bool suppress_log;
    uint8_t best_hyst;
    STC007Line temp_line;
    CoordinatePair temp_coords;
    std::vector<CoordinatePair> coord_list;

    suppress_log = !((log_level&LOG_COORD)!=0);
    // Preset default hysteresis level (hysteresis off).
    best_hyst = 0;
    // Check if START-marker sweep is enabled.
    if(do_start_mark_sweep!=false)
    {
        // Copy reference level from input PCM line.
        temp_line = *stc_line;
        // Cycle through hysteresis levels.
        for(uint8_t hyst_idx=0;hyst_idx<24;hyst_idx++)
        {
            // Search for markers with current hysteresis level.
            searchSTC007Markers(&temp_line, hyst_idx, true);
            // Check if markers were found.
            if(temp_line.hasMarkers()!=false)
            {
                temp_coords = temp_line.coords;
                temp_coords.reference = hyst_idx;
                temp_coords.not_sure = false;
                // Save result with valid data coordinates.
                coord_list.push_back(temp_coords);
            }
        }
        if(coord_list.size()>0)
        {
            // Sort coordinates list.
            std::sort(coord_list.begin(), coord_list.end());
            // Pick first in the list as the best one (and replace the default one).
            best_hyst = coord_list[0].reference;
        }
    }
#ifdef LB_EN_DBG_OUT
    if((do_start_mark_sweep!=false)&&(no_log==false))
    {
        QString log_line;
        if(best_hyst!=0)
        {
            if((log_level&LOG_PROCESS)!=0)
            {
                log_line.sprintf("[LB] Line [%03u:%03u] START marker hysteresis detected: [%02u]",
                                 stc_line->frame_number,
                                 stc_line->line_number,
                                 best_hyst);
                qInfo()<<log_line;
            }
        }
        if((suppress_log==false)&&(coord_list.size()>0))
        {
            for(uint8_t hyst_idx=0;hyst_idx<coord_list.size();hyst_idx++)
            {
                log_line.sprintf("[LB] Hyst.: [%02u], coords.: [%03u:%04u]", coord_list[hyst_idx].reference, coord_list[hyst_idx].data_start, coord_list[hyst_idx].data_stop);
                qInfo()<<log_line;
            }
        }
    }
#endif
    // Make final markers/coordinates search with set hysteresis level.
    searchSTC007Markers(stc_line, best_hyst, no_log);
}

//------------------------ Bruteforce pick bits that were cut off the line.
uint8_t Binarizer::pickCutBitsUpPCM1(PCM1Line *pcm_line, bool no_log)
{
    bool suppress_log;
    bool patch_found, coll_lock;
    uint8_t max_cut_bits, left_bit_count, right_bit_count;
    uint16_t index, idx_in;
    uint16_t first_pixel_coord, current_pixel_coord, left_rep_limit, right_rep_limit;
    uint16_t left_orig_word, right_orig_word, left_clean_word, right_clean_word;
    uint16_t left_patch_word, right_patch_word, left_fix_word, right_fix_word;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&(no_log==false));
    //suppress_log = false;

#ifdef LB_EN_DBG_OUT
    QString coords, log_line;
#endif

    // Reset picked bits counters.
    pcm_line->picked_bits_left = 0;
    pcm_line->picked_bits_right = 0;

    patch_found = coll_lock = false;
    left_bit_count = right_bit_count = 0;
    left_fix_word = right_fix_word = 0;

    // Detect how many bits are missing from the left of the line.
    // Determine limit for number of picked bits.
    max_cut_bits = digi_set.left_bit_pick;
    if(bin_mode==MODE_DRAFT)
    {
        // Halve allowed bit count in ultra-fast mode.
        max_cut_bits = max_cut_bits/2;
    }
    // Get leftmost bit pixel coordinate.
    first_pixel_coord = scan_start;
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        coords = QString::number(first_pixel_coord)+" (start) | ";
        qInfo()<<"[LB] Detecting number of bits cut from the left of the line"<<pcm_line->line_number<<"(maximum:"<<max_cut_bits<<")";
    }
#endif
    // Find first bit with different coordinate.
    for(index=0;index<max_cut_bits;index++)
    {
        // Get [i] bit coordinate.
        current_pixel_coord = pcm_line->getVideoPixelT(index, 0);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords += QString::number(current_pixel_coord)+" (bit "+QString::number(index)+")";
        }
#endif
        // Check if difference between bit coordinates is more than half of the PPB.
        if((current_pixel_coord-first_pixel_coord)>=((pcm_line->getPPB()+1)/2))
        {
            // Stop search.
            break;
        }
        // Replace line boundary with first "real" pixel coordinate.
        if(index==0)
        {
            first_pixel_coord = current_pixel_coord;
        }
        // Save bit count for cut pixels.
        left_bit_count = (index+1);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords += ", ";
        }
#endif
    }
    // Calculate how many iterations to substitute.
    left_rep_limit = (1<<left_bit_count);
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        coords = "[LB] Coordinates: "+coords;
        qInfo()<<coords;
        if(left_bit_count>0)
        {
            qInfo()<<"[LB] Calculated to pick"<<left_bit_count<<"leftmost bits through up to"<<left_rep_limit<<"iterations...";
        }
        else
        {
            qInfo()<<"[LB] Calculated to pick 0 leftmost bits";
        }
    }
#endif

    // Detect how many bits are missing from the right of the line.
    // Get rightmost bit pixel coordinate.
    first_pixel_coord = scan_end;
    // Determine limit for number of picked bits.
    max_cut_bits = digi_set.right_bit_pick;
    if(bin_mode==MODE_DRAFT)
    {
        // Halve allowed bit count in ultra-fast mode.
        max_cut_bits = max_cut_bits/2;
    }
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        coords = QString::number(first_pixel_coord)+" (stop) | ";
        qInfo()<<"[LB] Detecting number of bits cut from the right of the line"<<pcm_line->line_number<<"(maximum:"<<max_cut_bits<<")";
    }
#endif
    // Find first bit with different coordinate (searching backwards).
    for(index=0;index<max_cut_bits;index++)
    {
        // Get [i] bit coordinate.
        current_pixel_coord = pcm_line->getVideoPixelT((pcm_line->getBitsBetweenDataCoordinates()-1-index), 0);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords += QString::number(current_pixel_coord)+" (bit "+QString::number((pcm_line->getBitsBetweenDataCoordinates()-1-index))+")";
        }
#endif
        // Check if difference between bit coordinates is more than half of the PPB.
        if((first_pixel_coord-current_pixel_coord)>=((pcm_line->getPPB()+1)/2))
        {
            // Stop search.
            break;
        }
        // Replace line boundary with first "real" pixel coordinate.
        if(index==0)
        {
            first_pixel_coord = current_pixel_coord;
        }
        // Save bit count for cut pixels.
        right_bit_count = (index+1);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords += ", ";
        }
#endif
    }
    // Calculate how many iterations to substitute.
    right_rep_limit = (1<<right_bit_count);
#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        coords = "[LB] Coordinates: "+coords;
        qInfo()<<coords;
        if(right_bit_count>0)
        {
            qInfo()<<"[LB] Calculated to pick"<<right_bit_count<<"rightmost bits through up to"<<(1<<right_bit_count)<<"iterations...";
        }
        else
        {
            qInfo()<<"[LB] Calculated to pick 0 rightmost bits";
        }
    }
#endif

    // Check if Bit Picker is forced on line with valid CRC.
    if((force_bit_picker!=false)&&(pcm_line->isCRCValid()!=false))
    {
        // Bit Picker is forced in the line with already valid CRC.
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"[LB] Bit Picker was forced and line already has valid CRC, setting"<<left_bit_count<<"leftmost and"<<right_bit_count<<"rightmost bits as picked";
        }
#endif
        // Just save calculated number of picked bits and exit.
        // This is used to give more priority to PCM lines with less picked bits
        // when searching for data coordinates.
        pcm_line->picked_bits_left = left_bit_count;
        pcm_line->picked_bits_right = right_bit_count;
        return STG_DATA_OK;
    }
    else
    {
        // Bit Picker was not forced, runs on line with invalid CRC.
        left_clean_word = right_clean_word = left_orig_word = right_orig_word = 0;
        if(left_bit_count>0)
        {
            // Save original state of the word.
            left_orig_word = pcm_line->words[PCM1Line::WORD_L2];
            // Remove "empty" bits.
            left_clean_word = ((left_rep_limit-1)<<(PCM1Line::BITS_PER_WORD-left_bit_count));
            left_clean_word = ~left_clean_word;
            left_clean_word = left_orig_word&left_clean_word;
        }
        if(right_bit_count>0)
        {
            // Save original state of the word.
            right_orig_word = pcm_line->words[PCM1Line::WORD_CRCC];
            // Remove "empty" bits.
            right_clean_word = (right_rep_limit-1);
            right_clean_word = ~right_clean_word;
            right_clean_word = right_orig_word&right_clean_word;
        }

        // Check if any bits can be manipulated.
        if((left_bit_count>0)&&(right_bit_count>0))
        {
            // Brute-force cycle for left word.
            for(index=0;index<left_rep_limit;index++)
            {
                // Brute-force cycle for right word.
                for(idx_in=0;idx_in<right_rep_limit;idx_in++)
                {
                    // Create a patch for a leftmost word.
                    left_patch_word = (index<<(PCM1Line::BITS_PER_WORD-left_bit_count));
                    // Apply patch to the word.
                    pcm_line->words[PCM1Line::WORD_L2] = left_clean_word|left_patch_word;
                    // Create a patch for a rightmost word.
                    right_patch_word = idx_in;
                    // Apply patch to the word.
                    pcm_line->words[PCM1Line::WORD_CRCC] = right_clean_word|right_patch_word;
                    // Re-calc CRC.
                    pcm_line->calcCRC();
                    if(pcm_line->isCRCValid()!=false)
                    {
                        // CRC is now ok, patch found!
                        // Check if "valid" CRC was found before in the cycle.
                        if(patch_found!=false)
                        {
                            // At least two "valid" CRCs found - collision.
                            coll_lock = true;
                            break;
                        }
                        else
                        {
                            // This is the first valid CRC in the cycle.
                            patch_found = true;
                            // Save patch words to apply at the end if no collision will be detected.
                            left_fix_word = left_patch_word;
                            right_fix_word = right_patch_word;
                        }
                    }
                }
                if(coll_lock!=false)
                {
                    break;
                }
            }
            // Check if patch was found.
            if(coll_lock!=false)
            {
                // Found more than one patches: CRC collision detected!
                // Restore original state of the word.
                pcm_line->words[PCM1Line::WORD_L2] = left_orig_word;
                pcm_line->words[PCM1Line::WORD_CRCC] = right_orig_word;
                pcm_line->calcCRC();
                // Force line to be bad.
                pcm_line->setForcedBad();
#ifdef LB_EN_DBG_OUT
                //if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit pick had a CRC collision, failed";
                }
#endif
                return STG_NO_GOOD;
            }
            else if(patch_found==false)
            {
                // Fix was not found and no collision occured.
                // Restore original state of the words.
                pcm_line->words[PCM1Line::WORD_L2] = left_orig_word;
                pcm_line->words[PCM1Line::WORD_CRCC] = right_orig_word;
                pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit pick was unsuccessfull.";
                }
#endif
                return STG_NO_GOOD;
            }
            else
            {
                // Fix successfully found, no collision.
                // Apply patches to the words.
                pcm_line->words[PCM1Line::WORD_L2] = left_clean_word|left_fix_word;
                pcm_line->words[PCM1Line::WORD_CRCC] = right_clean_word|right_fix_word;
                // Re-calc CRC.
                pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                //suppress_log = false;
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Successfull bit pick for both sides (%u/%u bits).", left_bit_count, right_bit_count);
                    qInfo()<<log_line;
                    log_line.sprintf("[LB] Left word: original [0x%04x], cleaned [0x%04x], patch [0x%04x], result [0x%04x]",
                                     left_orig_word, left_clean_word, left_fix_word, pcm_line->words[PCM1Line::WORD_L2]);
                    qInfo()<<log_line;
                    log_line.sprintf("[LB] Right word: original [0x%04x], cleaned [0x%04x], patch [0x%04x], result [0x%04x]",
                                     right_orig_word, right_clean_word, right_fix_word, pcm_line->words[PCM1Line::WORD_CRCC]);
                    qInfo()<<log_line;
                }
#endif
                // Save number of patched bits.
                pcm_line->picked_bits_left = left_bit_count;
                pcm_line->picked_bits_right = right_bit_count;
                return STG_DATA_OK;
            }
        }
        else if(left_bit_count>0)
        {
            // Brute-force cycle only for the left word.
            for(index=0;index<left_rep_limit;index++)
            {
                // Create a patch.
                left_patch_word = (index<<(PCM1Line::BITS_PER_WORD-left_bit_count));
                // Apply patch to the word.
                pcm_line->words[PCM1Line::WORD_L2] = left_clean_word|left_patch_word;
                // Re-check CRC.
                pcm_line->calcCRC();
                if(pcm_line->isCRCValid()!=false)
                {
                    // CRC is now ok, patch found!
                    // Check if "valid" CRC was found before in the cycle.
                    if(patch_found!=false)
                    {
                        // At least two "valid" CRCs found - collision.
                        coll_lock = true;
                        break;
                    }
                    else
                    {
                        // This is the first valid CRC in the cycle.
                        patch_found = true;
                        // Save patch word to apply at the end if no collision will be detected.
                        left_fix_word = left_patch_word;
                    }
                }
            }
            // Check if patch was found.
            if(coll_lock!=false)
            {
                // Found more than one patches: CRC collision detected!
                // Restore original state of the word.
                pcm_line->words[PCM1Line::WORD_L2] = left_orig_word;
                pcm_line->calcCRC();
                // Force sub-line to be bad.
                pcm_line->setForcedBad();
#ifdef LB_EN_DBG_OUT
                //if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit pick had a CRC collision, failed";
                }
#endif
                return STG_NO_GOOD;
            }
            else if(patch_found==false)
            {
                // Fix was not found and no collision occured.
                // Restore original state of the word.
                pcm_line->words[PCM1Line::WORD_L2] = left_orig_word;
                pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit pick was unsuccessfull.";
                }
#endif
                return STG_NO_GOOD;
            }
            else
            {
                // Fix successfully found, no collision.
                // Apply patch to the word.
                pcm_line->words[PCM1Line::WORD_L2] = left_clean_word|left_fix_word;
                // Re-calc CRC.
                pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Successfull bit pick for the left side (%u bits).", left_bit_count);
                    qInfo()<<log_line;
                    log_line.sprintf("[LB] Left word: original [0x%04x], cleaned [0x%04x], patch [0x%04x], result [0x%04x]",
                                     left_orig_word, left_clean_word, left_fix_word, pcm_line->words[PCM1Line::WORD_L2]);
                    qInfo()<<log_line;
                }
#endif
                // Save number of patched bits.
                pcm_line->picked_bits_left = left_bit_count;
                return STG_DATA_OK;
            }
        }
        else if(right_bit_count>0)
        {
            // Brute-force cycle only for the right word.
            for(index=0;index<right_rep_limit;index++)
            {
                // Create a patch.
                right_patch_word = index;
                // Apply patch to the word.
                pcm_line->words[PCM1Line::WORD_CRCC] = right_clean_word|right_patch_word;
                // Re-check CRC.
                pcm_line->calcCRC();
                if(pcm_line->isCRCValid()!=false)
                {
                    // CRC is now ok, patch found!
                    // Check if "valid" CRC was found before in the cycle.
                    if(patch_found!=false)
                    {
                        // At least two "valid" CRCs found - collision.
                        coll_lock = true;
                        break;
                    }
                    else
                    {
                        // This is the first valid CRC in the cycle.
                        patch_found = true;
                        // Save patch word to apply at the end if no collision will be detected.
                        right_fix_word = right_patch_word;
                    }
                }
            }
            // Check if patch was found.
            if(coll_lock!=false)
            {
                // Found more than one patches: CRC collision detected!
                // Restore original state of the word.
                pcm_line->words[PCM1Line::WORD_CRCC] = right_orig_word;
                pcm_line->calcCRC();
                // Force sub-line to be bad.
                pcm_line->setForcedBad();
#ifdef LB_EN_DBG_OUT
                //if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit pick had a CRC collision, failed";
                }
#endif
                return STG_NO_GOOD;
            }
            else if(patch_found==false)
            {
                // Fix was not found and no collision occured.
                // Restore original state of the word.
                pcm_line->words[PCM1Line::WORD_CRCC] = right_orig_word;
                pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit pick was unsuccessfull.";
                }
#endif
                return STG_NO_GOOD;
            }
            else
            {
                // Fix successfully found, no collision.
                // Apply patch to the word.
                pcm_line->words[PCM1Line::WORD_CRCC] = right_clean_word|right_fix_word;
                // Re-calc CRC.
                pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[LB] Successfull bit pick for the right side (%u bits).", right_bit_count);
                    qInfo()<<log_line;
                    log_line.sprintf("[LB] Right word: original [0x%04x], cleaned [0x%04x], patch [0x%04x], result [0x%04x]",
                                     right_orig_word, right_clean_word, right_fix_word, pcm_line->words[PCM1Line::WORD_CRCC]);
                    qInfo()<<log_line;
                }
#endif
                // Save number of patched bits.
                pcm_line->picked_bits_right = right_bit_count;
                return STG_DATA_OK;
            }
        }
        else
        {
            // No repeating coordinates, no cut bits, unable to pick.
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] No cut bits detected, nothing to pick";
            }
#endif
            return STG_NO_GOOD;
        }
    }
}

//------------------------ Bruteforce pick bits that were cut off the line.
uint8_t Binarizer::pickCutBitsUpPCM16X0(PCM16X0SubLine *pcm_line, bool no_log)
{
    bool suppress_log;
    bool patch_found, coll_lock;
    uint8_t max_cut_bits, bit_count;
    uint16_t index;
    uint16_t first_pixel_coord, current_pixel_coord;
    uint16_t orig_word, clean_word, patch_word, fix_word, rep_limit;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&(no_log==false));
    //suppress_log = no_log;

#ifdef LB_EN_DBG_OUT
    QString coords, log_line;
#endif

    // Reset picked bits counters.
    pcm_line->picked_bits_left = 0;
    pcm_line->picked_bits_right = 0;

    patch_found = coll_lock = false;
    bit_count = 0;
    fix_word = 0x00;

    if(line_part_mode==PART_PCM16X0_LEFT)
    {
        // Detect how many bits are missing from the left of the line.
        // Get leftmost bit pixel coordinate.
        first_pixel_coord = scan_start;
        // Determine limit for number of picked bits.
        max_cut_bits = digi_set.left_bit_pick;
        if(bin_mode==MODE_DRAFT)
        {
            // Halve allowed bit count in ultra-fast mode.
            max_cut_bits = max_cut_bits/2;
        }
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords += QString::number(first_pixel_coord)+" (start) | ";
            qInfo()<<"[LB] Starting to pick up bits for left side of the sub-line"<<pcm_line->line_number<<"(maximum:"<<max_cut_bits<<")";
        }
#endif
        // Find first bit with different coordinate.
        for(index=0;index<max_cut_bits;index++)
        {
            // Get [i] bit coordinate.
            current_pixel_coord = pcm_line->getVideoPixelT(index, 0);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                coords += QString::number(current_pixel_coord)+" (bit "+QString::number(index)+")";
            }
#endif
            // Check if difference between bit coordinates is more than half of the PPB.
            if((current_pixel_coord-first_pixel_coord)>=((pcm_line->getPPB()+1)/2))
            {
                // Stop search.
                break;
            }
            // Replace line boundary with first "real" pixel coordinate.
            if(index==0)
            {
                first_pixel_coord = current_pixel_coord;
            }
            // Save bit count for cut pixels.
            bit_count = (index+1);

#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                coords += ", ";
            }
#endif
        }
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords = "[LB] Coordinates: "+coords;
            qInfo()<<coords;
        }
#endif
        // Check if Bit Picker is forced on line with valid CRC.
        if((force_bit_picker!=false)&&(pcm_line->isCRCValid()!=false))
        {
            // Bit Picker is forced in the line with already valid CRC.
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Bit Picker was forced and line already has valid CRC, setting"<<bit_count<<"leftmost bits as picked";
            }
#endif
            // Just save calculated number of picked bits and exit.
            // This is used to give more priority to PCM lines with less picked bits
            // when searching for data coordinates.
            pcm_line->picked_bits_left = bit_count;
            return STG_DATA_OK;
        }
        else
        {
            // Bit Picker was not forced, runs on line with invalid CRC.
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Calculated to pick"<<bit_count<<"leftmost bits";
            }
#endif
            // Check if any bits can be manipulated.
            if(bit_count>0)
            {
                // Save original state of the word.
                orig_word = pcm_line->words[PCM16X0SubLine::WORD_R1P1L1];
                // Calculate how many iterations to substitute.
                rep_limit = (1<<bit_count);
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit picking will take up to"<<rep_limit<<"iterations...";
                }
#endif
                // Remove left bits.
                clean_word = ((rep_limit-1)<<(PCM16X0SubLine::BITS_PER_WORD-bit_count));
                clean_word = ~clean_word;
                clean_word = orig_word&clean_word;
                // Brute-force cycle.
                for(index=0;index<rep_limit;index++)
                {
                    // Create a patch.
                    patch_word = (index<<(PCM16X0SubLine::BITS_PER_WORD-bit_count));
                    // Apply patch to the word.
                    pcm_line->words[PCM16X0SubLine::WORD_R1P1L1] = clean_word|patch_word;
                    // Re-check CRC.
                    pcm_line->calcCRC();
                    if(pcm_line->isCRCValid()!=false)
                    {
                        // CRC is now ok, patch found!
                        // Check if "valid" CRC was found before in the cycle.
                        if(patch_found!=false)
                        {
                            // At least two "valid" CRCs found - collision.
                            coll_lock = true;
                            break;
                        }
                        else
                        {
                            // This is the first valid CRC in the cycle.
                            patch_found = true;
                            // Save patch word to apply at the end if no collision will be detected.
                            fix_word = patch_word;
                        }
                    }
                }
                // Check if patch was found.
                if(coll_lock!=false)
                {
                    // Found more than one patches: CRC collision detected!
                    // Restore original state of the word.
                    pcm_line->words[PCM16X0SubLine::WORD_R1P1L1] = orig_word;
                    pcm_line->calcCRC();
                    // Force sub-line to be bad.
                    pcm_line->setForcedBad();
#ifdef LB_EN_DBG_OUT
                    //if(suppress_log==false)
                    {
                        qInfo()<<"[LB] Bit pick had a CRC collision, failed";
                    }
#endif
                    return STG_NO_GOOD;
                }
                else if(patch_found==false)
                {
                    // Fix was not found and no collision occured.
                    // Restore original state of the word.
                    pcm_line->words[PCM16X0SubLine::WORD_R1P1L1] = orig_word;
                    pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] Bit pick was unsuccessfull.";
                    }
#endif
                    return STG_NO_GOOD;
                }
                else
                {
                    // Fix successfully found, no collision.
                    // Apply patch to the word.
                    pcm_line->words[PCM16X0SubLine::WORD_R1P1L1] = clean_word|fix_word;
                    // Re-calc CRC.
                    pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Successfull bit pick (%u bits): original [0x%04x], cleaned [0x%04x], patch [0x%04x], result [0x%04x]",
                                         bit_count,
                                         orig_word,
                                         clean_word,
                                         fix_word,
                                         pcm_line->words[PCM16X0SubLine::WORD_R1P1L1]);
                        qInfo()<<log_line;
                    }
#endif
                    // Save number of patched bits.
                    pcm_line->picked_bits_left = bit_count;
                    return STG_DATA_OK;
                }
            }
            else
            {
                // No repeating coordinates, no cut bits, unable to pick.
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] No cut bits detected, nothing to pick";
                }
#endif
                return STG_NO_GOOD;
            }
        }
    }
    else if(line_part_mode==PART_PCM16X0_RIGHT)
    {
        // Detect how many bits are missing from the right of the line.
        // Get rightmost bit pixel coordinate.
        first_pixel_coord = scan_end;
        // Determine limit for number of picked bits.
        max_cut_bits = digi_set.right_bit_pick;
        if(bin_mode==MODE_DRAFT)
        {
            // Halve allowed bit count in ultra-fast mode.
            max_cut_bits = max_cut_bits/2;
        }
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords += QString::number(first_pixel_coord)+" (stop) | ";
            qInfo()<<"[LB] Starting to pick up bits for right side of the sub-line"<<pcm_line->line_number<<"(maximum:"<<max_cut_bits<<")";
        }
#endif
        // Find first bit with different coordinate (searching backwards).
        for(index=0;index<max_cut_bits;index++)
        {
            // Get [i] bit coordinate.
            current_pixel_coord = pcm_line->getVideoPixelT((pcm_line->getBitsBetweenDataCoordinates()-1-index), 0);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                coords += QString::number(current_pixel_coord)+" (bit "+QString::number((pcm_line->getBitsBetweenDataCoordinates()-1-index))+")";
            }
#endif
            // Check if difference between bit coordinates is more than half of the PPB.
            if((first_pixel_coord-current_pixel_coord)>=((pcm_line->getPPB()+1)/2))
            {
                // Stop search.
                break;
            }
            // Replace line boundary with first "real" pixel coordinate.
            if(index==0)
            {
                first_pixel_coord = current_pixel_coord;
            }
            // Save bit count for cut pixels.
            bit_count = (index+1);
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                coords += ", ";
            }
#endif
        }
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            coords = "[LB] Coordinates: "+coords;
            qInfo()<<coords;
        }
#endif
        // Check if Bit Picker is forced on line with valid CRC.
        if((force_bit_picker!=false)&&(pcm_line->isCRCValid()!=false))
        {
            // Bit Picker is forced in the line with already valid CRC.
            // Just save calculated number of picked bits and exit.
            // This is used to give more priority to PCM lines with less picked bits
            // when searching for data coordinates.
            pcm_line->picked_bits_right = bit_count;
            return STG_DATA_OK;
        }
        else
        {
            // Bit Picker was not forced, runs on line with invalid CRC.
#ifdef LB_EN_DBG_OUT
            if(suppress_log==false)
            {
                qInfo()<<"[LB] Calculated to pick"<<bit_count<<"rightmost bits";
            }
#endif
            // Check if any bits can be manipulated.
            if(bit_count>0)
            {
                // Save original state of the word.
                orig_word = pcm_line->words[PCM16X0SubLine::WORD_CRCC];
                // Calculate how many iterations to substitute.
                rep_limit = (1<<bit_count);
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] Bit picking will take up to"<<rep_limit<<"iterations...";
                }
#endif
                // Remove right bits.
                clean_word = (rep_limit-1);
                clean_word = ~clean_word;
                clean_word = orig_word&clean_word;
                // Brute-force cycle.
                for(index=0;index<rep_limit;index++)
                {
                    // Create a patch.
                    patch_word = index;
                    // Apply patch to the word.
                    pcm_line->words[PCM16X0SubLine::WORD_CRCC] = clean_word|patch_word;
                    // Re-check CRC.
                    pcm_line->calcCRC();
                    if(pcm_line->isCRCValid()!=false)
                    {
                        // CRC is now ok, patch found!
                        // Check if "valid" CRC was found before in the cycle.
                        if(patch_found!=false)
                        {
                            // At least two "valid" CRCs found - collision.
                            coll_lock = true;
                            break;
                        }
                        else
                        {
                            // This is the first valid CRC in the cycle.
                            patch_found = true;
                            // Save patch word to apply at the end if no collision will be detected.
                            fix_word = patch_word;
                        }
                    }
                }
                // Check if patch was found.
                if(coll_lock!=false)
                {
                    // Found more than one patches: CRC collision detected!
                    // Restore original state of the word.
                    pcm_line->words[PCM16X0SubLine::WORD_CRCC] = orig_word;
                    pcm_line->calcCRC();
                    // Force sub-line to be bad.
                    pcm_line->setForcedBad();
#ifdef LB_EN_DBG_OUT
                    //if(suppress_log==false)
                    {
                        qInfo()<<"[LB] Bit pick had a CRC collision, failed";
                    }
#endif
                    return STG_NO_GOOD;
                }
                else if(patch_found==false)
                {
                    // Fix was not found and no collision occured.
                    // Restore original state of the word.
                    pcm_line->words[PCM16X0SubLine::WORD_CRCC] = orig_word;
                    pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        qInfo()<<"[LB] Bit pick was unsuccessfull.";
                    }
#endif
                    return STG_NO_GOOD;
                }
                else
                {
                    // Fix successfully found, no collision.
                    // Apply patch to the word.
                    pcm_line->words[PCM16X0SubLine::WORD_CRCC] = clean_word|fix_word;
                    // Re-calc CRC.
                    pcm_line->calcCRC();
#ifdef LB_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[LB] Successfull bit pick (%u bits): original [0x%04x], cleaned [0x%04x], patch [0x%04x], result [0x%04x]",
                                         bit_count,
                                         orig_word,
                                         clean_word,
                                         fix_word,
                                         pcm_line->words[PCM16X0SubLine::WORD_CRCC]);
                        qInfo()<<log_line;
                    }
#endif
                    // Save number of patched bits.
                    pcm_line->picked_bits_right = bit_count;
                    return STG_DATA_OK;
                }
            }
            else
            {
                // No repeating coordinates, no cut bits, unable to pick.
#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    qInfo()<<"[LB] No cut bits detected, nothing to pick";
                }
#endif
                return STG_NO_GOOD;
            }
        }
    }
    else
    {
        // No pick for middle part is possible.
        return STG_NO_GOOD;
    }
}

//------------------------ Fill up data words for [PCM1Line] line object.
uint8_t Binarizer::fillPCM1(PCM1Line *fill_pcm_line, uint8_t ref_delta, uint8_t shift_stg, bool no_log)
{
    bool suppress_log, prev_high;
    uint8_t pcm_bit;
    uint8_t pixel_val;
    uint8_t low_ref, high_ref;

    uint8_t word_bit_pos;
    uint8_t word_index;
    uint16_t pcm_word;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&(no_log==false));
    //suppress_log = false;

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if(((log_level&LOG_RAWBIN)!=0)&&(no_log==false))
    {
        log_line.sprintf("[LB] Line [%03u:%03u] PS: [%01u], HL: [%01u], coords: ",
                         fill_pcm_line->frame_number, fill_pcm_line->line_number, shift_stg, ref_delta);
        pcm_word = 0;
        word_bit_pos = 0;
        for(pcm_bit=0;pcm_bit<fill_pcm_line->getBitsBetweenDataCoordinates();pcm_bit++)
        {
            pcm_word++;
            // Find bit coodinate in video line.
            log_line += QString::number(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg))+",";
        }
        log_line += " total: "+QString::number(pcm_word);
        qInfo()<<log_line;
    }
    if(suppress_log==false)
    {
        log_line.sprintf("[LB-%03u] Binarizing PCM-1 with hysteresis depth: %03u, pixel shift stage: %03u",
                         fill_pcm_line->ref_level,
                         ref_delta,
                         shift_stg);
        qInfo()<<log_line;
    }
#endif

    low_ref = fill_pcm_line->ref_low;
    high_ref = fill_pcm_line->ref_high;
    // Reset filling variables.
    prev_high = false;
    pcm_word = 0;
    word_bit_pos = (PCM1Line::BITS_PER_WORD-1);
    word_index = 0;
    pcm_bit = 0;
    // Fill up 13 bit words (and 16 bit CRCC).
    while(pcm_bit<=(PCM1Line::BITS_PCM_DATA-1))
    {
        // Pick the bit.
        pixel_val = getPixelBrightness(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg));

        // Perform binarization with hysteresis.
        if(prev_high==false)
        {
            // Previous bit was "0".
            if(pixel_val>low_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
                prev_high = true;
            }
        }
        else
        {
            // Previous bit was "1".
            if(pixel_val>=high_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
            }
            else
            {
                prev_high = false;
            }
        }

        // Check if the word is complete.
        if(word_bit_pos==0)
        {
            // Save completed word.
            fill_pcm_line->words[word_index] = pcm_word;
            // Reset bits in next word.
            pcm_word = 0;
            // Go to the next word.
            word_index++;
            // Check what number of bits to assemble next.
            if(pcm_bit>(PCM1Line::BITS_PCM_DATA-PCM1Line::BITS_PER_CRC-1))
            {
                // CRC was read, line is finished, exit cycle.
                break;
            }
            else if(pcm_bit==(PCM1Line::BITS_PCM_DATA-PCM1Line::BITS_PER_CRC-1))
            {
                // That was the last 13-bit word, set next word to be 16-bit CRCC.
                word_bit_pos = PCM1Line::BITS_PER_CRC;
            }
            else
            {
                // Word was read, set next 13-bit word.
                word_bit_pos = PCM1Line::BITS_PER_WORD;
            }
        }
        // Shift to the next bit (MSB -> LSB).
        word_bit_pos--;
        pcm_bit++;
    }

    // Calculate new CRCC for the data.
    fill_pcm_line->calcCRC();

    return STG_DATA_OK;
}

//------------------------ Fill up data words for [PCM16X0SubLine] line object.
uint8_t Binarizer::fillPCM16X0(PCM16X0SubLine *fill_pcm_line, uint8_t ref_delta, uint8_t shift_stg, bool no_log)
{
    bool suppress_log, prev_high;
    uint8_t pcm_bit;
    uint8_t pixel_val, bit_count;
    uint8_t start_bit, stop_bit;
    uint8_t low_ref, high_ref;

    uint8_t word_bit_pos;
    uint8_t word_index;
    uint16_t pcm_word;
#ifdef LB_EN_DBG_OUT
    QString log_line;
#endif
    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&(no_log==false));

    // Select part of the video line to binarize.
    if(line_part_mode==PART_PCM16X0_LEFT)
    {
        // Left part.
        start_bit = 0;
        stop_bit = (PCM16X0SubLine::BITS_PCM_DATA-1);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB-%03u] Binarizing left part of the line: bits [%03u|%03u]",
                             fill_pcm_line->ref_level,
                             start_bit,
                             stop_bit);
            qInfo()<<log_line;
        }
#endif
    }
    else if(line_part_mode==PART_PCM16X0_MIDDLE)
    {
        // Middle part.
        start_bit = PCM16X0SubLine::BITS_PCM_DATA;
        stop_bit = ((2*PCM16X0SubLine::BITS_PCM_DATA)-1);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB-%03u] Binarizing middle part of the line: bits [%03u|%03u]",
                             fill_pcm_line->ref_level,
                             start_bit,
                             stop_bit);
            qInfo()<<log_line;
        }
#endif
    }
    else if(line_part_mode==PART_PCM16X0_RIGHT)
    {
        // Right part.
        start_bit = ((2*PCM16X0SubLine::BITS_PCM_DATA)+1);  // Shift right by the service bit.
        stop_bit = (3*PCM16X0SubLine::BITS_PCM_DATA);
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB-%03u] Binarizing right part of the line: bits [%03u|%03u]",
                             fill_pcm_line->ref_level,
                             start_bit,
                             stop_bit);
            qInfo()<<log_line;
        }
#endif
    }
    else
    {
        // Unknown line part.
        return STG_NO_GOOD;
    }

#ifdef LB_EN_DBG_OUT
    if(((log_level&LOG_RAWBIN)!=0)&&(no_log==false))
    {
        log_line.sprintf("[LB] Line [%03u:%03u] PS: [%01u], HL: [%01u], coords: ",
                         fill_pcm_line->frame_number, fill_pcm_line->line_number, shift_stg, ref_delta);
        pcm_word = 0;
        word_bit_pos = 0;
        for(uint8_t pcm_bit=start_bit;pcm_bit<=stop_bit;pcm_bit++)
        {
            pcm_word++;
            // Find bit coodinate in video line.
            log_line += QString::number(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg))+",";
        }
        log_line += " total: "+QString::number(pcm_word);
        qInfo()<<log_line;
    }
    if(suppress_log==false)
    {
        log_line.sprintf("[LB-%03u] Binarizing PCM-16x0 with hysteresis depth: %03u, pixel shift stage: %03u",
                         fill_pcm_line->ref_level,
                         ref_delta,
                         shift_stg);
        qInfo()<<log_line;
    }
#endif

    low_ref = fill_pcm_line->ref_low;
    high_ref = fill_pcm_line->ref_high;
    // Reset filling variables.
    prev_high = false;
    bit_count = 0;
    pcm_word = 0;
    word_bit_pos = (PCM16X0SubLine::BITS_PER_WORD-1);
    word_index = 0;
    pcm_bit = start_bit;
    // Fill up 16 bit words (and 16 bit CRCC).
    while(pcm_bit<=stop_bit)
    {
        // Pick the bit.
        pixel_val = getPixelBrightness(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg));

        // Perform binarization with hysteresis.
        if(prev_high==false)
        {
            // Previous bit was "0".
            if(pixel_val>low_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
                prev_high = true;
            }
        }
        else
        {
            // Previous bit was "1".
            if(pixel_val>=high_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
            }
            else
            {
                prev_high = false;
            }
        }

        // Check if the word is complete.
        if(word_bit_pos==0)
        {
            // Save completed word.
            fill_pcm_line->words[word_index] = pcm_word;
            // Reset bits in next word.
            pcm_word = 0;
            // Go to the next word.
            word_index++;
            // Check what number of bits to assemble next.
            if(bit_count>(PCM16X0SubLine::BITS_PCM_DATA-PCM16X0SubLine::BITS_PER_CRC-1))
            {
                // CRC was read, line is finished, exit cycle.
                break;
            }
            else if(bit_count==(PCM16X0SubLine::BITS_PCM_DATA-PCM16X0SubLine::BITS_PER_CRC-1))
            {
                // That was the last 16-bit word, set next word to be 16-bit CRCC.
                word_bit_pos = PCM16X0SubLine::BITS_PER_CRC;
            }
            else
            {
                // Word was read, set next 16-bit word.
                word_bit_pos = PCM16X0SubLine::BITS_PER_WORD;
            }
        }
        // Shift to the next bit (MSB -> LSB).
        word_bit_pos--;
        bit_count++;
        pcm_bit++;
    }

    // Calculate new CRCC for the data.
    fill_pcm_line->calcCRC();

    fill_pcm_line->control_bit = true;
    if(fill_pcm_line->isCRCValid()!=false)
    {
        // Pick skew bit.
        // Control Bit is "1" by default.
        pixel_val = getPixelBrightness(fill_pcm_line->getVideoPixelT((2*PCM16X0SubLine::BITS_PCM_DATA), shift_stg));
        if(pixel_val<fill_pcm_line->ref_level)
        {
            fill_pcm_line->control_bit = false;
        }
    }

    return STG_DATA_OK;
}

//------------------------ Fill up data words for [STC007Line] line object.
uint8_t Binarizer::fillSTC007(STC007Line *fill_pcm_line, uint8_t ref_delta, uint8_t shift_stg, bool no_log)
{
    bool suppress_log, prev_high;
    uint8_t pcm_bit;
    uint8_t pixel_val;
    uint8_t low_ref, high_ref;

    uint8_t word_bit_pos;
    uint8_t word_index;
    uint16_t pcm_word;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&(no_log==false));

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if(((log_level&LOG_RAWBIN)!=0)&&(no_log==false))
    {
        log_line.sprintf("[LB] Line [%03u:%03u] PS: [%01u], HL: [%01u], coords: ",
                         fill_pcm_line->frame_number, fill_pcm_line->line_number, shift_stg, ref_delta);
        pcm_word = 0;
        word_bit_pos = 0;
        for(uint8_t pcm_bit=0;pcm_bit<=(STC007Line::BITS_PCM_DATA-1);pcm_bit++)
        {
            pcm_word++;
            // Find bit coodinate in video line.
            log_line += QString::number(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg))+",";
        }
        log_line += " total: "+QString::number(pcm_word);
        qInfo()<<log_line;
    }
    if(suppress_log==false)
    {
        log_line.sprintf("[LB-%03u] Binarizing STC-007 with hysteresis depth: %03u, pixel shift stage: %03u",
                         fill_pcm_line->ref_level,
                         ref_delta,
                         shift_stg);
        qInfo()<<log_line;
    }
#endif

    low_ref = fill_pcm_line->ref_low;
    high_ref = fill_pcm_line->ref_high;
    // Reset filling variables.
    prev_high = false;
    pcm_word = 0;
    word_bit_pos = (STC007Line::BITS_PER_WORD-1);
    word_index = 0;
    pcm_bit = 0;
    // Fill up 14 bit words (and 16 bit CRCC).
    while(pcm_bit<=(STC007Line::BITS_PCM_DATA-1))
    {
        // Pick the bit.
        pixel_val = getPixelBrightness(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg));

        // Perform binarization with hysteresis.
        if(prev_high==false)
        {
            // Previous bit was "0".
            if(pixel_val>low_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
                prev_high = true;
            }
        }
        else
        {
            // Previous bit was "1".
            if(pixel_val>=high_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
            }
            else
            {
                prev_high = false;
            }
        }

        // Check if the word is complete.
        if(word_bit_pos==0)
        {
            // Save completed word.
            if(pcm_bit>(STC007Line::BITS_PCM_DATA-STC007Line::BITS_PER_CRC-1))
            {
                // Save 16-bit CRC.
                fill_pcm_line->setSourceCRC(pcm_word);
            }
            else
            {
                // Save 14-bit word.
                fill_pcm_line->setWord(word_index, pcm_word);
            }
            // Reset bits in next word.
            pcm_word = 0;
            // Go to the next word.
            word_index++;
            // Check what number of bits to assemble next.
            if(pcm_bit>(STC007Line::BITS_PCM_DATA-STC007Line::BITS_PER_CRC-1))
            {
                // CRC was read, line is finished, exit cycle.
                break;
            }
            else if(pcm_bit==(STC007Line::BITS_PCM_DATA-STC007Line::BITS_PER_CRC-1))
            {
                // That was the last 14-bit word, set next word to be 16-bit CRCC.
                word_bit_pos = STC007Line::BITS_PER_CRC;
            }
            else
            {
                // Word was read, set next 14-bit word.
                word_bit_pos = STC007Line::BITS_PER_WORD;
            }
        }
        // Shift to the next bit (MSB -> LSB).
        word_bit_pos--;
        pcm_bit++;
    }

    // Calculate new CRCC for the data.
    fill_pcm_line->calcCRC();

    return STG_DATA_OK;
}

//------------------------ Fill up data words for [ArVidLine] line object.
uint8_t Binarizer::fillArVidAudio(ArVidLine *fill_pcm_line, uint8_t ref_delta, uint8_t shift_stg, bool no_log)
{
    bool suppress_log, prev_high;
    uint8_t pcm_bit;
    uint8_t pixel_val;
    uint8_t low_ref, high_ref;

    uint8_t word_bit_pos;
    uint8_t word_index;
    uint16_t pcm_word;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&(no_log==false));
    //suppress_log = false;

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if(((log_level&LOG_RAWBIN)!=0)&&(no_log==false))
    {
        log_line.sprintf("[LB] Line [%03u:%03u] PS: [%01u], HL: [%01u], coords: ",
                         fill_pcm_line->frame_number, fill_pcm_line->line_number, shift_stg, ref_delta);
        pcm_word = 0;
        word_bit_pos = 0;
        for(pcm_bit=0;pcm_bit<fill_pcm_line->getBitsBetweenDataCoordinates();pcm_bit++)
        {
            pcm_word++;
            // Find bit coodinate in video line.
            log_line += QString::number(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg))+",";
        }
        log_line += " total: "+QString::number(pcm_word);
        qInfo()<<log_line;
    }
    if(suppress_log==false)
    {
        log_line.sprintf("[LB-%03u] Binarizing ArVid Audio with hysteresis depth: %03u, pixel shift stage: %03u",
                         fill_pcm_line->ref_level,
                         ref_delta,
                         shift_stg);
        qInfo()<<log_line;
    }
#endif

    low_ref = fill_pcm_line->ref_low;
    high_ref = fill_pcm_line->ref_high;
    // Reset filling variables.
    prev_high = false;
    pcm_word = 0;
    word_bit_pos = (ArVidLine::BITS_PER_WORD-1);
    word_index = 0;
    pcm_bit = 0;
    // Fill up words.
    while(pcm_bit<=(ArVidLine::BITS_PCM_DATA-1))
    {
        // Pick the bit.
        pixel_val = getPixelBrightness(fill_pcm_line->getVideoPixelT(pcm_bit, shift_stg));

        // Perform binarization with hysteresis.
        if(prev_high==false)
        {
            // Previous bit was "0".
            if(pixel_val>low_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
                prev_high = true;
            }
        }
        else
        {
            // Previous bit was "1".
            if(pixel_val>=high_ref)
            {
                // Set the bit if presents at current bit position.
                pcm_word |= (1<<word_bit_pos);
            }
            else
            {
                prev_high = false;
            }
        }

        // Check if the word is complete.
        if(word_bit_pos==0)
        {
            // Save completed word.
            fill_pcm_line->words[word_index] = pcm_word;
            // Reset bits in next word.
            pcm_word = 0;
            // Go to the next word.
            word_index++;
            // Check what number of bits to assemble next.
            if(pcm_bit>(ArVidLine::BITS_PCM_DATA-ArVidLine::BITS_PER_WORD-1))
            {
                // CRC was read, line is finished, exit cycle.
                break;
            }
            {
                // Word was read, set next word.
                word_bit_pos = ArVidLine::BITS_PER_WORD;
            }
        }
        // Shift to the next bit (MSB -> LSB).
        word_bit_pos--;
        pcm_bit++;
    }

    // Calculate new CRCC for the data.
    fill_pcm_line->calcCRC();

    return STG_DATA_OK;
}

//------------------------ Fill up data words, recalculate CRC.
uint8_t Binarizer::fillDataWords(PCMLine *fill_pcm_line, uint8_t ref_delta, uint8_t shift_stg, bool no_log)
{
    uint8_t low_ref, high_ref;
    bool suppress_log;

    if(ref_delta>HYST_DEPTH_MAX)
    {
#ifdef LB_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[LB] Hysteresis level out-of-bounds with level:"<<ref_delta;
#endif
        return STG_NO_GOOD;
    }
    if(shift_stg>SHIFT_STAGES_MAX)
    {
#ifdef LB_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[LB] Pixel-shifting stages out-of-bounds with stage:"<<shift_stg;
#endif
        return STG_NO_GOOD;
    }

    suppress_log = !(((log_level&LOG_PROCESS)!=0)&&(no_log==false));
    //no_log = false;

    // Calculate LOW and HIGH reference levels for hysteresis.
    low_ref = getLowLevel(fill_pcm_line->ref_level, ref_delta);
    high_ref = getHighLevel(fill_pcm_line->ref_level, ref_delta);
    // Store actual reference levels.
    fill_pcm_line->ref_low = low_ref;
    fill_pcm_line->ref_high = high_ref;
    // Check if levels are within limits.
    if(low_ref<=(fill_pcm_line->black_level))
    {
        // Ref. levels clipping detected, force BAD CRC and exit.
        fill_pcm_line->setInvalidCRC();
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[LB-%03u] Too low ref. within hysteresis! [%03u<=%03u]",
                             fill_pcm_line->ref_level,
                             low_ref,
                             fill_pcm_line->black_level);
            qInfo()<<log_line;
        }
#endif
        return STG_NO_GOOD;
    }
    if(high_ref>=(fill_pcm_line->white_level))
    {
        // Ref. levels clipping detected, force BAD CRC and exit.
        fill_pcm_line->setInvalidCRC();
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            QString log_line;
            log_line.sprintf("[LB-%03u] Too high ref. within hysteresis! [%03u>=%03u]",
                             fill_pcm_line->ref_level,
                             high_ref,
                             fill_pcm_line->white_level);
            qInfo()<<log_line;
        }
#endif
        return STG_NO_GOOD;
    }

    // Store last hysteresis depth in PCM line.
    fill_pcm_line->hysteresis_depth = ref_delta;
    // Store last shift stage in PCM line.
    fill_pcm_line->shift_stage = shift_stg;

    if(fill_pcm_line->getPCMType()==PCMLine::TYPE_PCM1)
    {
        uint8_t bin_res;
        PCM1Line *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<PCM1Line *>(fill_pcm_line);
        //bin_res = STG_NO_GOOD;
        bin_res = fillPCM1(temp_pcm_ptr, ref_delta, shift_stg, no_log);
        if(bin_res==STG_DATA_OK)
        {
            // Fill was performed ok.
            // Check if CRC is ok.
            if(((temp_pcm_ptr->isCRCValid()==false)&&(temp_pcm_ptr->ref_level>digi_set.min_white_lvl)
                &&((digi_set.left_bit_pick!=0)||(digi_set.right_bit_pick!=0)))||(force_bit_picker!=false))
            {
                // Allow bit-picking only if reference level is high enough and Bit Picker is enabled.
                // Try to brute-force cut left and/or right pixels/bits with bit-picker.
                pickCutBitsUpPCM1(temp_pcm_ptr, no_log);
            }
            return STG_DATA_OK;
        }
        else
        {
            // Error occured while filling data.
            return bin_res;
        }
    }
    else if(fill_pcm_line->getPCMType()==PCMLine::TYPE_PCM16X0)
    {
        uint8_t bin_res;
        PCM16X0SubLine *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<PCM16X0SubLine *>(fill_pcm_line);
        bin_res = fillPCM16X0(temp_pcm_ptr, ref_delta, shift_stg, no_log);
        if(bin_res==STG_DATA_OK)
        {
            // Fill was performed ok.
            // Check if CRC is ok.
            if(((temp_pcm_ptr->isCRCValid()==false)&&(temp_pcm_ptr->ref_level>digi_set.min_white_lvl)
                &&((digi_set.left_bit_pick!=0)||(digi_set.right_bit_pick!=0)))||(force_bit_picker!=false))
            {
                // Allow bit-picking only if reference level is high enough and Bit Picker is enabled.
                // Try to brute-force cut left or right pixels/bits with bit-picker.
                pickCutBitsUpPCM16X0(temp_pcm_ptr, no_log);
            }
            return STG_DATA_OK;
        }
        else
        {
            // Error occured while filling data.
            return bin_res;
        }
    }
    else if(fill_pcm_line->getPCMType()==PCMLine::TYPE_STC007)
    {
        STC007Line *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<STC007Line *>(fill_pcm_line);
        return fillSTC007(temp_pcm_ptr, ref_delta, shift_stg, no_log);
    }
    else if(fill_pcm_line->getPCMType()==PCMLine::TYPE_ARVA)
    {
        ArVidLine *temp_pcm_ptr;
        temp_pcm_ptr = static_cast<ArVidLine *>(fill_pcm_line);
        return fillArVidAudio(temp_pcm_ptr, ref_delta, shift_stg, no_log);
    }
    else
    {
        // Unsupported PCM type.
#ifdef LB_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[LB] Unable to fill data for unknown PCM type";
#endif
        return STG_NO_GOOD;
    }
}

//------------------------ Calculate PPB, perform binarization and data fill-up,
//------------------------ calculate and compare CRCC, perform pixel-shift if CRCC failes (if enabled).
void Binarizer::readPCMdata(PCMLine *fill_pcm_line, uint8_t thread_id, bool no_log)
{
    bool suppress_log, invalid_hyst;
    uint8_t hyst_cnt, shift_try_cnt, valid_crcs_hyst, valid_crcs_shift, hyst_good_cnt;
    uint8_t valid_delta, valid_shift, invalid_shift;

    // Calculate PPB, perform binarization and data fill-up,
    // calculate and compare CRCC, perform pixel-shift if CRCC fails (if enabled).
    suppress_log = ((log_level&LOG_PROCESS)==0)||(no_log!=false);
    //suppress_log = false;

#ifdef LB_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[LB] ---------- Calculating PPB and reading bits...";
    }
#endif

    // Calculate PPB (number of Pixels Per Bit in the video line) and bit coordinates.
    fill_pcm_line->calcPPB(fill_pcm_line->coords);

#ifdef LB_EN_DBG_OUT
    QString log_line;
    if(suppress_log==false)
    {
        log_line.sprintf("[LB-%02u-%03u] Calculated PPB: %u.%u (data at [%03d:%04d])",
                         thread_id,
                         fill_pcm_line->ref_level,
                         fill_pcm_line->getPPB(),
                         fill_pcm_line->getPPBfrac(),
                         fill_pcm_line->coords.data_start,
                         fill_pcm_line->coords.data_stop);
        qInfo()<<log_line;
    }
#endif

    // Put limits into limits.
    if(hysteresis_depth_lim>HYST_DEPTH_MAX)
    {
        hysteresis_depth_lim = HYST_DEPTH_MAX;
    }
    if(shift_stages_lim>SHIFT_STAGES_MAX)
    {
        shift_stages_lim = SHIFT_STAGES_MAX;
    }

    if(fill_pcm_line->isDataByRefSweep()==false)
    {
        // Reference sweep was not performed on the line, "good" hysteresis depth and pixel-shifting are not set yet.
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB-%02u-%03u] Binarizing with max. hysteresis depth: %02u, max. shifting stage: %02u",
                             thread_id,
                             fill_pcm_line->ref_level,
                             hysteresis_depth_lim,
                             shift_stages_lim);
            qInfo()<<log_line;
        }
#endif

        //#pragma omp parallel

        // Preset bad CRC result for all hysteresis depths.
        hyst_cnt = (hysteresis_depth_lim+1);
        while(hyst_cnt>0)
        {
            hyst_cnt--;
            // Preset "bad CRC" result.
            hyst_crcs[hyst_cnt].result = REF_BAD_CRC;
        }
        valid_delta = hyst_good_cnt = 0;
        hyst_cnt = 0;
        // Hysteresis sweep cycle.
        do
        {
            invalid_hyst = false;
            // Reset CRC stats for pixel-shifting.
            resetCRCStats(crc_stats, (MAX_COLL_CRCS+1), &valid_crcs_shift);
            crc_stats[0].hyst_dph = 0;
            crc_stats[0].shift_stg = 0;

            valid_shift = invalid_shift = SHIFT_STAGES_MAX;
            // Preset bad CRC result for all pixel-shifts.
            shift_try_cnt = (shift_stages_lim+1);
            while(shift_try_cnt>0)
            {
                shift_try_cnt--;
                // Preset "bad CRC" result.
                shift_crcs[shift_try_cnt].result = REF_BAD_CRC;
            }
            // Pixel offset retry cycle.
            shift_try_cnt = 0;
            do
            {
                // Keep trying shifting bit coordinates until CRC is good.
                shift_crcs[shift_try_cnt].hyst_dph = hyst_cnt;
                // Save pixel-shifting stage.
                shift_crcs[shift_try_cnt].shift_stg = shift_try_cnt;
#ifdef LB_EN_DBG_OUT
                if((log_level&LOG_READING)!=0)
                {
                    log_line.sprintf("[LB-%02u-%03u] Reading with hysteresis [%02u], pixel-shifting stage [%02u]",
                                     thread_id,
                                     fill_pcm_line->ref_level,
                                     hyst_cnt,
                                     shift_try_cnt);
                    qInfo()<<log_line;
                }
#endif
                // Binarize the video line, fill up data words, calculate CRC.
                if(fillDataWords(fill_pcm_line, hyst_cnt, shift_try_cnt, true)!=STG_DATA_OK)
                {
                    // Data read produced an error.
                    // Hysteresis pulled reference beyond BLACK or WHITE levels.
                    invalid_hyst = true;
#ifdef LB_EN_DBG_OUT
                    if((log_level&LOG_READING)!=0)
                    {
                        log_line.sprintf("[LB-%02u-%03u] Read failed, probably bad reference levels",
                                         thread_id,
                                         fill_pcm_line->ref_level);
                        qInfo()<<log_line;
                    }
#endif
                    // Exit pixel-shifting cycle.
                    break;
                }
                else
                {
                    // Save CRC value.
                    shift_crcs[shift_try_cnt].crc = fill_pcm_line->getCalculatedCRC();
                    if(fill_pcm_line->isCRCValid()!=false)
                    {
                        // Save "good" result.
                        shift_crcs[shift_try_cnt].result = REF_CRC_OK;
                        // Update CRC stats, get number of valid CRCs.
                        updateCRCStats(crc_stats, shift_crcs[shift_try_cnt], &valid_crcs_shift);
#ifdef LB_EN_DBG_OUT
                        if((log_level&LOG_READING)!=0)
                        {
                            log_line.sprintf("[LB-%02u-%03u] Valid CRC: 0x%04x",
                                             thread_id,
                                             fill_pcm_line->ref_level,
                                             fill_pcm_line->getCalculatedCRC());
                            qInfo()<<log_line;
                        }
#endif
                        // Valid result is ready, no need to shift coordinates further.
                        break;
                    }
#ifdef LB_EN_DBG_OUT
                    else
                    {
                        if((log_level&LOG_READING)!=0)
                        {
                            log_line.sprintf("[LB-%02u-%03u] Invalid CRC: 0x%04x != 0x%04x",
                                             thread_id,
                                             fill_pcm_line->ref_level,
                                             fill_pcm_line->getCalculatedCRC(),
                                             fill_pcm_line->getSourceCRC());
                            qInfo()<<log_line;
                        }
                    }
#endif
                }
                // Proceed with next pixel-shifting stage.
                shift_try_cnt++;
            }
            while(shift_try_cnt<=shift_stages_lim);     // Pixel-shifting cycle end.

#ifdef LB_EN_DBG_OUT
            if((log_level&LOG_READING)!=0)
            {
                QString crcs;
                for(uint8_t i=1;i<=valid_crcs_shift;i++)
                {
                    crcs += "0x"+QString::number(crc_stats[i].crc, 16)+",";
                }
                log_line.sprintf("[LB-%02u-%03u] Pixel-shifting yelded [%u] good CRCs:",
                                 thread_id,
                                 fill_pcm_line->ref_level,
                                 valid_crcs_shift);
                if(valid_crcs_shift==0)
                {
                    qInfo()<<log_line;
                }
                else
                {
                    qInfo()<<log_line<<crcs;
                }
            }
#endif
            // Check if any good CRC were encountered at current hysteresis depth.
            if(valid_crcs_shift>0)
            {
                // Find the most frequent CRCs (or invalidate all of those).
                findMostFrequentCRC(crc_stats, &valid_crcs_shift, true, no_log);

#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    if(valid_crcs_shift==0)
                    {
                        qInfo()<<"[LB] No valid CRCs after pixel-shifting left...";
                    }
                }
#endif
                // Invalidate not frequent enough CRCs.
                invalidateNonFrequentCRCs(shift_crcs, 0, shift_stages_lim, valid_crcs_shift, crc_stats[0].crc, no_log);
            }

            // Update hysteresis stats.
            // Save pixel-shifting stage that produced a valid CRC (if any).
            hyst_crcs[hyst_cnt].shift_stg = crc_stats[0].shift_stg;
            // Save CRC value of that stage.
            hyst_crcs[hyst_cnt].crc = crc_stats[0].crc;
            // Check pixel-shifting results.
            if(valid_crcs_shift>0)
            {
                // Pixel-shifting yelded good result.
                // Save data.
                hyst_crcs[hyst_cnt].hyst_dph = crc_stats[0].hyst_dph;
                hyst_crcs[hyst_cnt].result = REF_CRC_OK;
                // Increase number of good hysteresis runs.
                hyst_good_cnt++;
                break;
                // Check if there were enough good runs.
                /*if(hyst_good_cnt>=HYST_MIN_GOOD_SPAN)
                {
                    // Count current hysteresis run as valid before exiting the cycle.
                    hyst_cnt++;
                    // No need to do more hysteresis runs.
                    break;
                }*/
            }
            else
            {
                // Pixel-shifting produced bad result.
                // "Bad" result was already preset before the cycle.
                hyst_crcs[hyst_cnt].hyst_dph = hyst_cnt;
                // Check if good CRCs were encountered before.
                if(hyst_good_cnt>0)
                {
                    // Count current hysteresis run as valid before exiting the cycle.
                    //hyst_cnt++;
                    // This is the end of good region, no need to check other hysteresis values.
                    break;
                }
            }
            // Check binarization state.
            if(invalid_hyst!=false)
            {
                // Invalid hysteresis depth, no need to increase it further.
                break;
            }
            // Next hysteresis iteration.
            hyst_cnt++;
        }
        while(hyst_cnt<=hysteresis_depth_lim);

        // Reset CRC stats for hysteresis depth sweep.
        resetCRCStats(crc_stats, MAX_COLL_CRCS, &valid_crcs_hyst);
        crc_stats[0].hyst_dph = 0;
        crc_stats[0].shift_stg = 0;

        // Check if any good result was found.
        if(hyst_good_cnt>0)
        {
            // Update CRC list for hysteresis sweep.
            for(uint8_t i=0;i<=hyst_cnt;i++)
            {
                if(hyst_crcs[i].result==REF_CRC_OK)
                {
                    // Update CRC stats.
                    updateCRCStats(crc_stats, hyst_crcs[i], &valid_crcs_hyst);
                }
            }

#ifdef LB_EN_DBG_OUT
            if((log_level&LOG_READING)!=0)
            {
                QString crcs;
                for(uint8_t i=1;i<=valid_crcs_hyst;i++)
                {
                    crcs += "0x"+QString::number(crc_stats[i].crc, 16)+",";
                }
                log_line.sprintf("[LB-%02u-%03u] Hysteresis sweeping yelded [%u] good CRCs:",
                                 thread_id,
                                 fill_pcm_line->ref_level,
                                 valid_crcs_hyst);
                qInfo()<<log_line<<crcs;
            }
#endif
            if(valid_crcs_hyst>0)
            {
                // Find the most frequent CRCs (or invalidate all of those).
                findMostFrequentCRC(crc_stats, &valid_crcs_hyst, true, no_log);

#ifdef LB_EN_DBG_OUT
                if(suppress_log==false)
                {
                    if(valid_crcs_hyst==0)
                    {
                        qInfo()<<"[LB] No valid CRCs after hysteresis sweep left...";
                    }
                }
#endif

                // Invalidate not frequent enough CRCs.
                invalidateNonFrequentCRCs(hyst_crcs, 0, (hyst_cnt+1), valid_crcs_hyst, crc_stats[0].crc, no_log);
            }
        }

        // Set valid parameters from CRC stat.
        valid_delta = crc_stats[0].hyst_dph;
        valid_shift = crc_stats[0].shift_stg;

#ifdef LB_EN_DBG_OUT
        if((log_level&LOG_READING)!=0)
        {
            if(hysteresis_depth_lim>0)
            {
                log_line = "Hysteresis sweeping results: "+QString::number(valid_crcs_hyst)+" unique CRCs, sweep: ";
                for(uint8_t i=0;i<=hyst_cnt;i++)
                {
                    if(hyst_crcs[i].result==REF_CRC_OK)
                    {
                        log_line += QString::number(hyst_crcs[i].shift_stg);
                    }
                    else if(hyst_crcs[i].result==REF_CRC_COLL)
                    {
                        log_line += "@";
                    }
                    else
                    {
                        log_line += ".";
                    }
                }
                qInfo()<<log_line;
            }
        }
#endif
    }
    else
    {
        // Preset stages as found in reference level sweeping.
        valid_delta = hysteresis_depth_lim;
        valid_shift = shift_stages_lim;
#ifdef LB_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[LB] Skipping final hysteresis sweeping and pixel-shifting, using HD[%u], SS[%u]", valid_delta, valid_shift);
            qInfo()<<log_line;
        }
#endif
    }

    // Final read with determined parameters.
    fillDataWords(fill_pcm_line, valid_delta, valid_shift, no_log);
}
