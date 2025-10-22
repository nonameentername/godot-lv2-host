#include "lilv_instance.h"
#include "lilv_server.h"
#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/audio_stream_wav.hpp"
#include "godot_cpp/classes/audio_stream_mp3.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/classes/time.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/variant.hpp"
#include <cstdio>
#include <cstdlib>

using namespace godot;

namespace godot {

using LilvInstance = godot::LilvInstance;

LilvInstance::LilvInstance() {
    lilv_host = NULL;
    initialized = false;
    active = false;
    channels_cleared = false;

    finished = false;

    world = LilvServer::get_singleton()->get_lilv_world();
    mix_rate = AudioServer::get_singleton()->get_mix_rate();

    //TODO: update block size
    lilv_host = new LilvHost(world, mix_rate, 512);

	if (!lilv_host->load_world()) {
        //TODO: log to godot
		std::cerr << "Failed to create/load lilv world\n";
	}

    configure_lilv();

    mutex.instantiate();
    semaphore.instantiate();

    temp_buffer.resize(BUFFER_FRAME_SIZE);

    for (int i = 0; i < BUFFER_FRAME_SIZE; i++) {
        temp_buffer.ptrw()[i] = 0;
    }
    call_deferred("initialize");
}

void LilvInstance::configure_lilv() {

    std::string plugin_uri = "http://code.google.com/p/amsynth/amsynth";

	if (!lilv_host->find_plugin(plugin_uri)) {
		std::cerr << "Plugin not found: " << plugin_uri << "\n";
	}

	if (!lilv_host->instantiate()) {
		std::cerr << "Failed to instantiate plugin\n";
	}

    std::vector<std::pair<std::string, float>> cli_sets;

	lilv_host->wire_worker_interface();
	lilv_host->set_cli_control_overrides(cli_sets);

    //TODO: use the block from godot instead of 512
	if (!lilv_host->prepare_ports_and_buffers(512)) {
		std::cerr << "Failed to prepare/connect ports\n";
	}
}

LilvInstance::~LilvInstance() {
    if (lilv_host != NULL) {
        delete lilv_host;
        lilv_host = NULL;
    }
}

void LilvInstance::start() {
    if (lilv_host != NULL) {
        lilv_host->activate();

        input_channels.resize(lilv_host->get_input_channel_count());
        output_channels.resize(lilv_host->get_output_channel_count());

        //TODO: this is a hack.  Figure out a nicer way to do this
        //move the output read index back so the read and write index are not the same
        int p_frames = 512;
        for (int channel = 0; channel < lilv_host->get_output_channel_count(); channel++) {
            output_channels[channel].buffer.update_read_index(CIRCULAR_BUFFER_SIZE - p_frames);
        }

        initialized = true;
        start_thread();

        emit_signal("lilv_ready", lilv_name);
    }
}

void LilvInstance::stop() {
    bool prev_initialized = initialized;
    initialized = false;
    stop_thread();

    if (lilv_host != NULL) {
        lilv_host->deactivate();
        if (prev_initialized) {
            cleanup_channels();
        }
    }
}

void LilvInstance::finish() {
    stop();
    finished = true;
}

void LilvInstance::reset() {
    bool prev_initialized = initialized;
    initialized = false;
    stop_thread();

    if (lilv_host != NULL) {
        if (prev_initialized) {
            cleanup_channels();
        }
        //TODO: handle resetting the host
        //lilv_host->Reset();
        configure_lilv();
    }
}

void LilvInstance::cleanup_channels() {
    input_channels.clear();
    output_channels.clear();
}

int LilvInstance::process_sample(AudioFrame *p_buffer, float p_rate, int p_frames) {
    if (finished) {
        return 0;
    }

    lock();

    int read = p_frames;

    if (Time::get_singleton()) {
        last_mix_time = Time::get_singleton()->get_ticks_usec();
    }

    if (!initialized || output_channels.size() == 0) {
        for (int frame = 0; frame < p_frames; frame++) {
            p_buffer[frame].left = 0;
            p_buffer[frame].right = 0;
        }
    } else if (output_channels.size() > 1) {
        output_channels[0].buffer.read_channel(temp_buffer.ptrw(), p_frames);
        for (int frame = 0; frame < read; frame++) {
            p_buffer[frame].left = temp_buffer[frame];
        }

        output_channels[1].buffer.read_channel(temp_buffer.ptrw(), p_frames);
        for (int frame = 0; frame < read; frame++) {
            p_buffer[frame].right = temp_buffer[frame];
        }
    } else if (output_channels.size() > 0) {
        output_channels[0].buffer.read_channel(temp_buffer.ptrw(), p_frames);
        for (int frame = 0; frame < read; frame++) {
            p_buffer[frame].left = temp_buffer[frame];
            p_buffer[frame].right = temp_buffer[frame];
        }
    }

    //TODO: when should the read index be updated?? before or after peform?
    for (int channel = 0; channel < lilv_host->get_output_channel_count(); channel++) {
        output_channels[channel].buffer.update_read_index(p_frames);
    }

    unlock();

    semaphore->post();

    return p_frames;
}

void LilvInstance::set_channel_sample(AudioFrame *p_buffer, float p_rate, int p_frames, int left, int right) {
    bool has_left_channel = left >= 0 && left < input_channels.size();
    bool has_right_channel = right >= 0 && right < input_channels.size();

    if (!has_left_channel && !has_right_channel && !active) {
        return;
    }

    lock();

    if (has_left_channel) {
        for (int frame = 0; frame < p_frames; frame++) {
            temp_buffer.write[frame] = p_buffer[frame].left;
        }

        input_channels[left].write_channel(temp_buffer.ptrw(), p_frames);
    }

    if (has_right_channel) {
        for (int frame = 0; frame < p_frames; frame++) {
            temp_buffer.write[frame] = p_buffer[frame].right;
        }

        input_channels[right].write_channel(temp_buffer.ptrw(), p_frames);
    }

    //TODO: does lv2 expect empty channels to be sent?

    unlock();
}

int LilvInstance::get_channel_sample(AudioFrame *p_buffer, float p_rate, int p_frames, int left, int right) {
    bool has_left_channel = left >= 0 && left < output_channels.size();
    bool has_right_channel = right >= 0 && right < output_channels.size();

    if (finished) {
        return 0;
    }

    lock();

    if (has_left_channel && active) {
        output_channels[left].buffer.read_channel(temp_buffer.ptrw(), p_frames);
        for (int frame = 0; frame < p_frames; frame++) {
            p_buffer[frame].left = temp_buffer[frame];
        }
    } else {
        for (int frame = 0; frame < p_frames; frame++) {
            p_buffer[frame].left = 0;
        }
    }
    if (has_right_channel && active) {
        output_channels[right].buffer.read_channel(temp_buffer.ptrw(), p_frames);
        for (int frame = 0; frame < p_frames; frame++) {
            p_buffer[frame].right = temp_buffer[frame];
        }
    } else {
        for (int frame = 0; frame < p_frames; frame++) {
            p_buffer[frame].right = 0;
        }
    }

    unlock();

    return p_frames;
}

void LilvInstance::program_select(int chan, int bank_num, int preset_num) {
}

void LilvInstance::note_on(int midi_bus, int chan, int key, int vel) {
    if (!initialized) {
        return;
    }

    MidiEvent event;

    //TODO: move the midi bit logic to a shared function
    if (vel > 0) {
        event.data[0] = (MIDIMessage::MIDI_MESSAGE_NOTE_ON << 4) | (chan & 0x0F);
    } else {
        event.data[0] = (MIDIMessage::MIDI_MESSAGE_NOTE_OFF << 4) | (chan & 0x0F);
    }

    event.data[1] = key;
    event.data[2] = vel;

    lilv_host->write_midi_in(midi_bus, event);
}

void LilvInstance::note_off(int midi_bus, int chan, int key) {
    if (!initialized) {
        return;
    }

    MidiEvent event;
    event.data[0] = (MIDIMessage::MIDI_MESSAGE_NOTE_OFF << 4) | (chan & 0x0F);
    event.data[1] = key;
    event.data[2] = 0;

    lilv_host->write_midi_in(midi_bus, event);
}


void LilvInstance::control_change(int midi_bus, int chan, int control, int value) {
    if (!initialized) {
        return;
    }

    MidiEvent event;
    event.data[0] = (MIDIMessage::MIDI_MESSAGE_CONTROL_CHANGE << 4) | (chan & 0x0F);
    event.data[1] = control;
    event.data[2] = value;

    lilv_host->write_midi_in(midi_bus, event);
}

//TODO: allow setting both the input and output controls
void LilvInstance::send_control_channel(int p_channel, float p_value) {
    if (!initialized) {
        return;
    }

    lilv_host->set_input_control_value(p_channel, p_value);
}

//TODO: allow setting both the input and output controls
float LilvInstance::get_control_channel(int p_channel) {
    if (!initialized) {
        return 0;
    }

    return lilv_host->get_output_control_value(p_channel);
}

void LilvInstance::pitch_bend(int chan, int val) {
}

void LilvInstance::thread_func() {
    int p_frames = 512;

    while (!exit_thread) {
        if (!initialized) {
            continue;
        }

        last_mix_frames = p_frames;

        lock();

        float volume = godot::UtilityFunctions::db_to_linear(volume_db);

        if (LilvServer::get_singleton()->get_solo_mode()) {
            if (!solo) {
                volume = 0.0;
            }
        } else {
            if (mute) {
                volume = 0.0;
            }
        }

        Vector<float> channel_peak;
        channel_peak.resize(output_channels.size());
        for (int i = 0; i < output_channels.size(); i++) {
            channel_peak.write[i] = 0;
        }

        Vector<float> named_channel_peak;
        named_channel_peak.resize(output_named_channels.size());
        for (int i = 0; i < output_named_channels.size(); i++) {
            named_channel_peak.write[i] = 0;
        }

        for (int channel = 0; channel < lilv_host->get_input_channel_count(); channel++) {
            input_channels[channel].read_channel(temp_buffer.ptrw(), p_frames);

            for (int frame = 0; frame < p_frames; frame++) {
                lilv_host->get_input_channel_buffer(channel)[frame] = temp_buffer[frame];
            }
        }

        int result = lilv_host->perform(p_frames);
        if (result == 0) {
            finished = true;
        }

        if (bypass) {
            for (int channel = 0; channel < lilv_host->get_output_channel_count(); channel++) {
                if (channel < input_channels.size()) {
                    input_channels[channel].read_channel(temp_buffer.ptrw(), p_frames);
                } else {
                    for (int frame = 0; frame < p_frames; frame++) {
                        temp_buffer.ptrw()[frame] = 0;
                    }
                }
                output_channels[channel].buffer.write_channel(temp_buffer.ptr(), p_frames);
            }
        } else {
            for (int channel = 0; channel < lilv_host->get_output_channel_count(); channel++) {
                for (int frame = 0; frame < p_frames; frame++) {
                    float value = lilv_host->get_output_channel_buffer(channel)[frame] * volume;
                    float p = Math::abs(value);
                    if (p > channel_peak[channel]) {
                        channel_peak.write[channel] = p;
                    }
                    temp_buffer.write[frame] = value;
                }
                output_channels[channel].buffer.write_channel(temp_buffer.ptr(), p_frames);
            }
        }

        for (int channel = 0; channel < lilv_host->get_input_channel_count(); channel++) {
            input_channels[channel].update_read_index(p_frames);
        }

        //TODO: when should the read index be updated?? before or after peform?
        //for (int channel = 0; channel < lilv_host->get_output_channel_count(); channel++) {
        //    output_channels[channel].buffer.update_read_index(p_frames);
        //}

        for (int channel = 0; channel < output_channels.size(); channel++) {
            output_channels[channel].peak_volume = godot::UtilityFunctions::linear_to_db(channel_peak[channel] + AUDIO_PEAK_OFFSET);

            if (channel_peak[channel] > 0) {
                output_channels[channel].active = true;
            } else {
                output_channels[channel].active = false;
            }
        }

        unlock();

        semaphore->wait();
    }
}

Error LilvInstance::start_thread() {
    if (thread.is_null()) {
        thread.instantiate();
        exit_thread = false;
        thread->start(callable_mp(this, &LilvInstance::thread_func), Thread::PRIORITY_HIGH);
    }
    return (Error)OK;
}

void LilvInstance::stop_thread() {
    if (thread.is_valid()) {
        exit_thread = true;
        semaphore->post();
        thread->wait_to_finish();
        thread.unref();
    }
}

void LilvInstance::lock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->lock();
}

