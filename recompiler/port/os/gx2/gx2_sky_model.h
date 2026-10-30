// gx2_sky_model.h - sun and moon positions and sunlight colour for the time
// of day, shared by the game-light driver (gx2_env_light.cpp) and the sky
// shaders (gx2_weather.cpp). World space is the plaza's: y up.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gx2sky {
using Vec3 = std::array<float, 3>;

constexpr double kPi = 3.14159265358979323846;
constexpr double kLatitude = 35.7 * kPi / 180;     // Tokyo
constexpr double kDeclination = 15.0 * kPi / 180;  // mid May: sunrise 05:15, sunset 18:45
// The plaza's own sun (plazaday.baglenv, light travel direction negated).
constexpr Vec3 kGameSun = {0.3687f, 0.766f, 0.5265f};

// Sun direction in local east/up/south axes for a solar hour.
inline std::array<double, 3> local_direction(double hour, double declination) {
    const double h = (hour - 12) * kPi / 12;
    const double east = -std::cos(declination) * std::sin(h);
    const double north = std::cos(kLatitude) * std::sin(declination) - std::sin(kLatitude) * std::cos(declination) * std::cos(h);
    const double up = std::sin(kLatitude) * std::sin(declination) + std::cos(kLatitude) * std::cos(declination) * std::cos(h);
    return {east, up, -north};
}

struct Frame {
    double reference_hour;  // the afternoon hour at which the sun stands where the game put it
    double cos_a, sin_a;    // rotation of local axes into plaza axes (about y)
};
inline const Frame& frame() {
    static const Frame f = [] {
        const double target = std::asin(double(kGameSun[1]));
        double lo = 12, hi = 18.5;
        for (int i = 0; i < 60; ++i) {
            const double mid = (lo + hi) / 2;
            (std::asin(local_direction(mid, kDeclination)[1]) > target ? lo : hi) = mid;
        }
        // Default: the sun sets in the plaza's main view, behind the tower
        // (world direction +0.10, -0.995 in xz), as in the realistic
        // reference; the evening is backlit. RBREW_SUN_FRAME=game: the game's
        // own sun stands exactly at the reference hour instead.
        const char* mode = std::getenv("RBREW_SUN_FRAME");
        double a;
        if (mode && std::strcmp(mode, "game") == 0) {
            const auto v = local_direction(lo, kDeclination);
            a = std::atan2(double(kGameSun[2]), double(kGameSun[0])) - std::atan2(v[2], v[0]);
        } else {
            const auto v = local_direction(18.6, kDeclination);
            a = std::atan2(-0.995, 0.10) - std::atan2(v[2], v[0]);
        }
        return Frame{lo, std::cos(a), std::sin(a)};
    }();
    return f;
}
inline Vec3 to_world(const std::array<double, 3>& v) {
    const Frame& f = frame();
    return {float(v[0] * f.cos_a - v[2] * f.sin_a), float(v[1]), float(v[0] * f.sin_a + v[2] * f.cos_a)};
}

struct Celestial {
    Vec3 sun, moon, pole;          // unit vectors towards the sun, the moon, the celestial pole
    float sun_elevation;           // degrees
    float moon_elevation;
    float star_angle;              // sidereal rotation of the star field, radians
};
inline Celestial celestial(double hour) {
    Celestial c;
    c.sun = to_world(local_direction(hour, kDeclination));
    // A waxing gibbous moon on a lower path, roughly opposite the sun.
    c.moon = to_world(local_direction(hour + 11.2, -0.6 * kDeclination));
    c.pole = to_world({0, std::sin(kLatitude), -std::cos(kLatitude)});
    c.sun_elevation = float(std::asin(std::clamp(double(c.sun[1]), -1.0, 1.0)) * 180 / kPi);
    c.moon_elevation = float(std::asin(std::clamp(double(c.moon[1]), -1.0, 1.0)) * 180 / kPi);
    c.star_angle = float(hour / 24 * 2 * kPi);
    return c;
}

// Relative air mass (Kasten & Young), finite below the horizon.
inline double air_mass(double elevation_deg) {
    const double e = std::max(elevation_deg, -1.0);
    return 1.0 / (std::sin(std::max(e, 0.0) * kPi / 180) + 0.50572 * std::pow(e + 6.07995, -1.6364));
}
// Direct sunlight after the atmosphere (Rayleigh + Mie + ozone), relative to
// the sun at the game's reference elevation.
inline Vec3 sun_transmittance(double elevation_deg) {
    static const double rayleigh[3] = {5.802e-6 * 8000, 13.558e-6 * 8000, 33.1e-6 * 8000};
    static const double ozone[3] = {0.650e-6 * 25000 * 0.3, 1.881e-6 * 25000 * 0.3, 0.085e-6 * 25000 * 0.3};
    constexpr double mie = 3.996e-6 * 1.11 * 1200 * 2.5;  // hazy city air
    auto t = [&](double e, int i) { return std::exp(-(rayleigh[i] + mie + ozone[i]) * air_mass(e)); };
    const double reference = std::asin(double(kGameSun[1])) * 180 / kPi;
    Vec3 out;
    for (int i = 0; i < 3; ++i) out[i] = float(t(elevation_deg, i) / t(reference, i));
    return out;
}

inline float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
}
