#pragma once
#include <algorithm>
#include <cmath>

#include "cocos2d.h"
#include "2d/CCClippingRectangleNode.h"

namespace mc {
namespace ui {

// A GameLayer: a plain node that can clip its children to a rectangle, as the Android engine does. The rectangle is
// either in screen coordinates (SetScissorSection) or in the layer's own coordinates (SetScissorSectionByLayer).
// Without a section it draws exactly like a Node.
class LayerNode : public cocos2d::ClippingRectangleNode {
public:
    static LayerNode* create() {
        auto* n = new LayerNode();
        n->init();
        n->setClippingEnabled(false);
        n->autorelease();
        return n;
    }
    void setSection(const cocos2d::Rect& r, bool onScreen) {
        section_ = r;
        onScreen_ = onScreen;
        setClippingRegion(r);
        setClippingEnabled(true);
    }
    // true when the world point lies in the clipped-away part of this layer
    bool clipsOut(const cocos2d::Vec2& world) {
        return isClippingEnabled() && !section_.containsPoint(onScreen_ ? world : convertToNodeSpace(world));
    }
    void visit(cocos2d::Renderer* renderer, const cocos2d::Mat4& parentTransform, uint32_t parentFlags) override {
        if (!isClippingEnabled()) { cocos2d::Node::visit(renderer, parentTransform, parentFlags); return; }
        if (onScreen_) followScreen();
        cocos2d::ClippingRectangleNode::visit(renderer, parentTransform, parentFlags);
    }

private:
    // the base class takes node coordinates and multiplies the size by the accumulated scale: undo both
    void followScreen() {
        float sx = getScaleX(), sy = getScaleY();
        for (cocos2d::Node* p = getParent(); p; p = p->getParent()) { sx *= p->getScaleX(); sy *= p->getScaleY(); }
        if (sx == 0 || sy == 0) return;
        cocos2d::Vec2 origin = convertToNodeSpace(section_.origin);
        setClippingRegion(cocos2d::Rect(origin.x, origin.y, section_.size.width / std::fabs(sx), section_.size.height / std::fabs(sy)));
    }
    cocos2d::Rect section_;
    bool onScreen_ = false;
};

}  // namespace ui
}  // namespace mc
