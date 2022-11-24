#ifndef STC007LINE_H
#define STC007LINE_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcmline.h"

//#define STC_LINE_EN_DBG_OUT     1     // Enable debug console output.
#define STC_LINE_HELP_SIZE      15      // Number of lines in help dump.

//TODO: update log legend
static const std::string STC007_HELP_LIST[STC_LINE_HELP_SIZE]
{
    "   >---Source frame number",
    "   |      >---Line number in the source frame                                                                                                                                                                                   CRC-16 value calculated for words in the line---<",
    "   |      |      >---BLACK level (by brightness statistics)                          Line state: 'CRC OK' - all good, 'FIXED' - words repaired by CWD, 'BD CRC!' - PCM detected, but with errors, 'No PCM!' - PCM not detected, 'CTRL BLOCK' - special control line---<         |",
    "   |      |      |   >---WHITE level (by brightness statistics)                                                                                                                                                          Time spent on line binarization (us)---<     |         |",
    "   |      |      |   |    >---Binarization detection: '=' - externally set, '>' - by full level sweep, '?' - by stats estimate                                                                Final binarization bit coordinates pixel shifting stage---<       |     |         |",
    "   |      |      |   |    |  >---Low binarization level (with hysteresis)                                                                                                                            Final binarization level hysteresis depth---<      |       |     |         |",
    "   |      |      |   |    |  |   >---Binarization level (0...255)                                                                                                                                                  Pixels Per PCM Bit---<        |      |       |     |         |",
    "   |      |      |   |    |  |   |   >---High binarization level (with hysteresis)                                                                  Audio words: 'NS' - not silent, 'AS' - almost silent, 'FS' - fully silent---<       |        |      |       |     |         |",
    "   |      |      |   |    |  |   |   |    >---Marker detection: '+' - success, [digit] - failed on that stage                                                                                         Marker last pixel---<     |       |        |      |       |     |         |",
    "   |      |      |   |    |  |   |   |    | >---Marker first pixel                                                                                    Marker coordinates: '|' - externally set, ':' - self calculated---< |     |       |        |      |       |     |         |",
    "   |      |      |   |    |  |   |   |    | | >---Marker coordinates: '|' - externally set, ':' - self calculated                                                          Marker first pixel (data end coordinate)---< | |     |       |        |      |       |     |         |",
    "   |      |      |   |    |  |   |   |    | | | >---Marker second pixel (data start coordinate)                                                 Marker detection: '+' - success, [digit] - failed on that stage---<   | | |     |       |        |      |       |     |         |",
    "   |      |      |   |    |  |   |   |    | | | |    >---PCM START marker                                                                                                                  PCM STOP marker---<    |   | | |     |       |        |      |       |     |         |",
    "   |      |      |   |    |  |   |   |    | | | |    |  [====================================================================PCM DATA====================================================================]   |    |   | | |     |       |        |      |       |     |         |",
    "   |      |      |   |    |  |   |   |    | | | |  [   ][      Ln      ][      Rn      ][     Ln+1     ][     Rn+1     ][     Ln+2     ][     Rn+2     ][      Pn      ][      Qn      ][      CRCCn     ][    ]  |   | | |     |       |        |      |       |     |         |"
};

class STC007Line : public PCMLine
{
public:
    // Bit counts.
    enum
    {
        BITS_PER_WORD = 14,     // Number of bits per PCM STC-007/STC-008 data word.
        BITS_PER_F1_WORD = 16,  // Number of bits per PCM PCM-F1 data word.
        BITS_PER_CRC = 16,      // Number of bits per CRC word.
        BITS_START = 4,         // Number of bits for PCM START marker.
        BITS_PCM_DATA = 128,    // Number of bits for data.
        BITS_STOP = 5,          // Number of bits for PCM STOP marker.
        BITS_IN_LINE = (BITS_START+BITS_PCM_DATA+BITS_STOP),    // Total number of useful bits in one video line.
        BITS_LEFT_SHIFT = 24,   // Highest bit number for left part pixel-shifting.
        BITS_RIGHT_SHIFT = 76,  // Lowest bit number for right part pixel-shifting.
        BIT_M2_RANGE_POS = (1<<13), // R bit, that determines value range of other bits for M2 sample format.
        BIT_M2_SIGN_POS = (1<<12)   // Sign bit in the source 14-bit word for M2 sample format.
    };

    // Word order in the [words[]] for the line.
    enum
    {
        WORD_L_SH0,         // Left channel 14-bit word for current data block (line offset = 0).
        WORD_R_SH48,        // Right channel 14-bit word for data block at +48 words (line offset = 16).
        WORD_L_SH95,        // Left channel 14-bit word for data block at +95 words (line offset = 32).
        WORD_R_SH143,       // Right channel 14-bit word for data block at +143 words (line offset = 48).
        WORD_L_SH190,       // Left channel 14-bit word for data block at +190 words (line offset = 64).
        WORD_R_SH238,       // Right channel 14-bit word for data block at +238 words (line offset = 80).
        WORD_P_SH288,       // P-word (parity) 14-bit word for data block at +288 words (line offset = 96).
        WORD_Q_SH336,       // Q-word (matrix-ECC) 14-bit word for data block at +336 words (line offset = 112) or S-word for 16-bit mode.
        WORD_CRCC_SH0,      // CRCC 16-bit word for current line (line offset = 0).
        WORD_CNT            // Limiter for word-operations.
    };

