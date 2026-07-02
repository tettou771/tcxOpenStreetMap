#include "tcxMapView.h"

#include <tcxCurl.h>     // HttpClient

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using namespace tc;

namespace tcx::openstreetmap {

MapView::MapView() {
    // Default to OSM so an out-of-the-box new MapView shows something.
    provider_ = std::make_shared<OSMTileProvider>();
    // Tile-usage policy across providers (OSM, Mapbox, …) basically requires
    // local caching of repeated requests. Pick a sensible default per OS so
    // the addon is a polite citizen even when the caller forgets to set one.
    tileCacheDir_ = defaultTileCacheDir();
}

std::string MapView::defaultTileCacheDir() {
    namespace fs = std::filesystem;
    std::string base;
#if defined(__APPLE__)
    if (const char* h = std::getenv("HOME"); h && *h)
        base = std::string(h) + "/Library/Caches/tcxOpenStreetMap";
#elif defined(_WIN32)
    if (const char* l = std::getenv("LOCALAPPDATA"); l && *l)
        base = std::string(l) + "\\tcxOpenStreetMap\\Cache";
#elif defined(__linux__) || defined(__unix__)
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x)
        base = std::string(x) + "/tcxOpenStreetMap";
    else if (const char* h = std::getenv("HOME"); h && *h)
        base = std::string(h) + "/.cache/tcxOpenStreetMap";
#endif
    if (base.empty()) return {};
    // Create lazily here so the worker thread doesn't race on the first tile
    // write. mkdir-on-existing is a no-op.
    std::error_code ec;
    fs::create_directories(base, ec);
    return base;
}

MapView::~MapView() {
    tileThreadStop_ = true;
    if (tileThread_.joinable()) {
        // Detached after start; .join would block here. Best effort: trust
        // the worker to notice tileThreadStop_ within ~one tile.
    }
}

// ---------------------------------------------------------------- camera

MapView& MapView::setSize(float w, float h) {
    sizeW_ = std::max(1.0f, w);
    sizeH_ = std::max(1.0f, h);
    return *this;
}

MapView& MapView::setCenter(LatLon p) {
    centerLat_ = mercatorClampLat(p.lat);
    centerLon_ = std::clamp(p.lon, -180.0, 180.0);
    return *this;
}

MapView& MapView::setZoom(double z) {
    zoom_ = std::clamp(z, (double)minZoom_, (double)maxZoom_);
    return *this;
}

// ------------------------------------------------------------ projection

Vec2 MapView::latLonToPixel(LatLon p) const {
    // Convert lat/lon to world pixel at the current zoom, then offset so the
    // viewport center sits at (sizeW/2, sizeH/2).
    WorldPixel target = latLonToWorldPixel(p, zoom_);
    WorldPixel center = latLonToWorldPixel({centerLat_, centerLon_}, zoom_);
    return Vec2{
        (float)(target.x - center.x + sizeW_ * 0.5),
        (float)(target.y - center.y + sizeH_ * 0.5)
    };
}

LatLon MapView::pixelToLatLon(Vec2 px) const {
    WorldPixel center = latLonToWorldPixel({centerLat_, centerLon_}, zoom_);
    WorldPixel target{
        center.x + (double)px.x - sizeW_ * 0.5,
        center.y + (double)px.y - sizeH_ * 0.5
    };
    return worldPixelToLatLon(target, zoom_);
}

bool MapView::isVisible(LatLon p) const {
    Vec2 px = latLonToPixel(p);
    return px.x >= 0 && px.x < sizeW_ && px.y >= 0 && px.y < sizeH_;
}

void MapView::visibleBounds(LatLon* topLeft, LatLon* bottomRight) const {
    if (topLeft)     *topLeft     = pixelToLatLon({0, 0});
    if (bottomRight) *bottomRight = pixelToLatLon({sizeW_, sizeH_});
}

// ------------------------------------------------------------ pan / zoom

MapView& MapView::panByPixels(float dx, float dy) {
    WorldPixel center = latLonToWorldPixel({centerLat_, centerLon_}, zoom_);
    WorldPixel next{center.x - (double)dx, center.y - (double)dy};
    LatLon ll = worldPixelToLatLon(next, zoom_);
    return setCenter(ll);
}

