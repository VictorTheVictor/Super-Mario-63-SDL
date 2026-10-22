#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace sm63 {
enum class Nozzle { None, Hover, Rocket, Turbo };
struct Fludd {
    Nozzle nozzle=Nozzle::None;
    std::array<bool,4> unlocked{{true,false,false,false}};
    float water=10000,power=100;
    int charge=0,blast=0;
    bool spraying=false,fired=false;
    void equip(Nozzle n){unlocked[static_cast<int>(n)]=true;nozzle=n;water=10000;charge=blast=0;}
    void cycle(){int n=static_cast<int>(nozzle);do{n=(n+1)%4;}while(!unlocked[n]);nozzle=static_cast<Nozzle>(n);charge=blast=0;spraying=false;}
    void apply(bool held,bool allowed,bool grounded,bool left,bool diving,float& vx,float& vy) {
        spraying=fired=false;
        if(grounded && blast==0)power=100;
        if(!held && nozzle!=Nozzle::Hover)power=std::min(100.f,power+1);
        if(!allowed){charge=blast=0;return;}
        if(nozzle==Nozzle::Rocket) {
            if(blast>0){--blast;power=0;vy-=.5f;spraying=true;}
            else {
                if(charge>=10 || (held && power>=100 && water>0))++charge;
                else charge=std::max(0,charge-1);
                if(charge>=10)vy*=.97f;
                if(charge>=16){charge=0;blast=8;water=std::max(0.f,water-500);power=0;vy=std::min(-25.f+std::max(vy/3,0.f),vy);spraying=fired=true;}
            }
        }else if(held && water>0 && power>0) {
            if(nozzle==Nozzle::Hover) {
                if(diving){vy*=.98f;vx*=.97f;vx+=(left?-1:1)*.92f;}
                else {vy*=.8f;vy-=((9.2f-vy)/10)*(power/175+.75f);vx=(vx<0?-1:1)*std::max(0.f,std::abs(vx)-.05f)/1.03f;}
                water=std::max(0.f,water-7);power=std::max(0.f,power-1.5f);spraying=true;
            }else if(nozzle==Nozzle::Turbo && power>=100) {
                water=std::max(0.f,water-(diving?10:5));
                vx+=((left?-1:1)*(grounded?75.f:37.5f)-vx)/30;spraying=true;
            }
        }
    }
};
}
