#include "enemy_art.hpp"
#include "sequence_art.hpp"
#include "sequences.hpp"
#include "signs.hpp"
#include "signs_data.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "assets.hpp"
#include "physics.hpp"
#include "enemies.hpp"
#include "items.hpp"
#include "audio.hpp"
#include "areas.hpp"
#include "sprite_pose.hpp"
#include "hud_data.hpp"
#include "mario.hpp"
#include "art.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>

namespace {
namespace assets = sm63::assets;
// Mario Main frame 72 (0.5,-13.25) composed with Mario Sign's bitmap (-7.5,-15.75).
constexpr sm63::mario::Sprite readingPose{"Char/BITS Mario/Overworld - Mario 1 - 3.png",0,1,1,0,0,1,-7,-29};
template <typename T, auto Destroy>
using Handle = std::unique_ptr<T, decltype(Destroy)>;
using Texture = Handle<SDL_Texture, SDL_DestroyTexture>;
using Surface = Handle<SDL_Surface, SDL_DestroySurface>;

void check(bool result) {
    if (!result) throw std::runtime_error(SDL_GetError());
}

struct Options {
    std::filesystem::path root = assets::defaultRoot;
    std::filesystem::path data = sm63::art::defaultRoot;
    std::string screenshot;
    int frameLimit = 0;
    bool smoke = false;
    bool replay = false;
    int area=6,readSign=0;
    std::string sequence;
    bool positioned=false;float startX=0,startY=0;
    bool explore=false;
    sm63::Nozzle nozzle=sm63::Nozzle::None;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value for " + arg);
            return argv[i];
        };
        if (arg == "--assets") options.root = value();
        else if (arg == "--data") options.data = value();
        else if (arg == "--screenshot") options.screenshot = value();
        else if (arg == "--frames") {
            const auto text = value();
            std::size_t consumed = 0;
            options.frameLimit = std::stoi(text, &consumed);
            if (consumed != text.size() || options.frameLimit <= 0)
                throw std::runtime_error("--frames must be a positive integer");
        } else if (arg == "--smoke-test") options.smoke = true;
        else if (arg == "--area") {
            const auto name=value();
            if(name=="tutorial" || name=="8-E1-1"){options.area=6;continue;}
            if(name=="8-E1-1-2"){options.area=7;continue;}
            if(name=="2-1"){options.area=5;continue;}
            if(name.size()!=3 || name.substr(0,2)!="1-" || name[2]<'1' || name[2]>'5')throw std::runtime_error("Area must be 1-1 through 1-5, 2-1, tutorial, 8-E1-1, or 8-E1-1-2");
            options.area=name.back()-'1';
        }
        else if (arg == "--nozzle") {
            const auto name=value();
            if(name=="hover")options.nozzle=sm63::Nozzle::Hover;
            else if(name=="rocket")options.nozzle=sm63::Nozzle::Rocket;
            else if(name=="turbo")options.nozzle=sm63::Nozzle::Turbo;
            else throw std::runtime_error("Nozzle must be hover, rocket or turbo");
        }
        else if (arg == "--preview-sequence") {
            options.sequence=value();
            if(options.sequence!="shine" && options.sequence!="death")throw std::runtime_error("Sequence must be shine or death");
        }
        else if (arg == "--position") {
            auto coordinates=value();auto comma=coordinates.find(',');
            if(comma==std::string::npos)throw std::runtime_error("Position must be X,Y");
            std::size_t nx=0,ny=0;options.startX=std::stof(coordinates.substr(0,comma),&nx);options.startY=std::stof(coordinates.substr(comma+1),&ny);
            if(nx!=comma || ny!=coordinates.size()-comma-1 || !std::isfinite(options.startX) || !std::isfinite(options.startY))throw std::runtime_error("Invalid position");
            options.positioned=true;
        }
        else if (arg == "--read-sign") {options.readSign=std::stoi(value());if(options.readSign<1)throw std::runtime_error("Sign numbers start at 1");}
        else if (arg == "--explore") options.explore = true;
        else if (arg == "--replay") options.replay = true;
        else throw std::runtime_error("Unknown argument: " + arg);
    }
    if (options.smoke) options.frameLimit = 192;
    if (options.replay && !options.frameLimit) options.frameLimit = 180;
    return options;
}

Texture loadTexture(SDL_Renderer* renderer, const std::filesystem::path& path) {
    Surface surface(SDL_LoadPNG(path.u8string().c_str()), SDL_DestroySurface);
    if (!surface) throw std::runtime_error("Cannot load " + path.string() + ": " + SDL_GetError());
    Texture texture(SDL_CreateTextureFromSurface(renderer, surface.get()), SDL_DestroyTexture);
    check(texture != nullptr);
    check(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST));
    check(SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND));
    return texture;
}

class Preview {
public:
    Preview(SDL_Renderer* renderer, const std::filesystem::path& root, const std::filesystem::path& data,int startArea=6)
        : renderer_(renderer), root_(root), data_(data), browser_(nullptr, SDL_DestroyTexture),
          groundTexture_(nullptr, SDL_DestroyTexture) {
        audio_=std::make_unique<sm63::Audio>(data / "audio");
        secretBackground_=loadTexture(renderer,data/"secret-background.png");
        snowBackground_=loadTexture(renderer,data/"snow-background.png");
        tutorialBackground_=loadTexture(renderer,data/"tutorial-background.png");
        backArt_ = loadTexture(renderer, data / "back.png");
        frontArt_ = loadTexture(renderer, data / "front.png");
        for (const auto& frame : assets::animation)
            animation_.push_back(loadTexture(renderer, root / frame.path));
        auto loadTrack = [&](const auto& track) {
            for (const auto& sprite : track)
                if (!marioTextures_.count(sprite.path))
                    marioTextures_.emplace(sprite.path, loadTexture(renderer, root / sprite.path));
        };
        marioTextures_.emplace(readingPose.path,loadTexture(renderer_,root_/readingPose.path));
        loadTrack(sm63::mario::swim);loadTrack(sm63::mario::flying);loadTrack(sm63::mario::spin); loadTrack(sm63::mario::walk); loadTrack(sm63::mario::run);
        loadTrack(sm63::mario::rise); loadTrack(sm63::mario::fall); loadTrack(sm63::mario::flip);
        loadTrack(sm63::mario::pound); loadTrack(sm63::mario::dive);
        loadTrack(sm63::mario::hover_dive);loadTrack(sm63::mario::rocket_dive);loadTrack(sm63::mario::turbo_dive);
        loadTrack(sm63::mario::hover_diveflip);loadTrack(sm63::mario::rocket_diveflip);loadTrack(sm63::mario::turbo_diveflip);
        loadTrack(sm63::mario::hover_backflip);loadTrack(sm63::mario::rocket_backflip);loadTrack(sm63::mario::turbo_backflip);
        for(const auto& track:sm63::sequenceart::tracks) {
            std::vector<Texture> frames;
            for(int i=0;i<track.length;++i)frames.push_back(loadTexture(renderer_,data_/"sequence-art"/(std::string(track.name)+"-"+std::to_string(i)+".png")));
            sequenceTextures_.emplace(track.name,std::move(frames));
        }
        loadTrack(sm63::mario::diveflip); loadTrack(sm63::mario::backflip);
        loadTrack(sm63::enemydata::walk); loadTrack(sm63::enemydata::squish);
        for(const auto& track:sm63::enemyart::tracks) {
            std::vector<Texture> frames;
            for(int i=0;i<track.length;++i)frames.push_back(loadTexture(renderer_,data_/"enemy-art"/(std::string(track.name)+"-"+std::to_string(i)+".png")));
            enemyTextures_.emplace(track.name,std::move(frames));
        }
        for(const auto& frame:sm63::platformdata::green)loadTrack(frame);
        for(const auto& frame:sm63::platformdata::log)loadTrack(frame);
        loadTrack(sm63::itemdata::blue);loadTrack(sm63::itemdata::gold); loadTrack(sm63::itemdata::red);
        marioTextures_.emplace(sm63::itemdata::box.path, loadTexture(renderer_, root_ / sm63::itemdata::box.path));
        for(const auto* group:{&sm63::huddata::pack})for(const char* path:*group)marioTextures_.emplace(path,loadTexture(renderer_,root_/path));
        auto loadIcons=[&](const auto& group){for(const char* path:group)marioTextures_.emplace(path,loadTexture(renderer_,root_/path));};
        loadIcons(sm63::huddata::hp);loadIcons(sm63::huddata::boxes);loadIcons(sm63::huddata::nozzles);loadIcons(sm63::huddata::icons);
        for(const auto& art:sm63::objectdata::art)objectTextures_.emplace(art.name,loadTexture(renderer_,data_/"object-art"/(std::string(art.name)+".png")));
        for(const auto& track:sm63::objectdata::bossTracks)for(int i=0;i<track.length;++i) {
            const std::string key="boss_"+std::string(track.name)+"_"+std::to_string(i);
            objectTextures_.emplace(key,loadTexture(renderer_,data_/"object-art"/(key+".png")));
        }
        items_.reset(terrain_);
        selectAsset(0);
        signTexture_=loadTexture(renderer_,data_/"signs/sign.png");
        signPrompt_=loadTexture(renderer_,data_/"signs/prompt.png");
        for(std::size_t i=0;i<sm63::signdata::cards.size();++i) {
            std::vector<Texture> frames;
            for(int f=0;f<sm63::signdata::cards[i].frames;++f)
                frames.push_back(loadTexture(renderer_,data_/"signs"/(std::to_string(i)+"-"+std::to_string(f)+".png")));
            signCards_.push_back(std::move(frames));
        }
        changeArea({startArea,0,0});
    }