MapView& MapView::zoomBy(double dZoom, Vec2 anchorPx) {
    // Keep the geographic point under anchorPx fixed in pixel space across
    // the zoom change. Standard "zoom-to-cursor" math.
    LatLon anchor = pixelToLatLon(anchorPx);
    setZoom(zoom_ + dZoom);
    Vec2 anchorAfter = latLonToPixel(anchor);
    panByPixels(anchorPx.x - anchorAfter.x, anchorPx.y - anchorAfter.y);
    return *this;
}

// ----------------------------------------------------------- pump + tiles

void MapView::update() {
    {
        std::lock_guard<std::mutex> lk(tileMutex_);
        for (auto& r : tileResults_) {
            if (r.pixels.isAllocated()) {
                Texture tex;
                tex.allocate(r.pixels, TextureUsage::Immutable, false);
                tileCache_[r.key] = std::move(tex);
                tileFailed_.erase(r.key);
            } else {
                tileFailed_[r.key]++;
            }
            tileLoading_.erase(r.key);
        }
        tileResults_.clear();
    }

    // Restart the worker if there's queued work and the previous worker
    // already exited (it self-terminates when the queues drain).
    if (!tileThreadRunning_ && !tileThreadStop_) {
        std::lock_guard<std::mutex> lk(tileMutex_);
        if (!tileQueue_.empty() || !prefetchQueue_.empty()) {
            tileThreadRunning_ = true;
            startTileThread();
        }
    }

    evictOldTiles();
}

void MapView::drawTiles() {
    if (!provider_) return;

    setColor(0.12f, 0.12f, 0.14f);
    fill();
    drawRect(0, 0, sizeW_, sizeH_);

    // Pick which integer tile zoom to render. zoomSnap_ controls *when* the
    // fractional zoom crosses to the next detail level — 0.5 = standard
    // rounding, >0.5 stays on coarse longer (fewer downloads).
    double zlo = std::floor(zoom_);
    int    intZoom = std::clamp(
        (int)((zoom_ - zlo >= zoomSnap_) ? (zlo + 1.0) : zlo),
        minZoom_, maxZoom_);
    double tileScale = std::pow(2.0, zoom_ - intZoom);
    double tileSize  = TILE_SIZE_PX * tileScale;

    WorldPixel center = latLonToWorldPixel({centerLat_, centerLon_}, intZoom);
    double half_w = sizeW_ * 0.5 / tileScale;
    double half_h = sizeH_ * 0.5 / tileScale;
    double left = center.x - half_w;
    double top  = center.y - half_h;

    int maxTile = (1 << intZoom);
    int minTX = std::max(0, (int)std::floor(left / TILE_SIZE_PX));
    int maxTX = std::min(maxTile - 1, (int)std::floor((left + sizeW_ / tileScale) / TILE_SIZE_PX));
    int minTY = std::max(0, (int)std::floor(top / TILE_SIZE_PX));
    int maxTY = std::min(maxTile - 1, (int)std::floor((top + sizeH_ / tileScale) / TILE_SIZE_PX));

    for (int ty = minTY; ty <= maxTY; ty++) {
        for (int tx = minTX; tx <= maxTX; tx++) {
            double drawX = (tx * TILE_SIZE_PX - left) * tileScale;
            double drawY = (ty * TILE_SIZE_PX - top ) * tileScale;
            double drawW = tileSize;
            double drawH = tileSize;

            TileKey key{intZoom, tx, ty};
            auto it = tileCache_.find(key);
            if (it != tileCache_.end() && it->second.isAllocated()) {
                setColor(1.0f, 1.0f, 1.0f);
                it->second.draw((float)drawX, (float)drawY, (float)drawW, (float)drawH);
                continue;
            }

            // Missing tile. Display fallback: walk up the pyramid (a parent
            // tile covers our area with a smaller sub-rect) until something
            // is in cache. If that fails, try the 4 immediate children at
            // intZoom + 1 (composited) — covers the "user zoomed OUT past
            // tiles they already have" case. Only when both walks miss do
            // we drop to the dark placeholder.
            bool drewFallback = false;
            setColor(1.0f, 1.0f, 1.0f);

            // Walk up: parent at intZoom-1, intZoom-2, ... The parent at
            // depth dz covers 2^dz x 2^dz tiles at intZoom; our location
            // inside it is (tx % factor, ty % factor), so we draw a
            // (256/factor)^2 sub-square.
            for (int dz = 1; intZoom - dz >= minZoom_ && !drewFallback; dz++) {
                int pz     = intZoom - dz;
                int factor = 1 << dz;
                TileKey pk{pz, tx >> dz, ty >> dz};
                auto pit = tileCache_.find(pk);
                if (pit != tileCache_.end() && pit->second.isAllocated()) {
                    float sub = (float)TILE_SIZE_PX / (float)factor;
                    float sx  = (float)(tx % factor) * sub;
                    float sy  = (float)(ty % factor) * sub;
                    pit->second.drawSubsection((float)drawX, (float)drawY,
                                               (float)drawW, (float)drawH,
                                               sx, sy, sub, sub);
                    drewFallback = true;
                }
            }

            // Walk down one level: composite the 4 children if any of them
            // are loaded. Each child fills a quadrant of our draw rect.
            // Limited to +1 because +2 = 16 lookups per missing tile and the
            // visual gain past one level is small.
            if (!drewFallback && intZoom < maxZoom_) {
                int cz   = intZoom + 1;
                int cxB  = tx * 2;
                int cyB  = ty * 2;
                bool any = false;
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        TileKey ck{cz, cxB + dx, cyB + dy};
                        auto cit = tileCache_.find(ck);
                        if (cit == tileCache_.end() || !cit->second.isAllocated()) continue;
                        float halfW = (float)drawW * 0.5f;
                        float halfH = (float)drawH * 0.5f;
                        cit->second.draw((float)drawX + dx * halfW,
                                         (float)drawY + dy * halfH,
                                         halfW, halfH);
                        any = true;
                    }
                }
                if (any) drewFallback = true;
            }

            if (!drewFallback) {
                setColor(0.15f, 0.15f, 0.18f);
                fill();
                drawRect((float)drawX, (float)drawY, (float)drawW, (float)drawH);
            }
            requestTile(key);
        }
    }

    if (prefetchNeighbors_) {
        if (intZoom + 1 <= maxZoom_) prefetchZoomLevel(intZoom + 1);
        if (intZoom - 1 >= minZoom_) prefetchZoomLevel(intZoom - 1);
    }
}

