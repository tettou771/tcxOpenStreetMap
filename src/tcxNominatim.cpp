#include "tcxNominatim.h"

#include <tcxCurl.h>
#include <nlohmann/json.hpp>
#include <TrussC.h>

#include <cmath>
#include <cstring>

namespace tcx {

// ---------------------------------------------------------------- helpers

std::string NominatimClient::urlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

void NominatimClient::waitForRateLimit() {
    if (minIntervalMs_ <= 0) return;
    std::unique_lock<std::mutex> lk(throttleMutex_);
    auto now = std::chrono::steady_clock::now();
    if (lastRequest_ != std::chrono::steady_clock::time_point{}) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - lastRequest_).count();
        if (elapsed < minIntervalMs_) {
            auto wait = std::chrono::milliseconds(minIntervalMs_ - (int)elapsed);
            // Release the mutex while sleeping so another async call queues
            // behind us instead of racing on the timer.
            lk.unlock();
            std::this_thread::sleep_for(wait);
            lk.lock();
        }
    }
    lastRequest_ = std::chrono::steady_clock::now();
}

static GeocodingResult parseEntry(const nlohmann::json& j) {
    GeocodingResult r{};
    auto getStr = [&](const char* k) -> std::string {
        if (!j.contains(k)) return {};
        if (j[k].is_string()) return j[k].get<std::string>();
        return {};
    };
    auto getDouble = [&](const char* k, double dflt = 0.0) -> double {
        if (!j.contains(k)) return dflt;
        if (j[k].is_number()) return j[k].get<double>();
        if (j[k].is_string()) {
            try { return std::stod(j[k].get<std::string>()); }
            catch (...) { return dflt; }
        }
        return dflt;
    };

    r.center.lat   = getDouble("lat");
    r.center.lon   = getDouble("lon");
    r.displayName  = getStr("display_name");
    r.category     = getStr("class");
    r.type         = getStr("type");
    r.importance   = getDouble("importance", 0.0);

    // boundingbox = [south, north, west, east] as strings.
    if (j.contains("boundingbox") && j["boundingbox"].is_array()
        && j["boundingbox"].size() == 4) {
        try {
            double s = std::stod(j["boundingbox"][0].get<std::string>());
            double n = std::stod(j["boundingbox"][1].get<std::string>());
            double w = std::stod(j["boundingbox"][2].get<std::string>());
            double e = std::stod(j["boundingbox"][3].get<std::string>());
            r.bboxSW = {s, w};
            r.bboxNE = {n, e};
        } catch (...) {
            r.bboxSW = r.center;
            r.bboxNE = r.center;
        }
    } else {
        r.bboxSW = r.center;
        r.bboxNE = r.center;
    }

    r.valid = true;
    return r;
}

// ----------------------------------------------------------- sync API

std::vector<GeocodingResult> NominatimClient::search(const std::string& query, int limit) {
    waitForRateLimit();
    if (limit < 1) limit = 1;
    if (limit > 50) limit = 50;       // Nominatim hard cap

    std::string url = server_
        + "/search?q=" + urlEncode(query)
        + "&format=json&addressdetails=0&limit=" + std::to_string(limit);

    HttpClient client;
    client.addHeader("User-Agent", ua_);
    if (!lang_.empty()) client.addHeader("Accept-Language", lang_);
    client.setBaseUrl("");
    auto res = client.get(url);

    std::vector<GeocodingResult> out;
    if (!res.ok() || res.body.empty()) return out;

    auto j = nlohmann::json::parse(res.body, nullptr, /*allow_exceptions*/false);
    if (!j.is_array()) return out;
    out.reserve(j.size());
    for (auto& entry : j) {
        if (!entry.is_object()) continue;
        out.push_back(parseEntry(entry));
    }
    return out;
}

GeocodingResult NominatimClient::reverse(LatLon p, int zoom) {
    waitForRateLimit();
    if (zoom < 0)  zoom = 0;
    if (zoom > 18) zoom = 18;

    char latBuf[32], lonBuf[32];
    std::snprintf(latBuf, sizeof(latBuf), "%.7f", p.lat);
    std::snprintf(lonBuf, sizeof(lonBuf), "%.7f", p.lon);
    std::string url = server_
        + "/reverse?lat=" + latBuf
        + "&lon=" + lonBuf
        + "&zoom=" + std::to_string(zoom)
        + "&format=json&addressdetails=0";

    HttpClient client;
    client.addHeader("User-Agent", ua_);
    if (!lang_.empty()) client.addHeader("Accept-Language", lang_);
    client.setBaseUrl("");
    auto res = client.get(url);

    GeocodingResult out{};
    if (!res.ok() || res.body.empty()) return out;

    auto j = nlohmann::json::parse(res.body, nullptr, false);
    if (!j.is_object()) return out;
    return parseEntry(j);
}

// ----------------------------------------------------------- async API

void NominatimClient::searchAsync(const std::string& query,
                                  std::function<void(std::vector<GeocodingResult>)> cb,
                                  int limit) {
    std::thread([this, query, cb = std::move(cb), limit]() {
        auto results = search(query, limit);
        if (cb) cb(std::move(results));
    }).detach();
}

void NominatimClient::reverseAsync(LatLon p,
                                   std::function<void(GeocodingResult)> cb,
                                   int zoom) {
    std::thread([this, p, cb = std::move(cb), zoom]() {
        auto r = reverse(p, zoom);
        if (cb) cb(std::move(r));
    }).detach();
}

// ----------------------------------------------------------- bbox -> zoom

double NominatimClient::zoomForBoundingBox(const GeocodingResult& r,
                                           float viewportW, float viewportH) {
    if (!r.valid) return 13.0;
    // Match TrussPhoto's heuristic: pick the highest zoom (0.1 steps) where
    // the bbox * 2 still fits inside the viewport.
    for (double z = 18.0; z >= 1.0; z -= 0.1) {
        WorldPixel pSW = latLonToWorldPixel(r.bboxSW, z);
        WorldPixel pNE = latLonToWorldPixel(r.bboxNE, z);
        double bboxW = std::abs(pNE.x - pSW.x);
        double bboxH = std::abs(pSW.y - pNE.y);
        if (bboxW * 2.0 < (double)viewportW && bboxH * 2.0 < (double)viewportH) {
            return std::ceil(z * 10.0) / 10.0;
        }
    }
    return 13.0;
}

} // namespace tcx
