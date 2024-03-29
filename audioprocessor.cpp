﻿#include "audioprocessor.h"

AudioProcessor::AudioProcessor(QObject *parent) : QObject(parent)
{
    in_samples = NULL;
    mtx_samples = NULL;
    tim_outflush = tim_vu_fade = NULL;
    prebuffer.clear();
    for(uint8_t idx=0;idx<CHANNEL_CNT;idx++)
    {
        channel_bufs[idx].clear();
        long_bads[idx].clear();
    }
    log_level = 0;
    mask_mode = DROP_IGNORE;
    min_valid_before = MIN_VALID_BEFORE;
    max_ramp_down = MAX_RAMP_DOWN;
    max_ramp_up = MAX_RAMP_UP;
    sample_index = 0;
    setSampleRate(PCMSamplePair::SAMPLE_RATE_44100);
    vu_left = vu_right = 0;
    file_end = false;
    unprocessed = false;
    remove_stray = false;
    out_to_wav = false;
    out_to_live = false;
    dbg_output = false;
    //dbg_output = true;
    finish_work = false;
    sc_output.setParent(this);

    connect(this, SIGNAL(newSource()), &sc_output, SLOT(prepareNewFile()));
    connect(&sc_output, SIGNAL(livePlayback(bool)), this, SLOT(livePlayUpdate(bool)));
    connect(&wav_output, SIGNAL(fileError(QString)), this, SLOT(redirectError(QString)));
    connect(this, SIGNAL(reqTimerRestart()), this, SLOT(restartFlushTimer()));
}

//------------------------ Set pointer to shared input/output data.
void AudioProcessor::setInputPointers(std::deque<PCMSamplePair> *in_samplepairs, QMutex *mtx_samplepairs)
{
    if((in_samplepairs==NULL)||(mtx_samplepairs==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Empty input pointer provided, unable to apply!";
    }
    else
    {
        in_samples = in_samplepairs;
        mtx_samples = mtx_samplepairs;
    }
}

//------------------------ Set audio sample rate.
void AudioProcessor::setSampleRate(uint16_t in_rate)
{
    if(sample_rate!=in_rate)
    {
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[AP] Sample rate set to"<<in_rate;
        }
#endif
    }
    sample_rate = in_rate;
    wav_output.setSampleRate(sample_rate);
    sc_output.setSampleRate(sample_rate);
}

//------------------------ Fill internal buffer until its full.
bool AudioProcessor::fillUntilBufferFull()
{
    size_t queue_size, added, buf_limit;
    PCMSamplePair work_pair;

    added = 0;

    if(file_end!=false)
    {
        // Prevent adding data from the next file until "file ended" flag is cleared.
        return false;
    }

    // Lock shared access.
    mtx_samples->lock();
    // Get the size of input queue.
    queue_size = in_samples->size();
    // Calculate minimum allowed buffer size;
    buf_limit = min_valid_before+max_ramp_down+max_ramp_up+1;
    if(buf_limit<BUF_SIZE)
    {
        // Extend buffer size if allowed.
        buf_limit = BUF_SIZE;
    }
#ifdef AP_EN_DBG_OUT
    if(((log_level&LOG_FILL)!=0)||((log_level&LOG_BUF_DUMP)!=0))
    {
        QString log_line;
        if((queue_size>0)&&(prebuffer.size()<buf_limit))
        {
            log_line.sprintf("[AP] Filling internal buffer ([%u/%u] occupied) from input ([%u] pending)",
                             prebuffer.size(), buf_limit, queue_size);
            qInfo()<<log_line;
        }
    }
#endif
    // Check if there is anything in the input
    // and there is space in the buffer.
    while((queue_size>0)&&(prebuffer.size()<buf_limit))
    {
        // Pick first data point from the input.
        work_pair = in_samples->front();
        in_samples->pop_front();
        queue_size = in_samples->size();
        added++;

        // Check if it has service tag.
        if(work_pair.isServicePair()!=false)
        {
            // Service tag is present.
            if(work_pair.isServNewFile()!=false)
            {
                // New file just started.
#ifdef AP_EN_DBG_OUT
                if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FILE_OP)!=0)||((log_level&LOG_LIVE_OP)!=0))
                {
                    qInfo()<<"[AP] Detected new source file:"<<QString::fromStdString(work_pair.file_path);
                }
#endif
                // Dump everything that's in the buffer from previous file to the output.
                purgePipeline();
                // Get file name of the new file.
                QFileInfo source_file(QString::fromStdString(work_pair.file_path));
                // Set target path and filename for output.
                setFolder(source_file.absolutePath());
                setFileName(source_file.completeBaseName());
                file_end = false;
            }
            else if(work_pair.isServEndFile()!=false)
            {
                // File ended.
#ifdef AP_EN_DBG_OUT
                if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FILE_OP)!=0)||((log_level&LOG_LIVE_OP)!=0))
                {
                    qInfo()<<"[AP] EOF detected";
                }
#endif
                // Set "file ended" flag.
                file_end = true;
                //log_level |= LOG_DROP_ACT|LOG_BUF_DUMP;
                // Stop adding data points until all samples from ended file are processed and output.
                break;
            }
        }
        else
        {
            // Normal audio data
            // Set index for the samples.
            work_pair.setIndex(sample_index);
            // Check for debug output (both channels have audio from the left channel).
            if(dbg_output!=false)
            {
                // Copy left channel data into the right channel.
                work_pair.samples[PCMSamplePair::CH_RIGHT] = work_pair.samples[PCMSamplePair::CH_LEFT];
            }
            // Check if masking should by done by data block validity.
            if((mask_mode==DROP_MUTE_BLOCK)||(mask_mode==DROP_HOLD_BLOCK)||(mask_mode==DROP_INTER_LIN_BLOCK))
            {
                work_pair.setValidityByBlock();
            }
            // Put data into internal buffer.
            prebuffer.push_back(work_pair);
            // Increase internal sample pair index for current file.
            sample_index++;
            /*if(sample_index>317000)
            {
                log_level |= LOG_DROP_ACT|LOG_BUF_DUMP;
            }*/
        }
    }
    // Unlock shared access.
    mtx_samples->unlock();

    if(added>0)
    {
        unprocessed = true;
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[AP] Added"<<added<<"data points into the buffer";
        }
        if((log_level&LOG_BUF_DUMP)!=0)
        {
            qInfo()<<"[AP] Prebuffer after filling (added"<<added<<"data points):";
            dumpPrebuffer();
        }
#endif
        return true;
    }
    return false;
}

//------------------------ Split prebuffer into per-channel buffers.
void AudioProcessor::splitPerChannel()
{
    uint8_t chnl_idx;
    std::deque<PCMSamplePair>::iterator buf_scaner;

    // Per-channel cycle.
    for(chnl_idx=0;chnl_idx<CHANNEL_CNT;chnl_idx++)
    {
        // Clear channel buffer.
        channel_bufs[chnl_idx].clear();
    }
    // Cycle through the prebuffer.
    buf_scaner = prebuffer.begin();
    while(buf_scaner!=prebuffer.end())
    {
        // Per-channel cycle.
        for(chnl_idx=0;chnl_idx<CHANNEL_CNT;chnl_idx++)
        {
            // Copy samples.
            channel_bufs[chnl_idx].push_back((*buf_scaner).samples[chnl_idx]);
        }
        buf_scaner++;
    }
}

