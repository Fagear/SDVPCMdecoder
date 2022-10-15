#include "samples2wav.h"

// WAV file header.
static const uint8_t header[TW_WAV_HEAD_SIZE] =
{
    'R', 'I', 'F', 'F',
    0, 0, 0, 0,                                 // File size - 8 (in bytes)
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    0x10, 0, 0, 0,                              // Header size (16)
    0x01, 0,                                    // Quantization (1 = linear)
    0x02, 0,                                    // Channels (2 = stereo)
    0x44, 0xAC, 0, 0,                           // Sample rate (44100)
    0x10, 0xB1, 0x02, 0,                        // Byterate (176400)
    //0x18, 0xAC, 0, 0,                           // Sample rate (44056)
    //0x60, 0xB0, 0x02, 0,                        // Byterate (176224)
    0x04, 0,                                    // Sample size (2 x 2 bytes)
    0x10, 0,                                    // Bits per sample (16)
    'd', 'a', 't', 'a',
    0, 0, 0, 0                                  // Audio data size (in bytes)
};

SamplesToWAV::SamplesToWAV()
{
    file_name = "output";
    for(uint16_t index=0;index<TW_AUD_BUF_SIZE;index++)
    {
        audio.words[index] = 0;
    }
    new_file = true;
    log_level = 0;
    sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
    cur_pos = 0;
    written_data = 0;
    file_hdl.rdbuf()->pubsetbuf(stream_buffer, sizeof(char)*TW_FS_BUF_SIZE);
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void SamplesToWAV::setLogLevel(uint8_t in_level)
{
    log_level = in_level;
}

//------------------------ Set folder to save wave data.
void SamplesToWAV::setFolder(std::string path)
{
    // Check if provided path is different from used.
    if(path_for_wav.compare(path)!=0)
    {
        // Close file at old location.
        releaseFile();
        // Save new path.
        path_for_wav = path;
#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TW] Setting new output to"<<QString::fromStdString(path_for_wav);
        }
#endif
    }
}

//------------------------ Set filename to write to.
void SamplesToWAV::setName(std::string name)
{
    // Check if provided filename is different from used.
    if(file_name.compare(name)!=0)
    {
        // Close file at old location.
        releaseFile();
        // Save new filename.
        file_name = name;
#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TW] Setting new filename to"<<QString::fromStdString(file_name);
        }
#endif
    }
}

//------------------------ Set samplerate of audio.
void SamplesToWAV::setSampleRate(uint16_t in_rate)
{
    if(in_rate==PCMSamplePair::SAMPLE_RATE_44100)
    {
        if(sample_rate!=PCMSamplePair::SAMPLE_RATE_44100)
        {
            sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
#ifdef TW_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[TW] Sample rate set to"<<sample_rate;
            }
#endif
        }
    }
    else
    {
        if(sample_rate!=PCMSamplePair::SAMPLE_RATE_44056)
        {
            sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
#ifdef TW_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[TW] Sample rate set to"<<sample_rate;
            }
#endif
        }
    }
}

//------------------------ Setup for new output.
void SamplesToWAV::prepareNewFile()
{
    new_file = true;
    // Reset buffer position.
    cur_pos = 0;
    // Reset written audio size.
    written_data = 0;
#ifdef TW_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[TW] Prepared for a new output";
    }
#endif
}

//------------------------ Save audio data from data block into buffer.
void SamplesToWAV::saveAudio(int16_t in_left, int16_t in_right)
{
    // Check available space in buffer.
    if((TW_AUD_BUF_SIZE-cur_pos)<=1)
    {
        // Buffer is full, need to dump its content into output file.
        purgeBuffer();
    }
    // Again check available space in buffer.
    if((TW_AUD_BUF_SIZE-cur_pos)>1)
    {
        // There is enough space for one sample pair.
        audio.words[cur_pos++] = in_left;
        audio.words[cur_pos++] = in_right;
    }
    else
    {
#ifdef TW_EN_DBG_OUT
        // No space.
        qWarning()<<DBG_ANCHOR<<"[TW] Buffer overflow!";
#endif
        // Drop the buffer.
        cur_pos = 0;
    }
}

//------------------------ Save audio data from data block into buffer.
void SamplesToWAV::saveAudio(PCMSamplePair in_audio)
{
    // Check available space in buffer.
    if((TW_AUD_BUF_SIZE-cur_pos)<=1)
    {
        // Buffer is full, need to dump its content into output file.
        purgeBuffer();
    }
    // Again check available space in buffer.
    if((TW_AUD_BUF_SIZE-cur_pos)>1)
    {
        // There is enough space for one sample pair.
        // Copy audio samples.
        audio.words[cur_pos++] = in_audio.samples[PCMSamplePair::CH_LEFT].audio_word;
        audio.words[cur_pos++] = in_audio.samples[PCMSamplePair::CH_RIGHT].audio_word;
        // Copy samplerate.
        sample_rate = in_audio.getSampleRate();
    }
    else
    {
#ifdef TW_EN_DBG_OUT
        // No space.
        qWarning()<<DBG_ANCHOR<<"[TW] Buffer overflow!";
#endif
        // Drop the buffer.
        cur_pos = 0;
    }
}