    void previewSequence(const std::string& name) {transitionTicks_=0;beginSequence(name=="shine"?sm63::Sequence::Kind::Shine:sm63::Sequence::Kind::Death);}
    void equip(sm63::Nozzle nozzle){player_.fludd.equip(nozzle);}
    void changeArea(sm63::Destination destination) {
        starCoinNotice_=0;sequence_=sm63::Sequence{};audio_->holdMusic(false);
        promptSign_=nullptr;signPromptAlpha_=0;
        areas_.enter(destination,terrain_,player_,items_,enemies_);
        if(player_.wingTicks>0)audio_->wingMusic();else audio_->enterArea(areas_.current);
        previousX_=player_.x;previousY_=player_.y;transitionTicks_=16;exitCooldown_=24;
        const auto path=areas_.current==0 ? data_ : data_/("area"+std::to_string(areas_.current));
        backArt_=loadTexture(renderer_,path/"back.png");frontArt_=loadTexture(renderer_,path/"front.png");
        Surface surface(SDL_CreateSurface(terrain_.width, terrain_.rows, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        check(surface != nullptr);
        check(SDL_ClearSurface(surface.get(), 0, 0, 0, 0));
        const auto* format = SDL_GetPixelFormatDetails(surface->format);
        for (const auto& span : sm63::world::areas[areas_.current].spans) {
            const SDL_Rect rect{span.x0 - terrain_.minX, span.y - terrain_.minY, span.x1 - span.x0, 1};
            const Uint32 color = span.flag == 1 ? SDL_MapRGBA(format, nullptr, 59, 119, 86, 255)
                                               : SDL_MapRGBA(format, nullptr, 178, 132, 56, 255);
            check(SDL_FillSurfaceRect(surface.get(), &rect, color));
        }
        groundTexture_.reset(SDL_CreateTextureFromSurface(renderer_, surface.get()));
        check(groundTexture_ != nullptr);
        check(SDL_SetTextureScaleMode(groundTexture_.get(), SDL_SCALEMODE_NEAREST));
        check(SDL_SetTextureBlendMode(groundTexture_.get(), SDL_BLENDMODE_BLEND));
    }

    void event(const SDL_Event& event) {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) left_ = right_ = up_ = space_ = pound_ = dive_ = spin_ = false;
        if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) return;
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (event.key.scancode == SDL_SCANCODE_LEFT) left_ = down;
        if (event.key.scancode == SDL_SCANCODE_RIGHT) right_ = down;
        if (event.key.scancode == SDL_SCANCODE_UP) up_ = down;
        if (event.key.scancode == SDL_SCANCODE_SPACE) space_ = down;
        if (event.key.scancode == SDL_SCANCODE_DOWN) dive_ = down;
        if (event.key.scancode == SDL_SCANCODE_X) spin_ = down;
        if (event.key.scancode == SDL_SCANCODE_Z) pound_ = down;
        if (!down || event.key.repeat) return;
        if((sequence_.active() || areas_.objects.active() || starCoinNotice_) && event.key.scancode!=SDL_SCANCODE_R && event.key.scancode!=SDL_SCANCODE_P && event.key.scancode!=SDL_SCANCODE_M)return;
        if(gameOver_ && event.key.scancode!=SDL_SCANCODE_R)return;
        if(sign_.active() && event.key.scancode!=SDL_SCANCODE_R && event.key.scancode!=SDL_SCANCODE_P && event.key.scancode!=SDL_SCANCODE_M && event.key.scancode!=SDL_SCANCODE_PAGEUP && event.key.scancode!=SDL_SCANCODE_PAGEDOWN)return;
        switch (event.key.scancode) {
            case SDL_SCANCODE_TAB:
                if (game_) { game_ = false; browsing_ = false; }
                else if (!browsing_) browsing_ = true;
                else { game_ = true; browsing_ = false; }
                left_ = right_ = up_ = space_ = pound_ = dive_ = spin_ = false;
                break;
            case SDL_SCANCODE_SPACE: if (!game_) paused_ = !paused_; break;
            case SDL_SCANCODE_LSHIFT:
            case SDL_SCANCODE_RSHIFT: player_.fludd.cycle();audio_->stop("spray");audio_->play("switch");break;
            case SDL_SCANCODE_M: audio_->mute(); break;
            case SDL_SCANCODE_P: paused_ = !paused_; break;
            case SDL_SCANCODE_F2:
                exploring_=!exploring_;player_.vx=player_.vy=0;
                if(!exploring_ && terrain_.body(player_.x,player_.y)){player_.x=areas_.spawnX;player_.y=areas_.spawnY;}
                previousX_=player_.x;previousY_=player_.y;
                break;
            case SDL_SCANCODE_PAGEUP:
            case SDL_SCANCODE_PAGEDOWN:
                signMessage_.clear();sign_=sm63::SignSession{};
                if(areas_.current>=6){changeArea({areas_.current==6?7:6,0,0});break;}
                if(areas_.current==5)break;
                changeArea({(areas_.current+(event.key.scancode==SDL_SCANCODE_PAGEDOWN ? 1 : 4))%5,0,0});
                break;
            case SDL_SCANCODE_F1: debug_ = !debug_; break;
            case SDL_SCANCODE_RIGHTBRACKET: selectAsset(1); break;
            case SDL_SCANCODE_LEFTBRACKET: selectAsset(-1); break;
            case SDL_SCANCODE_F3:
                restartCourse(areas_.current>=6?0:areas_.current==5?6:5);break;
            case SDL_SCANCODE_R:
                restartCourse(areas_.current>=6?6:areas_.current==5?5:0);
                break;
            default: break;
        }
    }

