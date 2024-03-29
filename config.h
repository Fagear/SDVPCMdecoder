﻿/**************************************************************************************************************************************************************
config.h

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

Created: 2020-05

Configuration file for the whole project.
It contains various global strings, flags and parameters for different modules.

**************************************************************************************************************************************************************/

#ifndef CONFIG_H
#define CONFIG_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <QString>

#define COMPILE_DATE    __DATE__
#define COMPILE_TIME    __TIME__
#define DBG_QFILE       QString(__FILE__)
#define DBG_QFUNCTION   QString(__FUNCTION__)
#define DBG_QLINE       QString::number(__LINE__, 10)
#define DBG_ANCHOR      QString("["+DBG_QFILE+": "+DBG_QFUNCTION+"(): #"+DBG_QLINE+"]")

#define APP_ORG_NAME    QString("Fagear")
#define APP_ORG_HTTP    QString("https://discord.gg/4GcefbPffX")
#define APP_NAME        QString("Video PCM decoder")
#define APP_NAME_LONG   (QObject::tr("SDVPCM: декодер PCM аудио из SD видео"))
#define APP_EXEC        QString("SDVPCMdecoder")
#define APP_VERSION     "0.99.7"
#define APP_INI_NAME    QString("sdv2pcm")
#define APP_LANG_PATH   (qApp->applicationDirPath())

#define GLOBAL_DEBUG_EN     1       // Enable any debug console output.

// Debug flags.
#ifdef GLOBAL_DEBUG_EN
    #define FFWR_EN_DBG_OUT     1       // Enable debug console output in [FFMPEGWrapper] module.
    #define VIP_EN_DBG_OUT      1       // Enable debug console output in [VideoInFFMPEG] module.
    //#define STC_LINE_EN_DBG_OUT 1       // Enable debug console output in [STC007Line] module.
    //#define PCM1_LINE_EN_DBG_OUT 1       // Enable debug console output in [PCM1Line] module.
    //#define PCM16X0_LINE_EN_DBG_OUT 1       // Enable debug console output in [PCM16X0SubLine] module.
    //#define DB_EN_DBG_OUT       1       // Enable debug console output in [xxxDataBlock] modules.
    #define LB_EN_DBG_OUT       1       // Enable debug console output in [Binarizer] module.
    #define DI_EN_DBG_OUT       1       // Enable debug console output in [xxxDataStitcher] and [xxxDeinterleaver] modules.
    #define AP_EN_DBG_OUT       1       // Enable debug console output in [AudioProcessor] module.
    #define TA_EN_DBG_OUT       1       // Enable debug console output in [SamplesToAudio] module.
    #define TW_EN_DBG_OUT       1       // Enable debug console output in [SamplesToWAV] module.
    //#define VIS_EN_DBG_OUT      1       // Enable debug console output in [RenderPCM] module.
    //#define VIS_NEAR_SILENT     1       // Enable debug drawing of almost silent lines in [RenderPCM] module.
    #define TST_EN_DBG_OUT      1       // Enable debug console output in [PCMTester] module.
#endif

#define FRAMES_READ_AHEAD_MAX       3       // Maximum number of frames read ahead.
#define FRAMES_ASM_BUF_MAX          4       // Maximum number of frames in the buffer after binarization.

#define LINES_PER_FRAME_MAX         640     // Total number of lines in the frame maximum for the source.
#define LINES_PER_NTSC_FIELD        245     // PCM lines in one field of a NTSC frame.
#define LINES_PER_PAL_FIELD         294     // PCM lines in one field of a PAL frame.

#define MAX_VLINE_QUEUE_SIZE        (FRAMES_READ_AHEAD_MAX*LINES_PER_FRAME_MAX)     // Maximum queue size (in lines) for video lines.
#define MAX_PCMLINE_QUEUE_SIZE      (FRAMES_ASM_BUF_MAX*LINES_PER_FRAME_MAX)        // Maximum queue size (in lines) for PCM lines.
#define MAX_SAMPLEPAIR_QUEUE_SIZE   22050   // Maximum queue size (in sample pairs) for audio data.

//#define LB_EN_PIXEL_DBG     1     // Enable boundaries check for pixel coordinate in [stc007binarizer] module.
//#define LB_ROUNDED_DIVS     1     // Enable rounded integer divisions in [findVideoPixel()] in [stc007binarizer] module.

#endif // CONFIG_H

