#pragma once

// =============================================================================
// tcxTileProvider.h - swappable source of slippy-map tile bytes
// =============================================================================
// A TileProvider answers "what URL serves tile (z,x,y), how deep does this
// source go, and how must it be credited?". Decoupled from the network layer so
// you can stub one (synthetic tiles) or swap to a self-hosted server / Mapbox /
// OpenTopoMap without touching MapView.
//
// Convention shared with every slippy-map provider:
//   https://<server>/<z>/<x>/<y>.png
//
// Bundled defaults:
//   OSMTileProvider — tile.openstreetmap.org, free, attribution required
//                     (see https://operations.osmfoundation.org/policies/tiles/
//                     for the live limits; the default User-Agent is what the
//                     policy wants you to set to something app-specific).
//                     Configured from a bundled TileJSON document rather than
//                     from C++ literals — see tcxTileJson.h.
// =============================================================================

#include "tcxOpenStreetMapTypes.h"
#include "tcxTileJson.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tcx::openstreetmap {

// -----------------------------------------------------------------------------
// Base interface — implementers return the URL for a given tile, plus an HTTP
// User-Agent (OSM in particular rejects requests that don't identify the app).
// -----------------------------------------------------------------------------
class TileProvider {
public:
    virtual ~TileProvider() = default;

    // The URL to GET for tile (z, x, y). Caller does HTTP itself (tcxCurl).
    // Called from MapView's tile worker thread — implementations must be safe
    // to call concurrently with the const accessors below.
    virtual std::string urlFor(const TileKey& key) = 0;

    // Sent as the User-Agent header. OSM tiles return 429 if this is blank /
    // looks default — change it to identify your app concretely.
    virtual std::string userAgent() const { return "tcxOpenStreetMap/0.1"; }

    // Highest zoom the source serves. Used by MapView to clamp the zoom level
    // it *requests tiles at* (over-zoom still renders, by upscaling).
    virtual int maxZoom() const { return 19; }

    // Lowest zoom the source serves. Default 0 = whole world in one tile, which
    // is what every conventional slippy source does.
    virtual int minZoom() const { return 0; }

    // Credit line the tile-usage policy requires be shown. May contain HTML;
    // attributionText() (tcxTileJson.h) strips it for bitmap-font overlays.
    // Empty by default — a custom provider that returns nothing here is saying
    // "I have no attribution requirement", so drawing it is the app's call.
    virtual std::string attribution() const { return {}; }
};

// -----------------------------------------------------------------------------
// A provider configured entirely by a TileJSON document — bundled, fetched, or
// handed over as a struct. This is the base for OSMTileProvider, so the bundled
// and the remote path go through exactly the same parser and the same accessors:
// there is no "remote-only" code path left to break on its own.
//
// Thread safety: the configuration is guarded by an internal mutex, because
// urlFor() runs on MapView's tile worker while maxZoom()/minZoom()/attribution()
// are read from the main thread during drawTiles(). Reconfiguring mid-flight is
// therefore safe; the tiles already in the queue just finish under the old URLs.
// -----------------------------------------------------------------------------
class TileJsonTileProvider : public TileProvider {
public:
    TileJsonTileProvider() = default;
    explicit TileJsonTileProvider(const std::string& tileJson) {
        configureFromJson(tileJson);
    }

    // Both return false (and change nothing) if the document can't serve tiles.
    bool configureFrom(const TileJsonDoc& doc);
    bool configureFromJson(const std::string& json);

    // GET a TileJSON document and reconfigure from it. Blocking / detached-thread
    // variants; both leave the current configuration intact on any failure.
    //
    // !! The async callback fires ON THE WORKER THREAD, not on the main thread.
    //    It is safe to read this provider from it, and NOT safe to touch the
    //    scene graph, textures, or anything else TrussC owns — marshal to your
    //    own frame loop if you need to react visually.
    //
    // Redirects are followed (services do hand TileJSON back via 302).
    bool fetchSync(const std::string& url);
    void fetchAsync(const std::string& url, std::function<void(bool ok)> cb = {});

    // Snapshot of the live configuration, taken under the lock.
    TileJsonDoc document() const;

    std::string urlFor(const TileKey& key) override;
    std::string userAgent()   const override;
    int         maxZoom()     const override;
    int         minZoom()     const override;
    std::string attribution() const override;

    TileJsonTileProvider& setUserAgent(std::string ua);

