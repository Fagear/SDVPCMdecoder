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
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QString>
#include "config.h"
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
        LOG_PIXELS = (1<<4)     // Output per-pixel datails.
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
        STG_SRC_DET,            // Source is provided and available, no playback.
        STG_PLAY,               // Decoding source.
        STG_PAUSE               // Waiting, ready to resume playback.
    };

private:
    uint8_t log_level;          // Setting for debugging log level.
    uint8_t proc_state;         // State of processing.
    uint8_t new_state;          // Desired new state.
    AVPixelFormat target_pixfmt;    // Pixel format for target frame (set in [initFrameConverter()]).
    uint16_t last_real_width;   // Last frame width.
    uint16_t last_width;        // Last frame width.
    uint16_t last_height;       // Last frame height.
    uint8_t last_color_mode;    // Last color mode (see [vid_preset_t.h]).
    uint32_t frame_counter;     // Frame counter from the start.
    uint32_t frames_total;      // Number of frames that is written into the current stream.
    int64_t last_dts;           // Last frame pts (for drop detection).
    vid_preset_t vip_set;       // "Video Input Processor" fine settings (see [vid_preset_t.h]).
    QString src_path;           // Location of the media source.
    QString last_error_txt;     // Human-readable description of the last error.
    std::deque<VideoLine> *out_lines;   // Pointer to output queue of video lines.
    VideoLine gray_line;
    VideoLine dummy_line;
    QMutex *mtx_lines;          // Lock for [lines_queue].
    AVFormatContext *format_ctx;        // Context for FFMPEG format.
    AVCodecContext *video_dec_ctx;      // Context for FFMPEG decoder.
    AVBufferRef *hw_dev_ctx;            // HW device context.
    AVHWDeviceType hw_type;             // Device type for HW decoder.
    AVPixelFormat last_frame_fmt;       // Last frame format.
    AVFrame *dec_frame;                 // Frame holder for FFMPEG decoded frame.
    AVPacket dec_packet;                // Packet container for FFMPEG decoder.
    SwsContext *conv_ctx;               // Context for FFMPEG frame converter.
    bool img_buf_free;
    uint8_t *video_dst_data[4];         // Container for frame data from the decoder.
    int video_dst_linesize[4];          // Container for frame parameters from the decoder.
    int stream_index;           // Current open video stream index.
    bool step_play;             // Enable or disable auto-pause after each frame.
    bool detect_frame_drop;     // Detect dropouts via PTS or not?
    bool dts_detect;            // Was PTS from new source already detected?
    bool finish_work;           // Flag to break executing loop.

public:
    explicit VideoInFFMPEG(QObject *parent = 0);

private:
    uint8_t failToPlay(QString);
    void initHWDecoder(AVCodecContext *in_dec_ctx);
    void findHWDecoder(AVCodec *in_codec);
    uint8_t decoderInit();
    void decoderStop();
    uint16_t getFinalHeight(uint16_t);
    uint16_t getFinalWidth(uint16_t);
    bool hasWidthDoubling(uint16_t);
    bool initFrameConverter(AVFrame *new_frame, uint16_t new_width, uint16_t new_height, uint8_t new_colors);
    void freeFrameConverter();
    bool keepFrameInCheck(AVFrame *new_frame, uint16_t new_width, uint16_t new_height, uint8_t new_colors);
    bool waitForOutQueue(uint16_t);
    void outNewLine(VideoLine *);
    void spliceFrame(AVFrame *);
    void insertDummyFrame(uint32_t frame_cnt, bool last_frame = false, bool report = true);
    void insertNewFileLine();
    void insertFileEndLine(uint16_t line_number = 0);
    void insertFieldEndLine(uint16_t line_number = 0);
    void insertFrameEndLine(uint16_t line_number = 0);

public slots:
    void setLogLevel(uint8_t new_log_lvl = 0);
    void setOutputPointers(std::deque<VideoLine> *in_queue = NULL, QMutex *in_mtx = NULL);
    void setSourceLocation(QString);
    void setStepPlay(bool in_step = false);
    void setDropDetect(bool in_detect = false);
    void setFineSettings(vid_preset_t in_set);  // Set fine video processing settings.
    void setDefaultFineSettings();          // Set fine video processor settings to defaults.
    void requestCurrentFineSettings();      // Get current fine video processor settings.
    void runFrameDecode();                  // Main execution loop.
    void mediaPlay();
    void mediaPause();
    void mediaStop();
    void stop();

signals:
    void mediaPlaying(uint32_t);
    void mediaPaused();
    void mediaStopped();
    void mediaError(QString);
    void guiUpdFineSettings(vid_preset_t);  // New fine settings were applied.
    void newFrame(uint16_t, uint16_t);      // New frame started processing.
    void newLine(VideoLine);                // New line is processed.
    void frameDropDetected();
    void frameDecoded(uint32_t);
    void finished();
};

#endif // VIN_FFMPEG_H
