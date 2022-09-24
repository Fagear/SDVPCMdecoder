#ifndef BIN_PRESET_T_H
#define BIN_PRESET_T_H

#include <stdint.h>

// "Digitizer preset" settings structure.
class bin_preset_t
{
public:
    uint8_t max_black_lvl;      // End point of BLACK level forwards search.
    uint8_t min_white_lvl;      // End point of WHITE level backwards search.
    uint8_t min_contrast;       // Minimum difference between BLACK and WHITE.
    uint8_t min_ref_lvl;        // Minimum brightness threshold.
    uint8_t max_ref_lvl;        // Maximum brightness threshold.
    uint8_t min_valid_crcs;     // Minimum refence levels with valid CRC for single line during reference sweep (to drop CRC collisions on low-count CRCs).
    uint8_t mark_max_dist;      // Percent of line from each edge of the line to search for markers (STC-007).
    bool en_coord_search;       // Enable PCM data search for PCM-1/PCM-16x0.
    bool en_bit_picker;         // Enable Bit Picker to recover bits cut from the sides (PCM-1/PCM-16x0).
    bool en_good_no_marker;     // Enable valid CRCs without PCM markers.

public:
    bin_preset_t();
    bin_preset_t(const bin_preset_t &in_object);
    bin_preset_t& operator= (const bin_preset_t &in_object);
};

#endif // BIN_PRESET_T_H
