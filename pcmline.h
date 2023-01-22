/**************************************************************************************************************************************************************
pcmline.h

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

Binarized PCM line container.
This is a base class for all PCM line objects for each PCM format.
It holds common fields such as source frame and line numbers, detected black ("0") and white ("1") levels, reference levels used for binarization,
horizontal data pixel coordinates and pre-calculated bit coordinates used for binarization.
This class provides CRC-16 calculation routines for any word width.
It also provides pixel coordinates calculation interface template for all sub-classes to implement for their specific format requirements.
As does [VideoLine], this class can hold service tag, signalling for such events as start of a new source or end of a frame.
When [service_type] is set to something different from [SRVLINE_NO], other fields are not used.

The main reason for this class to exist is to consolidate common data for different PCM formats in one structure
and to allow [Binarizer] be mostly format-agnostic and universal.
The [getPCMType()] function allowes automatic sub-class detection for pointer casting.

Binarization process from array of 8-bit pixels to array of bits is based on pre-calculating data bit centers' coordinates (in pixels):
    - PPB (Pixels Per Bit) is calculated from number of usefull pixels in the source video line and number of bits in the PCM line for the given format;
    - Coordinate of each data bit is calculated in pixels from calculated PPB and the number of each pixel;
    - Offset is applied to calculated coordinates for each stage of Pixel-Shifer;
    - All calculated coordinates are stored into two-dimentional array for future use during binarization.

To speed up highly iterative binarization process, coordinates for all bits for all possible pixel-shifting stages are pre-calculated
and stored into [pixel_coordinates[][]] arrays in each sub-class.
PPB calculation is performed by calling [setPPB()] and providing data coordinates in pixels.
The result is stored in [pixel_size_mult] and [halfpixel_size_mult], also starting pixel offset is stored in [pixel_start_offset].
Pre-calculation of bit coordinates is done by calling virtual [calcCoordinates()] that uses [getVideoPixeBylCalc()] to calculate pixel coordinates.
After pre-calculation is done, pixel coordinates of each bit should be retrieved by calling virtual [getVideoPixelByTable()].

All calculations are done in integer, no floating point is used.
To minimize rounding errors data is multipled by [INT_CALC_MULT] before calculations and divided by the same value at the end.
[INT_CALC_MULT] is picked as multiple of 2 to allow replacing multiplication and division by bit shifting operations.

**************************************************************************************************************************************************************/

#ifndef PCMLINE_H
#define PCMLINE_H

#include <stdint.h>
#include <string>
#include "config.h"
#include "frametrimset.h"

#define PCM_LINE_MAX_PS_STAGES      5       // Maximum supported number of pixel-shifting stages.

// Pixel shift from calculated bit center coordinate at the beginning of the line per shift stage.
static const int8_t PIX_SH_BG_TBL[PCM_LINE_MAX_PS_STAGES] =
{
    0, 1, -1, 2, -2/*, 0, -1, -1, 0, 1, 1*/
};
// Pixel shift from calculated bit center coordinate at the ending of the line per shift stage.
static const int8_t PIX_SH_ED_TBL[PCM_LINE_MAX_PS_STAGES] =
{
    0, 1, -1, 2, -2/*, 1, 0, 1, -1, 0, -1*/
};

//------------------------ PCM line container (base class).
class PCMLine
{
public:
    // Derived classes identification.
    enum
    {
        TYPE_PCM1,              // Class [PCM1Line] for PCM-1 standard.
        TYPE_PCM16X0,           // Class [PCM16X0SubLine] for PCM-1600/PCM-1610/PCM-1630 standard.
        TYPE_STC007,            // Class [STC007Line] for STC-007/STC-008/PCM-F1/M2 standard.
        TYPE_ARVA,              // Class [ArVidLine] for ArVid Audio.
        TYPE_MAX                // Limiter for type-operations.
    };

    // CRC constants.
    // Name: CRC-16 CCITT-FALSE
    // Init: 0xFFFF
    // Poly: 0x1021
    // Check: 0x29B1 ("123456789")
    enum
    {
        CRC_INIT = 0xFFFF,      // CRC initial value.
        CRC_MAX_BIT = 0x8000,   // CRC maximum bit mask (15-th bit for 16-bit CRC).
        CRC_POLY = 0x1021       // CRC polynomial.
    };

    // Integer multiplier for integer pixel coordinate calculations.
    enum
    {
        INT_CALC_MULT = 128,    // Multiplier, used for integer coordinates calculations (should be bultiple of 2).
        INT_CALC_PIXEL_DIV = 2  // Divider for [halfpixel_size_mult] calculation.
    };

    // Service tags for [service_type].
    enum
    {
        SRVLINE_NO,             // Regular PCM line with audio data.
        SRVLINE_NEW_FILE,       // New file opened (with path in [file_path]).
        SRVLINE_END_FILE,       // File ended.
        SRVLINE_FILLER,         // Filler line (for frame padding).
        SRVLINE_END_FIELD,      // Field of a frame ended.
        SRVLINE_END_FRAME,      // Frame ended.
        SRVLINE_HEADER_LINE,    // Header line (starting line and finishing/emphasis detection line) for [PCM1Line].
        SRVLINE_CTRL_BLOCK      // PCM Control Block (Emphasis, P and Q code presence) for [STC007Line].
    };

