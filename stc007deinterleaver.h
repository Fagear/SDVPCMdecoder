/**************************************************************************************************************************************************************
stc007deinterleaver.h

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

Deinterleaver module for STC-007/PCM-F1/M2 formats.
To disperse CRC errors and improve error correction capabilities STC-007/PCM-F1/M2 formats implement interleaving.
With interleaving original data is grouped, shuffled and re-ordered and than put into lines.
To decode data from a set of lines deinterleaving must be performed to convert lines into data blocks.
After deinterleaving error correction algorythms can process data in each data block.

[STC007Deinterleaver] takes a queue of [STC007Line] objects as input,
performs deinterleaving and error correction and than outputs [STC007DataBlock].

Typical use case:
    - Set pointer to the input [STC007Line] queue with [setInput()];
    - Set pointer to the output [STC007DataBlock] with [setOutput()];
    - Set resolution mode with [setResMode()] to [RES_MODE_14BIT] for STC-007/M2 or to [RES_MODE_16BIT] for PCM-F1;
    - Call [processBlock()] with set line offset from the start of the provided [STC007Line] queue.
    -- optional: CRC error check can be disabled with [setIgnoreCRC()] and [STC007Deinterleaver] will assume all words are valid.
                 Forced error check can be toggled with [setForcedErrorCheck()], this enables detection of "BROKEN" data in incorrectly assembled data blocks.
                 P-code ECC capability can be toggled with [setPCorrection()].
                 Q-code ECC capability can be toggled with [setQCorrection()] (used only for [RES_MODE_14BIT]).
                 Cross-Word Decoding (CWD) ECC assist can be toggled with [setCWDCorrection()],
                 this can improve error correction capabilities beyond 2 invalid words per data block in some cases.
                 CWD does not correct anything by itself, it relies on P and/or Q codes and additional buffer pre-scans.
                 CWD does not guarantee error correction beyond 2 invalid words per data block.

STC-007 standard uses CRC to perform error-detection and P-code and Q-code in a data block to perform error correction.
P-code is a simple parity word for all audio words. It is used for correcting a single word error in a data block.
Q-code is a part of interleaved matrix code (see "b-Adjacent Error Correction") and is not available in 16-bit PCM-F1.
Q-code (and P-code) is used for correcting double word errors in a data block or for correcting a single word error when P-code is invalid.
In STC-007 standard full error recovery is possible if there is 2 or less invalid words in a 8-word data block.
In PCM-F1 full error recovery is possible if there is 1 or less invalid words in a 8-word data block.
In combination with 16H word interleave it gives capability to fully recover up to 32 consecutive damaged lines (STC-007)
or up to 16 consecutive damaged lines (PCM-F1) in a span of 128 lines.

Cross-Word Decoding (CWD) is a method of utilizing existing ECC data to recover more than 2 errors per data block.
It is based on a assumption that not all words in the line may be damaged while CRC marks all words in the line as invalid,
because CRC code can not locate error position.
Additional pre-scans and checks on the data buffer must be performed to allow P-code and Q-code corrections to fix errors (up to 2 per data block)
and than fixed words are back-fed into their source lines and CRC checks are re-done. If there was a dropout that didn't affect the whole line,
CRC may became valid, "freeing up" other samples in the line from invalid flags and allowing for more valid samples to be present in the data stream
and that can lead to decreasing error count from 3 (or more) per data block down to 2 (or less) that makes more error corrections possible.
But all that assumes some luck with error distribution and CWD does not guarantee to recover more than 3 errors per data block.
No hardware decoders are known for STC-007 that implement CWD assistance.

[STC007Deinterleaver] has Q-code matrixes stored as constant arrays for fast calculations (see beginning of the [stc007deinterleaver.c]).

**************************************************************************************************************************************************************/

#ifndef STC007DEINTERLEAVER_H
#define STC007DEINTERLEAVER_H

#include <deque>
#include <stdint.h>
#include <vector>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include "config.h"
//#include "frametrimset.h"
#include "stc007datablock.h"

#ifndef QT_VERSION
    #undef DI_EN_DBG_OUT
#endif

//#define DI_EN_DBG_OUT       1       // Enable debug console output in [stc007datablock] module.

