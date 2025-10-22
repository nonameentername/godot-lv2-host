#include <cmath>
#include <cstring>
#include <sndfile.h>
#include <iostream>

#include "lilv_host.h"

using namespace godot;


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

bool run_offline(LilvHost *lilv_host, double sr, double duration_sec, double freq_hz, float gain, bool midi_enabled, int midi_note,
                           const std::string &out_path) {

    WavWriter writer;
    if (!writer.open(out_path.c_str(), (int)sr, (int)std::max(lilv_host->get_output_channel_count(), 1))) {
        std::cerr << "Failed to open output: " << out_path << "\n";
        return false;
    }

    int block = 1024;

    std::vector<float> zero_buf(block, 0.f);
    std::vector<const float *> outs(lilv_host->get_output_channel_count(), zero_buf.data());

    const size_t total_frames = (size_t)(duration_sec * sr);
    size_t done = 0;
    double phase = 0.0;
    const double twopi = 6.283185307179586;
    size_t elapsed_frames = 0;

    while (done < total_frames) {
        const uint32_t n = (uint32_t)std::min((size_t)block, total_frames - done);
        const uint32_t frames_to_write = n;

        if (lilv_host->get_input_channel_count() > 0) {
            for (uint32_t i = 0; i < n; ++i) {
                float s = gain * (float)std::sin(phase);
                phase += twopi * freq_hz / sr;
                for (uint32_t c = 0; c < lilv_host->get_input_channel_count(); ++c) {
                    lilv_host->get_input_channel_buffer(c)[i] = s;
                    //get_input_channel_buffer(c)[i] = 0;
                }
            }
            if (n < block) {
                for (uint32_t c = 0; c < lilv_host->get_input_channel_count(); ++c) {
                    //TODO: does adding n to get channel buffer work?
                    std::memset(lilv_host->get_input_channel_buffer(c) + n, 0, (block - n) * sizeof(float));
                }
            }
        }

        //for (int i = 0; i < lilv_host->get_input_midi_count(); i++) {
        //}

		if (midi_enabled && lilv_host->get_input_midi_count() > 0) {
			bool send_on = (elapsed_frames == 0);
			bool send_off = (elapsed_frames < (size_t)sr) && (elapsed_frames + n >= (size_t)sr);

			uint8_t ch = 0;
			uint8_t note_on = 0x90 | (ch & 0x0F);
			uint8_t note_off = 0x80 | (ch & 0x0F);

			if (send_on) {
				MidiEvent midi_event;
				midi_event.data[0] = note_on;
				midi_event.data[1] = (uint8_t)midi_note;
				midi_event.data[2] = 100;

				lilv_host->write_midi_in(0, midi_event);

				midi_event.data[1] = (uint8_t)midi_note + 4;
				lilv_host->write_midi_in(0, midi_event);

				midi_event.data[1] = (uint8_t)midi_note + 7;
				lilv_host->write_midi_in(0, midi_event);
			}

			if (send_off) {
				uint32_t t = (uint32_t)((size_t)sr - elapsed_frames);
				if (t >= n) {
					t = n - 1;
				}

				MidiEvent midi_event;
				midi_event.data[0] = note_off;
				midi_event.data[1] = (uint8_t)midi_note;
				midi_event.data[2] = 100;

				lilv_host->write_midi_in(0, midi_event);

				midi_event.data[1] = (uint8_t)midi_note + 4;
				lilv_host->write_midi_in(0, midi_event);

				midi_event.data[1] = (uint8_t)midi_note + 7;
				lilv_host->write_midi_in(0, midi_event);
			}
		}

		lilv_host->perform(block);

        bool dump_midi_out = false;

        if (dump_midi_out) {
            if (midi_enabled && lilv_host->get_output_midi_count() > 0) {
                MidiEvent midi_event;
                while(lilv_host->read_midi_out(0, midi_event)) {
                    std::cout << "  MIDI:";
                    for (uint32_t i = 0; i < midi_event.size; i++) {
                        std::cout << " " << std::hex << (int)midi_event.data[i] << std::dec;
                    }
                    std::cout << std::endl;
                }
            }
        }

        // write audio to wav
        for (uint32_t c = 0; c < lilv_host->get_output_channel_count(); ++c) {
            if (c < lilv_host->get_output_channel_count() && lilv_host->get_output_channel_buffer(c)) {
                outs[c] = lilv_host->get_output_channel_buffer(c);
            } else {
                outs[c] = zero_buf.data();
            }
        }

        if (frames_to_write > 0) {
            writer.write_interleaved((const float *const *)outs.data(), frames_to_write, (int)lilv_host->get_output_channel_count());
        }

        done += n;
        elapsed_frames += n;
    }

    return true;
}


