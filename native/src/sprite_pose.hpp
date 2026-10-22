#pragma once
#include <array>
#include <cmath>
namespace sm63 {
// Mario and attached artwork share the body pivot and mirror after rotation.
inline std::array<float,2> bodyPoint(float x,float y,float angle,bool facingLeft) {
    const float c=std::cos(angle),s=std::sin(angle),mirror=facingLeft?1.f:-1.f;
    return {mirror*(c*x-s*(y+17)),s*x+c*(y+17)-17};
}
}
