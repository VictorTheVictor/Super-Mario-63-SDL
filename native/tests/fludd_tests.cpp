#include "areas.hpp"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
int main(){try{
    sm63::Terrain terrain;terrain.load(-1000,-1000,2000,2000,std::vector<sm63::level::Span>{});
    sm63::Player player,plain;player.fludd.equip(sm63::Nozzle::Hover);player.vy=plain.vy=8;
    for(int i=0;i<20;++i){player.tick(terrain,{false,false,false,false,false,false,true});plain.tick(terrain,{});}
    require(player.y<plain.y && player.vy<1,"Hover must brake a fall");
    require(player.fludd.water==9860 && player.fludd.power==70,"Hover water and power drain");
    for(int i=0;i<60;++i)player.tick(terrain,{false,false,false,false,false,false,true});
    require(player.fludd.power==0 && !player.fludd.spraying,"Hover exhausts air power");
    player.fludd.apply(false,true,true,false,false,player.vx,player.vy);
    require(player.fludd.power==100,"Landing restores power");
    player.fludd.water=0;player.fludd.apply(true,true,false,false,false,player.vx,player.vy);
    require(!player.fludd.spraying,"Empty water blocks spray");
    player.reset();player.fludd.equip(sm63::Nozzle::Rocket);
    for(int i=0;i<15;++i)player.tick(terrain,{false,false,false,false,false,false,true});
    require(!player.fludd.fired,"Rocket must charge before firing");
    player.tick(terrain,{false,false,false,false,false,false,true});
    require(player.fludd.fired && player.vy<-15 && player.fludd.water==9500 && player.fludd.power==0,"Rocket launch and costs");
    terrain.blocks.push_back({-50,-80,50,-70});player.reset();player.fludd.equip(sm63::Nozzle::Rocket);
    for(int i=0;i<40;++i){player.tick(terrain,{false,false,false,false,false,false,true});require(!terrain.body(player.x,player.y),"Rocket thrust must respect solid ceilings");}
    terrain.blocks.clear();
    player.reset();player.fludd.equip(sm63::Nozzle::Turbo);float vx=0,vy=0;
    for(int i=0;i<60;++i)player.fludd.apply(true,true,true,false,false,vx,vy);
    require(vx>60 && player.fludd.water==9700,"Turbo ground thrust and water cost");
    player.fludd.cycle();require(player.fludd.nozzle==sm63::Nozzle::None,"Cycle skips locked nozzles");
    player.fludd.cycle();require(player.fludd.nozzle==sm63::Nozzle::Turbo,"Cycle finds unlocked nozzle");
    sm63::Items items;items.supplies.push_back({0,0,5});player.reset();player.y=-12;player.vy=6;
    items.tick(terrain,player,0,-20);
    require(items.nozzleBroken && items.supplies[0].out && player.vy==-6,"Stomp opens nozzle box and bounces");
    for(int i=0;i<8;++i){player.y=items.supplies[0].pickupY+5;items.tick(terrain,player,0,player.y);}
    require(player.fludd.nozzle==sm63::Nozzle::Hover && !items.supplies[0].out,"Released nozzle equips on contact");
    items.supplies.push_back({0,0,8});player.fludd.water=8000;player.y=0;items.tick(terrain,player,0,0);
    require(player.fludd.water==9500 && items.supplies.back().gone,"Small bottle refills 1500 once");
    items.tick(terrain,player,0,0);require(player.fludd.water==9500,"Bottle cannot be collected twice");
    sm63::Areas areas;sm63::Enemies enemies;
    areas.enter({0,0,0},terrain,player,items,enemies);areas.enter({1,0,0},terrain,player,items,enemies);
    require(player.fludd.nozzle==sm63::Nozzle::Hover && player.fludd.water==9500,"FLUDD survives area transitions");
    std::cout<<"FLUDD flight, fuel, recharge, nozzle cycle, pickups and transition persistence passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
