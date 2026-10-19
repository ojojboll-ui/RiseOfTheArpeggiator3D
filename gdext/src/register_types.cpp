#include "arp_game.h"
#include "band_stream.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static void initialize_arpeggiator (ModuleInitializationLevel level)
{
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    GDREGISTER_CLASS (BandStream);
    GDREGISTER_CLASS (BandPlayback);
    GDREGISTER_CLASS (ArpGame);
}

static void uninitialize_arpeggiator (ModuleInitializationLevel) {}

extern "C"
{
GDExtensionBool GDE_EXPORT arpeggiator_init (GDExtensionInterfaceGetProcAddress get_proc_address,
                                             GDExtensionClassLibraryPtr library,
                                             GDExtensionInitialization* initialization)
{
    GDExtensionBinding::InitObject init (get_proc_address, library, initialization);
    init.register_initializer (initialize_arpeggiator);
    init.register_terminator (uninitialize_arpeggiator);
    init.set_minimum_library_initialization_level (MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
