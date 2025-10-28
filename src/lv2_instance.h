#ifndef LV2_INSTANCE_H
#define LV2_INSTANCE_H

#include "godot_cpp/classes/mutex.hpp"
#include "godot_cpp/classes/semaphore.hpp"
#include "godot_cpp/classes/thread.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "lilv/lilv.h"
#include "lv2_control.h"
#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <lv2_circular_buffer.h>
#include <lv2_host.h>

static const float AUDIO_PEAK_OFFSET = 0.0000000001f;
static const float AUDIO_MIN_PEAK_DB = -200.0f;
static const int BUFFER_FRAME_SIZE = 512;
static const int CIRCULAR_BUFFER_SIZE = BUFFER_FRAME_SIZE * 2 + 10;

namespace godot {

class Lv2Instance : public Object {
    GDCLASS(Lv2Instance, Object);
    friend class Lv2Server;

private:
    LilvWorld *world;
    uint64_t last_mix_time;
    int last_mix_frames;
    bool active;
    bool channels_cleared;

    int sfont_id;
    Lv2Host *lv2_host;
    bool finished;
    String lv2_name;
    bool solo;
    bool mute;
    bool bypass;
    float volume_db;
    String uri;
    bool initialized;
    bool has_processed_audio;
    double mix_rate;

    bool thread_exited;
    mutable bool exit_thread;
    Ref<Thread> thread;
    Ref<Mutex> mutex;
    Ref<Semaphore> semaphore;

    struct Channel {
        String name;
        bool used = false;
        bool active = false;
        float peak_volume = AUDIO_MIN_PEAK_DB;
        Lv2CircularBuffer<float> buffer;
        Channel() {
        }
    };

    void *midi_buffer;

    std::vector<Lv2CircularBuffer<float>> input_channels;
    std::vector<Channel> output_channels;

    Vector<float> temp_buffer;

    Channel output_left_channel;
    Channel output_right_channel;

    HashMap<double, double> lv2_data;

    TypedArray<Lv2Control> input_controls;
    TypedArray<Lv2Control> output_controls;

    TypedArray<String> presets;

    void configure_lv2();

    Error start_thread();
    void stop_thread();
    void lock();
    void unlock();
    void cleanup_channels();

protected:
    static void _bind_methods();

public:
    Lv2Instance();
    ~Lv2Instance();

    void start();
    void stop();
    void finish();
    void reset();

    void program_select(int chan, int bank_num, int preset_num);

    void note_on(int midi_bus, int chan, int key, int vel);
    void note_off(int midi_bus, int chan, int key);
    void control_change(int midi_bus, int chan, int control, int value);

    void send_input_control_channel(int p_channel, float p_value);
    float get_input_control_channel(int p_channel);

    void send_output_control_channel(int p_channel, float p_value);
    float get_output_control_channel(int p_channel);

    // val value (0-16383 with 8192 being center)
    void pitch_bend(int chan, int val);

    int process_sample(AudioFrame *p_buffer, float p_rate, int p_frames);

    void set_channel_sample(AudioFrame *p_buffer, float p_rate, int p_frames, int left, int right);
    int get_channel_sample(AudioFrame *p_buffer, float p_rate, int p_frames, int left, int right);

    void set_lv2_name(const String &name);
    const String &get_lv2_name();

    int get_input_channel_count();
    int get_output_channel_count();

    int get_input_midi_count();
    int get_output_midi_count();

    TypedArray<Lv2Control> get_input_controls();
    TypedArray<Lv2Control> get_output_controls();

    TypedArray<String> get_presets();
    void load_preset(String p_preset);

    double get_time_since_last_mix();
    double get_time_to_next_mix();

    void thread_func();
    void initialize();

    void set_active(bool active);
    bool is_active();
};
} // namespace godot

#endif
