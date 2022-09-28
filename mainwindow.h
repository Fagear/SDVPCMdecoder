#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <deque>
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
#include <QStyleFactory>
#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <QTranslator>
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
#include "pcmtester.h"
#include "samples2wav.h"
#include "videotodigital.h"
#include "renderpcm.h"
//#include "ui_about.h"
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
        LIST_TYPE_STC007
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
    VideoInFFMPEG *VIN_worker;
    VideoToDigital *V2D_worker;
    PCM1DataStitcher *L2B_PCM1_worker;
    PCM16X0DataStitcher *L2B_PCM16X0_worker;
    STC007DataStitcher *L2B_STC007_worker;
    AudioProcessor *AP_worker;
    capt_sel *captureSelectDialog;
    fine_vidin_set *vipFineSetDialog;
    fine_bin_set *binFineSetDialog;
    fine_deint_set *deintFineSetDialog;
    frame_vis *visuSource;
    frame_vis *visuBin;
    frame_vis *visuAssembled;
    frame_vis *visuBlocks;
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
    std::deque<PCM16X0SubLine> pcm16x0_lines;  // Queue for PCM lines, binarized from video lines (in the same strict order).
    std::deque<STC007Line> stc007_lines;    // Queue for PCM lines, binarized from video lines (in the same strict order).
    std::deque<PCMSamplePair> audio_data;   // Queue for PCM sample pair, picked from assembled PCM data blocks.
    QMutex vl_lock;                     // Protects [video_lines].
    QMutex pcm1line_lock;               // Protects [pcm1_lines].
    QMutex pcm16x0subline_lock;         // Protects [pcm16x0subline].
    QMutex stcline_lock;                // Protects [stc007_lines].
    QMutex audio_lock;                  // Protects [audio_data].
    QPalette plt_redlabel;              // Red indicator palette.
    QPalette plt_yellowlabel;           // Yellow indicator palette.
    QPalette plt_greenlabel;            // Green indicator palette.

    bool shutdown_started;                  // Application exit procedure started.
    bool inhibit_setting_save;              // Disable updating settings.
    bool window_geom_saved;
    bool src_draw_deint;                    // Draw source video deinterlaced or not.
    uint8_t log_level;                      // Setting for debugging log level.
    uint8_t v_decoder_state;                // State of video decoder.
    uint8_t set_pcm_type;                   // PCM type set from UI.
    uint16_t lines_per_video;

    FrameAsmPCM1 frame_asm_pcm1;            // PCM-1 frame assembling data for GUI.
    FrameAsmPCM16x0 frame_asm_pcm16x0;      // PCM-16x0 frame assembling data for GUI.
    FrameAsmSTC007 frame_asm_stc007;        // STC-007 frame assembling data for GUI.

    // Internal statistics.
    uint8_t stat_dbg_index;
    quint64 stat_debug_avg[DBG_AVG_LEN];
    uint8_t stat_tracking_index;
    circarray<FrameBinDescriptor, TRACKING_BUF_LEN> stat_tracking_arr;
    FrameBinDescriptor stat_video_tracking;
    uint64_t stat_vlines_time_per_frame;
    uint32_t stat_min_vip_time;
    uint32_t stat_max_vip_time;
    uint64_t stat_lines_time_per_frame;
    uint32_t stat_min_bin_time;
    uint32_t stat_max_bin_time;
    uint16_t stat_lines_per_frame;
    uint64_t stat_blocks_time_per_frame;
    uint32_t stat_min_di_time;
    uint32_t stat_max_di_time;
    uint16_t stat_blocks_per_frame;
    uint8_t stat_ref_level;
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
    uint32_t stat_mute_cnt;
    uint32_t stat_mask_cnt;
    uint32_t stat_processed_frame_cnt;
    uint32_t stat_line_cnt;

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    void moveEvent(QMoveEvent *event);
    void resizeEvent(QMoveEvent *event);
    void closeEvent(QCloseEvent *event);

    void buffer_tester();

    QString generateTranslationPath(QString in_locale);
    QStringList getTranslationList();
    void updateGUILangList();
    void setGUILanguage(QString in_locale, bool suppress = false);      // Select UI language.
    // Update settings for modules.
    void setVIPOptions();                   // Set options for [vin_processor] module.
    void setLBOptions();                    // Set options for [videotodigital] and [stc007binarizer] modules.
    void setDIOptions();                    // Set options for [stc007datastitcher] and [stc007deinterleaver] modules.
    void setAPOptions();                    // Set options for [audioprocessor] and [stc007towav] modules.
    // Update logging settings.
    void setMainLogMode();                  // Set debug logging mode for main module.
    void setVIPLogMode();                   // Set debug logging mode for [vin_processor] module.
    void setLBLogMode();                    // Set debug logging mode for [videotodigital] and [stc007binarizer] modules.
    void setDILogMode();                    // Set debug logging mode for [stc007datastitcher] and [stc007deinterleaver] modules.
    void setAPLogMode();                    // Set debug logging mode for [audioprocessor] and [stc007towav] modules.

    void disableGUIEvents();                // Disable comboboxes events to prevent switching settings to default while changing translation.
    void enableGUIEvents();                 // Re-enable comboboxes events after changing translation.
    void applyGUISettings();                // Apply GUI settings to the decoder.
    void readGUISettings();                 // Read settings into GUI.
    void clearPCMQueue();

    CoordinatePair getCoordByFrameNo(uint16_t);

