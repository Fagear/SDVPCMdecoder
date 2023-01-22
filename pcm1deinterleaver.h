/**************************************************************************************************************************************************************
pcm1deinterleaver.h

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

Created: 2021-11

Deinterleaver module for PCM-1 format.
To disperse CRC errors and improve error correction capabilities PCM-1 format implement interleaving.
With interleaving original data is grouped, shuffled and re-ordered and than put into lines.
To decode data from a set of lines deinterleaving must be performed to convert lines into data blocks.
PCM-1 does not implement any error-correction capabilities.

[PCM1Deinterleaver] takes a queue of [PCM1SubLine] objects as input,
performs deinterleaving and error correction and than outputs [PCM1DataBlock].

Typical use case:
    - Set pointer to the input [PCM1SubLine] queue with [setInput()];
    - Set pointer to the output [PCM1DataBlock] with [setOutput()];
    - Call [processBlock()] with set interleave block number and set line offset from the start of the provided [PCM1SubLine] queue.
    -- optional: CRC error check can be disabled with [setIgnoreCRC()] and [PCM1Deinterleaver] will assume all words are valid.

**************************************************************************************************************************************************************/

#ifndef PCM1DEINTERLEAVER_H
#define PCM1DEINTERLEAVER_H

#include <deque>
#include <stdint.h>
#include <vector>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include "config.h"
#include "pcm1datablock.h"
#include "pcm1subline.h"

class PCM1Deinterleaver
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

private:
    std::vector<PCM1SubLine> *input_vector;  // Pointer to input PCM1SubLine vector.
    std::deque<PCM1SubLine> *input_deque;    // Pointer to input PCM1SubLine deque.
    PCM1DataBlock *out_data_block;           // Pointer to output PCM1DataBlock.
    uint8_t log_level;              // Setting for debugging log level.
    bool ignore_crc;                // Setting for ignore CRC in source line and assume it's always valid.

public:
    PCM1Deinterleaver();
    void setLogLevel(uint8_t in_level = 0);
    void setInput(std::deque<PCM1SubLine> *in_line_buffer = NULL);
    void setInput(std::vector<PCM1SubLine> *in_line_buffer = NULL);
    void setOutput(PCM1DataBlock *out_data = NULL);
    void setIgnoreCRC(bool flag = false);
    uint8_t processBlock(uint16_t itl_block_num, uint16_t line_sh = 0);

private:
    void setWordData(uint16_t itl_block_num, bool even_stripe, bool use_vector = false, uint16_t line_sh = 0);
};

#endif // PCM1DEINTERLEAVER_H
