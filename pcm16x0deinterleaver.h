/**************************************************************************************************************************************************************
pcm16x0deinterleaver.h

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

Created: 2021-07

Deinterleaver module for PCM-1600/PCM-1610/PCM-1630 formats.
To disperse CRC errors and improve error correction capabilities PCM-16x0 formats implement interleaving.
With interleaving original data is grouped, shuffled and re-ordered and than put into lines.
To decode data from a set of lines deinterleaving must be performed to convert lines into data blocks.
After deinterleaving error correction algorythms can process data in each data block.

[PCM16X0Deinterleaver] takes a queue of [PCM16X0SubLine] objects as input,
performs deinterleaving and error correction and than outputs [PCM16X0DataBlock].

Typical use case:
    - Set pointer to the input [PCM16X0SubLine] queue with [setInput()];
    - Set pointer to the output [PCM16X0DataBlock] with [setOutput()];
    - Set SI format with [setSIFormat()] or EI format (PCM-1630) is required with [setEIFormat()];
    - Call [processBlock()] with set line offset from the start of the provided [PCM16X0SubLine] queue and set sample order.
    -- optional: CRC error check can be disabled with [setIgnoreCRC()] and [PCM16X0Deinterleaver] will assume all words are valid.
                 Forced error check can be toggled with [setForcedErrorCheck()], this enables detection of "BROKEN" data in incorrectly assembled data blocks.
                 P-code ECC capability can be toggled with [setPCorrection()].

PCM-16x0 formats use CRC to perform error-detection and P-code in a data block to perform error correction.
P-code is a simple parity word for a pair of audio words. It is used for correcting a single word error in a sub-block of a data block.

**************************************************************************************************************************************************************/

#ifndef PCM16X0DEINTERLEAVER_H
#define PCM16X0DEINTERLEAVER_H

#include <deque>
#include <stdint.h>
#include <vector>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include "config.h"
#include "pcm16x0datablock.h"

#ifndef QT_VERSION
    #undef DI_EN_DBG_OUT
#endif

// TODO: CWD for PCM-16x0
class PCM16X0Deinterleaver
{
public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),      // External operations with settings.
        LOG_PROCESS = (1<<1),       // General stage-by-stage logging.
        LOG_ERROR_CORR = (1<<2),    // Error detection and correction.
    };

    // Format setting.
    enum
    {
        FORMAT_AUTO,            // Automatic format detection (TODO).
        FORMAT_SI,              // PCM-1600 or PCM-1630 SI format.
        FORMAT_EI,              // PCM-1630 EI format.
        FORMAT_MAX
    };

    // Results of [processBlock()].
    enum
    {
        DI_RET_NULL_LINES,      // Null poiner to input PCM line buffer was provided, unable to process.
        DI_RET_NULL_BLOCK,      // Null poiner to output PCM data block was provided, unable to process.
        DI_RET_NO_DATA,         // Provided PCM line buffer is too small to get data out.
        DI_RET_OK               // Processing done successfully.
    };

    enum
    {
        NO_ERR_INDEX = 64       // Value for "index not set" for bad sample location.
    };

    // Stages of processing.
    enum
    {
        STG_CRC_CHECK,      // Get number of audio samples with bad CRC and their positions.
        STG_P_CORR,         // Trying to fix errors with P-code.
        STG_BAD_BLOCK,      // Dropout detected in data block.
        STG_NO_CHECK,       // Audio data can not be checked.
        STG_DATA_OK,        // Audio data is ok/fixed.
        STG_CONVERT_MAX,
    };

    // Results of correction.
    enum
    {
        FIX_NOT_NEED,       // No parity error was found, data does not need fixing.
        FIX_BROKEN,         // Error(s) can not be fixed.
        FIX_DONE            // Error was fixed.
    };

private:
    std::vector<PCM16X0SubLine> *input_vector;  // Pointer to input [PCM16X0SubLine] vector.
    std::deque<PCM16X0SubLine> *input_deque;    // Pointer to input [PCM16X0SubLine] deque.
    PCM16X0DataBlock *out_data_block;           // Pointer to output [PCM16X0DataBlock].
    uint8_t log_level;              // Setting for debugging log level.
    bool force_ecc_check;           // Setting for forcing parity check even if CRC is ok.
    bool en_p_code;                 // Setting for allowing errors to be P-code corrected.
    bool ignore_crc;                // Setting for ignore CRC in source line and assume it's always valid.
    bool ei_format;                 // Setting for deinterleaving in EI format instead of SI format.
    uint8_t proc_state;             // State of processing.

public:
    PCM16X0Deinterleaver();
    void setLogLevel(uint8_t in_level = 0);
    void setInput(std::deque<PCM16X0SubLine> *in_line_buffer = NULL);
    void setInput(std::vector<PCM16X0SubLine> *in_line_buffer = NULL);
    void setOutput(PCM16X0DataBlock *out_data = NULL);
    void setIgnoreCRC(bool flag = false);
    void setForcedErrorCheck(bool flag = true);
    void setPCorrection(bool flag = true);
    void setSIFormat();
    void setEIFormat();
    uint8_t processBlock(uint16_t line_sh = 0, bool even_order = false);

private:
    void setWordData(PCM16X0SubLine *line1, PCM16X0SubLine *line2, PCM16X0SubLine *line3, PCM16X0DataBlock *out_data_block);
    static uint16_t calcPcode(PCM16X0DataBlock *data_block, uint8_t blk);
    static uint16_t calcSyndromeP(PCM16X0DataBlock *data_block, uint8_t blk);
    uint8_t fixByP(PCM16X0DataBlock *data_block, uint8_t blk, uint8_t bad_ptr = PCM16X0Deinterleaver::NO_ERR_INDEX, uint16_t synd_mask = 0x00);
};

#endif // PCM16X0DEINTERLEAVER_H
