#include "physics.hpp"

namespace sm63 {
void Player::tick(const Terrain& terrain, const Input& requestedInput) {
        respawned=false;
    const bool wasSwimming=swimming;swimming=terrain.wet(x,y-8.5f);
    const bool swimPressed=requestedInput.jump && !wasJump;
    if(terrain.wet(x,y-34)) {if(++breathTicks>80){breathTicks=0;if(--oxygen<=0){oxygen=0;health=0;}}}
    else if(++breathTicks>8){breathTicks=0;oxygen=std::min(8,oxygen+1);}
    const bool surfaceJump=!grounded && requestedInput.jump && !hurtTicks && !terrain.wet(x,y-12) && terrain.wet(x,y+5) && vy<1 && vy>-5;
    const float surfaceJumpSpeed=vy*.7f-level::jumpSpeed/1.5f;
    const bool onIce=terrain.icy(x,y-1) || terrain.icy(x,y+1);
    if(wingTicks>0)--wingTicks;
    if(flying && (wingTicks==0 || grounded)){flying=false;diving=true;}
    if(flying && requestedInput.pound){flying=false;diving=false;pound=Pound::Windup;poundTicks=0;wasPound=true;}
    if(slingTicks>0){--slingTicks;vy-=.5f;}
        const Input input = hurtTicks > 0 ? Input{} : requestedInput;
        if (invincibleTicks > 0) --invincibleTicks;
        if (hurtTicks > 0) --hurtTicks;
        // Basic dry-ground/no-powerup branches of UnpausedCharEngine. Units: pixels/tick.
        if (input.jump && !wasJump) jumpBuffer = 7;
        else jumpBuffer = std::max(0, jumpBuffer - 1);
        wasJump = input.jump;
        const bool startSpin=!flying && input.spin && !wasSpin && spinSpeed==0 && pound==Pound::None && !diving && !diveRecovery && !running && vy>-3;
        wasSpin=input.spin;
        if(startSpin) {
            spinSpeed=15;spinPhase=0;flipTicks=diveFlipTicks=jumpChain=chainWindow=0;
            if(!grounded)vy=vy*.5f-5;
        }
        if (!swimming && input.pound && !wasPound && !grounded && pound == Pound::None && !diving && !diveRecovery && spinSpeed==0) {
            pound = Pound::Windup;
            poundTicks = 0;
            running = false;
            flipTicks = jumpChain = chainWindow = jumpBuffer = 0;
        }
        wasPound = input.pound;
        const int direction = static_cast<int>(input.right) - static_cast<int>(input.left);
        const int previousFacing = facingLeft ? -1 : 1;
        if (diveFlipTicks > 0) --diveFlipTicks;
        const bool startDive = !swimming && !flying && input.dive && !wasDive && !grounded &&
                               pound == Pound::None && !diving && !diveRecovery && spinSpeed==0;
        wasDive = input.dive;
        if (startDive) {
            diving = true;
            diveTicks = 0;wingLaunch=false;
            running = false;
            flipTicks = jumpChain = chainWindow = jumpBuffer = diveFlipTicks = 0;
            vx += ((facingLeft ? -35.f : 35.f) - vx) / 5;
            vy += 3;
        }
        if (flipTicks > 0) --flipTicks;
        if (direction && pound == Pound::None && !diving && !diveRecovery && !diveFlipTicks && !flying) facingLeft = direction < 0;
        if(swimming && spinSpeed==0) {
            pound=Pound::None;diving=running=flying=false;diveRecovery=diveFlipTicks=flipTicks=0;
            vx+=direction*.15f;vy+=.08f;
            if(input.jump){if(swimPressed || !wasSwimming){vy-=input.dive?4:6;swimTicks=0;}else vy-=.2f;}
            if(input.dive)vy+=.3f;
            vx=friction(vx,.05f,1.05f);vy=friction(vy,.05f,1.1f);++swimTicks;
            fludd.water=std::min(10000.f,fludd.water+500);fludd.power=std::min(100.f,fludd.power+3);
        } else if (flying) {
            const float speed=std::max(.3f,std::hypot(vx,vy));
            const float turn=std::clamp(1.5f/speed,.001f,1.f);
            float rot=std::atan2(vy,facingLeft?-vx:vx)+1.57079632679f;
            if(input.jump)rot+=std::max(-rot*.08f,-turn);
            if(input.dive)rot+=std::min((3.14159265359f-rot)*.08f,turn);
            vx=(facingLeft?-1.f:1.f)*std::cos(rot-1.57079632679f)*speed;
            vy=std::sin(rot-1.57079632679f)*speed;
            if((rot<3.14159265359f/7 || rot>3.14159265359f*.9f) &&
               ((input.left && !facingLeft)||(input.right && facingLeft))) {vx*=-.9f;facingLeft=!facingLeft;}
            vy=(vy+.2f)*.999f;vx*=.999f;flightAngle=-rot;
        } else if (pound == Pound::Windup) {
            // The source cancels gravity during its 12-frame windup.
            vx = vy = 0;
            if (poundTicks++ >= 12) { pound = Pound::Falling; poundTicks = 0; vy = 8; }
        } else if (pound == Pound::Falling) {
            ++poundTicks;
            vy += 0.5f;
            vx *= 0.95f;
            if (direction > 0) vx -= std::min((vx - level::airSpeed) / level::airDivisor, 0.0f);
            if (direction < 0) vx -= std::max((vx + level::airSpeed) / level::airDivisor, 0.0f);
        } else if (pound == Pound::Landing) {
            vx *= 0.8f;
            vy = grounded ? 0 : vy + 1;
            if (++poundTicks >= 15) { pound = Pound::None; poundTicks = 0; }
        } else if (diving || diveRecovery) {
            if (grounded) {
                vx = friction(vx, onIce?.04f:.2f, onIce?1.007f:1.05f);
                vy = 0;
            } else if (!startDive) {
                vy += 1;
                if (vy > 0) vy = friction(vy, 0.2f, 1.05f);
                vy = friction(vy, 0, 1.001f);
                vx = friction(vx, 0, 1.001f);
            }
            if (diveRecovery) {
                --diveRecovery;
            } else {
                ++diveTicks;
                if(wingTicks>0 && diveTicks<=4 && !grounded && vy<0 && std::abs(vx)+std::abs(vy)>7)wingLaunch=true;
                if(wingTicks>0 && wingLaunch && diveTicks>20 && !grounded){flying=true;diving=false;wingLaunch=false;}
                // Dive timeline steering is one eighth of normal air steering.
                if (direction > 0) vx -= std::min((vx - level::airSpeed) / (level::airDivisor / 3), 0.f) / 8;
                if (direction < 0) vx -= std::max((vx + level::airSpeed) / (level::airDivisor / 3), 0.f) / 8;
                if (grounded && input.jump && jumpBuffer && diveTicks > 2) {
                    const bool reverse = direction && direction != previousFacing;
                    if (reverse && std::abs(vx) < 5) {
                        vx *= 0.75f;
                        vx += (-previousFacing * 30.f - vx) / 5;
                        vy = -level::jumpSpeed - 2;
                        backflip = true;
                        diveFlipTicks = 22;
                    } else if (!reverse && std::abs(vx) > 3) {
                        vx += (previousFacing * 15.f - vx) / 5;
                        vy = -level::jumpSpeed / 1.5f;
                        backflip = false;
                        diveFlipTicks = 9;
                    } else if (std::abs(vx) < 3) {
                        diving = false;
                        diveRecovery = 7;
                    }
                    if (vy < 0) { diving = false; grounded = false; }
                    jumpBuffer = 0;
                }
                if (grounded && std::abs(vx) < 1 && !input.dive) {
                    diving = false;
                    diveRecovery = 7;
                }
            }
        } else if (spinSpeed>0) {
            vx=friction(vx,grounded ? .3f : 0.f,grounded ? 1.15f : 1.001f);
            vy=grounded ? 0 : vy+(swimming ? 1.f/3 : 1);
            if(!grounded && vy>0)vy=friction(vy,.2f,1.05f);
            spinPhase=std::fmod(spinPhase+std::clamp(std::floor(spinSpeed+.5f),5.f,10.f),40.f);
            if(input.dive && !grounded && !swimming) {
                spinSpeed=std::max(10.f,spinSpeed+1);
                vy+=std::sqrt((spinSpeed+1)/50)/5;
            }
            spinSpeed=spinSpeed>7 ? friction(spinSpeed,.5f,1.1f) : friction(spinSpeed,.1f,1.03f);
            if(!grounded && !input.dive) {
                vy=spinSpeed>7 ? friction(vy,.3f,1.05f) : friction(vy,.1f,1.03f);
                vy-=std::sqrt((spinSpeed+1)/30)/3;
            }
            if(direction>0)vx-=std::min((vx-level::airSpeed)/(level::airDivisor/3),0.f)/1.5f;
            if(direction<0)vx-=std::max((vx+level::airSpeed)/(level::airDivisor/3),0.f)/1.5f;
            if(swimming) {
                spinSpeed=friction(spinSpeed,.3f,1.1f);
                vy-=std::sqrt((spinSpeed+1)/20)/2;
                vx=friction(vx,.05f,1.05f);vy=friction(vy,.05f,1.1f);
            }
            if(spinSpeed<=(grounded ? 5 : 3))spinSpeed=0;
        } else if (grounded) {
            if (chainWindow > 0) --chainWindow;
            if (!chainWindow) jumpChain = 0;
            if (tapWindow > 0) --tapWindow;
            if (direction && direction != previousDirection) {
                if (direction == tapDirection && tapWindow > 0) {
                    running = true;
                    runTicks = 0;
                    runPhase = 1;
                }
                tapDirection = direction;
                tapWindow = 9;
            }
            if (running && (direction == 0 || direction != previousFacing || input.jump)) running = false;
            vx = friction(vx, onIce?.05f:.3f, onIce?1.01f:1.15f);
            if (!running) vx += direction * level::walkAcceleration/(onIce?5.f:1.f);
            else if (runTicks >= 6) vx += direction * level::walkAcceleration * 1.5f;
            if (running) ++runTicks;
            vy = 0;
            if (input.jump && jumpBuffer > 0) {
                const bool triple = jumpChain == 2 && std::abs(vx) > 3;
                if (jumpChain == 2 && !triple) jumpChain = 0;
                vy = -level::jumpSpeed - 2.5f * jumpChain;
                if (triple) {
                    vx += ((facingLeft ? -15.0f : 15.0f) - vx) / 5;
                    flipTicks = 30;
                    jumpChain = 0;
                    chainWindow = 0;
                } else {
                    ++jumpChain;
                    chainWindow = 9;
                }
                grounded = false;
                jumpBuffer = 0;
            }
        } else {
            tapWindow = 0;
            tapDirection = 0;
            if (running && ++runTicks > 16) running = false;
            vy += 1; // Default course gravity in StartChar.
            if (vy > 0) vy = friction(vy, 0.2f, 1.05f);
            vy = friction(vy, 0, 1.001f);
            vx = friction(vx, 0, 1.001f);
            if (direction > 0) vx -= std::min((vx - level::airSpeed) / (level::airDivisor / 3), 0.0f);
            if (direction < 0) vx -= std::max((vx + level::airSpeed) / (level::airDivisor / 3), 0.0f);
        }
        // UnpausedCharEngine's surface jump uses feet probes, not a fresh key press.
        if(surfaceJump) {
            vy=std::min(vy,surfaceJumpSpeed);swimTicks=0;
        }
        previousDirection = direction;
        fludd.apply(input.fludd,!flying && hurtTicks==0 && pound==Pound::None && spinSpeed==0,grounded,facingLeft,diving,vx,vy);
        if(fludd.spraying && vy<0)grounded=false;

        // Native solver: subpixel sweeps prevent crossing thin walls at high speed.
        // This is an approximation of Flash's probes, not the complete original solver.
        wallSide = 0;
        const bool followSlope = grounded;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(vx), std::abs(vy)) * 2)));
        for (int i = 0; i < steps; ++i) {
            const float nx = x + vx / steps;
            float support = 0, raisedTop = 0;
            const bool onOneWay = grounded && terrain.oneWayLanding(x, y - 0.05f, y + 0.05f, support);
            if (onOneWay && terrain.oneWayLanding(nx, y - 4, y, raisedTop) && !terrain.body(nx, raisedTop)) {
                x = nx;
                y = raisedTop;
            } else if (!terrain.body(nx, y)) x = nx;
            else {
                bool stepped = false;
                if (grounded) {
                    for (int lift = 1; lift <= 4; ++lift) {
                        if (!terrain.body(nx, y - lift)) { x = nx; y -= lift; stepped = true; break; }
                    }
                }
                if (!stepped) {
                    float clear = x, blocked = nx;
                    for (int j = 0; j < 8; ++j) {
                        const float mid = (clear + blocked) / 2;
                        if (terrain.body(mid, y)) blocked = mid; else clear = mid;
                    }
                    x = clear;
                    wallSide = vx > 0 ? 1 : -1;
                    wallX = x; wallY = y; wallSpeed = vx;
                    // Ordinary wall rebound from the source; high-speed hurt2
                    // animation/stun is still pending. Mirror both sides equally.
                    const float rebound = -vx * 0.5f;
                    vx = direction == wallSide ? friction(rebound, 3, 1) : rebound;
                    running = false;
                }
            }
            const float ny = y + vy / steps;
            float platformTop = 0;
            if (vy >= 0 && terrain.oneWayLanding(x, y, ny, platformTop) && !terrain.body(x, platformTop)) {
                y = platformTop;
                vy = 0;
                grounded = true;
            } else if (!terrain.body(x, ny)) y = ny;
            else if (vy != 0) {
                // Resolve to within 1/256 pixel of contact rather than leaving a substep gap.
                float clear = y, blocked = ny;
                for (int j = 0; j < 8; ++j) {
                    const float mid = (clear + blocked) / 2;
                    if (terrain.body(x, mid)) blocked = mid; else clear = mid;
                }
                y = clear;
                if (vy > 0) grounded = true;
                vy = 0;
            }
        }
        float top = 0;
        grounded = vy >= 0 && (terrain.body(x, y + 0.05f) || terrain.oneWayLanding(x, y, y + 0.05f, top));
        if (followSlope && !grounded && vy >= 0) {
            for (int drop = 1; drop <= 6; ++drop) {
                if (terrain.oneWayLanding(x, y, y + drop, top)) { y = top; grounded = true; break; }
                if (terrain.body(x, y + drop)) {
                    float clear = y, blocked = y + drop;
                    for (int j = 0; j < 12; ++j) {
                        const float mid = (clear + blocked) / 2;
                        if (terrain.body(x, mid)) blocked = mid; else clear = mid;
                    }
                    y = clear;
                    grounded = true;
                    break;
                }
            }
        }
        if (grounded) {flipTicks = diveFlipTicks = 0;flying=wingLaunch=false;}
        if (grounded && pound == Pound::Falling) { pound = Pound::Landing; poundTicks = 0; }
        if (grounded && std::abs(vx) >= 0.1f) {
            walkPhase += vx;
            runPhase += std::floor(vx * 0.7f + 0.5f) + (facingLeft ? -1.5f : 1.5f);
            if (walkPhase >= 80) walkPhase = 1;
            if (walkPhase <= 0) walkPhase = 80;
            if (runPhase >= 80) runPhase = 1;
            if (runPhase <= 0) runPhase = 80;
        }
        if (y > terrain.minY + terrain.rows + 160 || x < terrain.minX - 80 || x > terrain.minX + terrain.width + 80)
            { const auto equipment=fludd;reset();fludd=equipment;fludd.water=10000;fludd.power=100;fludd.charge=fludd.blast=0;respawned=true; }
    }
} // namespace sm63
