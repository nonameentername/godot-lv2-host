#include "lilv_host.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
//#include <exception>
#include <iostream>
#include <sndfile.h>
#include <vector>

using namespace godot;

// helpers
static inline const char *cstr_or(const char *s, const char *fb = "") {
    return s ? s : fb;
}
static inline std::string to_string_safe(const char *s, const char *fb = "") {
    const char* value = s ? s : fb;
    return std::string(value);
}

// ============================================================
// Optional debug guards (enable with -DLILVHOST_DEBUG_GUARDS)
// ============================================================
#ifdef LILVHOST_DEBUG_GUARDS
#define LILVHOST_DBG 1
#else
#define LILVHOST_DBG 0
#endif

// ========== Robust RAII WAV writer (non-copyable, movable) ==========
struct WavWriter {
    SNDFILE *f = nullptr;
    SF_INFO info{};

    WavWriter() = default;
    ~WavWriter() {
        close();
    }

    WavWriter(const WavWriter &) = delete;
    WavWriter &operator=(const WavWriter &) = delete;

    WavWriter(WavWriter &&other) noexcept {
        *this = std::move(other);
    }
    WavWriter &operator=(WavWriter &&other) noexcept {
        if (this != &other) {
            close();
            f = other.f;
            info = other.info;
            other.f = nullptr;
            other.info = {};
        }
        return *this;
    }

    bool open(const char *path, int sr, int channels) {
        close(); // just in case
        info = {};
        info.samplerate = sr;
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

        f = sf_open(path, SFM_WRITE, &info);
        if (!f) {
            const char *err = sf_strerror(nullptr);
            std::fprintf(stderr, "[WavWriter] sf_open failed for '%s': %s\n", path ? path : "(null)",
                         err ? err : "(unknown)");
            return false;
        }
        return true;
    }

    void write_interleaved(const float *const *ch, size_t frames, int channels) {
        if (!f || !ch || channels <= 0 || frames == 0) {
            return;
        }

        std::vector<short> tmp(frames * (size_t)channels);
        for (size_t i = 0; i < frames; ++i) {
            for (int c = 0; c < channels; ++c) {
                float v = ch[c] ? ch[c][i] : 0.0f;
                if (v > 1.f) {
                    v = 1.f;
                }
                if (v < -1.f) {
                    v = -1.f;
                }
                tmp[i * channels + c] = (short)std::lrintf(v * 32767.f);
            }
        }

        sf_count_t want = (sf_count_t)tmp.size();
        sf_count_t wrote = sf_write_short(f, tmp.data(), want);
        if (wrote != want) {
            const int errc = sf_error(f);
            const char *msg = sf_error_number(errc);
            std::fprintf(stderr, "[WavWriter] sf_write_short wrote %lld/%lld: %s\n", (long long)wrote, (long long)want,
                         msg ? msg : "(unknown)");
        }
    }

    void close() {
        if (f) {
            (void)sf_write_sync(f);
            (void)sf_close(f);
            f = nullptr;
        }
    }
};

// ===================== Canary helpers (debug only) =====================
#if LILVHOST_DBG
namespace {
// ---- Atom redzone (bytes, placed at end of vector) ----
constexpr uint32_t kGuardPattern = 0xA5A5A5A5u;
constexpr uint32_t kGuardWords = 16; // 64 bytes
static inline size_t words_for_bytes_with_guard(uint32_t bytes) {
    const size_t W = sizeof(std::max_align_t);
    const size_t bytes_with_guard = (size_t)bytes + (size_t)kGuardWords * sizeof(uint32_t);
    return (bytes_with_guard + W - 1) / W;
}
static inline void guard_init(std::vector<std::max_align_t> &buf) {
    uint8_t *base = reinterpret_cast<uint8_t *>(buf.data());
    const size_t total_bytes = buf.size() * sizeof(std::max_align_t);
    if (total_bytes < kGuardWords * sizeof(uint32_t)) {
        return;
    }
    uint32_t *guard = reinterpret_cast<uint32_t *>(base + total_bytes - kGuardWords * sizeof(uint32_t));
    for (uint32_t i = 0; i < kGuardWords; ++i) {
        guard[i] = kGuardPattern;
    }
}
static inline bool guard_ok(const std::vector<std::max_align_t> &buf) {
    const uint8_t *base = reinterpret_cast<const uint8_t *>(buf.data());
    const size_t total_bytes = buf.size() * sizeof(std::max_align_t);
    if (total_bytes < kGuardWords * sizeof(uint32_t)) {
        return true;
    }
    const uint32_t *guard = reinterpret_cast<const uint32_t *>(base + total_bytes - kGuardWords * sizeof(uint32_t));
    for (uint32_t i = 0; i < kGuardWords; ++i) {
        if (guard[i] != kGuardPattern) {
            return false;
        }
    }
    return true;
}
static inline uint32_t usable_bytes_without_guard(const std::vector<std::max_align_t> &buf) {
    const size_t total = buf.size() * sizeof(std::max_align_t);
    const size_t guard_bytes = (size_t)kGuardWords * sizeof(uint32_t);
    return (total > guard_bytes) ? (uint32_t)(total - guard_bytes) : 0u;
}
// ---- Audio/CV tail redzone (samples) ----
constexpr uint32_t kSampleGuard = 0x7FC00000u; // NaN bit pattern
constexpr uint32_t kTailGuardSamples = 64;
static inline void add_tail_guard(std::vector<float> &v) {
    size_t old = v.size();
    v.resize(old + kTailGuardSamples, 0.0f);
    float *g = v.data() + old;
    for (uint32_t i = 0; i < kTailGuardSamples; ++i) {
        uint32_t *p = reinterpret_cast<uint32_t *>(g + i);
        *p = kSampleGuard;
    }
}
static inline bool tail_guard_ok(const std::vector<float> &v) {
    if (v.size() < kTailGuardSamples) {
        return true;
    }
    const float *g = v.data() + (v.size() - kTailGuardSamples);
    for (uint32_t i = 0; i < kTailGuardSamples; ++i) {
        const uint32_t *p = reinterpret_cast<const uint32_t *>(g + i);
        if (*p != kSampleGuard) {
            return false;
        }
    }
    return true;
}
} // namespace
#endif // LILVHOST_DBG

