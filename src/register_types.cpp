#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "lilv_server.h"
#include "lilv_instance.h"
#include "lilv_server_node.h"
#include "lilv_layout.h"
#include "audio_stream_lilv.h"
#include "audio_stream_player_lilv.h"


namespace godot {

static LilvServer *lilv_server;

void initialize_godot_lilv_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ClassDB::register_class<AudioStreamLilv>();
    ClassDB::register_class<AudioStreamPlaybackLilv>();
    ClassDB::register_class<LilvLayout>();
    ClassDB::register_class<LilvServerNode>();
    ClassDB::register_class<LilvInstance>();
    ClassDB::register_class<LilvServer>();

    lilv_server = memnew(LilvServer);
    Engine::get_singleton()->register_singleton("LilvServer", LilvServer::get_singleton());

}

void uninitialize_godot_lilv_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    Engine::get_singleton()->unregister_singleton("LilvServer");
    lilv_server->finish();
    memdelete(lilv_server);
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT godot_lilv_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                      const GDExtensionClassLibraryPtr p_library,
                                                      GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_godot_lilv_module);
    init_obj.register_terminator(uninitialize_godot_lilv_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
}