    void restartCourse(int area) {
        signMessage_.clear();sign_=sm63::SignSession{};audio_->restart();lives_=4;nextLifeCoin_=100;gameOver_=false;
        tick_=0;x_=225;facingLeft_=false;areas_.clear();player_.reset();items_=sm63::Items{};
        left_=right_=up_=space_=pound_=dive_=spin_=false;
        changeArea({area,0,0});
    }
    void tick() {
        if (paused_ || !signMessage_.empty()) return;
        if(transitionTicks_>0)--transitionTicks_;
        if(starCoinNotice_) {
            ++noticeAge_;if(!pound_)noticeReleased_=true;
            if(noticeAge_>6 && noticeReleased_ && pound_){starCoinNotice_=0;blockPound_=true;}
            return;
        }
        if(sequence_.active()) {
            previousX_=player_.x;previousY_=player_.y;
            const bool wasLanded=sequence_.landed;
            const bool done=sequence_.tick(player_,terrain_,audio_->durationTicks("get_star"));
            if(!wasLanded && sequence_.landed){audio_->stop("star_swish");audio_->play("get_star",.45f);}
            if(done) {
                const bool death=sequence_.dying();sequence_=sm63::Sequence{};
                audio_->clearEffects();audio_->holdMusic(false);audio_->enterArea(areas_.current);
                if(death) {
                    loseLife();const auto equipment=player_.fludd;player_.reset();player_.fludd=equipment;
                    player_.fludd.water=10000;player_.fludd.power=100;player_.fludd.charge=player_.fludd.blast=0;
                    player_.x=areas_.spawnX;player_.y=areas_.spawnY;transitionTicks_=16;
                }
                player_.invincibleTicks=64;exitCooldown_=24;
                previousX_=player_.x;previousY_=player_.y;
            }
            return;
        }
        const sm63::world::Object* nearby=nullptr;
        if(game_ && !exploring_ && !sign_.active())for(const auto& object:sm63::world::areas[areas_.current].objects)
            if((object.kind==4 || object.kind==20) && sm63::SignSession::canRead(player_,object.x,object.y,left_||right_)){nearby=&object;break;}
        if(nearby)promptSign_=nearby;
        signPromptAlpha_+=((nearby?1.f:0.f)-signPromptAlpha_)/3;
        if(sign_.active()) {
            previousX_=player_.x;previousY_=player_.y;sign_.tick(player_,pound_);if(areas_.current==6 && sign_.phase==sm63::SignSession::Phase::Closed && sign_.key=="@picture:controls")areas_.openTutorialGate(terrain_);blockPound_=true;return;
        }
        if(!pound_)blockPound_=false;
        if(game_ && !exploring_ && pound_ && !blockPound_) {
            for(const auto& object:sm63::world::areas[areas_.current].objects)
                if((object.kind==4 || object.kind==20) && sm63::SignSession::canRead(player_,object.x,object.y,left_||right_)) {
                    sign_.open(player_,object.kind==20?player_.x:object.x,object.text);audio_->stop("spray");return;
                }
        }
        if(exitCooldown_>0)--exitCooldown_;
        tick_ = (tick_ + 1) % assets::animationLength;
        if (game_ && exploring_) {
            previousX_=player_.x;previousY_=player_.y;
            player_.x=std::clamp(player_.x+12.f*(int(right_)-int(left_)),float(terrain_.minX),float(terrain_.minX+terrain_.width));
            player_.y=std::clamp(player_.y+12.f*(int(dive_)-int(up_)),float(terrain_.minY),float(terrain_.minY+terrain_.rows));
            return;
        }
        if (game_) {
            previousX_=player_.x;previousY_=player_.y;
            if(areas_.objects.boss.dialogue(player_,pound_)) {handleBossEvent();blockPound_=true;return;}
            if(areas_.objects.tick(player_,{left_,right_,up_,pound_,dive_,spin_,space_})) {
                audio_->stop("spray");if(areas_.objects.sound)audio_->play(areas_.objects.sound);
                return;
            }
            const auto oldPlayer=player_;
            const int oldCoins=items_.count,oldSilver=items_.silverCount;
            const auto oldBoxes=items_.boxes;
            if(coinSoundCooldown_>0)--coinSoundCooldown_;
            previousX_ = player_.x; previousY_ = player_.y;
            const float oldVX=player_.vx, oldVY=player_.vy;
            const auto oldPound=player_.pound;
            areas_.platforms.tick(terrain_,player_);
            player_.tick(terrain_, areas_.objects.boss.holding?sm63::Input{}:sm63::Input{left_, right_, up_, pound_ && !blockPound_, dive_, spin_, space_});
            if(player_.respawned) {
                player_=oldPlayer;beginSequence(sm63::Sequence::Kind::Fall);return;
            }
            areas_.objects.hitHazards(player_,previousX_,previousY_);
            items_.breakBoxes(terrain_,player_,oldVX,oldVY,oldPound);
            std::vector<bool> oldSeeing;
            for(const auto& enemy:enemies_.actors)oldSeeing.push_back(enemy.seeing);
            std::vector<sm63::EnemyState> oldStates;
            for(const auto& enemy:enemies_.actors)oldStates.push_back(enemy.state);
            enemies_.tick(terrain_, player_, previousX_, previousY_, up_);
            for(std::size_t i=0;i<enemies_.actors.size();++i) {
                auto& enemy=enemies_.actors[i];
                if(!enemy.rewarded && (enemy.state==sm63::EnemyState::Squished || enemy.state==sm63::EnemyState::Exploding)) {
                    items_.drop(enemy.x,enemy.y-12);enemy.rewarded=true;
                }
                if(enemy.state==sm63::EnemyState::Exploding && oldStates[i]!=enemy.state)audio_->play("explosion");
                const bool blast=enemy.state==sm63::EnemyState::Exploding && enemy.age<6;
                const bool shell=enemy.state==sm63::EnemyState::Shell && std::abs(enemy.vx)>2;
                if(blast || shell)items_.enemyImpact(terrain_,enemy.x,enemy.y,blast?48.f:24.f);
            }
            for(std::size_t i=0;i<enemies_.actors.size();++i) {
                const auto& enemy=enemies_.actors[i];
                if(enemy.seeing && !oldSeeing[i])audio_->play(enemy.kind==sm63::EnemyKind::Bobomb?"fuse":"notice",std::max(0.f,1.f-std::abs(enemy.x-player_.x)/600));
                if(enemy.state==sm63::EnemyState::Squished && oldStates[i]!=enemy.state)audio_->play("stomp");
                if(enemy.state==sm63::EnemyState::Gone && oldStates[i]==sm63::EnemyState::Squished)audio_->play("die");
            }
            areas_.objects.boss.tick(terrain_,player_,enemies_);handleBossEvent();
            if (player_.health > 0) items_.tick(terrain_,player_,previousX_,previousY_);
            if(items_.count>oldCoins && !coinSoundCooldown_){audio_->play("coin");coinSoundCooldown_=4;}
            if(items_.wingPicked)audio_->wingMusic();
            if(oldPlayer.wingTicks>0 && player_.wingTicks==0) {
                if(areas_.objects.boss.state==sm63::KingBoss::State::Fight)audio_->bossMusic();else audio_->enterArea(areas_.current);
            }
            if(items_.nozzleBroken)audio_->play("nozzlebox");
            if(items_.refilled)audio_->play(items_.silverCount>oldSilver?"coin":"nozzle");
            if(oldPlayer.fludd.spraying && !player_.fludd.spraying)audio_->stop("spray");
            if(player_.fludd.spraying && (!oldPlayer.fludd.spraying || fluddSoundTicks_--<=0)){audio_->play("spray",.4f);fluddSoundTicks_=48;}
            while(items_.count>=nextLifeCoin_){++lives_;nextLifeCoin_+=100;}
            for(std::size_t i=0;i<items_.boxes.size();++i)if(items_.boxes[i].broken && !oldBoxes[i].broken)audio_->play("box");
            if(player_.health<oldPlayer.health)audio_->play("hurt");
            if(player_.diving && !oldPlayer.diving)audio_->play("dive");
            if(oldPlayer.grounded && !player_.grounded && player_.vy<0 && player_.spinSpeed==0 && !player_.diving && !player_.fludd.spraying)
                audio_->play(player_.flipTicks>0 ? "triple" : player_.jumpChain==2 ? "double" : "jump");
            if(player_.pound==sm63::Pound::Landing && oldPlayer.pound!=player_.pound)audio_->play("impact");
            if(player_.grounded && std::abs(player_.vx)>1 && ++stepSoundTicks_>=8){audio_->play("step",.5f);stepSoundTicks_=0;}
            if (player_.health <= 0) {beginSequence(sm63::Sequence::Kind::Death);return;}
            if(items_.pickedStarCoin){starCoinNotice_=items_.pickedStarCoin;noticeAge_=0;noticeReleased_=!pound_;audio_->play("coin");return;}
            if(items_.shineAppeared)audio_->play("star_appear");
            if(items_.pickedShine) {beginSequence(sm63::Sequence::Kind::Shine);return;}
            if (std::abs(player_.x - previousX_) > 100 || std::abs(player_.y - previousY_) > 100) {
                previousX_ = player_.x; previousY_ = player_.y;
            }
            if(!exitCooldown_) {
                const int exit=sm63::exitAt(areas_.current,player_);
                if(exit) {
                    auto next=sm63::destination(areas_.current,exit,player_.x);
                    if(next.area>=0)changeArea(next);
                    else {signMessage_=areas_.current>=6?"The next tutorial and castle connection are not ported yet. Press E to close, or F3 to switch maps.":"The next Snowman's Land area is not ported yet. Press E to close, or F3 to switch maps.";exitCooldown_=60;player_.x=previousX_;player_.vx=0;}
                }
            }
            return;
        }
        if (!browsing_ && left_ != right_) {
            facingLeft_ = left_;
            // Preview positioning only; original player physics are not ported yet.
            x_ = std::clamp(x_ + (left_ ? -2.0f : 2.0f), 24.0f, 426.0f);
        }
    }

    void place(float x,float y){player_.x=previousX_=x;player_.y=previousY_=y;player_.vx=player_.vy=0;}
    void readSign(int number) {
        for(const auto& object:sm63::world::areas[areas_.current].objects)if(object.kind==4 && --number==0) {
            place(object.x,object.y);sign_.open(player_,object.x,object.text);return;
        }
        throw std::runtime_error("No such sign in selected area");
    }
    void replayInput(int frame) {
        left_ = false;
        right_ = (frame >= 40 && frame < 44) || (frame >= 46 && frame < 160) || (frame >= 200 && frame < 340);
        up_ = frame == 60;
        space_ = player_.fludd.nozzle!=sm63::Nozzle::None && frame>=80 && frame<125;
        pound_ = frame == 70;
        dive_ = frame == 145;
        spin_ = frame == 210;
        if (frame == 140 || frame == 158 || frame == 240) up_ = true;
    }

