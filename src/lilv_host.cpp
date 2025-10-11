#include "lilv_host.h"
#include <iostream>
#include <lilv/lilv.h>
#include <string>

using namespace godot;

//TODO: delete this functino
static inline const char *cstr_or(const char *s, const char *fallback = "") {
    return s ? s : fallback;
}

LilvHost::LilvHost() {
    world = lilv_world_new();

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


    //TODO: allow setting a location for the lv2 plugins
    /*
	LilvNode* lv2_path = lilv_new_string(world, "~/.lv2");
	lilv_world_set_option(world, LILV_OPTION_LV2_PATH, lv2_path);
  	lilv_node_free(lv2_path);
    */

    lilv_world_load_all(world);

    //TODO: allow configuration of the plugin_uri
    //std::string plugin_uri = "https://github.com/nonameentername/godot-distrho";
    std::string plugin_uri = "http://calf.sourceforge.net/plugins/Analyzer";

    const LilvPlugin *plugin = get_plugin(plugin_uri);

    if (plugin) {
        std::cerr << "Plugin found: " << plugin_uri << "\n";
    } else {
        std::cerr << "Plugin not found: " << plugin_uri << "\n";
    }

    dump_features(plugin);

    dump_port_info(plugin);


    float sr = 48000.0;
    const LV2_Feature *features[1]{nullptr};

    LilvInstance *inst = lilv_plugin_instantiate(plugin, sr, features);

    if (inst) {
        std::cerr << "Plugin created: " << plugin_uri << "\n";
    } else {
        std::cerr << "Plugin not created: " << plugin_uri << "\n";
    }

    lilv_instance_activate(inst);


    lilv_instance_deactivate(inst);
}

LilvHost::~LilvHost() {
    lilv_node_free(AUDIO);
    lilv_node_free(CONTROL);
    lilv_node_free(CV);
    lilv_node_free(INPUT);
    lilv_node_free(OUTPUT);
    lilv_node_free(ATOM);
    lilv_node_free(SEQUENCE);
    lilv_node_free(BUFTYPE);
    lilv_node_free(SUPPORTS);
    lilv_node_free(MIDI_EVENT);

    lilv_world_free(world);
}

void LilvHost::dump_features(const LilvPlugin *plugin) {
    //TODO: clean up this code to avoid using weird auto function
    auto _dump_features = [&](const LilvPlugin *plg) {
        auto print_nodes = [&](const char *label, const LilvNodes *nodes) {
            std::cout << label << ":\n";
            if (!nodes || lilv_nodes_size(nodes) == 0) {
                std::cout << "  (none)\n";
                return;
            }
            LILV_FOREACH(nodes, it, nodes) {
                const LilvNode *n = lilv_nodes_get(nodes, it);
                std::string value = lilv_node_as_string(n);
                if (value.length() == 0) {
                    value = "(null)";
                }
                std::cout << "  - " << value << "\n";
            }
        };
        LilvNodes *req = lilv_plugin_get_required_features(plg);
        LilvNodes *opt = lilv_plugin_get_optional_features(plg);

        print_nodes("requiredFeature", req);
        print_nodes("optionalFeature", opt);

        lilv_nodes_free(req);
        lilv_nodes_free(opt);
    };

    _dump_features(plugin);
}

void LilvHost::dump_port_info(const LilvPlugin *plugin) {
    //TODO: cleanup this function
    const uint32_t num_ports = lilv_plugin_get_num_ports(plugin);

    std::cout << "Ports for plugin:\n";
    for (uint32_t i = 0; i < num_ports; ++i) {
        const LilvPort *p = lilv_plugin_get_port_by_index(plugin, i);
        const LilvNode *sym = lilv_port_get_symbol(plugin, p);
        const char *sym_c = sym ? lilv_node_as_string(sym) : nullptr;
        bool is_audio = lilv_port_is_a(plugin, p, AUDIO);
        bool is_control = lilv_port_is_a(plugin, p, CONTROL);
        bool is_cv = lilv_port_is_a(plugin, p, CV);
        bool is_input = lilv_port_is_a(plugin, p, INPUT);
        bool is_output = lilv_port_is_a(plugin, p, OUTPUT);
        bool is_atom = lilv_port_is_a(plugin, p, ATOM);
        std::cout << "  [" << i << "] " << cstr_or(sym_c, "(no_symbol)") << "  " << (is_audio ? "audio " : "")
                  << (is_cv ? "cv " : "") << (is_control ? "control " : "")  << (is_atom ? "atom " : "") << (is_input ? "in " : "")
                  << (is_output ? "out " : "") << "\n";
    }
}

const LilvPlugin *LilvHost::get_plugin(std::string plugin_uri) {
    const LilvPlugins *plugins = lilv_world_get_all_plugins(world);
    LilvNode *uri_node = lilv_new_uri(world, plugin_uri.c_str());
    const LilvPlugin *plugin = nullptr;

    LILV_FOREACH(plugins, i, plugins) {
        const LilvPlugin *p = lilv_plugins_get(plugins, i);
        if (lilv_node_equals(lilv_plugin_get_uri(p), uri_node)) {
            plugin = p;
            break;
        }
    }
    lilv_node_free(uri_node);

    return plugin;
}

std::vector<std::string> LilvHost::get_plugins() {
    std::vector<std::string> plugin_list;
    const LilvPlugins *plugins = lilv_world_get_all_plugins(world);

    LILV_FOREACH(plugins, i, plugins) {
        const LilvPlugin *p = lilv_plugins_get(plugins, i);
        const LilvNode* uri = lilv_plugin_get_uri(p);
        plugin_list.push_back(lilv_node_as_string(uri));
    }

    return plugin_list;
}
