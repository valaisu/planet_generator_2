#include <algorithm>
#include <cmath>
#include <numeric>

#include "grid.h"

namespace {
constexpr float PI = 3.14159265358979323846f;

// Face frames: normal axis, u axis (cell i), v axis (cell j).
const Vec3 AXIS[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const Vec3 U[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
const Vec3 V[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
}  // namespace

CubeGrid::CubeGrid(int n) : n_(n) {
    const int total = size();
    dir_.resize(total);
    for (int f = 0; f < 6; ++f)
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
                dir_[index(f, i, j)] = face_dir(f, static_cast<float>(i), static_cast<float>(j));

    nbr_.resize(total);
    for (int f = 0; f < 6; ++f)
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
                nbr_[index(f, i, j)] = {cell_at(f, i - 1, j), cell_at(f, i + 1, j),
                                        cell_at(f, i, j - 1), cell_at(f, i, j + 1),
                                        cell_at(f, i - 1, j - 1), cell_at(f, i + 1, j - 1),
                                        cell_at(f, i - 1, j + 1), cell_at(f, i + 1, j + 1)};

    // Area from the half-way spans to the edge neighbours; normalised to sum 1.
    area_.resize(total);
    double sum = 0.0, dsum = 0.0;
    for (int c = 0; c < total; ++c) {
        const auto& nb = nbr_[c];
        float a = 0.25f * length(dir_[nb[1]] - dir_[nb[0]]) * length(dir_[nb[3]] - dir_[nb[2]]);
        area_[c] = a;
        sum += a;
        dsum += length(dir_[nb[1]] - dir_[c]);
    }
    for (float& a : area_) a = static_cast<float>(a / sum);
    spacing_ = static_cast<float>(dsum / total);
}

Vec3 CubeGrid::face_dir(int f, float ci, float cj) const {
    float s = 2.0f * (ci + 0.5f) / n_ - 1.0f;
    float t = 2.0f * (cj + 0.5f) / n_ - 1.0f;
    float x = std::tan(s * PI * 0.25f), y = std::tan(t * PI * 0.25f);
    return normalize(AXIS[f] + U[f] * x + V[f] * y);
}

namespace {
// atan for |x| <= 1 (face coordinates), max error ~1e-5 rad (~0.002 cell at
// N=256); std::atan dominated sampling cost. Falls back outside the range.
inline float fast_atan(float x) {
    if (std::fabs(x) > 1.0f) return std::atan(x);
    float x2 = x * x;
    return x * (0.99997726f + x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f
           + x2 * (0.05265332f - 0.01172120f * x2)))));
}
}  // namespace

void CubeGrid::locate(Vec3 d, int& f, float& ci, float& cj) const {
    float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    if (ax >= ay && ax >= az) f = d.x > 0 ? 0 : 1;
    else if (ay >= az)        f = d.y > 0 ? 2 : 3;
    else                      f = d.z > 0 ? 4 : 5;
    float w = dot(d, AXIS[f]);
    float x = dot(d, U[f]) / w, y = dot(d, V[f]) / w;
    float s = fast_atan(x) * 4.0f / PI, t = fast_atan(y) * 4.0f / PI;
    ci = (s + 1.0f) * 0.5f * n_ - 0.5f;
    cj = (t + 1.0f) * 0.5f * n_ - 0.5f;
}

int CubeGrid::cell_of(Vec3 d) const {
    int f;
    float ci, cj;
    locate(d, f, ci, cj);
    int i = std::clamp(static_cast<int>(std::lround(ci)), 0, n_ - 1);
    int j = std::clamp(static_cast<int>(std::lround(cj)), 0, n_ - 1);
    return index(f, i, j);
}

int CubeGrid::cell_at(int f, int i, int j) const {
    if (i >= 0 && i < n_ && j >= 0 && j < n_) return index(f, i, j);
    // Off the face: extrapolate the face projection and snap to the nearest cell.
    return cell_of(face_dir(f, static_cast<float>(i), static_cast<float>(j)));
}

template <class T>
T CubeGrid::sample_impl(const std::vector<T>& field, Vec3 d) const {
    int f;
    float ci, cj;
    locate(d, f, ci, cj);
    int i0 = static_cast<int>(std::floor(ci)), j0 = static_cast<int>(std::floor(cj));
    float fx = ci - i0, fy = cj - j0;
    T a = field[cell_at(f, i0, j0)], b = field[cell_at(f, i0 + 1, j0)];
    T c = field[cell_at(f, i0, j0 + 1)], e = field[cell_at(f, i0 + 1, j0 + 1)];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + e * fx) * fy;
}

float CubeGrid::sample(const std::vector<float>& field, Vec3 d) const { return sample_impl(field, d); }
Vec3 CubeGrid::sample(const std::vector<Vec3>& field, Vec3 d) const { return sample_impl(field, d); }

