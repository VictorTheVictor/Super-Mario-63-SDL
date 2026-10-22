#pragma once
#include "physics.hpp"
#include "boss.hpp"
#include "objects_data.hpp"
namespace sm63 {
struct CourseObject {
    objectdata::Spec spec;
    int age=0,cooldown=0,state=0;
    float angle=0,angularSpeed=0,x=0,y=0,previousX=0,previousY=0,scale=1;bool hurts=false;
    explicit CourseObject(objectdata::Spec s):spec(s),angle(s.kind==1?s.c:s.c<0?-45:s.c>0?45:22.5f){x=previousX=s.x;y=previousY=s.y;if(s.kind==1)cooldown=33;}
};
class CourseObjects {
public:
    KingBoss boss;
    std::vector<CourseObject> objects;
    int captured=-1;
    const char* sound=nullptr;
    void reset(int area){objects.clear();boss=KingBoss{};for(auto s:objectdata::areas.at(area)){objects.emplace_back(s);if(s.kind==3)boss.reset(s.x,s.y);}captured=-1;}
    void hitHazards(Player& p,float oldX,float oldY) {
        if(p.health<=0 || p.invincibleTicks)return;
        for(const auto& o:objects)if(o.spec.kind==2 && o.hurts) {
            const float ax=oldX-o.previousX,ay=oldY-17-o.previousY;
            const float dx=p.x-o.x-ax,dy=p.y-17-o.y-ay;
            const float t=std::clamp(-(ax*dx+ay*dy)/std::max(.001f,dx*dx+dy*dy),0.f,1.f);
            if(std::hypot(ax+dx*t,ay+dy*t)<15*o.scale+13) {
                p.health=std::max(0,p.health-2);p.invincibleTicks=90;p.hurtTicks=p.grounded?32:7;
                p.vx=p.grounded?0:p.x<o.x?-3:3;p.vy=p.grounded?0:-5;
                p.spinSpeed=0;p.diving=false;p.pound=Pound::None;break;
            }
        }
    }
    bool hidesPlayer() const {return active() && objects[captured].spec.kind==0;}
    bool active() const {return captured>=0;}
    // Capture and scripted transport override normal input without advancing player physics.
    bool tick(Player& p,const Input& input) {
        sound=nullptr;
        for(auto& o:objects) {
            if(o.cooldown>0)--o.cooldown;
            if(o.spec.kind==2) {
                const auto& path=objectdata::paths.at(static_cast<int>(o.spec.a));
                const auto& pose=path.at(o.age++%path.size());o.previousX=o.x;o.previousY=o.y;
                o.x=o.spec.x+pose.x*o.spec.c;o.y=o.spec.y+pose.y*o.spec.c;o.scale=pose.scale*o.spec.c;o.hurts=pose.hurts;
            }
        }
        if(captured<0)for(std::size_t i=0;i<objects.size();++i) {
            auto& o=objects[i];
            if(o.spec.kind==1 && !o.cooldown && !p.grounded && p.vy>-7 && p.health>0 &&
               std::abs(p.x-o.spec.x)<28*o.spec.d && std::abs(p.y-17-o.spec.y)<35*o.spec.d) {
                captured=static_cast<int>(i);o.age=0;o.state=1;
                p.diving=false;p.diveRecovery=p.diveFlipTicks=0;p.pound=Pound::None;
                p.fludd.spraying=false;p.fludd.charge=p.fludd.blast=0;break;
            }
            if(o.spec.kind==0 && !o.cooldown && std::abs(p.x-o.spec.x+.15f)<15 &&
               p.y>o.spec.y-3 && p.y<o.spec.y+18 && std::abs(p.vx)<7 && p.health>0) {
                captured=static_cast<int>(i);o.age=0;o.state=1; o.angle=o.spec.c<0?-45:o.spec.c>0?45:22.5f;o.angularSpeed=0;
                p.vx=p.vy=0;p.diving=false;p.diveRecovery=p.diveFlipTicks=0;p.spinSpeed=0;p.pound=Pound::None;
                p.fludd.spraying=false;p.fludd.charge=p.fludd.blast=0;sound="cannon_enter";break;
            }
        }
        if(captured<0)return false;
        auto& o=objects[captured];++o.age;
        if(o.spec.kind==1) {
            p.x+=(o.spec.x-p.x)/5;p.y+=(o.spec.y-p.y)/5;p.vx*=.6f;p.vy*=.6f;
            p.spinSpeed=15;p.spinPhase=std::fmod(p.spinPhase+2,40.f);
            if(o.state==1) {
                if(input.dive)o.age+=2;
                if(o.age>=4 && o.age<=29 && (input.spin || input.fludd)){o.state=2;o.age=0;}
                else if(o.age>36){o.state=0;o.cooldown=33;captured=-1;p.spinSpeed=0;p.vy=0;}
            } else if(o.age>=12) {
                p.vx=o.spec.a;p.vy=o.spec.b;p.grounded=false;p.facingLeft=p.vx<0;
                p.spinSpeed=0;p.flipTicks=16;p.slingTicks=8;
                o.state=0;o.cooldown=40;captured=-1;
            }
            return true;
        }
        p.x=o.spec.x;p.y=o.spec.y;p.vx=p.vy=0;
        if(o.age==39)sound="cannon_rise";
        if(o.age<50)return true;
        o.state=2;
        if(o.age==50)sound="cannon_ready";
        o.angularSpeed+=int(input.right)-int(input.left);o.angle+=o.angularSpeed;
        o.angle=std::clamp(o.angle,o.spec.c>0?0.f:-75.f,o.spec.c<0?0.f:75.f);
        o.angularSpeed=Player::friction(o.angularSpeed,.5f,1.1f);
        if(input.fludd) {
            const float radians=o.angle*.01745329252f;
            p.x=o.spec.x+std::sin(radians)*30;p.y=o.spec.y-20-std::cos(radians)*30;
            p.vx=std::sin(radians)*o.spec.a;p.vy=-std::cos(radians)*o.spec.b;
            p.facingLeft=p.vx<0;p.grounded=false;p.diving=true;p.diveTicks=3;p.wasDive=true;
            p.invincibleTicks=16;o.cooldown=48;o.state=0;captured=-1;sound="cannon_shot";
        }
        return true;
    }
};
}