//------------------------ Set all invalid flags in the provided range without altering samples.
uint16_t AudioProcessor::setInvalids(std::deque<PCMSample> *samples, CoordinatePair &range)
{
    uint16_t mark_cnt;

    mark_cnt = 0;
    if(range.areValid()!=false)
    {
        // Cycle through the region.
        for(int16_t queue_idx=range.data_start;queue_idx<=range.data_stop;queue_idx++)
        {
            // Mark as invalid.
            (*samples)[queue_idx].setInvalid();
            mark_cnt++;
        }
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_DROP_ACT)!=0)
        {
            QString log_line;
            if(mark_cnt!=0)
            {
                log_line.sprintf("[AP] Marked [%03u] samples as INVALID at range [%03d:%03d] (indexes [%08d:%08d])",
                                 mark_cnt,
                                 range.data_start,
                                 range.data_stop,
                                 (uint32_t)(*samples)[range.data_start].getIndex(),
                                 (uint32_t)(*samples)[range.data_stop].getIndex());
                qInfo()<<log_line;
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Invalid coordinates provided";
    }
    return mark_cnt;
}

//------------------------ Scan for stray valid samples and invalidate those.
void AudioProcessor::fixStraySamples(std::deque<PCMSample> *samples, std::deque<CoordinatePair> *regions)
{
    bool suppress_log;
    bool bad_start;
    size_t queue_size, queue_idx, start_idx;
    CoordinatePair new_region;
    std::deque<CoordinatePair> stray_regions;
    std::deque<CoordinatePair>::iterator buf_scaner;

#ifdef AP_EN_DBG_OUT
    QString log_line;
#endif

    suppress_log = ((log_level&LOG_DROP_ACT)==0);
    //suppress_log = false;

    queue_size = samples->size();
#ifdef AP_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[AP] Starting stray sample detection, queue size: [%03u], filled indexes: [%08u:%08u]",
                         queue_size,
                         (uint32_t)(*samples)[0].getIndex(),
                         (uint32_t)(*samples)[samples->size()-1].getIndex());
        qInfo()<<log_line;
    }
#endif
    if(queue_size==0) return;
    // Find regions of invalid samples.
    bad_start = false;
    queue_idx = start_idx = 1;
    while(queue_idx<queue_size)
    {
        // Check validity of the sample.
        if((*samples)[queue_idx].isValid()==false)
        {
            // Current sample is invalid.
            // Check if invalid region is already started.
            if(bad_start==false)
            {
                // No invalid region started before.
                // Start invalid region.
                bad_start = true;
                start_idx = queue_idx;
#ifdef AP_EN_DBG_OUT
                /*if(suppress_log==false)
                {
                    log_line.sprintf("[AP] Invalid region started at [%03u] ([%08u])", start_idx, (uint32_t)(*samples)[start_idx].getIndex());
                    qInfo()<<log_line;
                }*/
#endif
            }
        }
        else
        {
            // Current sample is valid.
            // Check if invalid region was started.
            if(bad_start!=false)
            {
                // Invalid region was started.
                bad_start = false;
#ifdef AP_EN_DBG_OUT
                /*if(suppress_log==false)
                {
                    log_line.sprintf("[AP] Invalid region stopped at [%03u] ([%08u]), length: [%03u]",
                                     queue_idx,
                                     (uint32_t)(*samples)[queue_idx].getIndex(),
                                     (queue_idx-start_idx));
                    qInfo()<<log_line;
                }*/
#endif
                // Calculate length of the invalid region.
                if((queue_idx-start_idx)>16)
                {
                    // Region is long enough.
                    new_region.clear();
                    new_region.data_start = start_idx;
                    new_region.data_stop = (queue_idx-1);
                    regions->push_back(new_region);
                }
            }
        }
        queue_idx++;
    }
    if(bad_start!=false)
    {
#ifdef AP_EN_DBG_OUT
        /*if(suppress_log==false)
        {
            log_line.sprintf("[AP] Invalid region stopped at [%03u] ([%08u]), length: [%03u] at the end of the buffer",
                             (queue_size-1),
                             (uint32_t)(*samples)[queue_size-1].getIndex(),
                             (queue_size-start_idx-1));
            qInfo()<<log_line;
        }*/
#endif
        // Calculate length of the invalid region.
        if((queue_size-start_idx-1)>16)
        {
            // Region is long enough.
            new_region.clear();
            new_region.data_start = start_idx;
            new_region.data_stop = (queue_size-1);
            // Use this field to mark region at the end of the buffer.
            new_region.not_sure = true;
            regions->push_back(new_region);
        }
    }

#ifdef AP_EN_DBG_OUT
    if(((log_level&LOG_PROCESS)!=0)||(suppress_log==false))
    {
        if(regions->size()>0)
        {
            log_line.sprintf("[AP] Found [%u] long invalid regions", regions->size());
            qInfo()<<log_line;
        }
    }
#endif
    // Cycle through found long regions of invalid data.
    bad_start = false;
    stray_regions.clear();
    buf_scaner = regions->begin();
    while(buf_scaner!=regions->end())
    {
#ifdef AP_EN_DBG_OUT
        if(suppress_log==false)
        {
            log_line.sprintf("[AP] [%03d:%03d] ([%08d:%08d])",
                             (*buf_scaner).data_start, (*buf_scaner).data_stop,
                             (uint32_t)(*samples)[(*buf_scaner).data_start].getIndex(), (uint32_t)(*samples)[(*buf_scaner).data_stop].getIndex());
            qInfo()<<log_line;
        }
#endif
        // Check if this is the first long invalid region.
        if(bad_start==false)
        {
            // The first region.
            bad_start = true;
            // Start to fill valid region coordinates.
            new_region.clear();
            if((*buf_scaner).not_sure==false)
            {
                new_region.data_start = (*buf_scaner).data_stop;
            }
            else
            {
                new_region.data_start = 1;      // First sample in the buffer should always stay valid.
            }
        }
        else
        {
            // No the first invalid region.
            // Save end of the valid region.
            new_region.data_stop = (*buf_scaner).data_start;
            // Put coordinates into the list of possibly stray samples.
            stray_regions.push_back(new_region);
            // Start to fill coordinates to the next valid region.
            new_region.clear();
            new_region.data_start = (*buf_scaner).data_stop;
        }
        buf_scaner++;
    }

    // Remove all regions.
    regions->clear();

    buf_scaner = stray_regions.begin();
    while(buf_scaner!=stray_regions.end())
    {
        if(((*buf_scaner).getDelta()>0)&&((*buf_scaner).getDelta()<28))
        {
#ifdef AP_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[AP] Found region of stray valid samples: [%03d:%03d] ([%08d:%08d])",
                                 (*buf_scaner).data_start, (*buf_scaner).data_stop,
                                 (uint32_t)(*samples)[(*buf_scaner).data_start].getIndex(), (uint32_t)(*samples)[(*buf_scaner).data_stop].getIndex());
                qInfo()<<log_line;
            }