    void draw(float alpha = 1) {
        audio_->pause(paused_ || !game_ || !signMessage_.empty());audio_->update();
        if (game_) { drawGame(paused_ ? 1 : alpha); return; }
        check(SDL_SetRenderDrawColor(renderer_, 19, 25, 38, 255));
        check(SDL_RenderClear(renderer_));
        check(SDL_SetRenderDrawColor(renderer_, 27, 35, 49, 255));
        for (int y = 48; y < 264; y += 16) {
            for (int x = 0; x < assets::width; x += 16) {
                if ((x / 16 + y / 16) % 2 == 0) {
                    const SDL_FRect rect{static_cast<float>(x), static_cast<float>(y), 16, 16};
                    check(SDL_RenderFillRect(renderer_, &rect));
                }
            }
        }
        if (browsing_) {
            const float scale = std::min({4.0f, 410.0f / browser_->w, 190.0f / browser_->h});
            const float width = browser_->w * scale;
            const float height = browser_->h * scale;
            const SDL_FRect dest{(450 - width) / 2, 56 + (200 - height) / 2, width, height};
            check(SDL_RenderTexture(renderer_, browser_.get(), nullptr, &dest));
            text(12, 12, "SUPER MARIO 63 / ASSET BROWSER");
            text(12, 30, std::to_string(assetIndex_ + 1) + " / " + std::to_string(assets::catalog.size()) +
                            "  " + std::to_string(browser_->w) + " x " + std::to_string(browser_->h));
            std::string filename = std::filesystem::path(assets::catalog[assetIndex_]).filename().string();
            if (filename.size() > 53) filename = filename.substr(0, 50) + "...";
            text(12, 266, filename);
        } else {
            std::size_t index = 0;
            for (std::size_t i = 0; i < assets::animation.size(); ++i)
                if (tick_ >= assets::animation[i].start) index = i;
            const auto& frame = assets::animation[index];
            auto* texture = animation_[index].get();
            // Preserve the source symbol's registration point, including when mirrored.
            const float offset = facingLeft_ ? -frame.x - texture->w : frame.x;
            const SDL_FRect dest{x_ + offset, 210 + frame.y,
                                 static_cast<float>(texture->w), static_cast<float>(texture->h)};
            check(SDL_SetRenderDrawColor(renderer_, 78, 108, 139, 255));
            check(SDL_RenderLine(renderer_, 12, 210, 438, 210));
            check(SDL_RenderTextureRotated(renderer_, texture, nullptr, &dest, 0, nullptr,
                                          facingLeft_ ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE));
            text(12, 12, "SUPER MARIO 63 / NATIVE PORT");
            text(12, 30, "Original XFL animation / 32 Hz");
            text(12, 236, "Animation preview - gameplay not ported yet");
            text(12, 252, "Left/Right: move   Space: pause   R: reset");
            text(12, 266, paused_ ? "PAUSED" : "C++17 / SDL3");
        }
        text(12, 284, "Tab: view   [ / ]: asset   Esc: quit");
    }

private:
    void handleBossEvent() {
        switch(areas_.objects.boss.event) {
            case 1:audio_->bossMusic();break;
            case 2:audio_->play("boss_hurt");break;
            case 3:items_.bossDefeated=true;audio_->holdMusic(false);audio_->enterArea(areas_.current);audio_->play("star_appear");break;
            case 4:audio_->play("boss_land");break;
            case 5:audio_->holdMusic(true);break;
            case 6:audio_->play("explosion");break;
            default:break;
        }
        areas_.objects.boss.event=0;
    }
    void beginSequence(sm63::Sequence::Kind kind) {
        areas_.objects.boss.holding=false;
        sequence_.begin(kind,player_);audio_->clearEffects();audio_->holdMusic(true);
        audio_->play(kind==sm63::Sequence::Kind::Shine?"star_swish":"mario_fall",.65f);
        previousX_=player_.x;previousY_=player_.y;
    }
    void loseLife(){if(lives_>0)--lives_;if(lives_==0){gameOver_=true;signMessage_="Game over. Press R to start a new run.";}}
    void drawGame(float alpha) {
        const float renderX = previousX_ + (player_.x - previousX_) * alpha;
        const float renderY = previousY_ + (player_.y - previousY_) * alpha;
        const float cameraX = std::clamp((sequence_.dying()?sequence_.anchorX:renderX) - 225, static_cast<float>(terrain_.minX),
                                        static_cast<float>(terrain_.minX + terrain_.width - assets::width));
        const float cameraY = std::clamp((sequence_.dying()?sequence_.anchorY:renderY) - 190, static_cast<float>(terrain_.minY),
                                        static_cast<float>(terrain_.minY + terrain_.rows - assets::height + 65));
        check(SDL_SetRenderDrawColor(renderer_, 141, 201, 240, 255));
        check(SDL_RenderClear(renderer_));
        if(areas_.current>=6){SDL_FRect background{0,0,450,300};check(SDL_RenderTexture(renderer_,tutorialBackground_.get(),nullptr,&background));}
        if(areas_.current==5){SDL_FRect background{0,0,450,300};check(SDL_RenderTexture(renderer_,snowBackground_.get(),nullptr,&background));}
        if(areas_.current==4){SDL_FRect background{0,0,450,300};check(SDL_RenderTexture(renderer_,secretBackground_.get(),nullptr,&background));}
        check(SDL_SetRenderDrawColor(renderer_, 34, 51, 69, 255));
        for (int x = static_cast<int>(std::floor(cameraX / 32)) * 32; debug_ && x < cameraX + 450; x += 32)
            check(SDL_RenderLine(renderer_, x - cameraX, 0, x - cameraX, 300));
        for (int y = static_cast<int>(std::floor(cameraY / 32)) * 32; debug_ && y < cameraY + 300; y += 32)
            check(SDL_RenderLine(renderer_, 0, y - cameraY, 450, y - cameraY));
        const SDL_FRect ground{terrain_.minX - cameraX, terrain_.minY - cameraY,
                              static_cast<float>(terrain_.width), static_cast<float>(terrain_.rows)};
        const SDL_FRect artwork{(areas_.current ? terrain_.minX-32 : sm63::art::x)-cameraX,
                               (areas_.current ? terrain_.minY-32 : sm63::art::y)-cameraY,
                               static_cast<float>(backArt_->w),static_cast<float>(backArt_->h)};
        if (debug_) check(SDL_RenderTexture(renderer_, groundTexture_.get(), nullptr, &ground));
        else check(SDL_RenderTexture(renderer_, backArt_.get(), nullptr, &artwork));
        auto drawItem = [&](const auto& frame,float x,float y,float opacity=1.f,float angle=0.f) {
            auto* texture=marioTextures_.at(frame.path).get();
            std::array<SDL_Vertex,4> vertices{};
            const std::array<SDL_FPoint,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
            for(int i=0;i<4;++i) {
                const float u=uv[i].x*texture->w,v=uv[i].y*texture->h;
                const float px=frame.a*u+frame.c*v+frame.x,py=frame.b*u+frame.d*v+frame.y;
                vertices[i]={{x-cameraX+std::cos(angle)*px-std::sin(angle)*py,
                              y-cameraY+std::sin(angle)*px+std::cos(angle)*py},{1,1,1,opacity},uv[i]};
            }
            constexpr int indices[]={0,1,2,0,2,3};
            check(SDL_RenderGeometry(renderer_,texture,vertices.data(),4,indices,6));
        };
        auto drawSequence=[&](const char* name,int time,float x,float y,bool flip=false,double rotation=0) {
            const auto& frames=sequenceTextures_.at(name);
            auto* texture=frames.at(time%frames.size()).get();
            SDL_FRect dest{x-cameraX-96,y-cameraY-128,192,192};SDL_FPoint origin{96,128};
            check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,rotation,&origin,flip?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE));
        };
        for(const auto& o:areas_.objects.objects) {
            auto drawObject=[&](const char* name,float y,double angle,float scale=1.f) {
                auto* texture=objectTextures_.at(name).get();
                SDL_FRect dest{o.spec.x-cameraX-96*scale,y-cameraY-128*scale,192*scale,192*scale};SDL_FPoint origin{96*scale,128*scale};
                check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,angle,&origin,SDL_FLIP_NONE));
            };
            if(o.spec.kind==3)continue;
            if(o.spec.kind==2) {
                auto* texture=objectTextures_.at("ball").get();
                SDL_FRect dest{o.previousX+(o.x-o.previousX)*alpha-cameraX-96*o.scale,
                              o.previousY+(o.y-o.previousY)*alpha-cameraY-128*o.scale,192*o.scale,192*o.scale};
                SDL_FPoint pivot{96*o.scale,128*o.scale};
                check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,o.age*6,&pivot,SDL_FLIP_NONE));continue;
            }
            if(o.spec.kind==1){drawObject("sling",o.spec.y,o.angle,o.spec.d);continue;}
            drawObject("cannon_hole",o.spec.y,0);
            if(o.state)drawObject("cannon_barrel",o.spec.y-27+std::max(0,39-o.age)*1.7f,o.angle);
        }
        for(const auto& coin:items_.starCoins) {
            auto* texture=objectTextures_.at("star_coin").get();
            check(SDL_SetTextureAlphaModFloat(texture,(items_.starCoinMask&(1<<coin.number))?.25f:1.f));
            SDL_FRect dest{coin.x-cameraX-96,coin.y-cameraY-128,192,192};
            check(SDL_RenderTexture(renderer_,texture,nullptr,&dest));
        }
        for(const auto& shine:items_.shines)if(items_.shineAvailable(shine))
            drawSequence("shine",tick_%24,shine.x,shine.y);
        for(const auto& platform:areas_.platforms.objects) {
            if(platform.spec.kind==4) {
                auto* texture=objectTextures_.at("block_"+std::to_string(static_cast<int>(platform.spec.style))).get();
                const float scale=platform.spec.scale/100.f;
                SDL_FRect dest{platform.previousX+(platform.x-platform.previousX)*alpha-cameraX-96*scale,
                              platform.previousY+(platform.y-platform.previousY)*alpha-cameraY-128*scale,192*scale,192*scale};
                SDL_FPoint pivot{96*scale,128*scale};
                check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,platform.previousAngle+(platform.angle-platform.previousAngle)*alpha,&pivot,SDL_FLIP_NONE));continue;
            }
            if(platform.spec.kind==3) {
                auto* texture=objectTextures_.at("falling_log").get();
                SDL_FRect dest{platform.previousX+(platform.x-platform.previousX)*alpha-cameraX-96,
                              platform.previousY+(platform.y-platform.previousY)*alpha-cameraY-128,192,192};
                check(SDL_SetTextureAlphaModFloat(texture,platform.opacity));check(SDL_RenderTexture(renderer_,texture,nullptr,&dest));continue;
            }
            const auto& frames=platform.spec.kind==1 ? sm63::platformdata::log : sm63::platformdata::green;
            for(const auto& part:frames.at(platform.spec.scale-1))
                drawItem(part,platform.previousX+(platform.x-platform.previousX)*alpha,
                         platform.previousY+(platform.y-platform.previousY)*alpha,platform.opacity,
                         platform.spec.kind==1 ? (platform.previousAngle+(platform.angle-platform.previousAngle)*alpha)*.01745329252f : 0.f);
        }
        if(debug_) {
            check(SDL_SetRenderDrawColor(renderer_,255,100,100,255));
            for(const auto& surface:terrain_.surfaces)
                check(SDL_RenderLine(renderer_,surface.x1-cameraX,surface.y1-cameraY,surface.x2-cameraX,surface.y2-cameraY));
        }
        for(const auto& object:sm63::world::areas[areas_.current].objects)if(object.kind==4 || object.kind==20) {
            if(object.kind==20) {
                SDL_FRect toad{object.x-cameraX-96,object.y-cameraY-128,192,192};
                check(SDL_RenderTexture(renderer_,objectTextures_.at("toad").get(),nullptr,&toad));
            }
            SDL_FRect sign{object.x-cameraX-40,object.y-cameraY-90,80,100};
            if(object.kind==4)check(SDL_RenderTexture(renderer_,signTexture_.get(),nullptr,&sign));
            if(promptSign_==&object && signPromptAlpha_>.01f) {
                SDL_FRect prompt{object.x-cameraX-14.75f,object.y-cameraY-89.25f,30,30};
                check(SDL_SetTextureAlphaModFloat(signPrompt_.get(),signPromptAlpha_));
                check(SDL_RenderTexture(renderer_,signPrompt_.get(),nullptr,&prompt));
                check(SDL_SetTextureAlphaMod(signPrompt_.get(),255));
            }
        }
        auto bitmap=[&](const char* path,float x,float y,float scale=1.f,bool flip=false) {
            auto* texture=marioTextures_.at(path).get();
            SDL_FRect dest{x,y,texture->w*scale,texture->h*scale};
            check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,0,nullptr,flip?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE));
        };
        for(const auto& supply:items_.supplies) {
            if(supply.gone)continue;
            if(supply.kind==18) {
                auto* texture=objectTextures_.at("heart").get();
                const float turn=std::sin(supply.spinPhase),scale=std::abs(turn);
                SDL_FRect dest{supply.x-cameraX-96*scale,supply.y-cameraY-128,192*scale,192};
                if(scale>.001f)check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,0,nullptr,turn<0?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE));
                continue;
            }
            if(supply.kind==17) {
                auto* texture=objectTextures_.at("rainbow").get();
                SDL_FRect dest{supply.x-cameraX-96,supply.y-cameraY-128,192,192};
                check(SDL_RenderTexture(renderer_,texture,nullptr,&dest));continue;
            }
            if(supply.kind==9) {bitmap(sm63::huddata::icons[1],supply.x-cameraX-12,supply.y-cameraY-12);continue;}
            if(supply.kind==8) {
                check(SDL_SetRenderDrawColor(renderer_,225,245,255,255));
                SDL_FRect bottle{supply.x-cameraX-6,supply.y-cameraY-10,12,20};check(SDL_RenderFillRect(renderer_,&bottle));
                check(SDL_SetRenderDrawColor(renderer_,40,150,245,255));
                SDL_FRect water{bottle.x+2,bottle.y+6,8,12};check(SDL_RenderFillRect(renderer_,&water));
                SDL_FRect cap{bottle.x+3,bottle.y-3,6,4};check(SDL_RenderFillRect(renderer_,&cap));
            }else {
                if(!supply.cooldown)bitmap(sm63::huddata::boxes[supply.kind-5],supply.x-cameraX-16,supply.y-cameraY-20);
                if(supply.out)bitmap(sm63::huddata::nozzles[supply.kind-5],supply.x-cameraX-11,supply.pickupY-cameraY-14);
            }
        }
        for(const auto& box:items_.boxes) {
            if(!box.broken)drawItem(sm63::itemdata::box,box.x,box.y);
            else if(box.brokenTicks<16) {
                // Small native debris effect; full Flash fragment timeline is pending.
                check(SDL_SetRenderDrawColor(renderer_,230,150,40,255));
                for(int i=0;i<4;++i) {
                    float t=box.brokenTicks;
                    SDL_FRect piece{box.x-cameraX+(i-1.5f)*t*1.5f,
                        box.y-cameraY-18-t*2+t*t*.15f+(i%2)*6,4,4};
                    check(SDL_RenderFillRect(renderer_,&piece));
                }
            }
        }
        for(const auto& coin:items_.coins) {
            if(coin.gone || (coin.dropped && coin.age>288 && coin.age%4<2))continue;
            const auto* track=coin.value==5 ? sm63::itemdata::blue.data() : coin.value==2 ? sm63::itemdata::red.data() : sm63::itemdata::gold.data();
            const int length=coin.value==1 ? 8 : 4;
            const auto* frame=track;
            for(int i=0;i<length;++i)if(coin.age%(length*2)>=track[i].start)frame=&track[i];
            drawItem(*frame,coin.previousX+(coin.x-coin.previousX)*alpha,
                     coin.previousY+(coin.y-coin.previousY)*alpha,
                     coin.collectedTicks ? (8-coin.collectedTicks)/8.f : 1.f);
        }
        for (const auto& enemy : enemies_.actors) {
            if (enemy.state == sm63::EnemyState::Gone) continue;
            if(enemy.kind!=sm63::EnemyKind::Goomba) {
                using K=sm63::EnemyKind;using S=sm63::EnemyState;
                std::string track;
                if(enemy.kind==K::Bobomb)track=enemy.state==S::Exploding?"bobomb_explode":enemy.state==S::Tumble?"bobomb_tumble":"bobomb_walk";
                if(enemy.kind==K::Koopa)track=enemy.state==S::Flying?"koopa_fly":enemy.state==S::Shell?"koopa_shell":"koopa_walk";
                if(enemy.kind==K::ShyGuy)track=enemy.state==S::Flying?"shy_fly":enemy.state==S::Tumble?"shy_tumble":enemy.state==S::Squished?"shy_squish":"shy_walk";
                if(enemy.kind==K::Goonie)track=enemy.riding?"goonie_ride":enemy.age%295>=250?"goonie_dive":"goonie_fly";
                if(enemy.kind==sm63::EnemyKind::Cheep)track="cheep_swim";
                if(enemy.kind==sm63::EnemyKind::Penguin)track=enemy.state==sm63::EnemyState::Tumble?"penguin_tumble":enemy.state==sm63::EnemyState::Flying?(enemy.vy>0?"penguin_fly_down":"penguin_fly_up"):std::abs(enemy.vx)>.15f?"penguin_walk":"penguin_idle";
                auto& frames=enemyTextures_.at(track);auto* texture=frames[enemy.age%frames.size()].get();
                float ex=enemy.previousX+(enemy.x-enemy.previousX)*alpha-cameraX;
                float ey=enemy.previousY+(enemy.y-enemy.previousY)*alpha-cameraY;
                bool mirror=enemy.kind==K::ShyGuy?enemy.direction<0:enemy.direction>0;
                SDL_FRect dest{ex-96,ey-128,192,192};SDL_FPoint pivot{96,116};
                if(enemy.fuse && enemy.state!=S::Exploding)check(SDL_SetTextureColorMod(texture,255,static_cast<Uint8>(255-enemy.fuse*2),static_cast<Uint8>(255-enemy.fuse*2)));
                check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,enemy.state==S::Tumble?enemy.age*17.:0,&pivot,mirror?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE));
                check(SDL_SetTextureColorMod(texture,255,255,255));continue;
            }
            const sm63::enemydata::Sprite* sprite = &sm63::enemydata::walk[enemy.age % 8];
            if (enemy.state == sm63::EnemyState::Squished) {
                sprite = &sm63::enemydata::squish.front();
                for (const auto& frame : sm63::enemydata::squish) if (enemy.age >= frame.start) sprite = &frame;
            }
            auto* texture = marioTextures_.at(sprite->path).get();
            const float ex=enemy.previousX+(enemy.x-enemy.previousX)*alpha-cameraX;
            const float ey=enemy.previousY+(enemy.y-enemy.previousY)*alpha-cameraY;
            const float mirror=enemy.direction<0 ? -1.f : 1.f;
            const float angle=enemy.state==sm63::EnemyState::Tumble ? enemy.age*.3f : 0;
            std::array<SDL_Vertex,4> vertices{};
            const std::array<SDL_FPoint,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
            for (int i=0;i<4;++i) {
                float u=uv[i].x*texture->w, v=uv[i].y*texture->h;
                float x=mirror*(sprite->a*u+sprite->c*v+sprite->x);
                float y=sprite->b*u+sprite->d*v+sprite->y;
                vertices[i]={{ex+std::cos(angle)*x-std::sin(angle)*(y+12),
                              ey+std::sin(angle)*x+std::cos(angle)*(y+12)-12},{1,1,1,1},uv[i]};
            }
            constexpr int indices[]={0,1,2,0,2,3};
            check(SDL_RenderGeometry(renderer_,texture,vertices.data(),4,indices,6));
        }
        const sm63::mario::Sprite* frame = &sm63::mario::walk.front();
        auto select = [&](const auto& track, int time) {
            frame = &track.front();
            for (const auto& candidate : track) if (time >= candidate.start) frame = &candidate;
        };
        if (!player_.grounded) {
            frame = player_.vy > 2 ? &sm63::mario::fall.front() : &sm63::mario::rise.front();
            if (player_.flipTicks > 8) frame = &sm63::mario::flip.front();
        } else if (player_.running) {
            select(sm63::mario::run, player_.runTicks < 6 ? player_.runTicks : 6 +
                   static_cast<int>(std::floor(player_.runPhase + 0.5f)));
        } else if (std::abs(player_.vx) > 0.1f) {
            select(sm63::mario::walk, std::clamp(static_cast<int>(std::floor(player_.walkPhase + 0.5f)) - 1, 0, 79));
        }
        if (player_.pound != sm63::Pound::None) {
            const int time = player_.pound == sm63::Pound::Windup ? std::min(player_.poundTicks, 11) :
                             player_.pound == sm63::Pound::Falling ? 12 + std::min(player_.poundTicks, 14) :
                             27 + std::min(player_.poundTicks, 15);
            select(sm63::mario::pound, time);
        }
        if (player_.diving || player_.diveRecovery)
            select(sm63::mario::dive, player_.diving ? std::min(player_.diveTicks - 1, 2) : 9 - player_.diveRecovery);
        if (player_.diveFlipTicks) {
            if (player_.backflip) select(sm63::mario::backflip, 22 - player_.diveFlipTicks);
            else select(sm63::mario::diveflip, 9 - player_.diveFlipTicks);
        }
        if(player_.spinSpeed>0)select(sm63::mario::spin,static_cast<int>(player_.spinPhase)+(player_.spinSpeed>10 ? 0 : 40));
        const bool equippedSlide=(player_.diving || player_.diveRecovery) && !player_.diveFlipTicks && player_.spinSpeed==0 && player_.fludd.nozzle!=sm63::Nozzle::None;
        if(equippedSlide) {
            const int time=player_.diving?std::min(player_.diveTicks-1,2):9-player_.diveRecovery;
            if(player_.fludd.nozzle==sm63::Nozzle::Hover)select(sm63::mario::hover_dive,std::min(time,7));
            if(player_.fludd.nozzle==sm63::Nozzle::Rocket)select(sm63::mario::rocket_dive,time);
            if(player_.fludd.nozzle==sm63::Nozzle::Turbo)select(sm63::mario::turbo_dive,time);
        }
        const bool equippedFlip=player_.diveFlipTicks && player_.fludd.nozzle!=sm63::Nozzle::None;
        if(equippedFlip) {
            const int time=player_.backflip?22-player_.diveFlipTicks:9-player_.diveFlipTicks;
            if(player_.fludd.nozzle==sm63::Nozzle::Hover) {if(player_.backflip)select(sm63::mario::hover_backflip,time);else select(sm63::mario::hover_diveflip,time);}
            if(player_.fludd.nozzle==sm63::Nozzle::Rocket) {if(player_.backflip)select(sm63::mario::rocket_backflip,time);else select(sm63::mario::rocket_diveflip,time);}
            if(player_.fludd.nozzle==sm63::Nozzle::Turbo) {if(player_.backflip)select(sm63::mario::turbo_backflip,time);else select(sm63::mario::turbo_diveflip,time);}
        }
        if(player_.flying)select(sm63::mario::flying,player_.flightAngle>-1.0472f?1:player_.flightAngle<-2.0944f?2:0);
        if(player_.swimming && player_.spinSpeed==0)select(sm63::mario::swim,player_.swimTicks%24);
        if(sign_.active())frame=&readingPose;
        auto* sprite = marioTextures_.at(frame->path).get();

        // Triple-jump turn uses the source spin bitmap; full Flash tween easing is pending.
        float angle = player_.flipTicks > 8 ? (30 - player_.flipTicks) * (-6.2831853f / 22) : 0;
        if (player_.pound == sm63::Pound::Windup && player_.poundTicks >= 2 && player_.poundTicks < 9)
            angle = (player_.poundTicks - 2) * (-6.2831853f / 7);
        if (player_.diveFlipTicks && player_.backflip) {
            const int time = 22 - player_.diveFlipTicks;
            if (time >= 3 && time < 21) angle = (time - 3) * (6.2831853f / 18);
        }
        if(player_.flying)angle=player_.flightAngle;

        std::array<SDL_Vertex, 4> vertices{};
        const std::array<SDL_FPoint, 4> uv{{{0,0},{1,0},{1,1},{0,1}}};
        for (std::size_t i = 0; i < 4; ++i) {
            const float u = uv[i].x * sprite->w, v = uv[i].y * sprite->h;
            float x = frame->a * u + frame->c * v + frame->x;
            float y = frame->b * u + frame->d * v + frame->y;
            // Rotate around the body center rather than around the feet.
            const auto posed=sm63::bodyPoint(x,y,angle,player_.facingLeft);
            vertices[i] = {{renderX + posed[0] - cameraX, renderY + posed[1] - cameraY},
                           {1,1,1,1}, uv[i]};
        }
        if(player_.fludd.nozzle!=sm63::Nozzle::None && (!player_.invincibleTicks || (player_.invincibleTicks/3)%2==0)) {
            const float packX=renderX-cameraX+(player_.facingLeft?2.f:-16.f),packY=renderY-cameraY-29;
            if(!equippedSlide && !equippedFlip && !sequence_.active() && !areas_.objects.hidesPlayer())bitmap(sm63::huddata::pack[0],packX,packY,1.f,player_.facingLeft);
            if(!equippedSlide && !equippedFlip && !sequence_.active() && !areas_.objects.hidesPlayer())bitmap(sm63::huddata::pack[static_cast<int>(player_.fludd.nozzle)],packX,packY-9,.8f,player_.facingLeft);
            if(player_.fludd.spraying) {
                check(SDL_SetRenderDrawColor(renderer_,160,225,255,210));
                for(int i=0;i<9;++i){float t=(tick_+i*3)%18;float sx=packX+6+(i%3-1)*t*.25f,sy=packY+24+t*2;
                    if(player_.fludd.nozzle==sm63::Nozzle::Turbo || (player_.fludd.nozzle==sm63::Nozzle::Hover && player_.diving)){sx=packX+(player_.facingLeft?1:-1)*t*2;sy=packY+18+(i%3-1)*2;}
                    SDL_FRect drop{sx,sy,2,4};check(SDL_RenderFillRect(renderer_,&drop));}
            }
        }
        constexpr int indices[] = {0,1,2,0,2,3};
        if (!sequence_.active() && !areas_.objects.hidesPlayer() && (!player_.invincibleTicks || (player_.invincibleTicks / 3) % 2 == 0))
            check(SDL_RenderGeometry(renderer_, sprite, vertices.data(), 4, indices, 6));
        if(player_.wingTicks>0 && !areas_.objects.hidesPlayer() && (player_.wingTicks>96 || (player_.wingTicks/4)%2==0)) {
            auto* wing=objectTextures_.at((player_.flying || (player_.vy>3 && up_)) && tick_%6>=3?"wing_flap":"wing").get();
            for(int part=0;part<frame->wingCount;++part) {
                const auto& w=frame->wings[part];std::array<SDL_Vertex,4> wingVertices{};
                for(std::size_t i=0;i<4;++i) {
                    const float u=uv[i].x*192-96,v=uv[i].y*192-128;
                    const float x=w.a*u+w.c*v+w.x,y=w.b*u+w.d*v+w.y;
                    const auto posed=sm63::bodyPoint(x,y,angle,player_.facingLeft);
                    wingVertices[i]={{renderX+posed[0]-cameraX,renderY+posed[1]-cameraY},{1,1,1,1},uv[i]};
                }
                constexpr int wingIndices[]={0,1,2,0,2,3};
                check(SDL_RenderGeometry(renderer_,wing,wingVertices.data(),4,wingIndices,6));
            }
        }
        // Clip pool shading to open terrain so submerged banks keep their artwork.
        for(const auto& water:terrain_.water) {
            const int left=std::max(0,static_cast<int>(std::ceil(water.left-cameraX)));
            const int right=std::min(450,static_cast<int>(std::ceil(water.right-cameraX)));
            const int top=std::max(0,static_cast<int>(std::ceil(water.top-cameraY)));
            const int bottom=std::min(300,static_cast<int>(std::ceil(water.bottom-cameraY)));
            for(int sy=top;sy<bottom;++sy) {
                const bool surface=sy==static_cast<int>(std::ceil(water.top-cameraY));
                check(SDL_SetRenderDrawColor(renderer_,surface?140:40,surface?215:128,surface?255:215,surface?180:95));
                int run=-1;
                for(int sx=left;sx<=right;++sx) {
                    const bool clear=sx<right && terrain_.at(static_cast<int>(std::floor(sx+cameraX)),static_cast<int>(std::floor(sy+cameraY)))==0;
                    if(clear && run<0)run=sx;
                    if(!clear && run>=0){SDL_FRect strip{static_cast<float>(run),static_cast<float>(sy),static_cast<float>(sx-run),1};check(SDL_RenderFillRect(renderer_,&strip));run=-1;}
                }
            }
        }
        if (!debug_) check(SDL_RenderTexture(renderer_, frontArt_.get(), nullptr, &artwork));
        const auto& boss=areas_.objects.boss;
        if(boss.state!=sm63::KingBoss::State::Absent && !boss.defeated()) {
            using S=sm63::KingBoss::State;using A=sm63::KingBoss::Action;
            std::string track="idle";int frame=0;
            if(boss.state==S::Hurt || boss.state==S::Dying || boss.state==S::Outro) {
                track="hurt";frame=boss.state==S::Hurt?std::min(boss.age,26):boss.state==S::Dying?std::min(8+boss.age,38):38;
            } else if(boss.state==S::Fight) {
                if(boss.action==A::Walk){track="walk";frame=boss.actionAge%24;}
                if(boss.action==A::Throw){track="throw";frame=std::min(boss.actionAge,44);}
                if(boss.action==A::Jump){track="jump";frame=boss.actionAge<5?boss.actionAge:boss.vy>2?6:5;}
                if(boss.action==A::Grab){track="grab";frame=std::min(boss.actionAge,84);}
            }
            auto drawRings=[&](const char* name) {
                auto* ringTexture=objectTextures_.at(name).get();
                for(const auto& ring:boss.rings) {
                    const float t=ring.age;
                    const float width=t<4?10+44*t/4:t<64?54+(700-54)*(t-4)/60:700+180*(t-64)/16;
                    const float opacity=t<4?.15f+.35f*t/4:t<64?.5f-.3f*(t-4)/60:.2f*(80-t)/16;
                    const float sx=width/700;
                    SDL_FRect dest{ring.x-cameraX-512*sx,ring.y-cameraY-32,1024*sx,64};
                    check(SDL_SetTextureAlphaModFloat(ringTexture,std::clamp(opacity,0.f,1.f)));
                    check(SDL_RenderTexture(renderer_,ringTexture,nullptr,&dest));
                }
            };
            drawRings("shock_back");
            auto* texture=objectTextures_.at("boss_"+track+"_"+std::to_string(frame)).get();
            SDL_FRect dest{boss.previousX+(boss.x-boss.previousX)*alpha-cameraX-96,
                          boss.previousY+(boss.y-boss.previousY)*alpha-cameraY-128,192,192};
            check(SDL_RenderTextureRotated(renderer_,texture,nullptr,&dest,0,nullptr,boss.direction>0?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE));
            drawRings("shock_front");
            if(boss.state==S::Fight || boss.state==S::Hurt)text(135,60,"KING BOB-OMB  "+std::to_string(boss.hp)+"/3");
        }
        if(sequence_.kind==sm63::Sequence::Kind::Shine) {
            const char* poses[]={"star_none","star_hover","star_rocket","star_turbo"};
            drawSequence(poses[static_cast<int>(player_.fludd.nozzle)],sequence_.pose,renderX,renderY,!player_.facingLeft);
        } else if(sequence_.kind==sm63::Sequence::Kind::Death) {
            const char* poses[]={"death_none","death_hover","death_rocket","death_turbo"};
            drawSequence(poses[static_cast<int>(player_.fludd.nozzle)],sequence_.age,renderX,renderY,false,sequence_.rotation);
        }
        if (debug_) {
            check(SDL_SetRenderDrawColor(renderer_, 255, 211, 112, 255));
            const SDL_FRect body{player_.x - 8 - cameraX, player_.y - 34 - cameraY, 16, 34};
            check(SDL_RenderRect(renderer_, &body));
        }
        if(player_.swimming || player_.oxygen<8)text(12,44,"AIR "+std::to_string(player_.oxygen)+"/8");
        if(player_.wingTicks>0)text(12,28,"WING CAP "+std::to_string((player_.wingTicks+31)/32));
        if(areas_.objects.active())text(90,70,areas_.objects.hidesPlayer()?"LEFT/RIGHT: AIM   SPACE: FIRE":"X / SPACE: SLING   DOWN: RELEASE");
        // HUD positions follow Stats Clip; gauges use native geometry.
        bitmap(sm63::huddata::hp[std::clamp(player_.health,0,8)],200.5f,2);
        bitmap(sm63::huddata::icons[0],267.5f,7.3f);
        text(292,10,"x"+std::to_string(lives_));
        drawItem(sm63::itemdata::gold[0],cameraX+342.65f,cameraY+10.35f);
        text(358,7,std::to_string(items_.count));
        drawItem(sm63::itemdata::red[0],cameraX+395,cameraY+34);
        text(407,30,std::to_string(items_.redCount)+"/8");
        bitmap(sm63::huddata::icons[1],337,31,.65f);text(358,32,std::to_string(items_.silverCount)+"/5");
        if(player_.fludd.nozzle!=sm63::Nozzle::None) {
            const char* names[]={"NONE","HOVER","ROCKET","TURBO"};
            const int nozzle=static_cast<int>(player_.fludd.nozzle);
            bitmap(sm63::huddata::nozzles[nozzle-1],384,230);
            text(342,214,names[nozzle]);
            check(SDL_SetRenderDrawColor(renderer_,15,35,55,220));
            SDL_FRect tank{419,198,22,80};check(SDL_RenderFillRect(renderer_,&tank));
            check(SDL_SetRenderDrawColor(renderer_,55,175,255,255));
            const float height=74*player_.fludd.water/10000;
            SDL_FRect water{422,275-height,16,height};check(SDL_RenderFillRect(renderer_,&water));
            text(406,284,std::to_string(static_cast<int>(player_.fludd.water/100))+"%");
            check(SDL_SetRenderDrawColor(renderer_,15,35,55,220));
            SDL_FRect meter{330,268,80,10};check(SDL_RenderFillRect(renderer_,&meter));
            check(SDL_SetRenderDrawColor(renderer_,245,205,50,255));
            SDL_FRect power{332,270,76*player_.fludd.power/100,6};check(SDL_RenderFillRect(renderer_,&power));
            text(330,284,player_.fludd.charge ? "CHARGE" : "POWER");
        }
        text(8,8,areas_.current>=6?"TUTORIAL "+std::to_string(areas_.current-5):areas_.current==5?"2-1 SNOWMAN'S LAND":"1-"+std::to_string(areas_.current+1));
        if(areas_.current==6 && !areas_.tutorialGateOpen && player_.x>280)text(10,242,"READ THE CONTROLS SIGN WITH Z");
        if(paused_)text(8,24,"PAUSED / P");
        if(exploring_){text(8,264,"EXPLORE / Arrows: fly");text(8,280,"F2: play / PgUp-Dn: area");}
        else {text(8,276,"Up: jump / Space: FLUDD");text(8,290,"Shift: nozzle / F2: fly / F3: map");}
        if(sign_.scale()>0) {
            for(std::size_t i=0;i<sm63::signdata::cards.size();++i)if(sign_.key==sm63::signdata::cards[i].key) {
                float scale=sign_.scale();SDL_FRect panel{225-225*scale,150-150*scale,450*scale,300*scale};
                auto& frames=signCards_[i];check(SDL_RenderTexture(renderer_,frames[(sign_.animation/2)%frames.size()].get(),nullptr,&panel));
                if(sign_.phase==sm63::SignSession::Phase::Reading){SDL_FRect prompt{207,260,36,36};check(SDL_RenderTexture(renderer_,signPrompt_.get(),nullptr,&prompt));}
                break;
            }
        }
        auto messageCard=[&](const std::string& key,int age) {
            const float scale=std::min(1.f,.1f+.9f*age/5);
            SDL_FRect panel{225-225*scale,150-150*scale,450*scale,300*scale};
            for(std::size_t i=0;i<sm63::signdata::cards.size();++i)if(key==sm63::signdata::cards[i].key) {
                check(SDL_RenderTexture(renderer_,signCards_[i][0].get(),nullptr,&panel));break;
            }
            if(age>=5){SDL_FRect prompt{207,260,36,36};check(SDL_RenderTexture(renderer_,signPrompt_.get(),nullptr,&prompt));}
        };
        if(boss.state==sm63::KingBoss::State::Intro || boss.state==sm63::KingBoss::State::Outro)
            messageCard(boss.state==sm63::KingBoss::State::Intro?"Mario... I remember you... You can't grab me and throw me this time, so I'll destroy you with my bombs!!":"Next...time...I'll...be...stronger...",boss.age);
        if(starCoinNotice_) {
            int total=0;for(int i=1;i<31;++i)if(items_.starCoinMask&(1<<i))++total;
            messageCard("You got a Star Coin!<br><br>"+std::string(total==1?"You have collected your First Star Coin!!":"You have collected "+std::to_string(total)+" Star Coins."),noticeAge_);
        }
        if(!signMessage_.empty()) {
            check(SDL_SetRenderDrawColor(renderer_,15,25,35,245));SDL_FRect panel{12,65,426,175};check(SDL_RenderFillRect(renderer_,&panel));
            std::istringstream words(signMessage_);std::string word,line;float y=78;
            while(words>>word){if(line.size()+word.size()>47){text(22,y,line);line.clear();y+=14;}line+=word+" ";}
            text(22,y,line);
        }
        if(sequence_.fade()>0) {
            check(SDL_SetRenderDrawColor(renderer_,0,0,0,static_cast<Uint8>(sequence_.fade()*255)));
            SDL_FRect screen{0,0,450,300};check(SDL_RenderFillRect(renderer_,&screen));
        }
        if(transitionTicks_>0) {
            check(SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND));
            check(SDL_SetRenderDrawColor(renderer_,0,0,0,static_cast<Uint8>(transitionTicks_*255/16)));
            check(SDL_RenderFillRect(renderer_,nullptr));
        }
        if (debug_) text(10, 52, "x " + std::to_string(static_cast<int>(player_.x)) +
                        "  y " + std::to_string(static_cast<int>(player_.y)) +
                        (player_.grounded ? "  grounded" : "  airborne"));
    }
    void text(float x, float y, const std::string& value) {
        check(SDL_SetRenderDrawColor(renderer_, 15, 22, 32, 255));
        check(SDL_RenderDebugText(renderer_, x+1, y+1, value.c_str()));
        check(SDL_SetRenderDrawColor(renderer_, 245, 245, 245, 255));
        check(SDL_RenderDebugText(renderer_, x, y, value.c_str()));
    }
    void selectAsset(int delta) {
        const auto count = static_cast<int>(assets::catalog.size());
        const auto next = (assetIndex_ + delta + count) % count;
        auto texture = loadTexture(renderer_, root_ / assets::catalog[next]);
        browser_ = std::move(texture);
        assetIndex_ = next;
    }
    SDL_Renderer* renderer_;
    std::filesystem::path root_,data_;
    sm63::Areas areas_;
    bool exploring_=false,gameOver_=false;
    int lives_=4,nextLifeCoin_=100,fluddSoundTicks_=0;
    std::unordered_map<std::string,Texture> objectTextures_;
    int starCoinNotice_=0,noticeAge_=0;bool noticeReleased_=false;
    sm63::Sequence sequence_;
    int transitionTicks_=0,exitCooldown_=0;
    const sm63::world::Object* promptSign_=nullptr;
    float signPromptAlpha_=0;
    sm63::SignSession sign_;
    bool blockPound_=false;
    std::unordered_map<std::string,std::vector<Texture>> enemyTextures_,sequenceTextures_;
    std::vector<std::vector<Texture>> signCards_;
    Texture signPrompt_{nullptr,SDL_DestroyTexture};
    std::string signMessage_;
    Texture signTexture_{nullptr,SDL_DestroyTexture};
    std::vector<Texture> animation_;
    Texture browser_;
    Texture groundTexture_;
    Texture tutorialBackground_{nullptr,SDL_DestroyTexture};
    Texture snowBackground_{nullptr,SDL_DestroyTexture};
    Texture secretBackground_{nullptr,SDL_DestroyTexture};
    Texture backArt_{nullptr, SDL_DestroyTexture}, frontArt_{nullptr, SDL_DestroyTexture};
    sm63::Terrain terrain_;
    sm63::Player player_;
    sm63::Enemies enemies_;
    sm63::Items items_;
    std::unique_ptr<sm63::Audio> audio_;
    int coinSoundCooldown_=0,stepSoundTicks_=0;
    std::unordered_map<std::string, Texture> marioTextures_;
    float previousX_ = 0, previousY_ = 0;
    int assetIndex_ = 0;
    int tick_ = 0;
    float x_ = 225;
    bool left_ = false, right_ = false, facingLeft_ = false;
    bool paused_ = false, browsing_ = false;
    bool game_ = true, up_ = false, space_ = false, pound_ = false, dive_ = false, spin_ = false, debug_ = false;
};

