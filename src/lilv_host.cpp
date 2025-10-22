#include "lilv_host.h"
#include "lilv/lilv.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
//#include <exception>
#include <iostream>
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
LilvHost::LilvHost(LilvWorld *p_world, double sr, int p_frames, uint32_t seq_bytes)
    : sr(sr), seq_bytes(seq_bytes) {

    world = p_world;

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
    rebuild_options(p_frames);

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
    /*
    if (world) {
        lilv_world_free(world);
        world = nullptr;
    }
    */
}

bool LilvHost::load_world() {
    //world = lilv_world_new();
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

        //TODO: turn on/off logging
        if (false) {
            std::cout << "plugin name: " << lilv_node_as_string(node) << "\n";
        }

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

    inst = lilv_plugin_instantiate(plugin, sr, features);

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

bool LilvHost::prepare_ports_and_buffers(int p_frames) {
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
                lv2_atom_forge_init(&a.forge, &map);
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

    //create midi buffers
	midi_input_buffer.resize(atom_inputs.size());
	midi_output_buffer.resize(atom_outputs.size());

    // audio buffers
    channels = std::max(num_audio_out, num_audio_in);
    if (channels == 0) {
        channels = 1;
    }
    audio.assign(channels, std::vector<float>(p_frames, 0.f));
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

    audio_in_ptrs.assign(num_audio_in, nullptr);
    audio_out_ptrs.assign(num_audio_out, nullptr);

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
            float *buf = new float[p_frames](); // zero-initialize
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

    for (int i = 0; i < num_ports; i++) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
        bool is_control = lilv_port_is_a(plugin, port, CONTROL);
        bool is_input = lilv_port_is_a(plugin, port, INPUT);
        bool is_output = lilv_port_is_a(plugin, port, OUTPUT);


        if (is_control) {
            std::string sym = port_symbol(port);

            LilvNode *node_name = lilv_port_get_name(plugin, port);
            std::string name = lilv_node_as_string(node_name);
            lilv_node_free(node_name);

            //TODO: What should the default be when they are not specified?
            float def = 0;
            float min = 0;
            float max = 1;

            LilvNode *def_node;
            LilvNode *min_node;
            LilvNode *max_node;
            lilv_port_get_range(plugin, port, &def_node, &min_node, &max_node);

            if (def_node) {
                def = lilv_node_as_float(def_node);
                lilv_node_free(def_node);
            }

            if (min_node) {
                min = lilv_node_as_float(min_node);
                lilv_node_free(min_node);
            }

            if (max_node) {
                max = lilv_node_as_float(max_node);
                lilv_node_free(max_node);
            }

			LilvNode* units_unit_uri = lilv_new_uri(world, LV2_UNITS__unit);
			const LilvNodes* unit_values = lilv_port_get_value(plugin, port, units_unit_uri);
			const LilvNode* unit_value = nullptr;

			if (unit_values && lilv_nodes_size(unit_values) > 0) {
				LILV_FOREACH(nodes, it, unit_values) {
					unit_value = lilv_nodes_get(unit_values, it);
					break;
				}
			}

			std::string unit;

			if (unit_value && lilv_node_is_uri(unit_value)) {
				unit = lilv_node_as_uri(unit_value);
				if (unit.rfind(LV2_UNITS_PREFIX) == 0) {
					unit = unit.substr(strlen(LV2_UNITS_PREFIX));
				}
			}

			lilv_node_free(units_unit_uri);

			LilvNodes* props = lilv_port_get_properties(plugin, port);
			LilvNode* log_uri = lilv_new_uri(world, LV2_PORT_PROPS__logarithmic);
			bool is_logarithmic = lilv_nodes_contains(props, log_uri);

			lilv_node_free(log_uri);

			auto has_prop = [&](const char* uri){
				LilvNode* n = lilv_new_uri(world, uri);
				bool ok = props && lilv_nodes_contains(props, n);
				lilv_node_free(n);
				return ok;
			};

			bool is_integer = has_prop(LV2_CORE__integer);
			bool is_enum    = has_prop(LV2_CORE__enumeration);
			bool is_toggle  = has_prop(LV2_CORE__toggled);

			// Scale points (for enums)
			const LilvScalePoints* sps = lilv_port_get_scale_points(plugin, port);
			std::vector<std::pair<std::string, float>> choices;
			if (sps && lilv_scale_points_size(sps) > 0) {
				LILV_FOREACH(scale_points, it, sps) {
					const LilvScalePoint* sp = lilv_scale_points_get(sps, it);
					const LilvNode* lab = lilv_scale_point_get_label(sp);
					const LilvNode* val = lilv_scale_point_get_value(sp);
					choices.emplace_back(lilv_node_as_string(lab), lilv_node_as_float(val));
				}
			}
			lilv_nodes_free(props);

            //TODO: Also provide the control properties and additional attributes

            Control control;
            control.index = i;
            control.symbol = sym;
            control.name = name;
            control.unit = unit;
            control.def = def;
            control.min = min;
            control.max = max;
            control.logarithmic = is_logarithmic;
            control.integer = is_integer;
            control.enumeration = is_enum;
			control.toggle = is_toggle;
			control.choices = choices;

            if (is_input) {
                control_inputs.push_back(control);
            }

            if (is_output) {
                control_outputs.push_back(control);
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

int LilvHost::perform(int p_frames) {
    rt_deliver_worker_responses();

	for (int i = 0; i < atom_inputs.size(); i++) {
		AtomIn &atom_input = atom_inputs[i];
        const uint32_t bytes = (uint32_t)(atom_input.buf.size() * sizeof(std::max_align_t));
        lv2_atom_forge_set_buffer(&atom_input.forge, reinterpret_cast<uint8_t*>(atom_input.buf.data()), bytes);
        LV2_Atom_Forge_Frame seq_frame;
        lv2_atom_forge_sequence_head(&atom_input.forge, &seq_frame, urids.atom_FrameTime);

        MidiEvent midi_event{};

		while(read_midi_in(i, midi_event)) {
			lv2_atom_forge_frame_time(&atom_input.forge, midi_event.frame);
			lv2_atom_forge_atom(&atom_input.forge, midi_event.size, urids.midi_MidiEvent);
			lv2_atom_forge_write(&atom_input.forge, midi_event.data, midi_event.size);
		}

		lv2_atom_forge_pop(&atom_input.forge, &seq_frame);

        atom_input.seq = reinterpret_cast<LV2_Atom_Sequence*>(atom_input.buf.data());
    }

    // 3) Prepare Atom OUTPUTS: mark empty for this block
	for (auto& atom_output : atom_outputs) {
		const uint32_t buf_bytes = (uint32_t)(atom_output.buf.size() * sizeof(std::max_align_t));
		const uint32_t body_capacity = (buf_bytes > sizeof(LV2_Atom)) ? (buf_bytes - (uint32_t)sizeof(LV2_Atom)) : 0u;

		atom_output.seq->atom.type = urids.atom_Sequence;
		atom_output.seq->atom.size = body_capacity;

    	std::memset(LV2_ATOM_BODY(&atom_output.seq->atom), 0, body_capacity);

		auto* body = (LV2_Atom_Sequence_Body*)LV2_ATOM_BODY(&atom_output.seq->atom);
		body->unit = urids.atom_FrameTime;
		body->pad  = 0;
	}

    // 4) Run DSP for this block
    lilv_instance_run(inst, p_frames);

    //TODO: reading is not working right now... or is it?
    // 5) Collect OUTPUT events → ABSOLUTE time → out ring
	for (int i = 0; i < atom_outputs.size(); i++) {
		AtomOut &atom_output = atom_outputs[i];
        auto* seq = atom_output.seq;
		if (seq->atom.size <= sizeof(LV2_Atom_Sequence_Body)) {
			continue;
		}
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
			if (ev->body.type != urids.midi_MidiEvent) {
				continue;
			}

            const auto* body = (const uint8_t*)LV2_ATOM_BODY(&ev->body);
			if (!body || ev->body.size == 0) {
				continue;
			}
            MidiEvent midi_event{};
            midi_event.frame = ev->time.frames;
            midi_event.size  = std::min<uint32_t>(ev->body.size, 3u);
			for (uint32_t j = 0; j < midi_event.size; ++j) {
				midi_event.data[j] = body[j];
			}
			write_midi_out(i, midi_event);
        }
    }

    // 6) Non-RT worker requests
    non_rt_do_worker_requests();

	return p_frames;
}

int LilvHost::get_input_channel_count() {
    return num_audio_in;
}

int LilvHost::get_output_channel_count() {
    return num_audio_out;
}

int LilvHost::get_input_midi_count() {
    return atom_inputs.size();;
}

int LilvHost::get_output_midi_count() {
    return atom_outputs.size();
}

int LilvHost::get_input_control_count() {
    return control_inputs.size();;
}

int LilvHost::get_output_control_count() {
    return control_outputs.size();
}

float *LilvHost::get_input_channel_buffer(int p_channel) {
    return audio_in_ptrs[p_channel];
}

float *LilvHost::get_output_channel_buffer(int p_channel) {
    return audio_out_ptrs[p_channel];
}

void LilvHost::write_midi_in(int p_bus, const MidiEvent& p_midi_event) {
	int event[MidiEvent::DATA_SIZE];

	for (int i = 0; i < MidiEvent::DATA_SIZE; i++) {
		event[i] = p_midi_event.data[i];
	}

	midi_input_buffer[p_bus].write_channel(event, MidiEvent::DATA_SIZE);
}

bool LilvHost::read_midi_in(int p_bus, MidiEvent& p_midi_event) {
	int event[MidiEvent::DATA_SIZE];
	int read = midi_input_buffer[p_bus].read_channel(event, MidiEvent::DATA_SIZE);

	if (read == MidiEvent::DATA_SIZE) {
		for (int i = 0; i < MidiEvent::DATA_SIZE; i++) {
			p_midi_event.data[i] = event[i];
		}
        midi_input_buffer[p_bus].update_read_index(MidiEvent::DATA_SIZE);
	}

	return read > 0;
}

void LilvHost::write_midi_out(int p_bus, const MidiEvent& p_midi_event) {
	int event[MidiEvent::DATA_SIZE];

	for (int i = 0; i < MidiEvent::DATA_SIZE; i++) {
		event[i] = p_midi_event.data[i];
	}

	midi_output_buffer[p_bus].write_channel(event, MidiEvent::DATA_SIZE);
}

bool LilvHost::read_midi_out(int p_bus, MidiEvent& p_midi_event) {
	int event[MidiEvent::DATA_SIZE];
	int read = midi_output_buffer[p_bus].read_channel(event, MidiEvent::DATA_SIZE);

	if (read == MidiEvent::DATA_SIZE) {
		for (int i = 0; i < MidiEvent::DATA_SIZE; i++) {
			p_midi_event.data[i] = event[i];
		}
	}

    midi_output_buffer[p_bus].update_read_index(MidiEvent::DATA_SIZE);

	return read > 0;
}

const Control *LilvHost::get_input_control(int p_index) {
    if (p_index < control_inputs.size()) {
        return &control_inputs[p_index];
    } else {
        return NULL;
    }
}

const Control *LilvHost::get_output_control(int p_index) {
    if (p_index < control_outputs.size()) {
        return &control_outputs[p_index];
    } else {
        return NULL;
    }
}

float LilvHost::get_input_control_value(int p_index) {
    if (p_index < control_inputs.size()) {
        return *port_buffers[control_inputs[p_index].index];
    } else {
        return 0;
    }
}

float LilvHost::get_output_control_value(int p_index) {
    if (p_index < control_outputs.size()) {
        return *port_buffers[control_outputs[p_index].index];
    } else {
        return 0;
    }
}

void LilvHost::set_input_control_value(int p_index, float p_value) {
    if (p_index < control_inputs.size()) {
        *port_buffers[control_inputs[p_index].index] = p_value;
    }
}

void LilvHost::set_output_control_value(int p_index, float p_value) {
    if (p_index < control_outputs.size()) {
        *port_buffers[control_outputs[p_index].index] = p_value;
    }
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
void LilvHost::rebuild_options(int p_frames) {
    sr_f = (float)sr;
    nom = min = max = p_frames;
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
