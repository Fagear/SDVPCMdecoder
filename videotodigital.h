#ifndef VIDEOTODIGITAL_H
#define VIDEOTODIGITAL_H

#include <deque>
#include <iterator>
#include <queue>
#include <stdint.h>
#include <QApplication>
#include <QDebug>
#include <QObject>
#include <QMutex>
#include <QThread>
#include <QString>
#include "config.h"
#include "circbuffer.h"
#include "binarizer.h"
#include "frametrimset.h"
#include "pcm16x0datastitcher.h"
#include "pcm16x0subline.h"
#include "pcm1datastitcher.h"
#include "pcm1line.h"
#include "stc007line.h"
#include "videoline.h"

class VideoToDigital : public QObject
{
    Q_OBJECT

public:
    // Field start detector states.
    enum
    {
        FIELD_INIT,     // Field detector starting idle state.
        FIELD_NEW,      // New field should start at this state, detect safe/unsafe condition.
        FIELD_SAFE,     // Field beginning passed, now in the field, first line is safe to process.
        FIELD_UNSAFE,   // Field beginning passed, now in the field, first line is unsafe (can be duplicated from inactive region).
    };

    enum
    {
        COORD_CHECK_LINES = 6,                      // Number of lines to check PCM coordinates on buffer prescan.
        COORD_CHECK_PARTS = (COORD_CHECK_LINES+2),  // Add regions at the top and the bottom of first and last checked lines.
        COORD_HISTORY_DEPTH = 8,                    // Width of sliding window of history of valid data coordinates.
        COORD_LONG_HISTORY = 16                     // Number of last frames to store averaged valid data coordinates for.
    };

private:
    Binarizer line_converter;               // Binarizator.
    std::deque<VideoLine> *in_video;        // Input video line queue (shared).
    std::deque<PCM1Line> *out_pcm1;         // Output PCM PCM-1 line queue (shared).
    std::deque<PCM16X0SubLine> *out_pcm16x0;    // Output PCM PCM-16x0 subline queue (shared).
    std::deque<STC007Line> *out_stc007;     // Output PCM STC-007 line queue (shared).
    std::deque<VideoLine> frame_buf;        // Buffer for current frame.
    QMutex *mtx_vid;                        // Mutex for input queue.
    QMutex *mtx_pcm1;                       // Mutex for PCM-1 output queue.
    QMutex *mtx_pcm16x0;                    // Mutex for PCM-16x0 output queue.
    QMutex *mtx_stc007;                     // Mutex for STC-007 output queue.
    uint8_t log_level;                      // Level of debug output.
    uint8_t pcm_type;                       // Set PCM type in source video.
    uint8_t binarization_mode;              // Binarization mode.
    FrameBinDescriptor signal_quality;      // Statistics for tracking (signal/noise ratio).
    bool line_dump_help_done;               // Was help for line dump printed?
    bool check_line_copy;                   // Perform dublicate line detection.
    bool coordinate_damper;                 // Limit horizonral drift of the data coordinates.
    bool reset_stats;                       // Flag to reset all statistics and start over.
    bool finish_work;                       // Flag to break executing loop.

public:
    explicit VideoToDigital(QObject *parent = 0);
    void setInputPointers(std::deque<VideoLine> *in_vline = NULL, QMutex *mtx_vline = NULL);
    void setOutPCM1Pointers(std::deque<PCM1Line> *out_pcmline = NULL, QMutex *mtx_pcmline = NULL);
    void setOutPCM16X0Pointers(std::deque<PCM16X0SubLine> *out_pcmline = NULL, QMutex *mtx_pcmline = NULL);
    void setOutSTC007Pointers(std::deque<STC007Line> *out_pcmline = NULL, QMutex *mtx_pcmline = NULL);

private:
    bool waitForOneFrame();                 // Wait for the end of the frame to appear in the input queue.
    CoordinatePair prescanCoordinates(uint8_t *out_ref = NULL);     // Pre-scan buffer for average data coordinates.
    static CoordinatePair medianCoordinates(std::deque<CoordinatePair> *in_list);   // Apply median filter to the list of coordinates.
    void outNewLine(PCMLine *in_line);      // Output processed line into output queue.

public slots:
    void setLogLevel(uint8_t);              // Set logging level (using [STC007Binarizer] defines).
    void setPCMType(uint8_t in_pcm = PCMLine::TYPE_STC007);                 // Set PCM type in source video.
    void setBinarizationMode(uint8_t in_mode = Binarizer::MODE_NORMAL);     // Set binarizator mode.
    void setCheckLineDup(bool = true);      // Set line duplication detection mode.
    void setFineSettings(bin_preset_t in_set);                              // Set fine binarization settings.
    void setDefaultFineSettings();          // Set fine binarization settings to defaults.
    void requestCurrentFineSettings();      // Get current fine binarization settings.
    void doBinarize();                      // Main execution loop.
    void stop();                            // Set the flag to break execution loop and exit.

signals:
    void guiUpdFrameBin(FrameBinDescriptor);    // New frame was binarized in LB thread.
    void guiUpdFineSettings(bin_preset_t);  // New fine settings were applied.
    void loopTime(quint64);                 // Useful loop time count.
    void newLine(PCM1Line);                 // Processed PCM-1 line object (for visualization).
    void newLine(PCM16X0SubLine);           // Processed PCM-16x0 subline object (for visualization).
    void newLine(STC007Line);               // Processed STC-007 line object (for visualization).
    void finished();                        // Thread is stopped.
};

#endif // VIDEOTODIGITAL_H
