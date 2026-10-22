#pragma once
#include "physics.hpp"
namespace sm63 {
// Script timing from Mario Fiant GFX Clip and Mario * Star, at 32 Hz.
class Sequence {
public:
    enum class Kind { None, Shine, Death, Fall };
    Kind kind=Kind::None;
    int age=0, pose=0, celebration=0;
    float anchorX=0,anchorY=0,velocity=-13,rotation=0;
    bool landed=false;
    bool active() const {return kind!=Kind::None;}
    bool dying() const {return kind==Kind::Death || kind==Kind::Fall;}
    void begin(Kind next,Player& player) {
        *this=Sequence{};kind=next;anchorX=player.x;anchorY=player.y;
        player.wingTicks=0;player.flying=player.wingLaunch=false;
        player.vx=0;player.vy*=.5f;player.diving=false;player.diveRecovery=player.diveFlipTicks=0;
        player.hurtTicks=player.jumpBuffer=0;player.running=false;
        player.pound=Pound::None;player.flipTicks=player.spinSpeed=0;player.invincibleTicks=0;
        player.fludd.spraying=false;player.fludd.charge=player.fludd.blast=0;
    }
    // Returns true only once the sequence has finished. The caller owns respawn/lives.
    bool tick(Player& player,const Terrain& terrain,int fanfareTicks) {
        ++age;
        if(dying()) {
            if(kind==Kind::Death && age>=16) {
                velocity+=.7f;player.y+=velocity;
                if(velocity>0)rotation+=15;
            }
            return age>=59;
        }
        if(kind!=Kind::Shine)return false;
        if(!landed) {
            const float x=player.x,y=player.y;
            player.tick(terrain,{});
            player.x=x;player.vx=0;
            if(player.respawned) {player.x=x;player.y=y;player.respawned=false;player.vy=0;landed=true;}
            landed=landed || player.grounded;
            if(landed){celebration=0;pose=1;}
        } else {
            ++celebration;
            if(celebration>=71)pose=2;
        }
        return landed && celebration>=std::max(96,fanfareTicks);
    }
    float fade() const {return dying()?std::clamp((age-47)/12.f,0.f,1.f):0.f;}
};
}
