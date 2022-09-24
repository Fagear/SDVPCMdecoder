#ifndef STC007TOAUDIO_H
#define STC007TOAUDIO_H

#include <QAudioOutput>
#include <QDebug>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QThread>
#include "config.h"
#include "audio_sample.h"
#include "circbuffer.h"
#include "pcmsamplepair.h"

#ifndef QT_VERSION
    #undef TA_EN_DBG_OUT
#endif

#define TA_AUDIO_CATEGORY   (APP_NAME)

//#define TA_AD_BUF_SIZE      (262144)
#define TA_AD_BUF_SIZE      (8192)

class STC007ToAudio : public QObject
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
    explicit STC007ToAudio(QObject *parent = 0);
    ~STC007ToAudio();

private:
    void deleteTimer();
    void deleteAudioInterface();

public slots:
    void setLogLevel(uint8_t in_level);
    void setSampleRate(uint16_t in_rate);
    void prepareNewFile();
    void saveAudio(int16_t in_left, int16_t in_right);
    void purgeBuffer();
    void stopOutput();

private slots:
    void scStateChanged(QAudio::State);

signals:
    void livePlayback(bool);
};

#endif // STC007TOAUDIO_H
