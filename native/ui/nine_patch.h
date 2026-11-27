#pragma once
#include <algorithm>

#include "cocos2d.h"
#include "ui/UIScale9Sprite.h"

namespace mc {
namespace ui {

// Image9Label: the centre rectangle is given as fractions of the image (default 0.4, 0.4, 0.2, 0.2). Stretched to a size
// no larger than the image in both directions it shows that part of the image instead of nine slices.
class NinePatch : public cocos2d::ui::Scale9Sprite {
public:
    static NinePatch* create() {
        auto* n = new NinePatch();
        n->init();
        n->autorelease();
        return n;
    }
    void setSource(cocos2d::Texture2D* tex, const cocos2d::Rect& rect) { source_ = tex; rect_ = rect; apply(); }
    void setInner(const cocos2d::Rect& fractions) { inner_ = fractions; apply(); }
    void stretch(float w, float h) { size_ = cocos2d::Size(w, h); stretched_ = true; apply(); }
    cocos2d::Size sourceSize() const { return rect_.size; }

private:
    void apply() {
        if (!source_) { if (stretched_) setContentSize(size_); return; }
        cocos2d::Size want = stretched_ ? size_ : rect_.size;
        bool crop = stretched_ && want.width <= rect_.size.width && want.height <= rect_.size.height;
        cocos2d::Rect part = crop ? cocos2d::Rect(rect_.origin.x, rect_.origin.y, want.width, want.height) : rect_;
        setSpriteFrame(cocos2d::SpriteFrame::createWithTexture(source_.get(), part));
        setRenderingType(crop ? RenderingType::SIMPLE : RenderingType::SLICE);
        if (!crop)
            setCapInsets(cocos2d::Rect(inner_.origin.x * part.size.width, inner_.origin.y * part.size.height,
                                       inner_.size.width * part.size.width, inner_.size.height * part.size.height));
        setContentSize(want);
    }
    cocos2d::RefPtr<cocos2d::Texture2D> source_;
    cocos2d::Rect rect_, inner_ = cocos2d::Rect(0.4f, 0.4f, 0.2f, 0.2f);
    cocos2d::Size size_;
    bool stretched_ = false;
};

// Progress9Control: a bar growing from the left. Its position is its lower left corner (the anchor is ignored); the
// content size is the full length and height, the nine-patch inside is stretched to the filled part.
class Bar9 : public cocos2d::Node {
public:
    static Bar9* create(cocos2d::Texture2D* tex, const cocos2d::Rect& rect) {
        auto* n = new Bar9();
        n->init();
        n->setIgnoreAnchorPointForPosition(true);
        n->autorelease();
        n->fill_ = NinePatch::create();
        n->fill_->setAnchorPoint(cocos2d::Vec2::ZERO);
        if (tex) n->fill_->setSource(tex, rect);
        n->addChild(n->fill_);
        n->setContentSize(rect.size);
        return n;
    }
    void setContentSize(const cocos2d::Size& size) override { cocos2d::Node::setContentSize(size); refresh(); }
    void setMax(float max) { max_ = max; refresh(); }
    void setValue(float value) { value_ = value; refresh(); }
    float value() const { return value_; }

private:
    void refresh() {
        if (!fill_) return;
        float part = max_ > 0 ? std::min(std::max(value_ / max_, 0.0f), 1.0f) : 0.0f;
        fill_->setVisible(part > 0);
        if (part > 0) fill_->stretch(getContentSize().width * part, getContentSize().height);
    }
    NinePatch* fill_ = nullptr;
    float max_ = 100, value_ = 0;
};

}  // namespace ui
}  // namespace mc
