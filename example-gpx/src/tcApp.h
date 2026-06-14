#pragma once

// =============================================================================
// example-gpx - load a GPX file and draw it over the map.
//
// Parses bin/data/palace-loop.gpx (a synthetic walk around the Imperial
// Palace), draws the track as a TrackLayer, pins its waypoints, and frames
// the view on the track's bounding box. Pan with left-drag, zoom with scroll.
// =============================================================================

#include <TrussC.h>
#include <tcxOpenStreetMap.h>

using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;

private:
    MapViewNode::Ptr map_;
    TrackLayer::Ptr  track_;

    // setup() is split into copy-pasteable parts — grab just the piece you need.
    void setupMap();
    void addTrackLayer(const GpxTrack& track);
    void addWaypointPins(const GpxFile& gpx);
    void frameOn(const GpxTrack& track);
};
