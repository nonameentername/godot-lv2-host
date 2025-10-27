#include "lv2_instance.h"
#include "lv2_control.h"
#include "lv2_server.h"
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

using Lv2Instance = godot::Lv2Instance;

Lv2Instance::Lv2Instance() {
    lv2_host = NULL;
    initialized = false;
    active = false;
    channels_cleared = false;

    finished = false;

    world = Lv2Server::get_singleton()->get_lilv_world();
    mix_rate = AudioServer::get_singleton()->get_mix_rate();

    //TODO: update block size
    int p_frames = 512;
    lv2_host = new Lv2Host(world, mix_rate, p_frames);

	if (!lv2_host->load_world()) {
        //TODO: log to godot
		std::cerr << "Failed to create/load lv2 world\n";
	}

    mutex.instantiate();
    semaphore.instantiate();

    temp_buffer.resize(BUFFER_FRAME_SIZE);

    for (int i = 0; i < BUFFER_FRAME_SIZE; i++) {
        temp_buffer.ptrw()[i] = 0;
    }
}

void Lv2Instance::configure_lv2() {
    lock();

	if (!lv2_host->find_plugin(std::string(uri.ascii()))) {
		std::cerr << "Plugin not found: " << uri.ascii() << "\n";
	}

	if (!lv2_host->instantiate()) {
		std::cerr << "Failed to instantiate plugin\n";
	}

    std::vector<std::pair<std::string, float>> cli_sets;

	lv2_host->wire_worker_interface();
	lv2_host->set_cli_control_overrides(cli_sets);

    //TODO: use the block from godot instead of 512
    int p_frames = 512;
	if (!lv2_host->prepare_ports_and_buffers(p_frames)) {
		std::cerr << "Failed to prepare/connect ports\n";
	}

    for (int i = 0; i < lv2_host->get_input_control_count(); i++) {
        const LilvControl *control = lv2_host->get_input_control(i);
        Lv2Control *lv2_control = memnew(Lv2Control);
        lv2_control->set_index(i);
        lv2_control->set_symbol(control->symbol.c_str());
        lv2_control->set_name(control->name.c_str());
        lv2_control->set_unit(control->unit.c_str());
        lv2_control->set_default(control->def);
        lv2_control->set_min(control->min);
        lv2_control->set_max(control->max);
        lv2_control->set_logarithmic(control->logarithmic);
        lv2_control->set_integer(control->integer);
        lv2_control->set_enumeration(control->enumeration);
        lv2_control->set_toggle(control->toggle);

        for (int j = 0; j < control->choices.size(); j++) {
            lv2_control->set_choice(control->choices[j].first.c_str(), control->choices[j].second);
        }

        input_controls.append(lv2_control);
    }

    for (int i = 0; i < lv2_host->get_output_control_count(); i++) {
        const LilvControl *control = lv2_host->get_output_control(i);
        Lv2Control *lv2_control = memnew(Lv2Control);
        lv2_control->set_index(i);
        lv2_control->set_symbol(control->symbol.c_str());
        lv2_control->set_name(control->name.c_str());
        lv2_control->set_unit(control->unit.c_str());
        lv2_control->set_default(control->def);
        lv2_control->set_min(control->min);
        lv2_control->set_max(control->max);
        lv2_control->set_logarithmic(control->logarithmic);
        lv2_control->set_integer(control->integer);
        lv2_control->set_enumeration(control->enumeration);
        lv2_control->set_toggle(control->toggle);

        for (int j = 0; j < control->choices.size(); j++) {
            lv2_control->set_choice(control->choices[j].first.c_str(), control->choices[j].second);
        }

        output_controls.append(lv2_control);
    }

    input_channels.resize(lv2_host->get_input_channel_count());
    output_channels.resize(lv2_host->get_output_channel_count());

    std::vector<std::string> host_presets = lv2_host->get_presets();

    presets.clear();

    for (int i = 0; i < host_presets.size(); i++) {
        presets.push_back(host_presets[i].c_str());
    }

    /*
    for (int channel = 0; channel < output_channels.size(); channel++) {
        output_channels[channel].buffer.write_channel(temp_buffer.ptrw(), p_frames);
    }
    */

    unlock();
}

Lv2Instance::~Lv2Instance() {
    if (lv2_host != NULL) {
        delete lv2_host;
        lv2_host = NULL;
    }
}

void Lv2Instance::start() {
    if (lv2_host != NULL) {
        lv2_host->activate();

        initialized = true;
        start_thread();

        emit_signal("lv2_ready", lv2_name);
    }
}

void Lv2Instance::stop() {
    bool prev_initialized = initialized;
    initialized = false;
    stop_thread();

    if (lv2_host != NULL) {
        lv2_host->deactivate();
        if (prev_initialized) {
            cleanup_channels();
        }
    }
}

void Lv2Instance::finish() {
    stop();
    finished = true;
}

