#ifndef LILV_HOST_H
#define LILV_HOST_H

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <lilv/lilv.h>

#include <cstdarg>
#include <cstddef> // std::max_align_t
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace godot {

struct URIDs {
    LV2_URID atom_Int{}, atom_Float{};
    LV2_URID param_sr{};
    LV2_URID buf_nom{}, buf_min{}, buf_max{}, buf_seq{};
    LV2_URID atom_Sequence{}, atom_FrameTime{}, midi_MidiEvent{};
    LV2_URID log_Error{}, log_Warning{}, log_Note{}, log_Trace{};
};

struct CtrlSet {
    uint32_t index{};
    float value{};
};

struct AtomIn {
    uint32_t index{};
    bool midi{};
    std::vector<std::max_align_t> buf; // aligned storage
    LV2_Atom_Sequence *seq{nullptr};
    LV2_Atom_Forge forge{};
};

struct AtomOut {
    uint32_t index{};
    std::vector<std::max_align_t> buf; // aligned storage
    LV2_Atom_Sequence *seq{nullptr};
};

class LilvHost {
private:
    // lv2:state helpers
    static char *s_state_abs_path(LV2_State_Map_Path_Handle, const char *p);
    static char *s_state_abspath_to_abstract(LV2_State_Map_Path_Handle, const char *abs);
    static char *s_state_make_path(LV2_State_Make_Path_Handle, const char *leaf);
    static void s_state_free_path(LV2_State_Free_Path_Handle, char *p);

    // helpers
    std::string port_symbol(const LilvPort *cport) const;

    // config
    double sr{};
    uint32_t block{};
    uint32_t seq_bytes{};

    // LILV objects
    LilvWorld *world{nullptr};
    const LilvPlugins *plugins{nullptr};
    const LilvPlugin *plugin{nullptr};
    LilvInstance *inst{nullptr};
    const LV2_Descriptor *desc{nullptr};

    // nodes
    LilvNode *AUDIO{}, *CONTROL{}, *CV{}, *INPUT{}, *OUTPUT{};
    LilvNode *ATOM{}, *SEQUENCE{}, *BUFTYPE{}, *SUPPORTS{}, *MIDI_EVENT{};

    uint32_t num_ports{};
    uint32_t num_audio_in{};
    uint32_t num_audio_out{};

    // URID map + storage
    LV2_URID_Map map{};
    LV2_Feature feat_map{};
    LV2_URID next_urid{1};
    std::unordered_map<std::string, LV2_URID> dict;
    std::unordered_map<LV2_URID, std::string> rev;

    // URID unmap
    LV2_URID_Unmap unmap{};
    LV2_Feature feat_unmap{};

    // URIDs
    URIDs urids{};

    // Options
    float sr_f{};
    uint32_t nom{}, min{}, max{}, seq{};
    LV2_Options_Option opts[6]{};
    LV2_Feature feat_opts{};

    // Optional features
    LV2_Feature feat_buf_fixed{};
    LV2_Feature feat_buf_bounded{};
    LV2_Feature feat_buf_pow2{};
    LV2_Log_Log log{};
    LV2_Feature feat_log{};
    LV2_Feature feat_worker{};

    LV2_State_Map_Path state_map{};
    LV2_State_Make_Path state_make{};
    LV2_State_Free_Path state_free{};
    LV2_Feature feat_state_map{};
    LV2_Feature feat_state_make{};
    LV2_Feature feat_state_free{};

    const LV2_Feature *features[12]{};

    // Ports / buffers
    std::vector<float *> port_buffers;
    std::vector<uint32_t> control_scalar_ports;
    std::vector<float *> cv_heap;

    std::vector<std::vector<float>> audio;
    std::vector<float *> audio_ptrs;
    std::vector<float *> audio_out_ptrs;
    uint32_t channels{};
    uint32_t out_ch_effective{};

    // Atom ports
    std::vector<AtomIn> atom_inputs;
    std::vector<AtomOut> atom_outputs;
    uint32_t seq_capacity_hint{}; // BYTES

    // CLI overrides
    std::vector<std::pair<std::string, float>> cli_sets;

    // URID map/unmap
    static LV2_URID s_map_cb(LV2_URID_Map_Handle, const char *uri);
    static const char *s_unmap_cb(LV2_URID_Unmap_Handle, LV2_URID urid);
    LV2_URID map_uri(const char *uri);
    void premap_common_uris();
    void rebuild_options();

    // lv2:log
    static int s_log_printf(LV2_Log_Handle, LV2_URID type, const char *fmt, ...);
    static int s_log_vprintf(LV2_Log_Handle, LV2_URID type, const char *fmt, va_list ap);

    // lv2:worker proxy
    struct WorkerState {
        const LV2_Worker_Interface *iface = nullptr;
        LV2_Handle handle = nullptr;
        LV2_Worker_Schedule sched{};
        std::vector<std::vector<uint8_t>> requests;
        std::vector<std::vector<uint8_t>> responses;
    } worker{};

    static LV2_Worker_Status s_schedule_work(LV2_Worker_Schedule_Handle, uint32_t size, const void *data);
    static LV2_Worker_Status s_worker_respond(LV2_Worker_Respond_Handle, uint32_t size, const void *data);

public:
    LilvHost(double sr, uint32_t block_frames, uint32_t seq_bytes = 4096);
    ~LilvHost();

    LilvHost(const LilvHost &) = delete;
    LilvHost &operator=(const LilvHost &) = delete;

    bool load_world();
    bool find_plugin(const std::string &plugin_uri);
    bool instantiate();
    void setCliControlOverrides(const std::vector<std::pair<std::string, float>> &name_value_pairs);

    void dump_plugin_features() const;
    void dump_host_features() const;
    void dump_ports() const;

    bool prepare_ports_and_buffers();
    void activate();
    void deactivate();

    void wire_worker_interface();

    bool run_offline(double duration_sec, double freq_hz, float gain, bool midi_enabled, int midi_note,
                     const std::string &out_path);

    const LV2_Feature *const *get_features() const {
        return features;
    }
    LV2_URID_Map *urid_map() {
        return &map;
    }
    const URIDs &get_urids() const {
        return urids;
    }
    double sample_rate() const {
        return sr;
    }
    uint32_t block_frames() const {
        return block;
    }

    void rt_deliver_worker_responses();
    void non_rt_do_worker_requests();
};

} // namespace godot

#endif
