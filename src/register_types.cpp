#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "audio_stream_lv2.h"
#include "audio_stream_player_lv2.h"
#include "editor_audio_meter_notches_lv2.h"
#include "lv2_control.h"
#include "lv2_instance.h"
#include "lv2_layout.h"
#include "lv2_server.h"
#include "lv2_server_node.h"

namespace godot {

static Lv2Server *lv2_server;

void initialize_godot_lv2_host_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ClassDB::register_class<EditorAudioMeterNotchesLv2>();
    ClassDB::register_class<AudioStreamLv2>();
    ClassDB::register_class<AudioStreamPlaybackLv2>();
    ClassDB::register_class<Lv2Control>();
    ClassDB::register_class<Lv2Layout>();
    ClassDB::register_class<Lv2ServerNode>();
    ClassDB::register_class<Lv2Instance>();
    ClassDB::register_class<Lv2Server>();

    lv2_server = memnew(Lv2Server);
    Engine::get_singleton()->register_singleton("Lv2Server", Lv2Server::get_singleton());
}

void uninitialize_godot_lv2_host_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    Engine::get_singleton()->unregister_singleton("Lv2Server");
    lv2_server->finish();
    memdelete(lv2_server);
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT godot_lv2_host_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                       const GDExtensionClassLibraryPtr p_library,
                                                       GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_godot_lv2_host_module);
    init_obj.register_terminator(uninitialize_godot_lv2_host_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
} // namespace godot