void MapView::prefetchZoomLevel(int z) {
    // Same viewport coverage math as drawTiles(), at integer zoom z.
    double tileScale = std::pow(2.0, zoom_ - z);
    WorldPixel center = latLonToWorldPixel({centerLat_, centerLon_}, z);
    double half_w = sizeW_ * 0.5 / tileScale;
    double half_h = sizeH_ * 0.5 / tileScale;

    int maxTile = (1 << z);
    int minTX = std::max(0, (int)std::floor((center.x - half_w) / TILE_SIZE_PX));
    int maxTX = std::min(maxTile - 1, (int)std::floor((center.x + half_w) / TILE_SIZE_PX));
    int minTY = std::max(0, (int)std::floor((center.y - half_h) / TILE_SIZE_PX));
    int maxTY = std::min(maxTile - 1, (int)std::floor((center.y + half_h) / TILE_SIZE_PX));

    // Bound the burst — a huge window at z+1 can cover hundreds of tiles, and
    // a warmup that large defeats its own purpose (and hammers the server).
    int count = (maxTX - minTX + 1) * (maxTY - minTY + 1);
    if (count > 64) return;

    for (int ty = minTY; ty <= maxTY; ty++)
        for (int tx = minTX; tx <= maxTX; tx++)
            requestTile({z, tx, ty}, /*prefetch=*/true);
}

// ------------------------------------------------------------ tile worker

