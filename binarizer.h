﻿/**************************************************************************************************************************************************************
binarizer.h

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

Created: 2020-03

Binarizator module.
This module takes one video line (of type [VideoLine]) and performs:
    - AGC (Automatic Gain Control), statistically detecting BLACK and WHITE levels;
    - ARLD (Automatic Reference Level Detection), detecting the best threshold for binarization;
    - TBC (Time-Based Correction), detecting horizontal offset of the PCM data in the line;
    - Binarization with set mode;
    - Brute force picking bits that were cut off the edge of the video line (for PCM-1 and PCM-16x0).

Binarization itself can be done with different speed/quality ratios,
that are set with [setMode()].

Output of the [processLine()] is one [PCMLine]
which contains data bits and misc. information about source video line and its parameters.

Binarization process can be much faster with use of
[setGoodParameters()], [setReferenceLevel()] and [setDataCoordinates()] functions,
providing those with known good reference level and data coordinates
from [PCMLine] object which has [isCRCvalid()] set to "true".
If those parameters are set than [Binarizer] skips most of the processing and tries
to read PCM data directly. And only if does not succeed than it restarts and
performs all stages of detecting optimal parameters for binarizing the line.

The goal of the [Binarizer] is to get valid CRC for the [PCMLine]
iteratively adjusting various parameters of binarization process,
while gathering various statistics and using those to produce best result.

Typical use case:
    - Set pointer to the input video line with [setSource()];
    - Set pointer to the output PCM line with [setOutput()];
    - Set mode for desired speed/quality with [setMode()];
    - Call [processLine()] to perform binarization of video line.
    -- optional: if binarization produces a good CRC (PCMLine.isCRCValid()==true)
                 provide "good" parameters from the result to [Binarizer]
                 by calling [setGoodParameters()] with "good" [PCMLine] as an argument
                 or by calling [setBWLevels()], [setReferenceLevel()], [setDataCoordinates()]:
                 that will significantly increase binarization speed on the next
                 [processLine()] calls.
                 Those parameters can be reset to defaults by calling [setGoodParameters()]
                 with no parameters.

[Binarizer] has a number of fine settings that limit allowed brighnesses, reference level and data coordinates.
Those settings are combined into [bin_preset_t] class.
[getDefaultFineSettings()] will return default fine settings.
[getCurrentFineSettings()] will return current fine settings.
New fine settings can be set with [setFineSettings()].

Go get the most data from noisy and/or low-band video signals [Binarizer] implements such features as:
    - Pixel-Shifer (Micro-TBC)
    - Reference level hysteresis

[Binarizer] performs binarization by pre-calculating horizontal pixel coordinates for centers of each bit in the PCM line
using start and stop data coordinates ([PCMLine.coords]) and [getBitsBetweenDataCoordinates()] of [PCMLine].
Sometimes those coordinates may be inaccurate (start/stop coordinates have some error or rounding errors during calculations)
or there can be preshoot/overshoot of the brightness transitions in the video line
or there may be some dropouts, noise, OSD in the video.
To mitigate all that Pixel-Shifer is used. If valid CRC can not be obtained with straight calculated coordinates,
[Binarizer] tries to shift (up to [SHIFT_STAGES_MAX] times) bits' coordinates from calculated centers
to one or the other side (see [PIX_SH_BG_TBL] in [PCMLine]) until CRC becomes valid.

Low-band video signals greatly reduce fidelity, bit transitions become very slow and smeared
and it may be impossible to correctly determine what's "0" and what's "1" with single reference level.
With slow brightness transitions levels of "0"s rise up and levels of "1" pull down and more important than that
level of each bit becomes dependent on what was before it in the line.
To combat that [Binarizer] calculates two reference levels
by adding and substracting the same small value of brightness from the single reference level.
After that binarization is performed conditionally: previous bit state determines
which of the two new reference levels (LOW and HIGH) is used to determine state of the current bit.
That's called "reference level hysteresis" and [Binarizer] can try up to [HYST_DEPTH_MAX] times
to widen that hysteresis window to obtain valid CRC.

STC-007/PCM-F1 formats have START ("1010") and STOP ("01111") markers at the edges of the line,
making it pretty easy to find data coordinates (Macro-TBC).
PCM-1 and PCM-16x0 formats do not have distinct markers to determine horizontal data coordinates,
so TBC for those formats is done via brute-force sweeping START and STOP coordinates until CRCs are good.
That is extremely resource heavy and slow.
Automatic coordinates search (Macro-TBC) can be enabled/disabled by [setCoordinatesSearch()].

Final horizontal data coordinates... can be NEGATIVE (for PCM-1 and PCM-16x0) and exceed limits of source video line.
That happens because PCM data can be cut off the screen with bad video capture.
START coordinate can be more to the left than the first pixel in video line (0) and
STOP coordinate can be more to the right than the last pixel in video line.
Also that means that some PCM data is lost from the source video.
AND that data may be brute-force picked for good CRC, because [Binarizer]
can determine how many bits were cut from the video line on each side.
Automatic brute-force Bit-Picker can be enabled/disabled by [setBitPicker()].

[Binarizer] fully depends on valid CRC to produce good result because it is closed-loop system based on CRC respond.
But CRC does not directly correlate to the only possible data set, CRC collisions are a thing.
Because of that [Binarizer] implements several measures to counteract false-positive CRCs from collisions.
For example, on many stages of processing statistics in form of array of [crc_handler_t] are collected
and than [findMostFrequentCRC()], [invalidateNonFrequentCRCs()] and [pickLevelByCRCStats()] are used
to determine if there is the only one valid CRC for the line and if not, which of "valid" CRCs is the most frequent.
If there no CRC with significantly higher encounter frequency, all "valid" CRCs are invalidated.
Better safe than sorry.
Is it even possible to get a collision on ~100 bits of data? Absolutely yes.
Some tests showed up to 20 (!) "valid" CRCs for the same line in worst case scenarios.

**************************************************************************************************************************************************************/