void LilvInstance::unlock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->unlock();
}

void LilvInstance::initialize() {
    start();
}

void LilvInstance::set_lilv_name(const String &name) {
    lilv_name = name;
}

const String &LilvInstance::get_lilv_name() {
    return lilv_name;
}

int LilvInstance::get_input_channel_count() {
    if (lilv_host != NULL) {
        return lilv_host->get_input_channel_count();
    } else {
        return 0;
    }
}

int LilvInstance::get_output_channel_count() {
    if (lilv_host != NULL) {
        return lilv_host->get_output_channel_count();
    } else {
        return 0;
    }
}

int LilvInstance::get_input_midi_count() {
    if (lilv_host != NULL) {
        return lilv_host->get_input_midi_count();
    } else {
        return 0;
    }
}

int LilvInstance::get_output_midi_count() {
    if (lilv_host != NULL) {
        return lilv_host->get_output_midi_count();
    } else {
        return 0;
    }
}

double LilvInstance::get_time_since_last_mix() {
    return (Time::get_singleton()->get_ticks_usec() - last_mix_time) / 1000000.0;
}

double LilvInstance::get_time_to_next_mix() {
    double total = get_time_since_last_mix();
    double mix_buffer = last_mix_frames / AudioServer::get_singleton()->get_mix_rate();
    return mix_buffer - total;
}

