#include "areas.hpp"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
    sm63::Areas areas;sm63::Terrain terrain;sm63::Player player;sm63::Items items;sm63::Enemies enemies;
    areas.enter({0,0,0},terrain,player,items,enemies);
    require(items.coins.size()==34 && items.boxes.size()==3 && enemies.actors.size()==13,"Opening area content regression");
    areas.platforms.objects[0].x+=12;const float platformX=areas.platforms.objects[0].x;
    items.starCoinMask=2;items.coins[0].gone=true;items.count=2;items.redCount=1;items.boxes[0].broken=true;
    enemies.actors[0].state=sm63::EnemyState::Gone;player.health=6;
    areas.enter(sm63::destination(0,1,2250),terrain,player,items,enemies);
    require(areas.current==1 && player.x==0 && player.health==6 && items.count==2,"Forward transition and global progress");
    require(terrain.rows>1400 && !terrain.body(player.x,player.y),"Destination terrain loaded and spawn clear");
    areas.enter(sm63::destination(1,1,0),terrain,player,items,enemies);
    require(areas.platforms.objects[0].x==platformX && terrain.surfaces.size()==2,"Platform state survives area revisit");
    require(items.starCoinMask==2,"Star Coin progress persists across areas");
    require(player.x==2100 && areas.current==0,"Return spawn must use Mario coordinates, not camera coordinates");
    require(items.coins[0].gone && items.boxes[0].broken && !terrain.blocks[0].active && enemies.actors[0].state==sm63::EnemyState::Gone,"Revisit must preserve collected/broken/defeated state");
    for(int area=0;area<5;++area) {
        for(const auto& exit:sm63::world::areas[area].exits) {
            player.reset();player.x=(exit.left+exit.right)/2;player.y=(exit.top+exit.bottom)/2;
            require(sm63::exitAt(area,player)==exit.number,"Source exit rectangle detection");
            auto to=sm63::destination(area,exit.number,player.x);
            require(to.area>=0 && to.area<5,"Every course exit must have a supported destination");
            areas.enter(to,terrain,player,items,enemies);
            require(!terrain.body(player.x,player.y),"Exit target spawn must be clear");
        }
    }
    require(sm63::world::areas.size()==8,"All course and opening areas imported");
    require(sm63::world::areas[3].exits.size()==2 && sm63::world::areas[4].exits.size()==1,"Post-initialization exits imported");
    player.reset();player.x=983;player.y=-326;player.vy=1;
    require(sm63::exitAt(0,player)==0,"Upper exit requires ascending or stationary Mario");
    areas.clear();items=sm63::Items{};player.reset();areas.enter({0,0,0},terrain,player,items,enemies);
    require(!items.coins[0].gone && terrain.blocks[0].active && items.count==0,"Fresh run resets visited areas");
    areas.clear();player.reset();items=sm63::Items{};areas.enter({5,0,0},terrain,player,items,enemies);
    require(areas.current==5 && items.coins.size()>60 && !enemies.actors.empty(),"Snowman opening imports gameplay placements");
    require(!terrain.body(player.x,player.y),"Snowman spawn must be clear");
    bool wing=false;for(const auto& supply:items.supplies)if(supply.kind==17){wing=true;require(supply.powerSeconds==30,"Snowman Wing Cap duration follows source override");}
    int penguins=0;for(const auto& enemy:enemies.actors)if(enemy.kind==sm63::EnemyKind::Penguin)++penguins;
    require(penguins==8 && !terrain.ice.empty(),"Snowman penguins and ice regions imported");
    require(wing && !areas.objects.objects.empty() && !areas.platforms.objects.empty(),"Snowman transport and powerups imported");
    for(int i=0;i<64;++i)player.tick(terrain,{});
    require(player.grounded && !player.respawned,"Snowman spawn settles on playable terrain");
    areas.clear();player.reset();items=sm63::Items{};areas.enter({6,0,0},terrain,player,items,enemies);
    require(terrain.body(340,80) && !terrain.water.empty(),"Opening controls gate and ponds loaded");
    for(int i=0;i<64;++i)player.tick(terrain,{});
    require(player.grounded && player.health>0,"Opening spawn lands safely");
    areas.openTutorialGate(terrain);require(!terrain.body(340,80),"Reading controls removes tutorial gate");
    require(sm63::destination(6,1,0).area==7 && sm63::destination(7,1,0).area==6,"Opening areas link in both directions");
    areas.enter({7,0,0},terrain,player,items,enemies);
    require(!terrain.water.empty() && sm63::world::areas[7].exits.size()==2,"Second opening area has pools and source exits");
    areas.enter({6,0,0},terrain,player,items,enemies);require(!terrain.body(340,80),"Controls gate stays open on revisit");
    std::cout<<"Area routes, spawns, bounds, state persistence and reset passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
