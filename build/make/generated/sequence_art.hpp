#pragma once
#include <array>
namespace sm63::sequenceart {
struct Track {const char* name; int length;};
inline constexpr std::array<Track,9> tracks={{
{"star_none",3},
{"star_hover",3},
{"star_rocket",3},
{"star_turbo",3},
{"death_none",60},
{"death_hover",60},
{"death_rocket",60},
{"death_turbo",60},
{"shine",24},
}};
}