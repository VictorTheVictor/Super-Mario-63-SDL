#include "signs.hpp"
#include <stdexcept>
#include <iostream>
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
    sm63::Player p;p.x=100;p.y=190;p.grounded=true;
    require(sm63::SignSession::canRead(p,110,190,false),"Nearby grounded Mario can read");
    require(!sm63::SignSession::canRead(p,130,190,false),"Distant sign must not steal ground pound input");
    require(!sm63::SignSession::canRead(p,110,190,true),"Moving horizontally blocks sign reading");
    sm63::SignSession sign;sign.open(p,110,"@picture:breakbox");
    for(int i=0;i<30;++i)sign.tick(p,true);
    require(sign.phase==sm63::SignSession::Phase::Reading,"Holding Z through opening must not dismiss");
    require(std::abs(p.x-110)<.1f && p.vx==0,"Mario aligns to sign while reading");
    sign.tick(p,false);sign.tick(p,true);
    require(sign.phase==sm63::SignSession::Phase::Closing,"Release then press Z closes sign");
    for(int i=0;i<6;++i)sign.tick(p,true);
    require(!sign.active(),"Closing restores gameplay");
    std::cout<<"Sign proximity, opening, release-to-close and alignment passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
