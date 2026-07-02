#pragma once

// =============================================================================
// tcxTileProvider.h - swappable source of slippy-map tile bytes
// =============================================================================
// A TileProvider answers "what URL serves tile (z,x,y), and what User-Agent
// should I identify as when fetching it?". Decoupled from the network layer so
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
// =============================================================================

#include "tcxOpenStreetMapTypes.h"

#include <memory>
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
    virtual std::string urlFor(const TileKey& key) = 0;

    // Sent as the User-Agent header. OSM tiles return 429 if this is blank /
    // looks default — change it to identify your app concretely.
    virtual std::string userAgent() const { return "tcxOpenStreetMap/0.1"; }

    // Highest zoom the source serves. Used by MapView to clamp zoom-in.
    virtual int maxZoom() const { return 19; }
};

// -----------------------------------------------------------------------------
// OpenStreetMap-hosted tiles. Default rotates across a/b/c subdomains the way
// OSM (and most slippy clones) recommend to spread cache headers.
// -----------------------------------------------------------------------------
class OSMTileProvider : public TileProvider {
public:
    OSMTileProvider() = default;

    OSMTileProvider& setUserAgent(std::string ua) { ua_ = std::move(ua); return *this; }
    OSMTileProvider& setSubdomains(std::vector<std::string> hosts) {
        subdomains_ = std::move(hosts);
        return *this;
    }

    std::string urlFor(const TileKey& key) override {
        // Round-robin by tile index so neighbouring tiles spread across
        // subdomains and stale-cache headers don't pile up on one of them.
        const std::string& host = subdomains_.empty()
            ? defaultHost_
            : subdomains_[(size_t)((key.x + key.y) % (int)subdomains_.size())];
        return "https://" + host + "/" + std::to_string(key.z) + "/"
             + std::to_string(key.x) + "/" + std::to_string(key.y) + ".png";
    }
    std::string userAgent() const override { return ua_; }
    int         maxZoom()   const override { return 19; }

private:
    std::string ua_ = "tcxOpenStreetMap/0.1";
    std::string defaultHost_ = "tile.openstreetmap.org";
    // Defaults match OSM's tile.openstreetmap.org subdomain convention.
    std::vector<std::string> subdomains_ = {
        "a.tile.openstreetmap.org",
        "b.tile.openstreetmap.org",
        "c.tile.openstreetmap.org",
    };
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

    std::string urlFor(const TileKey& key) override {
        std::string out = tpl_;
        replaceAll(out, "{z}", std::to_string(key.z));
        replaceAll(out, "{x}", std::to_string(key.x));
        replaceAll(out, "{y}", std::to_string(key.y));
        return out;
    }
    std::string userAgent() const override { return ua_; }
    int         maxZoom()   const override { return maxZoom_; }

private:
    static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) {
            s.replace(p, from.length(), to);
            p += to.length();
        }
    }
    std::string tpl_ = "";
    std::string ua_  = "tcxOpenStreetMap/0.1";
    int         maxZoom_ = 19;
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
namespace tcx    { using openstreetmap::OSMTileProvider; } // deprecated: remove at v1.0.0
namespace tcx    { using openstreetmap::UrlTemplateTileProvider; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::TileProvider; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::OSMTileProvider; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::UrlTemplateTileProvider; } // deprecated: remove at v1.0.0
