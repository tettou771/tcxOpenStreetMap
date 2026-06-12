#include "tcApp.h"

#include <cctype>

// Small red disc with the matched name displayed beside it.
class ResultPin : public RectNode {
public:
    std::string label;
    void setup() override {
        setSize(14, 14);
    }
    void draw() override {
        setColor(0.95f, 0.25f, 0.25f);
        fill();
        drawCircle(getWidth() * 0.5f, getHeight() * 0.5f, getWidth() * 0.5f);
        setColor(0.15f, 0.02f, 0.02f);
        noFill();
        drawCircle(getWidth() * 0.5f, getHeight() * 0.5f, getWidth() * 0.5f);
        if (!label.empty()) {
            setColor(0.95f);
            drawBitmapString(label, getWidth() + 4, getHeight() * 0.5f - 4);
        }
    }
};

void tcApp::setup() {
    auto osm = std::make_shared<OSMTileProvider>();
    osm->setUserAgent("tcxOpenStreetMap-example-search/0.1");

    map_ = std::make_shared<MapViewNode>();
    map_->setSize(getWidth(), getHeight());
    map_->map()
        .setTileProvider(osm)
        .setCenter({35.6586, 139.7454})
        .setZoom(13);
    addChild(map_);

    nominatim_.setUserAgent("tcxOpenStreetMap-example-search/0.1")
              .setLanguage("en");
}

void tcApp::update() {
    if (inboxReady_.exchange(false)) {
        std::vector<GeocodingResult> results;
        {
            std::lock_guard<std::mutex> lk(inboxMutex_);
            results = std::move(inbox_);
        }
        if (results.empty()) {
            status_ = "No result for \"" + queryBuf_ + "\"";
        } else {
            auto& top = results.front();
            double z = NominatimClient::zoomForBoundingBox(
                top, getWidth(), getHeight());
            map_->map().setCenter(top.center).setZoom(z);
            auto pin = std::make_shared<ResultPin>();
            pin->label = top.displayName;
            map_->addPin(top.center, pin);
            char buf[256];
            snprintf(buf, sizeof(buf), "Found \"%s\" @ %.5f,%.5f (zoom %.1f)",
                     top.displayName.c_str(),
                     top.center.lat, top.center.lon, z);
            status_ = buf;
        }
    }
}

void tcApp::draw() {
    // Search prompt overlay at the top.
    float pad = 8;
    float h   = 28;
    setColor(0.08f, 0.08f, 0.10f, 0.85f);
    fill();
    drawRect(pad, pad, getWidth() - 2 * pad, h);
    setColor(0.4f);
    noFill();
    drawRect(pad, pad, getWidth() - 2 * pad, h);

    setColor(0.95f);
    std::string prompt = "search: " + queryBuf_ + "_";
    drawBitmapString(prompt, pad + 8, pad + 10);

    // Status line at bottom.
    setColor(0.65f);
    drawBitmapString(status_, pad, getHeight() - 14);
}

void tcApp::keyPressed(int key) {
    if (key == KEY_ENTER) {
        if (queryBuf_.empty()) return;
        status_ = "Searching for \"" + queryBuf_ + "\"...";
        nominatim_.searchAsync(queryBuf_, [this](std::vector<GeocodingResult> results) {
            std::lock_guard<std::mutex> lk(inboxMutex_);
            inbox_ = std::move(results);
            inboxReady_ = true;
        }, /*limit*/1);
    } else if (key == KEY_BACKSPACE) {
        if (!queryBuf_.empty()) queryBuf_.pop_back();
    } else if (key == KEY_ESCAPE) {
        queryBuf_.clear();
    } else if (key >= 32 && key < 127) {
        // Printable ASCII. A real search field needs an IME for kana / CJK;
        // skipping that for a minimal Nominatim demo.
        queryBuf_.push_back((char)key);
    }
}
