#include "physics.hpp"
#include "sprite_pose.hpp"
#include <iostream>
#include <stdexcept>

void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}

int main() {
    try {
        sm63::Terrain terrain;
        sm63::Player player;
        require(std::abs(sm63::Player::friction(-10, 0.3f, 1.15f) + 9.7f / 1.15f) < 0.0001f,
                "Friction must preserve negative velocity");
        auto step = [&](sm63::Input input, int count) {
            for (int i = 0; i < count; ++i) {
                player.tick(terrain, input);
                if (terrain.body(player.x, player.y)) {
                    throw std::runtime_error("Player embedded at " + std::to_string(player.x) + "," + std::to_string(player.y));
                }
            }
        };
        step({}, 90);
        require(player.grounded && player.y > 185 && player.y < 195, "Spawn must land on original 1-1 floor");
        const float floor = player.y;
        step({false, false, true}, 1);
        require(!player.grounded && player.vy < 0, "Jump must leave the floor");
        float apex = player.y;
        for (int i = 0; i < 80; ++i) { step({false, false, true}, 1); apex = std::min(apex, player.y); }
        require(floor - apex > 40 && floor - apex < 65, "Basic jump must use original impulse/gravity scale");
        require(player.grounded, "Holding jump must not auto-jump after landing");
        step({false, true, false}, 32);
        require(player.x > 130 && player.x < 210, "Ground movement distance changed unexpectedly");
        step({}, 40);
        require(std::abs(player.vx) < 0.001f, "Ground friction must stop player");

        player.reset(); step({}, 90);
        step({false, true, false}, 4);
        step({}, 2);
        step({false, true, false}, 20);
        require(player.running && player.vx > 8, "Double tap must enter faster running state");
        step({true, false, false}, 1);
        require(!player.running && player.facingLeft, "Reversing direction must cancel running");
        player.reset(); step({}, 90);
        step({false, true, false}, 2); step({}, 12); step({false, true, false}, 1);
        require(!player.running, "Expired double-tap window must not start running");

        player.reset(); step({}, 90);
        step({false, false, true}, 1);
        require(player.jumpChain == 1 && player.vy == -10, "First chained jump impulse");
        for (int i = 0; i < 100 && !player.grounded; ++i) step({}, 1);
        require(player.grounded, "First chain jump must land");
        step({false, false, true}, 1);
        require(player.jumpChain == 2 && player.vy == -12.5f, "Second chained jump impulse");
        for (int i = 0; i < 100 && !player.grounded; ++i) step({}, 1);
        require(player.grounded, "Second chain jump must land");
        // Enough grounded acceleration within the nine-tick chain window.
        step({false, true, false}, 5);
        step({false, true, true}, 1);
        require(player.flipTicks == 30 && player.vy == -15, "Moving third jump must frontflip with stronger impulse");
        step({}, 1);
        require(player.flipTicks == 29, "Frontflip animation timer must advance");

        player.reset(); step({}, 90); step({false, false, true}, 1);
        for (int i = 0; i < 100 && !player.grounded; ++i) step({}, 1);
        step({}, 12); step({false, false, true}, 1);
        require(player.vy == -10 && player.jumpChain == 1, "Waiting on ground must reset jump chain");

        player.reset(); step({}, 90);
        step({false, false, false, true}, 1);
        require(player.pound == sm63::Pound::None, "Ground pound cannot start on ground");
        step({}, 1); step({false, false, true}, 1); step({}, 5);
        const float windupY = player.y;
        step({false, false, false, true}, 12);
        require(player.pound == sm63::Pound::Windup && player.y == windupY, "Pound windup must suspend for twelve ticks");
        step({false, false, false, true}, 1);
        require(player.pound == sm63::Pound::Falling && player.vy == 8, "Pound descent must begin at source speed eight");
        for (int i = 0; i < 80 && !player.grounded; ++i) step({false, false, false, true}, 1);
        require(player.pound == sm63::Pound::Landing && player.grounded, "Pound must enter landing recovery");
        step({false, false, false, true}, 15);
        require(player.pound == sm63::Pound::None, "Pound must recover without retriggering held Z");

        player.reset(); player.x = 700; player.y = -100;
        step({false, false, false, true}, 13);
        for (int i = 0; i < 80 && !player.grounded; ++i) step({}, 1);
        require(player.grounded && std::abs(player.y + 18) < 0.1f,
                "Ground pound must not tunnel through original one-way platform");

        player.reset(); step({}, 90);
        step({false, false, false, false, true}, 1);
        require(!player.diving, "Dive cannot start on the ground");
        step({}, 1); step({false, false, true}, 1); step({}, 4);
        const float beforeDive = player.vx, beforeDiveY = player.vy;
        step({false, false, false, false, true}, 1);
        require(player.diving && std::abs(player.vx - (beforeDive + (35-beforeDive)/5)) < 0.001f &&
                std::abs(player.vy - (beforeDiveY + 3)) < 0.001f, "Dive must use source directional boost");
        step({true, false, false, false, true}, 1);
        require(!player.facingLeft && player.vx > 6, "Dive steering must be weak and preserve facing");
        for (int i = 0; i < 100 && !player.grounded; ++i) step({}, 1);
        require(player.grounded && player.diving, "Dive must transition into a belly slide");
        step({false, false, true}, 1);
        require(!player.diving && player.diveFlipTicks == 9 && player.vy < -6,
                "Jumping from a moving slide must diveflip");
        step({}, 100);

        player.reset(); step({}, 90);
        player.diving = true; player.diveTicks = 5; player.vx = 2;
        step({true, false, true}, 1);
        require(player.backflip && player.diveFlipTicks == 22 && player.vx < 0 && player.vy == -12,
                "Reverse jump from slow slide must backflip");
        player.reset(); step({}, 90);
        player.diving = true; player.diveTicks = 5; player.vx = 0.5f;
        step({}, 1);
        require(!player.diving && player.diveRecovery == 7, "Slow slide must enter get-up recovery");
        step({}, 7);
        require(player.diveRecovery == 0, "Get-up recovery must end");
        step({false, false, true}, 1);
        require(player.vy == -10, "Dive exit must allow normal jumping again");

        // Both sides of the original enclosure: high-speed sweeps must rebound
        // symmetrically and remain clear, even with a dive's large velocity.
        for (int direction : {-1, 1}) {
            player.reset(); player.x = direction < 0 ? -180 : 2250; player.y = -200;
            player.vx = direction * 80;
            bool contacted = false;
            for (int i = 0; i < 20 && !contacted; ++i) {
                step({}, 1);
                contacted = player.wallSide == direction;
            }
            require(contacted && player.vx * direction < 0, "Fast wall impact must rebound away from wall");
        }

        player.reset();step({},90);
        step({false,false,false,false,false,true},1);
        require(player.spinSpeed>0,"X must start grounded spin");
        step({false,false,false,false,false,true},90);
        require(player.spinSpeed==0,"Spin must decay and held X must not retrigger");
        player.reset();player.y=-100;player.vy=4;
        step({false,false,false,false,false,true},1);
        require(player.spinSpeed>0 && player.vy<0,"Air spin must lift a falling Mario");
        player.reset();player.y=-100;player.vy=-10;
        step({false,false,false,false,false,true},1);
        require(player.spinSpeed==0,"Spin must not interrupt fast jump ascent");

        // Original floating one-way ledge spans x~600..900, surface y=-18.
        player.reset(); player.x = 700; player.y = 15; player.vy = -10;
        float highest = player.y;
        for (int i = 0; i < 100; ++i) { step({}, 1); highest = std::min(highest, player.y); }
        require(highest < -18, "Jump must pass upward through one-way ledge");
        require(player.grounded && std::abs(player.y + 18) < 0.1f, "Must land on one-way ledge from above");

        player.reset(); player.x = 1100; player.y = -80;
        step({}, 60);
        require(player.grounded, "Must settle on original one-way ramp approach");
        step({false, true, false}, 40);
        require(player.grounded && player.y < -35, "Walking uphill must follow a one-way slope");

        player.reset(); player.x = 1280; player.y = 120;
        step({}, 60);
        step({false, true, false}, 80);
        require(player.x < 1310, "Tall solid ledge must block horizontal movement");

        // Ceiling of the original collision enclosure is y~-348.
        player.reset(); player.x = 0; player.y = -300; player.vy = -80;
        require(terrain.at(0, -350) == 1, "Ceiling mask must exist");
        require(terrain.body(0, -320), "Body probe must detect ceiling");
        step({}, 1);
        require(player.y > -316 && player.vy == 0, "High-speed ascent must hit ceiling without tunneling");

        player.reset(); player.y = 1000;
        step({}, 1);
        require(player.x == 0 && player.y == 0, "Out-of-bounds player must respawn");
        sm63::Terrain sky;sky.load(-2000,-2000,4000,4000,std::vector<sm63::level::Span>{});
        player.reset();player.wingTicks=1440;player.vx=12;player.vy=-20;player.diving=true;
        for(int i=0;i<22;++i)player.tick(sky,{});
        require(player.flying,"Wing Cap converts a sustained rising dive to flight");
        auto climb=player,descend=player;sm63::Input up;up.jump=true;sm63::Input down;down.dive=true;
        for(int i=0;i<8;++i){climb.tick(sky,up);descend.tick(sky,down);}
        require(climb.vy<descend.vy,"Up pitches flight upward and Down pitches downward");
        player.wingTicks=1;player.tick(sky,{});
        require(!player.flying && player.wingTicks==0,"Wing power expires without permanent flight");
        sm63::Terrain iceGround;iceGround.load(0,0,200,200,std::vector<sm63::level::Span>{});
        iceGround.blocks.push_back({0,100,200,200});iceGround.ice.push_back({99,0,200,4});
        sm63::Player icy,normal;icy.x=normal.x=50;icy.y=normal.y=100;icy.grounded=normal.grounded=true;icy.vx=normal.vx=6;
        icy.tick(iceGround,{});iceGround.ice.clear();normal.tick(iceGround,{});
        require(icy.vx>normal.vx+.5f,"Ice retains more momentum than ordinary ground");
        sm63::Terrain pool;pool.load(0,0,400,600,std::vector<sm63::level::Span>{});pool.water.push_back({0,0,400,600});
        sm63::Player swimmer;swimmer.x=200;swimmer.y=200;sm63::Input stroke;stroke.jump=true;
        swimmer.tick(pool,stroke);require(swimmer.swimming && swimmer.vy<0,"Up swims upward inside imported water");
        for(int i=0;i<81;++i){swimmer.x=200;swimmer.y=200;swimmer.tick(pool,{});}
        require(swimmer.oxygen==7,"Underwater air drains at the source interval");
        swimmer.y=-10;for(int i=0;i<10;++i){swimmer.y=-10;swimmer.tick(pool,{});}
        require(swimmer.oxygen==8,"Air recovers above water");
        swimmer.reset();swimmer.x=200;swimmer.y=100;sm63::Input waterSpin;waterSpin.spin=true;
        swimmer.tick(pool,waterSpin);
        require(swimmer.swimming && swimmer.spinSpeed>0,"Underwater spin must survive the swimming controller");
        const float firstSpin=swimmer.spinSpeed;
        for(int i=0;i<6;++i)swimmer.tick(pool,{});
        require(swimmer.spinSpeed<firstSpin,"Water adds spin drag instead of infinite spinning");
        swimmer.reset();swimmer.x=200;swimmer.y=9;swimmer.vy=-1;swimmer.wasJump=true;
        swimmer.tick(pool,stroke);
        require(swimmer.vy<-7,"Holding Up at the surface applies the original water jump impulse");
        for(int i=0;i<8;++i)swimmer.tick(pool,stroke);
        require(swimmer.y<-20 && !swimmer.swimming,"Surface jump clears Mario's feet out of the pool");
        swimmer.reset();swimmer.x=200;swimmer.y=100;swimmer.wasJump=true;swimmer.swimming=true;
        swimmer.tick(pool,stroke);require(swimmer.vy>-3,"Deep water must not trigger a surface jump");
        const auto leftWing=sm63::bodyPoint(0,-29,1.57079632679f,true),rightWing=sm63::bodyPoint(0,-29,1.57079632679f,false);
        require(std::abs(leftWing[0]-12)<.001f && std::abs(rightWing[0]+12)<.001f && std::abs(leftWing[1]+17)<.001f,"Wing attachment orbits Mario's body pivot and mirrors with him");
        const auto pivot=sm63::bodyPoint(0,-17,2.f,false);require(std::abs(pivot[0])<.001f && std::abs(pivot[1]+17)<.001f,"Body pivot remains fixed while rotating");
        std::cout << "Spawn, jump, held input, slope, friction, one-way platform, wall, ceiling, respawn passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
