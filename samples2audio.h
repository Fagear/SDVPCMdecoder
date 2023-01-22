/**************************************************************************************************************************************************************
samples2audio.h

Copyright © 2023 Maksim Kryukov <fagear@mail.ru>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Created: 2020-09

**************************************************************************************************************************************************************/

#ifndef SAMPLES2AUDIO_H
#define SAMPLES2AUDIO_H

#include <QAudioOutput>
#include <QDebug>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QThread>
#include "config.h"
#include "circbuffer.h"
#include "pcmsamplepair.h"

#ifndef QT_VERSION
    #undef TA_EN_DBG_OUT
#endif

#define TA_AUDIO_CATEGORY   (APP_NAME)

//#define TA_AD_BUF_SIZE      (262144)
#define TA_AD_BUF_SIZE      (8192)

typedef union
{
    struct
    {
        int16_t word_left;
        int16_t word_right;
    };
    uint8_t bytes[4];
} sample_pair_t;

class SamplesToAudio : public QObject
{
    Q_OBJECT
public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_WAVE_LIVE = (1<<2),
    };

private:
    QIODevice *audio_dev;           // Underlying device, created and destroyed by QAudioOutput.
    QAudioDeviceInfo audio_info;
    QAudioFormat audio_settings;
    QAudioOutput *audio_if;
    QTimer *timDumpAudio;
    circarray<sample_pair_t, TA_AD_BUF_SIZE> audio;
    char live_buf[TA_AD_BUF_SIZE*4];
    uint8_t log_level;                  // Setting for debugging log level.
    uint16_t sample_rate;
    uint32_t cur_pos;
    bool output_en;

public:
    explicit SamplesToAudio(QObject *parent = 0);
    ~SamplesToAudio();

private:
    void deleteTimer();
    void deleteAudioInterface();

public slots:
    void setLogLevel(uint8_t in_level);
    void setSampleRate(uint16_t in_rate);
    void prepareNewFile();
    void saveAudio(int16_t in_left, int16_t in_right);
    void saveAudio(PCMSamplePair in_audio);
    void purgeBuffer();
    void stopOutput();

private slots:
    void scStateChanged(QAudio::State);

signals:
    void livePlayback(bool);
};

#endif // SAMPLES2AUDIO_H
