#ifndef PCM1DEINTERLEAVER_H
#define PCM1DEINTERLEAVER_H

#include <deque>
#include <stdint.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <vector>
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
