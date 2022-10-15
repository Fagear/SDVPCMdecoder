#ifndef FFMPEGWRAPPER_H
#define FFMPEGWRAPPER_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include <QImage>
#include <QObject>
#include <QThread>
#include <QString>
#include "config.h"
#include "vid_preset_t.h"

#define FFWR_WIN_GDI_NAME       (QObject::tr("Захват экрана Windows GDI"))
#define FFWR_WIN_GDI_CLASS      "gdigrab"
#define FFWR_WIN_GDI_CAP        "desktop"
#define FFWR_WIN_DSHOW_NAME     (QObject::tr("Захват экрана Windows DirectShow"))
#define FFWR_WIN_DSHOW_CLASS    "dshow"
#define FFWR_WIN_DSHOW_CAP      "screen-capture-recorder"
#define FFWR_LIN_X11_CLASS      "x11grab"
#define FFWRL_MAC_AVF_CLASS     "avfoundation"

//------------------------ Video capture device descriptor.
class VCapDevice
{
public:
    std::string dev_name;       // Device name for the user
    std::string dev_class;      // Device class (gdigrab, dshow, etc.)
    std::string dev_path;       // Device path in OS for [avformat_open_input()]

public:
    VCapDevice();
    VCapDevice(const VCapDevice &in_object);
    VCapDevice& operator= (const VCapDevice &in_object);
    void clear();
};

//------------------------ Video capture devices list.
class VCapList
{
public:
    std::vector<VCapDevice> dev_list;

public:
    VCapList();
    VCapList(const VCapList &in_object);
    VCapList& operator= (const VCapList &in_object);
    void clear();
};

//------------------------ Qt-wrapper for FFMPEG for capturing video.
class FFMPEGWrapper : public QObject
{
    Q_OBJECT

public:
    // Error codes for [sigVideoError()].
    enum
    {
        FFMERR_OK,                      // No error.
        FFMERR_EOF,                     // Last frame reached.
        FFMERR_NOT_INIT,                // Source was not opened before reading a frame.
        FFMERR_NO_SRC,                  // Failed to open source with set parameters.
        FFMERR_NO_INFO,                 // Failed to read any stream info.
        FFMERR_NO_VIDEO,                // Failed to find video stream in the source.
        FFMERR_NO_DECODER,              // Failed to find decoder for video stream.
        FFMERR_NO_RAM_DEC,              // Failed to allocate RAM for video decoder.
        FFMERR_DECODER_PARAM,           // Failed apply decoder parameters.
        FFMERR_DECODER_START,           // Failed to start video decoder.
        FFMERR_NO_RAM_FB,               // Failed to allocate RAM for frame buffer.
        FFMERR_NO_RAM_READ,             // Failed to read a packet from a decoder.
        FFMERR_DECODER_NR,              // Decoder was not ready for the packet.
        FFMERR_DECODER_REP,             // Not all frames were picked up from decoder.
        FFMERR_CONV_INIT,               // Failed to setup frame converter.
        FFMERR_FRM_CONV,                // Failed to convert to final frame.
        FFMERR_UNKNOWN                  // Some unknown error.
    };

    // Default capture dimensions.
    enum
    {
        DEF_CAP_WIDTH = 640,
        DEF_CAP_HEIGHT = 480
    };

    // Resolution for dummy frame.
    enum
    {
        DUMMY_CNT_MAX = 1024,
        DUMMY_WIDTH = 16,
        DUMMY_HEIGTH = 8
    };

    // Limits for enabling double width resise.
    enum
    {
        MIN_DBL_WIDTH = 10,
        MAX_DBL_WIDTH = 959
    };

    // Limit for cropping excess lines.
    enum
    {
        MAX_LINES_PER_FRAME = LINES_PER_FRAME_MAX
    };

    // Final frame buffer pixel format.
    enum
    {
        BUF_FMT_BW = AV_PIX_FMT_YUV420P,// Normal BW Y-decoded buffer.
        BUF_FMT_COLOR = AV_PIX_FMT_GBRP // Per channel GBR-decoded buffer.
    };

    // Color plane IDs.
    enum
    {
        PLANE_Y = 0,                    // Plane for Y level with AV_PIX_FMT_YUV420P pixel format.
        PLANE_G = 0,                    // Plane for GREEN color with AV_PIX_FMT_GBRP pixel format.
        PLANE_B = 1,                    // Plane for BLUE color with AV_PIX_FMT_GBRP pixel format.
        PLANE_R = 2                     // Plane for RED color with AV_PIX_FMT_GBRP pixel format.
    };

private:
    AVFormatContext *format_ctx;        // Context for FFMPEG format.
    const AVCodec *v_decoder;           // Codec structure.
    int stream_index;                   // Current open video stream index.
    AVCodecContext *video_dec_ctx;      // Context for FFMPEG decoder.
    AVFrame *dec_frame;                 // Frame holder for FFMPEG decoded frame.
    AVPacket dec_packet;                // Packet container for FFMPEG decoder.
    uint8_t *video_dst_data[4];         // Container for frame data from the decoder.
    int video_dst_linesize[4];          // Container for frame parameters from the decoder.
    SwsContext *conv_ctx;               // Context for FFMPEG frame converter.
    bool img_buf_free;                  // Final frame buffer was not allocated.
    bool source_open;                   // Source is open.
    AVPixelFormat last_frame_fmt;       // Last frame format.
    AVPixelFormat target_pixfmt;        // Pixel format for final frame (set in [initFrameConverter()]).
    uint32_t frame_count;
    bool file_capture;                  // Is source selected as file?
    bool detect_drops;                  // Detect droped frames via DTS.
    bool dts_detected;                  // Was DTS from new source already detected?
    int64_t last_dts;                   // Last frame DTS (for drop detection).
    bool last_double;                   // Last frame width was doubled.
    int last_width;                     // Last frame width.
    int last_height;                    // Last frame height.
    uint8_t last_color_mode;            // Last color mode (see [vid_preset_t.h]).
    vid_preset_t crop_n_color;          // Preset crop and color mode settings.
    uint16_t set_width;                 // Preset width of source for capture (0 = disabled).
    uint16_t set_height;                // Preset height of source for capture (0 = disabled).
    uint16_t set_x_ofs;                 // Preset x offset from left side (0 = disabled).
    uint16_t set_y_ofs;                 // Preset y offset from top side (0 = disabled).
    uint8_t set_fps;                    // Preset framerate of source (0 = disabled).

public:
    explicit FFMPEGWrapper(QObject *parent = 0);

private:
    bool getNextPacket();
    bool needsDoubleWidth(int in_width);
    int getFinalWidth(int in_width);
    bool initFrameConverter(AVFrame *new_frame, uint16_t new_width, uint16_t new_height, uint8_t new_colors);
    bool keepFrameInCheck(AVFrame *new_frame);
    void freeFrameConverter();

public slots:
    void slotLogStart();
    void slotGetDeviceList();
    void slotSetResize(uint16_t in_width = 0, uint16_t in_height = 0);
    void slotSetTLOffset(uint16_t in_x = 0, uint16_t in_y = 0);
    void slotSetFPS(uint8_t in_fps = 0);
    void slotSetCropColor(vid_preset_t in_preset);
    void slotSetDropDetector(bool in_drops);
    void slotOpenInput(QString path, QString class_type = "");
    void slotGetNextFrame();
    void slotCloseInput();

signals:
    void sigInputReady(int, int, uint32_t, float);
    void sigInputClosed();
    void sigVideoError(uint32_t);
    void newDeviceList(VCapList);
    void newImage(QImage, bool);
};

#endif // FFMPEGWRAPPER_H
