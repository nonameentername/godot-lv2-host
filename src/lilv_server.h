#ifndef LILV_PLUGIN_SERVER_H
#define LILV_PLUGIN_SERVER_H


#include "godot_cpp/classes/mutex.hpp"
#include "godot_cpp/classes/semaphore.hpp"
#include "godot_cpp/classes/thread.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include <functional>
#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/node.hpp>
#include <mutex>
#include <queue>

#include <lilv/lilv.h>


namespace godot {

const int num_channels = 16;
const int buffer_size = 2048;

class LilvServer : public Object {
    GDCLASS(LilvServer, Object);

private:
    bool initialized;

    bool active;

    mutable bool exit_thread;
    Ref<Thread> audio_thread;
    Ref<Mutex> audio_mutex;

    LilvWorld* world;

protected:
    static LilvServer *singleton;

public:
    LilvServer();
    ~LilvServer();

    static LilvServer *get_singleton();

    static void _bind_methods();

    void initialize();
    void audio_thread_func();

    void process();

    void note_on(int p_channel, int p_note, int p_velocity);
    void note_off(int p_channel, int p_note, int p_velocity);
    void program_change(int p_channel, int p_program_number);
    void control_change(int p_channel, int p_controller, int p_value);
    void pitch_bend(int p_channel, int p_value);
    void channel_pressure(int p_channel, int p_pressure);
    void midi_poly_aftertouch(int p_channel, int p_note, int p_pressure);

    template <typename T, typename R>
    void handle_rpc_call(std::function<void(typename T::Reader &, typename R::Builder &)> handle_request);

    int process_sample(AudioFrame *p_buffer, float p_rate, int p_frames);

    Error start();
    void lock_audio();
    void unlock_audio();
    void finish();

    String get_version();
    String get_build();
};
} // namespace godot

#endif
