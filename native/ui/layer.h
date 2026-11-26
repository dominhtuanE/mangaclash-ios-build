#pragma once
#include "cocos2d.h"
#include "2d/CCClippingRectangleNode.h"

namespace mc {
namespace ui {

// A GameLayer: a plain node that can clip its children to a rectangle in node space (SetScissorSection), as the
// Android engine does. Without a section it draws exactly like a Node.
class LayerNode : public cocos2d::ClippingRectangleNode {
public:
    static LayerNode* create() {
        auto* n = new LayerNode();
        n->init();
        n->setClippingEnabled(false);
        n->autorelease();
        return n;
    }
    void setSection(const cocos2d::Rect& r) { setClippingRegion(r); setClippingEnabled(true); }
    // true when the world point lies in the clipped-away part of this layer
    bool clipsOut(const cocos2d::Vec2& world) {
        return isClippingEnabled() && !getClippingRegion().containsPoint(convertToNodeSpace(world));
    }
    void visit(cocos2d::Renderer* renderer, const cocos2d::Mat4& parentTransform, uint32_t parentFlags) override {
        if (isClippingEnabled()) cocos2d::ClippingRectangleNode::visit(renderer, parentTransform, parentFlags);
        else cocos2d::Node::visit(renderer, parentTransform, parentFlags);
    }
};

}  // namespace ui
}  // namespace mc