int main(int argc, char **argv) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <plugin_uri>" << std::endl;
        return 1;
    }
    const std::string plugin_uri = argv[1];
	const double dur_sec = 3.0;
	const double sr      = 48000.0;
	uint32_t block       = 1024;
	const double freq    = 440.0;
	const float gain     = 0.2f;
	const std::string out_path = "out.wav";
	const uint32_t seq_capacity_hint = block * 64u + 2048u;

	bool midi_enabled = true;
    int midi_note = 60;

    std::vector<std::pair<std::string, float>> cli_sets;

    LilvWorld *world = lilv_world_new();

	LilvHost *lilv_host = new LilvHost(world, sr, block, seq_capacity_hint);

	if (!lilv_host->load_world()) {
		std::cerr << "Failed to create/load lilv world\n";
		return 2;
	}
	if (!lilv_host->find_plugin(plugin_uri)) {
		std::cerr << "Plugin not found: " << plugin_uri << "\n";
		return 2;
	}

    bool dump_plugin_info = true;

    if (dump_plugin_info) {
        lilv_host->dump_plugin_features();
    }

	if (!lilv_host->instantiate()) {
		std::cerr << "Failed to instantiate plugin\n";
		return 3;
	}

    if (dump_plugin_info) {
        lilv_host->dump_host_features();
    }

	lilv_host->wire_worker_interface();
	lilv_host->set_cli_control_overrides(cli_sets);

	if (!lilv_host->prepare_ports_and_buffers(block)) {
		std::cerr << "Failed to prepare/connect ports\n";
		return 3;
	}

    if (dump_plugin_info) {
        lilv_host->dump_ports();
    }

	if (dump_plugin_info) {
		std::cout << "input controls:" << std::endl;
		for (int i = 0; i < lilv_host->get_input_control_count(); i++) {
			const Control *control = lilv_host->get_input_control(i);
			std::cout << "  symbol[" << i <<  "] = " << control->symbol << std::endl;
			for (int j = 0; j < control->choices.size(); j++) {
				std::cout << "    choice = " << control->choices[j].first << std::endl;
			}
		}

		std::cout << "output controls:" << std::endl;
		for (int i = 0; i < lilv_host->get_output_control_count(); i++) {
			const Control *control = lilv_host->get_output_control(i);
			std::cout << "  symbol[" << i <<  "] = " << control->symbol << std::endl;
		}
	}

	lilv_host->activate();

    //Testing changing the volume parameter using http://code.google.com/p/amsynth/amsynth
    //lilv_host->set_input_control_value(14, 0.5);
    //std::cout << "master_volume = " << lilv_host->get_input_control_value(14) << std::endl;

	const bool ok = run_offline(lilv_host, sr, dur_sec, freq, gain, midi_enabled, midi_note, out_path);

	lilv_host->deactivate();

	if (!ok) {
		return 4;
	}

    std::cout << "Number of output channels = " << lilv_host->get_output_channel_count() << std::endl;

	std::cout << "Wrote " << out_path << "\n";

	delete lilv_host;

	return 0;
}