void LilvInstance::set_active(bool p_active) {
    active = p_active;
    channels_cleared = false;
    if (finished && p_active) {
        reset();
        finished = false;
        start();
    }
}

bool LilvInstance::is_active() {
    return active;
}

void LilvInstance::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &LilvInstance::initialize);
    ClassDB::bind_method(D_METHOD("program_select", "chan", "bank_num", "preset_num"), &LilvInstance::program_select);
    ClassDB::bind_method(D_METHOD("finish"), &LilvInstance::finish);

    ClassDB::bind_method(D_METHOD("note_on", "chan", "key", "vel"), &LilvInstance::note_on);
    ClassDB::bind_method(D_METHOD("note_off", "chan", "key"), &LilvInstance::note_off);
    ClassDB::bind_method(D_METHOD("control_change", "chan", "control", "key"), &LilvInstance::control_change);

    ClassDB::bind_method(D_METHOD("send_control_channel", "channel", "value"), &LilvInstance::send_control_channel);
    ClassDB::bind_method(D_METHOD("get_control_channel", "channel"), &LilvInstance::get_control_channel);

    ClassDB::bind_method(D_METHOD("pitch_bend", "chan", "vel"), &LilvInstance::pitch_bend);

    ClassDB::bind_method(D_METHOD("set_lilv_name", "name"), &LilvInstance::set_lilv_name);
    ClassDB::bind_method(D_METHOD("get_lilv_name"), &LilvInstance::get_lilv_name);

    ClassDB::add_property("LilvInstance", PropertyInfo(Variant::STRING, "lilv_name"), "set_lilv_name",
                          "get_lilv_name");

    ADD_SIGNAL(MethodInfo("lilv_ready", PropertyInfo(Variant::STRING, "lilv_name")));
}

}
