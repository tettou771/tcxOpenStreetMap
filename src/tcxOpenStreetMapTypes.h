#pragma once

// =============================================================================
// tcxOpenStreetMapTypes.h - tile-coordinate basics + Web Mercator projection
// =============================================================================
// Pulled into its own header so anyone using the Web Mercator projection
// helpers doesn't have to drag in the whole MapView interface. The LatLon
// coordinate type itself lives in tcxGeo (shared across geo addons).
// =============================================================================

#include <tcxLatLon.h>   // tcx::LatLon (from the tcxGeo addon)

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tcx {

// -----------------------------------------------------------------------------
// TileKey — identifies one slippy-map tile (z = zoom level, x/y = tile index
// inside the 2^z grid). Hashed by callers that cache tiles.
// -----------------------------------------------------------------------------
struct TileKey {
    int z = 0;
    int x = 0;
    int y = 0;

    bool operator==(const TileKey& o) const { return z == o.z && x == o.x && y == o.y; }
};

struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const noexcept {
        // Combine into a single 64-bit number. z ≤ 22, so 5 bits is plenty;
        // x and y each up to 2^z (≤ 22) bits, so 27+27+5 ≤ 64.
        uint64_t v = (uint64_t)(k.z & 0x1F);
        v = (v << 27) | (uint64_t)(k.x & 0x07FFFFFF);
        v = (v << 27) | (uint64_t)(k.y & 0x07FFFFFF);
        return (std::size_t)v;
    }
};

// -----------------------------------------------------------------------------
// Web Mercator projection helpers.
//
// The "world" at any zoom level is a 256·2^z pixel square — these helpers go
// between world pixels and lat/lon. Almost every slippy-map renderer uses this
// (OSM, Mapbox, Google for raster tiles, etc.), so it makes sense as a
// per-addon primitive rather than buried inside MapView.
//
// Caveat: Web Mercator is undefined past ±~85.05°. The clamp uses MAX_LAT.
// -----------------------------------------------------------------------------
inline constexpr double TILE_SIZE_PX = 256.0;
inline constexpr double MAX_LAT      = 85.05112877980659;

inline double mercatorClampLat(double lat) {
    if (lat >  MAX_LAT) return  MAX_LAT;
    if (lat < -MAX_LAT) return -MAX_LAT;
    return lat;
}

// World pixel = position inside a (TILE_SIZE_PX · 2^zoom) square centered on
// lat=0/lon=0. Top-left of the world is (0,0); +x = east, +y = south.
struct WorldPixel { double x = 0, y = 0; };

inline WorldPixel latLonToWorldPixel(LatLon p, double zoom) {
    double scale = TILE_SIZE_PX * std::pow(2.0, zoom);
    double x = (p.lon + 180.0) / 360.0 * scale;
    double s = std::sin(mercatorClampLat(p.lat) * M_PI / 180.0);
    double y = (0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * M_PI)) * scale;
    return {x, y};
}

inline LatLon worldPixelToLatLon(WorldPixel wp, double zoom) {
    double scale = TILE_SIZE_PX * std::pow(2.0, zoom);
    double lon = wp.x / scale * 360.0 - 180.0;
    double n   = M_PI - 2.0 * M_PI * wp.y / scale;
    double lat = 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
    return {lat, lon};
}

} // namespace tcx