#ifndef BINARIZER_H
#define BINARIZER_H

#ifndef QT_VERSION
    #undef LB_EN_DBG_OUT
#endif

#include <mutex>
#include <thread>
#include <stdint.h>
#include <string>
#include <vector>
#include "config.h"
#include "arvidline.h"
#include "frametrimset.h"
#include "pcm1line.h"
#include "pcm16x0subline.h"
#include "stc007line.h"
#include "videoline.h"

#ifdef LB_EN_DBG_OUT
    #include <QElapsedTimer>
    #include <QDebug>
//#define LB_LOG_BW_VERBOSE       1       // Produce verbose output for B&W search process.
    //#define LB_LOG_MARKER_VERBOSE   1       // Produce verbose output for START/STOP marker search process.
#endif

#ifndef LB_EN_PIXEL_DBG
    #define getPixelBrightness(x_coord) video_line->pixel_data[x_coord]
#endif

//------------------------ Element of statistics array for CRC verification.
// TODO: convert [crc_handler_t] from [int16_t] to [CoordinatePair].
typedef struct
{
    uint8_t result;         // CRC state for the line.
    uint16_t crc;           // CRC for the line.
    uint8_t hyst_dph;       // Reference level hysteresis for current CRC.
    uint8_t shift_stg;      // Pixel shifting stage for current CRC.
    int16_t data_start;     // Coordinate of data start in the line for current CRC.
    int16_t data_stop;      // Coordinate of data stop in the line for current CRC.
} crc_handler_t;

// "Fine binarization settings" structure.
class bin_preset_t
{
public:
    uint8_t max_black_lvl;      // End point of BLACK level forwards search.
    uint8_t min_white_lvl;      // End point of WHITE level backwards search.
    uint8_t min_contrast;       // Minimum allowed difference between BLACK and WHITE levels.
    uint8_t min_ref_lvl;        // Minimum allowed reference level value.
    uint8_t max_ref_lvl;        // Maximum allowed reference level value.
    uint8_t min_valid_crcs;     // Minimum refence levels with valid CRC for single line during reference sweep (to drop CRC collisions on low-count CRCs).
    uint8_t mark_max_dist;      // Percent of line from each edge of the line to search for markers (STC-007).
    uint8_t left_bit_pick;      // Maximum number of recoverable by Bit Picker bits from the left side of the line (PCM-1/PCM-16x0).
    uint8_t right_bit_pick;     // Maximum number of recoverable by Bit Picker bits from the right side of the line (PCM-1/PCM-16x0).
    CoordinatePair horiz_coords;// Preset horizontal data coordinates.
    bool en_force_coords;       // Enable forced horizontal data coordinates.
    bool en_coord_search;       // Enable PCM data coordinates sweep for PCM-1/PCM-16x0.
    bool en_first_line_dup;     // Force first valid line in the field to be bad if duplicated lines detection is enabled.
    bool en_good_no_marker;     // Allow valid CRCs without PCM markers.

public:
    bin_preset_t();
    bin_preset_t(const bin_preset_t &in_object);
    bin_preset_t& operator= (const bin_preset_t &in_object);
    void reset();
};

