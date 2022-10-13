#ifndef VIN_FFMPEG_H
#define VIN_FFMPEG_H

#include <queue>
#include <stdint.h>
#include <string>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QString>
#include "config.h"
#include "ffmpegwrapper.h"
#include "stc007datablock.h"
#include "stc007datastitcher.h"
#include "vid_preset_t.h"
#include "videoline.h"

//#define VIP_LINES_UPSIDEDOWN    1   // Scan frame from bottom to top
#define VIP_BUF_FMT_BW      (AV_PIX_FMT_YUV420P)      // Frame format for normal BW Y-decoded buffer.
#define VIP_BUF_FMT_COLOR   (AV_PIX_FMT_GBRP)       // Frame format for per-color-channel GBR-decoded buffer.

class VideoInFFMPEG : public QObject
{
    Q_OBJECT

public:
    // Results for [decoderInit()].
    enum
    {
        VIP_RET_NO_SRC,         // Failed to open source.
        VIP_RET_NO_STREAM,      // Can not find stream info.
        VIP_RET_NO_DECODER,     // No decoder found for video.
        VIP_RET_UNK_STREAM,     // Unknown error while searching video stream.
        VIP_RET_NO_DEC_CTX,     // Failed to allocate decoder context.
        VIP_RET_ERR_DEC_PARAM,  // Failed to set decoder parameters.
        VIP_RET_ERR_DEC_START,  // Failed to start decoder.
        VIP_RET_NO_FRM_BUF,     // Failed to initialize frame buffer.
        VIP_RET_OK              // All containers and data are ready to decode video.
    };

    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),  // External operations with settings.
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_FRAME = (1<<2),     // General stage-by-stage logging.
        LOG_LINES = (1<<3),     // Output per-line details.
    };

    // Limits for enabling double width resise.
    enum
    {
        MIN_DOUBLE_WIDTH = 10,
        MAX_DOUBLE_WIDTH = 959
    };

    // Source processing mode for [proc_state].
    enum
    {
        STG_IDLE,               // Idle state (no source, no playback).
        STG_LOADING,            // Got [EVT_USR_LOAD] event, trying to open source.
        STG_STOP,               // Source is provided and available, no playback.
        STG_PLAY,               // Decoding source.
        STG_PAUSE,              // Waiting, ready to resume playback.
        STG_STOPPING,           // Going from playback to stop.
        STG_REOPEN,             // Stop, finish output, close input, reopen it and wait.
        STG_MAX
    };

    // Event list from user.
    enum
    {
        EVT_USR_NO = 0,         // No new events.
        EVT_USR_LOAD = (1<<0),  // New source selected.
        EVT_USR_PLAY = (1<<1),  // Playback start request.
        EVT_USR_PAUSE = (1<<2), // Pause request.
        EVT_USR_STOP = (1<<3),  // Playback stop request.
        EVT_USR_UNLOAD = (1<<4) // Release/close source.
    };

    // Event list from capture.
    enum
    {
        EVT_CAP_NO = 0,         // No new events.
        EVT_CAP_OPEN = (1<<0),  // New source opened successfully.
        EVT_CAP_FRAME = (1<<1), // Received new frame.
        EVT_CAP_CLOSE = (1<<2), // Source closed.
        EVT_CAP_ERROR = (1<<3)  // Some error occurent while capturing.
    };

