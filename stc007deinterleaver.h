#ifndef STC007DEINTERLEAVER_H
#define STC007DEINTERLEAVER_H

#include <deque>
#include <mutex>
#include <stdint.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <vector>
#include "config.h"
#include "frametrimset.h"
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
    bool force_parity_check;        // Setting for forcing parity check even if CRC is ok.
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
    void setForceParity(bool flag = false);
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