#endif
            setInvalids(samples, (*buf_scaner));
        }
        buf_scaner++;
    }
}

//------------------------ Clear all invalid flags in the provided range without altering samples.
uint16_t AudioProcessor::clearInvalids(std::deque<PCMSample> *samples, CoordinatePair &range)
{
    uint16_t mark_cnt;

    mark_cnt = 0;
    if(range.areValid()!=false)
    {
        // Cycle through the region.
        for(int16_t queue_idx=range.data_start;queue_idx<=range.data_stop;queue_idx++)
        {
            // Mark as fixed.
            (*samples)[queue_idx].setValid();
            mark_cnt++;
        }
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_DROP_ACT)!=0)
        {
            QString log_line;
            if(mark_cnt!=0)
            {
                log_line.sprintf("[AP] Marked [%03u] samples as VALID at range [%03d:%03d] (indexes [%08d:%08d])",
                                 mark_cnt,
                                 range.data_start,
                                 range.data_stop,
                                 (uint32_t)(*samples)[range.data_start].getIndex(),
                                 (uint32_t)(*samples)[range.data_stop].getIndex());
                qInfo()<<log_line;
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Invalid coordinates provided";
    }
    return mark_cnt;
}

//------------------------ Perform zeroing out on a sample, pointet by [offset].
uint16_t AudioProcessor::sampleMute(std::deque<PCMSample> *samples, int16_t offset)
{
    // Check if provided coordinate fit inside the buffer.
    if((offset<0)||(offset>(uint16_t)(samples->size()-1)))
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Provided coordinate is out of range!"<<offset<<samples->size()<<sample_index;
        return 0;
    }
    // Mute one sample.
    (*samples)[offset].setValid();
    (*samples)[offset].setMasked();
    (*samples)[offset].setValue(SMP_NULL);
    return 1;
}

//------------------------ Perform zeroing out on all samples in the provided range.
uint16_t AudioProcessor::rangeMute(std::deque<PCMSample> *samples, CoordinatePair &range)
{
    uint16_t mask_cnt;

    mask_cnt = 0;
    if(range.areValid()!=false)
    {
        // Check if provided coordinates fit inside the buffer.
        if((range.data_start<0)||(range.data_stop>=(int16_t)samples->size()))
        {
            qWarning()<<DBG_ANCHOR<<"[AP] Provided coordinates are out of range!"<<range.data_start<<range.data_stop<<samples->size()<<sample_index;
            return mask_cnt;
        }
        // Cycle through the region.
        for(int16_t queue_idx=(range.data_start+1);queue_idx<range.data_stop;queue_idx++)
        {
            if((*samples)[queue_idx].getValue()!=SMP_NULL)
            {
                // Mute and mark as processed only if the value differs.
                mask_cnt++;
                (*samples)[queue_idx].setValue(SMP_NULL);
                (*samples)[queue_idx].setMasked();
            }
            // Mark as fixed.
            (*samples)[queue_idx].setValid();
        }
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_DROP_ACT)!=0)
        {
            QString log_line;
            if(mask_cnt!=0)
            {
                log_line.sprintf("[AP] Muted [%03u] samples at range [%03d:%03d] (indexes [%08d:%08d])",
                                 mask_cnt,
                                 range.data_start,
                                 range.data_stop,
                                 (uint32_t)(*samples)[range.data_start].getIndex(),
                                 (uint32_t)(*samples)[range.data_stop].getIndex());
                qInfo()<<log_line;
            }
            else
            {
                log_line.sprintf("[AP] Mute requested but samples are already muted at range [%03d:%03d] (indexes [%08d:%08d])",
                                 range.data_start,
                                 range.data_stop,
                                 (uint32_t)(*samples)[range.data_start].getIndex(),
                                 (uint32_t)(*samples)[range.data_stop].getIndex());
                qInfo()<<log_line;
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Invalid coordinates provided"<<range.data_start<<range.data_stop<<samples->size()<<sample_index;
    }
    return mask_cnt;
}

//------------------------ Perform level hold on all dropouts in the provided range.
uint16_t AudioProcessor::rangeLevelHold(std::deque<PCMSample> *samples, CoordinatePair &range)
{
    uint16_t mask_cnt;
    int16_t hold_word;

    mask_cnt = 0;
    if(range.areValid()!=false)
    {
        // Check if provided coordinates fit inside the buffer.
        if((range.data_start<0)||(range.data_stop>=(int16_t)samples->size()))
        {
            qWarning()<<DBG_ANCHOR<<"[AP] Provided coordinates are out of range!"<<range.data_start<<range.data_stop<<samples->size()<<sample_index;
            return mask_cnt;
        }
        // Save starting value to hold.
        hold_word = (*samples)[range.data_start].getValue();
        // Cycle through the region.
        for(int16_t queue_idx=(range.data_start+1);queue_idx<range.data_stop;queue_idx++)
        {
            if((*samples)[queue_idx].getValue()!=hold_word)
            {
                // Hold the same starting level and mark as processed only if the value differs.
                mask_cnt++;
                (*samples)[queue_idx].setValue(hold_word);
                (*samples)[queue_idx].setMasked();
            }
            // Mark as fixed.
            (*samples)[queue_idx].setValid();
        }
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_DROP_ACT)!=0)
        {
            QString log_line;
            if(mask_cnt!=0)
            {
                log_line.sprintf("[AP] Performed [%05d] level hold for [%03u] samples at range [%03d:%03d] (indexes [%08d:%08d])",
                                 hold_word,
                                 mask_cnt,
                                 range.data_start,
                                 range.data_stop,
                                 (uint32_t)(*samples)[range.data_start].getIndex(),
                                 (uint32_t)(*samples)[range.data_stop].getIndex());
                qInfo()<<log_line;
            }
            else
            {
                log_line.sprintf("[AP] Level hold requested but samples are already at that level at range [%03d:%03d] (indexes [%08d:%08d])",
                                 range.data_start,
                                 range.data_stop,
                                 (uint32_t)(*samples)[range.data_start].getIndex(),
                                 (uint32_t)(*samples)[range.data_stop].getIndex());
                qInfo()<<log_line;
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Invalid coordinates provided"<<range.data_start<<range.data_stop<<samples->size()<<sample_index;
    }
    return mask_cnt;
}