void Lv2Instance::reset() {
    bool prev_initialized = initialized;
    initialized = false;
    stop_thread();

    if (lv2_host != NULL) {
        if (prev_initialized) {
            cleanup_channels();
        }
        //TODO: handle resetting the host
        //lv2_host->Reset();
        configure_lv2();
    }
}

void Lv2Instance::cleanup_channels() {
    lock();

    input_channels.clear();
    output_channels.clear();

    unlock();
}

int Lv2Instance::process_sample(AudioFrame *p_buffer, float p_rate, int p_frames) {
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

    for (int channel = 0; channel < output_channels.size(); channel++) {
        output_channels[channel].buffer.update_read_index(p_frames);
    }

    unlock();

    semaphore->post();

    return p_frames;
}

void Lv2Instance::set_channel_sample(AudioFrame *p_buffer, float p_rate, int p_frames, int left, int right) {
    bool has_left_channel = left >= 0 && left < input_channels.size();
    bool has_right_channel = right >= 0 && right < input_channels.size();

    if (!has_left_channel && !has_right_channel && !active) {
        return;
    }

    lock();

    if (has_left_channel) {
        for (int frame = 0; frame < p_frames; frame++) {
            temp_buffer.ptrw()[frame] = p_buffer[frame].left;
        }

        input_channels[left].write_channel(temp_buffer.ptrw(), p_frames);
    }

    if (has_right_channel) {
        for (int frame = 0; frame < p_frames; frame++) {
            temp_buffer.ptrw()[frame] = p_buffer[frame].right;
        }

        input_channels[right].write_channel(temp_buffer.ptrw(), p_frames);
    }

    //TODO: does lv2 expect empty channels to be sent?

    unlock();
}

