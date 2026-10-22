#include "platforms.hpp"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
    sm63::Terrain terrain;terrain.load(-500,-500,1000,1000,std::vector<sm63::level::Span>{});
    sm63::Platforms platforms;sm63::Player player;
    require(sm63::platformdata::areas[0].size()==2 && sm63::platformdata::areas[1].size()==38 && sm63::platformdata::areas[2].size()>7,"Imported platform counts");
    platforms.objects.emplace_back(sm63::platformdata::Spec{0,0,0,2,20,30,1,-1,.5f,.5f,2,3});
    auto& green=platforms.objects.front();
    player.x=green.x;player.y=green.surface().y1;player.grounded=true;
    const float startX=green.x;bool reversed=false;
    for(int i=0;i<100;++i){
        platforms.tick(terrain,player);player.tick(terrain,{});
        require(player.grounded && std::abs(player.x-green.x)<.001f && std::abs(player.y-green.surface().y1)<.001f,"Moving platform must carry standing Mario on both axes");
        reversed|=green.vx<0;
    }
    require(reversed && green.x!=startX,"Timer reversal and movement");
    platforms.tick(terrain,player);player.tick(terrain,{false,false,true});
    require(!player.grounded && player.vy<0,"Jump must detach from moving platform");
    platforms.objects.clear();platforms.objects.emplace_back(sm63::platformdata::Spec{0,0,0,2,0,0,1,1,0,0,0,0});platforms.publish(terrain);
    player.reset();player.y=15;player.vy=-15;
    for(int i=0;i<4;++i)player.tick(terrain,{});
    require(player.y<-6.025f && !player.grounded,"Jump through platform underside");
    for(int i=0;i<40 && !player.grounded;++i)player.tick(terrain,{});
    require(player.grounded && std::abs(player.y+6.025f)<.001f,"Land on platform from above");
    platforms.objects.clear();
    platforms.objects.emplace_back(sm63::platformdata::Spec{0,0,2,2,0,0,1,1,0,0,0,0,50,0,5});
    auto& circle=platforms.objects.front();player.reset();player.x=circle.x;player.y=circle.surface().y1;player.grounded=true;
    for(int i=0;i<72;++i){platforms.tick(terrain,player);player.tick(terrain,{});require(player.grounded,"Orbiting platform carries rider for a full revolution");}
    require(std::abs(circle.x)<.001f && std::abs(circle.y-50)<.001f,"Circle returns to its source start after 360 degrees");
    platforms.objects.clear();platforms.objects.emplace_back(sm63::platformdata::Spec{0,0,1,1,0,0,1,1,0,0,0,0});
    auto& log=platforms.objects.front();player.reset();player.x=25;player.y=-13.5f;player.grounded=true;
    for(int i=0;i<10;++i){platforms.tick(terrain,player);player.tick(terrain,{});}
    require(log.angle>1 && player.grounded,"Off-center weight tips log while maintaining support");
    player.x=300;player.grounded=false;
    for(int i=0;i<1500;++i)platforms.tick(terrain,player);
    require(std::abs(log.angle)<.05f,"Unloaded log settles toward horizontal");
    platforms.objects.clear();platforms.objects.emplace_back(sm63::platformdata::Spec{0,0,0,2,100,100,1,-1,0,1,0,3});
    auto& lift=platforms.objects.front();player.reset();player.x=lift.x;player.y=lift.surface().y1;player.grounded=true;
    terrain.blocks.push_back({-30,player.y-40,30,player.y-34.1f});
    platforms.tick(terrain,player);
    require(!terrain.body(player.x,player.y),"Lift must not embed Mario in ceiling");
    platforms.objects.clear();
    sm63::platformdata::Spec falling{100,100,3,1,10,24,1,-1,0,3,0,0};
    platforms.objects.emplace_back(falling);platforms.publish(terrain);
    player.reset();player.x=100;player.y=93.5f;player.grounded=true;
    platforms.tick(terrain,player);
    require(platforms.objects[0].triggered,"Stepping on log triggers shaking");
    player.x=300;player.grounded=false;
    for(int i=0;i<15;++i)platforms.tick(terrain,player);
    require(platforms.objects[0].y>100 && platforms.objects[0].vy>0,"Log falls after warning");
    for(int i=0;i<19;++i)platforms.tick(terrain,player);
    require(platforms.objects[0].y==100 && !platforms.objects[0].solid(),"Faded log resets without carrying player");
    for(int i=0;i<11;++i)platforms.tick(terrain,player);
    require(platforms.objects[0].solid(),"Log becomes solid again");
    platforms.objects.clear();
    sm63::platformdata::Spec rotating{100,100,4,100,0,0,1,-1,0,0,0,0};
    rotating.wait=0;rotating.angularSpeed=3;
    platforms.objects.emplace_back(rotating);platforms.publish(terrain);
    player.reset();player.x=100;platforms.objects[0].support(player.x,player.y);player.grounded=true;
    for(int i=0;i<30;++i){platforms.tick(terrain,player);player.tick(terrain,{});require(!terrain.body(player.x,player.y),"Rotating block must not engulf rider");}
    require(std::abs(platforms.objects[0].angle-90)<.001f,"Quarter turn finishes on cardinal angle");
    platforms.objects[0].angle=45;platforms.publish(terrain);
    require(terrain.body(100,100,1,1),"Rotating block is solid through its center");
    require(!terrain.body(160,160,1,1),"Rotating collision must not be its bounding rectangle");
    require(sm63::platformdata::areas[4].size()>=21,"Secret rotating-block placements imported");
    platforms.objects.clear();terrain.polygons.clear();
    sm63::platformdata::Spec touch{100,100,5,2,65,0,1,-1,.5f,0,3,0};touch.wait=10;touch.style=20;
    platforms.objects.emplace_back(touch);platforms.publish(terrain);player.reset();player.x=400;
    for(int i=0;i<20;++i)platforms.tick(terrain,player);
    require(platforms.objects[0].x==100,"Touch platform waits for rider");
    player.x=100;player.y=platforms.objects[0].surface().y1;player.grounded=true;
    platforms.tick(terrain,player);require(platforms.objects[0].triggered,"Touch activates platform");
    player.x=400;player.grounded=false;
    for(int i=0;i<8;++i)platforms.tick(terrain,player);
    require(platforms.objects[0].x>100,"Activated platform moves");
    for(int i=0;i<9;++i)platforms.tick(terrain,player);
    require(platforms.objects[0].x==100 && !platforms.objects[0].triggered,"Touch platform resets after fading");
    std::cout<<"Platform import, carry, reversal, one-way landing, jumping, tipping and ceiling clearance passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
