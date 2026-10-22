#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
namespace sm63 {
class Audio {
    struct Voice {const std::vector<float>* samples; std::size_t position=0; float gain=1;};
    std::map<std::string,std::vector<float>> clips;
    std::vector<Voice> voices;
    SDL_AudioStream* stream=nullptr;
    std::size_t musicPosition=0;
    bool intro=true,paused=false,muted=false,musicSelected=false,musicHeld=false;
    std::string musicIntro="intro",musicLoop="loop";
public:
    int loops=0;
    explicit Audio(const std::filesystem::path& root,bool openDevice=true) {
        for(const char* name:{"tutorial_intro","tutorial_loop","snow_intro","snow_loop","wing_intro","wing_loop","boss_intro","boss_loop","boss_hurt","boss_land","cannon_enter","cannon_rise","cannon_ready","cannon_shot","star_swish","get_star","mario_fall","star_appear","secret_intro","secret_loop","intro","loop","jump","double","triple","dive","hurt","coin","box","stomp","die","notice","step","impact","nozzle","switch","spray","nozzlebox","fuse","explosion"}) {
            SDL_AudioSpec spec{};Uint8* data=nullptr;Uint32 length=0;
            if(!SDL_LoadWAV((root/(std::string(name)+".wav")).string().c_str(),&spec,&data,&length))throw std::runtime_error(SDL_GetError());
            const bool valid=spec.freq==48000 && spec.channels==2 && spec.format==SDL_AUDIO_F32 && length>0 && length%8==0;
            if(!valid){SDL_free(data);throw std::runtime_error("Invalid generated audio format");}
            auto& clip=clips[name];clip.resize(length/sizeof(float));std::memcpy(clip.data(),data,length);SDL_free(data);
        }
        if(openDevice && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
            stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            if(stream)SDL_ResumeAudioStreamDevice(stream);
        }
        if(openDevice && !stream)SDL_Log("Audio unavailable: %s",SDL_GetError());
    }
    ~Audio(){if(stream)SDL_DestroyAudioStream(stream);}
    void play(const std::string& name,float gain=1) {
        if(voices.size()>=32)voices.erase(voices.begin());
        voices.push_back({&clips.at(name),0,gain});
    }
    void stop(const std::string& name){const auto* clip=&clips.at(name);voices.erase(std::remove_if(voices.begin(),voices.end(),[&](const Voice& voice){return voice.samples==clip;}),voices.end());}
    // 1-2/1-3 inherit the outdoor song; 1-4 returns directly to its repeat.
    void enterArea(int area) {
        const std::string nextLoop=area>=6?"tutorial_loop":area==5?"snow_loop":area==4?"secret_loop":"loop";
        if(musicSelected && nextLoop==musicLoop)return;
        musicSelected=true;musicLoop=nextLoop;musicIntro=area>=6?"tutorial_intro":area==5?"snow_intro":area==4?"secret_intro":"intro";
        intro=area==0 || area==4 || area>=5;musicPosition=0;
        if(stream)SDL_ClearAudioStream(stream);
    }
    void wingMusic(){if(musicLoop=="wing_loop")return;musicIntro="wing_intro";musicLoop="wing_loop";intro=true;musicPosition=0;musicHeld=false;if(stream)SDL_ClearAudioStream(stream);}
    void bossMusic(){musicIntro="boss_intro";musicLoop="boss_loop";intro=true;musicPosition=0;musicHeld=false;if(stream)SDL_ClearAudioStream(stream);}
    const std::string& currentMusic() const {return intro?musicIntro:musicLoop;}
    std::size_t musicOffset() const {return musicPosition;}
    int durationTicks(const std::string& name) const {return static_cast<int>((clips.at(name).size()*32+95999)/96000);}
    void holdMusic(bool value){musicHeld=value;if(stream)SDL_ClearAudioStream(stream);}
    void clearEffects(){voices.clear();if(stream)SDL_ClearAudioStream(stream);}
    void restart(){musicHeld=false;musicPosition=0;intro=true;voices.clear();if(stream)SDL_ClearAudioStream(stream);}
    void pause(bool value){paused=value;if(stream){if(value)SDL_PauseAudioStreamDevice(stream);else SDL_ResumeAudioStreamDevice(stream);}}
    void mute(){muted=!muted;if(stream)SDL_SetAudioStreamGain(stream,muted?0.f:1.f);}
    void mix(float* output,int samples) {
        std::fill(output,output+samples,0.f);if(paused)return;
        for(int i=0;i<samples;++i) {
            float sample=0;
            if(!musicHeld) {
            auto* music=&clips.at(currentMusic());
            if(musicPosition==music->size()){musicPosition=0;intro=false;++loops;music=&clips.at(musicLoop);}
            sample=(*music)[musicPosition++]*.35f;
            }
            for(auto& voice:voices)if(voice.position<voice.samples->size())sample+=(*voice.samples)[voice.position++]*voice.gain*.8f;
            output[i]=std::clamp(sample,-1.f,1.f);
        }
        voices.erase(std::remove_if(voices.begin(),voices.end(),[](const Voice& v){return v.position==v.samples->size();}),voices.end());
    }
    void update(){
        if(!stream || paused)return;
        while(SDL_GetAudioStreamQueued(stream)<16384) {
            float samples[2048];mix(samples,2048);
            if(!SDL_PutAudioStreamData(stream,samples,sizeof(samples))){SDL_Log("Audio queue: %s",SDL_GetError());break;}
        }
    }
};
}
