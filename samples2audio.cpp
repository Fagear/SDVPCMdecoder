#include "samples2audio.h"

SamplesToAudio::SamplesToAudio(QObject *parent) : QObject(parent)
{
    audio_dev = NULL;
    audio_info = QAudioDeviceInfo::defaultOutputDevice();
    audio_if = NULL;
    timDumpAudio = NULL;

    log_level = 0;
    sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
    cur_pos = 0;

    // Set PCM settings.
    audio_settings.setChannelCount(2);     // Stereo output.
    audio_settings.setSampleSize(16);      // 16 bit.
    audio_settings.setSampleType(QAudioFormat::SignedInt);
    audio_settings.setByteOrder(QAudioFormat::LittleEndian);
    audio_settings.setCodec(QString("audio/pcm"));
    audio_settings.setSampleRate(PCMSamplePair::SAMPLE_RATE_44100);
}

SamplesToAudio::~SamplesToAudio()
{
    deleteTimer();
    deleteAudioInterface();
}

//------------------------
void SamplesToAudio::deleteTimer()
{
    if(timDumpAudio!=NULL)
    {
        timDumpAudio->stop();
        timDumpAudio->disconnect();
        delete timDumpAudio;
        timDumpAudio = NULL;
    }
}

//------------------------
void SamplesToAudio::deleteAudioInterface()
{
    if(audio_if!=NULL)
    {
        audio_if->stop();
        audio_if->reset();
        audio_if->disconnect();
        audio_dev = NULL;
        delete audio_if;
        audio_if = NULL;
    }
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void SamplesToAudio::setLogLevel(uint8_t in_level)
{
    log_level = in_level;
}

//------------------------ Set samplerate of audio.
void SamplesToAudio::setSampleRate(uint16_t in_rate)
{
    if(in_rate==PCMSamplePair::SAMPLE_RATE_44100)
    {
        if(sample_rate!=PCMSamplePair::SAMPLE_RATE_44100)
        {
            sample_rate = PCMSamplePair::SAMPLE_RATE_44100;
#ifdef TA_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[TA] Sample rate set to"<<sample_rate;
            }
#endif
        }
    }
    else
    {
        if(sample_rate!=PCMSamplePair::SAMPLE_RATE_44056)
        {
            sample_rate = PCMSamplePair::SAMPLE_RATE_44056;
#ifdef TA_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[TA] Sample rate set to"<<sample_rate;
            }
#endif
        }
    }
}

//------------------------ Setup for new stream.
void SamplesToAudio::prepareNewFile()
{
    // Close old output (if exists).
    deleteTimer();
    deleteAudioInterface();
    // Re-apply sample rate.
    audio_settings.setSampleRate(PCMSamplePair::SAMPLE_RATE_44100);
    // Check if hardware is capable.
    output_en = audio_info.isFormatSupported(audio_settings);
    if(output_en!=false)
    {
        // Create audio interface object.
        audio_if = new QAudioOutput(audio_info, audio_settings, this);
        if(audio_if==NULL)
        {
            output_en = false;
#ifdef TA_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[TA] Unable to create 'QAudioOutput' object, output unavailable!";
#endif
            return;
        }
        // Create timer object.
        timDumpAudio = new QTimer(this);
        if(timDumpAudio==NULL)
        {
            output_en = false;
#ifdef TA_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[TA] Unable to create 'QTimer' object, output unavailable!";
#endif
            delete audio_if;
            audio_if = NULL;
            return;
        }

        // Connect events and handlers of audio.
        connect(audio_if, SIGNAL(stateChanged(QAudio::State)), this, SLOT(scStateChanged(QAudio::State)));
        connect(timDumpAudio, SIGNAL(timeout()), this, SLOT(purgeBuffer()));

        audio_if->setCategory(TA_AUDIO_CATEGORY);
        //audio_if->setVolume(1.0);
#ifdef TA_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TA] Default buffer size:"<<(audio_if->bufferSize());
        }
#endif
        //audio_if->setBufferSize(TA_AD_BUF_SIZE*2);
#ifdef TA_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TA] New buffer size:"<<(audio_if->bufferSize());
        }
#endif
        audio_dev = audio_if->start();
        if((audio_if->state()!=QAudio::IdleState)||(audio_if->error()!=QAudio::NoError))
        {
#ifdef TA_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[TA] Unable to open audio device!";
            }
#endif
            deleteTimer();
            deleteAudioInterface();
        }
        else
        {
#ifdef TA_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[TA] Bytes per period required:"<<audio_if->periodSize()
                      <<", buffer size:"<<(audio_if->bufferSize())
                      <<", mode:"<<audio_dev->openMode();
            }
