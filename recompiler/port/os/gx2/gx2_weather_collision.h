#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace gx2weather {
struct Point { float x=0,y=0,z=0; };
inline Point operator+(Point a,Point b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Point operator*(Point a,float b) { return {a.x*b,a.y*b,a.z*b}; }
inline float dot(Point a,Point b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Point cross(Point a,Point b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
struct RoofTriangle { Point a,b,c; };
struct RainCover {
    std::vector<float> heights;
    std::array<float,4> mapping{}; // minimum x/z, reciprocal span in wind-projected space
    unsigned size=0;
};
inline uint32_t be32(std::span<const uint8_t> b,size_t at) {
    return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];
}
inline uint16_t be16(std::span<const uint8_t> b,size_t at) { return uint16_t(b[at])<<8|b[at+1]; }
inline bool unpack_yaz(std::span<const uint8_t> source,std::vector<uint8_t>& out) {
    out.clear();
    if(source.size()<16 || std::memcmp(source.data(),"Yaz0",4)) return false;
    const size_t size=be32(source,4);
    if(!size || size>(128u<<20)) return false;
    out.reserve(size); size_t at=16;
    while(out.size()<size) {
        if(at>=source.size()) return false;
        const uint8_t flags=source[at++];
        for(int bit=7;bit>=0 && out.size()<size;--bit) {
            if(flags&(1<<bit)) {
                if(at>=source.size()) return false;
                out.push_back(source[at++]);
            } else {
                if(at+2>source.size()) return false;
                const unsigned pair=be16(source,at); at+=2;
                unsigned length=pair>>12;
                if(!length) { if(at>=source.size())return false; length=source[at++]+18; }
                else length+=2;
                const size_t distance=(pair&4095)+1;
                if(distance>out.size() || length>size-out.size()) return false;
                for(unsigned i=0;i<length;++i) out.push_back(out[out.size()-distance]);
            }
        }
    }
    return true;
}
inline std::span<const uint8_t> archive_member(std::span<const uint8_t> b,std::string_view name) {
    if(b.size()<32 || std::memcmp(b.data(),"SARC",4) || be16(b,6)!=0xFEFF) return {};
    const size_t header=be16(b,4), data=be32(b,12);
    if(header<20 || header>b.size()-12 || std::memcmp(b.data()+header,"SFAT",4) || data>b.size()) return {};
    const size_t count=be16(b,header+6), nodes=header+12;
    if(count>(b.size()-nodes)/16) return {};
    const size_t strings=nodes+count*16;
    if(strings>b.size()-8 || std::memcmp(b.data()+strings,"SFNT",4)) return {};
    for(size_t i=0;i<count;++i) {
        const size_t node=nodes+i*16; const auto attributes=be32(b,node+4);
        if((attributes>>24)!=1) continue;
        const size_t at=strings+8+size_t(attributes&0xFFFFFF)*4;
        if(at>=data) continue;
        const auto* end=static_cast<const uint8_t*>(std::memchr(b.data()+at,0,data-at));
        if(!end || std::string_view(reinterpret_cast<const char*>(b.data()+at),end-(b.data()+at))!=name) continue;
        const size_t first=be32(b,node+8),last=be32(b,node+12);
        if(first>last || last>b.size()-data) return {};
        return b.subspan(data+first,last-first);
    }
    return {};
}
// KCL v2 stores triangles as one position, four plane normals and a prism length.
inline bool collision_triangles(std::span<const uint8_t> b,std::vector<RoofTriangle>& out) {
    out.clear();
    if(b.size()<56 || be32(b,0)!=0x02020000) return false;
    const size_t table=be32(b,8), models=be32(b,12);
    if(!models || models>64 || table>b.size() || models>(b.size()-table)/4) return false;
    auto number=[&](size_t at) { return std::bit_cast<float>(be32(b,at)); };
    auto point=[&](size_t at) { return Point{number(at),number(at+4),number(at+8)}; };
    auto valid=[](Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})<1e6f; };
    for(size_t model=0;model<models;++model) {
        const size_t base=be32(b,table+model*4);
        if(base>b.size() || b.size()-base<60) return false;
        const size_t positions=be32(b,base), normals=be32(b,base+4), faces=be32(b,base+8), end=be32(b,base+12);
        if(positions<56 || normals<positions || faces<normals || end<faces || end>b.size()-base ||
            (normals-positions)%12 || (faces-normals)%12 || (end-faces)%20) return false;
        const size_t position_count=(normals-positions)/12, normal_count=(faces-normals)/12;
        if(out.size()+(end-faces)/20>1000000) return false;
        for(size_t at=base+faces;at<base+end;at+=20) {
            const float length=number(at);
            const uint16_t pi=be16(b,at+4), di=be16(b,at+6), ai=be16(b,at+8), bi=be16(b,at+10), ci=be16(b,at+12);
            if(pi>=position_count || std::max({di,ai,bi,ci})>=normal_count || !std::isfinite(length)) return false;
            if(length<=0) continue;
            const Point a=point(base+positions+pi*12), n=point(base+normals+di*12), nc=point(base+normals+ci*12);
            const Point ab=cross(point(base+normals+bi*12),n), ac=cross(point(base+normals+ai*12),n);
            const float db=dot(ab,nc), dc=dot(ac,nc);
            if(!valid(a) || !valid(n) || !valid(nc) || !valid(ab) || !valid(ac)) return false;
            if(std::abs(db)<1e-8f || std::abs(dc)<1e-8f) continue;
            const Point v1=a+ab*(length/db),v2=a+ac*(length/dc);
            if(!valid(v1) || !valid(v2)) return false;
            out.push_back({a,v1,v2});
        }
    }
    return !out.empty();
}
inline RainCover rain_cover(std::span<const RoofTriangle> triangles,unsigned size=1024) {
    RainCover cover;
    if(triangles.empty() || size<4 || size>2048) return cover;
    // The projection follows the drop's fixed wind vector (18,-140,5), not the camera.
    auto project=[](Point p) { return Point{p.x+p.y*(18.f/140.f),p.y,p.z+p.y*(5.f/140.f)}; };
    float minx=1e6f,minz=1e6f,maxx=-1e6f,maxz=-1e6f;
    for(const auto& t:triangles) for(Point p:{t.a,t.b,t.c}) {
        if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return cover;
        p=project(p); minx=std::min(minx,p.x);minz=std::min(minz,p.z);maxx=std::max(maxx,p.x);maxz=std::max(maxz,p.z);
    }
    if(maxx-minx<.01f || maxz-minz<.01f) return cover;
    cover.mapping={minx-1,minz-1,1.f/(maxx-minx+2),1.f/(maxz-minz+2)};
    cover.size=size;cover.heights.assign(size_t(size)*size,-100000.f);
    for(const auto& t:triangles) {
        Point a=project(t.a),b=project(t.b),c=project(t.c);
        for(Point* p:{&a,&b,&c}) {p->x=(p->x-cover.mapping[0])*cover.mapping[2]*size; p->z=(p->z-cover.mapping[1])*cover.mapping[3]*size;}
        const float determinant=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
        if(std::abs(determinant)<1e-7f) continue;
        const int x0=std::clamp(int(std::floor(std::min({a.x,b.x,c.x}))),0,int(size)-1), x1=std::clamp(int(std::ceil(std::max({a.x,b.x,c.x}))),0,int(size)-1);
        const int z0=std::clamp(int(std::floor(std::min({a.z,b.z,c.z}))),0,int(size)-1), z1=std::clamp(int(std::ceil(std::max({a.z,b.z,c.z}))),0,int(size)-1);
        for(int z=z0;z<=z1;++z) for(int x=x0;x<=x1;++x) {
            const float u=((b.z-c.z)*(x+.5f-c.x)+(c.x-b.x)*(z+.5f-c.z))/determinant;
            const float v=((c.z-a.z)*(x+.5f-c.x)+(a.x-c.x)*(z+.5f-c.z))/determinant;
            if(u<-.00001f || v<-.00001f || u+v>1.00001f) continue;
            auto& height=cover.heights[size_t(z)*size+x];height=std::max(height,u*a.y+v*b.y+(1-u-v)*c.y);
        }
    }
    return cover;
}
}
