#pragma once
#include <array>
namespace sm63::enemyart {
struct Track {const char* name; int length;};
inline constexpr std::array<Track,19> tracks={{
{"cheep_swim",20},
{"penguin_idle",1},
{"penguin_walk",16},
{"penguin_fly_down",16},
{"penguin_fly_up",16},
{"penguin_tumble",12},
{"bobomb_walk",30},
{"bobomb_tumble",12},
{"bobomb_explode",30},
{"koopa_walk",24},
{"koopa_fly",24},
{"koopa_shell",8},
{"shy_walk",12},
{"shy_fly",24},
{"shy_tumble",12},
{"shy_squish",12},
{"goonie_fly",18},
{"goonie_ride",18},
{"goonie_dive",18},
}};
}