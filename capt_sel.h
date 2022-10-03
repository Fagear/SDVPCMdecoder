#ifndef CAPT_SEL_H
#define CAPT_SEL_H

#include <QDebug>
#include <QDialog>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QListWidgetItem>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include "config.h"

#define CSEL_WIN_GDI_NAME       tr("Захват экрана Windows GDI")
#define CSEL_WIN_GDI_CLASS      "gdigrab"
#define CSEL_WIN_GDI_CAP        "desktop"
#define CSEL_WIN_DSHOW_NAME     tr("Захват экрана Windows DirectShow")
#define CSEL_WIN_DSHOW_CLASS    "dshow"
#define CSEL_WIN_DSHOW_CAP      "screen-capture-recorder"
#define CSEL_LIN_X11_CLASS      "x11grab"
#define CSEL_MAC_AVF_CLASS      "avfoundation"
#define CSEL_PREV_WIDTH         640
#define CSEL_PREV_HEIGTH        480
#define CSEL_PREV_PIXFMT        (AV_PIX_FMT_YUV420P)

namespace Ui {
class capt_sel;
}

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

class capt_sel : public QDialog
{
    Q_OBJECT

public:
    explicit capt_sel(QWidget *parent = 0);
    ~capt_sel();
    int exec();

private:
    void getVideoCaptureList();
    void resetFFMPEG();
    void getNextFrame();
    void disableOffset();
    void enableOffset();
    void clearPreview();
    void redrawCapture();

private:
    Ui::capt_sel *ui;
    QGraphicsScene *scene;
    QGraphicsPixmapItem *pixels;
    QPixmap preview_pix;
    QTimer capture_poll;                // Timer for polling opened capture device for new frames.
    AVFormatContext *format_ctx;        // Context for FFMPEG format.
    AVCodecContext *video_dec_ctx;      // Context for FFMPEG decoder.
    int stream_index;                   // Current open video stream index.
    AVFrame *dec_frame;                 // Frame holder for FFMPEG decoded frame.
    AVPacket dec_packet;                // Packet container for FFMPEG decoder.
    SwsContext *conv_ctx;               // Context for FFMPEG frame converter.
    bool img_buf_free;
    uint8_t *video_dst_data[4];         // Container for frame data from the decoder.
    int video_dst_linesize[4];          // Container for frame parameters from the decoder.

private slots:
    void setChange();
    void usrRefresh();
    void usrSave();
    void usrClose();
    void toggleDefaults();
    void selectDevice();
    void usrSetNTSC();
    void usrSetPAL();
    void refillDevList(VCapList);
    void pollCapture();
    void redrawPreview(QPixmap);

signals:
    void newDeviceList(VCapList);
    void newPreview(QPixmap);
};

#endif // CAPT_SEL_H
