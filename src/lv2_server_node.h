#ifndef LV2_SERVER_NODE_H
#define LV2_SERVER_NODE_H

#include <godot_cpp/classes/node.hpp>

namespace godot {

class Lv2ServerNode : public Node {
    GDCLASS(Lv2ServerNode, Node);

private:
protected:
public:
    Lv2ServerNode();
    ~Lv2ServerNode();

    void _process();

    static void _bind_methods();
};
} // namespace godot

#endif