void MapView::requestTile(TileKey key, bool prefetch) {
    if (tileCache_.count(key)) return;

    auto fit = tileFailed_.find(key);
    if (fit != tileFailed_.end() && fit->second >= 3) return;

    {
        std::lock_guard<std::mutex> lk(tileMutex_);
        if (!tileLoading_.insert(key).second) return;     // already queued / loading
    }

    // Disk cache hit — load synchronously, no HTTP. Visible tiles only: a
    // prefetch is invisible by definition, so its disk I/O + PNG decode go to
    // the worker instead of stalling the frame.
    if (!prefetch && !tileCacheDir_.empty()) {
        std::string cachePath = tileCacheDir_ + "/" + std::to_string(key.z)
                              + "/" + std::to_string(key.x)
                              + "/" + std::to_string(key.y) + ".png";
        std::error_code ec;
        if (fs::exists(cachePath, ec)) {
            Pixels px;
            if (px.load(cachePath)) {
                Texture tex;
                tex.allocate(px, TextureUsage::Immutable, false);
                tileCache_[key] = std::move(tex);
                std::lock_guard<std::mutex> lk(tileMutex_);
                tileLoading_.erase(key);
                return;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(tileMutex_);
        (prefetch ? prefetchQueue_ : tileQueue_).push_back(key);
    }
    if (!tileThreadRunning_) {
        tileThreadRunning_ = true;
        startTileThread();
    }
}

void MapView::startTileThread() {
    if (tileThread_.joinable()) tileThread_.detach();
    tileThreadStop_ = false;
    tileThread_ = std::thread([this]() {
        while (!tileThreadStop_) {
            TileKey req{};
            {
                std::lock_guard<std::mutex> lk(tileMutex_);
                if (!tileQueue_.empty()) {
                    req = tileQueue_.front();
                    tileQueue_.pop_front();
                } else if (!prefetchQueue_.empty()) {
                    req = prefetchQueue_.front();
                    prefetchQueue_.pop_front();
                } else {
                    break;
                }
            }

            // Disk first. Visible tiles already checked synchronously before
            // queueing, so this only really fires for prefetches — but it's a
            // single cheap stat() for the others.
            Pixels px;
            bool fromDisk = false;
            if (!tileCacheDir_.empty()) {
                std::string cachePath = tileCacheDir_ + "/" + std::to_string(req.z)
                                      + "/" + std::to_string(req.x)
                                      + "/" + std::to_string(req.y) + ".png";
                std::error_code ec;
                if (fs::exists(cachePath, ec) && px.load(cachePath)) {
                    fromDisk = true;
                }
            }

            if (!fromDisk) {
                std::string url = provider_ ? provider_->urlFor(req) : std::string{};
                std::string ua  = provider_ ? provider_->userAgent() : std::string{"tcxOpenStreetMap/0.1"};
                if (!url.empty()) {
                    tcx::HttpClient client;
                    client.addHeader("User-Agent", ua);
                    client.setBaseUrl("");
                    auto res = client.get(url);
                    if (res.ok() && !res.body.empty()) {
                        px.loadFromMemory(
                            (const unsigned char*)res.body.data(),
                            (int)res.body.size());

                        if (!tileCacheDir_.empty()) {
                            std::string dir = tileCacheDir_
                                            + "/" + std::to_string(req.z)
                                            + "/" + std::to_string(req.x);
                            std::error_code ec;
                            fs::create_directories(dir, ec);
                            std::string cachePath = dir + "/" + std::to_string(req.y) + ".png";
                            std::ofstream f(cachePath, std::ios::binary);
                            if (f) f.write(res.body.data(), res.body.size());
                        }
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lk(tileMutex_);
                tileResults_.push_back({req, std::move(px)});
            }
            // Polite throttle — keeps us under OSM's recommended request rate
            // and avoids spinning when the queue is very long. Disk hits cost
            // the server nothing, so they skip it.
            if (!fromDisk) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        tileThreadRunning_ = false;
    });
    tileThread_.detach();
}

void MapView::evictOldTiles() {
    if ((int)tileCache_.size() <= maxMemoryTiles_) return;

    // Keep the tiles closest (in zoom) to the current view; drop the rest.
    int currentZ = (int)std::round(zoom_);
    while ((int)tileCache_.size() > maxMemoryTiles_) {
        auto worst = tileCache_.begin();
        int worstDist = -1;
        for (auto it = tileCache_.begin(); it != tileCache_.end(); ++it) {
            int dist = std::abs(it->first.z - currentZ);
            if (dist > worstDist) {
                worst = it;
                worstDist = dist;
            }
        }
        tileCache_.erase(worst);
    }
}

} // namespace tcx::openstreetmap