int run(const Options& options) {
    check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    Handle<SDL_Window, SDL_DestroyWindow> window(
        SDL_CreateWindow("Super Mario 63 - native port preview", assets::width * 2, assets::height * 2,
                         options.smoke ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    check(window != nullptr);
    Handle<SDL_Renderer, SDL_DestroyRenderer> renderer(SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
    check(renderer != nullptr);
    check(SDL_SetRenderLogicalPresentation(renderer.get(), assets::width, assets::height,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX));
    // VSync is optional: the frame limiter below also handles software renderers.
    SDL_SetRenderVSync(renderer.get(), 1);
    Preview preview(renderer.get(), options.root, options.data,options.area);
    if(options.positioned)preview.place(options.startX,options.startY);
    if(options.readSign)preview.readSign(options.readSign);
    if(options.nozzle!=sm63::Nozzle::None)preview.equip(options.nozzle);
    if(!options.sequence.empty())preview.previewSequence(options.sequence);
    if(options.explore) {
        SDL_Event toggle{};toggle.type=SDL_EVENT_KEY_DOWN;toggle.key.scancode=SDL_SCANCODE_F2;preview.event(toggle);
    }
    if (options.smoke) {
        for (const auto* path : assets::catalog) {
            auto texture = loadTexture(renderer.get(), options.root / path);
        }
        SDL_Log("Validated %zu PNG uploads and %zu original animation spans", assets::catalog.size(), assets::animation.size());
    }
    constexpr double step = 1.0 / assets::tickRate;
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    auto previous = SDL_GetPerformanceCounter();
    double accumulator = 0;
    int frames = 0;
    bool running = true;
    while (running) {
        const auto start = SDL_GetPerformanceCounter();
        accumulator += std::min(static_cast<double>(start - previous) / frequency, 0.25);
        previous = start;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)) running = false;
            preview.event(event);
        }
        if (!running) break;
        if (options.smoke || options.replay) {
            preview.replayInput(frames);
            preview.tick();
            if(options.smoke && options.explore && frames>0 && frames<=128 && frames%32==0) {
                SDL_Event next{};next.type=SDL_EVENT_KEY_DOWN;next.key.scancode=SDL_SCANCODE_PAGEDOWN;preview.event(next);
            }
            if(options.smoke && options.explore && frames==140) {
                SDL_Event toggle{};toggle.type=SDL_EVENT_KEY_DOWN;toggle.key.scancode=SDL_SCANCODE_F2;preview.event(toggle);
            }
            if(options.smoke && options.explore && (frames==145 || frames==155)) {
                SDL_Event toggle{};toggle.type=SDL_EVENT_KEY_DOWN;toggle.key.scancode=SDL_SCANCODE_F3;preview.event(toggle);
            }
            // Exercise both views through the same event path as interactive use.
            if (options.smoke && (frames == 160 || frames == 176)) {
                SDL_Event toggle{};
                toggle.type = SDL_EVENT_KEY_DOWN;
                toggle.key.scancode = SDL_SCANCODE_TAB;
                preview.event(toggle);
            }
        } else {
            while (accumulator >= step) { preview.tick(); accumulator -= step; }
        }
        preview.draw(options.smoke || options.replay ? 1.0f : static_cast<float>(accumulator / step));
        ++frames;
        if (options.frameLimit && frames >= options.frameLimit) running = false;
        if (!running && !options.screenshot.empty()) {
            Surface surface(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
            check(surface != nullptr);
            check(SDL_SavePNG(surface.get(), options.screenshot.c_str()));
        }
        check(SDL_RenderPresent(renderer.get()));
        if (!options.smoke && !options.replay) {
            const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - start) / frequency;
            if (elapsed < 1.0 / 120.0)
                SDL_DelayNS(static_cast<Uint64>((1.0 / 120.0 - elapsed) * 1e9));
        }
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--help") {
                std::cout << "sm63 [--assets LIBRARY_DIR] [--data GENERATED_DIR] [--frames N] [--screenshot FILE.png] [--smoke-test] [--replay] [--preview-sequence shine|death] [--position X,Y] [--read-sign N] [--nozzle hover|rocket|turbo] [--explore] [--area 1-1|1-2|1-3|1-4|1-5|2-1|tutorial|8-E1-1-2]\n";
                return 0;
            }
        }
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "sm63: " << error.what() << '\n';
        return 1;
    }
}
