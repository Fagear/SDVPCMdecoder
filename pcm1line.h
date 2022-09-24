#ifndef PCM1LINE_H
#define PCM1LINE_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcmline.h"

//#define PCM1_LINE_EN_DBG_OUT    1       // Enable debug console output.
#define PCM1_LINE_HELP_SIZE     12      // Number of lines in help dump.

static const std::string PCM1_HELP_LIST[PCM1_LINE_HELP_SIZE]
{
    "   >---Source frame number",
    "   |      >---Line number in the source frame                                                                                                                            CRC-16 value calculated for words in the line---<",
    "   |      |      >---BLACK level (by brightness statistics)                                          Line state: 'CRC OK' - all good, 'BD CRC!' - PCM detected, but with errors, 'No PCM!' - PCM not detected---<        |",
    "   |      |      |   >---WHITE level (by brightness statistics)                                                                                                    Time spent on line binarization (us)---<     |        |",
    "   |      |      |   |    >---Binarization detection: '=' - externally set, '?' - by stats estimate                                     Final binarization bit coordinates pixel shifting stage---<       |     |        |",
    "   |      |      |   |    |  >---Low binarization level (with hysteresis)                                                                       Final binarization level hysteresis depth---<     |       |     |        |",
    "   |      |      |   |    |  |   >---Binarization level (0...255)                                                                                            Pixels Per PCM Bit---<         |     |       |     |        |",
    "   |      |      |   |    |  |   |   >---High binarization level (with hysteresis)            Audio words: 'NS' - not silent, 'AS' - almost silent, 'FS' - fully silent---<       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      >---Data start coordinate                                                     Number of picked bits from the right side---<   |       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      |  >---Data end coordinate                                                   Number of picked bits from the left side---< |   |       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      |  |    [=================================================PCM DATA=================================================]    | |   |       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      |  |    [      Ln     ][      Rn     ][     Ln+1    ][     Rn+1    ][     Ln+2    ][     Rn+2    ][      CRCCn     ]    | |   |       |         |     |       |     |        |"
};

class PCM1Line : public PCMLine
{
public:
    enum
    {
        SUBLINES_PER_LINE = 3   // Number of [PCM1SubLine] produced from one [VideoLine]/[PCM1Line].
    };

    // Bit counts.
    enum
    {
        BITS_PER_WORD = 13,         // Number of bits per PCM data word.
        BITS_PER_CRC = 16,          // Number of bits per CRC word.
        BITS_PCM_DATA = ((BITS_PER_WORD*6)+BITS_PER_CRC),     // Number of bits for data.
        BITS_IN_LINE = BITS_PCM_DATA,   // Total number of useful bits in one video line.
        BITS_LEFT_SHIFT = 16,       // Highest bit number for left part pixel-shifting.
        BITS_RIGHT_SHIFT = 52,      // Lowest bit number for right part pixel-shifting.
        BIT_RANGE_POS = (1<<12),    // R bit, that determines value range of other bits.
        BIT_SIGN_POS = (1<<11)      // Sign bit in the source 13-bit word.
    };

    // Word order in the [words[]] for the line.
    enum
    {
        WORD_L2,                // Left channel 13-bit sample.
        WORD_R2,                // Right channel 13-bit sample.
        WORD_L4,                // Left channel 13-bit sample.
        WORD_R4,                // Right channel 13-bit sample.
        WORD_L6,                // Left channel 13-bit sample.
        WORD_R6,                // Right channel 13-bit sample.
        WORD_CRCC,              // CRCC 16-bit sample for current line.
        WORD_MAX                // Limiter for word-operations.
    };

    // Default CRC values.
    enum
    {
        CRC_SILENT = 0xECBF     // CRC for silent (muted) line.
    };

public:
    uint16_t words[WORD_MAX];   // 13 bit PCM words (3 MSBs unused) + 16 bit CRCC.
    uint8_t picked_bits_left;   // Number of left bits of [WORD_L2] that were picked after bad CRC.
    uint8_t picked_bits_right;  // Number of right bits of [WORD_CRCC] that were picked after bad CRC.

private:
    uint16_t pixel_coordinates[PCM_LINE_MAX_PS_STAGES][BITS_PCM_DATA];      // Pre-calculated coordinates for all bits and pixel-shift stages.

public:
    PCM1Line();
    PCM1Line(const PCM1Line &);
    PCM1Line& operator= (const PCM1Line &);

    void clear();
    void setServHeader();
    void setSourceCRC(uint16_t in_crc = 0);
    void setSilent();
    uint8_t getBitsPerSourceLine();
    uint8_t getBitsBetweenDataCoordinates();
    uint8_t getLeftShiftZoneBit();
    uint8_t getRightShiftZoneBit();
    uint16_t getVideoPixelT(uint8_t pcm_bit, uint8_t shift_stage);
    void calcCRC();
    uint16_t getSourceCRC();
    uint8_t getPCMType();
    int16_t getSample(uint8_t index);
    bool hasSameWords(PCM1Line *in_line = NULL);
    bool hasPickedWords();
    bool hasPickedLeft();
    bool hasPickedRight();
    bool hasHeader();
    bool isCRCValidIgnoreForced();
    bool isNearSilence(uint8_t index);
    bool isAlmostSilent();
    bool isSilent();
    bool isServHeader();
    std::string dumpWordsString();
    std::string dumpContentString();
    std::string helpDumpNext();

private:
    void calcCoordinates(uint8_t in_shift);
};

#endif // PCM1LINE_H
