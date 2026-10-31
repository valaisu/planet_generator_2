#pragma once

#include <vector>

#include "render.h"
#include "surface.h"
#include "view.h"

// Everything the shader samples: the planet's fields + derived colours/normals.
struct Scene {
    const Planet& planet;
    const SurfaceColors& colors;
    const std::vector<Vec3>& relief;  // relief_normals at the view's exaggeration
    Background bg;
    uint32_t seed;
};

enum class Seen { Space = 0, Ocean, Land, Cloud, Lights, Coast };  // Coast = beach / cyan shallows

struct Shaded {
    Vec3 color;
    Seen what;      // dominant thing the ray saw (drives the palette budget)
};

// Shade one camera ray: ground (coast + relief, lit, shadowed by clouds), a cloud
// layer ~8 km up composited over it, stars on a miss, then the atmosphere.
Shaded shade(const Scene& s, const View& v, const Ray& r);
