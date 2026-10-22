#include "audio.hpp"
#include <cmath>
#include <iostream>
int main(int argc,char** argv){
    try {
        if(argc!=2)throw std::runtime_error("Audio directory required");
        sm63::Audio a(argv[1],false),b(argv[1],false);
        float plain[2048],mixed[2048];
        a.play("coin");a.play("box");a.mix(mixed,2048);b.mix(plain,2048);
        bool different=false;
        for(int i=0;i<2048;++i){different|=mixed[i]!=plain[i];if(!std::isfinite(mixed[i]) || std::abs(mixed[i])>1)throw std::runtime_error("Invalid audio sample");}
        if(!different)throw std::runtime_error("Effects did not mix over music");
        a.restart();b.restart();a.pause(true);a.mix(mixed,2048);
        for(float sample:mixed)if(sample!=0)throw std::runtime_error("Paused audio not silent");
        a.pause(false);a.mix(mixed,2048);b.mix(plain,2048);
        for(int i=0;i<2048;++i)if(mixed[i]!=plain[i])throw std::runtime_error("Pause advanced music");
        // Exercise intro -> repeat and repeat -> repeat with real decoded assets.
        for(int i=0;i<8000;++i)a.mix(mixed,2048);
        if(a.loops<3)throw std::runtime_error("Music failed to repeat");
        a.restart();if(a.loops<3)throw std::runtime_error("Loop test lost diagnostics");
        a.enterArea(0);a.mix(mixed,2048);const auto position=a.musicOffset();
        a.enterArea(1);a.enterArea(2);a.enterArea(3);
        if(a.musicOffset()!=position || a.currentMusic()!="intro")throw std::runtime_error("Outdoor exits restarted music");
        a.enterArea(4);
        if(a.currentMusic()!="secret_intro" || a.musicOffset()!=0)throw std::runtime_error("Secret entrance did not start its intro");
        for(int i=0;i<1000 && a.currentMusic()=="secret_intro";++i)a.mix(mixed,2048);
        if(a.currentMusic()!="secret_loop")throw std::runtime_error("Secret intro did not reach its loop");
        auto secretOffset=a.musicOffset();a.enterArea(4);
        if(a.musicOffset()!=secretOffset)throw std::runtime_error("Same-area entry reset secret music");
        a.enterArea(3);
        if(a.currentMusic()!="loop" || a.musicOffset()!=0)throw std::runtime_error("Secret exit should start outdoor repeat, not intro");
        a.pause(true);a.enterArea(4);a.mix(mixed,2048);
        if(a.musicOffset()!=0)throw std::runtime_error("Music switch ignored pause");
        a.pause(false);a.clearEffects();a.holdMusic(true);
        const auto held=a.musicOffset();a.mix(mixed,2048);
        for(float sample:mixed)if(sample!=0)throw std::runtime_error("Held background should be silent");
        a.play("get_star");a.mix(mixed,2048);
        if(a.musicOffset()!=held || a.durationTicks("get_star")<71)throw std::runtime_error("Fanfare hold/timing");
        a.clearEffects();a.holdMusic(false);a.mix(mixed,2048);
        if(a.musicOffset()==held)throw std::runtime_error("Music did not resume after celebration");
        a.enterArea(5);
        if(a.currentMusic()!="snow_intro")throw std::runtime_error("Snowman map must select source music");
        for(int i=0;i<1000 && a.currentMusic()=="snow_intro";++i)a.mix(mixed,2048);
        if(a.currentMusic()!="snow_loop")throw std::runtime_error("Snowman intro must reach repeat");
        const auto snowOffset=a.musicOffset();a.enterArea(5);
        if(a.musicOffset()!=snowOffset)throw std::runtime_error("Snowman music must not restart on same-area selection");
        a.enterArea(0);
        if(a.currentMusic()!="intro")throw std::runtime_error("Switching back must restore Battlefield music");
        a.enterArea(6);
        if(a.currentMusic()!="tutorial_intro")throw std::runtime_error("Opening stage uses castle theme intro");
        a.enterArea(7);
        if(a.currentMusic()!="tutorial_intro")throw std::runtime_error("Opening area transition preserves music");
        std::cout<<"Audio assets, overlapping effects, pause and repeated music passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
