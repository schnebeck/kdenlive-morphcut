/*
 * kdenlive-morphcut - src/transition_morphcut.cpp
 * "Morph Cut" transition for MLT / Kdenlive.
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
 * How it works
 * ------------
 * The transition covers the overlap of two clips (A below, B above). Instead
 * of blending what both clips show *during* the overlap (that would be the
 * footage that was cut away, e.g. an "uhm"), it takes two key frames:
 *
 *   A0 = frame of clip A at the first frame of the transition
 *   B1 = frame of clip B at the last frame of the transition
 *
 * and synthesizes every frame in between with RIFE frame interpolation. The
 * result joins seamlessly with the material before and after the transition.
 *
 * Key frames are fetched with private producers (the timeline's producers
 * are being read by other threads), so clip effects are cloned onto them.
 */

#include "interpolator.h"

extern "C" {
#include <framework/mlt.h>
}

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef MORPHCUT_DATADIR
#define MORPHCUT_DATADIR ""
#endif

namespace {

const char *kStateProperty = "_morphcut_state";

struct Image
{
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgb;
    bool valid() const { return w > 0 && h > 0 && rgb.size() == size_t(w) * h * 3; }
};

// Where a frame of the timeline comes from.
struct SourceRef
{
    mlt_producer cut = nullptr; // timeline clip (a cut)
    mlt_position start = 0;     // timeline position where the clip starts
    mlt_position source = 0;    // frame number inside the parent producer
};

struct KeyFrames
{
    std::string key;
    Image a0;
    Image b1;
};

// Per-transition state: private producers, key frames, synthesized frames.
struct State
{
    std::mutex mutex;
    mlt_profile profile = nullptr;

    // private producers keyed by the timeline cut they mirror
    struct Private
    {
        mlt_producer parent = nullptr; // timeline parent the clone mirrors
        std::string signature;         // resource + effects, to detect changes
        mlt_producer producer = nullptr;
        mlt_producer cut = nullptr;
    };
    std::map<mlt_producer, Private> privates;

    KeyFrames keys;
    std::string engine_key;
    std::shared_ptr<morphcut::Interpolator> engine;
    std::string engine_error;

    // synthesized frames, keyed by "<keys.key>|<position>", LRU
    std::list<std::pair<std::string, Image>> frames;
    size_t max_frames = 32;

    ~State()
    {
        for (auto &p : privates) {
            if (p.second.cut)
                mlt_producer_close(p.second.cut);
            if (p.second.producer)
                mlt_producer_close(p.second.producer);
        }
    }

    const Image *find_frame(const std::string &k)
    {
        for (auto it = frames.begin(); it != frames.end(); ++it) {
            if (it->first == k) {
                frames.splice(frames.begin(), frames, it);
                return &frames.front().second;
            }
        }
        return nullptr;
    }