    // Hosts substituted into a `{s}` placeholder, round-robin by tile index so
    // neighbouring tiles spread across them and stale-cache headers don't pile
    // up on one. No effect on templates without `{s}`.
    TileJsonTileProvider& setSubdomains(std::vector<std::string> hosts);

protected:
    // Callers already holding mutex_ must use the *Locked forms.
    bool configureFromLocked(const TileJsonDoc& doc);

    mutable std::mutex       mutex_;
    TileJsonDoc              doc_;
    std::string              ua_ = "tcxOpenStreetMap/0.1";
    std::vector<std::string> subdomains_;
};

// -----------------------------------------------------------------------------
// OpenStreetMap-hosted tiles.
//
// The zoom ceiling, the URL template and the attribution all come from the
// TileJSON document below — a verbatim copy of what OSM publishes at
// render.openstreetmap.org/tilejson.json. Bundled rather than fetched on
// startup, deliberately:
//
//   - the document is only reachable on the raw origin, which is not a URL OSM
//     asks clients to use, and which serves 403 for the tiles themselves
//   - the values in it change on the order of years, while a fetch would make
//     every cold start depend on a network round trip
//   - and above all: with a bundled default there is no window in which the
//     provider has no URL to give. A provider that returns an empty urlFor()
//     gets every tile marked failed, and failures are permanent (three strikes
//     in MapView::requestTile), so a blank map would never recover.
//
// The honest cost is that `19` still exists as a string in this file. Removing
// it entirely would mean requiring the fetch, which trades an inert literal for
// a startup failure mode.
// -----------------------------------------------------------------------------
class OSMTileProvider : public TileJsonTileProvider {
public:
    OSMTileProvider();

    // Covariant chaining wrappers over the base setters.
    OSMTileProvider& setUserAgent(std::string ua) {
        TileJsonTileProvider::setUserAgent(std::move(ua));
        return *this;
    }
    OSMTileProvider& setSubdomains(std::vector<std::string> hosts) {
        TileJsonTileProvider::setSubdomains(std::move(hosts));
        return *this;
    }

    // The document this provider ships with, exactly as OSM publishes it.
    static const char* bundledTileJson();
};

// -----------------------------------------------------------------------------
// Tiny URL-template provider — useful for self-hosted tile servers and most
// vendors (Mapbox, OpenTopoMap, …). Substitutes {z} / {x} / {y} verbatim.
// -----------------------------------------------------------------------------
class UrlTemplateTileProvider : public TileProvider {
public:
    UrlTemplateTileProvider& setTemplate(std::string tpl) { tpl_ = std::move(tpl); return *this; }
    UrlTemplateTileProvider& setUserAgent(std::string ua) { ua_ = std::move(ua); return *this; }
    UrlTemplateTileProvider& setMaxZoom(int z) { maxZoom_ = z; return *this; }
    UrlTemplateTileProvider& setMinZoom(int z) { minZoom_ = z; return *this; }
    UrlTemplateTileProvider& setAttribution(std::string a) { attribution_ = std::move(a); return *this; }

    std::string urlFor(const TileKey& key) override {
        std::string out = tpl_;
        replaceAll(out, "{z}", std::to_string(key.z));
        replaceAll(out, "{x}", std::to_string(key.x));
        replaceAll(out, "{y}", std::to_string(key.y));
        return out;
    }
    std::string userAgent()   const override { return ua_; }
    int         maxZoom()     const override { return maxZoom_; }
    int         minZoom()     const override { return minZoom_; }
    std::string attribution() const override { return attribution_; }

    static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) {
            s.replace(p, from.length(), to);
            p += to.length();
        }
    }

private:
    std::string tpl_ = "";
    std::string ua_  = "tcxOpenStreetMap/0.1";
    std::string attribution_;
    int         maxZoom_ = 19;
    int         minZoom_ = 0;
};

} // namespace tcx::openstreetmap

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::openstreetmap`.
// These silent aliases keep older code compiling: flat `tcx::Xxx` and legacy
// `trussc::Xxx`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxOpenStreetMap README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using openstreetmap::TileProvider; } // deprecated: remove at v1.0.0
namespace tcx    { using openstreetmap::TileJsonTileProvider; } // deprecated: remove at v1.0.0
namespace tcx    { using openstreetmap::OSMTileProvider; } // deprecated: remove at v1.0.0
namespace tcx    { using openstreetmap::UrlTemplateTileProvider; } // deprecated: remove at v1.0.0
namespace tcx    { using openstreetmap::TileJsonDoc; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::TileProvider; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::TileJsonTileProvider; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::OSMTileProvider; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::UrlTemplateTileProvider; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::TileJsonDoc; } // deprecated: remove at v1.0.0