//------------------------ Perform level linear interpolation on all dropouts in the provided range.
uint16_t AudioProcessor::rangeLinearInterpolation(std::deque<PCMSample> *samples, CoordinatePair &range)
{
    bool same_level;
    uint16_t mask_cnt, int_idx;
    int16_t lvl_begin, lvl_end;
    int32_t mult_begin, mult_end, lvl_delta, step;

    same_level = false;
    mask_cnt = 0;
    mult_begin = mult_end = 0;
    if(range.areValid()!=false)
    {
        // Check if provided coordinates fit inside the buffer.
        if((range.data_start<0)||(range.data_stop>=(int16_t)samples->size()))
        {
            qWarning()<<DBG_ANCHOR<<"[AP] Provided coordinates are out of range!"<<range.data_start<<range.data_stop<<samples->size()<<sample_index;
            return mask_cnt;
        }
        // Get valid levels at the boundaries.
        lvl_begin = (*samples)[range.data_start].getValue();
        lvl_end = (*samples)[range.data_stop].getValue();
        if(lvl_begin==lvl_end)
        {
            // Levels are the same.
            lvl_delta = 0;
            same_level = true;
        }
        else
        {
            // Multiply levels for integer calculations.
            mult_begin = lvl_begin*CALC_MULT;
            mult_end = lvl_end*CALC_MULT;
            // Calculate difference between levels.
            lvl_delta = mult_end-mult_begin;
        }
        // Calculate how many samples should be interpolated and step size.
        mask_cnt = range.data_stop-range.data_start-1;
        mask_cnt++;
        step = (lvl_delta + mask_cnt/2)/mask_cnt;
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_DROP_ACT)!=0)
        {
            QString log_line;
            log_line.sprintf("[AP] Samples to mask: [%03u] at range [%03d:%03d] (indexes [%08u:%08u]) with data [%05d...%05d]",
                             (mask_cnt-1), range.data_start, range.data_stop,
                             (uint32_t)(*samples)[range.data_start].getIndex(), (uint32_t)(*samples)[range.data_stop].getIndex(),
                             lvl_begin, lvl_end);
            qInfo()<<log_line;
            if(same_level==false)
            {
                log_line.sprintf("[AP] Data (mult): start [%05d] stop [%05d] step [%05d]", mult_begin, mult_end, step);
                qInfo()<<log_line;
            }
        }
#endif
        mask_cnt = 0;
        int_idx = 1;
        // Pre-calculate same level.
        lvl_delta = lvl_begin;
        // Cycle through the region.
        for(int16_t queue_idx=(range.data_start+1);queue_idx<range.data_stop;queue_idx++)
        {
            if(same_level==false)
            {
                // Calculate level on the point of the line.
                lvl_delta = step*int_idx;
                // Apply offset.
                lvl_delta += mult_begin;
                // Divide back after calculation.
                lvl_delta = (lvl_delta + CALC_MULT/2)/CALC_MULT;
            }
            if((*samples)[queue_idx].getValue()!=(int16_t)lvl_delta)
            {
                // Interpolate and mark as processed only if the value differs.
                mask_cnt++;
                (*samples)[queue_idx].setValue((int16_t)lvl_delta);
                (*samples)[queue_idx].setMasked();
            }
            // Mark as fixed.
            (*samples)[queue_idx].setValid();
            int_idx++;
        }
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_DROP_ACT)!=0)
        {
            if(mask_cnt!=0)
            {
                QString log_line;
                log_line.sprintf("[AP] Performed linear interpolation for [%03u] of [%03u] samples at indexes [%08u:%08u]",
                                 mask_cnt, (range.data_stop-range.data_start-1),
                                 (uint32_t)(*samples)[range.data_start].getIndex(),
                                 (uint32_t)(*samples)[range.data_stop].getIndex());
                qInfo()<<log_line;
            }
        }
#endif
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Invalid coordinates provided"<<range.data_start<<range.data_stop<<samples->size()<<sample_index;
    }
    return mask_cnt;
}