    void store_frame(const std::string &k, Image &&img)
    {
        frames.emplace_front(k, std::move(img));
        while (frames.size() > max_frames)
            frames.pop_back();
    }
};

void state_destroy(void *p)
{
    delete static_cast<State *>(p);
}

bool dir_has_model(const std::string &dir)
{
    struct stat st;
    return stat((dir + "/flownet.param").c_str(), &st) == 0;
}

// Resolve the model name from the GUI to a directory.
std::string resolve_model_dir(const char *model)
{
    std::string name = (model && *model) ? model : "rife-v4.22";
    if (name[0] == '/')
        return name;
    std::vector<std::string> roots;
    if (const char *env = getenv("MORPHCUT_MODEL_PATH"))
        roots.push_back(env);
    if (*MORPHCUT_DATADIR)
        roots.push_back(std::string(MORPHCUT_DATADIR) + "/models");
    if (const char *xdg = getenv("XDG_DATA_HOME"))
        roots.push_back(std::string(xdg) + "/morphcut/models");
    if (const char *home = getenv("HOME"))
        roots.push_back(std::string(home) + "/.local/share/morphcut/models");
    if (const char *data = mlt_environment("MLT_DATA"))
        roots.push_back(std::string(data) + "/morphcut/models");
    for (const auto &r : roots) {
        if (dir_has_model(r + "/" + name))
            return r + "/" + name;
    }
    return roots.empty() ? name : roots.front() + "/" + name;
}

// Find the clip shown by a track producer at a timeline position.
// Handles Kdenlive's layout (track = tractor of playlists) and plain playlists.
bool clip_at(mlt_producer producer, mlt_position t, SourceRef &ref, int depth = 0)
{
    if (!producer || depth > 8 || t < 0)
        return false;
    if (mlt_producer_is_cut(producer)) {
        t += mlt_producer_get_in(producer);
        producer = mlt_producer_cut_parent(producer);
    }
    mlt_service service = MLT_PRODUCER_SERVICE(producer);
    switch (mlt_service_identify(service)) {
    case mlt_service_playlist_type: {
        mlt_playlist playlist = (mlt_playlist) producer;
        mlt_service_lock(service);
        bool found = false;
        int index = mlt_playlist_get_clip_index_at(playlist, t);
        mlt_playlist_clip_info info;
        if (index < mlt_playlist_count(playlist) && !mlt_playlist_is_blank(playlist, index)
            && !mlt_playlist_get_clip_info(playlist, &info, index) && info.cut
            && t >= info.start && t < info.start + info.frame_count) {
            mlt_producer cut = info.cut;
            mlt_position offset = t - info.start;
            if (mlt_producer_is_cut(cut)
                && mlt_service_identify(MLT_PRODUCER_SERVICE(mlt_producer_cut_parent(cut)))
                       == mlt_service_tractor_type) {
                // nested sequence: descend
                found = clip_at(cut, offset, ref, depth + 1);
            } else {
                ref.cut = cut;
                ref.start = info.start;
                ref.source = info.frame_in + offset;
                found = true;
            }
        }
        mlt_service_unlock(service);
        return found;
    }
    case mlt_service_tractor_type: {
        mlt_multitrack multitrack = mlt_tractor_multitrack((mlt_tractor) producer);
        if (!multitrack)
            return false;
        // the topmost visible track wins, as in mlt_tractor
        for (int i = mlt_multitrack_count(multitrack) - 1; i >= 0; i--) {
            mlt_producer track = mlt_multitrack_track(multitrack, i);
            if (!track || (mlt_properties_get_int(MLT_PRODUCER_PROPERTIES(track), "hide") & 1))
                continue;
            if (clip_at(track, t, ref, depth + 1))
                return true;
        }
        return false;
    }
    case mlt_service_multitrack_type:
    default:
        return false;
    }
}

// Walk the chain of planted transitions down to the multitrack.
mlt_multitrack find_multitrack(mlt_transition transition)
{
    mlt_service service = mlt_service_producer(MLT_TRANSITION_SERVICE(transition));
    for (int i = 0; service && i < 1000; i++) {
        if (mlt_service_identify(service) == mlt_service_multitrack_type)
            return (mlt_multitrack) service;
        service = mlt_service_producer(service);
    }
    return nullptr;
}

bool track_clip_at(mlt_transition transition, int track, mlt_position t, SourceRef &ref)
{
    mlt_multitrack multitrack = find_multitrack(transition);
    if (!multitrack || track < 0 || track >= mlt_multitrack_count(multitrack))
        return false;
    return clip_at(mlt_multitrack_track(multitrack, track), t, ref);
}

// Build a loader string for a private copy of the cut's parent producer.
std::string producer_spec(mlt_producer parent)
{
    mlt_properties p = MLT_PRODUCER_PROPERTIES(parent);
    const char *resource = mlt_properties_get(p, "resource");
    const char *service = mlt_properties_get(p, "mlt_service");
    if (!resource || !*resource)
        return std::string();
    if (!service || !strcmp(service, "chain") || !strncmp(service, "avformat", 8)
        || strchr(resource, ':'))
        return resource;
    return std::string(service) + ":" + resource;
}

std::string filters_signature(mlt_service service)
{
    std::string sig;
    for (int i = 0; i < mlt_service_filter_count(service); i++) {
        mlt_filter f = mlt_service_filter(service, i);
        mlt_properties fp = MLT_FILTER_PROPERTIES(f);
        if (mlt_properties_get_int(fp, "_loader") || mlt_properties_get_int(fp, "disable"))
            continue;
        const char *s = mlt_properties_get(fp, "mlt_service");
        sig += s ? s : "?";
        sig += '{';
        int n = mlt_properties_count(fp);
        for (int j = 0; j < n; j++) {
            const char *name = mlt_properties_get_name(fp, j);
            if (!name || name[0] == '_')
                continue;
            const char *v = mlt_properties_get_value(fp, j);
            sig += name;
            sig += '=';
            sig += v ? v : "";
            sig += ';';
        }
        sig += '}';
    }
    return sig;
}

void clone_filters(mlt_profile profile, mlt_service from, mlt_producer to)
{
    for (int i = 0; i < mlt_service_filter_count(from); i++) {
        mlt_filter f = mlt_service_filter(from, i);
        mlt_properties fp = MLT_FILTER_PROPERTIES(f);
        if (mlt_properties_get_int(fp, "_loader") || mlt_properties_get_int(fp, "disable"))
            continue;
        const char *service = mlt_properties_get(fp, "mlt_service");
        if (!service)
            continue;
        mlt_filter copy = mlt_factory_filter(profile, service, nullptr);
        if (!copy)
            continue;
        mlt_properties cp = MLT_FILTER_PROPERTIES(copy);
        // Values are copied as strings: keep the numeric locale they were written in.
        if (const char *locale = mlt_properties_get_lcnumeric(fp))
            mlt_properties_set_lcnumeric(cp, locale);
        int n = mlt_properties_count(fp);
        for (int j = 0; j < n; j++) {
            const char *name = mlt_properties_get_name(fp, j);
            if (!name || name[0] == '_' || !strcmp(name, "mlt_service")
                || !strcmp(name, "mlt_type"))
                continue;
            const char *v = mlt_properties_get_value(fp, j);
            if (v)
                mlt_properties_set(cp, name, v);
        }
        mlt_producer_attach(to, copy);
        mlt_filter_close(copy);
    }
}

State::Private *private_for(State &st, mlt_producer cut, bool with_effects)
{
    mlt_producer parent = mlt_producer_cut_parent(cut);
    std::string spec = producer_spec(parent);
    if (spec.empty())
        return nullptr;
    std::string sig = spec + "|" + std::to_string(mlt_producer_get_in(cut)) + "-"
                      + std::to_string(mlt_producer_get_out(cut));
    if (with_effects)
        sig += "|" + filters_signature(MLT_PRODUCER_SERVICE(cut));

    State::Private &priv = st.privates[cut];
    if (priv.producer && priv.signature == sig && priv.parent == parent)
        return &priv;
    if (priv.cut)
        mlt_producer_close(priv.cut);
    if (priv.producer)
        mlt_producer_close(priv.producer);
    priv = State::Private();

    mlt_producer producer = mlt_factory_producer(st.profile, nullptr, spec.c_str());
    if (!producer) {
        st.privates.erase(cut);
        return nullptr;
    }
    // Mirror things like forced frame rate / colorspace of the timeline copy.
    mlt_properties_pass_list(MLT_PRODUCER_PROPERTIES(producer),
                             MLT_PRODUCER_PROPERTIES(parent),
                             "force_aspect_ratio force_fps force_progressive force_tff "
                             "force_colorspace force_full_range video_index autorotate rotate");
    mlt_producer pcut = mlt_producer_cut(producer, mlt_producer_get_in(cut), mlt_producer_get_out(cut));
    if (with_effects)
        clone_filters(st.profile, MLT_PRODUCER_SERVICE(cut), pcut);
    priv.parent = parent;
    priv.signature = sig;
    priv.producer = producer;
    priv.cut = pcut;
    return &priv;
}

bool fetch_image(State &st,
                 const SourceRef &ref,
                 mlt_position source,
                 mlt_frame like,
                 int w,
                 int h,
                 bool with_effects,
                 Image &out)
{
    State::Private *priv = private_for(st, ref.cut, with_effects);
    if (!priv)
        return false;
    mlt_position in = mlt_producer_get_in(ref.cut);
    mlt_position rel = std::max<mlt_position>(0, source - in);
    mlt_producer_seek(priv->cut, rel);
    mlt_frame frame = nullptr;
    if (mlt_service_get_frame(MLT_PRODUCER_SERVICE(priv->cut), &frame, 0) || !frame)
        return false;

    // Let the normalizers scale and pad exactly like the consumer does.
    mlt_properties fp = MLT_FRAME_PROPERTIES(frame);
    mlt_properties lp = MLT_FRAME_PROPERTIES(like);
    mlt_properties_pass(fp, lp, "consumer.");
    mlt_properties_pass_list(fp, lp, "consumer_aspect_ratio consumer_deinterlace rescale.interp deinterlace_method");

    mlt_image_format format = mlt_image_rgb;
    uint8_t *image = nullptr;
    int iw = w, ih = h;
    int error = mlt_frame_get_image(frame, &image, &format, &iw, &ih, 0);
    bool ok = !error && image && format == mlt_image_rgb && iw == w && ih == h;
    if (ok) {
        out.w = w;
        out.h = h;
        out.rgb.assign(image, image + size_t(w) * h * 3);
    } else {
        mlt_log_warning(nullptr,
                        "[morphcut] could not fetch frame %d (%dx%d fmt %d)\n",
                        (int) source,
                        iw,
                        ih,
                        (int) format);
    }
    mlt_frame_close(frame);
    return ok;
}

float ease(float t, int mode)
{
    switch (mode) {
    case 1: // smooth (smoothstep)
        return t * t * (3.f - 2.f * t);
    case 2: // smoother
        return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
    default:
        return t;
    }
}

int output_image(mlt_frame frame,
                 const Image &img,
                 uint8_t **image,
                 mlt_image_format *format,
                 int *width,
                 int *height)
{
    bool rgba = *format == mlt_image_rgba;
    int bpp = rgba ? 4 : 3;
    int size = img.w * img.h * bpp;
    uint8_t *buf = (uint8_t *) mlt_pool_alloc(size);
    if (!buf)
        return 1;
    if (rgba) {
        const uint8_t *s = img.rgb.data();
        uint8_t *d = buf;
        for (int i = 0; i < img.w * img.h; i++, s += 3, d += 4) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
        }
    } else {
        memcpy(buf, img.rgb.data(), size);
    }
    mlt_frame_set_image(frame, buf, size, mlt_pool_release);
    *image = buf;
    *format = rgba ? mlt_image_rgba : mlt_image_rgb;
    *width = img.w;
    *height = img.h;
    mlt_properties props = MLT_FRAME_PROPERTIES(frame);
    mlt_properties_set_int(props, "format", *format);
    mlt_properties_set_int(props, "width", img.w);
    mlt_properties_set_int(props, "height", img.h);
    mlt_properties_set_int(props, "progressive", 1);
    return 0;
}

