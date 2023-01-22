/**************************************************************************************************************************************************************
renderpcm.h

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

Created: 2021-01

Renderer for visualization.
This module represents various data containers as an image for visualization purposes.

**************************************************************************************************************************************************************/

#ifndef RENDERPCM_H
#define RENDERPCM_H

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QThread>
#include "frametrimset.h"
#include "pcm1datablock.h"
#include "pcm1datastitcher.h"
#include "pcm1line.h"
#include "pcm1subline.h"
#include "pcm16x0datablock.h"
#include "pcm16x0datastitcher.h"
#include "pcm16x0subline.h"
#include "stc007datablock.h"
#include "stc007datastitcher.h"
#include "stc007line.h"
#include "videoline.h"

// Colors of bits.
#define VIS_RED_TINT        (qRgb(255, 190, 180))   // Video with red tint.
#define VIS_GREEN_TINT      (qRgb(150, 210, 150))   // Video with green tint.
#define VIS_BLUE_TINT       (qRgb(170, 180, 240))   // Video with blue tint.
#define VIS_BIT0_BLK        (Qt::black)             // Bit "0", PCM with valid CRC in the line.
#define VIS_BIT0_GRY        (qRgb(45, 45, 45))      // Bit "0", PCM with valid CRC in the line for STC-007.
#define VIS_BIT1_GRY        (qRgb(150, 150, 150))   // Bit "1", PCM with valid CRC in the line.
#define VIS_BIT0_YEL        (qRgb(127, 110, 0))     // Bit "0", PCM with yellow tint.
#define VIS_BIT1_YEL        (qRgb(255, 220, 0))     // Bit "1", PCM with yellow tint.
#define VIS_BIT0_GRN        (qRgb(0, 95, 30))       // Bit "0", PCM with green tint.
#define VIS_BIT1_GRN        (qRgb(0, 225, 70))      // Bit "1", PCM with green tint.
#define VIS_BIT0_RED        (qRgb(140, 0, 0))       // Bit "0", PCM with red tint.
#define VIS_BIT1_RED        (qRgb(255, 70, 43))     // Bit "1", PCM with red tint.
#define VIS_BIT0_BLU        (qRgb(0, 95, 127))      // Bit "0", PCM with blue tint.
#define VIS_BIT1_BLU        (qRgb(0, 191, 255))     // Bit "1", PCM with blue tint.
#define VIS_BIT0_MGN        (qRgb(140, 0, 140))     // Bit "0", PCM with magenta tint.
#define VIS_BIT1_MGN        (qRgb(255, 0, 255))     // Bit "1", PCM with magenta tint.
#define VIS_BIT1_MARK       (qRgb(255, 255, 255))   // STC-007/PCM-F1 white marker line.
#define VIS_LIM_OK          (qRgb(255, 255, 255))   // Data boundaries for data blocks.
#define VIS_LIM_MARK        (qRgb(224, 170, 170))   // Data boundaries with some marker (almost silent data or data on the seam).

class RenderPCM : public QObject
{
    Q_OBJECT

public:
    // Minimum time between frames (ms) in live playback.
    enum
    {
        TIME_START = 75,        // Playback start (buffer-up).
        TIME_NTSC = 27,         // NTSC.
        TIME_PAL = 31           // PAL.
    };

    // PPBs (Pixels Per Bit) for visualization.
    enum
    {
        PPB_PCM1LINE = 8,       // PPB for [PCM1Line].
        PPB_PCM1SUBLINE = 8,    // PPB for [PCM1SubLine].
        PPB_PCM1BLK = 6,        // PPB for [PCM1DataBlock].
        PPB_PCM1600SUBLINE = 4, // PPB for [PCM16X0SubLine].
        PPB_PCM1600BLK = 6,     // PPB for [PCM16X0DataBlock].
        PPB_STC007LINE = 5,     // PPB for [STC007Line].
        PPB_STC007BLK = 6       // PPB for [STC007DataBlock].
    };

    enum
    {
        PPL_PCM1BLK = 4,                // Word-Pair Per Line for [PCM1DataBlock].
        WPL_PCM1BLK = (PPL_PCM1BLK*2)   // Words Per Line for [PCM1DataBlock].
    };

    enum
    {
        PACING_DEPTH = 30       // Depth of frame pacing buffer between render and drawer.
    };

public:
    explicit RenderPCM(QObject *parent = 0);
    ~RenderPCM();

private:
    void resetFrame();

private:
    QImage *img_data;           // Image to render to.
    QTimer *frame_period;       // Frame pacing timer.
    QList<QImage> pacing_buf;   // Output images queue.
    bool live_pb;               // Is live playback enabled? (requires frame pacing)
    uint8_t frame_time_lim;     // Time between frames in [ms] for frame pacing.
    uint8_t queue_size;         // Queue length between renderer and drawer.
    uint16_t fill_line_num;     // Current line for filling in the image.
    uint16_t provided_width;    // Width of the render canvas.
    uint16_t provided_heigth;   // Height of the render canvas.

public slots:
    void dumpThreadDebug();
    void setLivePlay(bool);
    void setFrameTime(uint8_t);
    void setLineCount(uint8_t);
    void resizeToFrame(uint16_t, uint16_t);
    void startNewFrame(uint16_t, uint16_t);
    void startVideoFrame();
    void startPCM1Frame();
    void startPCM1SubFrame();
    void startPCM1600Frame();
    void startSTC007NTSCFrame();
    void startSTC007PALFrame();
    void startPCM1DBFrame();
    void startPCM1600DBFrame();
    void startSTC007DBFrame();
    void prepareNewFrame(uint32_t);
    void finishNewFrame(uint32_t);
    void renderNewLine(VideoLine);
    void renderNewLineInOrder(VideoLine);
    void renderNewLine(PCM1Line);
    void renderNewLine(PCM1SubLine);
    void renderNewLine(PCM16X0SubLine);
    void renderNewLine(STC007Line);
    void renderNewBlock(PCM1DataBlock);
    void renderNewBlock(PCM16X0DataBlock);
    void renderNewBlock(STC007DataBlock);
    void displayIsReady();

private slots:
    void framePacing();

signals:
    void renderedFrame(QImage);
};

#endif // RENDERPCM_H