//------------------------ Scan for invalid regions and mask those.
void AudioProcessor::fixBadSamples(std::deque<PCMSample> *samples)
{
    bool suppress_log;
    bool good_after_bad, good_before_bad, bad_lock;
    int16_t leftover, bad_stop, good_at_the_end, good_end;
    uint16_t masks_cnt;
    size_t queue_size, queue_idx;
    std::deque<CoordinatePair> bad_regions;

#ifdef AP_EN_DBG_OUT
    QString log_line;
#endif

    good_after_bad = good_before_bad = bad_lock = false;
    masks_cnt = 0;
    bad_stop = good_at_the_end = good_end = 0;

    suppress_log = ((log_level&LOG_DROP_ACT)==0);

    queue_size = samples->size();
#ifdef AP_EN_DBG_OUT
    if(suppress_log==false)
    {
        log_line.sprintf("[AP] Starting invalid sample detection, queue size: [%03u], filled indexes: [%08u:%08u]",
                         queue_size,
                         (uint32_t)(*samples)[0].getIndex(),
                         (uint32_t)(*samples)[samples->size()-1].getIndex());
        qInfo()<<log_line;
    }
#endif
    if(queue_size==0) return;
    // Limit processing only to first half of the buffer.
    queue_idx = queue_size-1;
    /*if((queue_idx>1536)&&(file_end==false))
    {
        queue_idx = 1536;
    }*/
    // Find regions of invalid samples (backwards search).
    while(1)
    {
        // Debug
        /*if(((*samples)[bad_stop].getIndex()>52800)&&((*samples)[bad_stop].getIndex()<52900))
        {
            suppress_log = false;
        }*/

        // Check validity of the sample.
        if((*samples)[queue_idx].isValid()==false)
        {
            // Current sample is invalid.
            if(bad_lock==false)
            {
                // Invalid samples were not found before.
                // Save and lock bad position.
                bad_lock = true;
                bad_stop = (int16_t)queue_idx;
#ifdef AP_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[AP] Found INVALID data end at [%03u] (index [%08u])",
                                     bad_stop, (uint32_t)(*samples)[bad_stop].getIndex());
                    qInfo()<<log_line;
                }
#endif
            }
        }
        else
        {
            // Current sample is valid.
            if(bad_lock==false)
            {
                // Invalid samples were not detected yet,
                // every sample from the end of the buffer is still valid.
                // Update last location with valid samples.
                good_at_the_end = (int16_t)queue_idx;
#ifdef AP_EN_DBG_OUT
                if(suppress_log==false)
                {
                    if(good_after_bad==false)
                    {
                        log_line.sprintf("[AP] Found VALID data at the end of the buffer at [%03u] (index [%08u])",
                                         good_at_the_end, (uint32_t)(*samples)[good_at_the_end].getIndex());
                        qInfo()<<log_line;
                    }
                }
#endif
                // Lock "there is good data at the end".
                good_after_bad = true;
            }
            else
            {
                // Some invalid samples were found towards the end of the buffer,
                // maybe an invalid region is found.
                // Save position of valid samples before invalid ones.
                good_end = (int16_t)queue_idx;
#ifdef AP_EN_DBG_OUT
                if(suppress_log==false)
                {
                    log_line.sprintf("[AP] Found VALID data before invalid at [%03u] (index [%08u])",
                                     good_end, (uint32_t)(*samples)[good_end].getIndex());
                    qInfo()<<log_line;
                }
#endif
                // Save coordinates of invalid region.
                bool add_coords;
                CoordinatePair new_region;
                add_coords = true;
                // Check if there was valid data at the end of the buffer.
                if(good_after_bad==false)
                {
                    // No valid samples found after this one and found some invalid samples.
                    // Check how far from the end of the buffer valid data ends.
                    if(good_end>=((int16_t)samples->size()-(int16_t)(max_ramp_down+max_ramp_up+1)))
                    {
                        // Not enough invalid samples to choose between one region
                        // or two ramps and silent region.
#ifdef AP_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            qInfo()<<"[AP] Not enough invalid samples for two ramps at the end, ignoring invalid samples after for now";
                        }
#endif
                        // Wait until valid sample shifts into first half of the buffer.
                        add_coords = false;
                    }
                    else
                    {
                        // There is at least space for full two ramps (+1 sample of silence) at the end of the buffer.
                        // Start ramp-down at last valid sample.
                        new_region.data_start = good_end;
                        // Calculating ending coordinate for the ramp-down.
                        new_region.data_stop = new_region.data_start+max_ramp_down+1;
                        // Force last sample of the ramp to valid zero.
                        sampleMute(samples, new_region.data_stop);
                        // Ramp-down region will be added to the list at the end.
#ifdef AP_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[AP] Forcing to mute invalid sample at [%03u] ([%08u])",
                                             new_region.data_stop, (uint32_t)(*samples)[new_region.data_stop].getIndex());
                            qInfo()<<log_line;
                            if((*samples)[new_region.data_start].getValue()==SMP_NULL)
                            {
                                log_line.sprintf("[AP] Muted region should be added at [%03u:%03u] ([%08u:%08u])",
                                                 new_region.data_start, new_region.data_stop,
                                                 (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                            }
                            else
                            {
                                log_line.sprintf("[AP] Ramp-down region should be added at [%03u:%03u] ([%08u:%08u])",
                                                 new_region.data_start, new_region.data_stop,
                                                 (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                            }
                            qInfo()<<log_line;
                        }
#endif
                    }
                }
                else
                {
                    // There is another valid sample after invalid region after current sample.
                    // Detected invalid region is surrounded by valid samples in the buffer.
                    {
                        // Save coordinates of the invalid region.
                        new_region.data_start = good_end;
                        new_region.data_stop = good_at_the_end;
                        // Check length of the invalid region.
                        leftover = (new_region.data_stop-new_region.data_start-1);
#ifdef AP_EN_DBG_OUT
                        if(suppress_log==false)
                        {
                            log_line.sprintf("[AP] Found INVALID region at [%03u:%03u] ([%08u:%08u]), containing [%03u] invalid samples between valid samples",
                                             new_region.data_start, new_region.data_stop,
                                             (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex(),
                                             leftover);
                            qInfo()<<log_line;
                        }
#endif
                        // Check if starting sample was processed earlier.
                        if(((*samples)[new_region.data_start].isMasked()==false)||
                           ((*samples)[new_region.data_start].audio_word!=SMP_NULL))
                        {
                            // Starting sample was not altered - it was valid from the source.
#ifdef AP_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[AP] Start of the invalid region was not altered";
                            }
#endif
                            // Check if there is space for silent region.
                            if(leftover>(max_ramp_down+max_ramp_up))
                            {
                                // The region is longer than ramp-down+ramp-up.
#ifdef AP_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    log_line.sprintf("[AP] Region [%08u:%08u] is too long: [%03u]>[%03u+%03u], splitting in three...",
                                                     (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex(),
                                                     leftover, max_ramp_down, max_ramp_up);
                                    qInfo()<<log_line;
                                }
#endif
                                // Add ramp-up region.
                                new_region.data_stop = good_at_the_end;
                                new_region.data_start = new_region.data_stop-max_ramp_up-1;
                                bad_regions.push_front(new_region);
                                // Force first sample of the ramp-up (last sample of muted region) to zero.
                                sampleMute(samples, new_region.data_start);
#ifdef AP_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    log_line.sprintf("[AP] Forcing to mute invalid sample at [%03u] ([%08u])",
                                                     new_region.data_start, (uint32_t)(*samples)[new_region.data_start].getIndex());
                                    qInfo()<<log_line;
                                    log_line.sprintf("[AP] Adding Ramp-up region [%03u:%03u] ([%08u:%08u])",
                                                     new_region.data_start, new_region.data_stop,
                                                     (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                                    qInfo()<<log_line;
                                }
#endif
                                // Add muted region.
                                new_region.data_stop = new_region.data_start;
                                new_region.data_start = good_end+max_ramp_down+1;
                                // Check if muted region is just one sample and no region required.
                                if(new_region.data_stop>new_region.data_start)
                                {
                                    bad_regions.push_front(new_region);
                                    // Force last sample of the ramp-down (first sample of muted region) to zero.
                                    sampleMute(samples, new_region.data_start);
#ifdef AP_EN_DBG_OUT
                                    if(suppress_log==false)
                                    {
                                        log_line.sprintf("[AP] Forcing to mute invalid sample at [%03u] ([%08u])",
                                                         new_region.data_start, (uint32_t)(*samples)[new_region.data_start].getIndex());
                                        qInfo()<<log_line;
                                        log_line.sprintf("[AP] Adding muted region [%03u:%03u] ([%08u:%08u])",
                                                         new_region.data_start, new_region.data_stop,
                                                         (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                                        qInfo()<<log_line;
                                    }
#endif
                                }
                                // Prepare ramp-down region.
                                new_region.data_stop = new_region.data_start;
                                new_region.data_start = good_end;
#ifdef AP_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    log_line.sprintf("[AP] Ramp-down region should be added at [%03u:%03u] ([%08u:%08u])",
                                                     new_region.data_start, new_region.data_stop,
                                                     (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                                    qInfo()<<log_line;
                                }
#endif
                            }
                        }
                        else
                        {
                            // Starting sample was processed - it was zeroed by masking.
#ifdef AP_EN_DBG_OUT
                            if(suppress_log==false)
                            {
                                qInfo()<<"[AP] Start of the invalid region was zeroed-out (altered)";
                            }
#endif
                            // No ramp-down should be generated, only ramp-up and optional silence.
                            // Check if there is space for silent region.
                            if(leftover>max_ramp_up)
                            {
                                // The region is longer than ramp-up.
#ifdef AP_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    log_line.sprintf("[AP] Region [%08u:%08u] is too long: [%03u]>[%03u], splitting in two...",
                                                     (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex(),
                                                     leftover, max_ramp_up);
                                    qInfo()<<log_line;
                                }
#endif
                                // Add ramp-up region.
                                new_region.data_stop = good_at_the_end;
                                new_region.data_start = new_region.data_stop-max_ramp_up-1;
                                bad_regions.push_front(new_region);
                                // Force first sample of the ramp-up (last sample of muted region) to zero.
                                sampleMute(samples, new_region.data_start);
#ifdef AP_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    log_line.sprintf("[AP] Forcing to mute invalid sample at [%03u] ([%08u])",
                                                     new_region.data_start, (uint32_t)(*samples)[new_region.data_start].getIndex());
                                    qInfo()<<log_line;
                                    log_line.sprintf("[AP] Adding Ramp-up region [%03u:%03u] ([%08u:%08u])",
                                                     new_region.data_start, new_region.data_stop,
                                                     (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                                    qInfo()<<log_line;
                                }
#endif
                                // Prepare muted region.
                                new_region.data_stop = new_region.data_start;
                                new_region.data_start = good_end;
#ifdef AP_EN_DBG_OUT
                                if(suppress_log==false)
                                {
                                    log_line.sprintf("[AP] Muted region should be added at [%03u:%03u] ([%08u:%08u])",
                                                     new_region.data_start, new_region.data_stop,
                                                     (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                                    qInfo()<<log_line;
                                }
#endif
                            }
                        }
                    }
                }
                // Check if new region should be added.
                if(add_coords!=false)
                {
                    // Add new invalid region.
                    bad_regions.push_front(new_region);
#ifdef AP_EN_DBG_OUT
                    if(suppress_log==false)
                    {
                        log_line.sprintf("[AP] Adding INVALID region [%03u:%03u] ([%08u:%08u])",
                                         new_region.data_start, new_region.data_stop,
                                         (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                        qInfo()<<log_line;
                    }
#endif
                }
                // Reset and prepare to find another invalid region.
                good_after_bad = true;
                bad_lock = false;
                // Update last location with valid samples.
                good_at_the_end = (int16_t)queue_idx;
            }
        }
        if(queue_idx==0) break;
        queue_idx--;
    }
    // Should be no unfinished regions.
    if(bad_lock!=false)
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Scan stopped with non-closed invalid region, logic error!";
    }
    // Check for unfinished regions.

    std::deque<CoordinatePair>::iterator buf_scaner;
#ifdef AP_EN_DBG_OUT
    if(((log_level&LOG_PROCESS)!=0)||(suppress_log==false))
    {
        if(bad_regions.size()>0)
        {
            log_line.sprintf("[AP] Found [%u] invalid regions", bad_regions.size());
            qInfo()<<log_line;
        }
    }
#endif
    // Cycle through found regions of invalid data.
    buf_scaner = bad_regions.begin();
    while(buf_scaner!=bad_regions.end())
    {
        // Check selected masking mode.
        if((mask_mode==DROP_MUTE_BLOCK)||(mask_mode==DROP_MUTE_WORD))
        {
            // Mute dropouts.
            masks_cnt += rangeMute(samples, *buf_scaner);
        }
        else if((mask_mode==DROP_HOLD_BLOCK)||(mask_mode==DROP_HOLD_WORD))
        {
            // Hold last valid level on dropouts.
            masks_cnt += rangeLevelHold(samples, *buf_scaner);
        }
        else if((mask_mode==DROP_INTER_LIN_BLOCK)||(mask_mode==DROP_INTER_LIN_WORD))
        {
            // Perform linear interpolation on dropouts.
            masks_cnt += rangeLinearInterpolation(samples, *buf_scaner);
        }
        buf_scaner++;
    }
    // Clear invalid region list.
    bad_regions.clear();

    // Check rezidual data at EOF condition.
    if(file_end!=false)
    {
        CoordinatePair new_region;
        good_before_bad = bad_lock = false;
        queue_idx = (queue_size-1);
        // Preset the whole buffer.
        new_region.data_start = 0;
        new_region.data_stop = queue_idx;
        while(queue_idx>0)
        {
            // Check validity of the sample.
            if((*samples)[queue_idx].isValid()==false)
            {
                // Current sample is invalid.
                bad_lock = true;
            }
            else
            {
                // Current sample is valid.
                if(bad_lock!=false)
                {
                    // There some invalid samples left in the buffer.
                    good_before_bad = true;
                    new_region.data_start = queue_idx;
                }
                // Exit on first valid (invalid should be left only at the very end of the buffer).
                break;
            }
            queue_idx--;
        }
        if(bad_lock!=false)
        {
            // Some unprocessed samples were found.
#ifdef AP_EN_DBG_OUT
            if(suppress_log==false)
            {
                log_line.sprintf("[AP] Found unprocessed samples [%03u:%03u] ([%08u:%08u]) at the EOF",
                                 new_region.data_start, new_region.data_stop,
                                 (uint32_t)(*samples)[new_region.data_start].getIndex(), (uint32_t)(*samples)[new_region.data_stop].getIndex());
                qInfo()<<log_line;
                log_line.sprintf("[AP] Forcing to mute invalid sample at [%03u] ([%08u])",
                                 new_region.data_stop, (uint32_t)(*samples)[new_region.data_stop].getIndex());
                qInfo()<<log_line;
            }
#endif
            // Force last sample of the ramp-down to zero.
            sampleMute(samples, new_region.data_stop);
            // Perform ramp-down.
            masks_cnt += rangeLinearInterpolation(samples, new_region);
        }
    }

    if(masks_cnt!=0)
    {
        emit guiAddMask(masks_cnt);
    }
}

//------------------------ Debug print all contents of the internal prebuffer.
void AudioProcessor::dumpPrebuffer()
{
    uint32_t idx;
    std::deque<PCMSamplePair>::iterator buf_scaner;
    QString log_line;

    idx = 0;
    buf_scaner = prebuffer.begin();
    while(buf_scaner!=prebuffer.end())
    {
        log_line.sprintf("[AP] Pos. [%03u] ", idx);
        log_line += QString::fromStdString((*buf_scaner).dumpContentString());
        qInfo()<<log_line;
        buf_scaner++;
        idx++;
    }
}

//------------------------ Re-fill internal buffer from per-channel buffers.
void AudioProcessor::fillBufferForOutput()
{
    uint8_t chnl_idx;
    size_t buffer_size, buf_idx;
    buffer_size = prebuffer.size();
    // Per-channel cycle.
    for(chnl_idx=0;chnl_idx<CHANNEL_CNT;chnl_idx++)
    {
        if(prebuffer.size()!=channel_bufs[chnl_idx].size())
        {
            qWarning()<<DBG_ANCHOR<<"[AP] Internal buffer size mismatch! Logic error!"<<prebuffer.size()<<chnl_idx<<channel_bufs[chnl_idx].size();
            return;
        }
    }
    // Cycle through the prebuffer.
    for(buf_idx=0;buf_idx<buffer_size;buf_idx++)
    {
        // Per-channel cycle.
        for(chnl_idx=0;chnl_idx<CHANNEL_CNT;chnl_idx++)
        {
            // Copy samples from per-channel buffers to prebuffer.
            prebuffer[buf_idx].samples[chnl_idx] = channel_bufs[chnl_idx][buf_idx];
        }
    }

#ifdef AP_EN_DBG_OUT
    if((log_level&LOG_BUF_DUMP)!=0)
    {
        qInfo()<<"[AP] Prebuffer ready for output:";
        dumpPrebuffer();
    }
#endif
}

//------------------------ Convert sample pair to VU levels.
void AudioProcessor::samplesToVU(PCMSamplePair *in_samples)
{
    uint16_t amp_left, amp_right;
    if(out_to_live==false)
    {
        // Don't show and update levels when not in live playback.
        vu_left = vu_right = 0;
    }
    else
    {
        // Get rectified sample for the left channel.
        amp_left = in_samples->samples[PCMSamplePair::CH_LEFT].getAmplitude();
        // Convert to logarithmic scale 8-bit level.
        amp_left = sample2vu[amp_left];
        // Do the same for the right channel.
        amp_right = in_samples->samples[PCMSamplePair::CH_RIGHT].getAmplitude();
        amp_right = sample2vu[amp_right];
        // Make current VU meters levels not less than current level from samples.
        if(vu_left<(uint8_t)amp_left)
        {
            vu_left = (uint8_t)amp_left;
        }
        if(vu_right<(uint8_t)amp_right)
        {
            vu_right = (uint8_t)amp_right;
        }
    }
}

//------------------------ Write an element to the outputs.
void AudioProcessor::outputWordPair(PCMSamplePair *out_samples)
{
    // Update current sample rate.
    setSampleRate(out_samples->getSampleRate());
    // Check if output to file is enabled.
    if(out_to_wav!=false)
    {
        wav_output.saveAudio(*out_samples);
        // Restart close timeout timer.
        emit reqTimerRestart();
    }
    // Check if output to audio device is enabled.
    if(out_to_live!=false)
    {
        sc_output.saveAudio(*out_samples);
    }
    // Process and report VU levels.
    samplesToVU(out_samples);
    emit outSamples(*out_samples);
}

//------------------------ Output processed audio.
void AudioProcessor::outputAudio()
{
    bool stop_output;
    uint16_t points_done, lim_play, lim_cutoff;
    PCMSamplePair data_point;

    lim_play = lim_cutoff = min_valid_before;
    if(file_end==false)
    {
        // If not EOF yet - leave some data in the buffer for look-ahead.
        //lim_play = MIN_TO_PLAYBACK;
        //lim_cutoff = MIN_TO_PROCESS;
        lim_play = lim_cutoff = (min_valid_before+max_ramp_down+max_ramp_up+1);
    }
    // Check if there is a chunk of data ready bigger than minimum storage for look-ahead.
    if(prebuffer.size()<lim_play)
    {
        // The chunk is not big enough to start output.
        return;
    }
    points_done = 0;
    stop_output = false;
    // Dump processed audio until minimum look-ahead number of data points left in the buffer.
    while(prebuffer.size()>min_valid_before)
    {
        // Make sure that first samples in the queue will remain valid.
        for(uint8_t idx=0;idx<=min_valid_before;idx++)
        {
            stop_output = stop_output||(prebuffer.at(idx).isReadyForOutput()==false);
        }
        if(stop_output!=false)
        {
#ifdef AP_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                QString log_line;
                log_line.sprintf("[AP] Stopped output at index [%08u], preserving [%02u] valid samples at the start",
                                 (uint32_t)prebuffer.front().getIndex(),
                                 min_valid_before);
                qInfo()<<log_line;
            }
#endif
            break;
        }
        data_point = prebuffer.front();
        // Remove data point from the output queue.
        prebuffer.pop_front();
        // Output a single data point from the top of the queue.
        outputWordPair(&data_point);
        points_done++;
    }
    if(file_end!=false)
    {
        // Dump everything that's in the buffer from previous file to the output.
        purgePipeline();
    }
#ifdef AP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[AP] Output"<<points_done<<"data points (buffer fill:"<<prebuffer.size()<<")";

    }
    if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FILE_OP)!=0)||((log_level&LOG_LIVE_OP)!=0))
    {
        if(file_end!=false)
        {
            qInfo()<<"[AP] Whole buffer was dumped due to end of file";
        }
    }
