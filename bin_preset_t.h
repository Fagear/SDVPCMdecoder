#ifndef BIN_PRESET_T_H
#define BIN_PRESET_T_H

#include <stdint.h>
#include "frametrimset.h"

// "Digitizer preset" settings structure.
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
    bool en_coord_search;       // Enable PCM data coordinates sweep for PCM-1/PCM-16x0.
    bool en_force_coords;       // Enable forced horizontal data coordinates.
    bool en_good_no_marker;     // Enable valid CRCs without PCM markers.

public:
    bin_preset_t();
    bin_preset_t(const bin_preset_t &in_object);
    bin_preset_t& operator= (const bin_preset_t &in_object);
    void reset();
};

#endif // BIN_PRESET_T_H
