/*
* binarizer.h
*
* Created:          2020-03
* Modified:         2021-11-18
* Author:           Maksim Kryukov aka Fagear (fagear@mail.ru)
*
* Description:      Binarizator module.
*
*                   This module takes one video line (of type [VideoLine]) and performs:
*                       - AGC (Automatic Gain Control), detecting BLACK and WHITE levels;
*                       - Automatic reference level detection, detecting the best threshold for binarization;
*                       - TBC (Time-Based Correction), detecting horizontal offset of the PCM data;
*                       - binarization with set mode;
*                       - Brute force picking bits that were cut off the edge of the video line (for PCM-1 and PCM-16x0).
*
*                   Binarization itself can be done with different speed/quality ratios,
*                   that are set with [setMode()].
*
*                   Output of the [processLine()] is one [PCMLine]
*                   which contains data bits and misc. information about source video line and its settings.
*
*                   Binarization process can be much faster with use of
*                   [setGoodParameters()], [setReferenceLevel()] and [setDataCoordinates()] functions,
*                   providing those with known good reference level and data coordinates
*                   from [PCMLine] object which has [isCRCvalid()] set to "true".
*
*                   The goal of the [Binarizer] is to get valid CRC for the [PCMLine]
*                   iteratively adjusting various parameters of binarization process,
*                   while gathering various statistics and using it to produce best result.
*
*                   Typical use case:
*                       - Set pointer to the input video line with [setSource()];
*                       - Set pointer to the output PCM line with [setOutput()];
*                       - Set mode to desired speed/quality with [setMode()];
*                       - Call [processLine()] to perform binarization of video line.
*                       -- optional: if binarization produces a good CRC (PCMLine.isCRCValid())
*                                    provide "good" parameters from the result to [Binarizer]
*                                    by calling [setGoodParameters()] with "good" [PCMLine] as an argument
*                                    or [setBWLevels()], [setReferenceLevel()], [setDataCoordinates()],
*                                    that will significantly increase binarization speed on other
*                                    [processLine()] calls.
*                                    Those "good" parameters can be reset by calling [setGoodParameters()]
*                                    with no parameters.
*
*                   PCM-1 and PCM-16x0 formats do not have distinct markers to determine data coordinates,
*                   so TBC for those formats is done via brute-force sweeping START and STOP coordinates until CRCs are good.
*                   That is extremely resource heavy and slow.
*                   Automatic coordinates search (Marco-TBC) can be enabled/disabled by [setCoordinatesSearch()].
*
*                   Final data coordinates... can be NEGATIVE and can exceed limits of source video line.
*                   That happens because PCM data can be cut off the screen with bad video capture.
*                   And so START coordinate can be more to the left that the first pixel in video line (0) and
*                   STOP coordinate can be more to the right that the last pixel in video line.
*                   Also that means that some PCM data is lost from the source video.
*                   AND that data may be brute-force picked for good CRC, because [Binarizer]
*                   can determine how many bits were cut from the video on each side.
*                   Automatic bit-picker can be enabled/disabled by [setBitPicker()].
*/

#ifndef BINARIZER_H
#define BINARIZER_H

#include <mutex>
#include <thread>
#include <stdint.h>
#include <string>
#include <vector>
#include <QElapsedTimer>
#include <QDebug>
#include "config.h"
#include "bin_preset_t.h"
#include "frametrimset.h"
#include "pcm1line.h"
#include "pcm16x0subline.h"
#include "stc007line.h"
#include "videoline.h"

#ifndef QT_VERSION
    #undef LB_EN_DBG_OUT
#endif

#ifdef LB_EN_DBG_OUT
    //#define LB_LOG_BW_VERBOSE       1       // Produce verbose output for B&W search process.
    //#define LB_LOG_MARKER_VERBOSE   1       // Produce verbose output for START/STOP marker search process.
#endif

#ifndef LB_EN_PIXEL_DBG
    #define getPixelBrightness(x_coord) video_line->pixel_data[x_coord]
#endif

// TODO: convert [crc_handler_t] from [int16_t] to [CoordinatePair].
// Element of statistics array for CRC verification.
typedef struct
{
    uint8_t result;         // CRC state for the line.
    uint16_t crc;           // CRC for the line.
    uint8_t hyst_dph;       // Reference level hysteresis for current CRC.
    uint8_t shift_stg;      // Pixel shifting stage for current CRC.
    int16_t data_start;     // Coordinate of data start in the line for current CRC.
    int16_t data_stop;      // Coordinate of data stop in the line for current CRC.
} crc_handler_t;

