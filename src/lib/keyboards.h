// what a keyboard looks like: per regional layout, every key with its name, its led and where it sits on the picture
// the tables are generated out of Swarms kb_matrix.xml files (tools/assets/kbmatrix.py), one .inc per keyboard in keyboards/
#pragma once

#include "rockcat/rockcat.h"

struct keyboard_region {
    int id;           // ROCCATs number for the layout: 0 US, 1 DE, 3 FR, 5 nordic, 14 CZ
    const char *name;
    const rockcat_key *keys;
    int count;
};

struct keyboard_layout {
    const keyboard_region *regions;
    int region_count;
    // the coordinate space the key positions live in (the size of Swarms smallest picture set, 1024x768/). any keyboard picture gets stretched onto it
    int picture_width, picture_height;
};

extern const keyboard_layout RYOS_MK_FX_KEYBOARD;
