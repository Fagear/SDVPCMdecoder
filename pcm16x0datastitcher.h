#ifndef PCM16X0DATASTITCHER_H
#define PCM16X0DATASTITCHER_H

#include <array>
#include <stdint.h>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QString>
#include <vector>
#include "config.h"
#include "circbuffer.h"
#include "frametrimset.h"
#include "pcm16x0datablock.h"
#include "pcm16x0deinterleaver.h"
#include "pcm16x0subline.h"
#include "pcmsamplepair.h"

#ifndef QT_VERSION
    #undef DI_EN_DBG_OUT
#endif

#ifdef DI_EN_DBG_OUT
    //#define DI_LOG_BUF_WAIT_VERBOSE     1       // Produce verbose output for buffer filling process.
    //#define DI_LOG_BUF_FILL_VERBOSE     1       // Produce verbose output for buffer filling process.
    //#define DI_LOG_TRIM_VERBOSE         1       // Produce verbose output for trimming process.
#endif

// TODO: SI/EI auto-detection
// TODO: fix erroneous SI format stitching (causing repetitive broken blocks)
class PCM16X0DataStitcher : public QObject
{
    Q_OBJECT

public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),          // External operations with settings.
        LOG_PROCESS = (1<<1),           // General stage-by-stage logging.
        LOG_TRIM = (1<<2),              // Detecting frame trimming
        LOG_DEINTERLEAVE = (1<<3),      // Process of converting lines into data block.
        LOG_ERROR_CORR = (1<<4),        // Process of detecting and correcting errors.
        LOG_PADDING = (1<<5),           // Searching for padding.
        LOG_PADDING_LINE = (1<<6),      // Dump PCM lines while adjusting padding.
        LOG_PADDING_BLOCK = (1<<7),     // Show data blocks while adjusting padding.
        LOG_FIELD_ASSEMBLY = (1<<8),    // Show process of assembling new fields with padding.
        LOG_UNSAFE = (1<<9),            // Show process of marking unsafe data blocks.
        LOG_BLOCK_DUMP = (1<<10)        // Dump PCM data block after data block processing is complete.
    };

    // Frame parameters.
    enum
    {
        LINES_PF = (LINES_PER_NTSC_FIELD),      // PCM lines in one field of a frame for PCM-16x0 (NTSC video standard only).
        SUBLINES_PF = (LINES_PF*PCM16X0SubLine::SUBLINES_PER_LINE),         // Number of sub-lines per video field.
        SI_TRUE_INTERLEAVE = (PCM16X0DataBlock::SI_INTERLEAVE_OFS*PCM16X0SubLine::SUBLINES_PER_LINE),   // Number of sub-lines per interleave block in SI format.
        EI_TRUE_INTERLEAVE = ((SUBLINES_PF*2)/PCM16X0DataBlock::LINE_CNT)   // Number of sub-lines in the only block in EI format.
    };

    // Buffer limits.
    enum
    {
        BUF_SIZE_TRIM = (MAX_VLINE_QUEUE_SIZE*PCM16X0SubLine::SUBLINES_PER_LINE),   // Maximum number of lines to store in [trim_buf] buffer.
        BUF_SIZE_FIELD = (SUBLINES_PF),                     // Maximum number of sub-lines to store in per-field buffers.
        MIN_GOOD_LINES_PF = (PCM16X0DataBlock::SI_INTERLEAVE_OFS*(PCM16X0DataBlock::INT_BLK_PER_FIELD-1)),      // Minimum number of lines with good CRC per field to enable aggresive trimming.
        MIN_GOOD_SUBLINES_PF = (MIN_GOOD_LINES_PF*PCM16X0SubLine::SUBLINES_PER_LINE),                           // Minimum number of sub-lines with good CRC per field to enable aggresive trimming.
        MIN_FILL_SUBLINES_PF_SI = (SI_TRUE_INTERLEAVE),     // Minimum number of sub-lines in per-field buffers to perform padding detection in SI format.
        MIN_FILL_SUBLINES_PF_EI = (82*PCM16X0SubLine::SUBLINES_PER_LINE)            // Minimum number of sub-lines in per-field buffers to perform padding detection in EI format.
    };

    // Paddings limits.
    enum
    {
        INT_BLK_LINE_DELIMITER = 45,                                    // Number of line to distinguish between 1st and 2nd interleave blocks Control Bits.
        MAX_PADDING_SI = (PCM16X0DataBlock::SI_INTERLEAVE_OFS),         // Maximum available line (not sub-line!) padding for SI format.
        MAX_PADDING_EI = 81,                                            // Maximum available line (not sub-line!) padding for EI format.
        MAX_BURST_SILENCE_SI = (PCM16X0DataBlock::SI_INTERLEAVE_OFS-1),             // Maximum number of consequtive silenced blocks before padding aborts for SI format.
        MAX_BURST_SILENCE_EI = (MAX_PADDING_EI*PCM16X0SubLine::SUBLINES_PER_LINE),  // Maximum number of consequtive silenced blocks before padding aborts for EI format.
        MAX_BURST_BROKEN = 1,                                                       // Maximum number of consequtive broken blocks before padding aborts.
        MAX_BURST_UNCH_SI = (PCM16X0DataBlock::SI_INTERLEAVE_OFS-1),    // Maximum number of consequtive unchecked blocks before padding aborts for SI format.
        MAX_BURST_UNCH_EI = (MAX_PADDING_EI*PCM16X0SubLine::SUBLINES_PER_LINE),     // Maximum number of consequtive unchecked blocks before padding aborts for EI format.
        UNCH_MASK_DURATION = MAX_PADDING_EI,                            // Default duration (in lines) of masking uncheckable and false-corrected blocks after BROKEN one.
        MIN_VALID_SI = (PCM16X0DataBlock::SI_INTERLEAVE_OFS/2),         // Minimum number of consequtive valid blocks for valid padding for SI format.
        MIN_VALID_EI = (EI_TRUE_INTERLEAVE/3),                          // Minimum number of consequtive valid blocks for valid padding for EI format.
        INVALID_PAD_MARK = 0xFF,                                        // Padding value to indicate invalid padding.
        STATS_DEPTH = 65                                                // History depth for stats.
    };

    // Skew/control bit offsets from the start of the interleave block.
    enum
    {
        BIT_EMPHASIS_OFS = (0*PCM16X0SubLine::SUBLINES_PER_LINE),       // Ephasis ON/OFF.
        BIT_SAMPLERATE_OFS = (1*PCM16X0SubLine::SUBLINES_PER_LINE),     // Sample rate 44100/44056.
        BIT_MODE_OFS = (2*PCM16X0SubLine::SUBLINES_PER_LINE),           // Format EI/SI.
        BIT_CODE_OFS = (3*PCM16X0SubLine::SUBLINES_PER_LINE),           // Code/Audio.
        BIT_MAX_OFS = (4*PCM16X0SubLine::SUBLINES_PER_LINE)             // Limiter for control bit locations.
    };

    // Emphasis flag state.
    enum
    {
        EMPH_UNKNOWN,           // Emphasis state is not determined.
        EMPH_OFF,               // Emphasis is not present.
        EMPH_ON                 // Emphasis is present, de-emphasis required.
    };

    // Format of PCM-16x0.
    enum
    {
        INTL_FMT_UNKNOWN,       // Unknown interleave format.
        INTL_FMT_SI,            // SI format.
        INTL_FMT_EI             // EI format.
    };

    // PCM-16x0 frame content.
    enum
    {
        CONTENT_UNKNOWN,        // Unknown content of the frame.
        CONTENT_AUDIO,          // Frame contains audio.
        CONTENT_CODE            // Frame contains code.
    };

    // Results of [findPadding()].
    enum
    {
        DS_RET_NO_DATA,         // Provided PCM line buffer is too small to get data out.
        DS_RET_SILENCE,         // Can not check parity and ECC due to total silence.
        DS_RET_BROKE,           // Incorrect padding due to broken data.
        DS_RET_NO_PAD,          // Can not find correct padding.
        DS_RET_OK               // Processing done successfully.
    };

    // Stages of processing.
    enum
    {
        STG_TRY_PREVIOUS,       // 0 Try to stitch field as before.
        STG_TRY_TFF,            // 1 Try to stitch field of current frame, assuming field order is TFF.
        STG_TRY_BFF,            // 2 Try to stitch field of current frame, assuming field order is BFF.
        STG_FULL_PREPARE,       // 3 Getting ready to find padding between fields of current frame.
        STG_INTERPAD_TFF,       // 4 Detecting padding between fields of current frame with TFF order.
        STG_INTERPAD_BFF,       // 5 Detecting padding between fields of current frame with BFF order.
        STG_ALIGN_TFF,          // 6 Align data to frame borders and recombine all paddings for TFF order.
        STG_ALIGN_BFF,          // 7 Align data to frame borders and recombine all paddings for BFF order.
        STG_FB_CTRL_EST,        // 8 Detecting independant field padding from Control Bit positions.
        STG_PAD_NO_GOOD,        // 9 Padding detection failed.
        STG_PAD_OK,             // 10 Padding done successfully.
        STG_PAD_SILENCE,        // 11 Padding detection can not be performed on silence.
        STG_PAD_MAX             // 12 Limiter for stages.
    };

