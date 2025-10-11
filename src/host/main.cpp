#include "lilv_host.h"
#include <iostream>

using namespace godot;

int main(int argc, char **argv) {

    LilvHost *lilv_host = new LilvHost();;

    /*
    for (const std::string& plugin_uri: lilv_host.get_plugins()) {
        std::cout << "Plugin found: " << plugin_uri << "\n";
    }
    */

    delete lilv_host;

    return 0;
}