private:
    uint8_t log_level;          // Setting for debugging log level.
    uint8_t proc_state;         // State of processing.
    uint8_t new_state;          // Desired new state.
    uint8_t event_usr;          // Events from user.
    uint8_t event_cap;          // Events from capture.
    QString evt_source;         // Source path, provided with [EVT_USR_LOAD] event.
    uint32_t evt_frame_cnt;     // Frame count, provided with [EVT_CAP_OPEN] event.
    std::deque<QImage> evt_frames;      // New frames, provided with [EVT_CAP_FRAME] event.
    std::deque<bool> evt_double;        // Double width flag for the frame, provided with [EVT_CAP_FRAME] event.
    uint32_t evt_errcode;       // Last error code, provided with [EVT_CAP_ERROR] event.
    uint16_t last_real_width;   // Last frame width.
    uint16_t last_height;       // Last frame height.
    uint32_t frame_counter;     // Frame counter from the start.
    uint32_t frames_total;      // Number of frames that is written into the current stream.
    vid_preset_t vip_set;       // "Video Input Processor" fine settings (see [vid_preset_t.h]).
    QString src_path;           // Location of the media source.
    QString last_error_txt;     // Human-readable description of the last error.
    std::deque<VideoLine> *out_lines;   // Pointer to output queue of video lines.
    VideoLine gray_line;
    VideoLine dummy_line;
    QMutex *mtx_lines;          // Lock for [lines_queue].
    QThread *ffmpeg_thread;     // Thread for FFMPEG wrapper.
    FFMPEGWrapper *ffmpeg_src;  // FFMPEG Qt-wrapper.
    bool new_file;              // Is this the first frame of a new source?
    bool step_play;             // Enable or disable auto-pause after each frame.
    bool detect_frame_drop;     // Detect dropouts via PTS or not?
    bool finish_work;           // Flag to break executing loop.

public:
    explicit VideoInFFMPEG(QObject *parent = 0);

private:
    uint16_t getFinalHeight(uint16_t);
    uint16_t getFinalWidth(uint16_t);
    void resetCounters();
    bool hasWidthDoubling(uint16_t);
    void askNextFrame();
    bool waitForOutQueue(uint16_t);
    void outNewLine(VideoLine *);
    void insertFileEndLine(uint16_t line_number = 0);
    void insertFieldEndLine(uint16_t line_number = 0);
    void insertFrameEndLine(uint16_t line_number = 0);
    void spliceFrame(QImage *in_frame, bool in_double = false);
    void insertDummyFrame(bool last_frame = false, bool report = true);
    void insertNewFileLine();
    void processFrame();

public slots:
    void setLogLevel(uint8_t new_log_lvl = 0);
    void setOutputPointers(std::deque<VideoLine> *in_queue = NULL, QMutex *in_mtx = NULL);
    void setSourceLocation(QString);
    void setStepPlay(bool in_step = false);
    void setDropDetect(bool in_detect = false);
    void setFineSettings(vid_preset_t in_set);  // Set fine video processing settings.
    void setDefaultFineSettings();          // Set fine video processor settings to defaults.
    void requestCurrentFineSettings();      // Get current fine video processor settings.
    QString logEventCapture(uint8_t in_evt);
    QString logEventUser(uint8_t in_evt);
    QString logState(uint8_t in_state);
    void runFrameDecode();                  // Main execution loop.
    void mediaPlay();
    void mediaPause();
    void mediaStop();
    void mediaUnload();
    void stop();

private slots:
    void captureReady(int in_width, int in_height, uint32_t in_frames, float in_fps);
    void captureClosed();
    void captureError(uint32_t);
    void receiveFrame(QImage, bool);

signals:
    void requestDropDet(bool);              // Set droped frames detector.
    void requestColor(vid_preset_t);        // Set color channel.
    void openDevice(QString, QString);      // Request opening file/device as video source.
    void closeDevice();                     // Request closing capture.
    void requestFrame();                    // Request next frame from the capture.
    void mediaNotFound();                   // Requested playback without a source.
    void mediaLoaded(QString);              // New source is opened.
    void mediaPlaying(uint32_t);            // Playback started/resumed.
    void mediaPaused();                     // Playback paused.
    void mediaStopped();                    // Playback stopped.
    void mediaError(QString);               // Error with capturing.
    void guiUpdFineSettings(vid_preset_t);  // New fine settings were applied.
    void newFrame(uint16_t, uint16_t);      // New frame started processing.
    void newLine(VideoLine);                // New line is processed.
    void frameDropDetected();
    void frameDecoded(uint32_t);
    void finished();
};

#endif // VIN_FFMPEG_H
