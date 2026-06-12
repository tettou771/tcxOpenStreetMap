#include "tcxMapViewNode.h"

using namespace tc;

namespace tcx {

void MapViewNode::setup() {
    enableEvents();
    setClipping(true);
    map_.setSize(getWidth(), getHeight());

    // Scroll comes through the global mouseScrolled event (sokol gives one
    // delta per wheel tick / trackpad gesture frame). We project the current
    // global mouse position into our local coords on each event so zoom
    // anchors on the cursor even when the panel was scaled / nested.
    scrollListener_ = events().mouseScrolled.listen([this](ScrollEventArgs& e) {
        if (!zoomEnabled || !isActive()) return;
        Vec3 lpos = globalToLocal({getMouseX(), getMouseY(), 0});
        Vec2 local{lpos.x, lpos.y};
        if (local.x < 0 || local.y < 0 ||
            local.x > getWidth() || local.y > getHeight()) return;
        map_.zoomBy(scrollZoomStep * (double)e.scrollY, local);
    });
}

void MapViewNode::update() {
    map_.setSize(getWidth(), getHeight());
    map_.update();
    layoutPins();
}

void MapViewNode::draw() {
    map_.drawTiles();
    // Pins draw themselves via the Node hierarchy — they're children of this
    // node, positioned by layoutPins() each frame.
}

// ----------------------------------------------------------------- pins

void MapViewNode::addPin(LatLon pos, std::shared_ptr<RectNode> pinNode) {
    if (!pinNode) return;
    pins_.push_back({pos, pinNode});
    addChild(pinNode);
}

void MapViewNode::removePin(std::shared_ptr<RectNode> pinNode) {
    for (auto it = pins_.begin(); it != pins_.end(); ++it) {
        if (it->node == pinNode) {
            removeChild(pinNode);
            pins_.erase(it);
            return;
        }
    }
}

void MapViewNode::clearPins() {
    for (auto& p : pins_) removeChild(p.node);
    pins_.clear();
}

LatLon MapViewNode::pinPos(std::shared_ptr<RectNode> pinNode) const {
    for (auto& p : pins_) if (p.node == pinNode) return p.pos;
    return {0, 0};
}

void MapViewNode::movePin(std::shared_ptr<RectNode> pinNode, LatLon newPos) {
    for (auto& p : pins_) {
        if (p.node == pinNode) { p.pos = newPos; return; }
    }
}

void MapViewNode::layoutPins() {
    for (auto& p : pins_) {
        Vec2 px = map_.latLonToPixel(p.pos);
        float w = p.node->getWidth();
        float h = p.node->getHeight();
        if (pinAnchorTopLeft_) {
            p.node->setPos(px.x, px.y);
        } else {
            // Default anchor: bottom-center (teardrop pin convention).
            p.node->setPos(px.x - w * 0.5f, px.y - h);
        }
    }
}

// ------------------------------------------------------------- input

bool MapViewNode::onMousePress(Vec2 local, int button) {
    if (panEnabled && button == MOUSE_BUTTON_LEFT) {
        dragging_ = true;
        dragLast_ = local;
    }
    return RectNode::onMousePress(local, button);
}

bool MapViewNode::onMouseDrag(Vec2 local, int button) {
    if (dragging_ && button == MOUSE_BUTTON_LEFT) {
        map_.panByPixels(local.x - dragLast_.x, local.y - dragLast_.y);
        dragLast_ = local;
    }
    return RectNode::onMouseDrag(local, button);
}

bool MapViewNode::onMouseRelease(Vec2 local, int button) {
    if (button == MOUSE_BUTTON_LEFT) dragging_ = false;
    return RectNode::onMouseRelease(local, button);
}

} // namespace tcx
