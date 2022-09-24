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
#define APP_ORG_HTTP    QString("http://fagear.ru")
#define APP_NAME        QString("Video PCM decoder")
#define APP_EXEC        QString("STC007decoder")
#define APP_VERSION     "0.99.3"
#define APP_INI_NAME    QString("pcmdecode")
#define APP_LANG_PATH   (qApp->applicationDirPath())

#define GLOBAL_DEBUG_EN     1       // Enable any debug console output.

#ifdef GLOBAL_DEBUG_EN
    #define VIP_EN_DBG_OUT      1       // Enable debug console output in [vin_ffmpeg] module.
    //#define STC_LINE_EN_DBG_OUT 1       // Enable debug console output in [stc007line] module.
    //#define PCM1_LINE_EN_DBG_OUT 1       // Enable debug console output in [pcm1line] module.
    //#define PCM16X0_LINE_EN_DBG_OUT 1       // Enable debug console output in [pcm16x0subline] module.
    //#define DB_EN_DBG_OUT       1       // Enable debug console output in [xxxdatablock] modules.
    #define LB_EN_DBG_OUT       1       // Enable debug console output in [binarizer] module.
    #define DI_EN_DBG_OUT       1       // Enable debug console output in [xxxdatastitcher] and [xxxdeinterleaver] modules.
    #define AP_EN_DBG_OUT       1       // Enable debug console output in [audioprocessor] module.
    #define TA_EN_DBG_OUT       1       // Enable debug console output in [stc007toaudio] module.
    #define TW_EN_DBG_OUT       1       // Enable debug console output in [stc007towav] module.
    //#define VIS_EN_DBG_OUT      1       // Enable debug console output in [renderpcm] module.
    #define TST_EN_DBG_OUT      1       // Enable debug console output in [pcmtester] module.
#endif

#define LINES_PER_NTSC_FIELD        245     // PCM lines in one field of a NTSC frame.
#define LINES_PER_PAL_FIELD         294     // PCM lines in one field of a PAL frame.

#define MAX_VLINE_QUEUE_SIZE        2800    // Maximum queue size (in lines) for video lines.
#define MAX_PCMLINE_QUEUE_SIZE      5400    // Maximum queue size (in lines) for PCM lines.
#define MAX_SAMPLEPAIR_QUEUE_SIZE   20480   // Maximum queue size (in sample pairs) for audio data.

//#define LB_EN_PIXEL_DBG     1     // Enable boundaries check for pixel coordinate in [stc007binarizer] module.
//#define LB_ROUNDED_DIVS     1     // Enable rounded integer divisions in [findVideoPixel()] in [stc007binarizer] module.

#define AP_DEBUG_UNCOR_MONO     1   // Enable debug left-channel "uncorrected+corrected" output.

#endif // CONFIG_H

