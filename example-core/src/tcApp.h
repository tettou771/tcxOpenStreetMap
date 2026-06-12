#pragma once

// =============================================================================
// example-core - drive tcx::MapView without the Node wrapper.
//
// Showcases the "I just want the math + the tile pipeline, I'll draw it
// myself" path. Pan with arrow keys, zoom with +/-. For the "just drop a
// widget into my scene graph" path, see example-mapNode.
// =============================================================================

#include <TrussC.h>
#include <tcxOpenStreetMap.h>

using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    MapView map_;
    // Tokyo Tower — same coordinates as the docs use everywhere, makes it
    // easy to eyeball whether the projection is sane.
    LatLon home_{35.6586, 139.7454};
};
