#pragma once

#include <vector>

#include "planet.h"

// Shading normals from the elevation gradient (land only; the sea surface is
// flat). `exaggeration` scales slopes so relief reads at planet scale.
std::vector<Vec3> relief_normals(const Planet& p, float exaggeration);

// Surface albedo per cell from the simulated fields, as two layers so the
// renderer can place the coastline itself (sharper than the grid):
//  - land: smooth Whittaker-style blend over biome anchors in (temperature,
//    log precipitation), photo-calibrated and saturated, with noise inside each
//    biome; permanent ice/snow where annual mean T is low (incl. via lapse rate);
//  - ocean: by depth (abyss -> shelf), sea ice where frozen; `warm` (0..1) says
//    how much very shallow water turns turquoise (tropical banks/reefs) — the
//    renderer applies it with its own sub-grid depth, see shallow_water();
//  - coasts: `sand` (low, gentle, not frozen, drier = sandier, varying along the
//    shore) drives beaches and cyan water over a sand bottom; `mud` = sediment
//    plumes off big river mouths.
// Both are filled for every cell (land colours offshore come from the sea-level
// climate there) so bilinear sampling near coasts never blends the two.
struct SurfaceColors {
    std::vector<Vec3> land, ocean;
    std::vector<float> warm;
    std::vector<float> sand;  // 0..1: sandy coast (beach + cyan shallows) vs rocky/muddy
    std::vector<float> mud;   // 0..1: river sediment plume in the sea near big mouths
    std::vector<float> coast_km;  // distance to the other side of the coastline
    Vec3 at(const Planet& p, int c) const { return p.elevation[c] > 0.0f ? land[c] : ocean[c]; }
};
SurfaceColors surface_albedo(const Planet& p, uint32_t seed);
// Sea colour at depth h (m, < 0): turquoise over bright bottoms (tropical banks
// with `warm`, sand bottoms with `sand`), silty brown-green with `mud`.
Vec3 shallow_water(Vec3 sea, float h, float warm, float sand, float mud);
// Pale beach sand (land colour on sandy shores).
extern const Vec3 BEACH_SAND;