private:
    PCM16X0DataBlock padding_block;             // Data block object, used in padding process.
    PCM16X0Deinterleaver pad_checker;           // Deinterleaver object, used for padding detection.
    PCM16X0Deinterleaver lines_to_block;        // Deinterleaver object, used for final lines-block processing.
    FrameAsmPCM16x0 frasm_f1;                   // Frame assembling data for the current frame.
    QElapsedTimer file_time;                    // Timer for measuring file processing time.
    std::deque<PCM16X0SubLine> *in_lines;       // Input PCM line quene (shared).
    std::deque<PCMSamplePair> *out_samples;     // Output sample pairs queue (shared).
    QMutex *mtx_lines;                          // Mutex for input queue.
    QMutex *mtx_samples;                        // Mutex for output queue.
    std::vector<PCM16X0SubLine> trim_buf;       // Internal buffer for collecting a frame and detecting trimming.
    std::vector<PCM16X0SubLine> frame1_odd;     // Internal buffer for Frame odd field for detecting padding.
    std::vector<PCM16X0SubLine> frame1_even;    // Internal buffer for Frame even field for detecting padding.
    std::deque<PCM16X0SubLine> padding_queue;   // Internal buffer for field padding detection.
    std::deque<PCM16X0SubLine> conv_queue;      // Output PCM lines buffer after trimming and padding, before converting into data blocks.
    std::string file_name;                      // Name of the file being processed (passed onto audio processing chain).
    uint8_t broken_mask_dur;                    // Duration (in lines) for error masking after BROKEN data block.
    uint8_t preset_field_order;                 // Field order, set externally.
    uint8_t preset_format;                      // PCM-1630 mode/format set externally.
    circarray<uint8_t, STATS_DEPTH> stats_emph;     // List of last emphasis states.
    circarray<uint16_t, STATS_DEPTH> stats_srate;   // List of last sample rate states.
    circarray<uint8_t, STATS_DEPTH> stats_code;     // List of last data content states.
    circarray<uint8_t, STATS_DEPTH> stats_padding;  // List of last paddings.
    uint16_t log_level;                         // Level of debug output.
    uint16_t trim_fill;                         // Number of filled sub-lines in [trim_buf] from input queue.
    uint16_t f1_srate;                          // Sample rate detected in [collectCtrlBitStats()].
    bool f1_emph;                               // Emphasis setting detected in [collectCtrlBitStats()].
    bool f1_code;                               // Code/Audio setting detected in [collectCtrlBitStats()].
    bool format_changed;                        // Flag to indicate unprocessed change of format (SI/EI).
    bool ignore_CRC;                            // Ignore CRC from video lines or not (and force parity check).
    bool enable_P_code;                         // Enable P-code error correction or not.
    bool mask_seams;                            // Mark data blocks on incorrect field seams as invalid (cleans most of clicks and pops on bad quality video).
    bool file_start;                            // Detected start of a new file, filename saved to [file_name].
    bool file_end;                              // Detected end of a file.
    bool finish_work;                           // Flag to break executing loop.