// ===== LilvHost =====
LilvHost::LilvHost(double sr, uint32_t block_frames, uint32_t seq_bytes)
    : sr(sr), block(block_frames), seq_bytes(seq_bytes) {
    // Ensure a sane minimum capacity (bytes) for atom sequences
    const uint32_t min_header = (uint32_t)(sizeof(LV2_Atom) + sizeof(LV2_Atom_Sequence_Body));
    seq_capacity_hint = std::max<uint32_t>(seq_bytes, min_header + 256);

    // URID map/unmap
    map.handle = this;
    map.map = &LilvHost::s_map_cb;
    feat_map.URI = LV2_URID__map;
    feat_map.data = &map;
    unmap.handle = this;
    unmap.unmap = &LilvHost::s_unmap_cb;
    feat_unmap.URI = LV2_URID__unmap;
    feat_unmap.data = &unmap;

    premap_common_uris();
    rebuild_options();

    // buf-size policy
    feat_buf_fixed.URI = LV2_BUF_SIZE__fixedBlockLength;
    feat_buf_bounded.URI = LV2_BUF_SIZE__boundedBlockLength;
    feat_buf_pow2.URI = LV2_BUF_SIZE__powerOf2BlockLength;

    // lv2:log
    log.handle = this;
    log.printf = &LilvHost::s_log_printf;
    log.vprintf = &LilvHost::s_log_vprintf;
    feat_log.URI = LV2_LOG__log;
    feat_log.data = &log;

    // lv2:worker proxy
    worker.sched.handle = &worker;
    worker.sched.schedule_work = &LilvHost::s_schedule_work;
    feat_worker.URI = LV2_WORKER__schedule;
    feat_worker.data = &worker.sched;

    // lv2:state helpers
    state_map.handle = this;
    state_map.absolute_path = &LilvHost::s_state_abs_path;
    state_map.abstract_path = &LilvHost::s_state_abspath_to_abstract;
    state_make.handle = this;
    state_make.path = &LilvHost::s_state_make_path;
    state_free.handle = this;
    state_free.free_path = &LilvHost::s_state_free_path;
    feat_state_map.URI = LV2_STATE__mapPath;
    feat_state_map.data = &state_map;
    feat_state_make.URI = LV2_STATE__makePath;
    feat_state_make.data = &state_make;
    feat_state_free.URI = LV2_STATE__freePath;
    feat_state_free.data = &state_free;

    // features array
    features[0] = &feat_map;
    features[1] = &feat_unmap;
    features[2] = &feat_opts;
    features[3] = &feat_buf_fixed;
    features[4] = &feat_buf_bounded;
    features[5] = &feat_buf_pow2;
    features[6] = &feat_log;
    features[7] = &feat_worker;
    features[8] = &feat_state_map;
    features[9] = &feat_state_make;
    features[10] = &feat_state_free;
    features[11] = nullptr;
}

LilvHost::~LilvHost() {
    if (inst) {
        for (uint32_t i = 0; i < num_ports; ++i) {
            lilv_instance_connect_port(inst, i, nullptr);
        }
    }
    for (uint32_t idx : control_scalar_ports) {
        if (idx < port_buffers.size()) {
            delete port_buffers[idx];
            port_buffers[idx] = nullptr;
        }
    }
    for (uint32_t i = 0; i < cv_heap.size(); ++i) {
        if (cv_heap[i]) {
            delete[] cv_heap[i];
        }
    }
    auto freeNode = [&](LilvNode *&n) {
        if (n) {
            lilv_node_free(n);
            n = nullptr;
        }
    };
    freeNode(AUDIO);
    freeNode(CONTROL);
    freeNode(CV);
    freeNode(INPUT);
    freeNode(OUTPUT);
    freeNode(ATOM);
    freeNode(SEQUENCE);
    freeNode(BUFTYPE);
    freeNode(SUPPORTS);
    freeNode(MIDI_EVENT);
    if (inst) {
        lilv_instance_free(inst);
        inst = nullptr;
    }
    if (world) {
        lilv_world_free(world);
        world = nullptr;
    }
}

