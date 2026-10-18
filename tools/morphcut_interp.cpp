/*
 * kdenlive-morphcut - tools/morphcut_interp.cpp
 * Command line test for the interpolation backend.
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
 *
 *
 *   morphcut-interp <model_dir> <a.png> <b.png> <frames> <out_prefix> [gpu]
 *
 * Writes <frames> images out_prefix_000.png ... including both key frames.
 */

#include "interpolator.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: %s <model_dir> <a.png> <b.png> <frames> <out_prefix> [gpu]\n", argv[0]);
        return 1;
    }
    int wa, ha, ca, wb, hb, cb;
    unsigned char *a = stbi_load(argv[2], &wa, &ha, &ca, 3);
    unsigned char *b = stbi_load(argv[3], &wb, &hb, &cb, 3);
    if (!a || !b || wa != wb || ha != hb) {
        fprintf(stderr, "cannot load images or size mismatch\n");
        return 1;
    }
    int frames = atoi(argv[4]);
    morphcut::EngineConfig cfg;
    cfg.model_dir = argv[1];
    cfg.gpu = argc > 6 ? atoi(argv[6]) : 0;
    std::string err;
    auto engine = morphcut::acquire_engine(cfg, &err);
    if (!engine) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    fprintf(stderr, "engine: %s, %dx%d\n", engine->name(), wa, ha);
    std::vector<unsigned char> out(size_t(wa) * ha * 3);
    for (int i = 0; i < frames; i++) {
        float t = frames > 1 ? float(i) / float(frames - 1) : 0.f;
        const unsigned char *img = a;
        auto t0 = std::chrono::steady_clock::now();
        if (i == frames - 1)
            img = b;
        else if (i > 0) {
            if (!engine->interpolate(a, b, wa, ha, t, out.data())) {
                fprintf(stderr, "interpolation failed\n");
                return 1;
            }
            img = out.data();
        }
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        char name[1024];
        snprintf(name, sizeof(name), "%s_%03d.png", argv[5], i);
        stbi_write_png(name, wa, ha, 3, img, wa * 3);
        fprintf(stderr, "t=%.3f  %.1f ms  %s\n", t, ms, name);
    }
    stbi_image_free(a);
    stbi_image_free(b);
    return 0;
}