public:
    explicit PCM16X0DataStitcher(QObject *parent = 0);
    void setInputPointers(std::deque<PCM16X0SubLine> *in_pcmline = NULL, QMutex *mtx_pcmline = NULL);
    void setOutputPointers(std::deque<PCMSamplePair> *out_pcmsamples = NULL, QMutex *mtx_pcmsamples = NULL);

private:
    void resetState();
    bool waitForOneFrame();
    void fillUntilFullFrame();
    void findFrameTrim();
    void splitFrameToFields();
    void prescanForFalsePosCRCs(std::vector<PCM16X0SubLine> *field_buf, uint16_t f_size);
    void cutFieldTop(std::vector<PCM16X0SubLine> *field_buf, uint16_t *f_size, uint16_t cut_cnt);
    int16_t findZeroControlBitOffset(std::vector<PCM16X0SubLine> *field, uint16_t f_size, bool from_top = false);
    uint8_t estimateBlockNumber(std::vector<PCM16X0SubLine> *field, uint16_t f_size, int16_t zero_ofs);
    uint8_t trySIPadding(std::deque<PCM16X0SubLine> *field_data,
                         uint8_t padding, FieldStitchStats *stitch_stats = NULL);
    uint8_t findSIPadding(std::vector<PCM16X0SubLine> *field_buf, uint16_t *f_size,
                           uint16_t *top_padding = NULL, uint16_t *bottom_padding = NULL);
    void findSIDataAlignment();
    uint8_t tryEIPadding(uint16_t padding, FieldStitchStats *stitch_stats = NULL);
    uint8_t findEIPadding(uint8_t field_order);
    void conditionEIFramePadding(std::vector<PCM16X0SubLine> *field1, std::vector<PCM16X0SubLine> *field2, uint16_t *f1_size, uint16_t *f2_size,
                                 uint16_t *f1_top_pad, uint16_t *f1_bottom_pad, uint16_t *f2_top_pad, uint16_t *f2_bottom_pad);
    uint8_t findEIDataAlignment(std::vector<PCM16X0SubLine> *field, uint16_t *f_size, uint16_t *top_pad, uint16_t *bottom_pad);
    uint8_t findEIFrameStitching();
    void clearCtrlBitStats();
    void updateCtrlBitStats(PCM16X0DataBlock *int_block_flags = NULL);
    bool getProbableEmphasesBit();
    bool getProbableCodeBit();
    uint16_t getProbableSampleRate();
    void clearPadStats();
    void updatePadStats(uint8_t new_pad, bool valid);
    uint8_t getProbablePadding();
    uint16_t getFirstFieldLineNum(uint8_t in_order);
    uint16_t getSecondFieldLineNum(uint8_t in_order);
    uint16_t addLinesFromField(std::vector<PCM16X0SubLine> *field_buf, uint16_t ind_start, uint16_t count, uint16_t *last_q_order, uint16_t *last_line_num = NULL);
    uint16_t addFieldPadding(uint32_t in_frame, uint16_t line_cnt, uint16_t *last_q_order, uint16_t *last_line_num = NULL);
    void fillFrameForOutput();
    bool collectCtrlBitStats(PCM16X0DataBlock *int_block_flags = NULL);
    void outputFileStart();
    void outputDataBlock(PCM16X0DataBlock *in_block = NULL);
    void outputFileStop();
    void performDeinterleave(uint8_t int_format);

