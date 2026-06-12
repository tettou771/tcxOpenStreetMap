#include "tcApp.h"

void tcApp::setup() {
    // Pick an OSM provider with a polite, recognisable User-Agent — the OSM
    // tile policy bounces anonymous default-UA requests.
    auto osm = std::make_shared<OSMTileProvider>();
    osm->setUserAgent("tcxOpenStreetMap-example-core/0.1");

    map_.setSize(getWidth(), getHeight())
        .setTileProvider(osm)
        .setCenter(home_)
        .setZoom(13);
}

void tcApp::update() {
    map_.setSize(getWidth(), getHeight());
    map_.update();
}

void tcApp::draw() {
    // The core MapView draws at the current cursor — no implicit position.
    // The translate() here is the example app's choice; nest / scale it
    // anywhere you want and the projection follows.
    pushMatrix();
    translate(0, 0);
    map_.drawTiles();
    popMatrix();

    // Demo: draw a red dot at the camera center, projected back via
    // pixelToLatLon → latLonToPixel for a round-trip sanity check.
    Vec2 px = map_.latLonToPixel(home_);
    setColor(0.95f, 0.25f, 0.25f);
    fill();
    drawCircle(px.x, px.y, 6);

    // Hint text
    setColor(0.9f);
    drawBitmapString("arrows = pan, +/- = zoom", 12, getHeight() - 24);
    char buf[96];
    snprintf(buf, sizeof(buf), "z=%.2f  cache=%zu  loading=%zu",
             map_.zoom(), map_.cachedTileCount(), map_.loadingTileCount());
    drawBitmapString(buf, 12, getHeight() - 10);
}

void tcApp::keyPressed(int key) {
    const float panStep = 64;
    switch (key) {
        case KEY_LEFT:  map_.panByPixels( panStep, 0); break;
        case KEY_RIGHT: map_.panByPixels(-panStep, 0); break;
        case KEY_UP:    map_.panByPixels(0,  panStep); break;
        case KEY_DOWN:  map_.panByPixels(0, -panStep); break;
        case '+': case '=': map_.zoomBy(+1, {getWidth() * 0.5f, getHeight() * 0.5f}); break;
        case '-': case '_': map_.zoomBy(-1, {getWidth() * 0.5f, getHeight() * 0.5f}); break;
    }
}
