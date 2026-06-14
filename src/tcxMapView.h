#pragma once

// =============================================================================
// tcxMapView.h - Node-agnostic slippy-map renderer
// =============================================================================
// The core of the addon. Knows about:
//   - Web Mercator projection (lat/lon ↔ local pixel)
//   - tile request queue + worker thread (via tcxCurl)
//   - on-disk + in-memory tile cache
//   - rendering the base tile layer at the current TrussC draw cursor
//
// Deliberately does NOT know about:
//   - the node hierarchy (so it composes inside a custom draw())
//   - markers / pins / trails (compose via your own draw after drawTiles())
//   - mouse / touch input (input is the wrapping context's job)
//
// Use MapViewNode (tcxMapViewNode.h) for the "drop me into a scene graph and
// I just work" experience.
// =============================================================================

#include "tcxOpenStreetMapTypes.h"
#include "tcxTileProvider.h"

#include <TrussC.h>     // Texture, Pixels, Vec2, drawRect, ...
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace tcx {

class MapView {
public:
    MapView();
    ~MapView();

    // ------------------------------------------------------------------ camera
    MapView& setSize(float w, float h);
    tc::Vec2 size() const { return {sizeW_, sizeH_}; }
    float    width()  const { return sizeW_; }
    float    height() const { return sizeH_; }

    MapView& setCenter(LatLon p);
    MapView& setZoom(double z);
    LatLon center() const { return {centerLat_, centerLon_}; }
    double zoom()   const { return zoom_; }
    int    minZoom() const { return minZoom_; }
    int    maxZoomLimit() const { return maxZoom_; }
    MapView& setZoomLimits(int minZ, int maxZ) { minZoom_ = minZ; maxZoom_ = maxZ; return *this; }

    // -------------------------------------------------------------- projection
    // Origin = top-left of the (sizeW_ x sizeH_) viewport. caller's drawTiles()
    // translates from its own coordinate space before invoking us.
    tc::Vec2 latLonToPixel(LatLon p) const;
    LatLon   pixelToLatLon(tc::Vec2 px) const;

    // Bounds check + viewport corners in lat/lon. Useful for layer pruning.
    bool isVisible(LatLon p) const;
    void visibleBounds(LatLon* topLeft, LatLon* bottomRight) const;

    // ------------------------------------------------------------- pan / zoom
    // No interaction handler here — callers (MapViewNode or your own input
    // code) call these in response to mouse / touch.
    MapView& panByPixels(float dx, float dy);            // negative dx pans left
    MapView& zoomBy(double dZoom, tc::Vec2 anchorPx);    // anchored on a pixel

    // ----------------------------------------------------------- tile provider
    MapView& setTileProvider(std::shared_ptr<TileProvider> p) { provider_ = std::move(p); return *this; }
    std::shared_ptr<TileProvider> tileProvider() const { return provider_; }

    // Disk tile cache. Defaults to the OS user-cache dir (see defaultTileCacheDir),
    // because the OSM tile-usage policy explicitly asks consumers to cache and
    // not re-fetch what they already have. Pass an empty string to disable.
    //
    // Disk pruning is the application's responsibility — the addon never
    // deletes from disk. A long-running app that pans a lot will grow the
    // cache forever; either rotate the dir or sweep it yourself.
    MapView& setTileCacheDir(std::string path) { tileCacheDir_ = std::move(path); return *this; }
    const std::string& tileCacheDir() const { return tileCacheDir_; }

    // Resolved per-platform default ("" on platforms we don't recognise):
    //   macOS   ~/Library/Caches/tcxOpenStreetMap
    //   Linux   $XDG_CACHE_HOME/tcxOpenStreetMap (else ~/.cache/...)
    //   Win     %LOCALAPPDATA%\tcxOpenStreetMap\Cache
    static std::string defaultTileCacheDir();

    // In-memory texture cache cap. The addon keeps at most this many tiles
    // resident; evict-on-overflow drops whichever tile is zoom-farthest from
    // the current view. Defaults to 256 — fine for one ~1080p viewport, raise
    // for fullscreen / multi-map UIs.
    MapView& setMaxMemoryTiles(int n) { maxMemoryTiles_ = std::max(1, n); return *this; }
    int      maxMemoryTiles() const { return maxMemoryTiles_; }

    // At fractional zoom z, we render tiles at integer zoom round(z), then
    // scale to fit. This threshold controls *when* we cross to the next
    // detail level: a value of 0.5 = standard rounding (default), 0.7 = stay
    // on coarse tiles longer (fewer downloads, slightly blurrier zoom-in),
    // 0.3 = switch eagerly. Clamped to [0.05, 0.95].
    MapView& setZoomSnapThreshold(float t) {
        zoomSnap_ = std::clamp(t, 0.05f, 0.95f);
        return *this;
    }
    float zoomSnapThreshold() const { return zoomSnap_; }

    // Prefetch the viewport's tiles at the neighbouring integer zooms (z-1
    // and z+1) so a zoom gesture lands on real imagery instead of the
    // stretched-parent fallback. Off by default: z+1 alone is ~4 tiles per
    // visible tile, which is real load against the public OSM server. Visible
    // tiles always jump ahead of prefetches in the download queue.
    MapView& setPrefetchNeighborZooms(bool on) { prefetchNeighbors_ = on; return *this; }
    bool prefetchNeighborZooms() const { return prefetchNeighbors_; }

    // ----------------------------------------------------------------- pump
    // Must run once per frame so worker-thread tile loads land in the texture
    // cache and a new worker spins up if anything's queued.
    void update();

    // ------------------------------------------------------------- rendering
    // Draws the base tile layer at the current TrussC draw cursor / matrix —
    // (0,0) is the top-left of the viewport. Caller is responsible for any
    // surrounding translate() / push() / pop().
    void drawTiles();

    // Diagnostics — useful for status overlays.
    size_t cachedTileCount() const { return tileCache_.size(); }
    size_t loadingTileCount() const {
        std::lock_guard<std::mutex> lk(tileMutex_);
        return tileLoading_.size();
    }

private:
    // --------------------------------------------------------------- camera
    float  sizeW_     = 256;
    float  sizeH_     = 256;
    double centerLat_ = 0;
    double centerLon_ = 0;
    double zoom_      = 2;
    int    minZoom_   = 1;
    int    maxZoom_   = 19;

    // --------------------------------------------------------------- tiles
    std::shared_ptr<TileProvider> provider_;
    std::string tileCacheDir_;       // populated to defaultTileCacheDir() in ctor
    int   maxMemoryTiles_ = 256;
    float zoomSnap_       = 0.5f;
    bool  prefetchNeighbors_ = false;

    std::unordered_map<TileKey, tc::Texture, TileKeyHash> tileCache_;
    std::unordered_map<TileKey, int,         TileKeyHash> tileFailed_;     // retry counter

    struct TileResult {
        TileKey key;
        tc::Pixels pixels;       // empty on failure
    };

    mutable std::mutex          tileMutex_;
    std::deque<TileKey>         tileQueue_;       // visible tiles — served first
    std::deque<TileKey>         prefetchQueue_;   // neighbour-zoom warmup
    std::unordered_set<TileKey, TileKeyHash> tileLoading_;
    std::vector<TileResult>     tileResults_;

    std::atomic<bool> tileThreadRunning_{false};
    std::atomic<bool> tileThreadStop_{false};
    std::thread tileThread_;

    void requestTile(TileKey key, bool prefetch = false);
    void prefetchZoomLevel(int z);
    void startTileThread();
    void evictOldTiles();

    // No copy — the worker thread captures `this`.
    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;
};

} // namespace tcx