    // Word order for Control Block.
    enum
    {
        WORD_CB_CUE1 = WORD_L_SH0,      // Cue signal, part 1
        WORD_CB_CUE2 = WORD_R_SH48,     // Cue signal, part 2
        WORD_CB_CUE3 = WORD_L_SH95,     // Cue signal, part 3
        WORD_CB_CUE4 = WORD_R_SH143,    // Cue signal, part 4
        WORD_CB_ID = WORD_L_SH190,      // Identification
        WORD_CB_ADDR1 = WORD_R_SH238,   // Address (MSB)
        WORD_CB_ADDR2 = WORD_P_SH288,   // Address (LSB)
        WORD_CB_CTRL = WORD_Q_SH336,    // Control bits
    };

    // Default CRC values.
    enum
    {
        CRC_SILENT = 0xA96A // CRC for silent (muted) line.
    };

    // States for PCM START-marker forwards search state machine.
    enum
    {
        MARK_ST_START,      // Search just started, searching for "1" in the line.
        MARK_ST_TOP_1,      // START-bit[1] ("1") found, searching for "1 -> 0" transition.
        MARK_ST_BOT_1,      // START-bit[2] ("0") found, searching for "0 -> 1" transition.
        MARK_ST_TOP_2,      // START-bit[3] ("1") found, searching for "1 -> 0" transition.
        MARK_ST_BOT_2       // START-bit[4] ("0") found, START-marker found successfully.
    };

    // States for PCM STOP-marker backwards search state machine.
    enum
    {
        MARK_ED_START,      // Search just started, searching for "1" in the line.
        MARK_ED_TOP,        // STOP-bits[5...2] ("1") found, searching for "1 -> 0" transition.
        MARK_ED_BOT,        // STOP-bit[1] ("0") found, checking length of the "white line".
        MARK_ED_LEN_OK      // Length of the STOP-marker is ok, STOP-marker found successfully.
    };

    // Bit masks for Control Block.
    enum
    {
        CTRL_FMT_ID    = 0x3000,    // Format ID.
        CTRL_FMT_M2    = 0x1000,    // [CTRL_FMT_ID] for M2 sample format.
        CTRL_COPY_MASK = 0x0008,    // Prohibition of digital dubbing ("0" = allowed, "1" = prohibited).
        CTRL_EN_P_MASK = 0x0004,    // Presence of P-word ("0" = present, "1" = absent).
        CTRL_EN_Q_MASK = 0x0002,    // Presence of Q-word/14-16 bit mode ("0" = present [14-bit], "1" = absent [16-bit]).
        CTRL_EMPH_MASK = 0x0001     // Pre-emphasis ("0" = enabled, "1" = disabled).
    };

public:
    uint8_t mark_st_stage;          // START marker detection state.
    uint8_t mark_ed_stage;          // STOP marker detection state.
    uint16_t marker_start_bg_coord; // Pixel coordinate of 1st bit of PCM START marker.
    uint16_t marker_start_ed_coord; // Pixel coordinate of 4th bit of PCM START marker.
    uint16_t marker_stop_ed_coord;  // Pixel coordinate of PCM STOP marker.

private:
    bool m2_format;                 // Are samples formatted for M2?
    bool word_crc[WORD_CNT];        // Flags for each word (was word intact or not by CRC, used for CWD).
    bool word_valid[WORD_CNT];      // Flags for each word (is word intact after CWD correction).
    uint16_t words[WORD_CNT];       // 14 bit PCM words (2 MSBs unused) + 16 bit CRCC.
    uint16_t pixel_coordinates[PCM_LINE_MAX_PS_STAGES][BITS_PCM_DATA];      // Pre-calculated coordinates for all bits and pixel-shift stages.

public:
    STC007Line();
    STC007Line(const STC007Line &);
    STC007Line& operator= (const STC007Line &);

    void clear();
    void setServCtrlBlk();
    void setSourceCRC(uint16_t in_crc = 0);
    void setSilent();
    void setWord(uint8_t index, uint16_t in_word, bool in_valid = false);
    void setFixed(uint8_t index);
    void setM2Format(bool in_set = false);
    void forceMarkersOk();
    void applyCRCStatePerWord();
    uint8_t getBitsPerSourceLine();
    uint8_t getBitsBetweenDataCoordinates();
    uint8_t getLeftShiftZoneBit();
    uint8_t getRightShiftZoneBit();
    uint16_t getVideoPixelT(uint8_t pcm_bit, uint8_t shift_stage);
    void calcCRC();
    uint16_t getSourceCRC();
    uint8_t getPCMType();
    int16_t getSample(uint8_t index);
    int16_t getCtrlID();
    int8_t getCtrlIndex();
    int8_t getCtrlHour();
    int8_t getCtrlMinute();
    int8_t getCtrlSecond();
    int8_t getCtrlFieldCode();
    bool hasStartMarker();
    bool hasStopMarker();
    bool hasMarkers();
    bool hasSameWords(STC007Line *in_line = NULL);
    bool hasControlBlock();
    bool isCRCValidIgnoreForced();
    bool isCtrlFormatM2();
    bool isCtrlCopyProhibited();
    bool isCtrlEnabledP();
    bool isCtrlEnabledQ();
    bool isCtrlEnabledEmphasis();
    bool isNearSilence(uint8_t index);
    bool isAlmostSilent();
    bool isSilent();
    bool isFixedByCWD();
    bool isServCtrlBlk();
    bool isWordCRCOk(uint8_t index);
    bool isWordValid(uint8_t index);
    uint16_t getWord(uint8_t index);
    std::string dumpWordsString();
    std::string dumpContentString();
    std::string helpDumpNext();

private:
    void calcCoordinates(uint8_t in_shift);
};

#endif // STC007LINE_H