public slots:
    void setLogLevel(uint16_t);             // Set logging level.
    void setFormat(uint8_t);                // Set PCM-1630 mode/format.
    void setFieldOrder(uint8_t);            // Preset field order.
    void setPCorrection(bool);              // Enable/disable P-code error correction.
    void setSampleRatePreset(uint16_t);     // Preset audio sample rate.
    void setFineUseECC(bool);               // Set fine settings: usage of ECC on CRC-marked words.
    void setFineMaskSeams(bool);            // Set fine settings: usage of unchecked seams masking.
    void setFineBrokeMask(uint8_t);         // Set fine settings: number of lines to mask after BROKEN data block.
    void setDefaultFineSettings();          // Set fine settings to defaults.
    void requestCurrentFineSettings();      // Get current fine settings.
    void doFrameReassemble();               // Main execution loop.
    void stop();                            // Set the flag to break execution loop and exit.

signals:
    void guiUpdFrameAsm(FrameAsmPCM16x0);   // New frame assembling data calculated in DI thread, need to update GUI.
    void guiUpdFineUseECC(bool);            // New fine setting: usage of ECC on CRC-marked words.
    void guiUpdFineMaskSeams(bool);         // New fine setting: usage of unchecked seams masking.
    void guiUpdFineBrokeMask(uint8_t);      // New fine setting: number of lines to mask after BROKEN data block.
    void newLineProcessed(PCM16X0SubLine);  // Processed PCM-16x0 sub-line object (for visualization).
    void newBlockProcessed(PCM16X0DataBlock);   // Processed PCM-16x0 data block object (for visualization).
    void loopTime(quint64);                 // Useful loop time count.
    void finished();                        // Thread is stopped.
};

#endif // PCM16X0DATASTITCHER_H
