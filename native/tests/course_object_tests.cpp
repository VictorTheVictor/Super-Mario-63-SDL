#include "course_objects.hpp"
#include "areas.hpp"
#include <stdexcept>
#include <iostream>
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
 sm63::CourseObjects objects;objects.reset(0);sm63::Player p;
 require(objects.objects.size()==1,"Opening cannon placement");
 const auto s=objects.objects[0].spec;p.x=s.x;p.y=s.y;p.vx=8;
 require(!objects.tick(p,{}),"Fast pass must not enter cannon");
 p.vx=0;require(objects.tick(p,{}) && objects.active(),"Cannon capture");
 for(int i=0;i<70;++i)objects.tick(p,{true});
 require(objects.objects[0].angle==0,"Right-facing cannon aim limit");
 sm63::Input fire;fire.fludd=true;objects.tick(p,fire);
 require(!objects.active() && p.diving && p.vy==-25 && std::abs(p.vx)<.01f,"Cannon launch strength and dive");
 require(p.y<s.y-40,"Launch starts outside cannon");
 objects.reset(1);objects.objects.erase(std::remove_if(objects.objects.begin(),objects.objects.end(),[](const auto& o){return o.spec.kind!=0;}),objects.objects.end());require(objects.objects.size()==1,"Middle cannon placement");
 const auto left=objects.objects[0].spec;p.x=left.x;p.y=left.y;p.vx=0;
 objects.tick(p,{});
 for(int i=0;i<80;++i)objects.tick(p,{true});
 objects.tick(p,fire);require(p.vx<-38 && p.vy<0,"Left cannon uses original 40/35 launch power");
 objects.objects.clear();objects.captured=-1;
 objects.objects.emplace_back(sm63::objectdata::Spec{100,100,1,-5,-21,-15,.8f,0,0});
 auto& sling=objects.objects[0];sling.cooldown=0;p.reset();p.x=100;p.y=115;p.vy=-8;
 require(!objects.tick(p,{}),"Fast upward pass does not catch sling");
 p.vy=0;require(objects.tick(p,{}) && objects.active(),"Airborne sling catch");
 for(int i=0;i<4;++i)objects.tick(p,{});
 sm63::Input spin;spin.spin=true;objects.tick(p,spin);
 for(int i=0;i<12;++i)objects.tick(p,{});
 require(!objects.active() && p.vx==-5 && p.vy==-21,"Sling uses placement-specific trajectory");
 require(p.slingTicks==8,"Sling launch gravity phase");
 objects.reset(3);require(objects.objects.size()>=3,"Rolling ball routes imported");
 objects.objects.clear();objects.objects.emplace_back(sm63::objectdata::Spec{100,100,2,0,0,1,0,0,0});
 objects.objects[0].hurts=true;p.reset();p.x=130;p.y=117;
 objects.hitHazards(p,70,117);require(p.health==6 && p.invincibleTicks==90,"Swept rolling ball damage");
 objects.hitHazards(p,70,117);require(p.health==6,"Ball cannot hurt during invulnerability");
 sm63::Terrain terrain;terrain.load(-500,-500,1000,1000,std::vector<sm63::level::Span>{});
 sm63::Enemies enemies;enemies.actors.clear();sm63::KingBoss boss;boss.reset(100,100);
 p.reset();p.x=180;p.y=100;p.grounded=true;
 require(boss.dialogue(p,false),"Boss introduces encounter");
 for(int i=0;i<7;++i)boss.dialogue(p,false);
 boss.dialogue(p,true);require(boss.state==sm63::KingBoss::State::Fight,"Z starts boss fight");
 boss.action=sm63::KingBoss::Action::Throw;boss.actionAge=24;boss.direction=-1;boss.tick(terrain,p,enemies);
 require(enemies.actors.size()==1 && enemies.actors[0].vx==-12.5f && enemies.actors[0].fuse==1,"Boss throws live Bob-ombs");
 sm63::Enemy reflected(110,80,sm63::EnemyKind::Bobomb,4);reflected.vx=12.5f;
 p.reset();p.x=130;p.y=100;p.spinSpeed=15;
 reflected.interact(p,130,100,false);
 require(reflected.vx<0 && reflected.powerHit,"Spin can return an already tumbling boss bomb");
 enemies.actors.clear();
 for(int hit=0;hit<3;++hit) {
     enemies.actors.emplace_back(100,75,sm63::EnemyKind::Bobomb);enemies.actors.back().explode();
     boss.tick(terrain,p,enemies);require(boss.state==sm63::KingBoss::State::Hurt,"Bomb explosion damages boss");
     enemies.actors.clear();
     for(int i=0;i<27;++i)boss.tick(terrain,p,enemies);
     require(boss.hp==2-hit,"Boss loses exactly one HP per hurt sequence");
 }
 while(boss.state!=sm63::KingBoss::State::Outro)boss.tick(terrain,p,enemies);
 for(int i=0;i<7;++i)boss.dialogue(p,false);
 boss.dialogue(p,true);require(boss.defeated(),"Three bomb hits and outro finish boss");
 // Regression on the actual rounded summit: the old wide body check kept him stationary.
 sm63::Areas areas;sm63::Items items;p.reset();areas.enter({3,0,0},terrain,p,items,enemies);
 auto& summitBoss=areas.objects.boss;const float originalX=summitBoss.x;
 summitBoss.state=sm63::KingBoss::State::Fight;summitBoss.action=sm63::KingBoss::Action::Walk;
 p.x=originalX-150;p.y=summitBoss.y;p.grounded=true;enemies.actors.clear();
 for(int i=0;i<30;++i)summitBoss.tick(terrain,p,enemies);
 require(summitBoss.x<originalX-20,"King Bob-omb must walk across actual summit terrain");
 summitBoss.action=sm63::KingBoss::Action::Jump;summitBoss.actionAge=0;
 for(int i=0;i<30 && summitBoss.waves.empty();++i)summitBoss.tick(terrain,p,enemies);
 require(summitBoss.waves.size()==2 && summitBoss.rings.size()==1,"Boss landing produces two invisible damage waves and one visible ring");
 const float waveX=summitBoss.waves[0].x;summitBoss.tick(terrain,p,enemies);
 require(std::abs(summitBoss.waves[0].x-waveX+5.3125f)<.01f,"Shockwave travels at source speed");
 std::cout<<"Course object interactions passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
