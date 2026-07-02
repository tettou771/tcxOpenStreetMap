#include "tcxTrackLayer.h"

#include <cmath>

using namespace tc;

namespace tcx::openstreetmap {

// ---------------------------------------------------------------- data

TrackLayer& TrackLayer::setTrack(const GpxTrack& track) {
    segments_.clear();
    for (auto& seg : track.segments) {
        std::vector<LatLon> pts;
        pts.reserve(seg.size());
        for (auto& p : seg) pts.push_back(p.pos);
        if (pts.size() >= 2) segments_.push_back(std::move(pts));
    }
    dirty_ = true;
    return *this;
}

TrackLayer& TrackLayer::setPoints(std::vector<LatLon> pts) {
    segments_.clear();
    if (pts.size() >= 2) segments_.push_back(std::move(pts));
    dirty_ = true;
    return *this;
}

TrackLayer& TrackLayer::clear() {
    segments_.clear();
    meshes_.clear();
    dirty_ = true;
    return *this;
}

// ---------------------------------------------------------------- style

TrackLayer& TrackLayer::setColor(Color c) {
    color_ = c;
    dirty_ = true;
    return *this;
}

TrackLayer& TrackLayer::setWidth(float px) {
    width_ = std::max(0.1f, px);
    dirty_ = true;
    return *this;
}

// ---------------------------------------------------------------- mesh

void TrackLayer::rebuild(double zoom) {
    meshes_.clear();
    anchor_ = latLonToWorldPixel(segments_.front().front(), zoom);

    for (auto& seg : segments_) {
        auto mesh = std::make_unique<StrokeMesh>();
        mesh->setWidth(width_);
        mesh->setColor(color_);
        mesh->setJoinType(StrokeMesh::JOIN_ROUND);
        mesh->setCapType(StrokeMesh::CAP_ROUND);
        for (auto& ll : seg) {
            WorldPixel wp = latLonToWorldPixel(ll, zoom);
            mesh->addVertex((float)(wp.x - anchor_.x), (float)(wp.y - anchor_.y));
        }
        mesh->update();
        meshes_.push_back(std::move(mesh));
    }
    meshZoom_ = zoom;
    dirty_ = false;
}

// ---------------------------------------------------------------- draw

void TrackLayer::draw(const MapView& map) {
    if (segments_.empty()) return;

    double z = map.zoom();
    if (dirty_ || meshZoom_ != z) rebuild(z);

    // anchor's viewport position, computed in double before narrowing.
    WorldPixel c = latLonToWorldPixel(map.center(), meshZoom_);
    float ax = (float)(anchor_.x - c.x + map.width()  * 0.5);
    float ay = (float)(anchor_.y - c.y + map.height() * 0.5);

    // No setColor here — StrokeMesh bakes color_ into its vertices, and
    // vertex-colored meshes ignore the global draw color entirely.
    pushMatrix();
    translate(ax, ay);
    for (auto& m : meshes_) m->draw();
    popMatrix();
}

} // namespace tcx::openstreetmap
