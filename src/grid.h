#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "vec3.h"

// Cube-sphere grid: 6 faces x N x N cells, each face gnomonically projected onto
// the unit sphere with the S2 tangent warp (x = tan(s*pi/4)) so cell areas vary by
// only ~1.4x instead of ~5x. Every simulation field is a std::vector over cells;
// the renderer samples fields bilinearly by direction (no mesh, no seams to speak of).
//
// Conventions: +y is the north pole; latitude = asin(dir.y); longitude 0 faces +z
// (the camera), increasing toward +x.
class CubeGrid {
public:
    explicit CubeGrid(int n = 256);

    int n() const { return n_; }
    int size() const { return 6 * n_ * n_; }
    int index(int f, int i, int j) const { return (f * n_ + j) * n_ + i; }

    Vec3 dir(int c) const { return dir_[c]; }      // unit cell-centre direction
    float area(int c) const { return area_[c]; }   // fraction of the sphere (sums to 1)
    float spacing() const { return spacing_; }     // mean neighbour distance (radians)
    // 8-neighbourhood (4 edge neighbours first, then diagonals). Crosses faces.
    const std::array<int, 8>& nbr(int c) const { return nbr_[c]; }

    // Cell nearest a direction.
    int cell_of(Vec3 d) const;
    // Bilinear sample of a per-cell field at a direction.
    float sample(const std::vector<float>& field, Vec3 d) const;
    Vec3 sample(const std::vector<Vec3>& field, Vec3 d) const;

    // Least-squares tangent gradient (per radian) of a field at a cell, fitted
    // over the 8-neighbourhood (robust to the irregular cells at face seams).
    Vec3 gradient(const std::vector<float>& field, int c) const;
    // Divergence (per radian) of a tangent vector field at a cell.
    float divergence(const std::vector<Vec3>& field, int c) const;
    // Weights of a least-squares quadratic fit over the 8-neighbourhood in local
    // tangent coordinates: for f, with df_k = f[nbr k] - f[c],
    //   grad f = east * sum gx_k df_k + north * sum gy_k df_k,
    //   lap f  = sum lap_k df_k   (Laplace-Beltrami; curvature comes out right).
    // Gradient and Laplacian from one fit are mutually consistent (fixed stencils
    // break across cube faces). False if degenerate.
    struct QuadFit { std::array<float, 8> gx, gy, lap; Vec3 east, north; };
    bool quadratic_fit(int c, QuadFit& out) const;

    // Local east/north unit vectors at a direction (tangent plane).
    static void tangent_basis(Vec3 d, Vec3& east, Vec3& north);

private:
    // Continuous face coordinates of a direction: face + cell-space (ci, cj).
    void locate(Vec3 d, int& f, float& ci, float& cj) const;
    // Cell (f, i, j), where i/j may step one past the face edge (wraps onto the
    // neighbouring face by projection).
    int cell_at(int f, int i, int j) const;
    Vec3 face_dir(int f, float ci, float cj) const;

    template <class T>
    T sample_impl(const std::vector<T>& field, Vec3 d) const;

    int n_;
    float spacing_ = 0.0f;
    std::vector<Vec3> dir_;
    std::vector<float> area_;
    std::vector<std::array<int, 8>> nbr_;
};

// Area-weighted value below which `frac` of the sphere lies.
float area_percentile(const CubeGrid& g, const std::vector<float>& field, float frac);

// Multi-source geodesic distance (radians) over the 8-neighbourhood (Dijkstra).
// `nearest` (optional) receives each cell's closest source. `allow(from, to)`
// (optional) gates which steps are permitted, e.g. to stay within one plate.
std::vector<float> distance_from(const CubeGrid& g, const std::vector<int>& sources,
                                 std::vector<int>* nearest = nullptr,
                                 const std::function<bool(int, int)>& allow = nullptr);