//------------------------ Single-line binarizer class.
class Binarizer
{
public:

    // Console logging options for [setLogLevel()] and [log_level] (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),  // External operations with settings.
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_BRIGHT = (1<<2),    // Output brightness spread data.
        LOG_REF_SWEEP = (1<<3), // Output full process of reference level sweeping.
        LOG_COORD = (1<<4),     // Data coordinates search.
        LOG_RAWBIN = (1<<5),    // Output string of raw binarized data.
        LOG_READING = (1<<6),   // Final binarization run.
        LOG_LINE_DUMP = (1<<7), // Output resulting line.
    };

    // Binarization modes for [setMode()] and [bin_mode].
    enum
    {
        MODE_DRAFT,             // Maximum speed, level sweep and pixel shifting stages are disbled.
        MODE_FAST,              // Pretty fast, some pixel shifting stages are enabled, but reference level sweeping is disabled.
        MODE_NORMAL,            // Fast on good files, sluggish on noisy ones (level sweep is enabled, limited count of pixel shifting stages are enabled).
        MODE_INSANE,            // Powerfull but incredibly slow on noisy files (level sweeping and all pixel shifting stages are enabled).
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

    // Results.
    enum
    {
        LB_RET_OK,              // Success.
        LB_RET_NULL_VIDEO,      // Null poiner to input video line was provided, unable to process.
        LB_RET_NULL_PCM,        // Null poiner to output PCM line was provided, unable to process.
        LB_RET_SHORT_LINE,      // Provided video line is too short to have PCM data.
        LB_RET_NO_COORD,        // PCM data coordinates were not detected.
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
    bool do_coord_search;               // Setting for performing data coordinates detection.
    bool do_start_mark_sweep;           // Setting for performing START-marker search reference level hysteresis sweep.
    bool do_ref_lvl_sweep;              // Setting for performing full reference level sweep to determine best reference level.
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
    uint16_t getMostFrequentBrightnessCount(const uint16_t *brght_sprd);
    uint8_t getUsefullLowLevel(const uint16_t *brght_sprd);
    uint8_t getUsefullHighLevel(const uint16_t *brght_sprd);
    void findPCM1BW(uint16_t *brght_sprd);
    void findPCM16X0BW(uint16_t *brght_sprd);
    void findSTC007BW(uint16_t *brght_sprd);
    void findArVidBW(uint16_t *brght_sprd);
    bool findBlackWhite();
    // Reference level detection.
    uint8_t getLowLevel(uint8_t in_lvl, uint8_t diff);
    uint8_t getHighLevel(uint8_t in_lvl, uint8_t diff);
    uint8_t pickCenterRefLevel(uint8_t lvl_black, uint8_t lvl_white);
    void sweepRefLevel(PCMLine *pcm_line, crc_handler_t *crc_res);
    void calcRefLevelBySweep(PCMLine *pcm_line = NULL);
    // Data coordinates detection (Macro-TBC).
    uint8_t searchPCM1Data(PCM1Line *pcm_line, CoordinatePair data_loc);
    uint8_t searchPCM16X0Data(PCM16X0SubLine *pcm_line, CoordinatePair data_loc);
    void searchSTC007Markers(STC007Line *stc_line, uint8_t hyst_lvl, bool no_log = false);
    bool findPCM1Coordinates(PCM1Line *pcm1_line, CoordinatePair coord_history);
    bool findPCM16X0Coordinates(PCM16X0SubLine *pcm16x0_line, CoordinatePair coord_history);
    void findSTC007Coordinates(STC007Line *stc_line, bool no_log = false);
    // Lost bits recovery (Bit Picker).
    uint8_t pickCutBitsUpPCM1(PCM1Line *pcm_line, bool no_log = false);
    uint8_t pickCutBitsUpPCM16X0(PCM16X0SubLine *pcm_line, bool no_log = false);
    // Binarization (with reference level hysteresis and pixel-shifting Micro-TBC).
    uint8_t fillPCM1(PCM1Line *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    uint8_t fillPCM16X0(PCM16X0SubLine *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    uint8_t fillSTC007(STC007Line *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    uint8_t fillArVidAudio(ArVidLine *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    uint8_t fillDataWords(PCMLine *fill_pcm_line, uint8_t ref_delta = 0, uint8_t shift_stg = 0, bool no_log = false);
    void readPCMdata(PCMLine *fill_pcm_line, uint8_t thread_id = 0, bool no_log = false);
};

#endif // BINARIZER_H