//------------------------ Output all data from the buffer into file.
void SamplesToWAV::purgeBuffer()
{
    std::string audio_path;
    audio_path = path_for_wav+"/"+file_name+"_v"+APP_VERSION+".wav";
    // Check if there is anything to dump.
    if(cur_pos>0)
    {
        // Check if file is not yet opened.
        if(file_hdl.is_open()==false)
        {
            // Check if file was not opened/created yet.
            if(new_file==false)
            {
                // This file was opened before.
#ifdef TW_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[TW] File is not opened yet, but was opened before, appending...";
                }
#endif
                // Open file to append.
                file_hdl.open(audio_path, (std::ios::in|std::ios::out|std::ios::binary));
                // Check if file opened.
                if(file_hdl.is_open()==false)
                {
                    // No luck, failed to open.
#ifdef TW_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        qInfo()<<"[TW] Failed to append the file!"<<QString::fromStdString(audio_path);
                    }
#endif
                }
                else
                {
                    // File opened ok.
                    // Seek to the end.
                    file_hdl.seekp(0, std::ios::end);
                    // File opened.
                    new_file = false;
#ifdef TW_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        qInfo()<<"[TW] Opened file location:"<<file_hdl.tellp();
                    }
#endif
                }
            }
            else
            {
#ifdef TW_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[TW] File is not opened yet, creating/rewriting new...";
                }
#endif
                // Open file to append.
                file_hdl.open(audio_path, (std::ios::out|std::ios::binary|std::ios::trunc));
                // Check if file opened.
                if(file_hdl.is_open()==false)
                {
                    // No luck, failed to open.
#ifdef TW_EN_DBG_OUT
                    if((log_level&LOG_PROCESS)!=0)
                    {
                        qInfo()<<"[TW] Failed to rewrite the file!"<<QString::fromStdString(audio_path);
                    }
#endif
                }
                else
                {
                    // File opened ok.
                    // Write WAV header.
                    addHeader();
                    // File opened.
                    new_file = false;
                }
            }
        }
        // Check if file is opened successfully.
        if(file_hdl.is_open()!=false)
        {
            // Write chunk of audio data.
            file_hdl.write((const char*)audio.bytes, cur_pos*2);
            written_data += (cur_pos*2);
            // Reset buffer fill position.
            cur_pos = 0;
#ifdef TW_EN_DBG_OUT
            if((log_level&LOG_WAVE_SAVE)!=0)
            {
                qInfo()<<"[TW] Wrote a chunk of audio into file,"<<(written_data/2)<<"sample pairs total.";
            }
#endif
            // Update file size.
            updateHeader();
        }
        else
        {
#ifdef TW_EN_DBG_OUT
            if((log_level&LOG_WAVE_SAVE)!=0)
            {
                qInfo()<<"[TW] Unable to write to file!";
            }
#endif
        }
    }
    // No luck, failed to open.
#ifdef TW_EN_DBG_OUT
    else
    {
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TW] Dump skip.";
        }
    }
#endif
}

//------------------------ Perform final header update and close file.
void SamplesToWAV::releaseFile()
{
    // Check if file is open.
    if(file_hdl.is_open()!=false)
    {
        // Update header.
        updateHeader();
        // Write out all data and close.
        file_hdl.flush();
        file_hdl.close();

#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_WAVE_SAVE)!=0)
        {
            qInfo()<<"[TW] Closed file";
        }
#endif
    }
}

//------------------------ Write WAV-header into new opened file.
void SamplesToWAV::addHeader()
{
    // Seek to the beginning of the file.
    file_hdl.seekp(0, std::ios::beg);
    // Write WAV header.
    file_hdl.write((const char*)header, TW_WAV_HEAD_SIZE);
#ifdef TW_EN_DBG_OUT
    if((log_level&LOG_WAVE_SAVE)!=0)
    {
        qInfo()<<"[TW] WAV-header written";
    }
#endif
}

//------------------------ Update file size in WAV-header.
void SamplesToWAV::updateHeader()
{
    uint32_t wav_size, byte_rate;
    std::ifstream::streampos writing_pos;
    // Calculate total file size.
    wav_size = written_data+TW_WAV_HEAD_SIZE-8;

    // Save current audio data position.
    writing_pos = file_hdl.tellp();

    // 2 channels with one sample per channel, one sample = 2 bytes.
    byte_rate = sample_rate*PCMSamplePair::CH_MAX*2;

    // Seek to "file size" field.
    file_hdl.seekp(4, std::ios::beg);
    // Update size.
    file_hdl.write((const char*)&wav_size, 4);

    // Seek to "sample rate" field.
    file_hdl.seekp(24, std::ios::beg);
    // Update rate.
    file_hdl.write((const char*)&sample_rate, 4);

    // Seek to "byterate" field.
    file_hdl.seekp(28, std::ios::beg);
    // Update rate.
    file_hdl.write((const char*)&byte_rate, 4);

    // Seek to "audio data size" field.
    file_hdl.seekp(40, std::ios::beg);
    // Update size.
    file_hdl.write((const char*)&written_data, 4);

    // Return to previous position.
    file_hdl.seekp(writing_pos);
#ifdef TW_EN_DBG_OUT
    if((log_level&LOG_WAVE_SAVE)!=0)
    {
        qInfo()<<"[TW] WAV-header updated, new size:"<<wav_size;
    }
#endif
}
