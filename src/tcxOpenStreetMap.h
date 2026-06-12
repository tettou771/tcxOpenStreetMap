#pragma once

// =============================================================================
// tcxOpenStreetMap - slippy-map tile renderer (OpenStreetMap + others)
// =============================================================================
// Renders OSM-compatible tile maps inside a TrussC app. Two surfaces:
//
//   MapView      — Node-agnostic core. Lat/lon projection, viewport math,
//                  visibility queries, tile cache, drawTiles() at the
//                  current draw cursor. Compose freely inside your own
//                  draw().
//
//   MapViewNode  — RectNode subclass that wraps a MapView and adds:
//                    - left-drag pan, scroll zoom
//                    - pins as child nodes positioned by LatLon
//                  The recommended starting point.
//
// Backed by TrussC HTTP (tcxCurl). Set a TileProvider — OSMTileProvider by
// default — to point at different tile servers / Mapbox / your own.
// =============================================================================

#include "tcxOpenStreetMapTypes.h"   // LatLon, TileKey, projection helpers
#include "tcxTileProvider.h"         // OSMTileProvider, UrlTemplateTileProvider
#include "tcxMapView.h"              // MapView
#include "tcxMapViewNode.h"          // MapViewNode
#include "tcxNominatim.h"            // NominatimClient, GeocodingResult
