#include "vid_preset_t.h"

vid_preset_t::vid_preset_t()
{
    clear();
}

vid_preset_t::vid_preset_t(const vid_preset_t &in_object)
{
    skip_lines = in_object.skip_lines;
    colors = in_object.colors;
    crop_left = in_object.crop_left;
    crop_right = in_object.crop_right;
    crop_top = in_object.crop_top;
    crop_bottom = in_object.crop_bottom;
}

vid_preset_t& vid_preset_t::operator= (const vid_preset_t &in_object)
{
    if(this==&in_object) return *this;

    skip_lines = in_object.skip_lines;
    colors = in_object.colors;
    crop_left = in_object.crop_left;
    crop_right = in_object.crop_right;
    crop_top = in_object.crop_top;
    crop_bottom = in_object.crop_bottom;

    return *this;
}

void vid_preset_t::clear()
{
    skip_lines = false;
    colors = COLOR_BW;
    crop_left = crop_right = crop_top = crop_bottom = 0;
}
