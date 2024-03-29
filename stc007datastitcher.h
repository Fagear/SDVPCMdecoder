﻿/**************************************************************************************************************************************************************
stc007datastitcher.h

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

Created: 2020-06

"Data-stitcher" module for STC-007/PCM-F1/M2 formats. Wrapper for [STC007Deinterleaver].
[STC007DataStitcher] takes input queue of [STC007Line] and puts out set of the [PCMSamplePair] sub-class objects into the output queue.
[STC007DataStitcher] processes data on per-frame basis, waiting for "End Of Frame" service line tag in the input queue before starting processing the queue.
In STC-007 all fields are interlocked, so [STC007DataStitcher] has to collect and keep in memory
at least two adjacent frames to reliably perform all its functions.
[STC007DataStitcher] performs:
    - Vertical data coordinates detection;
    - Frame trimming detection (empty lines discarding);
    - Video standard detection (PAL/NTSC);
    - Field padding detection;
    - Field order detection (TFF/BFF);
    - Audio resolution detection (14/16 bits);
    - Deinterleaving and error correction with [STC007Deinterleaver];
    - [STC007DataBlock] to [PCMSamplePair] conversion;
    - Cross-Word Decoding (CWD) pre-scan ECC assist (if set by [setCWDCorrection()]);
    - Per-frame statistics collection.

Typical use case:
    - Set pointer to the input PCM line queue with [setInputPointers()];
    - Set pointer to the output audio sample queue with [setOutputPointers()];
    - Call [doFrameReassemble()] to start execution loop;
    - Feed data into the input queue that was set with [setInputPointers()].
    -- optional: P-code and Q-code corrections can be toggled with [setPCorrection()] and [setQCorrection()],
                 CWD assist can be toggled with [setCWDCorrection()],
                 video standard can be preset with [setVideoStandard()], field order can be preset with [setFieldOrder()],
                 audio resolution can be preset with [setResolutionPreset()], sample rate can be preset with [setSampleRatePreset()].

[STC007DataStitcher] has a number of fine settings for error correction and masking.
[setDefaultFineSettings()] will set default fine settings.
[requestCurrentFineSettings()] will return current fine settings.
New fine settings can be set with [setFineMaxUnch14()], [setFineMaxUnch16()], [setFineUseECC()],
[setFineTopLineFix()], [setFineMaskSeams()] and [setFineBrokeMask()].

It would be easy to process all lines, deinterleave those and decode into samples if video captures were ideal
and contained all source data lines and in the exact same places every time. But the reality is different.
PCM processors utilize lines in the inactive region right after VBI. Most video capture devices unable to capture inactive region.
Worse than that, different capture devices have different vertical offsets from VBI and it also can vary with video standard (PAL/NTSC).
There are professional capture devices that can capture the whole frame but those are rare and expensive.
This situation introduces static uncertainty to the vertical data coordinates.
If captured video originated from a VTR, it introduces its own defects to the frame.
Misaligned tape transport can introduce additional unknown vertical offset to the data, damaged tape can lead to sync losses, frame rolling, etc.
In edge cases, two fields of the same frame can have different vertical offsets and that offset can jump and vary during playback.
So, VTR introduces dynamic uncertainty to the capture.
Sometimes noise in the picture can be treated as PCM data with invalid CRC by [Binarizer], other times lines with PCM damaged beyond recovery and lost.
If those lines are right at the top or the bottom of the screen the decision has to be made: include those lines in the data stream or not?
Usually capture is done with additional empty lines at the top and/or bottom of the screen.
All that introduces even more uncertainty to the original data location.
The decoder has no way of knowing how many lines been cut from the top of the frame (between VBI and first line of the captured frame)
and where captured lines were placed in the original frame. That's a problem, because even if data is shifted by one line, data alignment will fail
and we'll have incorrectly working error correction and corrupted data in general at the output.

STC-007/PCM-F1 uses inverleaving that is not fixed to field or frame limits, several data blocks can have their source data distributed
between two adjacent field in different proportions, all fields are interlocked together in a chain.
That means that not only vertical position of the data in single frame/field is important,
but that the exact connection of each pair of fields must be preserved, that's critical.

During development of the decoder a note was made that if there are none CRC errors and error correction procedure is forced,
it should return zeroes as syndromes. But if data block was assembled from lines of adjacent fields that were out of alignment,
then even if there were no CRC error flags in the data, syndromes became non-zero, indicating an error in data.
Those data blocks were named "BROKEN" and became a vital part of auto-detection algorithm for padding between fields.
It became clear that data blocks that spanned across two fields will not be "BROKEN"
only in the one case when fields are aligned exactly as those were in the original data stream.
Also another note was made: maximum number of lines that can be missing between fields is 32 for STC-007 and 16 for PCM-F1
(exact limits of burst error correction capabilities) and after that data will be erroneously "fixed" and "BROKEN" blocks will be masked and undetected.

So, [STC007DataStitcher] implements padding sweeping between each adjacent fields ("stitching" process) inserting from 0 to 32(16) empty lines,
deinterleaving all data blocks on the "seam" between the fields and collecting statistics.
After sorting statistics data [STC007DataStitcher] selects the best result that becomes the set padding between fields.
This must be performed on fields of the same frame and on the fields of adjacent frames.
Thus "inner (interframe) padding" and "outer (frame-to-frame) padding" were introduced.
It was later discovered that the same technique can be used for auto-detecting the field order and audio sample resolution of the data.
[STC007DataStitcher] now can perform additional padding sweeping, trying to set different order of the fields of the two adjacent frames
and different resolution modes to determine correct settings by searching for the minimum of "BROKEN" data blocks.

When error correction for the data block is required (it has CRC flags), it can not be used to detect "BROKEN" state anymore.
So when there are too many "normal" CRC errors in the stream, automatic detection of padding, field order and audio resolution suffers.
Also if samples are silent or near it (~2 LSBs) algorithm also can not reliably detect "BROKEN" blocks.
In cases when algorithm can not reliably detect exact padding, it marks that "seam" as "unchecked"
and error correction is turned off for that seam to prevent clicks and noise from misbehaving error-correction on probably incorrectly aligned fields.
When padding can not be auto-detected, there are fallback algorithms that set padding by number of existing lines in the field
and set field order and audio resolution by previous statistics.

**************************************************************************************************************************************************************/

