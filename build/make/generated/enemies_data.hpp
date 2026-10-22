#pragma once
#include <array>
namespace sm63::enemydata {
struct Sprite { const char* path; int start; float a,b,c,d,x,y; };
inline constexpr std::array<Sprite,8> walk = {{
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 1.png", 0, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -8.00000000f, -17.50000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 2.png", 1, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -8.50000000f, -19.00000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 3.png", 2, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -9.00000000f, -18.50000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 4.png", 3, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -10.00000000f, -17.00000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 5.png", 4, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -10.00000000f, -17.50000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 6.png", 5, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -10.00000000f, -18.00000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 7.png", 6, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -8.00000000f, -17.50000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Walking - 8.png", 7, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -7.00000000f, -17.00000000f},
}};
inline constexpr std::array<Sprite,9> squish = {{
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Scared.png", 0, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -7.95000000f, -16.90000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Scared.png", 1, 1.00000000f, 0.00000000f, 0.00000000f, 0.66667175f, -7.95000000f, -11.40000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Squished - 6.png", 2, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -9.30000000f, -16.90000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Squished - 4.png", 3, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -12.65000000f, -6.90000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba Squished - 5.png", 5, 1.00000000f, 0.00000000f, 0.00000000f, 1.00000000f, -13.65000000f, -5.05000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba - 4.png", 7, 1.00000000f, 0.00000000f, 0.00000000f, 0.66667175f, -8.65000000f, -10.90000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba - 4.png", 8, 0.77777100f, 0.00000000f, 0.00000000f, 1.00001526f, -6.65000000f, -17.90000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba - 4.png", 9, 0.66667175f, 0.00000000f, 0.00000000f, 1.22222900f, -5.65000000f, -21.90000000f},
{"Enemies/Normal/G-Z/Goomba/YI Style Goomba - 4.png", 10, 0.33334351f, 0.00000000f, 0.00000000f, 1.55494690f, -2.65000000f, -27.90000000f},
}};
struct Spawn { float x,y; };
inline constexpr std::array<Spawn,6> spawns = {{
{1470.40000000f,70.60000000f},
{1542.90000000f,70.65000000f},
{1107.15000000f,103.65000000f},
{753.95000000f,-36.10000000f},
{612.20000000f,159.65000000f},
{1860.35000000f,0.90000000f},
}};
}
