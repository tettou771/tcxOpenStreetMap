#pragma once

// =============================================================================
// tcxMapViewNode.h - drop-in slippy-map widget for the Node hierarchy
// =============================================================================
// Wraps a MapView in a RectNode so you can `addChild(make_shared<MapViewNode>())`
// and get pan / zoom / pin placement for free. For the lower-level "I want the
// projection math without the node" version, use MapView directly.
//
// Pins are themselves Nodes — addPin(latlon, myButtonNode) lays out your widget
// at the projected pixel each frame; you keep full control over its look and
// interaction (mouse listeners, etc.) by writing your own Node subclass.
// =============================================================================

#include "tcxMapView.h"
#include "tcxTrackLayer.h"

#include <TrussC.h>     // RectNode, MouseEventArgs, EventListener
#include <memory>
#include <vector>

namespace tcx::openstreetmap {

class MapViewNode : public tc::RectNode {
public:
    using Ptr = std::shared_ptr<MapViewNode>;

    // Built-in input handlers — public so an app can turn them off if it wants
    // its own gestures (e.g. drag pans the parent panel instead of the map).
    bool panEnabled  = true;        // left-button drag
    bool zoomEnabled = true;        // mouse scroll
    double scrollZoomStep = 0.5;    // zoom delta per +1 scroll unit

    // Access to the underlying core. Use this for setCenter / setZoom /
    // setTileProvider / setTileCacheDir / projection / visibility queries.
    MapView& map() { return map_; }
    const MapView& map() const { return map_; }

    // ---------------------------------------------------------------- pins
    // A pin is a Node positioned by its (LatLon) — every frame we project to
    // pixel and re-set the pin's position. The pin's `setSize()` determines
    // visual size; we anchor by its size's bottom-center (classic teardrop
    // pin) unless setPinAnchorTopLeft(true) is called.
    void addPin(LatLon pos, std::shared_ptr<tc::RectNode> pinNode);
    void removePin(std::shared_ptr<tc::RectNode> pinNode);
    void clearPins();
    void setPinAnchorTopLeft(bool topLeft) { pinAnchorTopLeft_ = topLeft; }

    // Pin position lookup, useful if the app re-arranges pins by LatLon.
    LatLon pinPos(std::shared_ptr<tc::RectNode> pinNode) const;
    void   movePin(std::shared_ptr<tc::RectNode> pinNode, LatLon newPos);

    // -------------------------------------------------------------- tracks
    // TrackLayers (tcxTrackLayer.h) drawn between the tiles and the pins.
    // The layer object stays yours — restyle it (setColor/setWidth) anytime.
    void addTrack(TrackLayer::Ptr track);
    void removeTrack(TrackLayer::Ptr track);
    void clearTracks();

    // ---------------------------------------------------------------- node
    void setup() override;
    void update() override;
    void draw() override;

protected:
    bool onMousePress(tc::Vec2 local, int button) override;
    bool onMouseDrag (tc::Vec2 local, int button) override;
    bool onMouseRelease(tc::Vec2 local, int button) override;
    // Scroll comes through a separate global event; wired in setup().

private:
    MapView map_;
    struct PinEntry {
        LatLon pos;
        std::shared_ptr<tc::RectNode> node;
    };
    std::vector<PinEntry> pins_;
    std::vector<TrackLayer::Ptr> tracks_;
    bool   pinAnchorTopLeft_ = false;

    // Drag state.
    bool     dragging_   = false;
    tc::Vec2 dragLast_   = {0, 0};

    tc::EventListener scrollListener_;

    void layoutPins();
};

} // namespace tcx::openstreetmap

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::openstreetmap`.
// These silent aliases keep older code compiling: flat `tcx::Xxx` and legacy
// `trussc::Xxx`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxOpenStreetMap README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using openstreetmap::MapViewNode; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::openstreetmap::MapViewNode; } // deprecated: remove at v1.0.0
