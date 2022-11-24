#include "bin_preset_t.h"

bin_preset_t::bin_preset_t()
{
    reset();
}

bin_preset_t::bin_preset_t(const bin_preset_t &in_object)
{
    max_black_lvl = in_object.max_black_lvl;
    min_white_lvl = in_object.min_white_lvl;
    min_contrast = in_object.min_contrast;
    min_ref_lvl = in_object.min_ref_lvl;
    max_ref_lvl = in_object.max_ref_lvl;
    min_valid_crcs = in_object.min_valid_crcs;
    mark_max_dist = in_object.mark_max_dist;
    left_bit_pick = in_object.left_bit_pick;
    right_bit_pick = in_object.right_bit_pick;
    horiz_coords = in_object.horiz_coords;
    en_coord_search = in_object.en_coord_search;
    en_force_coords = in_object.en_force_coords;
    en_good_no_marker = in_object.en_good_no_marker;
}

bin_preset_t& bin_preset_t::operator= (const bin_preset_t &in_object)
{
    if(this==&in_object) return *this;

    max_black_lvl = in_object.max_black_lvl;
    min_white_lvl = in_object.min_white_lvl;
    min_contrast = in_object.min_contrast;
    min_ref_lvl = in_object.min_ref_lvl;
    max_ref_lvl = in_object.max_ref_lvl;
    min_valid_crcs = in_object.min_valid_crcs;
    mark_max_dist = in_object.mark_max_dist;
    left_bit_pick = in_object.left_bit_pick;
    right_bit_pick = in_object.right_bit_pick;
    horiz_coords = in_object.horiz_coords;
    en_coord_search = in_object.en_coord_search;
    en_force_coords = in_object.en_force_coords;
    en_good_no_marker = in_object.en_good_no_marker;

    return *this;
}

void bin_preset_t::reset()
{
    max_black_lvl = 160;
    min_white_lvl = 28;
    min_contrast = 10;
    min_ref_lvl = 7;
    max_ref_lvl = 240;
    min_valid_crcs = 5;     // (picked after extensive testing)
    mark_max_dist = 6;
    left_bit_pick = 4;
    right_bit_pick = 2;
    horiz_coords.clear();
    horiz_coords.setToZero();
    en_force_coords = false;
    en_coord_search = en_good_no_marker = true;
}