#endif
}

//------------------------ Scan internal buffer for errors.
bool AudioProcessor::scanBuffer()
{
    size_t scan_limit;

    scan_limit = 0;
    if(file_end==false)
    {
        scan_limit = prebuffer.size()-min_valid_before-max_ramp_down-max_ramp_up;
    }
    if(prebuffer.size()>scan_limit)
    {
        // Split prebuffer into per-channel buffers.
        splitPerChannel();
        // Cycle through all channels.
        scan_limit = 1;
        if(dbg_output==false)
        {
            // Process only left channel for debug output.
            scan_limit = CHANNEL_CNT;
        }
        // Cycle through channels.
        for(uint8_t chnl_idx=0;chnl_idx<scan_limit;chnl_idx++)
        {
            // Check if dropouts should be procssed.
            if(mask_mode!=DROP_IGNORE)
            {
#ifdef AP_EN_DBG_OUT
                if((log_level&LOG_BUF_DUMP)!=0)
                {
                    qInfo()<<"[AP] Processing channel"<<chnl_idx;
                }
#endif
                // Dropouts should dealt with.
                if(remove_stray!=false)
                {
                    // Scan for stray VALID samples and invalid those.
                    fixStraySamples(&channel_bufs[chnl_idx], &long_bads[chnl_idx]);
                }
                // Scan for INVALID regions and mask those.
                fixBadSamples(&channel_bufs[chnl_idx]);
            }
            else
            {
                // Do nothing with dropouts.
                CoordinatePair buf_lims;
                // Select the whole buffer.
                buf_lims.data_start = 0;
                buf_lims.data_stop = (channel_bufs[chnl_idx].size()-1);
                // Clear invalid flags.
                clearInvalids(&channel_bufs[chnl_idx], buf_lims);
            }
            // TODO: perform de-emphasis if required.
        }
        return true;
    }
    else
    {
        // Not enough data for look-ahead.
        return false;
    }
}

