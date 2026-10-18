/*
 * kdenlive-morphcut - src/interpolator.h
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

#ifndef MORPHCUT_INTERPOLATOR_H
#define MORPHCUT_INTERPOLATOR_H

#include <memory>
#include <string>

namespace morphcut {

struct EngineConfig
{
    std::string model_dir; // directory holding flownet.param/.bin
    int gpu = 0;           // Vulkan device index, -1 = CPU
    bool tta = false;      // test-time augmentation (slower, cleaner)
    bool uhd = false;      // UHD mode (for >= 4K material)
    int threads = 4;

    bool operator==(const EngineConfig &o) const
    {
        return model_dir == o.model_dir && gpu == o.gpu && tta == o.tta && uhd == o.uhd
               && threads == o.threads;
    }
};

// Interpolates between two packed RGB24 images of identical size.
class Interpolator
{
public:
    virtual ~Interpolator() = default;
    // timestep in (0,1); out must hold w*h*3 bytes. Returns true on success.
    virtual bool interpolate(const unsigned char *rgb0,
                             const unsigned char *rgb1,
                             int w,
                             int h,
                             float timestep,
                             unsigned char *out)
        = 0;
    virtual const char *name() const = 0;
};

// Returns a shared engine for the given configuration, or nullptr if the
// model could not be loaded. Thread-safe; the engine serializes inference.
std::shared_ptr<Interpolator> acquire_engine(const EngineConfig &cfg, std::string *error);

// Number of Vulkan devices available to ncnn.
int gpu_count();

} // namespace morphcut

#endif
