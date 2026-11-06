#include "core/rules/MapGenerator.h"

#include <algorithm>
#include <cmath>

#include "core/content/ContentRegistry.h"
#include "core/util/Rng.h"

namespace kot::core {

namespace {

using Field = QList<double>;

// Random noise smoothed by repeated 3x3 averaging, then stretched to 0..1.
Field smoothNoise(int w, int h, Rng& rng, int passes) {
    Field f(w * h);
    for (double& v : f) v = rng.nextDouble();
    Field tmp(w * h);
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double sum = 0;
                int count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        sum += f[ny * w + nx];
                        ++count;
                    }
                }
                tmp[y * w + x] = sum / count;
            }
        }
        f = tmp;
    }
    const auto [lo, hi] = std::minmax_element(f.begin(), f.end());
    const double range = std::max(1e-9, *hi - *lo);
    const double low = *lo;
    for (double& v : f) v = (v - low) / range;
    return f;
}

}  // namespace

bool generateMap(const ContentRegistry& content, const MapGenParams& p, GameMap* out,
                 QString* error) {
    // The terrains the generator places, looked up once.
    struct {
        TerrainId sea, sand, grass, dirt, forest, swamp, hills, snow, mountains, river;
    } T;
    const std::pair<TerrainId*, const char*> needed[] = {{&T.sea, "sea"},     {&T.sand, "sand"},   {&T.grass, "grass"}, {&T.dirt, "dirt"},
                                                          {&T.forest, "forest"}, {&T.swamp, "swamp"}, {&T.hills, "hills"}, {&T.snow, "snow"},
                                                          {&T.mountains, "mountains"}, {&T.river, "river"}};
    for (const auto& [id, key] : needed) {
        *id = content.terrainId(QString::fromLatin1(key));
        if (!id->isValid()) {
            if (error) *error = QStringLiteral("terrain '%1' is missing from the content").arg(key);
            return false;
        }
    }
    if (p.width < 8 || p.height < 8 || p.width > 1024 || p.height > 1024) {
        if (error) *error = QStringLiteral("map size must be between 8 and 1024");
        return false;
    }

    const int w = p.width, h = p.height;
    Rng rng(p.seed);
    Field elevation = smoothNoise(w, h, rng, 6);
    const Field moisture = smoothNoise(w, h, rng, 5);
    // Climate: cold on one side of the map, hot on the other (which side: by the seed),
    // with large patches, so snow, deserts and swamps come as regions, not specks.
    Field temperature = smoothNoise(w, h, rng, 8);
    {
        const int side = rng.nextInt(0, 3);  // where it is cold: top, right, bottom, left
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const double fy = (y + 0.5) / h, fx = (x + 0.5) / w;
                const double cold = side == 0 ? fy : side == 1 ? 1 - fx : side == 2 ? 1 - fy : fx;  // 0 cold .. 1 hot
                double& t = temperature[y * w + x];
                t = 0.45 * t + 0.55 * cold;
            }
    }
    const TerrainId lava = content.terrainId(QStringLiteral("lava"));  // optional

    // Pull the edges down so the map is land in the middle and sea around it.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double dx = std::abs((x + 0.5) / w - 0.5) * 2.0;
            const double dy = std::abs((y + 0.5) / h - 0.5) * 2.0;
            const double edge = std::max(dx, dy);
            elevation[y * w + x] = elevation[y * w + x] * 0.7 + (1.0 - edge) * 0.3;
        }
    }

    // Convert the sea level setting into an elevation value (a quantile).
    Field sorted = elevation;
    std::sort(sorted.begin(), sorted.end());
    const auto quantile = [&](double fraction) {
        const double f = std::clamp(fraction, 0.0, 1.0);
        return sorted[std::min<qsizetype>(sorted.size() - 1, static_cast<qsizetype>(sorted.size() * f))];
    };
    const double seaElev = quantile(p.seaLevel);
    const double mountainElev = quantile(1.0 - p.mountainFraction);
    const double hillElev = std::max(quantile(1.0 - p.mountainFraction - p.hillFraction), seaElev + 0.03);

    QVector<TerrainId> tiles(w * h);
    for (int i = 0; i < w * h; ++i) {
        const double e = elevation[i], m = moisture[i], heat = temperature[i];
        TerrainId t;
        if (e < seaElev) t = T.sea;
        else if (e < seaElev + 0.02) t = heat < 0.25 ? T.snow : T.sand;  // beaches (frozen in the cold)
        else if (e >= mountainElev) t = (lava.isValid() && heat > 0.78 && m < 0.35) ? lava : T.mountains;
        else if (e >= hillElev) t = heat < 0.38 ? T.snow : T.hills;
        else if (heat < 0.2) t = T.snow;                                     // tundra
        else if (m > 0.68 && e < seaElev + 0.16 && heat > 0.35) t = T.swamp;  // wet lowland
        else if (heat > 0.7 && m < 0.42) t = T.sand;                         // desert
        else if (m > 0.55) t = T.forest;
        else if (m < 0.3) t = T.dirt;
        else t = T.grass;
        tiles[i] = t;
    }

    // The outermost ring is always sea, so the map is surrounded by water.
    for (int x = 0; x < w; ++x) {
        tiles[x] = T.sea;
        tiles[(h - 1) * w + x] = T.sea;
    }
    for (int y = 0; y < h; ++y) {
        tiles[y * w] = T.sea;
        tiles[y * w + w - 1] = T.sea;
    }

    // Mountain ranges: ridges across the land with passes of hills every few tiles,
    // so they shape the map without cutting it in two.
    const int ridges = p.ridges >= 0 ? p.ridges : 1 + w * h / 1800;
    for (int r = 0; r < ridges; ++r) {
        int x = rng.nextInt(3, w - 4), y = rng.nextInt(3, h - 4);
        if (tiles[y * w + x] == T.sea) continue;
        const int dirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
        int d = rng.nextInt(0, 7);
        const int length = rng.nextInt(6, 14);
        int sincePass = 0, passAt = rng.nextInt(3, 5);
        for (int step = 0; step < length; ++step) {
            if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2) break;
            const int i = y * w + x;
            if (tiles[i] == T.sea) break;
            if (++sincePass >= passAt) {  // a pass
                tiles[i] = T.hills;
                sincePass = 0;
                passAt = rng.nextInt(3, 5);
            } else {
                tiles[i] = T.mountains;
                // Foothills on the sides.
                for (const int side : {-1, 1}) {
                    const int sx = x + dirs[(d + 2 * side + 8) % 8][0], sy = y + dirs[(d + 2 * side + 8) % 8][1];
                    const int si = sy * w + sx;
                    if (sx > 0 && sy > 0 && sx < w - 1 && sy < h - 1 && rng.nextInt(0, 1) == 0 &&
                        (tiles[si] == T.grass || tiles[si] == T.dirt))
                        tiles[si] = T.hills;
                }
            }
            if (rng.nextInt(0, 3) == 0) d = (d + (rng.nextInt(0, 1) ? 1 : 7)) % 8;  // bend a little
            x += dirs[d][0];
            y += dirs[d][1];
        }
    }

    QList<int> mountains;
    for (int i = 0; i < w * h; ++i) {
        if (tiles[i] == T.mountains) mountains.append(i);
    }

    // Rivers: from a random mountain, always step to the lowest neighbour
    // until reaching the sea, another river, or a dead end.
    for (int r = 0; r < p.rivers && !mountains.isEmpty(); ++r) {
        int cur = mountains[rng.nextInt(0, static_cast<int>(mountains.size()) - 1)];
        for (int length = 0; length < w + h; ++length) {
            const int cx = cur % w, cy = cur / w;
            int next = -1;
            double lowest = elevation[cur];
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx == 0 && dy == 0) || (dx != 0 && dy != 0)) continue;
                    const int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const int ni = ny * w + nx;
                    if (elevation[ni] < lowest) {
                        lowest = elevation[ni];
                        next = ni;
                    }
                }
            }
            if (next < 0) break;
            if (tiles[next] == T.sea || tiles[next] == T.river) break;
            if (tiles[next] != T.mountains) tiles[next] = T.river;
            cur = next;
        }
    }

    // Islands: wavy sea channels (3 tiles wide) cut the land into a grid of pieces.
    if (p.islands > 1) {
        int cols = 1, rows = 1;
        while (cols * rows < p.islands) (cols <= rows ? cols : rows) += 1;
        const double phase = double(p.seed % 1000) / 1000.0 * 6.28;
        for (int i = 1; i < cols; ++i)
            for (int y = 0; y < h; ++y) {
                const int cx = w * i / cols + int(2.0 * std::sin(y * 0.25 + phase + i));
                for (int x = cx - 1; x <= cx + 1; ++x)
                    if (x >= 0 && x < w) tiles[y * w + x] = T.sea;
            }
        for (int j = 1; j < rows; ++j)
            for (int x = 0; x < w; ++x) {
                const int cy = h * j / rows + int(2.0 * std::sin(x * 0.25 + phase + 2 * j));
                for (int y = cy - 1; y <= cy + 1; ++y)
                    if (y >= 0 && y < h) tiles[y * w + x] = T.sea;
            }
        // Rivers that ran into a channel end at the sea; fix shores to sand next to the new sea.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                TerrainId& t = tiles[y * w + x];
                if (t == T.sea || t == T.river || t == T.mountains) continue;
                bool shore = false;
                for (int dy = -1; dy <= 1 && !shore; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h && tiles[ny * w + nx] == T.sea) shore = true;
                    }
                if (shore && (t == T.grass || t == T.dirt)) t = T.sand;
            }
    }

    *out = GameMap::fromCells(w, h, std::move(tiles));
    return true;
}

}  // namespace kot::core
