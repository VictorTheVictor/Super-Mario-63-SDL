#include "enemies.hpp"

namespace sm63 {
namespace {
void hurt(Player& p,float source,int damage) {
    if(p.invincibleTicks)return;
    p.health-=damage;p.invincibleTicks=90;p.hurtTicks=7;p.spinSpeed=0;
    p.vx=p.x>=source?7.f:-7.f;p.vy=-5;p.grounded=false;p.pound=Pound::None;
    p.diving=p.running=false;p.diveRecovery=p.flipTicks=p.diveFlipTicks=p.jumpBuffer=0;p.jumpChain=p.chainWindow=0;
}
}
void Enemy::knock(float speed,float lift,bool strong) {
    state=kind==EnemyKind::Koopa?EnemyState::Shell:EnemyState::Tumble;
    vx=speed;vy=lift;grounded=false;age=0;powerHit|=strong;
    if(kind==EnemyKind::Bobomb && !fuse)fuse=1;
}
bool Enemy::waterHit(const Player& p) const {
    if(!p.fludd.spraying)return false;
    const float pack=p.x+(p.facingLeft?2.f:-16.f);
    if(p.fludd.nozzle==Nozzle::Turbo || (p.fludd.nozzle==Nozzle::Hover && p.diving)) {
        const float end=pack+(p.facingLeft?36:-36);
        return x+10>std::min(pack,end) && x-10<std::max(pack,end) && y>p.y-14 && y-height()<p.y-8;
    }
    return x+10>pack+1 && x-10<pack+11 && y>p.y-5 && y-height()<p.y+34;
}

void Enemy::tick(const Terrain& terrain, const Player& player) {
    previousX = x; previousY = y;
    if (state == EnemyState::Gone) return;
    if (state == EnemyState::Squished) {
        if (++age >= 11) state = EnemyState::Gone;
        return;
    }
    ++age;
    if(contactCooldown>0)--contactCooldown;
    if(state==EnemyState::Exploding){if(age>=30)state=EnemyState::Gone;return;}
    if(kind==EnemyKind::Goonie) {
        vx=direction*(age%295>=250?3.3f:2.2f);vy=riding?.667f:(age%295>=250?1.5f:-.825f);
        x+=vx;y+=vy;
        if(x<terrain.minX-5)x+=terrain.width;
        if(x>terrain.minX+terrain.width+5)x-=terrain.width;
        if(y<terrain.minY-5)y+=terrain.rows;
        if(y>terrain.minY+terrain.rows+5)y-=terrain.rows;
        if(std::abs(x-previousX)>10 || std::abs(y-previousY)>10){previousX=x;previousY=y;riding=false;}
        return;
    }
    if(kind==EnemyKind::Cheep && state==EnemyState::Flying) {
        const bool water=terrain.wet(x,y);const float distance=std::hypot(player.x-x,player.y-17-y);
        if(water) {
            seeing=hungry && distance<80;
            if(seeing){direction=player.x>x?1:-1;vx+=(player.x-x)/std::max(1.f,distance)*.3f;vy+=(player.y-17-y)/std::max(1.f,distance)*.3f;}
            else {if(++patrolTicks>=96){patrolTicks=0;direction=-direction;}vx+=(direction*2-vx)/7;}
            vx*=.7f;vy*=.7f;
        } else {if(lastWater && seeing){vx*=2;vy=-11;}vy+=1;}
        if(!terrain.body(x+vx,y,9,8))x+=vx;else {vx=-vx*.7f;direction=-direction;}
        if(!terrain.body(x,y+vy,9,8))y+=vy;else vy=-vy*.5f;
        lastWater=water;return;
    }
    if(state==EnemyState::Flying) {
        if(kind==EnemyKind::Penguin) {
            if(++patrolTicks>=100){direction=-direction;patrolTicks=0;}
            vx=(vx+direction*walkSpeed/6)*.7f;vy=std::cos(age*.1f)*.75f;
            y+=(homeY-y)/50+vy;
        }
        if(kind==EnemyKind::ShyGuy){if(++patrolTicks>75){direction=-direction;patrolTicks=0;}vx+=(direction-vx)/5;}
        // Original winged Koopas hover; Shy Guys patrol horizontally.
        if(!terrain.body(x+vx,y,10,height()))x+=vx;else {vx=0;direction=-direction;}
        return;
    }
    // Native rectangular perception approximates the source eyesight movieclip.
    const bool visible = (kind==EnemyKind::Goomba || kind==EnemyKind::Bobomb) && std::abs(player.x-x) < 120 && std::abs(player.y-y) < 45;
    if(kind==EnemyKind::Bobomb) {
        if(visible && !fuse)fuse=1;
        if(fuse && ++fuse>=96){explode();return;}
    }
    if (state == EnemyState::Walking) {
        if (visible) direction = player.x < x ? -1 : 1;
        if (visible && !seeing && grounded) { vy = -5; grounded = false; }
        seeing = visible;
        if (!visible && kind!=EnemyKind::Penguin && ++patrolTicks > 100) { direction = -direction; patrolTicks = 0; }
        bool supportAhead = false;
        for (int dy = -5; dy < 30; ++dy)
            supportAhead |= terrain.at(static_cast<int>(x + direction*20 + vx*7), static_cast<int>(y)+dy) != 0;
        if (grounded && !supportAhead) { direction = -direction; vx = 0; }
        else if (!visible || std::abs(player.x-x)>20) {
            if(kind==EnemyKind::Penguin) {
                if(++patrolTicks>=64){direction=-direction;patrolTicks=0;}
                if(patrolTicks<32)vx+=(direction*walkSpeed-vx)/5;
                else vx=Player::friction(vx,.5f,1.2f);
            }
            else if(kind==EnemyKind::Koopa || kind==EnemyKind::ShyGuy)vx+=(direction*(kind==EnemyKind::Koopa?3.f:2.f)-vx)/5;
            else vx += direction * (fuse?.67f:.5f);
        }
    }
    if (!grounded) vy = std::min(10.f, (vy + 1) * 0.98f);
    vx = std::clamp(vx, -15.f, 15.f);
    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(vx),std::abs(vy))*2)));
    auto body = [&](float px, float py) { return terrain.body(px,py,10,height()); };
    for (int i=0;i<steps;++i) {
        const float nx=x+vx/steps;
        if (!body(nx,y)) x=nx;
        else {
            bool stepped=false;
            if (grounded) for (int lift=1;lift<=3;++lift)
                if (!body(nx,y-lift)) { x=nx; y-=lift; stepped=true; break; }
            if (!stepped && kind==EnemyKind::Bobomb && state==EnemyState::Tumble && powerHit && std::abs(vx)>=7){explode();return;}
            if (!stepped) { direction=-direction; vx=(state==EnemyState::Tumble || state==EnemyState::Shell) ? -vx*.7f : 0; }
        }
        const float ny=y+vy/steps;
        float top=0;
        const float impactSpeed=std::abs(vy)+std::abs(vx/2);
        bool landed=false;
        if (vy>=0 && terrain.oneWayLanding(x,y,ny,top) && !body(x,top)) {
            y=top; grounded=true; landed=true;vy=0;
        } else if (!body(x,ny)) y=ny;
        else if (vy!=0) {
            float clear=y, blocked=ny;
            for (int j=0;j<10;++j) {
                float mid=(clear+blocked)/2;
                if (body(x,mid)) blocked=mid; else clear=mid;
            }
            y=clear; grounded=vy>0;landed=grounded;vy=0;
        }
        if(landed && kind==EnemyKind::Bobomb && state==EnemyState::Tumble && powerHit && impactSpeed>11){explode();return;}
    }
    float top=0;
    grounded=vy>=0 && (body(x,y+.05f) || terrain.oneWayLanding(x,y,y+.05f,top));
    if (grounded && state==EnemyState::Tumble) {
        vx=Player::friction(vx,.5f,1.1f);
        if (std::abs(vx)<2 && kind==EnemyKind::Bobomb && powerHit){explode();return;}
        if (std::abs(vx)<2) { state=powerHit && kind!=EnemyKind::Bobomb && kind!=EnemyKind::Penguin ? EnemyState::Squished : EnemyState::Walking; age=0; }
    }
    if(kind!=EnemyKind::Penguin || state==EnemyState::Tumble)vx=state==EnemyState::Shell?vx*.99f:Player::friction(vx,state==EnemyState::Tumble?.05f:.3f,state==EnemyState::Tumble?1.01f:1.1f);
    if (y>terrain.minY+terrain.rows+100) state=EnemyState::Gone;
}

