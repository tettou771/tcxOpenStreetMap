#pragma once

// =============================================================================
// tcxTileJson.h - TileJSON 3.0.0 documents as the tile-source configuration
// =============================================================================
// A tile source is not one number, it is a set: which URLs serve the tiles,
// how deep they go, how shallow, and how the data must be credited. TileJSON
// (https://github.com/mapbox/tilejson-spec) is the format that carries all of
// that in one document, so this addon treats it as the single source of truth
// instead of scattering the same facts across C++ literals.
//
// This header is the pure-data half: the parsed document and the parser. The
// provider that consumes it lives in tcxTileProvider.h (TileJsonTileProvider),
// which is also where the bundled OSM document is applied.
//
// What TileJSON is NOT: a discovery mechanism. There is no conventional path
// to probe — providers publish at /tiles.json, /{id}.json, /tilejson.json, or
// nowhere at all, and OSM's own document is only reachable on the raw origin
// (render.openstreetmap.org/tilejson.json), not through its CDN. The document
// URL is configuration you supply; only its *contents* are discovered.
// =============================================================================

#include <string>
#include <vector>

namespace tcx::openstreetmap {

// The fields of a TileJSON document this addon acts on. Everything else in the
// spec (bounds, center, vector_layers, …) is ignored rather than stored, so a
// reader doesn't have to wonder which parts are live.
struct TileJsonDoc {
    // Tile URL templates. `{z}` / `{x}` / `{y}` are substituted per request,
    // and `{s}` (a de-facto extension, not in the spec) picks a subdomain.
    // Multiple entries are equivalent mirrors — the provider spreads across them.
    std::vector<std::string> tiles;

    int minzoom = 0;
    int maxzoom = 22;   // spec default when the document omits it

    // Credit line REQUIRED to be displayed by most tile-usage policies.
    // Contains HTML in practice (OSM's has an <a href>), so a plain-text
    // renderer has to strip tags — see attributionText().
    std::string attribution;

    std::string name;

    // False until a parse (or an explicit fill) succeeded. A doc with no tile
    // template can never serve anything, so `tiles` empty ⇒ invalid.
    bool valid = false;
};

// Parse a TileJSON document. Exception-free (nlohmann's allow_exceptions=false),
// matching the addon's existing convention in tcxNominatim.cpp — a malformed
// response is a false return, never a throw across a worker-thread boundary.
//
// Returns false and leaves `out` untouched unless the document is usable, so a
// bad remote fetch can never downgrade a working configuration.
bool parseTileJson(const std::string& json, TileJsonDoc& out);

// `attribution` with HTML tags and entities removed, for bitmap-font overlays
// that cannot render markup. Lossy by design: link targets are dropped, so use
// the raw string anywhere real HTML can be shown.
std::string attributionText(const std::string& html);

} // namespace tcx::openstreetmap