void crossfade(const Image &a, const Image &b, float t, Image &out)
{
    out.w = a.w;
    out.h = a.h;
    out.rgb.resize(a.rgb.size());
    int wa = int(std::lround((1.f - t) * 256.f));
    for (size_t i = 0; i < a.rgb.size(); i++)
        out.rgb[i] = uint8_t((a.rgb[i] * wa + b.rgb[i] * (256 - wa) + 128) >> 8);
}

// `frame` is the track frame that carries the result, `other` the second one.
int transition_get_image(mlt_frame frame,
                         uint8_t **image,
                         mlt_image_format *format,
                         int *width,
                         int *height,
                         int writable)
{
    mlt_frame_pop_frame(frame); // the other track's frame is not needed
    mlt_transition transition = (mlt_transition) mlt_frame_pop_service(frame);
    mlt_properties tprops = MLT_TRANSITION_PROPERTIES(transition);
    mlt_properties fprops = MLT_FRAME_PROPERTIES(frame);

    char key[64];
    snprintf(key, sizeof(key), "morphcut.%p.", (void *) transition);
    std::string k(key);
    mlt_position pos = mlt_properties_get_position(fprops, (k + "pos").c_str());
    mlt_position len = mlt_properties_get_position(fprops, (k + "len").c_str());

    State *st = (State *) mlt_properties_get_data(tprops, kStateProperty, nullptr);
    int w = *width, h = *height;
    if (w <= 0 || h <= 0 || !st || mlt_frame_is_test_card(frame))
        return mlt_frame_get_image(frame, image, format, width, height, writable);

    int mode = mlt_properties_get_int(tprops, "mode"); // 0 = morph, 1 = crossfade (freeze)
    int easing = mlt_properties_get_int(tprops, "easing");
    bool with_effects = mlt_properties_get_int(tprops, "clip_effects");
    int debug = mlt_properties_get_int(tprops, "debug");

    // Timeline range of the transition and the clips at its borders. The two
    // clips may overlap on two tracks, adjoin on two tracks, or both sit on
    // one track (the transition then lies on an empty track above the cut).
    mlt_position now = mlt_frame_get_position(frame);
    mlt_position first = now - pos;
    mlt_position last = first + len - 1;
    int track_a = mlt_transition_get_a_track(transition);
    int track_b = mlt_transition_get_b_track(transition);
    SourceRef a_in, b_in, a_out, b_out;
    bool have_a_in = track_clip_at(transition, track_a, first, a_in);
    bool have_b_in = track_clip_at(transition, track_b, first, b_in);
    bool have_a_out = track_clip_at(transition, track_a, last, a_out);
    bool have_b_out = track_clip_at(transition, track_b, last, b_out);

    // Outgoing clip at the first frame: by default the one that started earlier.
    int direction = mlt_properties_get_int(tprops, "direction"); // 0 auto, 1 a->b, 2 b->a
    bool from_b;
    if (direction == 1)
        from_b = false;
    else if (direction == 2)
        from_b = true;
    else
        from_b = have_b_in && (!have_a_in || b_in.start < a_in.start);
    SourceRef ra = from_b ? b_in : a_in;
    bool have_a = from_b ? have_b_in : have_a_in;

    // Incoming clip at the last frame: preferably on the other track, but it
    // must be a different clip than the outgoing one.
    SourceRef rb;
    bool have_b = false;
    const SourceRef &other_out = from_b ? a_out : b_out;
    const SourceRef &same_out = from_b ? b_out : a_out;
    bool have_other_out = from_b ? have_a_out : have_b_out;
    bool have_same_out = from_b ? have_b_out : have_a_out;
    if (have_other_out && (direction || !have_a || other_out.cut != ra.cut)) {
        rb = other_out;
        have_b = true;
    } else if (!direction && have_same_out && (!have_a || same_out.cut != ra.cut)) {
        rb = same_out;
        have_b = true;
    }

    if (debug)
        mlt_log_info(MLT_TRANSITION_SERVICE(transition),
                     "[morphcut] t=%d range %d-%d tracks %d/%d from_b=%d A=%d:%d B=%d:%d\n",
                     (int) now,
                     (int) first,
                     (int) last,
                     track_a,
                     track_b,
                     from_b,
                     have_a,
                     (int) ra.source,
                     have_b,
                     (int) rb.source);

    if (!have_a || !have_b || len < 2) {
        mlt_log_warning(MLT_TRANSITION_SERVICE(transition),
                        "[morphcut] no clip change inside the transition (outgoing=%d incoming=%d)\n",
                        have_a,
                        have_b);
        // Nothing to morph between; show the track as it is.
        return mlt_frame_get_image(frame, image, format, width, height, writable);
    }

    mlt_position a0 = ra.source;
    mlt_position b1 = rb.source;

    std::lock_guard<std::mutex> lock(st->mutex);

    char keybuf[512];
    snprintf(keybuf,
             sizeof(keybuf),
             "%p:%d|%p:%d|%dx%d|fx%d",
             (void *) mlt_producer_cut_parent(ra.cut),
             (int) a0,
             (void *) mlt_producer_cut_parent(rb.cut),
             (int) b1,
             w,
             h,
             with_effects ? 1 : 0);
    std::string keyframes_key = keybuf;
    if (st->keys.key != keyframes_key) {
        st->keys = KeyFrames();
        st->frames.clear();
        Image a, b;
        if (!fetch_image(*st, ra, a0, frame, w, h, with_effects, a)
            || !fetch_image(*st, rb, b1, frame, w, h, with_effects, b)) {
            mlt_log_warning(MLT_TRANSITION_SERVICE(transition),
                            "[morphcut] key frames unavailable, falling back to dissolve\n");
            return mlt_frame_get_image(frame, image, format, width, height, writable);
        }
        st->keys.key = keyframes_key;
        st->keys.a0 = std::move(a);
        st->keys.b1 = std::move(b);
        if (debug)
            mlt_log_info(MLT_TRANSITION_SERVICE(transition),
                         "[morphcut] key frames A=%d B=%d (%dx%d)\n",
                         (int) a0,
                         (int) b1,
                         w,
                         h);
    }

    if (pos <= 0)
        return output_image(frame, st->keys.a0, image, format, width, height);
    if (pos >= len - 1)
        return output_image(frame, st->keys.b1, image, format, width, height);

    float t = ease(float(pos) / float(len - 1), easing);

    // Engine configuration from the GUI.
    morphcut::EngineConfig cfg;
    cfg.model_dir = resolve_model_dir(mlt_properties_get(tprops, "model"));
    cfg.gpu = mlt_properties_get_int(tprops, "gpu");
    cfg.tta = mlt_properties_get_int(tprops, "tta");
    cfg.uhd = mlt_properties_get_int(tprops, "uhd");
    cfg.threads = std::max(1, mlt_properties_get_int(tprops, "threads"));

    char frame_key[64];
    snprintf(frame_key, sizeof(frame_key), "|%.6f|m%d|", t, mode);
    std::string fkey = keyframes_key + frame_key + cfg.model_dir + (cfg.tta ? "|tta" : "")
                       + (cfg.uhd ? "|uhd" : "") + "|g" + std::to_string(cfg.gpu);
    if (const Image *cached = st->find_frame(fkey))
        return output_image(frame, *cached, image, format, width, height);

    Image out;
    out.w = w;
    out.h = h;
    out.rgb.resize(size_t(w) * h * 3);
    bool done = false;
    if (mode == 0) {
        std::string ekey = cfg.model_dir + "|" + std::to_string(cfg.gpu) + "|" + std::to_string(cfg.tta)
                           + std::to_string(cfg.uhd) + "|" + std::to_string(cfg.threads);
        if (st->engine_key != ekey) {
            st->engine_key = ekey;
            st->engine_error.clear();
            st->engine = morphcut::acquire_engine(cfg, &st->engine_error);
            if (!st->engine)
                mlt_log_error(MLT_TRANSITION_SERVICE(transition),
                              "[morphcut] %s\n",
                              st->engine_error.c_str());
            else if (debug)
                mlt_log_info(MLT_TRANSITION_SERVICE(transition),
                             "[morphcut] engine %s, model %s\n",
                             st->engine->name(),
                             cfg.model_dir.c_str());
        }
        if (st->engine)
            done = st->engine->interpolate(st->keys.a0.rgb.data(),
                                           st->keys.b1.rgb.data(),
                                           w,
                                           h,
                                           t,
                                           out.rgb.data());
    }
    if (!done)
        crossfade(st->keys.a0, st->keys.b1, t, out);

    int error = output_image(frame, out, image, format, width, height);
    st->store_frame(fkey, std::move(out));
    return error;
}

