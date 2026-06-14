#pragma once

// =============================================================================
// tcxNominatim.h - OSM Nominatim geocoding client
// =============================================================================
// Forward geocoding (place name → lat/lon) and reverse geocoding (lat/lon →
// address) over OSM's Nominatim API. Used standalone, or together with
// MapView to power a search bar that recenters the view on a place name.
//
// Public OSM Nominatim server (https://nominatim.openstreetmap.org) is the
// default. Its usage policy:
//   - 1 request per second maximum (the addon throttles)
//   - identify yourself with setUserAgent("YourApp/1.0")
//   - no large bulk-geocoding (use a self-hosted Nominatim or Pelias/Geocode.Earth)
// The addon enforces the rate limit by default — turn it off if you point at
// a self-hosted server with no such restriction.
// =============================================================================

#include "tcxOpenStreetMapTypes.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tcx {

struct GeocodingResult {
    LatLon       center{};
    // Returned bounding box for the matched place. Equal to center when
    // Nominatim doesn't give one (e.g. some POIs have a point geometry).
    LatLon       bboxSW{};
    LatLon       bboxNE{};
    std::string  displayName;       // "Tokyo Tower, ..., Tokyo, Japan"
    std::string  category;          // "tourism" / "amenity" / ...
    std::string  type;              // "attraction" / "restaurant" / ...
    double       importance = 0.0;  // 0..1 (Nominatim's own ranking)
    bool         valid = false;     // false = parse failed / no hit
};

class NominatimClient {
public:
    NominatimClient& setServer(std::string url) { server_ = std::move(url); return *this; }
    NominatimClient& setUserAgent(std::string ua) { ua_ = std::move(ua); return *this; }
    // ISO language code — controls displayName language.
    // Examples: "ja", "en", "en-US", "ja,en;q=0.5".
    NominatimClient& setLanguage(std::string lang) { lang_ = std::move(lang); return *this; }
    // OSM-policy throttle (ms between requests). 0 disables — use only when
    // pointing at a self-hosted server.
    NominatimClient& setMinRequestIntervalMs(int ms) { minIntervalMs_ = ms; return *this; }

    // ----------------------------------------------------------- sync API
    // Blocks the calling thread (waits on HTTP). Honours the throttle.
    // reverse(): `zoom` picks the address granularity Nominatim answers with
    // (0 = country … 10 = city … 14 = neighbourhood … 18 = building).
    std::vector<GeocodingResult> search (const std::string& query, int limit = 1);
    GeocodingResult              reverse(LatLon point, int zoom = 18);

    // ---------------------------------------------------------- async API
    // Spawns a detached worker. Callback fires on that worker — marshal back
    // to the main thread yourself if you need to touch the scene graph.
    void searchAsync (const std::string& query,
                      std::function<void(std::vector<GeocodingResult>)> cb,
                      int limit = 1);
    void reverseAsync(LatLon point,
                      std::function<void(GeocodingResult)> cb,
                      int zoom = 18);

    // ---------------------------------------------------------- helpers
    // Compute the fractional zoom at which the result's bounding box fits a
    // viewport. The "fit" is intentionally a bit loose (bbox × 2 inside
    // viewport) so the matched place isn't pasted against the edge of
    // the frame. Matches what TrussPhoto-mapView used.
    static double zoomForBoundingBox(const GeocodingResult& r,
                                     float viewportW, float viewportH);

private:
    std::string server_  = "https://nominatim.openstreetmap.org";
    std::string ua_      = "tcxOpenStreetMap/0.1";
    std::string lang_;
    int         minIntervalMs_ = 1000;

    std::mutex                                   throttleMutex_;
    std::chrono::steady_clock::time_point        lastRequest_{};

    // Block on the calling thread until at least minIntervalMs_ has elapsed
    // since the previous request finished.
    void waitForRateLimit();

    static std::string urlEncode(const std::string& s);
};

} // namespace tcx