// TODO: add preset data coordinates offset [-30...+30] [bin_preset_t] and fine settings in GUI
// TODO: add counters for left/right bits instead of [en_bit_picker] in [bin_preset_t] and fine settings in GUI
class Binarizer
{
public:
    // Results.
    enum
    {
        LB_RET_OK,              // Success.
        LB_RET_NULL_VIDEO,      // Null poiner to input video line was provided, unable to process.
        LB_RET_NULL_PCM,        // Null poiner to output PCM line was provided, unable to process.
        LB_RET_SHORT_LINE,      // Provided video line is too short to have PCM data.
        LB_RET_NO_COORD,        // PCM data coordinates were not detected.
    };

    // Pixel Per Bit (PPB) limits.
    enum
    {
        PPB_MAX = 50,           // Maximum PPB for processing.
        PPB_EQ_PASSES_MAX = 7   // Maximum number of PPB equalization tries per line in [calcRefLevelByPPB()].
    };

    // CRC limits.
    enum
    {
        MIN_VALID_CRCS = 5,     // Minimum number of runs that produce valid CRCs in [pickLevelByCRCStats()].
        MAX_COLL_CRCS = 32,     // Maximum number of "valid" CRCs per line/operation while detecting CRC collision.
        MIN_CRC_CNT_DELTA = 2   // Minimum difference between CRC counts for multiple CRCs (CRC collision) in a single line.
    };

    // Reference level hysteresis limits.
    enum
    {
        HYST_DEPTH_MIN = 0,     // Minimum hysteresis depth of reference level.
        HYST_DEPTH_SAFE = 4,    // Recommended depth of hysteresis sweep that is safe for realtime decoding.
        HYST_DEPTH_MAX = 10,    // Maximum allowed depth of hysteresis sweep of reference level.
        HYST_MIN_GOOD_SPAN = 3  // Minimum number of "good CRC" runs for hysteresis sweep.
    };

    // Pixel-shifting (Micro-TBC) limits.
    enum
    {
        SHIFT_STAGES_MIN = 0,   // Number of minimum possible pixel-shifting stages.
        SHIFT_STAGES_SAFE = 2,  // Index of maximum pixel-shift stage which is safe to use without many false-positive CRCs.
        SHIFT_STAGES_MAX = (PCM_LINE_MAX_PS_STAGES-1)   // Number of maximum available pixel-shift stages.
    };

    // PCM-1 data coordinates search settings.
    enum
    {
        PCM1_SEARCH_STEP_DIV = 4,       // PPB divider for data coordinates search pixel offset.
        PCM1_SEARCH_MAX_OFS = 12,       // Maximum offset from provided starting coordinate.
        PCM1_SEARCH_STEP_CNT = ((PCM1_SEARCH_MAX_OFS+1)*2),         // Number of steps for full coordinate sweep.
    };

    // PCM-16x0 data coordinates search settings.
    enum
    {
        PCM16X0_SEARCH_STEP_DIV = 2,    // PPB divider for data coordinates search pixel offset.
        PCM16X0_SEARCH_MAX_OFS = 10,    // Maximum offset from provided starting coordinate.
        PCM16X0_SEARCH_STEP_CNT = ((PCM16X0_SEARCH_MAX_OFS+1)*2),   // Number of steps for full coordinate sweep.
    };

    // Bit Picker defaults.
    enum
    {
        PCM1_PICKBITS_MAX_LEFT = 4,     // Maximum number of bits from the left side of the PCM-1 line to pick.
        PCM1_PICKBITS_MAX_RIGHT = 2,    // Maximum number of bits from the right side of the PCM-1 line to pick.
        PCM16X0_PICKBITS_MAX_LEFT = 8,  // Maximum number of bits from the left side of the PCM-16x0 line to pick.
        PCM16X0_PICKBITS_MAX_RIGHT = 5  // Maximum number of bits from the right side of the PCM-16x0 line to pick.
    };