private slots:
    //void dbgSlot(int);

    void updateWindowPosition();
    void displayErrorMessage(QString);

    void setLang(QAction *);

    void playVideo();
    void pauseVideo();
    void loadVideo();
    void loadPicture();
    void exitAction();

    // GUI selection reactions.
    void updateGUISettings();               // Save settings for GUI options.
    void clearStat();                       // Clear decoder stats.

    // Main menu reactions.
    void resetOptDecoder();
    void resetVisPositions();
    void resetFull();

    void showAbout();                       // Display "About" window.
    void showCaptureSelector();             // Display video capture selection dialog.
    void showVidInFineSettings();           // Display video processor fine settings dialog.
    void showBinFineSettings();             // Display binarizator fine settings dialog.
    void showDeintFineSettings();           // Display deinterleaver fine settings dialog.

    void setDefaultFineSettings();          // Receive request for fine settings reset from video processor fine settings dialog.
    void requestCurrentFineSettings();      // Receive request for fine settings from video processor fine settings dialog.
    void setFineDrawDeint(bool);

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
    void updateSetVIPLog();                 // Save settings for [vin_processor] module.
    void updateSetLBLog();                  // Save settings for [videotodigital] and [stc007binarizer] modules.
    void updateSetDILog();                  // Save settings for [stc007datastitcher] and [stc007deinterleaver] modules.
    void updateSetAPLog();                  // Save settings for [audioprocessor] and [stc007towav] modules.
    void clearMainPLog();                   // Turn off debug logging for main module.
    void clearVIPLog();                     // Turn off debug logging for [vin_processor] module.
    void clearLBLog();                      // Turn off debug logging for [videotodigital] and [stc007binarizer] modules.
    void clearDILog();                      // Turn off debug logging for [stc007datastitcher] and [stc007deinterleaver] modules.
    void clearAPLog();                      // Turn off debug logging for [audioprocessor] and [stc007towav] modules.
    void clearAllLogging();                 // Turn off all debug logging.

    // Player state reactions.
    void playerStarted();                   // React on video decoder starting playback.
    void playerStopped();                   // React on video decoder stopping playback.
    void playerPaused();                    // React on video decoder pausing playback.
    void playerError(QString);              // React on video decoder error.
    void livePBUpdate(bool);                // React on live playback state.

    // Timer reactions.
    void checkThreads();                    // Check if all threads are alive.

    void updateGUIByTimer();
    void updatePCM1FrameData();             // Update PCM-1 frame assembling data.
    void updatePCM16x0FrameData();          // Update PCM-16x0 frame assembling data.
    void updateSTC007FrameData();           // Update STC-007 frame assembling data.

    // Buffered receivers for visualizers.
    void receiveBinLine(PCM1Line);
    void receiveBinLine(PCM16X0SubLine);
    void receiveBinLine(STC007Line);
    void receiveAsmLine(PCM1SubLine);
    void receiveAsmLine(PCM16X0SubLine);
    void receiveAsmLine(STC007Line);
    void receivePCMDataBlock(PCM1DataBlock);
    void receivePCMDataBlock(PCM16X0DataBlock);
    void receivePCMDataBlock(STC007DataBlock);

    // Stats gathering reactions.
    void updateDebugBar(quint64);
    void updateStatsVideoLineTime(uint32_t);// Update stats after VIP has finished a line and provided spent time count.
    void updateStatsVIPFrame(uint16_t);     // Update stats after VIP has read a frame.
    void updateStatsVideoTracking(FrameBinDescriptor);  // Update stats after VIP has spliced a frame.
    void updateStatsDroppedFrame();         // Update stats with new value for dropped frames.
    void updateStatsLineTime(unsigned int); // Update stats after LB has finished a line and provided spent time count.
    void updateStatsLBFrame(unsigned int);  // Update stats after LB has finished a frame.
    void updateStatsFrameAsm(FrameAsmPCM1);     // Update stats and video processor with new trim settings.
    void updateStatsFrameAsm(FrameAsmPCM16x0);  // Update stats and video processor with new trim settings.
    void updateStatsFrameAsm(FrameAsmSTC007);   // Update stats and video processor with new trim settings.
    void updateStatsMutes(uint16_t);        // Update stats with new value for muted samples.
    void updateStatsMaskes(uint16_t);       // Update stats with new value for masked samples.
    void updateStatsBlockTime(STC007DataBlock); // Update stats after DI has finished a data block and provided spent time count.
    void updateStatsDIFrame(uint16_t);      // Update stats after DI has finished a frame.

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
    void newWindowPosition();               // React to new size/location of the main window.
    void aboutToExit();                     // Application is about to close.
    void newTargetPath(QString);            // Full path for new source.
    // Signals for video input processor.
    void newVIPLogLevel(uint8_t);           // Send new logging level for VIP thread.
    void newFrameDropDetection(bool);       // Send new "frame drop detection" setting for VIP thread.
    void newStepPlay(bool);                 // Send new "stepped play" setting for VIP thread.
    // Signals for binarizer (video-to-digital) module.
    void newV2DLogLevel(uint8_t);           // Send new logging level for V2D thread.
    void newBinMode(uint8_t);               // Send new "binarization mode" setting for V2D.
    void newLineDupMode(bool);              // Send new "line duplication detection" setting for V2D.
    void newPCMType(uint8_t);               // Send new "PCM type" setting for V2D.
    // Signals for deinterleaver (line-to-block) module.
    void newL2BLogLevel(uint16_t);          // Send new logging level for L2B thread.
    void newPCM1FieldOrder(uint8_t);        // Send new PCM-1 field order setting.
    void newPCM16x0Format(uint8_t);         // Send new PCM-1630 format setting.
    void newPCM16x0FieldOrder(uint8_t);     // Send new PCM-16x0 field order setting.
    void newPCM16x0PCorrection(bool);       // Send new PCM-16x0 P-code correction setting.
    void newPCM16x0SampleRatePreset(uint16_t);  // Send new PCM-16x0 "sample rate" for L2B thread.
    void newSTC007VidStandard(uint8_t);     // Send new STC-007 video standard setting.
    void newSTC007FieldOrder(uint8_t);      // Send new STC-007 field order setting.
    void newSTC007PCorrection(bool);        // Send new STC-007 P-code correction setting.
    void newSTC007QCorrection(bool);        // Send new STC-007 Q-code correction setting.
    void newSTC007CWDCorrection(bool);      // Send new STC-007 CWD correction setting.
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
    void newFrameBinarized(uint16_t);       // Binarized a whole new frame (frame number for visualization).
    void newFrameAssembled(uint16_t);       // Assembled a whole new frame (frame number for visualization).
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

    void doPlayStart();
    void doPlayPause();
    void doPlayStop();
};

#endif // MAINWINDOW_H
