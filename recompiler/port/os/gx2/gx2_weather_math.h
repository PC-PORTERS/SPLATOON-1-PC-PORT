#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace gx2weather {
inline bool scene_color_extent(uint32_t width,uint32_t height,bool plaza) {
    return width>=(plaza?1000u:640u) && height>=(plaza?600u:360u);
}
inline bool matching_scene_aspect(uint32_t width,uint32_t height,uint32_t depth_width,uint32_t depth_height) {
    return width && height && depth_width && depth_height &&
           std::abs(double(depth_width)*height-double(depth_height)*width)<=.02*double(width)*depth_height;
}
inline uint64_t mesh_hash(const void* bytes, size_t size) {
    uint64_t hash=14695981039346656037ull;
    const auto* p=static_cast<const uint8_t*>(bytes);
    for(size_t i=0;i<size;++i) { hash^=p[i]; hash*=1099511628211ull; }
    return hash;
}
// AGMP v16's VR_Sky sphere, verified against the loaded mesh and original models.
// A distance cutoff would also replace distant buildings.
inline bool is_sky_mesh(uint64_t hash, uint32_t bytes, uint32_t stride, uint32_t count) {
    return hash==0x821FD18E81E01923ull && bytes==7024 && stride==16 && count==2280;
}
struct Camera {
    std::array<float,12> inverse_view{};
    std::array<float,4> projection{}; // horizontal/vertical scale, near, far
    std::array<float,2> depth_range{0,1};
};
inline float cumulus_coverage(int percent,bool after_rain,bool enabled) {
    return enabled ? (percent<0 ? (after_rain?.9f:.55f) : std::clamp(percent,0,100)*.01f) : 0.f;
}
struct CloudClock {
    double seconds=0,last=-1;
    double advance(double now,int speed) {
        if(!std::isfinite(now))return seconds;
        const double step=now-last;
        if(last>=0 && step>0 && step<2)seconds+=step*std::clamp(speed,0,400)*.01;
        last=now;return seconds;
    }
};
// The identified main camera is authoritative even when only the sky is drawn;
// an old, still-bound shader bank need not belong to the current view.
inline bool look_at_camera(const float* pos,const float* at,const float* up,float fovy,float near_z,float far_z,float aspect,Camera& out) {
    if(!pos || !at || !up || !std::isfinite(fovy) || !std::isfinite(near_z) || !std::isfinite(far_z) || !std::isfinite(aspect) ||
       fovy<5 || fovy>175 || near_z<=.01f || far_z<=near_z || far_z>1e7f || aspect<.1f || aspect>10)return false;
    for(unsigned i=0;i<3;++i)for(float v:{pos[i],at[i],up[i]})if(!std::isfinite(v) || std::abs(v)>1e7f)return false;
    auto norm=[](std::array<float,3>& v){const float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(l<1e-6f)return false;for(auto& x:v)x/=l;return true;};
    std::array<float,3> back{pos[0]-at[0],pos[1]-at[1],pos[2]-at[2]};if(!norm(back))return false;
    std::array<float,3> right{up[1]*back[2]-up[2]*back[1],up[2]*back[0]-up[0]*back[2],up[0]*back[1]-up[1]*back[0]};
    if(!norm(right))return false;
    const std::array<float,3> vertical{back[1]*right[2]-back[2]*right[1],back[2]*right[0]-back[0]*right[2],back[0]*right[1]-back[1]*right[0]};
    Camera result;
    for(unsigned i=0;i<3;++i){result.inverse_view[i*4]=right[i];result.inverse_view[i*4+1]=vertical[i];result.inverse_view[i*4+2]=back[i];result.inverse_view[i*4+3]=pos[i];}
    const float t=std::tan(fovy*.5f*.01745329252f);
    result.projection={1.f/(t*aspect),1.f/t,near_z,far_z};out=result;return true;
}
// std140 layout shared by the atmosphere passes and their GPU tests.
struct alignas(16) WeatherParams {
    float cam0[4], cam1[4], cam2[4], proj[4], depth_range[4], sun[4], moon[4], pole[4];
    float exposure[4], clouds[4], wind[4], target[4], dome[4], effects[4], cover[4], storm[4], post[4], sway[4], look[4], prev0[4], prev1[4], prev2[4], taa[4], hero[4], picture[4], extra[4], prev_proj[4], cinema[4], cloud_control[4],grade_tools[4];
};
static_assert(sizeof(WeatherParams)==30*16);
// AGMP v16's view block: VP, view 3x4, inverse view 3x4, near/far.
// GPU uniform blocks contain native little-endian floats, unlike guest scalars.
inline bool decode_camera(const void* bytes, size_t size, Camera& out) {
    if (!bytes || size<176) return false;
    float f[44]; std::memcpy(f,bytes,sizeof f);
    for (float v:f) if (!std::isfinite(v) || std::abs(v)>1e7f) return false;
    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) {
        float dot=0;
        for (unsigned k=0;k<3;++k) dot+=f[16+r*4+k]*f[28+k*4+c];
        if (std::abs(dot-(r==c?1.f:0.f))>.003f) return false;
    }
    for (unsigned r=0;r<3;++r) {
        float t=f[19+r*4];
        for (unsigned k=0;k<3;++k) t+=f[16+r*4+k]*f[31+k*4];
        if (std::abs(t)>.05f) return false;
    }
    float x=0,y=0;
    for (unsigned k=0;k<3;++k) { x+=f[k]*f[16+k]; y+=f[4+k]*f[20+k]; }
    if (x<.1f || x>10 || y<.1f || y>10 || f[40]<=.01f || f[41]<100) return false;
    for (unsigned k=0;k<3;++k) if (std::abs(f[12+k]+f[24+k])>.003f) return false;
    std::copy_n(f+28,12,out.inverse_view.begin());
    out.projection={x,y,f[40],f[40]+f[41]};
    return true;
}
inline double day_hour(int minutes, bool cycle, int duration_minutes, double seconds) {
    const double hour=minutes/60.0+(cycle?std::max(0.0,seconds)*24.0/(std::max(1,duration_minutes)*60.0):0);
    return hour-std::floor(hour/24.0)*24.0;
}
inline std::array<float,3> sun_direction(double hour) {
    const double a=(hour-6)*6.283185307179586/24;
    const double x=.25*std::sin(a), y=.9*std::sin(a), z=std::cos(a);
    const double length=std::sqrt(x*x+y*y+z*z);
    return {float(x/length),float(y/length),float(z/length)};
}
}