    // Console logging options for [setLogLevel()] and [log_level] (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),  // External operations with settings.
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_COORD = (1<<2),     // Data coordinates search.
        LOG_PIXELS = (1<<3),    // Output pixel coordinates and string of binarized data.
        LOG_BRIGHT = (1<<4),    // Output brightness spread data.
        LOG_SWEEP = (1<<5),     // Output full process of reference level sweeping.
        LOG_READING = (1<<6),   // Final binarization run.
        LOG_LINE_DUMP = (1<<7), // Output resulting line.
    };

    // Binarization modes for [setMode()] and [bin_mode].
    enum
    {
        MODE_DRAFT,             // Maximum speed, level sweep and pixel shifting stages are disbled.
        MODE_FAST,              // Pretty fast, some pixel shifting stages are enabled, but level sweeping is disabled.
        MODE_NORMAL,            // Fast on good files, sluggish on noisy ones (level sweep is enabled, limited count of pixel shifting stages are enabled).
        MODE_INSANE,            // Incredibly slow on noisy files (level sweeping and all pixel shifting stages are enabled).
        MODE_MAX                // Limiter for mode operations.
    };

    // Line part setting for [setLinePartMode()] and [line_part_mode].
    enum
    {
        FULL_LINE,              // Binarize full video line (for PCM-1 and STC-007).
        PART_PCM16X0_LEFT,      // Binarize 1st part of video line (for PCM-16x0).
        PART_PCM16X0_MIDDLE,    // Binarize 2nd part of video line (for PCM-16x0).
        PART_PCM16X0_RIGHT,     // Binarize 3rd part of video line (for PCM-16x0).
        PART_MAX                // Limiter for setting validation.
    };

    // Stages of main state machine for [proc_state].
    enum
    {
        STG_INPUT_ALL,          // Try to get valid PCM data with provided reference level and data coordinates.
        STG_INPUT_LEVEL,        // Try to get valid PCM data just with provided reference level (search for data coordinates).
        STG_REF_FIND,           // No external data provided, or no valid PCM data was found with provided data, try to find reference level and data coordinates.
        STG_REF_SWEEP_RUN,      // Full reference level sweep mode.
        STG_READ_PCM,           // Perform final binarization, get PCM data with provided or found parameters.
        STG_DATA_OK,            // Exit-state, valid PCM data (CRC OK) was detected in the line.
        STG_NO_GOOD,            // Exit-state, no valid PCM data was detected (no PCM markers or bad CRC).
        STG_MAX                 // Stage limiter.
    };

    // State of line binarization for level sweep stats.
    enum
    {
        REF_NO_PCM,             // No PCM markers found.
        REF_BAD_CRC,            // PCM markers were found, but CRC was bad.
        REF_CRC_COLL,           // Detected CRC collision (more than one "valid" CRC).
        REF_CRC_OK              // Valid PCM data found, CRC ok.
    };

    // Results of [pickRefAfterScan()].
    enum
    {
        SPAN_NOT_FOUND,         // No reference levels with good CRC.
        SPAN_TOO_NARROW,        // Span of reference levels with good CRC is too small.
        SPAN_OK                 // Wide enough span of reference levels with good CRC was found, one level with minimum pixel-shifting and hysteresis is picked.
    };

private:
    bin_preset_t digi_set;              // "Digitizer preset" fine settings.
    VideoLine *video_line;              // Pointer to input VideoLine.
    PCMLine *out_pcm_line;              // Pointer to output PCMLine.
    uint8_t log_level;                  // Setting for debugging log level.
    uint8_t in_def_black;               // External default BLACK level.
    uint8_t in_def_white;               // External default WHITE level.
    uint8_t in_def_reference;           // External default reference level.
    CoordinatePair in_def_coord;        // External default data pixel coordinates.
    uint8_t in_max_hysteresis_depth;    // Maximum depth of reference level hysteresis sweeping.
    uint8_t in_max_shift_stages;        // Maximum number of active pixel-shifting stages.
    bool do_ref_lvl_sweep;              // Setting for performing full reference level sweep to determine best value.
    bool do_coord_search;               // Setting for temporary override [digi_set.en_coord_search].
    bool force_bit_picker;              // Setting for forcing bit-limited Bit Picker even if coordinates do not clip.

    uint8_t proc_state;                 // State of processing.
    uint8_t bin_mode;                   // Binarization mode.
    uint8_t line_part_mode;             // Part of the video line to binarize (for PCM-16x0).
    uint8_t hysteresis_depth_lim;       // Current maximum depth of reference level hysteresis sweeping.
    uint8_t shift_stages_lim;           // Current maximum number of active pixel-shifting stages.
    uint16_t line_length;               // Length of current [VideoLine] (in pixels).
    uint16_t scan_start;                // Starting pixel at the left for all operations.
    uint16_t scan_end;                  // Ending pixel at the right for all operations.
    uint16_t mark_start_max;            // Maximum pixel coordinate of the START marker search area (for STC-007).
    uint16_t mark_end_min;              // Minimum pixel coordinate of the STOP marker search area (for STC-007).
    uint16_t estimated_ppb;             // Estimated or calculated PPB for marker coordinates verify.
    bool was_BW_scanned;                // Was [findBlackWhite()] called at least once while processing the line?
    uint16_t brightness_spread[256];    // Brightness spread statistics.
    crc_handler_t scan_sweep_crcs[256];             // CRC data for reference level sweep stats.
    crc_handler_t shift_crcs[SHIFT_STAGES_MAX+1];   // CRC data for pixel shifting.
    crc_handler_t hyst_crcs[HYST_DEPTH_MAX+1];      // CRC data for hysteresis sweep.
    crc_handler_t crc_stats[MAX_COLL_CRCS+1];       // CRC stats and counters.

