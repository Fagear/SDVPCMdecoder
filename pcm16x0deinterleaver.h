#ifndef PCM16X0DEINTERLEAVER_H
#define PCM16X0DEINTERLEAVER_H

#include <deque>
#include <mutex>
#include <stdint.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <vector>
#include "config.h"
#include "frametrimset.h"
#include "pcm16x0datablock.h"

#ifndef QT_VERSION
    #undef DI_EN_DBG_OUT
#endif

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
        FORMAT_AUTO,            // Automatic format detection.
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
        STG_CRC_CHECK,      // 0 Get number of audio samples with bad CRC and their positions.
        STG_P_CORR,         // 1 Trying to fix errors with P-code.
        STG_BAD_BLOCK,      // 2 Dropout detected in data block.
        STG_NO_CHECK,       // 3 Audio data can not be checked.
        STG_DATA_OK,        // 4 Audio data is ok/fixed.
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
    PCM16X0DataBlock *out_data_block;           // Pointer to output PCM16X0DataBlock.
    uint8_t log_level;              // Setting for debugging log level.
    uint8_t no_pcm_mode;            // Mode of "No PCM" lines sample fill.
    bool force_parity_check;        // Setting for forcing parity check even if CRC is ok.
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
    void setForceParity(bool flag = false);
    void setPCorrection(bool flag = true);
    void setSIFormat();
    void setEIFormat();
    uint8_t processBlock(uint16_t line_sh = 0, bool even_order = false);

private:
    void setWordData(PCM16X0SubLine *line1, PCM16X0SubLine *line2, PCM16X0SubLine *line3, PCM16X0DataBlock *out_data_block);
    uint16_t calcSyndromeP(PCM16X0DataBlock *data_block, uint8_t blk);
    uint8_t fixByP(PCM16X0DataBlock *data_block, uint8_t blk, uint8_t bad_ptr = PCM16X0Deinterleaver::NO_ERR_INDEX);
};

#endif // PCM16X0DEINTERLEAVER_H
