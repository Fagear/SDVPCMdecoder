#include "pcmsamplepair.h"

PCMSample::PCMSample()
{
    this->clear();
}

PCMSample::PCMSample(const PCMSample &in_object)
{
    audio_word = in_object.audio_word;
    index = in_object.index;
    data_block_ok = in_object.data_block_ok;
    word_ok = in_object.word_ok;
    word_processed = in_object.word_processed;
}

PCMSample& PCMSample::operator= (const PCMSample &in_object)
{
    if(this==&in_object) return *this;

    audio_word = in_object.audio_word;
    index = in_object.index;
    data_block_ok = in_object.data_block_ok;
    word_ok = in_object.word_ok;
    word_processed = in_object.word_processed;

    return *this;
}

//------------------------ Reset object.
void PCMSample::clear()
{
    audio_word = 0;
    index = 0;
    data_block_ok = word_ok = word_processed = false;
}

//------------------------ Set value of the sample.
void PCMSample::setValue(int16_t in_val)
{
    audio_word = in_val;
}

//------------------------ Set index of the sample.
void PCMSample::setIndex(uint64_t in_idx)
{
    index = in_idx;
}

//------------------------ Mark sample as invalid.
void PCMSample::setInvalid()
{
    word_ok = false;
}

//------------------------ Mark sample as fixed.
void PCMSample::setFixed()
{
    word_ok = true;
}

//------------------------ Mark sample as processed.
void PCMSample::setProcessed()
{
    word_processed = true;
}

//------------------------ Set (irreversably) word validity by data block validity.
void PCMSample::setValidityByBlock()
{
    word_ok = data_block_ok;
}

//------------------------ Get value of the sample.
int16_t PCMSample::getValue()
{
    return audio_word;
}

//------------------------ Get amlitude of the sample (rectify audio).
uint16_t PCMSample::getAmplitude()
{
    if(audio_word<0)
    {
        return (uint16_t)(0-audio_word);
    }
    else
    {
        return (uint16_t)audio_word;
    }
}

//------------------------ Get index of the sample;
uint64_t PCMSample::getIndex()
{
    return index;
}

//------------------------ Is sample zeroed?
bool PCMSample::isSilent()
{
    if(audio_word==0)
    {
        return true;
    }
    return false;
}

//------------------------ Is sample valid?
bool PCMSample::isValid()
{
    return word_ok;
}

//------------------------ Is sample processed?
bool PCMSample::isProcessed()
{
    return word_processed;
}