void Enemy::interact(Player& player, float oldX, float oldY, bool jumpHeld) {
    if (state==EnemyState::Gone || state==EnemyState::Squished) return;
    if(state==EnemyState::Exploding) {
        if(age<6 && std::abs(player.x-x)<48 && std::abs(player.y-17-(y-15))<45)hurt(player,x,2);
        return;
    }
    if(kind==EnemyKind::Goonie) {
        const float top=y-8;
        if(player.vy>=0 && oldY<=previousY-8+2 && player.y>=top && std::abs(player.x-x)<30) {
            player.x+=x-previousX;player.y=top;player.vy=0;player.grounded=true;riding=true;
        }else riding=false;
        return;
    }
    if(kind==EnemyKind::Penguin) {
        const bool touch=std::abs(player.x-x)<22 && player.y>y-height() && player.y-34<y;
        if(touch && player.fludd.spraying && player.fludd.nozzle==Nozzle::Turbo && std::abs(player.vx)>=7) {
            knock((vx+(player.vx-vx)/2)*.4f,(vy-5)*.7f,false);return;
        }
        if(player.spinSpeed>0 && std::abs(player.x-x)<36 && player.y>y-30 && player.y-34<y) {
            const float side=player.x>x?1.f:-1.f;player.vx+=side*2;vx=(vx-side*3)*.8f;return;
        }
        if(player.vy>1 && oldY<=previousY-height()+2 && player.y>=y-height() && std::abs(player.x-x)<20) {
            player.y=y-height();player.vy=-player.vy*.5f-5;player.grounded=false;player.pound=Pound::None;return;
        }
        if(touch && state!=EnemyState::Tumble && !contactCooldown && (player.grounded || player.y-34>y-height())) {
            if(grounded && state!=EnemyState::Flying)vx+=player.vx/2;
            player.vx=-player.vx*.5f+(player.x>x?7:-7);player.vy=(player.vy-5)*.5f;player.grounded=false;contactCooldown=8;
        }
        return;
    }
    if(waterHit(player)) {
        bool rear=player.fludd.nozzle==Nozzle::Turbo || (player.fludd.nozzle==Nozzle::Hover && player.diving);
        float speed=vx*.9f+(player.facingLeft?-1.f:1.f)*(rear?-6.f:3.f);
        knock(speed,vy-1.5f,std::abs(speed)>4.5f);return;
    }
    if(player.spinSpeed>0 && (state!=EnemyState::Tumble || kind==EnemyKind::Bobomb) && std::abs(player.x-x)<40 && player.y>y-32 && player.y-34<y) {
        knock(vx*.7f+(player.x>x ? -20.f : 20.f),vy-1.5f,true);return;
    }
    // Relative swept feet/head crossing catches fast falls without side-hit stomps.
    const float before=oldY-(previousY-height()), after=player.y-(y-height());
    if (before<=1 && after>=0 && after>before && player.vy>0) {
        const float t=std::clamp(-before/(after-before),0.f,1.f);
        const float px=oldX+(player.x-oldX)*t, ex=previousX+(x-previousX)*t;
        if (std::abs(px-ex)<18) {
            const auto beforeState=state;
            if(state==EnemyState::Flying && kind!=EnemyKind::Cheep)state=EnemyState::Walking;
            else if(kind==EnemyKind::Bobomb){knock((player.x<x?1.f:-1.f)*5,-2,false);}
            else if(kind==EnemyKind::Koopa){state=EnemyState::Shell;vx=beforeState==EnemyState::Shell && std::abs(vx)<1?(player.x<x?10.f:-10.f):0;contactCooldown=10;}
            else state=EnemyState::Squished;
            age=0;
            // Bounce immediately; source EJump/squish staging remains pending.
            player.y=std::min(player.y,y-height());
            player.vy=jumpHeld ? -10.f : -8.f;
            player.grounded=false;
            player.pound=Pound::None; player.diving=false; player.diveRecovery=0;
            player.running=false; player.jumpChain=player.chainWindow=0;
            return;
        }
    }
    float enter=0, leave=1;
    auto sweepAxis = [&](float start, float end, float low, float high) {
        const float delta=end-start;
        if (std::abs(delta)<0.00001f) return start>low && start<high;
        float a=(low-start)/delta, b=(high-start)/delta;
        if (a>b) std::swap(a,b);
        enter=std::max(enter,a); leave=std::min(leave,b);
        return enter<leave;
    };
    if (!sweepAxis(oldX-previousX,player.x-x,-18,18) ||
        !sweepAxis(oldY-previousY,player.y-y,-25,34)) return;
    if (((player.diving && std::abs(player.vx)>1) || (player.fludd.spraying && player.fludd.nozzle==Nozzle::Turbo && std::abs(player.vx)>=7)) && (state!=EnemyState::Tumble || kind==EnemyKind::Bobomb)) {
        float speed=vx+player.vx-vx/1.5f;
        knock(speed,-std::abs((player.vx+1)/5),std::abs(speed)>8.5f);
        return;
    }
    if(state==EnemyState::Shell) {
        if(!contactCooldown){vx+=(player.vx-vx)/2+(player.x<x?3.f:-3.f);contactCooldown=8;}
        return;
    }
    if (state==EnemyState::Tumble || player.invincibleTicks) return;
    if(kind==EnemyKind::Bobomb) {
        const float incoming=player.vx;
        player.vx=oldX>=previousX?3.f:-3.f;
        vx-=oldX>=previousX?std::max(incoming/3,0.f):std::min(incoming/3,0.f);
        return;
    }
    hurt(player,previousX,1);player.vx=oldX>=previousX?7.f:-7.f;
}
void Enemies::tick(const Terrain& terrain,Player& player,float oldX,float oldY,bool jumpHeld) {
    for(auto& enemy:actors) {
        enemy.tick(terrain,player);
        const float px=player.x,py=player.y,pvy=player.vy;
        enemy.interact(player,oldX,oldY,jumpHeld);
        if(enemy.kind==EnemyKind::Goonie && enemy.riding && terrain.body(player.x,player.y)) {
            player.x=px;player.y=py;player.vy=pvy;player.grounded=false;enemy.riding=false;
        }
    }
    for(auto& attacker:actors) {
        const bool blast=attacker.state==EnemyState::Exploding && attacker.age<6;
        const bool shell=attacker.state==EnemyState::Shell && std::abs(attacker.vx)>2;
        if(!blast && !shell)continue;
        for(auto& target:actors) {
            if(&attacker==&target || target.kind==EnemyKind::Goonie || target.state==EnemyState::Gone || target.state==EnemyState::Exploding || target.state==EnemyState::Squished)continue;
            if(std::abs(target.x-attacker.x)<(blast?48:24) && std::abs(target.y-attacker.y)<(blast?45:25)) {
                if(target.kind==EnemyKind::Bobomb){target.fuse=95;target.knock(attacker.vx,-5,true);}
                else if(target.state!=EnemyState::Tumble){target.knock(blast?(target.x<attacker.x?-10.f:10.f):attacker.vx,-5,true);}
            }
        }
    }
}
}