    // Bits placeholders in debug log string.
    enum
    {
        DUMP_BIT_ONE = '#',     // Bit char representing "1" with valid CRC.
        DUMP_BIT_ZERO = '-',    // Bit char representing "0" with valid CRC.
        DUMP_BIT_ONE_BAD = '1', // Bit char representing "1" with invalid CRC.
        DUMP_BIT_ZERO_BAD = '0',// Bit char representing "0" with invalid CRC.
        DUMP_WBRL_OK = '[',     // Start of the word with valid CRC.
        DUMP_WBRR_OK = ']',     // End of the word with valid CRC.
        DUMP_WBRL_BAD = '@',    // Start of the word with invalid CRC.
        DUMP_WBRR_BAD = '@'     // End of the word with invalid CRC.
    };

public:
    uint32_t frame_number;      // Number of source frame for this line.
    uint16_t line_number;       // Number of the line in the frame (#1=topmost).
    uint8_t black_level;        // Detected brightness level of "BLACK" (bit 0).
    uint8_t white_level;        // Detected brightness level of "WHITE" (bit 1).
    uint8_t ref_low;            // Low binarization level for binarization with hysteresis.
    uint8_t ref_level;          // Binarization reference level [0...255].
    uint8_t ref_high;           // High binarization level for binarization with hysteresis.
    CoordinatePair coords;      // Pixel coordinates for PCM data start and stop pixels (can be outside of available pixels in source [VideoLine]).
    uint32_t process_time;      // Amount of time spent processing the line [us].
    uint8_t hysteresis_depth;   // Last hysteresis depth run.
    uint8_t shift_stage;        // Last stage of pixel-shift process.
    bool ref_level_sweeped;     // Reference level was found by full reference level sweeping.
    bool coords_sweeped;        // Data coordinates were found by sweeping ranges from starting points.
    bool data_by_ext_tune;      // Valid data (by CRC) was detected with provided external parameters.
    std::string file_path;      // Path of decoded file (set with [SRVLINE_NEW_FILE]).

protected:
    uint8_t help_stage;         // Stage of help output.
    uint16_t calc_crc;          // Re-calculated CRCC.
    bool blk_wht_set;           // Black and white levels are set for the line.
    bool coords_set;            // Data coordinates are set for the line.
    bool forced_bad;            // This line must have bad CRC even if data is good.
    uint8_t service_type;       // Type of the service line.

private:
    uint16_t pixel_start;           // Source video line first pixel.
    uint16_t pixel_stop;            // Source video line last pixel +1.
    int16_t pixel_start_offset;     // Offset (in pixels) of first pixel with usefull data.
    uint32_t pixel_size_mult;       // Calculated PCM bit size in pixels, multiplied by [INT_CALC_MULT].
    uint32_t halfpixel_size_mult;   // Half of the [pixel_size_mult], used for bit center calculation.

public:
    PCMLine();
    PCMLine(const PCMLine &);
    PCMLine& operator= (const PCMLine &);
    bool operator!= (const PCMLine &);

    void clear();
    void setServNewFile(std::string path);
    void setServEndFile();
    void setServFiller();
    void setServEndField();
    void setServEndFrame();
    void setFromDoubledState(bool in_flag = false);
    void setBWLevelsState(bool in_flag = false);
    void setDataCoordinatesState(bool in_flag = false);
    void setSweepedReference(bool in_flag = false);
    void setSweepedCoordinates(bool in_flag = false);
    virtual void setSourceCRC(uint16_t in_crc = 0) = 0;
    virtual void setSilent() = 0;
    void setInvalidCRC();
    void setForcedBad();
    void setSourcePixels(uint16_t in_start, uint16_t in_stop);
    void calcPPB(CoordinatePair in_coords);
    // PCM-1: 94 bits
    // STC-007: 137 bits (128 bits of data)
    // ArVid Audio: 156 bits (144 bits of data)
    // PCM-16x0: 193 bits (192 bits of data)
    virtual uint8_t getBitsPerSourceLine() = 0;
    virtual uint8_t getBitsBetweenDataCoordinates() = 0;        // Should NEVER return 0!
    uint8_t getPPB();
    uint8_t getPPBfrac();
    virtual uint8_t getLeftShiftZoneBit() = 0;
    virtual uint8_t getRightShiftZoneBit() = 0;
    uint16_t getVideoPixeBylCalc(uint8_t pcm_bit, uint8_t in_shift = 0, uint8_t bit_ofs = 0);
    virtual uint16_t getVideoPixelByTable(uint8_t pcm_bit, uint8_t shift_stage = 0) = 0;
    virtual void calcCRC() = 0;
    uint16_t getCalculatedCRC();
    virtual uint16_t getSourceCRC() = 0;
    virtual uint8_t getPCMType() = 0;       // Sub-class detection, should return value from enum above.
    virtual int16_t getSample(uint8_t index) = 0;
    bool canUseMarkers();
    bool hasBWSet();
    bool hasDataCoordSet();
    bool isSourceDoubleWidth();
    bool isForcedBad();
    bool isCRCValid();
    virtual bool isCRCValidIgnoreForced() = 0;      // Should be virtual because [PCM1Line] has a special case.
    bool isDataByRefSweep();
    bool isDataByCoordSweep();
    bool isDataBySkip();
    virtual bool isNearSilence(uint8_t index) = 0;
    virtual bool isAlmostSilent() = 0;
    virtual bool isSilent() = 0;
    bool isServiceLine();
    bool isServNewFile();
    bool isServEndFile();
    bool isServFiller();
    bool isServEndField();
    bool isServEndFrame();
    virtual std::string dumpWordsString() = 0;
    virtual std::string dumpContentString() = 0;
    void helpDumpRestart();
    virtual std::string helpDumpNext() = 0;

protected:
    static void CRC16_init(uint16_t *CRC_data);
    static uint16_t getCalcCRC16(uint16_t CRC_data, uint16_t in_data, uint8_t bit_cnt = 16);
    void setServiceLine();
    void setPPB(CoordinatePair in_coords);
    virtual void calcCoordinates(uint8_t in_shift) = 0;
};

#endif // PCMLINE_H
