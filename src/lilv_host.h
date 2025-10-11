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
#include <lv2/uri-map/uri-map.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lilv/lilv.h>
#include <string>
#include <vector>

namespace godot {

class LilvHost {
    private:
    LilvWorld *world;
    LilvNode *AUDIO;
    LilvNode *CONTROL;
    LilvNode *CV;
    LilvNode *INPUT;
    LilvNode *OUTPUT;
    LilvNode *ATOM;
    LilvNode *SEQUENCE;
    LilvNode *BUFTYPE;
    LilvNode *SUPPORTS;
    LilvNode *MIDI_EVENT;

    //std::list<std::string> plugin_list;

    void dump_features(const LilvPlugin *plugin);
    void dump_port_info(const LilvPlugin *plugin);

    protected:
        const LilvPlugin *get_plugin(std::string plugin_uri);

    public:
        LilvHost();
        ~LilvHost();

        std::vector<std::string> get_plugins();
};

} // namespace godot

#endif