//------------------------ Dump audio processor buffer into output.
void AudioProcessor::dumpBuffer()
{
    PCMSamplePair data_point;
    while(prebuffer.size()>1)
    {
        data_point = prebuffer.front();
        // Remove data point from the output queue.
        prebuffer.pop_front();
        outputWordPair(&data_point);
    }
    wav_output.purgeBuffer();
}

//------------------------ Reset timer.
void AudioProcessor::restartFlushTimer()
{
    // Restart close timeout timer.
    tim_outflush->stop();
    tim_outflush->start();
}

//------------------------ Receive error message and redirect it.
void AudioProcessor::redirectError(QString in_err)
{
    emit mediaError(in_err);
}

//------------------------ Close output by timeout.
void AudioProcessor::actFlushOutput()
{
#ifdef AP_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[AP] Flushing output by timeout...";
    }
#endif
    wav_output.purgeBuffer();
    tim_outflush->stop();
}

//------------------------ Emit [guiLivePB] signal to report live playback state.
void AudioProcessor::livePlayUpdate(bool flag)
{
    emit guiLivePB(flag);
}

//------------------------ Timer event for VU-meters updating and fading.
void AudioProcessor::updateVUMeters()
{
    // Report VU meters values.
    if(out_to_live==false)
    {
        // Don't show levels if live playback is not enabled.
        vu_left = vu_right = 0;
    }
    else
    {
        // Fade meters down.
        if(vu_left>6)
        {
            vu_left -= 6;
        }
        else if(vu_left>0)
        {
            vu_left--;
        }
        if(vu_right>6)
        {
            vu_right -= 6;
        }
        else if(vu_right>0)
        {
            vu_right--;
        }
    }
    emit outVULevels(vu_left, vu_right);
}

//------------------------ Set logging level.
void AudioProcessor::setLogLevel(uint8_t new_log)
{
    log_level = new_log;
}