public:
    Binarizer();
    // External settings.
    void setLogLevel(uint8_t in_level = 0);
    void setSource(VideoLine *in_ptr = NULL);
    void setOutput(PCMLine *out_ptr = NULL);
    void setMode(uint8_t in_mode = MODE_FAST);
    void setLinePartMode(uint8_t in_mode = FULL_LINE);
    void setCoordinatesSearch(bool in_flag = true);
    void setBWLevels(uint8_t in_black = 0, uint8_t in_white = 0);
    void setReferenceLevel(uint8_t in_ref = 0);
    void setDataCoordinates(int16_t in_data_start = 0, int16_t in_data_stop = 0);
    void setDataCoordinates(CoordinatePair in_data_coord);
    void setGoodParameters(PCMLine *in_pcm_line = NULL);
    // Fine settings.
    bin_preset_t getDefaultFineSettings();
    bin_preset_t getCurrentFineSettings();
    void setFineSettings(bin_preset_t in_set);
    // Return some information.
    bool areBWLevelsPreset();
    bool isRefLevelPreset();
    // MAIN function.
    uint8_t processLine();

private:
#ifdef LB_EN_PIXEL_DBG
    uint8_t getPixelBrightness(uint16_t x_coord);
#endif
    // Statistics operations.
    void fillArray(uint16_t *in_arr, uint16_t count, uint16_t value = 0);
    void resetCRCStats(crc_handler_t *crc_array, uint16_t count, uint8_t *valid_cnt = NULL);
    void updateCRCStats(crc_handler_t *crc_array, crc_handler_t in_crc, uint8_t *valid_cnt);
    void findMostFrequentCRC(crc_handler_t *crc_array, uint8_t *valid_cnt, bool skip_equal = true, bool no_log = false);
    void invalidateNonFrequentCRCs(crc_handler_t *crc_array, uint8_t low_level, uint8_t high_level, uint8_t valid_cnt, uint16_t target_crc, bool no_log = false);
    uint8_t pickLevelByCRCStats(crc_handler_t *crcs, uint8_t *ref_result, uint8_t low_lvl, uint8_t high_lvl,
                                uint8_t target_result = REF_CRC_OK, uint8_t max_hyst = HYST_DEPTH_MAX, uint8_t max_shift = SHIFT_STAGES_MAX);
    uint8_t pickLevelByCRCStatsOpt(crc_handler_t *crcs, uint8_t *ref_result, uint8_t low_lvl, uint8_t high_lvl,
                                uint8_t target_result = REF_CRC_OK, uint8_t max_hyst = HYST_DEPTH_MAX, uint8_t max_shift = SHIFT_STAGES_MAX);
    void textDumpCRCSweep(crc_handler_t *crc_array, uint8_t low_level, uint8_t high_level, uint16_t target1 = 0xFFFF, uint16_t target2 = 0xFFFF);
    // BLACK and WHITE levels detection (AGC).
    uint16_t getMostFrequentBrightnessCount();
    uint8_t getUsefullLowLevel();
    uint8_t getUsefullHighLevel();
    bool findBlackWhite();
    // Reference level detection.
    uint8_t getLowLevel(uint8_t in_lvl, uint8_t diff);
    uint8_t getHighLevel(uint8_t in_lvl, uint8_t diff);
    uint8_t pickCenterRefLevel(uint8_t lvl_black, uint8_t lvl_white);
    uint8_t calcRefLevelByPPB(uint8_t lvl_black, uint8_t lvl_white);
    void sweepRefLevel(PCMLine *pcm_line, crc_handler_t *crc_res);
    void calcRefLevelBySweep(PCMLine *pcm_line = NULL);
    // Data coordinates detection (Macro-TBC).
    uint8_t searchPCM1Data(PCM1Line *pcm_line, CoordinatePair data_loc);
    uint8_t searchPCM16X0Data(PCM16X0SubLine *pcm_line, CoordinatePair data_loc);
    bool findPCM1Coordinates(PCM1Line *pcm1_line, CoordinatePair coord_history);
    bool findPCM16X0Coordinates(PCM16X0SubLine *pcm16x0_line, CoordinatePair coord_history);
    void findSTC007Markers(STC007Line *stc_line, bool no_log = false);
    // Lost bits recovery.
    uint8_t pickCutBitsUpPCM1(PCM1Line *pcm_line, bool no_log = false);
    uint8_t pickCutBitsUpPCM16X0(PCM16X0SubLine *pcm_line, bool no_log = false);
    // Binarization (with reference level hysteresis and pixel-shifting Micro-TBC).
    uint8_t fillPCM1(PCM1Line *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    uint8_t fillPCM16X0(PCM16X0SubLine *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    uint8_t fillSTC007(STC007Line *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    uint8_t fillDataWords(PCMLine *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    void readPCMdata(PCMLine *fill_pcm_line, uint8_t thread_id = 0, bool no_log = false);
};

#endif // BINARIZER_H
