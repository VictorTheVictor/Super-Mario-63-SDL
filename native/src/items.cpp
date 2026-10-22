#include "items.hpp"
namespace sm63 {
void Items::reset(Terrain& terrain) {
    shines.clear();starCoins.clear();starCoinMask=pickedStarCoin=0;bossDefeated=false;shineMask=pickedShine=0;shineAppeared=false;coins.clear();boxes.clear();supplies.clear();terrain.blocks.clear();count=redCount=silverCount=healingPoints=0;randomState=0x63c01u;
    for (const auto& s:itemdata::coins) coins.emplace_back(s.x,s.y,s.value);
    for (const auto& s:itemdata::boxes) {
        boxes.push_back({s.x,s.y,s.value});
        // Original OrangeBlockPL polygon, in pixels from its XFL twip edges.
        terrain.blocks.push_back({s.x-16,s.y-31,s.x+16,s.y-4.95f});
    }
}
float Items::randomUnit() {
    // Fixed seed makes replay/comparison runs reproducible across platforms.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return (randomState >> 8) * (1.f / 16777216.f);
}
void Items::launch(float x,float y,int reward) {
    Coin coin(x,y);coin.dropped=true;
    float verticalRange=0,verticalBase=-7,horizontalRange=4;
    switch(reward) {
        case 5: verticalRange=5;horizontalRange=10;break;
        case 10: verticalRange=6;verticalBase=-10;horizontalRange=12;break;
        case 20: verticalRange=7;verticalBase=-15;horizontalRange=14;break;
        case 30: verticalRange=10;verticalBase=-15;horizontalRange=16;break;
        case 100: verticalRange=15;verticalBase=-18;horizontalRange=18;break;
        default:break;
    }
    coin.vy=verticalBase+randomUnit()*verticalRange;
    coin.vx=(randomUnit()-.5f)*horizontalRange;
    coins.push_back(coin);
}
void Items::drop(float x,float y,int amount) {
    for(int i=0;i<amount;++i)launch(x,y,amount);
}
void Items::enemyImpact(Terrain& terrain,float x,float y,float radius) {
    for(std::size_t i=0;i<boxes.size();++i) {
        auto& box=boxes[i];
        if(!box.broken && std::abs(box.x-x)<radius+10 && std::abs(box.y-y)<radius) {
            box.broken=true;terrain.blocks.at(i).active=false;
        }
    }
}
void Items::breakBoxes(Terrain& terrain,Player& player,float oldVX,float oldVY,Pound oldPound) {
    for(std::size_t i=0;i<boxes.size();++i) {
        auto& box=boxes[i];auto& b=terrain.blocks[i];
        if(box.broken) continue;
        const bool horizontal=player.x+8>b.left && player.x-8<b.right;
        const bool onTop=horizontal && std::abs(player.y-b.top)<.1f;
        const float contactX=player.wallSide ? player.wallX : player.x;
        const float contactY=player.wallSide ? player.wallY : player.y;
        const bool side=contactY>b.top+.1f && contactY-34<b.bottom &&
            (std::abs(contactX+8-b.left)<.1f || std::abs(contactX-8-b.right)<.1f);
        const bool firstPoundFall=oldPound==Pound::Windup && player.pound==Pound::Landing;
        const float poundSpeed=firstPoundFall ? 8.f : oldVY;
        const float sideSpeed=player.wallSide ? player.wallSpeed : oldVX;
        const bool spinHit=player.spinSpeed>0 && player.x+34>b.left && player.x-34<b.right && player.y>b.top && player.y-34<b.bottom;
        if(!spinHit && !(onTop && (oldPound==Pound::Falling || firstPoundFall) && poundSpeed>3) &&
           !(side && std::abs(sideSpeed)>9)) continue;
        box.broken=true;b.active=false;
        if(onTop) {player.pound=Pound::Falling;player.grounded=false;player.vy=poundSpeed*.75f-3;}
        else if(!spinHit) {player.vx=0;player.running=false;player.diving=false;player.hurtTicks=7;}
    }
}
void Items::tick(const Terrain& terrain,Player& player,float oldX,float oldY) {
    const int oldReds=redCount,oldSilvers=silverCount;
    pickedShine=pickedStarCoin=0;shineAppeared=false;
    for(auto& box:boxes) if(box.broken) {
        ++box.brokenTicks;
        int rate=1;
        switch(box.reward) {
            case 5:rate=2;break;
            case 10:rate=3;break;
            case 20:rate=5;break;
            case 30:rate=6;break;
            case 100:rate=10;break;
            default:break;
        }
        for(int i=0;i<rate && box.emitted<box.reward;++i,++box.emitted)
            launch(box.x-5,box.y-12,box.reward);
    }
    nozzleBroken=refilled=wingPicked=false;
    for(auto& supply:supplies) {
        if(supply.gone)continue;
        if(supply.kind==18) {
            // Heart Clip1: running through spins it; accumulated rotation heals.
            if(supply.spinSpeed>1) {
                supply.healingSpin+=supply.spinSpeed;
                if(supply.healingSpin>24){supply.healingSpin=0;healingPoints+=5;}
                supply.spinSpeed-=.2f;
                if(supply.spinSpeed<=1){supply.spinSpeed=1;healingPoints+=5;}
            }
            supply.spinPhase=std::fmod(supply.spinPhase+std::ceil(supply.spinSpeed)*.06981317f,6.28318531f);
            const bool touching=std::abs(player.x-supply.x)<22 && std::abs(player.y-17-supply.y)<24;
            if(touching && !supply.wasTouch && player.grounded && std::abs(player.vx)>2) {
                supply.healingSpin=0;
                supply.spinSpeed=std::round((supply.spinSpeed-1)*.5f)+std::round(std::abs(player.vx*2));
            }
            supply.wasTouch=touching;
            continue;
        }
        if(supply.kind==17) {
            if(std::abs(player.x-supply.x)<22 && std::abs(player.y-17-supply.y)<24) {
                player.wingTicks=32*supply.powerSeconds;supply.gone=true;wingPicked=true;
            }
            continue;
        }
        if(supply.kind==9) {
            if(std::abs(player.x-supply.x)<22 && player.y>supply.y-16 && player.y-34<supply.y+16){supply.gone=true;++silverCount;refilled=true;}
            continue;
        }
        if(supply.kind==8) {
            if(std::abs(player.x-supply.x)<20 && player.y>supply.y-12 && player.y-34<supply.y+12) {
                player.fludd.water=std::min(10000.f,player.fludd.water+1500);supply.gone=true;refilled=true;
            }
            continue;
        }
        if(supply.cooldown>0)--supply.cooldown;
        const float top=supply.y-14;
        if(supply.cooldown==0 && std::abs(player.x-supply.x)<24 && oldY<=top+2 && player.y>=top && player.vy>=0) {
            supply.cooldown=312;supply.out=true;supply.age=0;supply.pickupY=supply.y+10;supply.vy=-3;
            for(int i=0;i<64 && terrain.body(supply.x,supply.pickupY+7,6,14);++i)--supply.pickupY;
            player.y=top;player.vy=-6;player.grounded=false;nozzleBroken=true;
            player.fludd.unlocked[supply.kind-4]=true;
        }
        if(supply.out) {
            ++supply.age;
            const float next=supply.pickupY+supply.vy;float landing=0;
            if(supply.vy>=0 && terrain.oneWayLanding(supply.x,supply.pickupY+7,next+7,landing)){supply.pickupY=landing-7;supply.vy=0;}
            else if(!terrain.body(supply.x,next+7,6,14)){supply.pickupY=next;supply.vy=(supply.vy+.5f)*.95f;}
            else supply.vy=0;
            if(supply.age>6 && std::abs(player.x-supply.x)<24 && player.y>supply.pickupY-22 && player.y-34<supply.pickupY+21) {
                player.fludd.equip(static_cast<Nozzle>(supply.kind-4));supply.out=false;refilled=true;
            }
            if(supply.age>1000)supply.out=false;
        }
    }
    for(auto& coin:coins) {
        coin.previousX=coin.x;coin.previousY=coin.y;
        if(coin.gone) continue;
        if(coin.collectedTicks) {
            coin.y-=1;
            if(++coin.collectedTicks>7) coin.gone=true;
            continue;
        }
        ++coin.age;
        if(coin.dropped) {
            coin.vy+=1;if(coin.vy>0) coin.vy*=.97f;
            const int steps=16;
            for(int i=0;i<steps;++i) {
                float nx=coin.x+std::clamp(coin.vx,-7.f,7.f)/steps;
                if(terrain.body(nx,coin.y+8,7,16)) coin.vx=-coin.vx*.6f;else coin.x=nx;
                float ny=coin.y+std::clamp(coin.vy,-8.f,8.f)/steps,top=0;
                if(coin.vy>=0 && terrain.oneWayLanding(coin.x,coin.y+8,ny+8,top) && !terrain.body(coin.x,top,7,16)) {
                    coin.y=top-8;coin.vy=Player::friction(-coin.vy,.5f,1.2f);coin.vx=Player::friction(coin.vx,.3f,1.1f);
                } else if(terrain.body(coin.x,ny+8,7,16)) {
                    coin.vy=Player::friction(-coin.vy,.5f,1.2f);coin.vx=Player::friction(coin.vx,.3f,1.1f);
                } else coin.y=ny;
            }
            if(coin.age>384 || coin.y>terrain.minY+terrain.rows+100) {coin.gone=true;continue;}
            if(coin.age<=12) continue;
        }
        // Swept relative body/coin overlap avoids skipping pickups during dives.
        float enter=0,leave=1;
        auto axis=[&](float a,float b,float lo,float hi) {
            float d=b-a;
            if(std::abs(d)<.00001f)return a>lo&&a<hi;
            float t0=(lo-a)/d,t1=(hi-a)/d;if(t0>t1)std::swap(t0,t1);
            enter=std::max(enter,t0);leave=std::min(leave,t1);return enter<leave;
        };
        if(axis(oldX-coin.previousX,player.x-coin.x,-15,15)&&axis(oldY-coin.previousY,player.y-coin.y,-8,42)) {
            count+=coin.value;if(coin.value==2)++redCount;
            healingPoints+=coin.value==5 ? 15 : coin.value==2 ? 5 : 1;
            if(coin.value==5)player.fludd.power=std::min(100.f,player.fludd.power+50);
            while(healingPoints>=5 && player.health<8) {healingPoints-=5;++player.health;}
            if(player.health==8)healingPoints=0;
            coin.collectedTicks=1;
        }
    }
    while(healingPoints>=5 && player.health>0 && player.health<8){healingPoints-=5;++player.health;}
    if(player.health==8)healingPoints=0;
    for(auto& coin:starCoins) {
        if(coin.gone || (starCoinMask&(1<<coin.number)))continue;
        if(player.health>0 && std::abs(player.x-coin.x)<22 && std::abs(player.y-20-coin.y)<24) {
            coin.gone=true;starCoinMask|=1<<coin.number;pickedStarCoin=coin.number;break;
        }
    }
    shineAppeared=(oldReds<8 && redCount>=8) || (oldSilvers<5 && silverCount>=5);
    if(player.health>0)for(auto& shine:shines) {
        if(!shineAvailable(shine))continue;
        // Flash tests Mario's body-center point against the Shine's box.
        if(std::abs(player.x-shine.x)<20 && std::abs(player.y-20-shine.y)<20) {
            shine.gone=true;shineMask|=1<<shine.number;pickedShine=shine.number;
            player.x=shine.x;player.y=shine.y+20;break;
        }
    }
}
}
