#ifndef AUDIOPROCESSOR_H
#define AUDIOPROCESSOR_H

#include <deque>
#include <stdint.h>
#include <thread>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QThread>
#include <QString>
#include "config.h"
#include "frametrimset.h"
#include "pcmsamplepair.h"
#include "stc007datablock.h"
#include "stc007toaudio.h"
#include "stc007towav.h"

class AudioProcessor : public QObject
{
    Q_OBJECT
public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),  // External operations with settings.
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_FILL = (1<<2),      // Data fill events.
        LOG_DROP_ACT = (1<<3),  // Log sample corrections.
        LOG_BUF_DUMP = (1<<4),  // Output full buffer content.
        LOG_FILE_OP = (1<<5),   // File operations.
        LOG_LIVE_OP = (1<<6)    // Live output operations.
    };

    enum
    {
        SMP_NULL = 0,           // Silent sample.
    };

    // Buffer limits.
    enum
    {
        BUF_SIZE = 512,                         // Buffer size for buffered playback (must divide by 4!).
        MIN_LONG_INVALID = 16,                  // Minimum length of invalid region to be count as long.
        MAX_STRAY_LEN = 24,                     // Maximum length of valid region to be count as stray.
        MAX_RAMP = 128,                         // Maximum length of fade-in and fade-out ramps.
        MIN_TO_PROCESS = (BUF_SIZE/2),          // Minimum size of data set to perform scanning.
        MIN_TO_PLAYBACK = (BUF_SIZE*3/4),
        CHANNEL_CNT = PCMSamplePair::CH_MAX,    // Number of channels for processing.
    };

    // Multiplier for integer calculations.
    enum
    {
        CALC_MULT = 16          // Audio data multiplier for interpolation integer calculation.
    };

    // Modes of drop masking.
    enum
    {
        DROP_IGNORE,            // Do nothing with dropouts.
        DROP_MUTE_BLOCK,        // Mute (zero out) dropouts on whole data block flags.
        DROP_MUTE_WORD,         // Mute (zero out) dropouts on per-word flags.
        DROP_HOLD_BLOCK,        // Hold last good level on whole data block flags.
        DROP_HOLD_WORD,         // Hold last good level on per-word flags.
        DROP_INTER_LIN_BLOCK,   // Interpolate dropouts on whole data block flags.
        DROP_INTER_LIN_WORD,    // Interpolate dropouts on per-word flags.
        DROP_MAX
    };

private:
    STC007ToAudio sc_output;                    // Handler for soundcard operations.
    STC007ToWAV wav_output;                     // Handler for WAV-file operations.
    QString file_path;
    QString file_name;
    std::deque<PCMSamplePair> *in_samples;      // Input sample pair queue (shared).
    QMutex *mtx_samples;                        // Mutex for input queue.
    QTimer *tim_outclose;
    std::deque<PCMSamplePair> prebuffer;        // Buffer for input data.
    std::deque<PCMSample> channel_bufs[CHANNEL_CNT];    // Per-channel buffers for sample processing.
    std::deque<CoordinatePair> long_bads[CHANNEL_CNT];
    uint8_t log_level;                          // Level of debug output.
    uint8_t mask_mode;                          // Mode of masking dropouts.
    uint64_t sample_index;                      // Master index for samples for the current file.
    uint16_t sample_rate;                       // Last sample rate.
    bool file_end;                              // Detected end of a file.
    bool unprocessed;                           // Are there unprocessed audio samples?
    bool remove_stray;                          // Invalidate stray VALID samples.
    bool out_to_wav;                            // Output data to WAV file.
    bool out_to_live;                           // Output data to soundcard.
    bool dbg_output;                            // Debug mode of the output.
    bool finish_work;                           // Flag to break executing loop.

public:
    explicit AudioProcessor(QObject *parent = 0);
    void setInputPointers(std::deque<PCMSamplePair> *in_samplepairs = NULL, QMutex *mtx_samplepairs = NULL);

private:
    bool fillUntilBufferFull();
    bool scanBuffer();
    void splitPerChannel();
    uint16_t setInvalids(std::deque<PCMSample> *samples, CoordinatePair &range);
    void fixStraySamples(std::deque<PCMSample> *samples, std::deque<CoordinatePair> *regions);
    uint16_t clearInvalids(std::deque<PCMSample> *samples, CoordinatePair &range);
    uint16_t performMute(std::deque<PCMSample> *samples, CoordinatePair &range);
    uint16_t performLevelHold(std::deque<PCMSample> *samples, CoordinatePair &range);
    uint16_t performLinearInterpolation(std::deque<PCMSample> *samples, CoordinatePair &range);
    void fixBadSamples(std::deque<PCMSample> *samples);
    void fillBufferForOutput();
    void outputAudio();
    void dumpPrebuffer();

    void setSampleRate(uint16_t);           // Set audio sample rate.
    void outputWordPair();
    void dumpBuffer();

private slots:
    void restartCloseTimer();
    void actCloseOutput();

public slots:
    void setLogLevel(uint8_t);              // Set logging level.
    void setFolder(QString);                // Set path for output file.
    void setFileName(QString);              // Set filename for output file.
    void setMasking(uint8_t);               // Enable/disable interpolation.
    void setOutputToFile(bool);             // Enable/disable output to a file.
    void setOutputToLive(bool);             // Enable/disable output to soundcard.
    void livePlayUpdate(bool);              // Emit [guiLivePB] signal to report live playback state.
    void processAudio();                    // Main execution loop.
    void purgePipeline();                   // Output all data from the buffer.
    void stop();                            // Set the flag to break execution loop and exit.

signals:
    void guiAddMute(uint16_t);              // Report new muted samples.
    void guiAddMask(uint16_t);              // Report new masked samples.
    void guiLivePB(bool);                   // Report live playback state.
    void newSource();                       // Report about changed source.
    void reqTimerRestart();
    void stopOutput();                      // Report about thread terminating.
    void finished();
};

#endif // AUDIOPROCESSOR_H
