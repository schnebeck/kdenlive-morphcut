/*
 * kdenlive-morphcut - src/factory.cpp
 * MLT module registration for morphcut.
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

extern "C" {
#include <framework/mlt.h>
}

#include <string>
#include <sys/stat.h>

#ifndef MORPHCUT_DATADIR
#define MORPHCUT_DATADIR ""
#endif

extern "C" mlt_transition transition_morphcut_init(mlt_profile profile,
                                                   mlt_service_type type,
                                                   const char *id,
                                                   char *arg);

static mlt_properties metadata(mlt_service_type, const char *, void *data)
{
    const char *file = (const char *) data;
    std::string candidates[]
        = {std::string(MORPHCUT_DATADIR) + "/" + file,
           std::string(mlt_environment("MLT_DATA") ? mlt_environment("MLT_DATA") : "") + "/morphcut/"
               + file};
    for (const auto &path : candidates) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0)
            return mlt_properties_parse_yaml(path.c_str());
    }
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) MLT_REPOSITORY
{
    MLT_REGISTER(mlt_service_transition_type, "morphcut", transition_morphcut_init);
    MLT_REGISTER_METADATA(mlt_service_transition_type,
                          "morphcut",
                          metadata,
                          (void *) "transition_morphcut.yml");
}
