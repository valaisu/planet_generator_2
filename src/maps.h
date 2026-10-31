#pragma once

#include <functional>
#include <string>

#include "planet.h"

// Equirectangular debug image (lon -180..180 left->right, lat 90 top) of any
// per-direction colour function — the main way to inspect each sim stage.
bool write_map(const std::string& path, int w, const std::function<Vec3(Vec3 dir)>& color);

// Standard field views (each writes <dir>/<name>.png).
void write_planet_maps(const Planet& p, const std::string& dir);

// Shared elevation colouring (hypsometric tint), also used as a stand-in surface.
Vec3 elevation_color(float metres);