#ifndef STC007DATASTITCHER_H
#define STC007DATASTITCHER_H

#include <algorithm>
#include <iterator>
#include <deque>
#include <stdint.h>
#include <vector>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QString>
#include "config.h"
#include "circbuffer.h"
#include "frametrimset.h"
#include "pcmsamplepair.h"
#include "stc007datablock.h"
#include "stc007deinterleaver.h"
#include "stc007line.h"

#ifndef QT_VERSION
    #undef DI_EN_DBG_OUT
#endif

#ifdef DI_EN_DBG_OUT
    //#define DI_LOG_BUF_WAIT_VERBOSE         1       // Produce verbose output for buffer filling process.
    //#define DI_LOG_BUF_FILL_VERBOSE         1       // Produce verbose output for buffer filling process.
    //#define DI_LOG_TRIM_VERBOSE             1       // Produce verbose output for trimming process.
    //#define DI_LOG_NOLINE_SKIP_VERBOSE      1       // Produce verbose output for line skipping when splitting frames.
    //#define DI_LOG_AUDRES_VERBOSE           1       // Produce verbose output for audio resolution detection.
#endif

// TODO: try to pick best padding for failed stitches by minimum BROKEN blocks
// TODO: don't mask first 32 lines in the failed stitch
// TODO: set parameters according to Control Block, if detected
// TODO: fix stray broken data after stop and restart
// TODO: update to support single frame videos
class STC007DataStitcher : public QObject
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
        LINES_PF_NTSC = (LINES_PER_NTSC_FIELD),     // PCM lines in one field of a frame for STC-007 NTSC video standard.
        LINES_PF_PAL = (LINES_PER_PAL_FIELD),       // PCM lines in one field of a frame for STC-008 PAL video standard.
        LINES_PF_DEFAULT = (LINES_PF_NTSC),         // Default video standard if not detected automatically.
        LINES_PF_MAX_PAL = (LINES_PF_PAL+STC007DataBlock::INTERLEAVE_OFS),      // Maximum number of lines with data in the field until [VID_UNKNOWN] assumed above [VID_PAL].
        LINES_PF_MAX_NTSC = (LINES_PF_PAL-2*STC007DataBlock::INTERLEAVE_OFS),   // Maximum number of lines with data in the field until [VID_PAL] assumed above [VID_NTSC].
        FLD_ORDER_DEFAULT = (FrameAsmDescriptor::ORDER_TFF)   // Default field order if not detected while padding.
    };

    // Buffer limits.
    enum
    {
        BUF_SIZE_TRIM = (MAX_VLINE_QUEUE_SIZE*3),   // Maximum number of lines to store in [trim_buf] buffer (must contain at least two full frames).
        BUF_SIZE_FIELD = LINES_PER_PAL_FIELD,       // Maximum number of lines to store in per-field buffers.
        MIN_GOOD_LINES_PF = (LINES_PF_DEFAULT-(STC007DataBlock::INTERLEAVE_OFS)/2), // Minimum number of lines with good CRC per field to enable aggresive trimming.
        MIN_FILL_LINES_PF = (STC007DataBlock::MIN_DEINT_DATA/2),                    // Minimum number of lines in per-field buffers to perform padding detection.
    };

    // Paddings limits.
    enum
    {
        MAX_PADDING_14BIT = (STC007DataBlock::INTERLEAVE_OFS*2),    // Maximum available line padding (for 14-bit mode) before Q false corrections will ruin field stitching.
        MAX_PADDING_16BIT = (STC007DataBlock::INTERLEAVE_OFS),      // Maximum available line padding (for 16-bit mode) before two errors per block will be introduced.
        MAX_BURST_SILENCE = (STC007DataBlock::INTERLEAVE_OFS/2),    // Maximum number of consequtive silenced data blocks before field stitching aborts.
        MAX_BURST_BROKEN = 1,                                       // Maximum number of consequtive BROKEN data blocks before field stitching aborts.
        MAX_BURST_UNCH_DELTA = 8,
        MAX_BURST_UNCH_14BIT = 0x40,                                // Default maximum number of consequtive unchecked/Q-corrected 14-bit data blocks before field stitching aborts.
        MAX_BURST_UNCH_16BIT = 0x20,                                // Default maximum number of consequtive unchecked 16-bit data blocks before field stitching aborts.
        UNCH_MASK_DURATION = (STC007DataBlock::INTERLEAVE_OFS*STC007DataBlock::WORD_CNT),   // Default duration (in lines) of masking uncheckable and false-corrected blocks after BROKEN one.
        STATS_DEPTH = 65                                            // History depth for stats.
    };

    // Results of [findFieldResolution()].
    enum
    {
        SAMPLE_RES_UNKNOWN,     // Unable to determine resolution.
        SAMPLE_RES_14BIT,       // Detected field resolution: 14-bit (STC-007).
        SAMPLE_RES_16BIT,       // Detected field resolution: 16-bit (PCM-F1).
        SAMPLE_RES_MAX
    };

    // Results of [tryPadding()] and [findPadding()].
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
        STG_TRY_PREVIOUS,       // Try to stitch fields as before.
        STG_TRY_TFF_TO_TFF,     // Try to stitch current frame with next one as before, assuming Frame A is TFF and Frame B is TFF.
        STG_TRY_BFF_TO_BFF,     // Try to stitch current frame with next one as before, assuming Frame A is BFF and Frame B is BFF.
        STG_A_PREPARE,          // Getting ready to find padding between fields of Frame A.
        STG_A_PAD_TFF,          // Detecting padding between fields of Frame A, assuming TFF (top (odd) field, then bottom (even) field).
        STG_A_PAD_BFF,          // Detecting padding between fields of Frame A, assuming BFF (bottom (even) field, then top (odd) field).
        STG_AB_UNK_PREPARE,     // Getting ready to find padding between different frames, while Frame A padding is unknown.
        STG_AB_TFF_TO_TFF,      // Detecting padding between different frames, assuming Frame A is TFF and Frame B is TFF.
        STG_AB_TFF_TO_BFF,      // Detecting padding between different frames, assuming Frame A is TFF and Frame B is BFF.
        STG_AB_BFF_TO_BFF,      // Detecting padding between different frames, assuming Frame A is BFF and Frame B is BFF.
        STG_AB_BFF_TO_TFF,      // Detecting padding between different frames, assuming Frame A is BFF and Frame B is TFF.
        STG_PAD_NO_GOOD,        // Padding detection failed.
        STG_PAD_SILENCE,        // Padding detection can not be performed on silence.
        STG_PAD_OK,             // Padding done successfully.
        STG_PAD_MAX             // Limiter for stages.
    };

