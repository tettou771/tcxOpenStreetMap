#include "tcApp.h"
#include "WaypointPin.h"

// Highest zoom (0.1 steps) where the track's bbox fits in ~70% of the view.
static double zoomToFit(LatLon sw, LatLon ne, float viewW, float viewH) {
    for (double z = 18.0; z >= 1.0; z -= 0.1) {
        WorldPixel pSW = latLonToWorldPixel(sw, z);
        WorldPixel pNE = latLonToWorldPixel(ne, z);
        double w = std::abs(pNE.x - pSW.x);
        double h = std::abs(pSW.y - pNE.y);
        if (w < viewW * 0.7 && h < viewH * 0.7) return z;
    }
    return 13.0;
}

void tcApp::setup() {
    // An agent can drive pan/zoom for verification in a dev build
    // (`trusscli run --mcp` grants Control). A normal run stays inert.

    setupMap();

    GpxFile gpx = loadGpx(getDataPath("palace-loop.gpx"));
    if (!gpx.valid || gpx.tracks.empty()) {
        logError("example-gpx") << "could not load palace-loop.gpx";
        map_->map().setCenter({35.6852, 139.7528}).setZoom(14);
        return;
    }

    const GpxTrack& track = gpx.tracks.front();
    logNotice("example-gpx") << track.name << ": " << track.pointCount()
                             << " points, " << gpx.waypoints.size() << " waypoints";

    addTrackLayer(track);
    addWaypointPins(gpx);
    frameOn(track);
}

void tcApp::setupMap() {
    auto osm = make_shared<OSMTileProvider>();
    osm->setUserAgent("tcxOpenStreetMap-example-gpx/0.1");

    map_ = make_shared<MapViewNode>();
    map_->setSize(getWidth(), getHeight());
    map_->map()
        .setTileProvider(osm)
        .setPrefetchNeighborZooms(true);    // smoother zoom on a small demo area
    addChild(map_);
}

void tcApp::addTrackLayer(const GpxTrack& track) {
    track_ = make_shared<TrackLayer>();
    track_->setTrack(track).setColor({0.90f, 0.25f, 0.20f, 0.9f}).setWidth(4);
    map_->addTrack(track_);
}

void tcApp::addWaypointPins(const GpxFile& gpx) {
    for (const auto& wp : gpx.waypoints) {
        auto pin = make_shared<WaypointPin>();
        // Name + elevation, matching tcxGeo's example-gpxInfo.
        pin->label = wp.name + " (elev. " + to_string((int)wp.ele) + " m)";
        map_->addPin(wp.pos, pin);
    }
}

void tcApp::frameOn(const GpxTrack& track) {
    LatLon sw, ne;
    if (track.bounds(&sw, &ne)) {
        map_->map()
            .setCenter({(sw.lat + ne.lat) * 0.5, (sw.lon + ne.lon) * 0.5})
            .setZoom(zoomToFit(sw, ne, getWidth(), getHeight()));
    }
}