int Lv2Instance::get_channel_sample(AudioFrame *p_buffer, float p_rate, int p_frames, int left, int right) {
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

void Lv2Instance::program_select(int chan, int bank_num, int preset_num) {
}

void Lv2Instance::note_on(int midi_bus, int chan, int key, int vel) {
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

    lv2_host->write_midi_in(midi_bus, event);
}

void Lv2Instance::note_off(int midi_bus, int chan, int key) {
    if (!initialized) {
        return;
    }

    MidiEvent event;
    event.data[0] = (MIDIMessage::MIDI_MESSAGE_NOTE_OFF << 4) | (chan & 0x0F);
    event.data[1] = key;
    event.data[2] = 0;

    lv2_host->write_midi_in(midi_bus, event);
}


void Lv2Instance::control_change(int midi_bus, int chan, int control, int value) {
    if (!initialized) {
        return;
    }

    MidiEvent event;
    event.data[0] = (MIDIMessage::MIDI_MESSAGE_CONTROL_CHANGE << 4) | (chan & 0x0F);
    event.data[1] = control;
    event.data[2] = value;

    lv2_host->write_midi_in(midi_bus, event);
}

void Lv2Instance::send_input_control_channel(int p_channel, float p_value) {
    if (!initialized) {
        return;
    }

    lv2_host->set_input_control_value(p_channel, p_value);
}

float Lv2Instance::get_input_control_channel(int p_channel) {
    if (!initialized) {
        return 0;
    }

    return lv2_host->get_input_control_value(p_channel);
}

void Lv2Instance::send_output_control_channel(int p_channel, float p_value) {
    if (!initialized) {
        return;
    }

    lv2_host->set_output_control_value(p_channel, p_value);
}

float Lv2Instance::get_output_control_channel(int p_channel) {
    if (!initialized) {
        return 0;
    }

    return lv2_host->get_output_control_value(p_channel);
}

void Lv2Instance::pitch_bend(int chan, int val) {
}

void Lv2Instance::thread_func() {
    int p_frames = 512;

    while (!exit_thread) {
        if (!initialized) {
            continue;
        }

        last_mix_frames = p_frames;

        lock();

        float volume = godot::UtilityFunctions::db_to_linear(volume_db);

        if (Lv2Server::get_singleton()->get_solo_mode()) {
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
            channel_peak.ptrw()[i] = 0;
        }

        for (int channel = 0; channel < lv2_host->get_input_channel_count(); channel++) {
            input_channels[channel].read_channel(temp_buffer.ptrw(), p_frames);

            for (int frame = 0; frame < p_frames; frame++) {
                lv2_host->get_input_channel_buffer(channel)[frame] = temp_buffer[frame];
            }
        }

        int result = lv2_host->perform(p_frames);
        if (result == 0) {
            finished = true;
        }

        if (bypass) {
            for (int channel = 0; channel < lv2_host->get_output_channel_count(); channel++) {
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
            for (int channel = 0; channel < lv2_host->get_output_channel_count(); channel++) {
                for (int frame = 0; frame < p_frames; frame++) {
                    float value = lv2_host->get_output_channel_buffer(channel)[frame] * volume;
                    float p = Math::abs(value);
                    if (p > channel_peak[channel]) {
                        channel_peak.ptrw()[channel] = p;
                    }
                    temp_buffer.ptrw()[frame] = value;
                }
                output_channels[channel].buffer.write_channel(temp_buffer.ptr(), p_frames);
            }
        }

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

Error Lv2Instance::start_thread() {
    if (thread.is_null()) {
        thread.instantiate();
        exit_thread = false;
        thread->start(callable_mp(this, &Lv2Instance::thread_func), Thread::PRIORITY_HIGH);
    }
    return (Error)OK;
}

void Lv2Instance::stop_thread() {
    if (thread.is_valid()) {
        exit_thread = true;
        semaphore->post();
        thread->wait_to_finish();
        thread.unref();
    }
}

void Lv2Instance::lock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->lock();
}

void Lv2Instance::unlock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->unlock();
}

void Lv2Instance::initialize() {
    configure_lv2();

    start();
}

void Lv2Instance::set_lv2_name(const String &name) {
    lv2_name = name;
}

const String &Lv2Instance::get_lv2_name() {
    return lv2_name;
}

int Lv2Instance::get_input_channel_count() {
    if (lv2_host != NULL) {
        return lv2_host->get_input_channel_count();
    } else {
        return 0;
    }
}

int Lv2Instance::get_output_channel_count() {
    if (lv2_host != NULL) {
        return lv2_host->get_output_channel_count();
    } else {
        return 0;
    }
}

int Lv2Instance::get_input_midi_count() {
    if (lv2_host != NULL) {
        return lv2_host->get_input_midi_count();
    } else {
        return 0;
    }
}

int Lv2Instance::get_output_midi_count() {
    if (lv2_host != NULL) {
        return lv2_host->get_output_midi_count();
    } else {
        return 0;
    }
}

TypedArray<Lv2Control> Lv2Instance::get_input_controls() {
    return input_controls;
}

TypedArray<Lv2Control> Lv2Instance::get_output_controls() {
    return output_controls;
}

TypedArray<String> Lv2Instance::get_presets() {
    return presets;
}

void Lv2Instance::load_preset(String p_preset) {
    lv2_host->load_preset(std::string(p_preset.ascii()));
}

double Lv2Instance::get_time_since_last_mix() {
    return (Time::get_singleton()->get_ticks_usec() - last_mix_time) / 1000000.0;
}

double Lv2Instance::get_time_to_next_mix() {
    double total = get_time_since_last_mix();
    double mix_buffer = last_mix_frames / AudioServer::get_singleton()->get_mix_rate();
    return mix_buffer - total;
}

void Lv2Instance::set_active(bool p_active) {
    active = p_active;
    channels_cleared = false;
    if (finished && p_active) {
        reset();
        finished = false;
        start();
    }
}

bool Lv2Instance::is_active() {
    return active;
}

void Lv2Instance::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &Lv2Instance::initialize);
    ClassDB::bind_method(D_METHOD("program_select", "chan", "bank_num", "preset_num"), &Lv2Instance::program_select);
    ClassDB::bind_method(D_METHOD("finish"), &Lv2Instance::finish);

    ClassDB::bind_method(D_METHOD("note_on", "chan", "key", "vel"), &Lv2Instance::note_on);
    ClassDB::bind_method(D_METHOD("note_off", "chan", "key"), &Lv2Instance::note_off);
    ClassDB::bind_method(D_METHOD("control_change", "chan", "control", "key"), &Lv2Instance::control_change);

    ClassDB::bind_method(D_METHOD("send_input_control_channel", "channel", "value"), &Lv2Instance::send_input_control_channel);
    ClassDB::bind_method(D_METHOD("get_input_control_channel", "channel"), &Lv2Instance::get_input_control_channel);

    ClassDB::bind_method(D_METHOD("send_output_control_channel", "channel", "value"), &Lv2Instance::send_output_control_channel);
    ClassDB::bind_method(D_METHOD("get_output_control_channel", "channel"), &Lv2Instance::get_output_control_channel);

    ClassDB::bind_method(D_METHOD("pitch_bend", "chan", "vel"), &Lv2Instance::pitch_bend);

    ClassDB::bind_method(D_METHOD("set_lv2_name", "name"), &Lv2Instance::set_lv2_name);
    ClassDB::bind_method(D_METHOD("get_lv2_name"), &Lv2Instance::get_lv2_name);

    ClassDB::bind_method(D_METHOD("get_input_controls"), &Lv2Instance::get_input_controls);
    ClassDB::bind_method(D_METHOD("get_output_controls"), &Lv2Instance::get_output_controls);

    ClassDB::bind_method(D_METHOD("get_presets"), &Lv2Instance::get_presets);
    ClassDB::bind_method(D_METHOD("load_preset", "preset"), &Lv2Instance::load_preset);

    ClassDB::add_property("Lv2Instance", PropertyInfo(Variant::STRING, "lv2_name"), "set_lv2_name",
                          "get_lv2_name");

    ADD_SIGNAL(MethodInfo("lv2_ready", PropertyInfo(Variant::STRING, "lv2_name")));
}

}
