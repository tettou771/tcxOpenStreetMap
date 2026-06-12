#pragma once

// =============================================================================
// example-mapNode - the recommended way to embed a map.
//
// Drops a MapViewNode into the scene graph, then right-click anywhere to
// place a pin (also a Node — a tiny round button that prints its lat/lon
// when clicked). Pan with left-drag, zoom with scroll.
// =============================================================================

#include <TrussC.h>
#include <tcxOpenStreetMap.h>

using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void update() override;

private:
    MapViewNode::Ptr map_;
    EventListener mapPressListener_;
};
