#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("tcxOpenStreetMap - example-core");

    return TC_RUN_APP(tcApp, settings);
}
