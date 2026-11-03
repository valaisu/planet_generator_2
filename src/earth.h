#pragma once

#include <cstdint>
#include <vector>

#include "camera.h"
#include "planet.h"
#include "post.h"
#include "render.h"

// The fixed "studio" every Earth render (and every stylized reference) shares, so
// generated planets and references are directly comparable pixel-for-pixel:
// a unit sphere at the origin, framed so its disk spans PLANET_FRAC of the
// (square) image, one sun, the starfield, and the pixel-art post-process.

constexpr float PLANET_FRAC = 0.84f;  // planet diameter / image size
constexpr float CAMERA_FOV = 30.0f;   // vertical FOV in degrees

// Disk-view camera; yaw_deg turns it about the pole (positive shows longitudes
// further west, i.e. --yaw 80 centres 80 W).
Camera earth_camera(float yaw_deg = 0.0f);
Background earth_background();
constexpr float AMBIENT = 0.04f;

// Single-scattering Rayleigh + Mie through the atmosphere shell around the unit
// planet, applied to `base` (what the ray sees: surface or stars). t_surface < 0
// = the ray misses the planet. `scale` sets the shell thickness: 1 = the ~6x
// exaggerated disk-view atmosphere, ~1/6 = Earth's real thickness (low orbit);
// the vertical optical depth stays Earth's either way.
// `haze` scales the in-scattered light over a surface (not the limb or sky).
// `opt` scales Earth's coefficients / heights for other worlds (off = airless).
Vec3 atmosphere(Vec3 origin, Vec3 dir, Vec3 base, float t_surface, Vec3 to_sun,
                Vec3 sun_color, float scale, float haze = 1.0f, const AtmoOptics& opt = AtmoOptics{});

// Ray through image-space point (x, y) of a w x h image (row 0 = top; pixel
// centres at +0.5).
Ray pixel_ray(const Camera& cam, float x, float y, int w, int h);
inline Ray pixel_ray(const Camera& cam, float x, float y, int size) { return pixel_ray(cam, x, y, size, size); }
