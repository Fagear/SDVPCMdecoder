#ifndef CAPT_SEL_H
#define CAPT_SEL_H

#include <QDebug>
#include <QDialog>
#include <QElapsedTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include "config.h"
#include "ffmpegwrapper.h"

namespace Ui {
class capt_sel;
}

//------------------------ Capture device selection dialog.
class capt_sel : public QDialog
{
    Q_OBJECT

private:
    // Default preview resolution.
    enum
    {
        PV_WIDTH = 640,
        PV_HEIGHT = 480
    };

    // FFMPEG timeouts (in ms).
    enum
    {
        FMPG_TIME_DEV_LIST = 5000,      // Maximum time for device listing.
        FMPG_TIME_OPEN = 3000,          // Maximum time for device opening.
        FMPG_TIME_FRAME = 1500          // Maximum time for one frame capture.
    };

    // Dropout action list indexes for [lbxColorCh].
    enum
    {
        LIST_COLORS_ALL,
        LIST_COLOR_R,
        LIST_COLOR_G,
        LIST_COLOR_B,
    };

private:
    Ui::capt_sel *ui;
    QGraphicsScene *scene;
    QGraphicsPixmapItem *pixels;
    QPixmap preview_pix;                // Blank screen for preview.
    QThread *ffmpeg_thread;             // Thread for FFMPEG wrapper.
    FFMPEGWrapper *capt_dev;            // FFMPEG Qt-wrapper.
    QTimer capture_poll;                // Timer for polling opened capture device for new frames.
    QTimer capt_busy;                   // Timer for measuring FFMPEG response.
    QElapsedTimer capt_meas;            // Timer for measuring capture rate.
    bool img_buf_free;
    uint8_t capture_rate;

public:
    explicit capt_sel(QWidget *parent = 0);
    ~capt_sel();
    int exec();

private:
    void disableOffset();
    void enableOffset();
    void stopCapture();
    void stopPreview();

private slots:
    void usrRefresh();
    void usrSave();
    void usrClose();
    void toggleDefaults();
    void selectDevice();
    void usrSetNTSC();
    void usrSetPAL();
    void pollCapture();
    void lostFFMPEG();
    void refillDevList(VCapList);
    void captureReady(int in_width, int in_height, uint32_t in_frames, float in_fps);
    void captureClosed();
    void captureError(uint32_t);
    void redrawPreview(QImage, bool);

signals:
    void requestDropDet(bool);                  // Set droped frames detector.
    void requestColor(vid_preset_t);            // Set color channel.
    void requestResize(uint16_t, uint16_t);     // Set capture dimensions.
    void requestFPS(uint8_t);                   // Set capture framerate.
    void requestOffset(uint16_t, uint16_t);     // Set screencap offset from top left corner.
    void requestDeviceList();                   // Request video capture device list.
    void openDevice(QString, QString);          // Request opening file/device as video source.
    void closeDevice();                         // Request closing capture.
    void requestFrame();                        // Request next frame from the capture.
};

#endif // CAPT_SEL_H
