#include "items.hpp"
#include <stdexcept>
#include <iostream>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    sm63::Terrain terrain;sm63::Items items;sm63::Player player;
    items.reset(terrain);
    require(items.coins.size()==34 && items.boxes.size()==3,"Original item counts");
    require(items.boxes[0].reward==5 && items.boxes[1].reward==3 && items.boxes[2].reward==30,"Original box rewards");
    // Collect along a fast dive-sized sweep, once only.
    items.coins.clear();items.coins.emplace_back(100,100);
    player.x=130;player.y=110;player.health=6;
    items.tick(terrain,player,70,110);
    require(items.count==1 && player.health==6,"Swept coin collection and partial healing");
    items.tick(terrain,player,70,110);require(items.count==1,"Coin cannot count twice");
    for(int i=0;i<4;++i)items.coins.emplace_back(130,100);
    items.tick(terrain,player,130,110);
    require(items.count==5 && player.health==7,"Five gold coins restore one health");
    items.coins.emplace_back(130,100,2);items.tick(terrain,player,130,110);
    require(items.count==7 && items.redCount==1 && player.health==8,"Red coin value and healing");
    // Landing on the upper box is solid; ordinary jumps do not break it.
    items.reset(terrain);const auto box=items.boxes[1];
    player.reset();player.x=box.x;player.y=box.y-70;
    for(int i=0;i<60;++i){player.tick(terrain,{});require(!terrain.body(player.x,player.y),"Player penetrated intact box");}
    require(player.grounded && std::abs(player.y-(box.y-31))<.05f,"Box must support player");
    items.breakBoxes(terrain,player,0,0,sm63::Pound::None);
    require(!items.boxes[1].broken,"Standing does not break box");
    auto before=items.coins.size();items.breakBoxes(terrain,player,0,10,sm63::Pound::Falling);
    require(items.boxes[1].broken && !terrain.blocks[1].active && items.coins.size()==before,"Pound removes collider and queues reward");
    items.breakBoxes(terrain,player,0,10,sm63::Pound::Falling);
    player.x=-100;player.y=-100;
    for(int i=1;i<=3;++i) {
        items.tick(terrain,player,player.x,player.y);
        require(items.coins.size()==before+i,"Three-coin box must emit one coin per tick");
    }
    items.tick(terrain,player,player.x,player.y);
    require(items.coins.size()==before+3,"Box reward only once");
    // Large rewards stream over five ticks rather than appearing in one burst.
    items.reset(terrain);player.reset();player.x=items.boxes[2].x;player.y=items.boxes[2].y-31;
    items.breakBoxes(terrain,player,0,10,sm63::Pound::Falling);
    player.x=-100;player.y=-100;before=items.coins.size();
    for(int i=1;i<=5;++i) {
        items.tick(terrain,player,player.x,player.y);
        require(items.coins.size()==before+i*6,"Thirty-coin box must emit six coins per tick");
    }
    items.tick(terrain,player,player.x,player.y);
    require(items.coins.size()==before+30 && items.boxes[2].emitted==30,"Large reward must stop at exact total");
    // The first descent tick can reach a box directly after pound windup.
    items.reset(terrain);player.reset();player.x=items.boxes[1].x;player.y=items.boxes[1].y-35;
    player.pound=sm63::Pound::Windup;player.poundTicks=12;
    player.tick(terrain,{});
    items.breakBoxes(terrain,player,0,0,sm63::Pound::Windup);
    require(items.boxes[1].broken && player.pound==sm63::Pound::Falling,"First pound descent must break nearby box");
    // Side impact threshold, using actual contact position on a fresh box.
    items.reset(terrain);player.reset();player.x=items.boxes[1].x-24;player.y=items.boxes[1].y-5;
    items.breakBoxes(terrain,player,8,0,sm63::Pound::None);
    require(!items.boxes[1].broken,"Slow side contact must not break box");
    items.breakBoxes(terrain,player,10,0,sm63::Pound::None);
    require(items.boxes[1].broken && player.vx==0,"Fast impact breaks box and stops Mario");
    items.reset(terrain);player.reset();player.x=items.boxes[1].x-35;player.y=items.boxes[1].y-5;player.vx=15;
    player.tick(terrain,{});
    items.breakBoxes(terrain,player,15,0,sm63::Pound::None);
    require(items.boxes[1].broken,"Actual swept wall impact must break box even after rebound");
    items.reset(terrain);player.reset();player.x=items.boxes[1].x-40;player.y=items.boxes[1].y-5;player.spinSpeed=15;
    items.breakBoxes(terrain,player,0,0,sm63::Pound::None);
    require(items.boxes[1].broken && player.spinSpeed==15,"Spin must break nearby box without cancelling spin");
    items.reset(terrain);items.coins.clear();player.reset();player.x=-100;
    items.enemyImpact(terrain,items.boxes[1].x,items.boxes[1].y,48);
    require(items.boxes[1].broken && !terrain.blocks[1].active,"Bob-omb blast removes box collision");
    for(int i=0;i<40;++i){items.enemyImpact(terrain,items.boxes[1].x,items.boxes[1].y,48);items.tick(terrain,player,player.x,player.y);}
    require(items.coins.size()==static_cast<std::size_t>(items.boxes[1].reward),"Repeated blast frames release box reward only once");
    // Dropped coins cannot be collected immediately and eventually expire.
    items.reset(terrain);items.coins.clear();items.drop(0,100);player.reset();player.x=0;player.y=100;
    int count=items.count;items.tick(terrain,player,0,100);
    require(items.count==count && !items.coins[0].collectedTicks,"Drop pickup delay");
    player.x=-150;player.y=-100;
    for(int i=0;i<390;++i)items.tick(terrain,player,player.x,player.y);
    require(items.coins[0].gone,"Uncollected drop expires");
    items.reset(terrain);items.coins.clear();items.drop(0,100);
    float launchX=items.coins[0].vx,launchY=items.coins[0].vy;
    items.reset(terrain);items.coins.clear();items.drop(0,100);
    require(items.coins[0].vx==launchX && items.coins[0].vy==launchY,"Drop random seed must reset for reproducible replays");
    items.reset(terrain);require(items.count==0 && items.redCount==0 && terrain.blocks[1].active,"Reset restores items and counters");
    items=sm63::Items{};terrain.blocks.clear();player.reset();player.x=100;player.y=100;player.health=3;player.fludd.power=10;
    items.coins.emplace_back(100,90,5);items.tick(terrain,player,100,100);
    require(items.count==5 && player.health==6 && player.fludd.power==60,"Blue coin value, healing and power");
    items.starCoins.push_back({100,80,2});items.tick(terrain,player,100,100);
    require(items.starCoinMask==(1<<2) && items.pickedStarCoin==2,"Star Coin tracks its original ID");
    items.tick(terrain,player,100,100);require(items.pickedStarCoin==0,"Star Coin cannot collect twice");
    items.supplies.push_back({100,80,17});items.tick(terrain,player,100,100);
    require(items.wingPicked && player.wingTicks==1440,"Course Rainbow Star grants 45-second Wing Cap");
    items=sm63::Items{};items.supplies.push_back({100,80,18});player.reset();
    player.x=100;player.y=100;player.health=2;player.grounded=true;
    for(int i=0;i<40;++i)items.tick(terrain,player,100,100);
    require(player.health==2 && items.supplies[0].spinSpeed==1,"Standing in heart must not heal");
    player.x=200;items.tick(terrain,player,200,100);
    player.x=100;player.grounded=false;player.vx=6;items.tick(terrain,player,100,100);
    require(items.supplies[0].spinSpeed==1,"Airborne heart contact must not activate");
    player.x=200;items.tick(terrain,player,200,100);
    player.x=100;player.grounded=true;items.tick(terrain,player,100,100);
    require(items.supplies[0].spinSpeed==12 && player.health==2,"Running contact starts gradual healing");
    items.tick(terrain,player,100,100);
    require(items.supplies[0].spinSpeed<12,"Remaining inside cannot restart heart");
    for(int i=0;i<80;++i)items.tick(terrain,player,100,100);
    require(player.health==8 && !items.supplies[0].gone && items.supplies[0].spinSpeed==1,"Heart heals and remains reusable");
    player.health=4;player.x=200;items.tick(terrain,player,200,100);
    player.x=100;items.tick(terrain,player,100,100);
    for(int i=0;i<80;++i)items.tick(terrain,player,100,100);
    require(player.health==8,"Heart can heal again after re-entry");
    std::cout<<"Coin collection, healing, box collision, rewards, drops and reset passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
