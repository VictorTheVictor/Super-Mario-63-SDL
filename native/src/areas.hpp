#pragma once
#include "world.hpp"
#include "course_objects.hpp"
#include "items.hpp"
#include "enemies.hpp"
#include "platforms.hpp"
#include <map>
namespace sm63 {
struct Destination {int area=-1;float x=0,y=0;};
inline Destination destination(int area,int exit,float x) {
    if(area==6 && exit==1)return {7,0,0};
    if(area==7 && exit==1)return {6,1760,-20};
    if(area==0 && exit==1)return {1,0,0};
    if(area==0 && exit==2)return {2,x-1800,0};
    if(area==1 && exit==1)return {0,2100,0};
    if(area==1 && exit==3)return {2,0,0};
    if(area==2 && exit==1)return {0,2000+x,-300};
    if(area==2 && exit==2)return {1,0,-320};
    if(area==1 && exit==2)return {3,0,0};
    if(area==3 && exit==1)return {1,2100,-150};
    if(area==3 && exit==2)return {4,0,0};
    if(area==4 && exit==1)return {3,1950,-650};
    return {};
}
inline int exitAt(int area,const Player& p) {
    for(const auto& e:world::areas.at(area).exits)
        if(p.x+8>e.left && p.x-8<e.right && p.y+2>e.top && p.y-2<e.bottom &&
           !(area==0 && e.number==2 && p.vy>0))return e.number;
    return 0;
}
class Areas {
    struct Saved {Items items;Enemies enemies;Platforms platforms;CourseObjects objects;};
    std::map<int,Saved> saved;
    bool loaded=false;
public:
    CourseObjects objects;
    Platforms platforms;
    bool tutorialGateOpen=false;int gateBlock=-1;
    void openTutorialGate(Terrain& terrain){tutorialGateOpen=true;if(gateBlock>=0)terrain.blocks.at(gateBlock).active=false;}
    int current=0;
    float spawnX=0,spawnY=0;
    void clear(){saved.clear();loaded=false;tutorialGateOpen=false;gateBlock=-1;}
    void enter(Destination to,Terrain& terrain,Player& player,Items& items,Enemies& enemies) {
        const int wingTicks=player.wingTicks;const int starCoinMask=items.starCoinMask;const int shineMask=items.shineMask;const bool bossDefeated=items.bossDefeated;
        const int count=items.count,reds=items.redCount,silvers=items.silverCount,heal=items.healingPoints,health=player.health;
        if(loaded)saved[current]={items,enemies,platforms,objects};
        const auto& area=world::areas.at(to.area);
        terrain.load(area.minX,area.minY,area.width,area.height,area.spans);
        terrain.ice=area.ice;
        for(auto w:area.water)terrain.water.push_back({w.left,w.top,w.right,w.bottom,w.cosmetic});
        items=Items{};enemies.actors.clear();
        auto found=saved.find(to.area);
        if(found!=saved.end()){items=found->second.items;enemies=found->second.enemies;}
        else for(const auto& object:area.objects) {
            if(object.kind<2)items.coins.emplace_back(object.x,object.y,object.kind+1);
            if(object.kind==2)items.boxes.push_back({object.x,object.y,object.reward});
            if((object.kind>=5 && object.kind<=9) || (object.kind==17 || object.kind==18)){items.supplies.push_back({object.x,object.y,object.kind});if(object.kind==17)items.supplies.back().powerSeconds=object.reward;}
            if(object.kind==15)items.starCoins.push_back({object.x,object.y,object.reward});
            if(object.kind==16)items.coins.emplace_back(object.x,object.y,5);
            if(object.kind==14)items.shines.push_back({object.x,object.y,object.reward});
            if(object.kind==21)enemies.actors.emplace_back(object.x,object.y,EnemyKind::Cheep,object.reward);
            if(object.kind==19)enemies.actors.emplace_back(object.x,object.y,EnemyKind::Penguin,object.reward);
            if(object.kind==3)enemies.actors.emplace_back(object.x,object.y);
            if(object.kind>=10 && object.kind<=13)enemies.actors.emplace_back(object.x,object.y,static_cast<EnemyKind>(object.kind-9),object.reward);
        }
        if(found!=saved.end())objects=found->second.objects;else objects.reset(to.area);
        objects.captured=-1;
        if(found!=saved.end()){platforms=found->second.platforms;platforms.publish(terrain);}
        else platforms.reset(to.area,terrain);
        for(const auto& box:items.boxes)terrain.blocks.push_back({box.x-16,box.y-31,box.x+16,box.y-4.95f,!box.broken});
        gateBlock=-1;
        if(to.area==6 && !tutorialGateOpen){gateBlock=static_cast<int>(terrain.blocks.size());terrain.blocks.push_back({316,-388,369,112});}
        items.starCoinMask=starCoinMask;items.pickedStarCoin=0;items.bossDefeated=bossDefeated;items.shineMask=shineMask;items.pickedShine=0;items.shineAppeared=false;
        items.count=count;items.redCount=reds;items.silverCount=silvers;items.healingPoints=heal;
        const auto equipment=player.fludd;player.reset();player.wingTicks=wingTicks;player.fludd=equipment;player.fludd.charge=player.fludd.blast=0;player.fludd.spraying=false;player.health=health;player.x=to.x;player.y=to.y;
        // Source probe shapes differ from the native body; resolve tiny spawn overlaps.
        for(int i=0;i<64 && terrain.body(player.x,player.y);++i)player.y-=1;
        spawnX=player.x;spawnY=player.y;current=to.area;loaded=true;
    }
};
}
