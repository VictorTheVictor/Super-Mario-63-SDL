#pragma once
#include "physics.hpp"
#include <string>
namespace sm63 {
// Source sign: approach for seven frames, open, await release then press Z, close.
struct SignSession {
    enum class Phase { Closed, Approach, Opening, Reading, Closing };
    Phase phase=Phase::Closed;
    int ticks=0, animation=0;
    bool released=false;
    float x=0;
    std::string key;
    bool active() const {return phase!=Phase::Closed;}
    static bool canRead(const Player& p,float x,float y,bool horizontal) {
        return !horizontal && p.grounded && !p.diving && !p.spinSpeed && p.pound==Pound::None &&
            !p.hurtTicks && std::abs(p.x-x)<15.5f && std::abs(p.y-y)<10;
    }
    void open(Player& p,float sx,const char* text) {
        x=sx;key=text;phase=Phase::Approach;ticks=animation=0;released=false;
        p.vx=p.vy=0;p.fludd.spraying=false;
    }
    void tick(Player& p,bool z) {
        if(!active())return;
        p.x+=(x-p.x)/5;p.vx=p.vy=0;++ticks;++animation;
        if(!z)released=true;
        if(phase==Phase::Approach && ticks>=7){phase=Phase::Opening;ticks=0;}
        else if(phase==Phase::Opening && ticks>=5){phase=Phase::Reading;ticks=0;}
        else if(phase==Phase::Reading && released && z){phase=Phase::Closing;ticks=0;}
        else if(phase==Phase::Closing && ticks>=6){phase=Phase::Closed;ticks=0;}
    }
    float scale() const {
        if(phase==Phase::Closed || phase==Phase::Approach)return 0;
        if(phase==Phase::Opening)return .1f+.9f*ticks/5;
        if(phase==Phase::Closing)return std::max(0.f,1.f-ticks/6.f);
        return 1;
    }
};
}
