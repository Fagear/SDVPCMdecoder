﻿/**************************************************************************************************************************************************************
mainwindow.h

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

Created: 2020-04

**************************************************************************************************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <deque>
#include <string>
#include <thread>
#include <queue>
#ifdef _WIN32
    #include "windows.h"
#endif
#include <QObject>
#include <QFileInfo>
#include <QMainWindow>
#include <QMutex>
#include <QPoint>
#include <QRect>
#include <QSettings>
#include <QString>
//#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QWinTaskbarProgress>
#include <QtWidgets>
#include "about_wnd.h"
#include "audioprocessor.h"
#include "binarizer.h"
#include "capt_sel.h"
#include "circbuffer.h"
#include "config.h"
#include "fine_bin_set.h"
#include "fine_deint_set.h"
#include "fine_vidin_set.h"
#include "frame_vis.h"
#include "frametrimset.h"
#include "pcm16x0datastitcher.h"
#include "pcm16x0subline.h"
#include "pcm1datastitcher.h"
#include "pcm1line.h"
#include "pcmline.h"
#include "stc007datastitcher.h"
#include "stc007datablock.h"
#include "stc007deinterleaver.h"
#include "stc007line.h"
#include "pcmsamplepair.h"
#include "pcmtester.h"
#include "samples2wav.h"
#include "videotodigital.h"
#include "renderpcm.h"
#include "videoline.h"
#include "vin_ffmpeg.h"

// TODO: add support for capture devices through FFMPEG
#define DBG_AVG_LEN         1
#define TRACKING_BUF_LEN    30

// Locales.
#define PCM_LCL_DEFAULT     ("ru")      // Default locale of the software.
#define PCM_LCL_FALLBACK    ("en")      // Fallback locale for other languages than Russian if translation file was not found.

#define LIST_PCM16X_SI      (QObject::tr("Формат SI"))
#define LIST_PCM16X_EI      (QObject::tr("Формат EI"))

#define LIST_ORDER_UNK      (QObject::tr("не определён"))
#define LIST_ORDER_TFF      (QObject::tr("TFF, нечётное поле первое"))
#define LIST_ORDER_BFF      (QObject::tr("BFF, чётное поле первое"))
#define LIST_ORDER_FORCE    (QObject::tr(" (форсировано)"))

#define LIST_VIDSTD_UNK     (QObject::tr("не определён"))
#define LIST_VIDSTD_NTSC    (QObject::tr("NTSC (525i), 245 строк с PCM в поле"))
#define LIST_VIDSTD_PAL     (QObject::tr("PAL (625i), 294 строк с PCM в поле"))
#define LIST_VIDSTD_FORCE   (QObject::tr(" (форсирован)"))

#define LIST_14BIT_AUD      (QObject::tr("14 бит (STC-007)"))
#define LIST_14AUTO_AUD     (QObject::tr("14 бит (неточно)"))
#define LIST_16AUTO_AUD     (QObject::tr("16 бит (неточно)"))
#define LIST_16BIT_AUD      (QObject::tr("16 бит (PCM-F1)"))

// Vizualizer window titles.
#define TITLE_RENDER_SOURCE (QObject::tr("Источник видео"))
#define TITLE_RENDER_BIN    (QObject::tr("Бинаризированные кадры"))
#define TITLE_RENDER_REASM  (QObject::tr("Пересобранные кадры"))
#define TITLE_RENDER_BLOCKS (QObject::tr("Блоки данных"))

#define DEBUG_MENU_OFF_HINT (QObject::tr("Для использования меню отладки запустите приложение из коммандной строки"))

#define WL_VERSION              "1.0"
#define WL_HEADER               QString(APP_EXEC+" v"+APP_VERSION+", work log v"+WL_VERSION)
#define WL_SOURCE               QString("Source loaded: ")
#define WL_PCM1_HELP_SIZE       20
#define WL_PCM16X0_HELP_SIZE    23
#define WL_STC007_HELP_SIZE     26

static const std::string WL_PCM1_HELP[WL_PCM1_HELP_SIZE]
{
    "                  Horizontal coordinates state: [A] = auto-detected OK, [G] = guessed by stats <      > Field order: [TFF] = top (odd) field first, [BFF] = bottom (even) field first",
    "                        Average (by frame) data stop horizontal data coordinate (in pixels) <  |      |  > Field order state: [F] = forced by user",
    "                   Average (by frame) data start horizontal data coordinate (in pixels) <   |  |      |  |",
    "                                             Width of the source frame (in pixels) <    |   |  |      |  |      > Average (by frame) odd field reference brightness level (0...255)",
    "                                                                                   |    |   |  |      |  |      |   > Average (by frame) even field reference brightness level (0...255)",
    "                                         Even field data stop line number <        |    |   |  |      |  |      |   |",
    "                                    Even field data start line number <   |        |    |   |  |      |  |      |   |       > Odd field top padding (in lines)",
    "                              Odd field data stop line number <       |   |        |    |   |  |      |  |      |   |       |   > Odd field bottom padding (in lines)",
    "                         Odd field data start line number <   |       |   |        |    |   |  |      |  |      |   |       |   |       > Even field top padding (in lines)",
    "                                                          |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   > Even field bottom padding (in lines)",
    "                  Even field lines with valid CRC <       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |",
    "                   Even field lines with data <   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     > Total data block count (1 data block = 184 or 182 audio samples)",
    "                   Even field total lines <   |   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     |  > Data blocks with data corrected by Bit Picker (errors caused by frame sides cropping)",
    "   Odd field lines with valid CRC <       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     |  |  > Data blocks with dropped/uncorrected samples (actual error count)",
    "    Odd field lines with data <   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     |  |  |",
    "    Odd field total lines <   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     |  |  |        > Audio sample rate for odd field",
    "                          |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     |  |  |        |     > Audio sample rate for even field",
    "    Video standard <      |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     |  |  |        |     |    > Emphasis presence for odd field",
    "Frame number <     |      |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |       |   |       |   |     |  |  |        |     |    | > Emphasis presence for even field",
    "        _____|  ___|    __| __| __|     __| __| __|     __| __|     __| __|      __|  __| __|  |    __|  |    __| __|     __| __|     __| __|    _| _| _|    ____| ____|    | |",
};

static const std::string WL_PCM16x0_HELP[WL_PCM16X0_HELP_SIZE]
{
    "                                                                    Field order state: [F] = forced by user <      > Average (by frame) odd field reference brightness level (0...255)",
    "                           Field order: [TFF] = top (odd) field first, [BFF] = bottom (even) field first <  |      |   > Average (by frame) even field reference brightness level (0...255)",
    "                                                                                                         |  |      |   |",
    "                     Horizontal coordinates state: [A] = auto-detected OK, [G] = guessed by stats <      |  |      |   |     > Padding state: [OK] = auto-detected OK, [SL] = too much silence to detect, [BD] = failed to detect",
    "                           Average (by frame) data stop horizontal data coordinate (in pixels) <  |      |  |      |   |     |    > Odd field top padding (in lines)",
    "                      Average (by frame) data start horizontal data coordinate (in pixels) <   |  |      |  |      |   |     |    |   > Odd field bottom padding (in lines)",
    "                                                Width of the source frame (in pixels) <    |   |  |      |  |      |   |     |    |   |    > Even field top padding (in lines)",
    "                                                                                      |    |   |  |      |  |      |   |     |    |   |    |   > Even field bottom padding (in lines)",
    "                                            Even field data stop line number <        |    |   |  |      |  |      |   |     |    |   |    |   |",
    "                                       Even field data start line number <   |        |    |   |  |      |  |      |   |     |    |   |    |   |       > Total data sub-block count (1 data block = 3 data sub-blocks, 1 sub-block = 2 audio samples)",
    "                                 Odd field data stop line number <       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    > Data sub-blocks with data corrected by Bit Picker (errors caused by frame sides cropping)",
    "                            Odd field data start line number <   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    > Data sub-blocks with data corrected by P-correction (1 error per data sub-block)",
    "                                                             |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    > Data sub-blocks with data corrected with CWD assistance",
    "                     Even field lines with valid CRC <       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    > Data sub-blocks with BROKEN data",
    "                      Even field lines with data <   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    > Data sub-blocks with dropped/uncorrected samples (actual error count)",
    "                      Even field total lines <   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    |",
    "      Odd field lines with valid CRC <       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    |     > PCM-1630 data format: [SI] = SI format, [EI] = EI format",
    "       Odd field lines with data <   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    |     |",
    "       Odd field total lines <   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    |     |        > Audio sample rate for odd field",
    "                             |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    |     |        |     > Audio sample rate for even field",
    "       Video standard <      |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    |     |        |     |    > Emphasis presence for odd field",
    "   Frame number <     |      |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |   |       |    |    |    |    |    |     |        |     |    | > Emphasis presence for even field",
    "           _____|  ___|    __| __| __|     __| __| __|     __| __|     __| __|      __|  __| __|  |    __|  |    __| __|    _|  __| __|  __| __|    ___| ___| ___| ___| ___| ___|    _|    ____| ____|    | |",
};

static const std::string WL_STC007_HELP[WL_STC007_HELP_SIZE]
{
    "                                                   Average (by frame) even field reference brightness level (0...255) <     > Inter-frame padding state: [OK] = auto-detected OK, [SL] = too much silence to detect, [BD] = failed to detect",
    "                                                Average (by frame) odd field reference brightness level (0...255) <   |     |    > Inter-frame padding size (in lines)",
    "                                                                                                                  |   |     |    |   > Frame-to-next padding state: [OK] = auto-detected OK, [SL] = too much silence to detect, [BD] = failed to detect",
    "                   Field order state: [A] = auto-detected OK, [F] = forced by user, [G] = guessed by stats <      |   |     |    |   |    > Frame-to-next padding size (in lines)",
    "                          Field order: [TFF] = top (odd) field first, [BFF] = bottom (even) field first <  |      |   |     |    |   |    |",
    "                                                                                                        |  |      |   |     |    |   |    |      > Total data block count (1 data block = 6 audio samples)",
    "                    Horizontal coordinates state: [A] = auto-detected OK, [G] = guessed by stats <      |  |      |   |     |    |   |    |      |   > Data blocks with data corrected by P-correction (1 error per data block)",
    "                          Average (by frame) data stop horizontal data coordinate (in pixels) <  |      |  |      |   |     |    |   |    |      |   |   > Data blocks with data corrected by Q-correction (2 errors per data block)",
    "                     Average (by frame) data start horizontal data coordinate (in pixels) <   |  |      |  |      |   |     |    |   |    |      |   |   |   > Data blocks with data corrected with CWD assistance",
    "                                               Width of the source frame (in pixels) <    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   > Data blocks with BROKEN data",
    "                                                                                     |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   > Data blocks with dropped/uncorrected samples (actual error count)",
    "                                           Even field data stop line number <        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |",
    "                                      Even field data start line number <   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        > Audio sample resolution for odd field: [14bit] = STC-007, [16bit] = PCM-F1",
    "                                Odd field data stop line number <       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     > Audio sample resolution for even field: [14bit] = STC-007, [16bit] = PCM-F1",
    "                           Odd field data start line number <   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |",
    "                                                            |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     > Timestamp program index (0...63)",
    "                    Even field lines with valid CRC <       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   > Timestamp hours (0...15)",
    "                     Even field lines with data <   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  > Timestamp minutes (0...59)",
    "                     Even field total lines <   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  |  > Timestamp seconds (0...59)",
    "     Odd field lines with valid CRC <       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  |  |  > Timestamp field index (0...59)",
    "      Odd field lines with data <   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  |  |  |",
    "      Odd field total lines <   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  |  |  |        > Audio sample rate for odd field",
    "                            |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  |  |  |        |     > Audio sample rate for even field",
    "      Video standard <      |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  |  |  |        |     |    > Emphasis presence for odd field",
    "  Frame number <     |      |   |   |       |   |   |       |   |       |   |        |    |   |  |      |  |      |   |     |    |   |    |      |   |   |   |   |   |        |     |     |   |  |  |  |        |     |    | > Emphasis presence for even field",
    "          _____|  ___|    __| __| __|     __| __| __|     __| __|     __| __|      __|  __| __|  |    __|  |    __| __|    _|  __|  _|  __|    __| __| __| __| __| __|    ____| ____|    _|  _| _| _| _|    ____| ____|    | |",
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum
    {
        LOG_SETTINGS = (1<<0),  // External operations with settings.
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_LINE_DUMP = (1<<2), // Dump PCM lines while adjusting trimming and padding.
        LOG_BLOCK_DUMP = (1<<3) // Dump PCM data blocks while adjusting padding.
    };

    // Decoder states.
    enum
    {
        VDEC_IDLE,
        VDEC_STOP,
        VDEC_PLAY,
        VDEC_PAUSE
    };

    // Timers.
    enum
    {
        TIM_WIN_POS_INT = 500,      // Window position/size update pause (ms).
        TIM_GUI_INT = 20,           // GUI update period (ms)
        TIM_FLAG_GREEN_CNT = 15,    // Number of GUI timer ticks to turn off green flags.
        TIM_FLAG_RED_CNT = 60,      // Number of GUI timer ticks to turn off red/yellow flags.
    };

    // Binarization quality indexes for [lbxBinQuality].
    enum
    {
        LIST_BQ_INSANE,
        LIST_BQ_NORMAL,
        LIST_BQ_FAST,
        LIST_BQ_DRAFT
    };

    // PCM type list indexes for [lbxPCMType], [stcDecoderSettings], [stcFrameAsm].
    enum
    {
        LIST_TYPE_PCM1,
        LIST_TYPE_PCM16X0,
        LIST_TYPE_STC007,
        LIST_TYPE_M2
    };

    // Field order list indexes for [lbxPCM1FieldOrder].
    enum
    {
        LIST_PCM1_FO_TFF,
        LIST_PCM1_FO_BFF
    };

    // PCM-1630 formats for [lbxPCM16x0Format].
    enum
    {
        LIST_PCM16X0_FMT_AUTO,
        LIST_PCM16X0_FMT_SI,
        LIST_PCM16X0_FMT_EI
    };

    // Field order list indexes for [lbxPCM16x0FieldOrder].
    enum
    {
        LIST_PCM16X0_FO_TFF,
        LIST_PCM16X0_FO_BFF
    };

    // Enabled error correction stages for [lbxPCM16x0ECC].
    enum
    {
        LIST_PCM16X0_ECC_PARITY,
        LIST_PCM16X0_ECC_NONE
    };

    // Sample rate list indexes for [lbxPCM16x0SampleRate].
    enum
    {
        LIST_PCM16X0_SRATE_AUTO,
        LIST_PCM16X0_SRATE_44056,
        LIST_PCM16X0_SRATE_44100
    };

    // Video standard list indexes for [lbxSTC007VidStandard].
    enum
    {
        LIST_STC007_VID_AUTO,
        LIST_STC007_VID_NTSC,
        LIST_STC007_VID_PAL,
    };

    // Field order list indexes for [lbxSTC007FieldOrder].
    enum
    {
        LIST_STC007_FO_AUTO,
        LIST_STC007_FO_TFF,
        LIST_STC007_FO_BFF
    };

    // Enabled error correction stages for [lbxSTC007ECC].
    enum
    {
        LIST_STC007_ECC_FULL,
        LIST_STC007_ECC_PARITY,
        LIST_STC007_ECC_NONE
    };

    // Additional error-correction mode for [lbxSTC007CWD].
    enum
    {
        LIST_STC007_CWD_EN,
        LIST_STC007_CWD_DIS
    };

    // Resolution list indexes for [lbxSTC007Resolution].
    enum
    {
        LIST_STC007_RES_AUTO,
        LIST_STC007_RES_14BIT,
        LIST_STC007_RES_16BIT
    };

    // Sample rate list indexes for [lbxSTC007SampleRate].
    enum
    {
        LIST_STC007_SRATE_AUTO,
        LIST_STC007_SRATE_44056,
        LIST_STC007_SRATE_44100
    };

    // Video standard list indexes for [lbxM2VidStandard].
    enum
    {
        LIST_M2_VID_AUTO,
        LIST_M2_VID_NTSC,
        LIST_M2_VID_PAL,
    };

    // Field order list indexes for [lbxM2FieldOrder].
    enum
    {
        LIST_M2_FO_AUTO,
        LIST_M2_FO_TFF,
        LIST_M2_FO_BFF
    };

    // Enabled error correction stages for [lbxM2ECC].
    enum
    {
        LIST_M2_ECC_FULL,
        LIST_M2_ECC_PARITY,
        LIST_M2_ECC_NONE
    };

    // Additional error-correction mode for [lbxM2CWD].
    enum
    {
        LIST_M2_CWD_EN,
        LIST_M2_CWD_DIS
    };

    // Sample rate list indexes for [lbxM2SampleRate].
    enum
    {
        LIST_M2_SRATE_AUTO,
        LIST_M2_SRATE_44056,
        LIST_M2_SRATE_44100
    };

    // Dropout action list indexes for [lbxDropAction].
    enum
    {
        LIST_DOA_INTER_WORD,
        LIST_DOA_INTER_BLOCK,
        LIST_DOA_HOLD_WORD,
        LIST_DOA_HOLD_BLOCK,
        LIST_DOA_MUTE_WORD,
        LIST_DOA_MUTE_BLOCK,
        LIST_DOA_SKIP
    };

private:
    Ui::MainWindow *ui;
    QTranslator trUI;
    VideoInFFMPEG *VIN_worker;              // Video input processor (captures, decodes video and slices frames into lines).
    VideoToDigital *V2D_worker;             // Video to digital converter, performes binarization and data stabilization.
    PCM1DataStitcher *L2B_PCM1_worker;
    PCM16X0DataStitcher *L2B_PCM16X0_worker;
    STC007DataStitcher *L2B_STC007_worker;
    AudioProcessor *AP_worker;
    frame_vis *visuSource;
    frame_vis *visuBin;
    frame_vis *visuAssembled;
    frame_vis *visuBlocks;
    QTimer timResizeUpd;                    // Timer for waiting for user-input while resizing/moving window.
    QTimer timUIUpdate;                     // Timer for GUI updating.
    QTimer timTRDUpdate;                    // Timer for thread checking.
    QThread *input_FPU;                     // Frame processing unit thread.
    QThread *conv_V2D;                      // Video to PCM lines converter thread.
    QThread *conv_L2B_PCM1;                 // Lines to PCM-1 data blocks converter thread.
    QThread *conv_L2B_PCM16X0;              // Lines to PCM-16x0 data blocks converter thread.
    QThread *conv_L2B_STC007;               // Lines to STC-007 data blocks converter thread.
    QThread *audio_PU;                      // Audio processor thread.
    // Queues for data transfer and buffering between threads/modules.
    std::deque<VideoLine> video_lines;      // Queue for video lines from frame (in the same order as in frame, deinterlaced).
    std::deque<PCM1Line> pcm1_lines;        // Queue for PCM lines, binarized from video lines (in the same strict order).
    std::deque<PCM16X0SubLine> pcm16x0_sublines;  // Queue for PCM lines, binarized from video lines (in the same strict order).
    std::deque<STC007Line> stc007_lines;    // Queue for PCM lines, binarized from video lines (in the same strict order).
    std::deque<PCMSamplePair> audio_data;   // Queue for PCM sample pair, picked from assembled PCM data blocks.
    QMutex vl_lock;                         // Protects [video_lines].
    QMutex pcm1line_lock;                   // Protects [pcm1_lines].
    QMutex pcm16x0subline_lock;             // Protects [pcm16x0subline].
    QMutex stcline_lock;                    // Protects [stc007_lines].
    QMutex audio_lock;                      // Protects [audio_data].
    QPalette plt_redlabel;                  // Red indicator palette.
    QPalette plt_yellowlabel;               // Yellow indicator palette.
    QPalette plt_greenlabel;                // Green indicator palette.

    bool shutdown_started;                  // Application exit procedure started.
    bool inhibit_setting_save;              // Disable updating settings.
    bool src_draw_deint;                    // Draw source video deinterlaced or not.
    uint8_t log_level;                      // Setting for debugging log level.
    uint8_t v_decoder_state;                // State of video decoder.
    uint8_t set_pcm_type;                   // PCM type set from UI.
    int pcm1_ofs_diff;

    FrameAsmPCM1 frame_asm_pcm1;            // PCM-1 frame assembling data for GUI.
    FrameAsmPCM16x0 frame_asm_pcm16x0;      // PCM-16x0 frame assembling data for GUI.
    FrameAsmSTC007 frame_asm_stc007;        // STC-007 frame assembling data for GUI.
    FrameAsmSTC007 frame_asm_m2;            // M2 frame assembling data for GUI.

    // Internal statistics.
    uint8_t stat_dbg_index;
    quint64 stat_debug_avg[DBG_AVG_LEN];
    uint8_t stat_tracking_index;
    circarray<FrameBinDescriptor, TRACKING_BUF_LEN> stat_tracking_arr;
    FrameBinDescriptor stat_video_tracking;
    uint16_t flag_bad_stitch_cnt;
    uint16_t flag_p_corr_cnt;
    uint16_t flag_q_corr_cnt;
    uint16_t flag_cwd_corr_cnt;
    uint16_t flag_broken_cnt;
    uint16_t flag_dropout_cnt;
    uint16_t stat_lines_per_frame;
    uint64_t stat_blocks_time_per_frame;
    uint32_t stat_min_di_time;
    uint32_t stat_max_di_time;
    uint16_t stat_blocks_per_frame;
    uint8_t stat_ref_level;
    uint32_t stat_total_frame_cnt;
    uint32_t stat_read_frame_cnt;
    uint32_t stat_drop_frame_cnt;
    uint32_t stat_no_pcm_cnt;
    uint32_t stat_crc_err_cnt;
    uint32_t stat_dup_err_cnt;
    uint32_t stat_bad_stitch_cnt;
    uint32_t stat_p_fix_cnt;
    uint32_t stat_q_fix_cnt;
    uint32_t stat_cwd_fix_cnt;
    uint32_t stat_broken_block_cnt;
    uint32_t stat_drop_block_cnt;
    uint32_t stat_drop_sample_cnt;
    uint32_t stat_mask_cnt;
    uint32_t stat_processed_frame_cnt;
    uint32_t stat_line_cnt;
    uint8_t vu_left;                        // Left audio channel level.
    uint8_t vu_right;                       // Right audio channel level.

    uint8_t wl_pcm1_help_idx;               // Help string index for PCM-1 work log.
    uint8_t wl_pcm16x0_help_idx;            // Help string index for PCM-16x0 work log.
    uint8_t wl_stc007_help_idx;             // Help string index for STC-007 work log.

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    void moveEvent(QMoveEvent *event);      // Main window was moved.
    void resizeEvent(QResizeEvent *event);  // Main window was resized.
    void closeEvent(QCloseEvent *event);    // Application is about to close.

    QString generateTranslationPath(QString in_locale);
    QStringList getTranslationList();
    void updateGUILangList();
    void setGUILanguage(QString in_locale, bool suppress = false);      // Select UI language.
    // Update settings for modules.
    void setVIPOptions();                   // Set options for [VideoInFFMPEG] module.
    void setLBOptions();                    // Set options for [VideoToDigital] and [Binarizer] modules.
    void setDIOptions();                    // Set options for [xDataStitcher] and [xDeinterleaver] modules.
    void setAPOptions();                    // Set options for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.
    // Update logging settings.
    void setMainLogMode();                  // Set debug logging mode for main module.
    void setVIPLogMode();                   // Set debug logging mode for [VideoInFFMPEG] module.
    void setLBLogMode();                    // Set debug logging mode for [VideoToDigital] and [Binarizer] modules.
    void setDILogMode();                    // Set debug logging mode for [xDataStitcher] and [xDeinterleaver] modules.
    void setAPLogMode();                    // Set debug logging mode for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.

    void disableGUIEvents();                // Disable GUI events to prevent switching settings to default while changing translation.
    void enableGUIEvents();                 // Re-enable GUI events after changing translation.
    void applyGUISettings();                // Apply GUI settings to different modules.
    void readGUISettings();                 // Read settings into GUI.

    FrameBinDescriptor getBinDescriptorByFrameNo(uint32_t);     // Find and return coordinates from video tracking history.

private slots:
    //void dbgSlot(int);

    void exitAction();                      // Exit application.
    void setLang(QAction *);                // User request to set GUI language.
    void updateWindowPosition();            // Save new main window position and size in setting storage.
    void displayErrorMessage(QString);      // Display window with an error, stop playback.

    void loadVideo();                       // User request to open a new source.
    void unloadSource();                    // User request to close the source and free resources.
    void usrPlayStop();                     // User request to start/stop video playback/decode.
    void usrPause();                        // User request to pause video playback/decode.

    // GUI selection reactions.
    void updateGUISettings();               // Save settings for GUI options.
    void clearStat();                       // Clear decoder stats.
    void sliderDisplayUpdate();             // Update PCM-1 vertical offset indicators.

    // Main menu reactions.
    void resetOptDecoder();                 // Confirm and reset decoder settings.
    void resetVisPositions();               // Confirm and reset visualizer windows positions.
    void resetFull();                       // Confirm and reset all settings.
    void showAbout();                       // Display "About" window.
    void showCaptureSelector();             // Display video capture selection dialog.
    void showVidInFineSettings();           // Display video processor fine settings dialog.
    void showBinFineSettings();             // Display binarizator fine settings dialog.
    void showDeintFineSettings();           // Display deinterleaver fine settings dialog.
    void setDefaultFineSettings();          // Receive request for fine settings reset from video processor fine settings dialog.
    void requestCurrentFineSettings();      // Receive request for fine settings from video processor fine settings dialog.
    void setFineDrawDeint(bool);            // Save new fine setting for source drawing deinterlacing.

    void showVisSource(bool);               // Display source visualization window.
    void showVisBin(bool);                  // Display binarized visualization window.
    void showVisAssembled(bool);            // Display re-assembled visualization window.
    void showVisBlocks(bool);               // Display data block visualization window.
    void hideVisSource();                   // Hide source visualization window.
    void hideVisBin();                      // Hide binarized visualization window.
    void hideVisAssembled();                // Hide re-assembled visualization window.
    void hideVisBlocks();                   // Hide data block visualization window.
    void reopenVisSource();                 // Re-open source visualization window.
    void reopenVisualizers();               // Re-open all vizualization windows.

    void updateSetMainLog();                // Save settings for logging mode for main module.
    void updateSetVIPLog();                 // Save settings for [VideoInFFMPEG] module.
    void updateSetLBLog();                  // Save settings for [VideoToDigital] and [Binarizer] modules.
    void updateSetDILog();                  // Save settings for [xDataStitcher] and [xDeinterleaver] modules.
    void updateSetAPLog();                  // Save settings for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.
    void clearMainPLog();                   // Turn off debug logging for main module.
    void clearVIPLog();                     // Turn off debug logging for [VideoInFFMPEG] module.
    void clearLBLog();                      // Turn off debug logging for [VideoToDigital] and [stc007binarizer] modules.
    void clearDILog();                      // Turn off debug logging for [xDataStitcher] and [xDeinterleaver] modules.
    void clearAPLog();                      // Turn off debug logging for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.
    void clearAllLogging();                 // Turn off all debug logging.

    // Player state reactions.
    void playerNoSource();                  // React on video decoder asking for a source.
    void playerLoaded(QString);             // React on video decoder loading a new source.
    void playerStarted(uint32_t);           // React on video decoder starting playback.
    void playerStopped();                   // React on video decoder stopping playback.
    void playerPaused();                    // React on video decoder pausing playback.
    void playerError(QString);              // React on video decoder error.
    void livePBUpdate(bool);                // React on live playback state.

    void updateGUIByTimer();                // Update GUI counters and bars with data.
    void updatePCM1FrameData();             // Update PCM-1 frame assembling data.
    void updatePCM16x0FrameData();          // Update PCM-16x0 frame assembling data.
    void updateSTC007FrameData();           // Update STC-007 frame assembling data.
    void updateM2FrameData();               // Update M2 frame assembling data.

    // Buffered receivers for visualizers.
    void receiveBinLine(PCM1Line);          // Receive and retransmit binarized PCM-1 line.
    void receiveBinLine(PCM16X0SubLine);    // Receive and retransmit binarized PCM-16x0 sub-line.
    void receiveBinLine(STC007Line);        // Receive and retransmit binarized STC-007 line.
    void receiveAsmLine(PCM1SubLine);       // Receive and retransmit PCM-1 sub-line from re-assembled frame.
    void receiveAsmLine(PCM16X0SubLine);    // Receive and retransmit PCM-16x0 sub-line from re-assembled frame.
    void receiveAsmLine(STC007Line);        // Receive and retransmit STC-007 line from re-assembled frame.
    void receivePCMDataBlock(PCM1DataBlock);            // Receive and retransmit PCM-1 data block after deinterleave.
    void receivePCMDataBlock(PCM16X0DataBlock);         // Receive and retransmit PCM-16x0 data block after deinterleave.
    void receivePCMDataBlock(STC007DataBlock);          // Receive and retransmit STC-007 data block after deinterleave.
    void receiveVUMeters(uint8_t, uint8_t); // Receive VU levels for displaying.

    // Stats gathering reactions.
    void updateDebugBar(quint64);
    void updateStatsVIPFrame(uint32_t);     // Update stats after VIP has read a frame.
    void updateStatsVideoTracking(FrameBinDescriptor);  // Update stats after VIP has spliced a frame.
    void updateStatsDroppedFrame();         // Update stats with new value for dropped frames.
    void resetWorkLogHelpPCM1();
    void resetWorkLogHelpPCM16X0();
    void resetWorkLogHelpSTC007();
    QString logHelpNextPCM1();
    QString logHelpNextPCM16X0();
    QString logHelpNextSTC007();
    QString logStatsFrameAsm(FrameAsmPCM1);   // Make a user-log string for the frame assembly data.
    QString logStatsFrameAsm(FrameAsmPCM16x0);   // Make a user-log string for the frame assembly data.
    QString logStatsFrameAsm(FrameAsmSTC007);   // Make a user-log string for the frame assembly data.
    void updateStatsFrameAsm(FrameAsmPCM1);     // Update stats and video processor with new trim settings.
    void updateStatsFrameAsm(FrameAsmPCM16x0);  // Update stats and video processor with new trim settings.
    void updateStatsFrameAsm(FrameAsmSTC007);   // Update stats and video processor with new trim settings.
    void updateStatsMaskes(uint16_t);       // Update stats with new value for masked samples.
    void updateStatsBlockTime(STC007DataBlock); // Update stats after DI has finished a data block and provided spent time count.
    void updateStatsDIFrame(uint32_t);      // Update stats after DI has finished a frame.

    // Self-test start and result reactions.
    void testStartCRCC();                   // Perform internal test of CRCC within PCM line.
    void testStartECC();                    // Perform internal test of ECC within PCM data block.
    void testCRCCPassed();                  // CRCC test finished with PASSED result.
    void testCRCCFailed();                  // CRCC test finished with FAILED result.
    void testECCPassed();                   // ECC test finished with PASSED result.
    void testECCFailed();                   // ECC test finished with FAILED result.
    void testCRCCCleanup();                 // CRCC test has finished, perform housekeeping.
    void testECCCleanup();                  // ECC test has finished, perform housekeeping.

signals:
    // GUI stuff.
    void aboutToExit();                     // Application is about to close.
    void newTargetPath(QString);            // Full path for new source.
    // Signals for video input processor.
    void newVIPLogLevel(uint8_t);           // Send new logging level for VIP thread.
    void newFrameDropDetection(bool);       // Send new "frame drop detection" setting for VIP thread.
    void newStepPlay(bool);                 // Send new "stepped play" setting for VIP thread.
    // Signals for binarizer (video-to-digital) module.
    void newV2DLogLevel(uint8_t);           // Send new logging level for V2D thread.
    void newBinMode(uint8_t);               // Send new "binarization mode" setting for V2D.
    void newBinPCMType(uint8_t);            // Send new "PCM type" setting for V2D.
    void newLineDupMode(bool);              // Send new "line duplication detection" setting for V2D.
    // Signals for deinterleaver (line-to-block) module.
    void newL2BLogLevel(uint16_t);          // Send new logging level for L2B thread.
    void newPCM1FieldOrder(uint8_t);        // Send new PCM-1 field order setting.
    void newPCM1AutoOffset(bool);           // Send new PCM-1 auto offset setting.
    void newPCM1OddOffset(int8_t);          // Send new PCM-1 odd line offset setting.
    void newPCM1EvenOffset(int8_t);         // Send new PCM-1 even line offset setting.
    void newPCM16x0Format(uint8_t);         // Send new PCM-1630 format setting.
    void newPCM16x0FieldOrder(uint8_t);     // Send new PCM-16x0 field order setting.
    void newPCM16x0PCorrection(bool);       // Send new PCM-16x0 P-code correction setting.
    void newPCM16x0SampleRatePreset(uint16_t);  // Send new PCM-16x0 "sample rate" for L2B thread.
    void newSTC007VidStandard(uint8_t);     // Send new STC-007 video standard setting.
    void newSTC007FieldOrder(uint8_t);      // Send new STC-007 field order setting.
    void newSTC007PCorrection(bool);        // Send new STC-007 P-code correction setting.
    void newSTC007QCorrection(bool);        // Send new STC-007 Q-code correction setting.
    void newSTC007CWDCorrection(bool);      // Send new STC-007 CWD correction setting.
    void newSTC007M2Sampling(bool);         // Send new STC-007/M2 sample mode setting.
    void newSTC007ResolutionPreset(uint8_t);    // Send new STC-007 "resolution mode" for L2B thread.
    void newSTC007SampleRatePreset(uint16_t);   // Send new STC-007 "sample rate" for L2B thread.
    void newUseCRC(bool);                   // Send new "ignore CRC" for L2B thread.
    void newMaskSeams(bool);                // Send new "mask invalid seams" for L2B thread.
    // Signals for audio output processor.
    void newAPLogLevel(uint8_t);            // Send new logging level for AP thread.
    void newMaskingMode(uint8_t);           // Send new "dropout masking" for AP thread.
    void newEnableLivePB(bool);             // Send new "live playback" setting for AP thread.
    void newEnableWaveSave(bool);           // Send new "save to file" setting for AP thread.
    // Signals for visualizers and fine settings dialogs.
    void newFrameBinarized(uint32_t);       // Binarized a whole new frame (frame number for visualization).
    void newFrameAssembled(uint32_t);       // Assembled a whole new frame (frame number for visualization).
    void newVideoStandard(uint8_t);         // Video standard of the last assembled frame.
    void retransmitBinLine(PCM1Line);
    void retransmitBinLine(PCM16X0SubLine);
    void retransmitBinLine(STC007Line);
    void retransmitAsmLine(PCM1SubLine);
    void retransmitAsmLine(PCM16X0SubLine);
    void retransmitAsmLine(STC007Line);
    void retransmitPCMDataBlock(PCM1DataBlock);
    void retransmitPCMDataBlock(PCM16X0DataBlock);
    void retransmitPCMDataBlock(STC007DataBlock);
    void newFineReset();
    void guiUpdFineDrawDeint(bool);

    void doPlayUnload();                    // Request unload/close source to video input processor.
    void doPlayStart();                     // Request start playback to video input processor.
    void doPlayPause();                     // Request pause playback to video input processor.
    void doPlayStop();                      // Request stop playback to video input processor.
};

#endif // MAINWINDOW_H
