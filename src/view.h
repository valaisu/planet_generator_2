#pragma once

#include <cmath>
#include <cstdint>

#include "camera.h"
#include "earth.h"
#include "planet.h"

// What to look at: camera, image size, sun, and the per-view render knobs.
struct View {
    Camera cam;
    int width, height;
    Vec3 to_sun;                         // unit vector toward the sun
    Vec3 sun_color{1.12f, 1.12f, 1.10f};
    float gain = 1.0f;                   // camera gain on rays that hit the planet (auto exposure; not the stars)
    float ambient = AMBIENT;             // light on the night side (dims with the starlight under a glow exposure)
    float atmo_scale = 1.0f;             // 1 = exaggerated disk atmosphere, ~0.16 = real
    float rim_scale = 0.0f;              // > 0: rays missing the planet see this shell instead (limb glow)
    float haze = 1.0f;                   // scales the haze over the ground (not the limb/sky)
    float relief = 25.0f;                // relief-normal slope exaggeration
    bool detail = false;                 // render-time detail (coasts, relief, cloud tops)
    float pixel_km = 2.5f;               // ground size of a pixel (city lights blur to it)
    float pixel_rad = 0.0f;              // angular size of a pixel (LEO: river line width)
    float sky_turn = 0.0f;               // turns the starfield lookup about +y (rad; --spin: slow sky)
};

// The reference framing: the whole disk, sun front-right (like earth_2).
// `sun_az_deg` turns the sun further around (180 = the night side faces us).
View disk_view(int size, float yaw_deg, float sun_az_deg = 0.0f);

// Low Earth orbit: looking ahead and down toward the horizon. NaN lat/lon/heading
// = pick a seeded coastline / random heading. The sun sits `sun_elev_deg` above
// the horizon at the look point, off to the front-right.
struct LeoParams {
    float alt_km = 400.0f;
    float lat = NAN, lon = NAN, heading = NAN;  // degrees; heading 0 = north
    float sun_elev_deg = 50.0f;
    float along_km = 0.0f;  // fly-along: camera moved this far ahead (sun stays fixed in space)
};
View leo_view(const Planet& p, const LeoParams& prm, int width, int height, uint32_t seed);