private:
    STC007DataBlock padding_block;              // Data block object, used in padding process.
    STC007Deinterleaver pad_checker;            // Deinterleaver object, used for padding detection.
    STC007Deinterleaver lines_to_block;         // Deinterleaver object, used for final lines-block processing.
    FrameAsmSTC007 frasm_f0;                    // Frame assembling for the previous frame.
    FrameAsmSTC007 frasm_f1;                    // Frame assembling for the current frame (Frame A).
    FrameAsmSTC007 frasm_f2;                    // Frame assembling for the next frame (Frame B).
    QElapsedTimer file_time;                    // Timer for measuring file processing time.
    std::deque<STC007Line> *in_lines;           // Input PCM line quene (shared).
    std::deque<PCMSamplePair> *out_samples;     // Output sample pairs queue (shared).
    QMutex *mtx_lines;                          // Mutex for input queue.
    QMutex *mtx_samples;                        // Mutex for output queue.
    std::vector<STC007Line> trim_buf;           // Internal buffer for collecting two frames and trimming detection.
    std::vector<STC007Line> frame1_even;        // Internal buffer for Frame A even field for padding detection.
    std::vector<STC007Line> frame1_odd;         // Internal buffer for Frame A odd field for padding detection.
    std::vector<STC007Line> frame2_even;        // Internal buffer for Frame B even field for padding detection.
    std::vector<STC007Line> frame2_odd;         // Internal buffer for Frame B odd field for padding detection.
    std::deque<STC007Line> padding_queue;       // Internal buffer for field padding detection.
    std::deque<STC007Line> conv_queue;          // Output PCM lines buffer after trimming and padding, before converting into data blocks.
    std::string file_name;                      // Name of the file being processed (passed onto audio processing chain).
    uint8_t max_unchecked_14b_blocks;           // Maximum number of consequtive unchecked data blocks for a seam in 14-bit mode.
    uint8_t max_unchecked_16b_blocks;           // Maximum number of consequtive unchecked data blocks for a seam in 16-bit mode.
    uint8_t broken_mask_dur;                    // Duration (in lines) for error masking after BROKEN data block (cleans small number of small clicks).
    uint8_t broken_countdown;                   // How many data blocks to invalidate after BROKEN data block.
    uint8_t preset_video_mode;                  // Detected video standard.
    uint8_t preset_field_order;                 // Field order, set externally.
    uint8_t preset_audio_res;                   // Resolution of audio samples, set externally.
    uint8_t last_pad_counter;                   // Last padding runs count in [tryPadding()].
    circarray<uint8_t, STATS_DEPTH> stats_field_order;  // List of last field orders.
    circarray<uint8_t, STATS_DEPTH> stats_resolution;   // List of last audio sample resolutions.
    uint16_t log_level;                         // Level of debug output.
    uint16_t trim_fill;                         // Number of filled lines in [trim_buf] from input queue.
    uint16_t f1_max_line;                       // Largest line number in Frame A (used to determine video standard by line count).
    uint16_t f2_max_line;                       // Largest line number in Frame B (used to determine video standard by line count).
    uint16_t preset_sample_rate;                // Sample rate of audio samples, set externally.
    bool file_start;                            // Detected start of a new file, filename saved to [file_name].
    bool file_end;                              // Detected end of a file.
    bool ignore_CRC;                            // Ignore CRC from video lines or not (and force parity check).
    bool enable_P_code;                         // Enable P-code error correction or not.
    bool enable_Q_code;                         // Enable Q-code error correction or not.
    bool enable_CWD;                            // Enable CWD (Cross-Word Decoding) error correction or not.
    bool mode_m2;                               // Set new M2 mode and disable STC-007/PCM-F1 output.
    bool fix_cut_above;                         // Add artificial line to begining of the frame for frames with invalid seams.
    bool mask_seams;                            // Mark data blocks on incorrect field seams as invalid (cleans most of clicks and pops on bad quality video).
    bool finish_work;                           // Flag to break executing loop.

