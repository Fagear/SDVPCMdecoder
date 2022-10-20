#include "samples2wav.h"

// WAV file header.
const uint8_t SamplesToWAV::default_header[SamplesToWAV::HDR_SZ] =
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
    log_level = 0;
    sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void SamplesToWAV::setLogLevel(uint8_t in_level)
{
    log_level = in_level;
}

//------------------------ Set folder to save wave data.
void SamplesToWAV::setFolder(QString path)
{
    // Check if provided path is different from used.
    if(file_path.compare(path)!=0)
    {
        // Close file at old location.
        releaseFile();
        // Save new path.
        file_path = path;
#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TW] Setting new output to"<<file_path;
        }
#endif
    }
}

//------------------------ Set filename to write to.
void SamplesToWAV::setName(QString name)
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
            qInfo()<<"[TW] Setting new filename to"<<file_name;
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
    file_name.clear();
    file_path.clear();
#ifdef TW_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[TW] Prepared for a new output";
    }
#endif
}

//------------------------ Save audio data from data block into buffer.
void SamplesToWAV::saveAudio(PCMSamplePair in_audio)
{
    if(openOutput()!=false)
    {
        file_out.write((const char*)&in_audio.samples[PCMSamplePair::CH_LEFT].audio_word, PCMSamplePair::getSampleSize());
        file_out.write((const char*)&in_audio.samples[PCMSamplePair::CH_RIGHT].audio_word, PCMSamplePair::getSampleSize());
    }
}

//------------------------ Output all data from the buffer into file.
void SamplesToWAV::purgeBuffer()
{
    if(file_out.isOpen()!=false)
    {
        // Purge output buffer.
        file_out.flush();
        // Update file header.
        updateHeader();
        // Purge again.
        file_out.flush();
#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_WAVE_SAVE)!=0)
        {
            qInfo()<<"[TW] Flushed file";
        }
#endif
    }
}

//------------------------ Perform final header update and close file.
void SamplesToWAV::releaseFile()
{
    purgeBuffer();
    // Check if file is open.
    if(file_out.isOpen()!=false)
    {
        // Close output file.
        file_out.close();
#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_WAVE_SAVE)!=0)
        {
            qInfo()<<"[TW] Closed file";
        }
#endif
    }
}

//------------------------ Open output file.
bool SamplesToWAV::openOutput()
{
    QString output_path;
    // Check if file is not yet opened.
    if(file_out.isOpen()!=false)
    {
        return true;
    }
    if((file_path.isEmpty()!=false)||(file_name.isEmpty()!=false))
    {
        return false;
    }
#ifdef TW_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[TW] File is not opened yet, creating/rewriting new...";
    }
#endif
    output_path = file_path+"/"+file_name+"_v"+APP_VERSION+".wav";
    // Open file to append.
    file_out.setFileName(output_path);
    file_out.open(QIODevice::ReadWrite|QIODevice::Truncate);
    // Check if file opened.
    if(file_out.isOpen()!=false)
    {
        // File opened ok.
        // Write default WAV header.
        addHeader();
        return true;
    }
    // No luck, failed to open.
#ifdef TW_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[TW] Failed to open the file for writing!"<<output_path;
    }
#endif
    return false;
}

//------------------------ Write WAV-header into new opened file.
void SamplesToWAV::addHeader()
{
    if(file_out.isOpen()!=false)
    {
        // Seek to the beginning of the file.
        file_out.seek(HDR_ID_OFS);
        // Write WAV header.
        file_out.write((const char*)default_header, HDR_SZ);
#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_WAVE_SAVE)!=0)
        {
            qInfo()<<"[TW] WAV-header written";
        }
#endif
    }
}

//------------------------ Update file size in WAV-header.
void SamplesToWAV::updateHeader()
{
    uint32_t file_size, wave_size, byte_rate, smp_rate;
    qint64 file_pos;

    if(file_out.isOpen()!=false)
    {
        // Save current audio data position.
        file_pos = file_out.pos();
        // Get size of the file.
        file_size = wave_size = file_out.size();


        // Substract first two header fields from the total size.
        file_size = file_size-(HDR_ID_SZ+HDR_SIZE_SZ);
        // Seek to "file size" field.
        file_out.seek(HDR_SIZE_OFS);
        // Update size.
        file_out.write((const char*)&file_size, HDR_SIZE_SZ);

        // Copy from [uint16_t] to [uint32_t] to get two zero MSbytes written later.
        smp_rate = sample_rate;
        // Seek to "sample rate" field.
        file_out.seek(HDR_SRATE_OFS);
        // Update rate.
        file_out.write((const char*)&smp_rate, HDR_SRATE_SZ);

        // 2 channels with one sample per channel, one sample = 2 bytes.
        byte_rate = sample_rate*PCMSamplePair::CH_MAX*2;
        // Seek to "byterate" field.
        file_out.seek(HDR_BRATE_OFS);
        // Update rate.
        file_out.write((const char*)&byte_rate, HDR_BRATE_SZ);

        // Substract header size from total file size.
        wave_size -= HDR_SZ;
        // Seek to "audio data size" field.
        file_out.seek(HDR_SUBSIZE2_OFS);
        // Update size.
        file_out.write((const char*)&wave_size, HDR_SUBSIZE2_SZ);

        // Return to previous position.
        file_out.seek(file_pos);
#ifdef TW_EN_DBG_OUT
        if((log_level&LOG_WAVE_SAVE)!=0)
        {
            qInfo()<<"[TW] WAV-header updated, new size:"<<wave_size;
        }
#endif
    }
}
