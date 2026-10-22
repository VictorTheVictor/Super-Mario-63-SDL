#pragma once
#include "physics.hpp"
#include "items_data.hpp"
#include <cstdint>
namespace sm63 {
struct Coin {
    float x,y,previousX,previousY,vx=0,vy=0;
    int value=1, age=0, collectedTicks=0;
    bool dropped=false, gone=false;
    Coin(float px,float py,int v=1):x(px),y(py),previousX(px),previousY(py),value(v){}
};
struct Box { float x,y; int reward, brokenTicks=0, emitted=0; bool broken=false; };
struct Supply { float x,y;int kind,cooldown=0,age=0;bool gone=false,out=false;float pickupY=0,vy=0;float spinSpeed=1,spinPhase=1.57079633f,healingSpin=0;bool wasTouch=false;int powerSeconds=45; };
struct Shine {float x,y;int number;bool gone=false;};
class Items {
public:
    std::vector<Shine> shines,starCoins;
    int starCoinMask=0,pickedStarCoin=0;
    bool bossDefeated=false;
    int shineMask=0, pickedShine=0;
    bool shineAppeared=false;
    bool shineAvailable(const Shine& shine) const {
        return !shine.gone && !(shineMask & (1<<shine.number)) &&
            ((shine.number==1 && bossDefeated) || shine.number==2 || shine.number==5 || (shine.number==3 && silverCount>=5) || (shine.number==4 && redCount>=8));
    }
    std::vector<Coin> coins;
    std::vector<Box> boxes;
    std::vector<Supply> supplies;
    bool nozzleBroken=false,refilled=false,wingPicked=false;
    int count=0, redCount=0, silverCount=0, healingPoints=0;
    void reset(Terrain& terrain);
    void drop(float x,float y,int amount=1);
    void enemyImpact(Terrain& terrain,float x,float y,float radius);
    void breakBoxes(Terrain& terrain,Player& player,float oldVX,float oldVY,Pound oldPound);
    void tick(const Terrain& terrain,Player& player,float oldX,float oldY);
private:
    std::uint32_t randomState = 0x63c01u;
    float randomUnit();
    void launch(float x,float y,int reward);
};
}
