#pragma once

// =============================================================================
// tcxTrackLayer.h - lat/lon polyline overlay (GPX tracks, trails, routes)
// =============================================================================
// MapView deliberately knows nothing about overlays; TrackLayer is the
// composition piece for "draw this lat/lon polyline over the map". It caches
// a stroked mesh and only re-tessellates when the zoom or the data changes —
// panning is a pure translate.
//
//   // setup
//   GpxFile gpx = loadGpx(dataPath("hike.gpx"));
//   track_.setTrack(gpx.tracks[0]).setColor({0.9f, 0.2f, 0.2f}).setWidth(3);
//
//   // draw — same transform as the tiles, right after them
//   map_.drawTiles();
//   track_.draw(map_);
//
// With MapViewNode, use addTrack() instead and it slots in between the tiles
// and the pins automatically.
// =============================================================================

#include "tcxGpx.h"
#include "tcxMapView.h"

#include <TrussC.h>     // StrokeMesh, Color, pushMatrix/translate
#include <memory>
#include <vector>

namespace tcx {

class TrackLayer {
public:
    using Ptr = std::shared_ptr<TrackLayer>;

    // ------------------------------------------------------------- data
    // Each setter replaces the layer's geometry and invalidates the mesh.
    TrackLayer& setTrack (const GpxTrack& track);          // all segments
    TrackLayer& setPoints(std::vector<LatLon> pts);        // one polyline
    TrackLayer& clear();

    // ------------------------------------------------------------- style
    // Width is in screen pixels, independent of zoom.
    TrackLayer& setColor(tc::Color c);
    TrackLayer& setWidth(float px);
    tc::Color color() const { return color_; }
    float     width() const { return width_; }

    // ------------------------------------------------------------- draw
    // Call with the same transform as map.drawTiles() (i.e. right after it).
    void draw(const MapView& map);

private:
    std::vector<std::vector<LatLon>> segments_;
    tc::Color color_ = {0.86f, 0.20f, 0.18f, 0.9f};
    float     width_ = 3.0f;

    // One mesh per segment, in world pixels relative to anchor_. The anchor
    // keeps vertex magnitudes small — absolute world pixels overflow float
    // precision past zoom ~15 and the line starts to wobble.
    std::vector<std::unique_ptr<tc::StrokeMesh>> meshes_;
    WorldPixel anchor_{};
    double     meshZoom_ = -1e9;
    bool       dirty_    = true;

    void rebuild(double zoom);
};

} // namespace tcx
