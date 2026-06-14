#pragma once

#include <TrussC.h>

using namespace std;
using namespace tc;

// A map pin: a filled blue disc with a dark outline, plus a highlighted label
// to its right. The label uses drawBitmapStringHighlight (a filled background
// behind the text) so it stays legible over busy map tiles.
//
// Pins are Nodes — this is the "bring your own pin" half of MapViewNode's
// pin-as-Node design. Swap this class for whatever shape your app wants.
class WaypointPin : public RectNode {
public:
    string label;

    void setup() override {
        setSize(12, 12);
    }

    void draw() override {
        setColor(0.20f, 0.45f, 0.95f);
        fill();
        drawCircle(getWidth() * 0.5f, getHeight() * 0.5f, getWidth() * 0.5f);

        setColor(0.05f, 0.10f, 0.25f);
        noFill();
        drawCircle(getWidth() * 0.5f, getHeight() * 0.5f, getWidth() * 0.5f);

        // The highlight's background rect honours the current fill mode, so
        // restore fill() after the outline circle above or it draws as a frame.
        fill();
        drawBitmapStringHighlight(label, getWidth() + 4, getHeight() * 0.5f + 4,
                                  Color(0.05f, 0.10f, 0.25f), Color(1, 1, 1));
    }
};