//------------------------ Set path for output file.
void AudioProcessor::setFolder(QString in_path)
{
#ifdef AP_EN_DBG_OUT
    if((log_level&LOG_SETTINGS)!=0)
    {
        qInfo()<<"[AP] Target folder set to"<<in_path;
    }
#endif
    wav_output.setFolder(in_path);
}

//------------------------ Set filename for output file.
void AudioProcessor::setFileName(QString in_name)
{
#ifdef AP_EN_DBG_OUT
    if((log_level&LOG_SETTINGS)!=0)
    {
        qInfo()<<"[AP] Target file name set to"<<in_name;
    }
#endif
    wav_output.setName(in_name);
}

//------------------------ Enable/disable interpolation.
void AudioProcessor::setMasking(uint8_t in_mask)
{
    if(in_mask<DROP_MAX)
    {
        if(mask_mode!=in_mask)
        {
            mask_mode = in_mask;
#ifdef AP_EN_DBG_OUT
            if((log_level&LOG_SETTINGS)!=0)
            {
                if(mask_mode==DROP_IGNORE)
                {
                    qInfo()<<"[AP] Dropouts will be ignored.";
                }
                else if(mask_mode==DROP_MUTE_BLOCK)
                {
                    qInfo()<<"[AP] Dropouts will be muted by data block.";
                }
                else if(mask_mode==DROP_MUTE_WORD)
                {
                    qInfo()<<"[AP] Dropouts will be muted by sample.";
                }
                else if(mask_mode==DROP_HOLD_BLOCK)
                {
                    qInfo()<<"[AP] Dropouts will be hold on last good level by data block.";
                }
                else if(mask_mode==DROP_HOLD_WORD)
                {
                    qInfo()<<"[AP] Dropouts will be hold on last good level by sample.";
                }
                else if(mask_mode==DROP_INTER_LIN_BLOCK)
                {
                    qInfo()<<"[AP] Dropouts will be linearly interpolated by data block.";
                }
                else if(mask_mode==DROP_INTER_LIN_WORD)
                {
                    qInfo()<<"[AP] Dropouts will be linearly interpolated by sample.";
                }
            }
#endif
        }
    }
}

//------------------------ Enable/disable output to a file.
void AudioProcessor::setOutputToFile(bool flag)
{
    if(out_to_wav!=flag)
    {
        out_to_wav = flag;
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[AP] File output set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[AP] File output set to 'enabled'.";
            }
        }
#endif
    }
}

//------------------------ Enable/disable output to soundcard.
void AudioProcessor::setOutputToLive(bool flag)
{
    if(out_to_live!=flag)
    {
        out_to_live = flag;
#ifdef AP_EN_DBG_OUT
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[AP] Live output set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[AP] Live output set to 'enabled'.";
            }
        }
#endif
    }
}

//------------------------ Main execution loop.
void AudioProcessor::processAudio()
{
    uint8_t ext_tw_log_lvl, ext_ta_log_lvl;
#ifdef AP_EN_DBG_OUT
    qInfo()<<"[AP] Launched, thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId());
#endif
    // Check working pointers.
    if((in_samples==NULL)||(mtx_samples==NULL))
    {
        qWarning()<<DBG_ANCHOR<<"[AP] Empty input pointer provided, unable to continue!";
        emit finished();
        return;
    }

    connect(this, SIGNAL(stopOutput()), &sc_output, SLOT(stopOutput()));

    QTimer timCloseTimer;
    timCloseTimer.setInterval(250);
    tim_outflush = &timCloseTimer;
    connect(tim_outflush, SIGNAL(timeout()), this, SLOT(actFlushOutput()));

    QTimer timVUTimer;
    timVUTimer.setSingleShot(false);
    timVUTimer.setInterval(15);
    tim_vu_fade = &timVUTimer;
    connect(tim_vu_fade, SIGNAL(timeout()), this, SLOT(updateVUMeters()));
    tim_vu_fade->start();

    // Inf. loop in a thread.
    while(finish_work==false)
    {
        // Process Qt events.
        QApplication::processEvents();

        if(finish_work!=false)
        {
            // Break the loop and prepare for shutdown.
            purgePipeline();
            break;
        }
        // Set logging parameters.
        ext_tw_log_lvl = ext_ta_log_lvl = 0;
        if((log_level&LOG_FILE_OP)!=0)
        {
            ext_tw_log_lvl |= (SamplesToWAV::LOG_PROCESS|SamplesToWAV::LOG_WAVE_SAVE);
        }
        if((log_level&LOG_LIVE_OP)!=0)
        {
            ext_ta_log_lvl |= (SamplesToAudio::LOG_PROCESS|SamplesToAudio::LOG_WAVE_LIVE);
        }
        wav_output.setLogLevel(ext_tw_log_lvl);
        sc_output.setLogLevel(ext_ta_log_lvl);

        QElapsedTimer dbg_timer;
        qint64 tm_data_lock, tm_data_read, tm_data_process, tm_loop;
        Q_UNUSED(tm_data_lock);
        Q_UNUSED(tm_data_read);
        Q_UNUSED(tm_data_process);
        Q_UNUSED(tm_loop);

        tm_data_lock = 0;
        tm_data_read = 0;

        dbg_timer.start();

        //log_level |= LOG_FILL;

        // Move data to the internal buffer.
        if(fillUntilBufferFull()==false)
        {
            // No new data has been added.
            // Calm down.
            QThread::msleep(50);
        }
        else
        {
            // Process data in the internal buffer.
            if(scanBuffer()!=false)
            {
                // Re-fill prebuffer from per-channel buffers.
                fillBufferForOutput();
                // Output from the internal buffer.
                outputAudio();
                unprocessed = false;
            }
        }

        file_end = false;
    }

    qInfo()<<"[AP] Loop stop.";
    emit finished();
}

//------------------------ Output all data from the buffer.
void AudioProcessor::purgePipeline()
{
#ifdef AP_EN_DBG_OUT
    if(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_FILE_OP)!=0)||((log_level&LOG_LIVE_OP)!=0))
    {
        qInfo()<<"[AP] Purging buffer...";
    }
#endif

    tim_outflush->stop();
    // TODO: maybe add processing dropouts before dumping.
    dumpBuffer();
    wav_output.releaseFile();
    wav_output.prepareNewFile();
    sc_output.purgeBuffer();
    emit stopOutput();
    emit newSource();

    PCMSamplePair empty_smp;
    unprocessed = false;
    prebuffer.clear();
    for(uint8_t idx=0;idx<CHANNEL_CNT;idx++)
    {
        channel_bufs[idx].clear();
        long_bads[idx].clear();
    }
    empty_smp.setSamplePair(SMP_NULL, SMP_NULL, true, true, true, true, false, false);
    prebuffer.push_back(empty_smp);
    sample_index = 1;
}

//------------------------ Set the flag to break execution loop and exit.
void AudioProcessor::stop()
{
#ifdef AP_EN_DBG_OUT
    qInfo()<<"[AP] Received termination request";
#endif
    finish_work = true;
}
