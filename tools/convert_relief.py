"""Convert GMT earth_relief_01d_g.grd (netCDF-4) to a raw equirect grid for earthgen.

Output: int32 width, int32 height, then float32 elevation in metres, row 0 = north
pole, column 0 = longitude -180, gridline registered (inclusive endpoints).
"""
import sys

import h5py
import numpy as np

src, dst = sys.argv[1], sys.argv[2]
with h5py.File(src, "r") as f:
    lat = f["lat"][:]
    lon = f["lon"][:]
    z = f["z"][:].astype(np.float32) * float(f["z"].attrs.get("scale_factor", [1.0])[0])
    print("lat", lat[0], "..", lat[-1], "lon", lon[0], "..", lon[-1], "z", z.shape, z.min(), z.max())
if lat[0] < lat[-1]:  # make row 0 = north
    z = z[::-1]
h, w = z.shape
with open(dst, "wb") as out:
    np.array([w, h], dtype=np.int32).tofile(out)
    z.tofile(out)
print("wrote", dst, w, "x", h)
