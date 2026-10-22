#pragma once
#include <array>
namespace sm63::itemdata {
struct Sprite { const char* path; int start; float a,b,c,d,x,y; };
inline constexpr std::array<Sprite,8> gold = {{
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 1.png",0,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 2.png",2,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 3.png",4,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 4.png",6,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 5.png",8,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 6.png",10,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 7.png",12,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Yellow Folder/Coin Spin - 8.png",14,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-9.00000000f},
}};
inline constexpr std::array<Sprite,4> red = {{
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Red Folder/Red Coin - 1.png",0,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-6.00000000f,-8.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Red Folder/Red Coin - 2.png",2,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-4.00000000f,-8.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Red Folder/Red Coin - 3.png",4,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-2.00000000f,-8.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Red Folder/Red Coin - 4.png",6,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-4.00000000f,-8.00000000f},
}};
inline constexpr std::array<Sprite,4> blue = {{
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Blue Folder/Blue Coin Animation.png",0,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-10.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Blue Folder/Bitmap 580.png",2,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-10.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Blue Folder/Bitmap 581.png",4,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-8.00000000f,-10.00000000f},
{"Course Items-PL/In-course Items/Collectables/Coin&#058 Blue Folder/Bitmap 582.png",6,1.00000000f,0.00000000f,0.00000000f,1.00000000f,-7.00000000f,-10.00000000f},
}};
inline constexpr Sprite box={"Course Items-PL/Blocks&#044 Tiles & Other Textures/Bricks & Woods/Switch Brick.png",0,1,0,0,1,-16,-32};
struct Spawn { float x,y; int value; };
inline constexpr std::array<Spawn,34> coins = {{
{1284.95000000f,113.45000000f,2},
{1.00000000f,51.65000000f,1},
{1.00000000f,101.65000000f,1},
{1.00000000f,151.65000000f,1},
{1913.90000000f,15.50000000f,2},
{646.25000000f,-71.85000000f,2},
{249.00000000f,138.60000000f,1},
{249.00000000f,108.60000000f,1},
{249.00000000f,78.60000000f,1},
{1164.95000000f,143.45000000f,1},
{1204.95000000f,133.45000000f,1},
{1244.95000000f,123.45000000f,1},
{1012.95000000f,84.60000000f,1},
{1052.95000000f,84.60000000f,1},
{1092.95000000f,84.60000000f,1},
{1132.95000000f,84.60000000f,1},
{1399.90000000f,52.65000000f,1},
{1479.90000000f,52.65000000f,1},
{1559.90000000f,52.65000000f,1},
{1599.90000000f,52.65000000f,1},
{1319.90000000f,52.65000000f,1},
{587.35000000f,165.45000000f,1},
{627.35000000f,165.45000000f,1},
{547.35000000f,165.45000000f,1},
{807.35000000f,167.45000000f,1},
{867.35000000f,167.45000000f,1},
{747.35000000f,167.45000000f,1},
{1739.35000000f,45.65000000f,1},
{1809.35000000f,45.65000000f,1},
{1689.35000000f,45.65000000f,1},
{867.35000000f,-67.05000000f,1},
{1859.35000000f,45.65000000f,1},
{1139.40000000f,-33.10000000f,1},
{1059.40000000f,-33.10000000f,1},
}};
inline constexpr std::array<Spawn,3> boxes = {{
{1127.45000000f,181.45000000f,5},
{1506.90000000f,-42.05000000f,3},
{1127.45000000f,149.45000000f,30},
}};
}
