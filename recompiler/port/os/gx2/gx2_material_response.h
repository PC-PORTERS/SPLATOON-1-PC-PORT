#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace gx2material {
// AGMP v16: names, texture identities and the bound FMAT parameter offsets
// were verified in the original resource and a live plaza material scan.
struct Response {
    uint64_t albedo_hash;
    std::array<float,3> original_albedo, original_specular, albedo, specular;
    float original_intensity, original_roughness, intensity, roughness, normal_weight;
};
inline constexpr Response responses[]={
    {0xfa033aacc6c5430dull,{.30f,.32f,0},{.98f,.98f,.22f},{.35f,.28f,.12f},{.65f,.66f,.64f},.4f,.5f,.30f,.62f,.7f},
    {0x64a69a11cbe244d2ull,{.53f,.57f,0},{.98f,.98f,.22f},{.58f,.50f,.20f},{.65f,.66f,.64f},.4f,.75f,.30f,.68f,.7f},
    {0x7d7003200cb4e2ddull,{.84f,1,1},{.9f,1,1},{.84f,.94f,.93f},{.8f,.8f,.8f},1,.46f,.7f,.65f,.8f},
    {0x3f5b42d3a7972341ull,{.44f,.48f,.44f},{1,1,1},{.48f,.49f,.47f},{1,1,1},.54f,.54f,.40f,.75f,.75f},
};
inline const Response* response(uint64_t hash){for(const auto& r:responses)if(r.albedo_hash==hash)return &r;return nullptr;}
inline float value(std::span<const uint8_t> bytes,size_t at){float f;std::memcpy(&f,bytes.data()+at,4);return f;}
inline unsigned layout(std::span<const uint8_t> bytes,const Response& r){
    auto same=[&](size_t at,float expected){const float v=value(bytes,at);return std::isfinite(v) && std::abs(v-expected)<1e-5f;};
    for(unsigned scale:{1u,4u}){
        if(bytes.size()<420*scale+4)continue;
        bool match=true;
        for(unsigned i=0;i<3;++i)match=match && same(240*scale+i*4,r.original_albedo[i]) && same(368*scale+i*4,r.original_specular[i]);
        if(match && same(380*scale,r.original_intensity) && same(384*scale,r.original_roughness) && same(420*scale,.5f))return scale;
    }
    return 0;
}
inline bool matches(std::span<const uint8_t> bytes,const Response& r){return layout(bytes,r)!=0;}
inline bool apply_range(std::span<uint8_t> bytes,size_t source_offset,const Response& r,unsigned scale){
    if(scale!=1 && scale!=4)return false;
    bool changed=false;
    auto put=[&](size_t at,float f){if(at>=source_offset && at-source_offset<=bytes.size() && 4<=bytes.size()-(at-source_offset)){
        std::memcpy(bytes.data()+at-source_offset,&f,4);changed=true;
    }};
    for(unsigned i=0;i<3;++i){put(240*scale+i*4,r.albedo[i]);put(368*scale+i*4,r.specular[i]);}
    put(380*scale,r.intensity);put(384*scale,r.roughness);put(420*scale,r.normal_weight);
    return changed;
}
inline bool apply(std::span<uint8_t> bytes,const Response& r){
    // A texture may be reused by another material or shader layout. Preserve
    // it unless every relevant original parameter matches this exact layout.
    const unsigned scale=layout(bytes,r);
    return scale && apply_range(bytes,0,r,scale);
}
}