public:
    explicit STC007DataStitcher(QObject *parent = 0);
    void setInputPointers(std::deque<STC007Line> *in_pcmline = NULL, QMutex *mtx_pcmline = NULL);
    void setOutputPointers(std::deque<PCMSamplePair> *out_pcmsamples = NULL, QMutex *mtx_pcmsamples = NULL);

private:
    void resetState();
    bool waitForTwoFrames();
    void fillUntilTwoFrames();
    void findFramesTrim();
    void splitFramesToFields();
    uint8_t getFieldResolution(std::vector<STC007Line> *field = NULL, uint16_t f_size = 0);
    uint8_t getResolutionModeForSeam(uint8_t in_res_field1, uint8_t in_res_field2);
    uint8_t getResolutionForSeam(uint8_t in_res_field1, uint8_t in_res_field2);
    uint8_t getDataBlockResolution(std::deque<STC007Line> *in_line_buffer = NULL, uint16_t line_sh = 0);
    uint8_t tryPadding(std::vector<STC007Line> *field1 = NULL, uint16_t f1_size = 0,
                        std::vector<STC007Line> *field2 = NULL, uint16_t f2_size = 0,
                        uint16_t padding = 0,
                        FieldStitchStats *stitch_stats = NULL);
    uint8_t findPadding(std::vector<STC007Line> *field1 = NULL, uint16_t f1_size = 0,
                        std::vector<STC007Line> *field2 = NULL, uint16_t f2_size = 0,
                        uint8_t in_std = FrameAsmDescriptor::VID_NTSC, uint8_t in_resolution = STC007DataBlock::RES_14BIT, uint16_t *padding = NULL);
    void clearFieldOrderStats();
    void updateFieldOrderStats(uint8_t new_order);
    uint8_t getProbableFieldOrder();
    void clearResolutionStats();
    void updateResolutionStats(uint8_t new_res);
    uint8_t getProbableResolution();
    void detectAudioResolution();
    void detectVideoStandard();
    uint8_t findFieldStitching();
    uint8_t getAssemblyFieldOrder();
    uint16_t getFirstFieldLineNum(uint8_t in_order);
    uint16_t getSecondFieldLineNum(uint8_t in_order);
    uint16_t addLinesFromField(std::vector<STC007Line> *field_buf, uint16_t ind_start, uint16_t count, uint16_t *last_line_num = NULL);
    uint16_t addFieldPadding(uint32_t in_frame, uint16_t line_cnt, uint16_t *last_line_num = NULL);
    bool isBlockNoReport(STC007DataBlock *in_block);
    void fillFrameForOutput();
    bool fillNextFieldForCWD();
    void removeNextFieldAfterCWD();
    uint16_t patchBrokenLines(std::deque<STC007Line> *in_queue);
    uint16_t patchBrokenLinesOld(std::deque<STC007Line> *in_queue);
    uint16_t performCWD(std::deque<STC007Line> *in_queue);
    void prescanFrame();
    void setBlockSampleRate(STC007DataBlock *in_block);
    void outputFileStart();
    void outputSamplePair(STC007DataBlock *in_block, uint8_t idx_left, uint8_t idx_right);
    void outputDataBlock(STC007DataBlock *in_block = NULL);
    void outputFileStop();
    void performDeinterleave();

