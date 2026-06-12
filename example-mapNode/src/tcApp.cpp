#include "tcApp.h"

// A simple pin: round red circle, prints its LatLon to the log when clicked.
// Real apps swap this for whatever Node shape they like — that's the whole
// point of the pin-as-Node design.
class PinNode : public RectNode {
public:
    LatLon pos;
    void setup() override {
        enableEvents();
        setSize(14, 14);
        listener_ = mousePressed.listen([this](MouseEventArgs&) {
            logNotice("pin") << pos.lat << ", " << pos.lon;
        });
    }
    void draw() override {
        // Visible "pin" — solid disc with a darker outline.
        setColor(0.95f, 0.25f, 0.25f);
        fill();
        drawCircle(getWidth() * 0.5f, getHeight() * 0.5f, getWidth() * 0.5f);
        setColor(0.15f, 0.02f, 0.02f);
        noFill();
        drawCircle(getWidth() * 0.5f, getHeight() * 0.5f, getWidth() * 0.5f);
    }
private:
    EventListener listener_;
};

void tcApp::setup() {
    auto osm = std::make_shared<OSMTileProvider>();
    osm->setUserAgent("tcxOpenStreetMap-example-mapNode/0.1");

    map_ = std::make_shared<MapViewNode>();
    map_->setSize(getWidth(), getHeight());
    map_->map()
        .setTileProvider(osm)
        .setCenter({35.6586, 139.7454})        // Tokyo Tower
        .setZoom(13);
    addChild(map_);

    // Right-click anywhere on the map → drop a pin at that latitude/longitude.
    // Note: pin clicks themselves go to PinNode (it consumed the press), so
    // pin placement and pin interaction don't collide.
    mapPressListener_ = map_->mousePressed.listen([this](MouseEventArgs& e) {
        if (e.button != MOUSE_BUTTON_RIGHT) return;
        LatLon p = map_->map().pixelToLatLon({e.x, e.y});
        auto pin = std::make_shared<PinNode>();
        pin->pos = p;
        map_->addPin(p, pin);
    });
}

void tcApp::update() {
    // The Node hierarchy drives MapViewNode::update() itself; nothing to do
    // here unless your app wants to react to anything frame-by-frame.
}
