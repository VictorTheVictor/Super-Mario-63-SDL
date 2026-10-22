#pragma once
#include "enemies.hpp"
namespace sm63 {
class KingBoss {
public:
    enum class State {Absent,Waiting,Intro,Fight,Hurt,Dying,Outro,Defeated};
    enum class Action {Idle,Walk,Throw,Jump,Grab};
    State state=State::Absent;Action action=Action::Idle;
    float x=0,y=0,startX=0,startY=0,previousX=0,previousY=0,vx=0,vy=0;
    int hp=3,age=0,actionAge=0,direction=-1,turns=0,event=0;
    bool holding=false,releasedZ=false,thrown=false;
    struct Wave {float x,y;int direction,age=0;};
    std::vector<Wave> waves;
    struct Ring {float x,y;int age=0;};
    std::vector<Ring> rings;
    void reset(float px,float py){*this=KingBoss{};x=startX=previousX=px;y=startY=previousY=py;state=State::Waiting;}
    bool defeated() const{return state==State::Defeated;}
    bool dialogue(Player& p,bool z) {
        event=0;
        if(state==State::Waiting && p.grounded && std::abs(p.x-x)<140 && std::abs(p.y-y)<70) {
            state=State::Intro;event=5;age=0;releasedZ=!z;p.vx=p.vy=0;
        }
        if(state!=State::Intro && state!=State::Outro)return false;
        ++age;if(!z)releasedZ=true;
        if(age>6 && releasedZ && z) {
            if(state==State::Intro){state=State::Fight;age=actionAge=0;event=1;}
            else {state=State::Defeated;event=3;}
        }
        return true;
    }
    void impact() {
        if(state!=State::Fight)return;
        state=State::Hurt;age=0;holding=false;vx=0;
    }
    void tick(const Terrain& terrain,Player& p,Enemies& enemies) {
        previousX=x;previousY=y;
        if(state==State::Absent || state==State::Waiting || state==State::Intro || state==State::Outro || defeated())return;
        ++age;
        for(auto& ring:rings)++ring.age;
        rings.erase(std::remove_if(rings.begin(),rings.end(),[](const Ring& r){return r.age>=81;}),rings.end());
        for(auto& wave:waves) {
            ++wave.age;wave.x+=wave.direction*5.3125f;
            if(wave.age<70 && p.grounded && std::abs(p.x-wave.x)<15 && std::abs(p.y-wave.y)<12 && !p.invincibleTicks) {
                p.health=std::max(0,p.health-1);p.vy=-4;p.vx=wave.direction;p.grounded=false;p.invincibleTicks=90;p.hurtTicks=7;
            }
        }
        waves.erase(std::remove_if(waves.begin(),waves.end(),[](const Wave& w){return w.age>=70;}),waves.end());
        if(state==State::Hurt) {
            if(age==8){--hp;event=2;if(hp==0){state=State::Dying;age=0;}}
            else if(age>=27){state=State::Fight;age=0;action=Action::Idle;actionAge=0;}
            return;
        }
        if(state==State::Dying){if(age>=30){state=State::Outro;event=5;age=0;releasedZ=false;}return;}
        for(auto& enemy:enemies.actors)
            if(enemy.kind==EnemyKind::Bobomb && enemy.state==EnemyState::Tumble && enemy.powerHit &&
               std::abs(enemy.x-x)<45 && std::abs(enemy.y-(y-25))<40) {enemy.explode();event=6;}
        for(const auto& enemy:enemies.actors)
            if(enemy.state==EnemyState::Exploding && enemy.age<6 && std::abs(enemy.x-x)<75 && std::abs(enemy.y-(y-25))<65){impact();return;}
        // Flash uses point probes for his feet, not a wide rectangular body.
        // The source placement starts slightly inside the rounded summit.
        auto ground=[&](float px,float py){return terrain.at(static_cast<int>(std::floor(px)),static_cast<int>(std::floor(py)))!=0;};
        if(action!=Action::Jump) {
            for(int i=0;i<64 && (ground(x,y-1)||ground(x,y-3));++i)--y;
            for(int i=0;i<64 && ground(x,y+3) && !ground(x,y-1) && !ground(x,y-3);++i)++y;
            startY=y;
        }
        ++actionAge;
        if(action==Action::Idle && actionAge>=32) {
            direction=p.x<x?-1:1;actionAge=0;
            // Reproducible walk/jump/throw cycle; source chooses these with random().
            ++turns;action=turns%3==0?Action::Throw:turns%2==0?Action::Jump:Action::Walk;
        }
        if(action==Action::Walk) {
            direction=p.x<x?-1:1;vx=(vx+direction*.7f)*.7f;
            const float next=x+vx;
            if(ground(next+direction*(100.f/15),y+5))x=next;
            if(std::abs(p.x-x)<35 && std::abs(p.y-y)<40){action=Action::Grab;actionAge=0;thrown=false;}
            else if(actionAge>=40){action=Action::Idle;actionAge=0;}
        } else vx*=.7f;
        if(action==Action::Throw) {
            if(actionAge==25) {
                enemies.actors.emplace_back(x,y-27,EnemyKind::Bobomb,4);
                auto& bomb=enemies.actors.back();bomb.vx=direction*12.5f;bomb.vy=1;bomb.fuse=1;bomb.direction=direction;
            }
            if(actionAge>=45){action=Action::Idle;actionAge=0;}
        }
        if(action==Action::Grab) {
            if(actionAge>=4 && actionAge<=9 && std::abs(p.x-(x+direction*25))<35 && std::abs(p.y-y)<45 && !p.invincibleTicks)holding=true;
            if(actionAge==12 && !holding){action=Action::Idle;actionAge=0;}
            if(holding){p.x=x+direction*25;p.y=y-12;p.vx=p.vy=0;p.invincibleTicks=2;p.spinSpeed=0;p.diving=false;p.fludd.spraying=false;}
            if(actionAge==62 && holding) {
                holding=false;p.vx=direction*12.5f;p.vy=-5;p.y-=25;p.grounded=false;p.diving=true;p.diveTicks=3;p.facingLeft=direction<0;thrown=true;
            }
            if(actionAge>=85){action=Action::Idle;actionAge=0;}
        }
        if(action==Action::Jump) {
            if(actionAge==3)vy=-6;
            if(actionAge>=3) {
                vy+=.5f;float next=y+vy;
                if(vy>=0 && next>=startY) {
                    y=startY;vy=0;rings.push_back({x,y});waves.push_back({x-10,y,-1});waves.push_back({x+10,y,1});event=4;
                    action=Action::Idle;actionAge=0;
                } else y=next;
            }
        }
        if(!p.invincibleTicks && p.vy>=0 && std::abs(p.x-x)<28 && p.y>y-55 && p.y<y-35) {
            p.health=std::max(0,p.health-1);p.vx=p.x<x?-7:7;p.vy=5;p.invincibleTicks=30;p.grounded=false;
        }
    }
};
}