bool LilvHost::load_world() {
    world = lilv_world_new();
    if (!world) {
        return false;
    }
    lilv_world_load_all(world);
    plugins = lilv_world_get_all_plugins(world);
    AUDIO = lilv_new_uri(world, LILV_URI_AUDIO_PORT);
    CONTROL = lilv_new_uri(world, LILV_URI_CONTROL_PORT);
    CV = lilv_new_uri(world, LILV_URI_CV_PORT);
    INPUT = lilv_new_uri(world, LILV_URI_INPUT_PORT);
    OUTPUT = lilv_new_uri(world, LILV_URI_OUTPUT_PORT);
    ATOM = lilv_new_uri(world, LV2_ATOM__AtomPort);
    SEQUENCE = lilv_new_uri(world, LV2_ATOM__Sequence);
    BUFTYPE = lilv_new_uri(world, LV2_ATOM__bufferType);
    SUPPORTS = lilv_new_uri(world, LV2_ATOM__supports);
    MIDI_EVENT = lilv_new_uri(world, LV2_MIDI__MidiEvent);
    return true;
}

bool LilvHost::find_plugin(const std::string &plugin_uri) {
    if (!world || !plugins) {
        return false;
    }
    LilvNode *uri_node = lilv_new_uri(world, plugin_uri.c_str());
    plugin = nullptr;
    LILV_FOREACH(plugins, i, plugins) {
        const LilvPlugin *p = lilv_plugins_get(plugins, i);
        const LilvNode *node = lilv_plugin_get_uri(p);

        std::cout << "plugin name: " << lilv_node_as_string(node) << "\n";

        if (lilv_node_equals(node, uri_node)) {
            plugin = p;
            break;
        }
    }
    lilv_node_free(uri_node);
    if (!plugin) {
        return false;
    }
    num_ports = lilv_plugin_get_num_ports(plugin);
    return true;
}

bool LilvHost::instantiate() {
    if (!plugin) {
        return false;
    }
    //try {
        inst = lilv_plugin_instantiate(plugin, sr, get_features());
    //} catch (const std::exception &e) {
    //    std::cerr << "[host] Exception during instantiate: " << e.what() << "\n";
    //    return false;
    //} catch (...) {
    //    std::cerr << "[host] Unknown exception during instantiate\n";
    //    return false;
    //}
    if (!inst) {
        return false;
    }
    desc = lilv_instance_get_descriptor(inst);
    return true;
}

void LilvHost::wire_worker_interface() {
    if (!desc || !desc->extension_data) {
        return;
    }
    const LV2_Worker_Interface *wiface = (const LV2_Worker_Interface *)desc->extension_data(LV2_WORKER__interface);
    worker.iface = wiface;
    worker.handle = lilv_instance_get_handle(inst);
}

void LilvHost::set_cli_control_overrides(const std::vector<std::pair<std::string, float>> &nvp) {
    cli_sets = nvp;
}

void LilvHost::dump_plugin_features() const {
    if (!plugin) {
        return;
    }
    auto print_nodes = [&](const char *label, const LilvNodes *nodes) {
        std::cout << label << ":\n";
        if (!nodes || lilv_nodes_size(nodes) == 0) {
            std::cout << "  (none)\n";
            return;
        }
        LILV_FOREACH(nodes, it, nodes) {
            const LilvNode *n = lilv_nodes_get(nodes, it);
            std::cout << "  - " << cstr_or(lilv_node_as_string(n), "(null)") << "\n";
        }
    };
    print_nodes("requiredFeature", lilv_plugin_get_required_features(plugin));
    print_nodes("optionalFeature", lilv_plugin_get_optional_features(plugin));
}

void LilvHost::dump_host_features() const {
    std::cout << "host features passed:\n";
    for (int i = 0; features[i]; ++i) {
        std::cout << "  - " << cstr_or(features[i]->URI, "(null)") << (features[i]->data ? " (data:yes)" : " (data:no)")
                  << "\n";
    }
}

void LilvHost::dump_ports() const {
    if (!plugin) {
        return;
    }
    std::cout << "Ports:\n";
    for (uint32_t i = 0; i < num_ports; ++i) {
        const LilvPort *p = lilv_plugin_get_port_by_index(plugin, i);
        const LilvNode *sym = lilv_port_get_symbol(plugin, p);
        const char *s = sym ? lilv_node_as_string(sym) : nullptr;
        bool is_audio = lilv_port_is_a(plugin, p, AUDIO);
        bool is_control = lilv_port_is_a(plugin, p, CONTROL);
        bool is_cv = lilv_port_is_a(plugin, p, CV);
        bool is_input = lilv_port_is_a(plugin, p, INPUT);
        bool is_output = lilv_port_is_a(plugin, p, OUTPUT);
        std::cout << "  [" << i << "] " << cstr_or(s, "(no_symbol)") << "  " << (is_audio ? "audio " : "")
                  << (is_cv ? "cv " : "") << (is_control ? "control " : "") << (is_input ? "in " : "")
                  << (is_output ? "out " : "") << "\n";
    }
}

std::string LilvHost::port_symbol(const LilvPort *cport) const {
    const LilvNode *sym = lilv_port_get_symbol(plugin, cport);
    const char *s = sym ? lilv_node_as_string(sym) : nullptr;
    return to_string_safe(s);
}