#endif
            timDumpAudio->start(20);
        }
    }
    else
    {
#ifdef TA_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[TA] No compatible hardware found!";
#endif
    }
}

//------------------------ Save audio data from data block into buffer (blocking).
void SamplesToAudio::saveAudio(int16_t in_left, int16_t in_right)
{
    //qInfo()<<"[TA] Saving...";
#ifdef TA_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        if(audio.full()!=false)
        {
            qInfo()<<"[TA] Waiting for free space in the buffer...";
        }
    }
#endif
    // Check available space in buffer.
    while(audio.full()!=false)
    {
        purgeBuffer();
    };
    sample_pair_t in_sample;
    in_sample.word_left = in_left;
    in_sample.word_right = in_right;
    audio.push(in_sample);
}

//------------------------ Output all data from the buffer into sound device.
void SamplesToAudio::purgeBuffer()
{
    // Check if there is anything to dump.
    if(audio.empty()==false)
    {
        if((audio_if!=NULL)&&(audio_if->state()!=QAudio::StoppedState))
        {
            int32_t chunks;
            uint32_t written;
            qint64 to_write;
            sample_pair_t temp_pair;

            chunks = audio_if->bytesFree()/audio_if->periodSize();
#ifdef TA_EN_DBG_OUT
            if((log_level&LOG_WAVE_LIVE)!=0)
            {
                qInfo()<<"[TA] Audio buffer free:"<<audio_if->bytesFree()<<", state:"<<audio_if->state();
                qInfo()<<"[TA] Chunks to write:"<<chunks<<"each of size:"<<audio_if->periodSize()<<", input size:"<<audio.size();
            }
#endif
            written = 0;
            while(chunks!=0)
            {
                to_write = 0;
                while(to_write<audio_if->periodSize())
                {
                    if(audio.empty()==false)
                    {
                        temp_pair = audio.pop();
                        live_buf[to_write++] = temp_pair.bytes[0];
                        live_buf[to_write++] = temp_pair.bytes[1];
                        live_buf[to_write++] = temp_pair.bytes[2];
                        live_buf[to_write++] = temp_pair.bytes[3];
                    }
                    else
                    {
                        break;
                    }
                }

                if(to_write>0)
                {
                    written += audio_dev->write(live_buf, to_write);
                }
#ifdef TA_EN_DBG_OUT
                if((log_level&LOG_WAVE_LIVE)!=0)
                {
                    qInfo()<<"[TA] Written:"<<written<<", to_write:"<<to_write;
                }
#endif
                if(to_write!=audio_if->periodSize())
                {
                    // Nothing left in the input buffer.
                    break;
                }
                chunks--;
            }
        }
    }
}

//------------------------ Stop playback and timers, flush all buffers.
void SamplesToAudio::stopOutput()
{
    deleteTimer();
    deleteAudioInterface();
#ifdef TA_EN_DBG_OUT
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[TA] Stopping output...";
    }
#endif
}

//------------------------ React on sound card state change.
void SamplesToAudio::scStateChanged(QAudio::State new_state)
{
    if(new_state==QAudio::IdleState)
    {
#ifdef TA_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TA] Audio device entered idle state";
        }
#endif
        emit livePlayback(false);
    }
    else if(new_state==QAudio::ActiveState)
    {
#ifdef TA_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TA] Audio device entered active state";
        }
#endif
        emit livePlayback(true);
    }
    else if(new_state==QAudio::SuspendedState)
    {
#ifdef TA_EN_DBG_OUT
        if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[TA] Audio device entered suspended state";
        }
#endif
        emit livePlayback(false);
    }
    else if(new_state==QAudio::StoppedState)
    {
        emit livePlayback(false);
        if(timDumpAudio!=NULL)
        {
            timDumpAudio->stop();
        }
        QAudio::Error aif_err;
        if(audio_if!=NULL)
        {
            aif_err = audio_if->error();
            if(aif_err==QAudio::OpenError)
            {
#ifdef TA_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[TA] Unable to open audio device!";
#endif
            }
            else if(aif_err==QAudio::IOError)
            {
#ifdef TA_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[TA] Unexpected error while writing to audio device!";
#endif
            }
            else if(aif_err==QAudio::FatalError)
            {
#ifdef TA_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[TA] Audio devide is not usable!";
#endif
            }
            else if(aif_err==QAudio::UnderrunError)
            {
#ifdef TA_EN_DBG_OUT
                qWarning()<<DBG_ANCHOR<<"[TA] Buffer underrun occured!";
#endif
            }
        }
    }
}