class STC007Deinterleaver
{
public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),      // External operations with settings.
        LOG_PROCESS = (1<<1),       // General stage-by-stage logging.
        LOG_ERROR_CORR = (1<<2),    // Error detection and correction.
    };

    // Results of [processBlock()].
    enum
    {
        DI_RET_NULL_LINES,      // Null poiner to input PCM line buffer was provided, unable to process.
        DI_RET_NULL_BLOCK,      // Null poiner to output PCM data block was provided, unable to process.
        DI_RET_NO_DATA,         // Provided PCM line buffer is too small to get data out.
        DI_RET_OK               // Processing done successfully.
    };

    // Audio resolution mode.
    enum
    {
        RES_MODE_14BIT,         // Stick with 14-bit decoding (can cause broken field seams and artifacts on 16-bit audio).
        RES_MODE_14BIT_AUTO,    // Prefer to decode into 14-bit, even if audio is 16-bit (STC-007 mode).
        RES_MODE_16BIT_AUTO,    // Prefer to decode into 16-bit, falling back to 14-bit if resolution of audio was switched (can cause audio artifacts).
        RES_MODE_16BIT,         // Stick with 16-bit decoding (can cause broken field seams and artifacts on 14-bit audio).
        RES_MODE_MAX
    };

    enum
    {
        NO_ERR_INDEX = 64       // Value for "index not set" for bad sample location.
    };

    enum
    {
        MAX_PASSES = 3          // Maximum number of deinterleave passes to determine resolution.
    };

    // Stages of processing.
    enum
    {
        STG_DATA_FILL,      // Fill in data words from PCM lines.
        STG_ERROR_CHECK,    // Get number of audio samples with bad CRC and their positions.
        STG_TASK_SELECTION, // Try to decide what to do with errors in the data block.
        STG_CWD_CORR,       // Fix data with Cross-Word Decoding.
        STG_P_CORR,         // Trying to fix errors with P-code.
        STG_Q_CORR,         // Trying to fix errors with Q-code.
        STG_BAD_BLOCK,      // Dropout detected in data block.
        STG_NO_CHECK,       // Audio data can not be checked.
        STG_DATA_OK,        // Audio data is ok/fixed.
        STG_CONVERT_MAX,
    };

    // Results of correction.
    enum
    {
        FIX_NOT_NEED,       // No parity error was found, data does not need fixing.
        FIX_SWITCH_P,       // Fix can not be performed with Q-code, need to switch to P-code.
        FIX_BROKEN,         // Error(s) can not be fixed.
        FIX_NA,             // Fix can not be performed with Q-code.
        FIX_DONE            // Error was fixed.
    };

private:
    std::vector<STC007Line> *input_vector;  // Pointer to input STC007Line vector.
    std::deque<STC007Line> *input_deque;    // Pointer to input STC007Line deque.
    STC007DataBlock *out_data_block;        // Pointer to output STC007DataBlock.
    uint8_t log_level;              // Setting for debugging log level.
    uint8_t data_res_mode;          // Sample resolution mode.
    bool ignore_crc;                // Setting for ignore CRC in source line and assume it's always valid.
    bool force_ecc_check;           // Setting for forcing P-code and/or Q-code check even if CRC is ok.
    bool en_p_code;                 // Setting for allowing errors to be P-code corrected.
    bool en_q_code;                 // Setting for allowing errors to be Q-code corrected (14-bit only).
    bool en_cwd;                    // Setting for allowing errors to be corrected with Cross-Word Decoding.
    uint8_t proc_state;             // State of processing.

public:
    STC007Deinterleaver();
    void clear();
    void setLogLevel(uint8_t in_level = 0);
    void setInput(std::deque<STC007Line> *in_line_buffer = NULL);
    void setInput(std::vector<STC007Line> *in_line_buffer = NULL);
    void setOutput(STC007DataBlock *out_data = NULL);
    void setResMode(uint8_t in_resolution = RES_MODE_14BIT_AUTO);
    void setIgnoreCRC(bool flag = false);
    void setForcedErrorCheck(bool flag = true);
    void setPCorrection(bool flag = true);
    void setQCorrection(bool flag = true);
    void setCWDCorrection(bool flag = false);
    uint8_t processBlock(uint16_t line_shift = 0);

private:
    void setWordData(STC007Line *line_L0, STC007Line *line_R0,
                     STC007Line *line_L1, STC007Line *line_R1,
                     STC007Line *line_L2, STC007Line *line_R2,
                     STC007Line *line_P0, STC007Line *line_Q0,
                     STC007DataBlock *out_data_block,
                     uint8_t in_resolution = STC007DataBlock::RES_14BIT);
    static uint16_t calcPcode(STC007DataBlock *data_block);
    static uint16_t calcQcode(STC007DataBlock *data_block);
    static uint16_t calcSyndromeP(STC007DataBlock *data_block);
    static uint16_t calcSyndromeQ(STC007DataBlock *data_block);
    void recalcP(STC007DataBlock *data_block);
    uint8_t fixByP(STC007DataBlock *data_block, uint8_t first_bad);
    uint8_t fixByQ(STC007DataBlock *data_block, uint8_t first_bad, uint8_t second_bad);
    static uint16_t multMatrix(uint16_t *matrix, uint16_t vector);
    static uint8_t bitXOR(uint16_t vector);
};

#endif // STC007DEINTERLEAVER_H
