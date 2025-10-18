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

#include "lilv_circular_buffer.h"

namespace godot {

const int MIDI_BUFFER_SIZE = 2048;

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

struct MidiEvent {
	static constexpr const uint32_t DATA_SIZE = 3;

	int frame{};
	uint8_t data[DATA_SIZE];
	int size = DATA_SIZE;
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
    std::vector<float *> audio_in_ptrs;
    std::vector<float *> audio_out_ptrs;
    uint32_t channels{};

    // Atom ports
    std::vector<AtomIn> atom_inputs;
    std::vector<AtomOut> atom_outputs;
    uint32_t seq_capacity_hint{}; // BYTES

    // Midi buffers
    std::vector<LilvCircularBuffer<int>> midi_input_buffer;
    std::vector<LilvCircularBuffer<int>> midi_output_buffer;

    // CLI overrides
    std::vector<std::pair<std::string, float>> cli_sets;

    // URID map/unmap
    static LV2_URID s_map_cb(LV2_URID_Map_Handle, const char *uri);
    static const char *s_unmap_cb(LV2_URID_Unmap_Handle, LV2_URID urid);
    LV2_URID map_uri(const char *uri);
    void premap_common_uris();
    void rebuild_options(int p_frames);

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
    LilvHost(double sr, int p_frames, uint32_t seq_bytes = 4096);
    ~LilvHost();

    LilvHost(const LilvHost &) = delete;
    LilvHost &operator=(const LilvHost &) = delete;

    bool load_world();
    bool find_plugin(const std::string &plugin_uri);
    bool instantiate();
    void set_cli_control_overrides(const std::vector<std::pair<std::string, float>> &name_value_pairs);

    void dump_plugin_features() const;
    void dump_host_features() const;
    void dump_ports() const;

    bool prepare_ports_and_buffers(int p_frames);
    void activate();
    void deactivate();

    void wire_worker_interface();

    int perform(int p_frames);

    int get_input_channel_count();
    int get_output_channel_count();

    int get_input_midi_count();
    int get_output_midi_count();

    float *get_input_channel_buffer(int p_channel);
    float *get_output_channel_buffer(int p_channel);

	void write_midi_in(int p_bus, const MidiEvent& p_midi_event);
	bool read_midi_in(int p_bus, MidiEvent& p_midi_event);

	void write_midi_out(int p_bus, const MidiEvent& p_midi_event);
	bool read_midi_out(int p_bus, MidiEvent& p_midi_event);

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

    void rt_deliver_worker_responses();
    void non_rt_do_worker_requests();
};

} // namespace godot

#endif