bool LilvHost::prepare_ports_and_buffers() {
    if (!plugin || !inst) {
        return false;
    }

    const uint32_t min_header = (uint32_t)(sizeof(LV2_Atom) + sizeof(LV2_Atom_Sequence_Body));
    if (seq_capacity_hint < min_header) {
        seq_capacity_hint = min_header + 256;
    }

    port_buffers.assign(num_ports, nullptr);
    control_scalar_ports.clear();
    atom_inputs.clear();
    atom_outputs.clear();
    num_audio_in = num_audio_out = 0;

    // Map CLI --set to indices
    std::vector<CtrlSet> ctrl_sets;
    for (uint32_t i = 0; i < num_ports; ++i) {
        const LilvPort *cport = lilv_plugin_get_port_by_index(plugin, i);
        if (lilv_port_is_a(plugin, cport, CONTROL)) {
            std::string sym = port_symbol(cport);
            for (auto &kv : cli_sets) {
                if (kv.first == sym) {
                    ctrl_sets.push_back({i, kv.second});
                }
            }
        }
    }

    // pass 1: classify + allocate scalar controls + prepare atom port descriptors
    for (uint32_t i = 0; i < num_ports; ++i) {
        const LilvPort *cport = lilv_plugin_get_port_by_index(plugin, i);
        const bool is_audio = lilv_port_is_a(plugin, cport, AUDIO);
        const bool is_control = lilv_port_is_a(plugin, cport, CONTROL);
        const bool is_cv = lilv_port_is_a(plugin, cport, CV);
        const bool is_input = lilv_port_is_a(plugin, cport, INPUT);
        const bool is_output = lilv_port_is_a(plugin, cport, OUTPUT);

        if (is_audio && is_input) {
            ++num_audio_in;
        }
        if (is_audio && is_output) {
            ++num_audio_out;
        }

        if (is_control) {
            LilvNode *d = nullptr, *mn = nullptr, *mx = nullptr;
            lilv_port_get_range(plugin, cport, &d, &mn, &mx);
            float def = 0.f;
            if (d) {
                if (lilv_node_is_float(d)) {
                    def = (float)lilv_node_as_float(d);
                } else if (lilv_node_is_int(d)) {
                    def = (float)lilv_node_as_int(d);
                }
            }
            for (const auto &cs : ctrl_sets) {
                if (cs.index == i) {
                    def = cs.value;
                    break;
                }
            }
            port_buffers[i] = new float(def);
            lilv_instance_connect_port(inst, i, port_buffers[i]);
            control_scalar_ports.push_back(i);
            if (d) {
                lilv_node_free(d);
            }
            if (mn) {
                lilv_node_free(mn);
            }
            if (mx) {
                lilv_node_free(mx);
            }
        }

        // Atom inputs
        if (lilv_port_is_a(plugin, cport, ATOM) && is_input) {
            bool is_sequence = false, supports_midi = false;
            LilvNodes *buftypes = lilv_port_get_value(plugin, cport, BUFTYPE);
            LILV_FOREACH(nodes, it, buftypes) {
                const LilvNode *n = lilv_nodes_get(buftypes, it);
                if (lilv_node_equals(n, SEQUENCE)) {
                    is_sequence = true;
                }
            }
            lilv_nodes_free(buftypes);

            LilvNodes *supp = lilv_port_get_value(plugin, cport, SUPPORTS);
            LILV_FOREACH(nodes, it2, supp) {
                const LilvNode *n = lilv_nodes_get(supp, it2);
                if (lilv_node_equals(n, MIDI_EVENT)) {
                    supports_midi = true;
                }
            }
            lilv_nodes_free(supp);

            if (is_sequence) {
                AtomIn a{};
                a.index = i;
                a.midi = supports_midi;

#if LILVHOST_DBG
                const size_t words = ([](uint32_t bytes) {
                    const size_t W = sizeof(std::max_align_t);
                    const size_t bytes_with_guard = (size_t)bytes + 16u * sizeof(uint32_t);
                    return (bytes_with_guard + W - 1) / W;
                })(seq_capacity_hint);
                a.buf.assign(words, {});
                // init guard
                {
                    uint8_t *base = reinterpret_cast<uint8_t *>(a.buf.data());
                    const size_t total_bytes = a.buf.size() * sizeof(std::max_align_t);
                    if (total_bytes >= 16u * sizeof(uint32_t)) {
                        uint32_t *guard = reinterpret_cast<uint32_t *>(base + total_bytes - 16u * sizeof(uint32_t));
                        for (uint32_t i2 = 0; i2 < 16u; ++i2) {
                            guard[i2] = 0xA5A5A5A5u;
                        }
                    }
                }
                const uint32_t forge_bytes =
                    (uint32_t)(a.buf.size() * sizeof(std::max_align_t) - 16u * sizeof(uint32_t));
#else
                const size_t W = sizeof(std::max_align_t);
                const size_t words = (seq_capacity_hint + W - 1) / W;
                a.buf.assign(words, {});
                const uint32_t forge_bytes = (uint32_t)(a.buf.size() * sizeof(std::max_align_t));
#endif
                a.seq = reinterpret_cast<LV2_Atom_Sequence *>(a.buf.data());
                lv2_atom_forge_init(&a.forge, urid_map());
                // Save per-input forge size if you prefer; here we recompute per block.
                atom_inputs.push_back(std::move(a));
            }
        }

        // Atom outputs
        if (lilv_port_is_a(plugin, cport, ATOM) && is_output) {
            bool is_sequence = false;
            LilvNodes *buftypes = lilv_port_get_value(plugin, cport, BUFTYPE);
            LILV_FOREACH(nodes, it, buftypes) {
                const LilvNode *n = lilv_nodes_get(buftypes, it);
                if (lilv_node_equals(n, SEQUENCE)) {
                    is_sequence = true;
                }
            }
            lilv_nodes_free(buftypes);

            if (is_sequence) {
                AtomOut o{};
                o.index = i;

#if LILVHOST_DBG
                const size_t words = ([](uint32_t bytes) {
                    const size_t W = sizeof(std::max_align_t);
                    const size_t bytes_with_guard = (size_t)bytes + 16u * sizeof(uint32_t);
                    return (bytes_with_guard + W - 1) / W;
                })(seq_capacity_hint);
                o.buf.assign(words, {});
                // init guard
                {
                    uint8_t *base = reinterpret_cast<uint8_t *>(o.buf.data());
                    const size_t total_bytes = o.buf.size() * sizeof(std::max_align_t);
                    if (total_bytes >= 16u * sizeof(uint32_t)) {
                        uint32_t *guard = reinterpret_cast<uint32_t *>(base + total_bytes - 16u * sizeof(uint32_t));
                        for (uint32_t i2 = 0; i2 < 16u; ++i2) {
                            guard[i2] = 0xA5A5A5A5u;
                        }
                    }
                }
                const uint32_t buf_bytes = (uint32_t)(o.buf.size() * sizeof(std::max_align_t) - 16u * sizeof(uint32_t));
#else
                const size_t W = sizeof(std::max_align_t);
                const size_t words = (seq_capacity_hint + W - 1) / W;
                o.buf.assign(words, {});
                const uint32_t buf_bytes = (uint32_t)(o.buf.size() * sizeof(std::max_align_t));
#endif
                o.seq = reinterpret_cast<LV2_Atom_Sequence *>(o.buf.data());

                // Capacity semantics: atom.size is body capacity (bytes), not just used bytes
                const uint32_t body_capacity =
                    (buf_bytes > sizeof(LV2_Atom)) ? (buf_bytes - (uint32_t)sizeof(LV2_Atom)) : 0u;
                o.seq->atom.type = urids.atom_Sequence;
                o.seq->atom.size = body_capacity; // capacity!
                auto *body = reinterpret_cast<LV2_Atom_Sequence_Body *>(LV2_ATOM_BODY(&o.seq->atom));
                body->unit = urids.atom_FrameTime;
                body->pad = 0;

                atom_outputs.push_back(std::move(o));
            }
        }
    }

    // audio buffers
    channels = std::max(num_audio_out, num_audio_in);
    if (channels == 0) {
        channels = 1;
    }
    audio.assign(channels, std::vector<float>(block, 0.f));
#if LILVHOST_DBG
    // add tail guards (debug only)
    for (auto &v : audio) {
        const uint32_t kTailGuardSamples = 64;
        size_t old = v.size();
        v.resize(old + kTailGuardSamples, 0.0f);
        float *g = v.data() + old;
        for (uint32_t i = 0; i < kTailGuardSamples; ++i) {
            uint32_t *p = reinterpret_cast<uint32_t *>(g + i);
            *p = 0x7FC00000u; // NaN-ish marker
        }
    }
#endif
    audio_ptrs.assign(channels, nullptr);
    for (uint32_t c = 0; c < channels; ++c) {
        audio_ptrs[c] = audio[c].data();
    }


    in_ch_effective = (num_audio_out ? num_audio_out : 1u);
    audio_in_ptrs.assign(in_ch_effective, nullptr);

    out_ch_effective = (num_audio_out ? num_audio_out : 1u);
    audio_out_ptrs.assign(out_ch_effective, nullptr);

    // CV heap
    cv_heap.assign(num_ports, nullptr);

    // pass 2: connect
    uint32_t in_idx = 0, out_idx = 0;
    for (uint32_t i = 0; i < num_ports; ++i) {
        const LilvPort *cport = lilv_plugin_get_port_by_index(plugin, i);
        const bool is_audio = lilv_port_is_a(plugin, cport, AUDIO);
        const bool is_cv = lilv_port_is_a(plugin, cport, CV);
        const bool is_input = lilv_port_is_a(plugin, cport, INPUT);
        const bool is_output = lilv_port_is_a(plugin, cport, OUTPUT);

        if (is_audio && is_input) {
            float *buf = (channels ? audio_ptrs[std::min(in_idx, channels - 1)] : nullptr);
            port_buffers[i] = buf;
            lilv_instance_connect_port(inst, i, buf);
            if (in_idx < audio_in_ptrs.size()) {
                audio_in_ptrs[in_idx] = buf;
            }
            ++in_idx;
            continue;
        }
        if (is_audio && is_output) {
            float *buf = (channels ? audio_ptrs[std::min(out_idx, channels - 1)] : nullptr);
            port_buffers[i] = buf;
            lilv_instance_connect_port(inst, i, buf);
            if (out_idx < audio_out_ptrs.size()) {
                audio_out_ptrs[out_idx] = buf;
            }
            ++out_idx;
            continue;
        }
        if (is_cv) {
#if LILVHOST_DBG
            // allocate block + guard
            const uint32_t kTailGuardSamples = 64;
            float *buf = new float[block + kTailGuardSamples]();
            // place guard pattern at tail
            for (uint32_t g = 0; g < kTailGuardSamples; ++g) {
                uint32_t *p = reinterpret_cast<uint32_t *>(buf + block + g);
                *p = 0x7FC00000u;
            }
#else
            float *buf = new float[block](); // zero-initialize
#endif
            cv_heap[i] = buf;
            port_buffers[i] = buf;
            lilv_instance_connect_port(inst, i, buf);
            continue;
        }

        for (auto &a : atom_inputs) {
            if (a.index == i) {
                lilv_instance_connect_port(inst, i, a.seq);
            }
        }
        for (auto &o : atom_outputs) {
            if (o.index == i) {
                lilv_instance_connect_port(inst, i, o.seq);
            }
        }
    }

    return true;
}

