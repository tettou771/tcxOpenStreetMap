#include "tcxTileJson.h"

#include "tcxTileProvider.h"

#include <tcxCurl.h>            // HttpClient (pulls in nlohmann/json)
#include <nlohmann/json.hpp>

#include <thread>
#include <utility>

namespace tcx::openstreetmap {

// ------------------------------------------------------------------- parsing

bool parseTileJson(const std::string& json, TileJsonDoc& out) {
    auto j = nlohmann::json::parse(json, nullptr, /*allow_exceptions*/false);
    if (!j.is_object()) return false;

    TileJsonDoc d;

    // `tiles` is the only field a document cannot do without: no template, no
    // map. Everything else has a defensible default.
    if (!j.contains("tiles") || !j["tiles"].is_array()) return false;
    for (const auto& t : j["tiles"]) {
        if (t.is_string() && !t.get<std::string>().empty()) {
            d.tiles.push_back(t.get<std::string>());
        }
    }
    if (d.tiles.empty()) return false;

    auto getInt = [&](const char* k, int dflt) {
        if (j.contains(k) && j[k].is_number_integer()) return j[k].get<int>();
        return dflt;
    };
    auto getStr = [&](const char* k) -> std::string {
        if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
        return {};
    };

    d.minzoom     = getInt("minzoom", 0);
    d.maxzoom     = getInt("maxzoom", 22);   // spec default
    d.attribution = getStr("attribution");
    d.name        = getStr("name");

    // A document claiming maxzoom < minzoom describes nothing renderable; treat
    // it as malformed rather than silently producing an empty zoom range.
    if (d.maxzoom < d.minzoom) return false;
    d.valid = true;

    out = std::move(d);
    return true;
}

std::string attributionText(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    for (size_t i = 0; i < html.size(); i++) {
        const char c = html[i];
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (inTag) continue;
        if (c == '&') {
            // Only the entities that actually turn up in attribution strings;
            // anything else is passed through so nothing silently vanishes.
            const size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 6) {
                const std::string ent = html.substr(i, semi - i + 1);
                if      (ent == "&amp;")  out += '&';
                else if (ent == "&lt;")   out += '<';
                else if (ent == "&gt;")   out += '>';
                else if (ent == "&quot;") out += '"';
                else if (ent == "&#39;" || ent == "&apos;") out += '\'';
                else if (ent == "&nbsp;") out += ' ';
                else if (ent == "&copy;") out += "©";
                else                      out += ent;
                i = semi;
                continue;
            }
        }
        out += c;
    }
    return out;
}

// ------------------------------------------------------- TileJsonTileProvider

bool TileJsonTileProvider::configureFromLocked(const TileJsonDoc& doc) {
    if (!doc.valid || doc.tiles.empty()) return false;
    doc_ = doc;
    return true;
}

bool TileJsonTileProvider::configureFrom(const TileJsonDoc& doc) {
    std::lock_guard<std::mutex> lk(mutex_);
    return configureFromLocked(doc);
}

bool TileJsonTileProvider::configureFromJson(const std::string& json) {
    TileJsonDoc d;
    if (!parseTileJson(json, d)) return false;
    return configureFrom(d);
}

bool TileJsonTileProvider::fetchSync(const std::string& url) {
    HttpClient client;
    client.addHeader("User-Agent", userAgent());
    client.setBaseUrl("");
    // Off by default in tcxCurl, and some services hand TileJSON back via 302.
    client.setFollowRedirects(true);
    auto res = client.get(url);
    if (!res.ok() || res.body.empty()) return false;
    return configureFromJson(res.body);
}

void TileJsonTileProvider::fetchAsync(const std::string& url,
                                      std::function<void(bool ok)> cb) {
    // Detached-thread pattern, same as NominatimClient::searchAsync. The
    // callback therefore runs HERE, on the worker — see the header warning.
    std::thread([this, url, cb = std::move(cb)]() {
        const bool ok = fetchSync(url);
        if (cb) cb(ok);
    }).detach();
}

TileJsonDoc TileJsonTileProvider::document() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return doc_;
}

std::string TileJsonTileProvider::urlFor(const TileKey& key) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (doc_.tiles.empty()) return {};

    // Spread across mirrors and subdomains by tile index rather than by a
    // counter: the same tile always resolves to the same URL, so a redraw hits
    // the same upstream cache instead of walking every mirror in turn.
    const int spread = key.x + key.y;
    const size_t ti = (size_t)((spread % (int)doc_.tiles.size() + (int)doc_.tiles.size())
                               % (int)doc_.tiles.size());
    std::string out = doc_.tiles[ti];

    if (!subdomains_.empty()) {
        const size_t si = (size_t)((spread % (int)subdomains_.size() + (int)subdomains_.size())
                                   % (int)subdomains_.size());
        UrlTemplateTileProvider::replaceAll(out, "{s}", subdomains_[si]);
    }
    UrlTemplateTileProvider::replaceAll(out, "{z}", std::to_string(key.z));
    UrlTemplateTileProvider::replaceAll(out, "{x}", std::to_string(key.x));
    UrlTemplateTileProvider::replaceAll(out, "{y}", std::to_string(key.y));
    return out;
}

std::string TileJsonTileProvider::userAgent() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return ua_;
}

int TileJsonTileProvider::maxZoom() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return doc_.valid ? doc_.maxzoom : 19;
}

int TileJsonTileProvider::minZoom() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return doc_.valid ? doc_.minzoom : 0;
}

std::string TileJsonTileProvider::attribution() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return doc_.attribution;
}

TileJsonTileProvider& TileJsonTileProvider::setUserAgent(std::string ua) {
    std::lock_guard<std::mutex> lk(mutex_);
    ua_ = std::move(ua);
    return *this;
}

TileJsonTileProvider& TileJsonTileProvider::setSubdomains(std::vector<std::string> hosts) {
    std::lock_guard<std::mutex> lk(mutex_);
    subdomains_ = std::move(hosts);
    return *this;
}

// ------------------------------------------------------------ OSMTileProvider

const char* OSMTileProvider::bundledTileJson() {
    // Verbatim from https://render.openstreetmap.org/tilejson.json (2026-07).
    return R"({
    "tilejson": "3.0.0",
    "attribution": "© <a href='https://www.openstreetmap.org/copyright'>OpenStreetMap</a> contributors",
    "name": "OpenStreetMap Standard",
    "tiles": [
        "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
    ],
    "minzoom": 0,
    "maxzoom": 19
})";
}

OSMTileProvider::OSMTileProvider() {
    configureFromJson(bundledTileJson());

    // The published document names the bare host; OSM's long-standing advice is
    // to spread requests over a/b/c so no single upstream cache carries them
    // all. Rewriting the host into a `{s}` placeholder keeps that behaviour
    // while leaving the document itself as the source of everything else — and
    // a document that already carries `{s}` is left alone.
    std::lock_guard<std::mutex> lk(mutex_);
    subdomains_ = {"a", "b", "c"};
    for (auto& t : doc_.tiles) {
        if (t.find("{s}") != std::string::npos) continue;
        UrlTemplateTileProvider::replaceAll(t, "//tile.openstreetmap.org/",
                                               "//{s}.tile.openstreetmap.org/");
    }
}

} // namespace tcx::openstreetmap
