// backup files: every profile of a device in one file. used by the cli (backup / restore) and the gui
//
// "RSWB", format version, then for version 2 the number of profiles and per profile a 16 bit length (low byte first)
// and the profile as rockcat_profile_export gives it. version 1 was the Kone AIMO Remastered only layout of
// rossat-swarm (five times the settings report and the button table, no lengths), it still restores

#include "internal.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr char MAGIC[4] = {'R', 'S', 'W', 'B'};
// per profile in a version 1 backup: the 126 byte settings report and 24 buttons at 3 bytes each
constexpr size_t V1_PROFILE = 126 + 3 * 24;

bool slurp(const char *path, std::vector<uint8_t> &out)
{
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    out.resize(1 << 20);
    out.resize(std::fread(out.data(), 1, out.size(), f));
    std::fclose(f);
    return true;
}

} // namespace

extern "C" int rockcat_backup_save(rockcat_device *dev, const char *path)
{
    if (!dev || !path)
        return ROCKCAT_ERR_ARGUMENT;
    const rockcat_caps *caps = rockcat_get_caps(dev);
    if (!caps)
        return ROCKCAT_ERR_UNSUPPORTED;
    int count = caps->profile_count;
    std::vector<uint8_t> out = {'R', 'S', 'W', 'B', 2, static_cast<uint8_t>(count)};
    for (int i = 0; i < count; ++i) {
        rockcat_profile *p = nullptr;
        int r = rockcat_profile_read(dev, i, &p);
        if (r != ROCKCAT_OK)
            return r;
        int n = rockcat_profile_export(p, nullptr, 0);
        if (n < 0) {
            rockcat_profile_free(p);
            return n;
        }
        std::vector<uint8_t> blob(static_cast<size_t>(n));
        rockcat_profile_export(p, blob.data(), blob.size());
        rockcat_profile_free(p);
        out.push_back(static_cast<uint8_t>(blob.size() & 0xff));
        out.push_back(static_cast<uint8_t>(blob.size() >> 8));
        out.insert(out.end(), blob.begin(), blob.end());
    }
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return ROCKCAT_ERR_ACCESS;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = std::fclose(f) == 0 && ok;
    return ok ? count : ROCKCAT_ERR_ACCESS;
}

extern "C" int rockcat_backup_restore(rockcat_device *dev, const char *path, int profile)
{
    if (!dev || !path)
        return ROCKCAT_ERR_ARGUMENT;
    std::vector<uint8_t> in;
    if (!slurp(path, in))
        return ROCKCAT_ERR_ACCESS;
    if (in.size() < 6 || std::memcmp(in.data(), MAGIC, 4) != 0 || (in[4] != 1 && in[4] != 2))
        return ROCKCAT_ERR_PROTOCOL;
    rockcat_device_info info;
    int r = rockcat_get_info(dev, &info);
    if (r != ROCKCAT_OK)
        return r;
    std::vector<std::vector<uint8_t>> blobs;
    if (in[4] == 1) {
        // the old layout has no idea which mouse it came from, it only ever came from this one
        if (info.product_id != ROCKCAT_PID_KONE_AIMO_REMASTERED || in.size() != 5 + 5 * V1_PROFILE)
            return ROCKCAT_ERR_UNSUPPORTED;
        for (uint8_t i = 0; i < 5; ++i) {
            std::vector<uint8_t> blob = {'R', 'C', 'P', 'F', 1, static_cast<uint8_t>(info.product_id & 0xff),
                                         static_cast<uint8_t>(info.product_id >> 8), i};
            blob.insert(blob.end(), in.begin() + 5 + i * V1_PROFILE, in.begin() + 5 + (i + 1) * V1_PROFILE);
            blobs.push_back(blob);
        }
    } else {
        size_t pos = 6;
        for (int i = 0; i < in[5]; ++i) {
            if (pos + 2 > in.size())
                return ROCKCAT_ERR_PROTOCOL;
            size_t len = in[pos] | (in[pos + 1] << 8);
            pos += 2;
            if (pos + len > in.size())
                return ROCKCAT_ERR_PROTOCOL;
            blobs.emplace_back(in.begin() + static_cast<long>(pos), in.begin() + static_cast<long>(pos + len));
            pos += len;
        }
    }
    if (profile >= static_cast<int>(blobs.size()))
        return ROCKCAT_ERR_ARGUMENT;
    // every profile in the file has to fit before the first one gets written, half a restore helps nobody
    std::vector<rockcat_profile *> ready;
    for (const auto &blob : blobs) {
        rockcat_profile *p = nullptr;
        if (rockcat_profile_import(dev, blob.data(), blob.size(), &p) != ROCKCAT_OK) {
            for (rockcat_profile *q : ready)
                rockcat_profile_free(q);
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        ready.push_back(p);
    }
    int done = 0;
    r = ROCKCAT_OK;
    for (size_t n = 0; n < ready.size(); ++n) {
        if (r == ROCKCAT_OK && (profile < 0 || profile == static_cast<int>(n))) {
            r = rockcat_profile_set_index(ready[n], static_cast<int>(n));
            if (r == ROCKCAT_OK)
                r = rockcat_profile_write(dev, ready[n]);
            if (r == ROCKCAT_OK)
                ++done;
        }
        rockcat_profile_free(ready[n]);
    }
    return r == ROCKCAT_OK ? done : r;
}