public slots:
    void setLogLevel(uint16_t);             // Set logging level.
    void setVideoStandard(uint8_t);         // Preset video standard for stitching.
    void setFieldOrder(uint8_t);            // Preset field order.
    void setPCorrection(bool);              // Enable/disable P-code error correction.
    void setQCorrection(bool);              // Enable/disable Q-code error correction.
    void setCWDCorrection(bool);            // Enable/disable CWD error correction.
    void setM2SampleFormat(bool);           // Enable/disable M2 sample format (13/16-bits in 14-bit words).
    void setResolutionPreset(uint8_t);      // Preset audio resolution.
    void setSampleRatePreset(uint16_t);     // Preset audio sample rate.
    void setFineMaxUnch14(uint8_t);         // Set fine settings: maximum unchecked data blocks for a seam in 14-bit mode.
    void setFineMaxUnch16(uint8_t);         // Set fine settings: maximum unchecked data blocks for a seam in 16-bit mode.
    void setFineUseECC(bool);               // Set fine settings: usage of ECC on CRC-marked words.
    void setFineMaskSeams(bool);            // Set fine settings: usage of unchecked seams masking.
    void setFineBrokeMask(uint8_t);         // Set fine settings: number of lines to mask after BROKEN data block.
    void setFineTopLineFix(bool);           // Set fine settings: allow automatic top-line insertion in odd line counted frames.
    void setDefaultFineSettings();          // Set fine settings to defaults.
    void requestCurrentFineSettings();      // Get current fine settings.
    void doFrameReassemble();               // Main execution loop.
    void stop();                            // Set the flag to break execution loop and exit.

signals:
    void guiUpdFrameAsm(FrameAsmSTC007);    // New frame assembling data calculated in DI thread, need to update GUI.
    void guiUpdFineMaxUnch14(uint8_t);      // New fine setting: maximum unchecked data blocks for a seam in 14-bit mode.
    void guiUpdFineMaxUnch16(uint8_t);      // New fine setting: maximum unchecked data blocks for a seam in 16-bit mode.
    void guiUpdFineUseECC(bool);            // New fine setting: usage of ECC on CRC-marked words.
    void guiUpdFineMaskSeams(bool);         // New fine setting: usage of unchecked seams masking.
    void guiUpdFineBrokeMask(uint8_t);      // New fine setting: number of lines to mask after BROKEN data block.
    void guiUpdFineTopLineFix(bool);        // New fine setting: allow automatic top-line insertion in odd line counted frames.
    void newLineProcessed(STC007Line);      // Processed STC-007 line object (for visualization).
    void newBlockProcessed(STC007DataBlock);    // Processed STC-007 data block object (for visualization).
    void loopTime(quint64);                 // Useful loop time count.
    void finished();                        // Thread is stopped.
};

#endif // STC007DATASTITCHER_H