//------------------------ Convert PCM data to string for debug.
std::string PCMSample::dumpWordsString()
{
    std::string text_out;
    uint8_t bit_pos;

    if(data_block_ok==false)
    {
        text_out = DUMP_WBRL_BAD;
    }
    else
    {
        text_out = DUMP_WBRL_OK;
    }
    // Printing 16 bit samples data.
    bit_pos = 16;
    do
    {
        bit_pos--;
        if(word_ok==false)
        {
            if((audio_word&(1<<bit_pos))==0)
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
            if((audio_word&(1<<bit_pos))==0)
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
    if(data_block_ok==false)
    {
        text_out += DUMP_WBRR_BAD;
    }
    else
    {
        text_out += DUMP_WBRR_OK;
    }

    return text_out;
}





PCMSamplePair::PCMSamplePair()
{
    this->clear();
}

PCMSamplePair::PCMSamplePair(const PCMSamplePair &in_object)
{
    for(uint8_t idx=0;idx<CH_MAX;idx++)
    {
        samples[idx] = in_object.samples[idx];
    }
    sample_rate = in_object.sample_rate;
    index = in_object.index;
    emphasis = in_object.emphasis;
    service_type = in_object.service_type;
    file_path = in_object.file_path;
}

PCMSamplePair& PCMSamplePair::operator= (const PCMSamplePair &in_object)
{
    if(this==&in_object) return *this;

    for(uint8_t idx=0;idx<CH_MAX;idx++)
    {
        samples[idx] = in_object.samples[idx];
    }
    sample_rate = in_object.sample_rate;
    index = in_object.index;
    emphasis = in_object.emphasis;
    service_type = in_object.service_type;
    file_path = in_object.file_path;

    return *this;
}

//------------------------ Reset object.
void PCMSamplePair::clear()
{
    for(uint8_t idx=0;idx<CH_MAX;idx++)
    {
        samples[idx].clear();
    }
    sample_rate = SAMPLE_RATE_44056;
    index = 0;
    emphasis = false;
    service_type = SRV_NO;
    file_path.clear();
}

//------------------------ Set service tag "new file opened".
void PCMSamplePair::setServNewFile(std::string path)
{
    // Clear all fields.
    clear();
    // Set source file path.
    file_path = path;
    service_type = SRV_NEW_FILE;
}

//------------------------ Set service tag "file ended".
void PCMSamplePair::setServEndFile()
{
    // Clear all fields.
    clear();
    service_type = SRV_END_FILE;
}

//------------------------ Set sample for one channel.
bool PCMSamplePair::setSample(uint8_t channel, int16_t sample, bool block_ok, bool word_ok)
{
    if(channel>=CH_MAX)
    {
        return false;
    }
    samples[channel].audio_word = sample;
    samples[channel].data_block_ok = block_ok;
    samples[channel].word_ok = word_ok;
    return true;
}

//------------------------ Set sample pair.
void PCMSamplePair::setSamplePair(int16_t ch1_sample, int16_t ch2_sample, bool ch1_block, bool ch2_block, bool ch1_word, bool ch2_word)
{
    setSample(CH_LEFT, ch1_sample, ch1_block, ch1_word);
    setSample(CH_RIGHT, ch2_sample, ch2_block, ch2_word);
}

//------------------------ Set (irreversably) samples validity by data block validity.
void PCMSamplePair::setValidityByBlock()
{
    for(uint8_t idx=0;idx<CH_MAX;idx++)
    {
        samples[idx].setValidityByBlock();
    }
}

//------------------------ Set sample rate.
void PCMSamplePair::setSampleRate(uint16_t in_rate)
{
    if(in_rate<SAMPLE_RATE_MAX)
    {
        sample_rate = in_rate;
    }
}

//------------------------ Set index of the set.
void PCMSamplePair::setIndex(uint64_t in_idx)
{
    index = in_idx;
    for(uint8_t idx=0;idx<CH_MAX;idx++)
    {
        samples[idx].setIndex(index);
    }
}

//------------------------ Set emphasis state for samples.
void PCMSamplePair::setEmphasis(bool on)
{
    emphasis = on;
}

//------------------------ Set sample for one channel.
int16_t PCMSamplePair::getSample(uint8_t channel)
{
    if(channel>=CH_MAX)
    {
        return 0;
    }
    return samples[channel].audio_word;
}

//------------------------ Get sample rate.
uint16_t PCMSamplePair::getSampleRate()
{
    return sample_rate;
}

//------------------------ Get index of the set;
uint64_t PCMSamplePair::getIndex()
{
    return index;
}

//------------------------ Get state of the exact sample.
bool PCMSamplePair::isSampleValid(uint8_t channel)
{
    if(channel>=CH_MAX)
    {
        return false;
    }
    return samples[channel].word_ok;
}

//------------------------ Get state of source word.
bool PCMSamplePair::isBlockValid(uint8_t channel)
{
    if(channel>=CH_MAX)
    {
        return false;
    }
    return samples[channel].data_block_ok;
}

//------------------------ Should de-emphasis be applied?
bool PCMSamplePair::isEmphasisSet()
{
    return emphasis;
}

//------------------------ Are all samples zeroed?
bool PCMSamplePair::isSilent()
{
    bool silent;
    silent = true;
    for(uint8_t idx=0;idx<CH_MAX;idx++)
    {
        silent = silent&&samples[idx].isSilent();
    }
    return silent;
}

//------------------------ Check if it has any service tag.
bool PCMSamplePair::isServicePair()
{
    if(service_type==SRV_NO)
    {
        return false;
    }
    else
    {
        return true;
    }
}

//------------------------ Check if it has service tag "new file opened".
bool PCMSamplePair::isServNewFile()
{
    if(service_type==SRV_NEW_FILE)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Check if is has service tag "file ended".
bool PCMSamplePair::isServEndFile()
{
    if(service_type==SRV_END_FILE)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//------------------------ Are samples ready for output (valid or processed)?
bool PCMSamplePair::isReadyForOutput()
{
    bool ready;
    ready = true;
    for(uint8_t idx=0;idx<CH_MAX;idx++)   // TODO: set to all channels
    //for(uint8_t idx=0;idx<CH_RIGHT;idx++)
    {
        ready = ready&&(samples[idx].isValid()||samples[idx].isProcessed());
        if(ready==false) break;
    }
    return ready;
}

//------------------------ Convert PCM data to string for debug.
std::string PCMSamplePair::dumpWordsString()
{
    std::string text_out;

    for(uint8_t idx=0;idx<CH_MAX;idx++)
    {
        text_out += samples[idx].dumpWordsString();
    }

    return text_out;
}

//------------------------ Output full information about the sample pair.
std::string PCMSamplePair::dumpContentString()
{
    std::string text_out;
    char c_buf[256];

    if(isServicePair()!=false)
    {
        if(isServNewFile()!=false)
        {
            sprintf(c_buf, "SERVICE PAIR: next samples are from new file: %s", file_path.substr(0, 192).c_str());
            text_out += c_buf;
        }
        else if(isServEndFile()!=false)
        {
            text_out += "SERVICE PAIR: previous samples were last in the file";
        }
    }
    else
    {
        sprintf(c_buf, "I[%08u]", (uint32_t)index);
        text_out = c_buf;

        text_out += dumpWordsString();

        text_out += " P[";
        for(uint8_t idx=0;idx<CH_MAX;idx++)
        {
            if(samples[idx].word_processed==false)
            {
                text_out += " ";
            }
            else
            {
                text_out += "+";
            }
            if(idx!=(CH_MAX-1))
            {
                text_out += "|";
            }
        }
        text_out += "]";
        sprintf(c_buf, " S[%05u]", sample_rate);
        text_out += c_buf;

        if(isEmphasisSet()==false)
        {
            text_out += " NOEMPH  ";
        }
        else
        {
            text_out += " EMPHASIS";
        }
    }

    return text_out;
}
