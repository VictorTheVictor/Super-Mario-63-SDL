#pragma once
#include "physics.hpp"
#include "platforms_data.hpp"
namespace sm63 {
struct Platform {
    platformdata::Spec spec;
    float x,y,previousX,previousY,vx=0,vy=0,angle=0,previousAngle=0,rotationSpeed=0;
    int timerX,timerY,dirX,dirY,age=0;
    float opacity=1;bool triggered=false;
    bool solid() const{return opacity>=.5f;}
    explicit Platform(platformdata::Spec s):spec(s),x(s.x),y(s.y),previousX(x),previousY(y),
        timerX(static_cast<int>(s.speedX+s.offsetX)),timerY(static_cast<int>(s.speedY+s.offsetY)),dirX(s.dirX),dirY(s.dirY) {
        if(s.kind==2){angle=s.phase;x=s.x-std::sin(angle*.01745329252f)*s.radius;y=s.y+std::cos(angle*.01745329252f)*s.radius;previousX=x;previousY=y;}
        if(!s.kind || s.kind==4){x+=s.speedX;y+=s.dirY*s.speedY;previousX=x;previousY=y;}
    }
    Terrain::Polygon polygon() const {
        Terrain::Polygon result;const float radians=angle*.01745329252f,cs=std::cos(radians)*spec.scale/100,sn=std::sin(radians)*spec.scale/100;
        for(auto p:platformdata::blockHull)result.push_back({x+cs*p[0]-sn*p[1],y+sn*p[0]+cs*p[1]});
        return result;
    }
    bool support(float px,float& top) const {
        if(spec.kind!=4)return Terrain::surfaceHeight(surface(),px,top);
        bool found=false;top=1e9f;auto points=polygon();
        for(std::size_t i=0;i<points.size();++i) {
            auto a=points[i],b=points[(i+1)%points.size()];
            if(b[0]-a[0]<.001f)continue;
            const float left=std::max(px-8,a[0]),right=std::min(px+8,b[0]);
            if(left>right)continue;
            auto at=[&](float u){return a[1]+(b[1]-a[1])*(u-a[0])/(b[0]-a[0]);};
            top=std::min({top,at(left),at(right)});found=true;
        }
        return found;
    }
    Terrain::Surface surface() const {
        if(spec.kind==3)return {x-8.5f,y-6.5f,x+8.5f,y-6.5f};
        if(spec.kind!=1){float scale=(spec.scale+1)*.5f;return {x-17.525f*scale,y-6.025f,x+17.475f*scale,y-6.025f};}
        const float radians=angle*.01745329252f,cs=std::cos(radians),sn=std::sin(radians),half=41.f*spec.scale;
        return {x-half*cs+13.5f*sn,y-half*sn-13.5f*cs,x+half*cs+13.5f*sn,y+half*sn-13.5f*cs};
    }
};
class Platforms {
public:
    std::vector<Platform> objects;
    void reset(int area,Terrain& terrain) {
        objects.clear();for(auto s:platformdata::areas.at(area))objects.emplace_back(s);publish(terrain);
    }
    void publish(Terrain& terrain)const {
        terrain.surfaces.clear();terrain.polygons.clear();
        for(const auto& p:objects)if(p.solid()) {
            if(p.spec.kind==4)terrain.polygons.push_back(p.polygon());
            else terrain.surfaces.push_back(p.surface());
        }
    }
    void tick(Terrain& terrain,Player& player) {
        for(auto& p:objects) {
            p.previousX=p.x;p.previousY=p.y;p.previousAngle=p.angle;
            float oldTop=0;
            const bool over=p.solid() && p.support(player.x,oldTop);
            const bool riding=over && player.grounded && std::abs(player.y-oldTop)<.12f;
            if(p.spec.kind==4) {
                ++p.age;
                if(p.spec.wait>0 && p.age<=p.spec.wait)
                    p.angle=std::round(p.angle/90)*90+(p.age+20>p.spec.wait && p.age<p.spec.wait?std::sin(p.age*2.7f)*2.5f:0);
                if(p.spec.wait>=0 && p.age>p.spec.wait) {
                    p.angle+=p.spec.angularSpeed;
                    if((p.age-static_cast<int>(p.spec.wait))*std::abs(p.spec.angularSpeed)>=90){p.angle=std::round(p.angle/90)*90;p.age=0;}
                }
                auto axis=[](int& timer,int period,int& direction,float accel,float speed,float& velocity,float& position){
                    if(++timer>=period){timer=0;direction=-direction;}
                    velocity=std::clamp(velocity+direction*accel,-speed,speed);position+=velocity;
                };
                axis(p.timerX,p.spec.periodX,p.dirX,p.spec.accelX,p.spec.speedX,p.vx,p.x);
                axis(p.timerY,p.spec.periodY,p.dirY,p.spec.accelY,p.spec.speedY,p.vy,p.y);
                // Publish the new shape before testing carried-player clearance.
                publish(terrain);
            }else if(p.spec.kind==3) {
                if(!p.triggered) {
                    p.opacity=std::min(1.f,p.opacity+.1f);
                    if(riding && p.opacity>=1){p.triggered=true;p.age=0;}
                }
                if(p.triggered) {
                    ++p.age;
                    if(p.age<=p.spec.periodX) {
                        p.x=p.spec.x+std::sin(p.age*2.7f)*p.spec.accelY;
                        p.y=p.spec.y+std::sin(p.age*4.1f)*p.spec.accelY;
                        if(p.age==p.spec.periodX){p.x=p.spec.x;p.y=p.spec.y;}
                    } else {
                        p.vy=Player::friction(p.vy+1,.1f,1.05f);p.y+=p.vy;
                        const int fallen=p.age-p.spec.periodX;
                        if(fallen>p.spec.periodY-10)p.opacity=std::max(0.f,p.opacity-.1f);
                        if(fallen>p.spec.periodY) {
                            if(riding){player.grounded=false;player.vy=p.vy;}
                            p.x=p.previousX=p.spec.x;p.y=p.previousY=p.spec.y;p.vy=0;p.triggered=false;p.opacity=0;
                        }
                    }
                }
            }else if(p.spec.kind==2) {
                p.angle=std::fmod(p.angle+p.spec.angularSpeed,360.f);
                p.x=p.spec.x-std::sin(p.angle*.01745329252f)*p.spec.radius;
                p.y=p.spec.y+std::cos(p.angle*.01745329252f)*p.spec.radius;
                p.vx=p.x-p.previousX;p.vy=p.y-p.previousY;
            }else if(p.spec.kind==1) {
                p.rotationSpeed=Player::friction(p.rotationSpeed,.15f,1.05f);
                if(riding)p.rotationSpeed+=(player.x-p.x)/p.spec.scale/5/((p.spec.scale-1)*.5f+1);
                p.rotationSpeed-=p.angle-Player::friction(p.angle,(riding?.3f:1.f)/p.spec.scale,1+(riding?.07f:.12f)/p.spec.scale);
                p.angle+=p.rotationSpeed/20;
                if(std::abs(p.angle)>=80)p.rotationSpeed*=-.7f;
                p.angle=std::clamp(p.angle,-80.f,80.f);
            }else {
                if(p.spec.kind==5 && !p.triggered) {
                    p.opacity=std::min(1.f,p.opacity+p.spec.reappear/100);
                    if(riding && p.opacity>=1){p.triggered=true;p.age=0;}else continue;
                }
                auto axis=[](int& timer,int period,int& direction,float accel,float speed,float& velocity,float& position){
                    if(++timer>=period){timer=0;direction=-direction;}
                    velocity=std::clamp(velocity+direction*accel,-speed,speed);position+=velocity;
                };
                axis(p.timerX,p.spec.periodX,p.dirX,p.spec.accelX,p.spec.speedX,p.vx,p.x);
                axis(p.timerY,p.spec.periodY,p.dirY,p.spec.accelY,p.spec.speedY,p.vy,p.y);
                if(p.spec.kind==5 && ++p.age>p.spec.wait) {
                    p.opacity=std::max(0.f,p.opacity-p.spec.style/100);
                    if(p.opacity==0) {
                        if(riding){player.grounded=false;player.vx+=p.vx;player.vy+=p.vy;}
                        const auto spec=p.spec;p=Platform(spec);p.opacity=0;
                    }
                }
            }
            float nextTop=0;float nx=player.x+(riding ? (p.spec.kind==1 ? p.angle/7 : p.x-p.previousX) : 0);
            if(p.solid() && p.support(nx,nextTop)) {
                const bool sweptUp=over && oldTop>=player.y && nextTop<=player.y && player.vy>=0;
                if((riding || sweptUp) && !terrain.body(nx,nextTop)) {
                    player.x=nx;player.y=nextTop;player.grounded=true;player.vy=0;
                }
            }
        }
        publish(terrain);
        bool overlap=false;
        for(const auto& polygon:terrain.polygons)overlap|=Terrain::overlaps(polygon,player.x,player.y,8,34);
        if(overlap) {
            bool resolved=false;
            for(int distance=1;distance<=128 && !resolved;++distance)
                for(const auto delta:std::array<Terrain::Point,4>{{{0,-1},{-1,0},{1,0},{0,1}}}) {
                    const float x=player.x+delta[0]*distance,y=player.y+delta[1]*distance;
                    if(!terrain.body(x,y)){player.x=x;player.y=y;player.grounded=terrain.body(x,y+.05f);resolved=true;break;}
                }
            if(!resolved)player.health=0;
        }
    }
};
}
