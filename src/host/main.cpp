#include "lilv_host.h"
#include <iostream>

using namespace godot;

static inline const char* cstr_or(const char* s, const char* fallback = "") { return s ? s : fallback; }
static inline std::string to_string_safe(const char* s, const char* fallback = "") { return std::string(cstr_or(s, fallback)); }

int main(int argc, char **argv) {
    const std::string plugin_uri = to_string_safe(argv[1], "");
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

	LilvHost *lilv_host = new LilvHost(sr, block, seq_capacity_hint);

	if (!lilv_host->load_world()) {
		std::cerr << "Failed to create/load lilv world\n";
		return 2;
	}
	if (!lilv_host->find_plugin(plugin_uri)) {
		std::cerr << "Plugin not found: " << plugin_uri << "\n";
		return 2;
	}

	lilv_host->dump_plugin_features();

	if (!lilv_host->instantiate()) {
		std::cerr << "Failed to instantiate plugin\n";
		return 3;
	}
	lilv_host->dump_host_features();

	lilv_host->wire_worker_interface();
	lilv_host->setCliControlOverrides(cli_sets);

	if (!lilv_host->prepare_ports_and_buffers()) {
		std::cerr << "Failed to prepare/connect ports\n";
		return 3;
	}

	lilv_host->dump_ports();

	lilv_host->activate();

	const bool ok = lilv_host->run_offline(dur_sec, freq, gain, midi_enabled, midi_note, out_path);

	lilv_host->deactivate();

	if (!ok) {
		return 4;
	}

	std::cout << "Wrote " << out_path << "\n";

	delete lilv_host;

	return 0;
}
