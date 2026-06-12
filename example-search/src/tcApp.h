#pragma once

// =============================================================================
// example-search - geocoding + map demo using Nominatim.
//
// Type a place name (e.g. "Tokyo Tower") and press Enter; the app calls
// Nominatim asynchronously, recenters the map on the matched location and
// drops a pin. Backspace edits, Esc clears.
// =============================================================================

#include <TrussC.h>
#include <tcxOpenStreetMap.h>

#include <atomic>
#include <mutex>

using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    MapViewNode::Ptr  map_;
    NominatimClient   nominatim_;

    std::string queryBuf_;
    std::string status_ = "Type a place + Enter (Backspace = edit, Esc = clear)";

    // Cross-thread inbox — Nominatim's async callback fires on a worker
    // thread; park the result there and apply it from update() so we can
    // touch the scene graph safely.
    std::mutex                    inboxMutex_;
    std::atomic<bool>             inboxReady_{false};
    std::vector<GeocodingResult>  inbox_;
};
