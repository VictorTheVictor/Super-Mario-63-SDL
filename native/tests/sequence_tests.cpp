#include "sequences.hpp"
#include "areas.hpp"
#include <stdexcept>
#include <iostream>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    sm63::Terrain terrain;sm63::Player player;sm63::Items items;sm63::Enemies enemies;sm63::Areas areas;
    for(int area=0;area<5;++area) {
        areas.enter({area,0,0},terrain,player,items,enemies);
        require(items.shines.size()==1,"Each course area imports its source Shine");
        auto& shine=items.shines.front();
        require(items.shineAvailable(shine)==(area==2 || area==4),"Objective Shines must remain gated");
    }
    areas.enter({0,0,0},terrain,player,items,enemies);
    items.redCount=7;auto shine=items.shines.front();
    player.x=shine.x;player.y=shine.y+20;
    items.tick(terrain,player,player.x,player.y);
    require(items.pickedShine==0,"Seven red coins cannot unlock Shine");
    items.redCount=8;items.tick(terrain,player,player.x,player.y);
    require(items.pickedShine==4 && items.shineMask==(1<<4),"Eight red coins unlock original reward");
    items.tick(terrain,player,player.x,player.y);
    require(items.pickedShine==0,"Shine cannot collect twice");
    areas.enter({1,0,0},terrain,player,items,enemies);
    items.silverCount=5;require(items.shineAvailable(items.shines.front()),"Five Silver Stars unlock reward");
    areas.enter({0,0,0},terrain,player,items,enemies);
    require(!items.shineAvailable(items.shines.front()),"Collected Shine survives revisiting an area");
    areas.enter({3,0,0},terrain,player,items,enemies);
    require(!items.shineAvailable(items.shines.front()),"Undefeated boss must not grant a free reward");
    // Source health-death hop: 16-frame reaction, -13 velocity, +0.7 gravity, spin on descent.
    player.reset();player.x=100;player.y=100;player.fludd.equip(sm63::Nozzle::Rocket);
    sm63::Sequence sequence;sequence.begin(sm63::Sequence::Kind::Death,player);
    for(int i=0;i<15;++i)require(!sequence.tick(player,terrain,120),"Death cannot finish during reaction");
    require(player.y==100,"Reaction stays in place");
    sequence.tick(player,terrain,120);require(std::abs(player.y-87.7f)<.01f,"Source death hop velocity");
    while(sequence.age<58)require(!sequence.tick(player,terrain,120),"Death cannot respawn before fade ends");
    require(sequence.rotation>0 && sequence.fade()>0,"Death spins and fades");
    require(sequence.tick(player,terrain,120),"Death completes on original frame 60");
    require(player.fludd.nozzle==sm63::Nozzle::Rocket,"Death preserves equipment");
    // Pickup falls to solid terrain, holds the first pose for 2.2 seconds, then celebrates.
    areas.enter({0,0,0},terrain,player,items,enemies);player.reset();
    for(int i=0;i<100 && !player.grounded;++i)player.tick(terrain,{});
    require(player.grounded,"Test spawn must land");
    sequence.begin(sm63::Sequence::Kind::Shine,player);
    sequence.tick(player,terrain,120);require(sequence.landed && sequence.pose==1,"Shine waits for landing");
    for(int i=0;i<70;++i)require(!sequence.tick(player,terrain,120),"Hold pickup until fanfare");
    require(sequence.pose==1,"First pickup pose lasts 2.2 seconds");
    sequence.tick(player,terrain,120);require(sequence.pose==2,"Victory pose follows pickup");
    while(sequence.celebration<119)require(!sequence.tick(player,terrain,120),"Fanfare must complete");
    require(sequence.tick(player,terrain,120),"Return control after fanfare without leaving area");
    std::cout<<"Shine conditions, persistence, pickup and death sequences passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
