/*
 * kdenlive-morphcut - src/interpolator.cpp
 * Generative frame interpolation backend (RIFE / ncnn).
 *
 *   Copyright (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
 *   Produced by Thorsten Schnebeck - the idea, the decisions, the testing.
 *   Written by Anthropic Claude Opus 5.5 - AI generated content.
 *
 *   Free software under the GNU General Public License, version 3 or later.
 *   There is no warranty, to the extent permitted by law. The full text is in
 *   LICENSES/GPL-3.0-or-later.txt.
 *
 * SPDX-FileCopyrightText: (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
 * SPDX-FileContributor: Anthropic Claude Opus 5.5 (AI generated content)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interpolator.h"

#include "rife.h"

#include <gpu.h>
#include <sys/stat.h>

#include <mutex>
#include <vector>

namespace morphcut {

namespace {

std::mutex g_instance_mutex;
bool g_instance_created = false;

void ensure_gpu_instance()
{
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    if (!g_instance_created) {
        ncnn::create_gpu_instance();
        g_instance_created = true;
    }
}

bool file_exists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

class RifeInterpolator : public Interpolator
{
public:
    RifeInterpolator(const EngineConfig &cfg, bool v2, bool v4, int padding, int gpu)
        : m_rife(gpu, cfg.tta, false, cfg.uhd, cfg.threads, v2, v4, padding)
        , m_gpu(gpu)
    {}

    int load(const std::string &dir) { return m_rife.load(dir); }

    bool interpolate(const unsigned char *rgb0,
                     const unsigned char *rgb1,
                     int w,
                     int h,
                     float timestep,
                     unsigned char *out) override
    {
        if (timestep <= 0.f || timestep >= 1.f)
            return false;
        ncnn::Mat in0(w, h, (void *) rgb0, (size_t) 3, 3);
        ncnn::Mat in1(w, h, (void *) rgb1, (size_t) 3, 3);
        ncnn::Mat outimage(w, h, (void *) out, (size_t) 3, 3);
        // RIFE keeps per-call state on the Vulkan device; serialize inference.
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_rife.process(in0, in1, timestep, outimage) == 0;
    }

    const char *name() const override { return m_gpu < 0 ? "rife-cpu" : "rife-vulkan"; }

private:
    RIFE m_rife;
    int m_gpu;
    std::mutex m_mutex;
};

struct CacheEntry
{
    EngineConfig cfg;
    std::weak_ptr<Interpolator> engine;
};

// The caches are intentionally leaked: ncnn tears down its Vulkan instance in
// a static destructor, and engines must never outlive it during exit.
std::mutex g_cache_mutex;
std::vector<CacheEntry> &g_cache = *new std::vector<CacheEntry>;
// Keep the most recently used engine alive, so that closing the last
// transition (e.g. while editing) does not force a reload of the model.
std::shared_ptr<Interpolator> &g_last_engine = *new std::shared_ptr<Interpolator>;

} // namespace

int gpu_count()
{
    ensure_gpu_instance();
    return ncnn::get_gpu_count();
}

std::shared_ptr<Interpolator> acquire_engine(const EngineConfig &requested, std::string *error)
{
    EngineConfig cfg = requested;
    if (cfg.gpu >= 0) {
        int count = gpu_count();
        if (count <= 0)
            cfg.gpu = -1;
        else if (cfg.gpu >= count)
            cfg.gpu = ncnn::get_default_gpu_index();
    }

    std::lock_guard<std::mutex> lock(g_cache_mutex);
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        auto engine = it->engine.lock();
        if (!engine) {
            it = g_cache.erase(it);
            continue;
        }
        if (it->cfg == cfg)
            return engine;
        ++it;
    }

    if (!file_exists(cfg.model_dir + "/flownet.param") || !file_exists(cfg.model_dir + "/flownet.bin")) {
        if (error)
            *error = "RIFE model not found in " + cfg.model_dir;
        return nullptr;
    }
    // Model generation is encoded in the directory name, as in rife-ncnn-vulkan.
    std::string base = cfg.model_dir.substr(cfg.model_dir.find_last_of('/') + 1);
    bool v4 = base.find("rife-v4") != std::string::npos;
    bool v2 = !v4
              && (base.find("rife-v2") != std::string::npos
                  || base.find("rife-v3") != std::string::npos);
    if (!v4 && !file_exists(cfg.model_dir + "/contextnet.param")) {
        if (error)
            *error = "incomplete RIFE model in " + cfg.model_dir;
        return nullptr;
    }

    // Newer models need a larger input alignment (as in TNTwise/rife-ncnn-vulkan).
    int padding = 32;
    if (base.find("rife-v4.25") != std::string::npos || base.find("rife-v4.26") != std::string::npos)
        padding = 64;
    if (base.find("rife-v4.25-lite") != std::string::npos)
        padding = 128;

    auto engine = std::make_shared<RifeInterpolator>(cfg, v2, v4, padding, cfg.gpu);
    if (engine->load(cfg.model_dir) != 0) {
        if (error)
            *error = "failed to load RIFE model " + cfg.model_dir;
        return nullptr;
    }
    g_cache.push_back({cfg, engine});
    g_last_engine = engine;
    return engine;
}

} // namespace morphcut
