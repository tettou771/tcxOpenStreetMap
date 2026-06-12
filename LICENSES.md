# tcxOpenStreetMap Licenses

## tcxOpenStreetMap

MIT License

Copyright (c) 2026 tettou771

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## OpenStreetMap data & tiles (fetched at runtime, not bundled)

This addon ships no map data. At runtime, the default `OSMTileProvider`
downloads tiles from `tile.openstreetmap.org` and `NominatimClient` queries
`nominatim.openstreetmap.org`.

- Map data is © OpenStreetMap contributors, licensed under the
  Open Data Commons Open Database License (ODbL) 1.0.
  https://www.openstreetmap.org/copyright
- **Applications that display OSM tiles or Nominatim results must show the
  attribution "© OpenStreetMap contributors"** (visible on or near the map,
  or in an about/credits screen where a visible overlay is impractical).
- The public tile server is run by the OpenStreetMap Foundation and is
  subject to the Tile Usage Policy:
  https://operations.osmfoundation.org/policies/tiles/
- The public Nominatim instance is subject to the Nominatim Usage Policy
  (max 1 request/sec, which this addon throttles to by default):
  https://operations.osmfoundation.org/policies/nominatim/

Tiles fetched from other providers via `UrlTemplateTileProvider`
(Mapbox, OpenTopoMap, self-hosted, …) are subject to that provider's own
terms instead.

---

## docs/preview.png

Screenshot of this addon rendering the OpenStreetMap standard tile layer
(Tokyo). Map data © OpenStreetMap contributors (ODbL 1.0).
https://www.openstreetmap.org/copyright