namespace {
// Fit (gx, gy) minimising sum (df_k - g . t_k)^2 over neighbour offsets t_k in a
// local (east, north) frame.
template <class F>
void lsq_fit(const CubeGrid& g, int c, F&& df, float& gx, float& gy) {
    Vec3 d = g.dir(c), e, n;
    CubeGrid::tangent_basis(d, e, n);
    float a11 = 0, a12 = 0, a22 = 0, b1 = 0, b2 = 0;
    for (int nb : g.nbr(c)) {
        if (nb == c) continue;
        Vec3 t = g.dir(nb) - d;
        float x = dot(t, e), y = dot(t, n), f = df(nb);
        a11 += x * x; a12 += x * y; a22 += y * y;
        b1 += x * f; b2 += y * f;
    }
    float det = a11 * a22 - a12 * a12;
    if (std::fabs(det) < 1e-20f) { gx = gy = 0.0f; return; }
    gx = (a22 * b1 - a12 * b2) / det;
    gy = (a11 * b2 - a12 * b1) / det;
}
}  // namespace

Vec3 CubeGrid::gradient(const std::vector<float>& f, int c) const {
    float gx, gy;
    lsq_fit(*this, c, [&](int nb) { return f[nb] - f[c]; }, gx, gy);
    Vec3 e, n;
    tangent_basis(dir_[c], e, n);
    return e * gx + n * gy;
}

float CubeGrid::divergence(const std::vector<Vec3>& w, int c) const {
    Vec3 e, n;
    tangent_basis(dir_[c], e, n);
    float ex, ey, nx, ny;  // d(w.e)/dx, d(w.e)/dy, d(w.n)/dx, d(w.n)/dy
    lsq_fit(*this, c, [&](int nb) { return dot(w[nb] - w[c], e); }, ex, ey);
    lsq_fit(*this, c, [&](int nb) { return dot(w[nb] - w[c], n); }, nx, ny);
    (void)ey; (void)nx;
    return ex + ny;
}

bool CubeGrid::quadratic_fit(int c, QuadFit& out) const {
    Vec3 d = dir(c), e, n;
    CubeGrid::tangent_basis(d, e, n);
    double A[5][5] = {}, rows[8][5];
    for (int k = 0; k < 8; ++k) {
        int nb = nbr(c)[k];
        Vec3 t = dir(nb) - d;
        double x = dot(t, e), y = dot(t, n);
        double r[5] = {x, y, 0.5 * x * x, x * y, 0.5 * y * y};
        for (int i = 0; i < 5; ++i) rows[k][i] = nb != c ? r[i] : 0.0;
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j) A[i][j] += rows[k][i] * rows[k][j];
    }
    // Solve A Z = [l_gx l_gy l_lap] (A symmetric), then weight_k = Z^T row_k.
    double M[5][8];
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) M[i][j] = A[i][j];
        M[i][5] = i == 0;                  // d/dx
        M[i][6] = i == 1;                  // d/dy
        M[i][7] = (i == 2 || i == 4);      // Hxx + Hyy
    }
    for (int col = 0; col < 5; ++col) {
        int piv = col;
        for (int r = col + 1; r < 5; ++r) if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
        if (std::fabs(M[piv][col]) < 1e-30) return false;
        for (int j = 0; j < 8; ++j) std::swap(M[col][j], M[piv][j]);
        for (int r = 0; r < 5; ++r) {
            if (r == col) continue;
            double f = M[r][col] / M[col][col];
            for (int j = col; j < 8; ++j) M[r][j] -= f * M[col][j];
        }
    }
    for (int k = 0; k < 8; ++k) {
        double gx = 0, gy = 0, lap = 0;
        for (int i = 0; i < 5; ++i) {
            gx += M[i][5] / M[i][i] * rows[k][i];
            gy += M[i][6] / M[i][i] * rows[k][i];
            lap += M[i][7] / M[i][i] * rows[k][i];
        }
        out.gx[k] = static_cast<float>(gx);
        out.gy[k] = static_cast<float>(gy);
        out.lap[k] = static_cast<float>(lap);
    }
    out.east = e;
    out.north = n;
    return true;
}

void CubeGrid::tangent_basis(Vec3 d, Vec3& east, Vec3& north) {
    Vec3 e = cross(Vec3(0, 1, 0), d);
    float l = length(e);
    east = (l > 1e-6f) ? e / l : Vec3(1, 0, 0);
    north = cross(d, east);
}

float area_percentile(const CubeGrid& g, const std::vector<float>& field, float frac) {
    std::vector<int> order(field.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return field[a] < field[b]; });
    double acc = 0.0;
    for (int c : order) {
        acc += g.area(c);
        if (acc >= frac) return field[c];
    }
    return field[order.back()];
}

#include <queue>

std::vector<float> distance_from(const CubeGrid& g, const std::vector<int>& sources,
                                 std::vector<int>* nearest,
                                 const std::function<bool(int, int)>& allow) {
    const float INF = 1e9f;
    std::vector<float> dist(g.size(), INF);
    if (nearest) nearest->assign(g.size(), -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (int s : sources) {
        dist[s] = 0.0f;
        if (nearest) (*nearest)[s] = s;
        pq.push({0.0f, s});
    }
    while (!pq.empty()) {
        auto [d, c] = pq.top();
        pq.pop();
        if (d > dist[c]) continue;
        for (int nb : g.nbr(c)) {
            if (allow && !allow(c, nb)) continue;
            float nd = d + length(g.dir(nb) - g.dir(c));
            if (nd < dist[nb]) {
                dist[nb] = nd;
                if (nearest) (*nearest)[nb] = (*nearest)[c];
                pq.push({nd, nb});
            }
        }
    }
    return dist;
}
