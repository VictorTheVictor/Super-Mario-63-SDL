#pragma once
#include "physics.hpp"
#include "enemies_data.hpp"

namespace sm63 {
enum class EnemyState { Walking, Flying, Shell, Exploding, Tumble, Squished, Gone };
enum class EnemyKind { Goomba, Bobomb, Koopa, ShyGuy, Goonie, Penguin, Cheep };
struct Enemy {
    float x, y, previousX, previousY, vx = 0, vy = 0;
    int direction = -1, age = 0, patrolTicks = 0;
    bool grounded = false, seeing = false, powerHit = false;
    EnemyKind kind=EnemyKind::Goomba;
    int fuse=0, contactCooldown=0;
    bool riding=false, rewarded=false;float homeY=0,walkSpeed=1.5f;bool hungry=true,lastWater=false;
    EnemyState state = EnemyState::Walking;
    explicit Enemy(float px, float py, EnemyKind type=EnemyKind::Goomba, int flags=0) : x(px), y(py), previousX(px), previousY(py), kind(type) {
        homeY=py;
        direction=(flags&2)?1:-1;
        if(kind==EnemyKind::Penguin && (flags&2)){walkSpeed=0;direction=-1;}
        if(flags&1)state=EnemyState::Flying;
        if(kind==EnemyKind::Cheep){state=EnemyState::Flying;hungry=!(flags&1);}
        if(flags&4){state=EnemyState::Tumble;vx=1;}
    }
    float height() const {return kind==EnemyKind::Cheep ? 8.f : state==EnemyState::Shell ? 16.f : 25.f;}
    void explode(){state=EnemyState::Exploding;age=0;vx=vy=0;}
    void knock(float speed,float lift,bool strong);
    bool waterHit(const Player& player) const;
    void tick(const Terrain& terrain, const Player& player);
    void interact(Player& player, float oldX, float oldY, bool jumpHeld);
};
class Enemies {
public:
    std::vector<Enemy> actors;
    Enemies() { reset(); }
    void reset() {
        actors.clear();
        for (const auto& spawn : enemydata::spawns) actors.emplace_back(spawn.x, spawn.y);
    }
    void tick(const Terrain& terrain, Player& player, float oldX, float oldY, bool jumpHeld);
};
}