void LilvHost::activate() {
    if (inst) {
        lilv_instance_activate(inst);
    }
}
void LilvHost::deactivate() {
    if (inst) {
        lilv_instance_deactivate(inst);
    }
}

bool LilvHost::run_offline(double duration_sec, double freq_hz, float gain, bool midi_enabled, int midi_note,
                           const std::string &out_path) {
    if (!inst) {
        return false;
    }

    WavWriter writer;
    if (!writer.open(out_path.c_str(), (int)sr, (int)out_ch_effective)) {
        std::cerr << "Failed to open output: " << out_path << "\n";
        return false;
    }

    std::vector<float> zero_buf(block, 0.f);
    std::vector<const float *> outs(out_ch_effective, zero_buf.data());

    const size_t total_frames = (size_t)(duration_sec * sr);
    size_t done = 0;
    double phase = 0.0;
    const double twopi = 6.283185307179586;
    size_t elapsed_frames = 0;

    while (done < total_frames) {
        const uint32_t n = (uint32_t)std::min((size_t)block, total_frames - done);
        const uint32_t frames_to_write = n;

        rt_deliver_worker_responses();

        if (num_audio_in > 0) {
            for (uint32_t i = 0; i < n; ++i) {
                float s = gain * (float)std::sin(phase);
                phase += twopi * freq_hz / sr;
                for (uint32_t c = 0; c < in_ch_effective; ++c) {
                    audio_in_ptrs[c][i] = s;
                    //audio_in_ptrs[c][i] = 0;
                }
            }
            if (n < block) {
                for (uint32_t c = 0; c < channels; ++c) {
                    std::memset(audio[c].data() + n, 0, (block - n) * sizeof(float));
                }
            }
        }

        // forge atom input headers (+ MIDI if applicable)
        for (auto &a : atom_inputs) {
#if LILVHOST_DBG
            const uint32_t forge_bytes = (uint32_t)(a.buf.size() * sizeof(std::max_align_t) - 16u * sizeof(uint32_t));
#else
            const uint32_t forge_bytes = (uint32_t)(a.buf.size() * sizeof(std::max_align_t));
#endif
            lv2_atom_forge_set_buffer(&a.forge, reinterpret_cast<uint8_t *>(a.buf.data()), forge_bytes);

            LV2_Atom_Forge_Frame seq_frame;
            lv2_atom_forge_sequence_head(&a.forge, &seq_frame, urids.atom_FrameTime);

            if (midi_enabled && a.midi) {
                bool send_on = (elapsed_frames == 0);
                bool send_off = (elapsed_frames < (size_t)sr) && (elapsed_frames + n >= (size_t)sr);

                auto write_midi = [&](uint32_t frame_off, uint8_t st, uint8_t d1, uint8_t d2) {
                    uint8_t msg[3] = {st, d1, d2};
                    lv2_atom_forge_frame_time(&a.forge, frame_off);
                    lv2_atom_forge_atom(&a.forge, 3, urids.midi_MidiEvent);
                    lv2_atom_forge_write(&a.forge, msg, 3);
                };

                uint8_t ch = 0;
                uint8_t note_on = 0x90 | (ch & 0x0F);
                uint8_t note_off = 0x80 | (ch & 0x0F);

                if (send_on) {
                    write_midi(0, note_on, (uint8_t)midi_note, 100);
                    write_midi(0, note_on, (uint8_t)(midi_note + 4), 100);
                    write_midi(0, note_on, (uint8_t)(midi_note + 7), 100);
                }
                if (send_off) {
                    uint32_t t = (uint32_t)((size_t)sr - elapsed_frames);
                    if (t >= n) {
                        t = n - 1;
                    }
                    write_midi(t, note_off, (uint8_t)midi_note, 0);
                    write_midi(t, note_off, (uint8_t)(midi_note + 4), 0);
                    write_midi(t, note_off, (uint8_t)(midi_note + 7), 0);
                }
            }

            // keep typed pointer in sync (same buffer)
            a.seq = reinterpret_cast<LV2_Atom_Sequence *>(a.buf.data());
        }

        // Reset atom outputs each block with full capacity (body bytes)
        for (auto &o : atom_outputs) {
#if LILVHOST_DBG
            const uint32_t buf_bytes = (uint32_t)(o.buf.size() * sizeof(std::max_align_t) - 16u * sizeof(uint32_t));
#else
            const uint32_t buf_bytes = (uint32_t)(o.buf.size() * sizeof(std::max_align_t));
#endif
            const uint32_t body_capacity =
                (buf_bytes > sizeof(LV2_Atom)) ? (buf_bytes - (uint32_t)sizeof(LV2_Atom)) : 0u;

            o.seq->atom.type = urids.atom_Sequence;
            o.seq->atom.size = body_capacity; // capacity!
            auto *body = reinterpret_cast<LV2_Atom_Sequence_Body *>(LV2_ATOM_BODY(&o.seq->atom));
            body->unit = urids.atom_FrameTime;
            body->pad = 0;
        }

        // run
        //try {
            lilv_instance_run(inst, block);
        //} catch (const std::exception &e) {
        //    std::cerr << "[host] Exception during run: " << e.what() << "\n";
        //    writer.close();
        //    return false;
        //} catch (...) {
        //    std::cerr << "[host] Unknown exception during run\n";
        //    writer.close();
        //    return false;
        //}

#if LILVHOST_DBG
        // Debug guards: Atom outputs
        for (auto &o : atom_outputs) {
            // re-check end-of-buffer guard
            const uint8_t *base = reinterpret_cast<const uint8_t *>(o.buf.data());
            const size_t total_bytes = o.buf.size() * sizeof(std::max_align_t);
            bool ok = true;
            if (total_bytes >= 16u * sizeof(uint32_t)) {
                const uint32_t *guard = reinterpret_cast<const uint32_t *>(base + total_bytes - 16u * sizeof(uint32_t));
                for (uint32_t i2 = 0; i2 < 16u; ++i2) {
                    if (guard[i2] != 0xA5A5A5A5u) {
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok) {
                std::cerr << "[AtomGuard] OVERFLOW on atom out port " << o.index << "\n";
                writer.close();
                return false;
            }
        }
        // Debug guards: audio tails
        for (uint32_t c = 0; c < channels; ++c) {
            const uint32_t kTailGuardSamples = 64;
            const float *g = audio[c].data() + block;
            bool ok = true;
            for (uint32_t i = 0; i < kTailGuardSamples; ++i) {
                const uint32_t *p = reinterpret_cast<const uint32_t *>(g + i);
                if (*p != 0x7FC00000u) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                std::cerr << "[AudioGuard] OVERFLOW on audio buffer ch=" << c << " (plugin wrote past " << block
                          << " samples)\n";
                writer.close();
                return false;
            }
        }
        // Debug guards: CV tails
        for (uint32_t i = 0; i < num_ports; ++i) {
            const LilvPort *cport = lilv_plugin_get_port_by_index(plugin, i);
            if (!lilv_port_is_a(plugin, cport, CV)) {
                continue;
            }
            float *buf = cv_heap[i];
            if (!buf) {
                continue;
            }
            const uint32_t kTailGuardSamples = 64;
            bool ok = true;
            for (uint32_t g = 0; g < kTailGuardSamples; ++g) {
                const uint32_t *p = reinterpret_cast<const uint32_t *>(buf + block + g);
                if (*p != 0x7FC00000u) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                std::cerr << "[CVGuard] OVERFLOW on CV port " << i << " (plugin wrote past " << block << " samples)\n";
                writer.close();
                return false;
            }
        }
#endif

        // (optional) dump atom outputs
        /*
        for (auto& o : atom_outputs_) {
            auto* seq = o.seq;
            if (seq->atom.size <= sizeof(LV2_Atom_Sequence_Body)) continue;
            std::cout << "[events_out port " << o.index << "] size=" << seq->atom.size << "\n";
            LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
                std::cout << "  ev t=" << ev->time.frames << " type=" << ev->body.type << " size=" << ev->body.size;
                if (ev->body.type == urids_.midi_MidiEvent && ev->body.size >= 1) {
                    const uint8_t* b = (const uint8_t*)LV2_ATOM_BODY(&ev->body);
                    std::cout << "  MIDI:";
                    for (uint32_t i = 0; i < ev->body.size; ++i) std::cout << " " << std::hex << (int)b[i] << std::dec;
                }
                std::cout << "\n";
            }
        }
        */

        // write audio to wav
        for (uint32_t c = 0; c < out_ch_effective; ++c) {
            if (c < audio_out_ptrs.size() && audio_out_ptrs[c]) {
                outs[c] = audio_out_ptrs[c];
            } else if (c < channels && !audio.empty() && !audio[c].empty()) {
                outs[c] = audio[c].data();
            } else {
                outs[c] = zero_buf.data();
            }
        }
        if (frames_to_write > 0) {
            writer.write_interleaved((const float *const *)outs.data(), frames_to_write, (int)out_ch_effective);
        }

        non_rt_do_worker_requests();

        done += n;
        elapsed_frames += n;
    }

    return true;
}

// worker plumbing
void LilvHost::rt_deliver_worker_responses() {
    if (!worker.iface || !worker.handle) {
        return;
    }
    for (auto &r : worker.responses) {
        worker.iface->work_response(worker.handle, (uint32_t)r.size(), r.data());
    }
    worker.responses.clear();
    if (worker.iface->end_run) {
        worker.iface->end_run(worker.handle);
    }
}
void LilvHost::non_rt_do_worker_requests() {
    if (!worker.iface || !worker.handle) {
        return;
    }
    for (auto &q : worker.requests) {
        worker.iface->work(worker.handle, &LilvHost::s_worker_respond, &worker, (uint32_t)q.size(), q.data());
    }
    worker.requests.clear();
}
LV2_Worker_Status LilvHost::s_schedule_work(LV2_Worker_Schedule_Handle h, uint32_t size, const void *data) {
    auto *ws = static_cast<WorkerState *>(h);
    const uint8_t *b = static_cast<const uint8_t *>(data);
    ws->requests.emplace_back(b, b + size);
    return LV2_WORKER_SUCCESS;
}
LV2_Worker_Status LilvHost::s_worker_respond(LV2_Worker_Respond_Handle h, uint32_t size, const void *data) {
    auto *ws = static_cast<WorkerState *>(h);
    const uint8_t *b = static_cast<const uint8_t *>(data);
    ws->responses.emplace_back(b, b + size);
    return LV2_WORKER_SUCCESS;
}

// URID map/unmap
LV2_URID LilvHost::s_map_cb(LV2_URID_Map_Handle h, const char *uri) {
    return static_cast<LilvHost *>(h)->map_uri(uri);
}
const char *LilvHost::s_unmap_cb(LV2_URID_Unmap_Handle h, LV2_URID urid) {
    auto *self = static_cast<LilvHost *>(h);
    auto it = self->rev.find(urid);
    return (it != self->rev.end()) ? it->second.c_str() : "";
}
LV2_URID LilvHost::map_uri(const char *uri) {
    if (!uri) {
        return 0;
    }
    auto it = dict.find(uri);
    if (it != dict.end()) {
        return it->second;
    }
    LV2_URID id = next_urid++;
    const std::string key = to_string_safe(uri);
    dict.emplace(key, id);
    rev.emplace(id, key);
    return id;
}
void LilvHost::premap_common_uris() {
    urids.atom_Int = map_uri(LV2_ATOM__Int);
    urids.atom_Float = map_uri(LV2_ATOM__Float);
    urids.param_sr = map_uri(LV2_PARAMETERS__sampleRate);
    urids.buf_nom = map_uri(LV2_BUF_SIZE__nominalBlockLength);
    urids.buf_min = map_uri(LV2_BUF_SIZE__minBlockLength);
    urids.buf_max = map_uri(LV2_BUF_SIZE__maxBlockLength);
    urids.buf_seq = map_uri(LV2_BUF_SIZE__sequenceSize);
    urids.atom_Sequence = map_uri(LV2_ATOM__Sequence);
    urids.atom_FrameTime = map_uri(LV2_ATOM__frameTime);
    urids.midi_MidiEvent = map_uri(LV2_MIDI__MidiEvent);
    urids.log_Error = map_uri(LV2_LOG__Error);
    urids.log_Warning = map_uri(LV2_LOG__Warning);
    urids.log_Note = map_uri(LV2_LOG__Note);
#ifdef LV2_LOG__Trace
    urids.log_Trace = map_uri(LV2_LOG__Trace);
#endif
}
void LilvHost::rebuild_options() {
    sr_f = (float)sr;
    nom = min = max = block;
    seq = seq_bytes;
    opts[0] = {LV2_OPTIONS_INSTANCE, 0, urids.param_sr, sizeof(sr_f), urids.atom_Float, &sr_f};
    opts[1] = {LV2_OPTIONS_INSTANCE, 0, urids.buf_nom, sizeof(nom), urids.atom_Int, &nom};
    opts[2] = {LV2_OPTIONS_INSTANCE, 0, urids.buf_min, sizeof(min), urids.atom_Int, &min};
    opts[3] = {LV2_OPTIONS_INSTANCE, 0, urids.buf_max, sizeof(max), urids.atom_Int, &max};
    opts[4] = {LV2_OPTIONS_INSTANCE, 0, urids.buf_seq, sizeof(seq), urids.atom_Int, &seq};
    std::memset(&opts[5], 0, sizeof(opts[5]));
    feat_opts.URI = LV2_OPTIONS__options;
    feat_opts.data = opts;
}

// lv2:log
int LilvHost::s_log_printf(LV2_Log_Handle h, LV2_URID type, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = s_log_vprintf(h, type, fmt, ap);
    va_end(ap);
    return r;
}
int LilvHost::s_log_vprintf(LV2_Log_Handle h, LV2_URID type, const char *fmt, va_list ap) {
    auto *self = static_cast<LilvHost *>(h);
    const char *tag = "LOG";
    FILE *out = stdout;
    if (type == self->urids.log_Error) {
        tag = "ERROR";
        out = stderr;
    } else if (type == self->urids.log_Warning) {
        tag = "WARN";
        out = stderr;
    } else if (type == self->urids.log_Trace) {
        tag = "TRACE";
        out = stdout;
    } else if (type == self->urids.log_Note) {
        tag = "NOTE";
        out = stdout;
    }
    std::fputs("[lv2:", out);
    std::fputs(tag, out);
    std::fputs("] ", out);
    std::vfprintf(out, fmt, ap);
    std::fputc('\n', out);
    return 0;
}

// lv2:state helpers
char *LilvHost::s_state_abs_path(LV2_State_Map_Path_Handle, const char *p) {
    if (!p || !*p) {
        return strdup("");
    }
    if (p[0] == '/' || (std::strlen(p) > 1 && p[1] == ':')) {
        return strdup(p);
    }
    std::string out = std::string("/tmp/") + p;
    return strdup(out.c_str());
}
char *LilvHost::s_state_abspath_to_abstract(LV2_State_Map_Path_Handle, const char *abs) {
    return strdup((!abs || !*abs) ? "" : abs);
}
char *LilvHost::s_state_make_path(LV2_State_Make_Path_Handle, const char *leaf) {
    std::string out = std::string("/tmp/") + (leaf ? leaf : "");
    return strdup(out.c_str());
}
void LilvHost::s_state_free_path(LV2_State_Free_Path_Handle, char *p) {
    if (p) {
        std::free(p);
    }
}
