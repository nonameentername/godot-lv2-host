#ifndef LILV_SERVER_NODE_H
#define LILV_SERVER_NODE_H

#include <godot_cpp/classes/node.hpp>

namespace godot {

class LilvServerNode : public Node {
    GDCLASS(LilvServerNode, Node);

private:
protected:
public:
    LilvServerNode();
    ~LilvServerNode();

    void _process();

    static void _bind_methods();
};
} // namespace godot

#endif