mlt_frame transition_process(mlt_transition transition, mlt_frame a_frame, mlt_frame b_frame)
{
    // With accepts_blanks one track may be empty here; draw on the other one.
    mlt_frame frame = a_frame, other = b_frame;
    if (mlt_frame_is_test_card(a_frame) && !mlt_frame_is_test_card(b_frame))
        std::swap(frame, other);

    // Positions must be taken now; the transition may move before rendering.
    char key[64];
    snprintf(key, sizeof(key), "morphcut.%p.", (void *) transition);
    std::string k(key);
    mlt_properties props = MLT_FRAME_PROPERTIES(frame);
    mlt_properties_set_position(props,
                                (k + "pos").c_str(),
                                mlt_transition_get_position(transition, frame));
    mlt_properties_set_position(props, (k + "len").c_str(), mlt_transition_get_length(transition));

    mlt_frame_push_service(frame, transition);
    mlt_frame_push_frame(frame, other);
    mlt_frame_push_get_image(frame, transition_get_image);
    return frame;
}

} // namespace

extern "C" mlt_transition transition_morphcut_init(mlt_profile profile,
                                                   mlt_service_type,
                                                   const char *,
                                                   char *arg)
{
    mlt_transition transition = mlt_transition_new();
    if (!transition)
        return nullptr;
    transition->process = transition_process;
    mlt_properties props = MLT_TRANSITION_PROPERTIES(transition);
    mlt_properties_set(props, "model", arg && *arg ? arg : "rife-v4.22");
    mlt_properties_set_int(props, "mode", 0);
    mlt_properties_set_int(props, "easing", 1);
    mlt_properties_set_int(props, "gpu", 0);
    mlt_properties_set_int(props, "tta", 0);
    mlt_properties_set_int(props, "uhd", 0);
    mlt_properties_set_int(props, "threads", 4);
    mlt_properties_set_int(props, "clip_effects", 1);
    mlt_properties_set_int(props, "direction", 0);
    mlt_properties_set_int(props, "debug", 0);
    // video only
    mlt_properties_set_int(props, "_transition_type", 1);
    // also run where one track is empty (clips adjoining or on the same track)
    mlt_properties_set_int(props, "accepts_blanks", 1);

    State *st = new State();
    st->profile = profile;
    mlt_properties_set_data(props, kStateProperty, st, 0, state_destroy, nullptr);
    return transition;
}
