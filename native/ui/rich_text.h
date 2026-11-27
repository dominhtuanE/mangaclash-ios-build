#pragma once
#include <string>
#include <vector>

#include "cocos2d.h"

#include "nine_patch.h"

namespace mc {
namespace ui {

// The game's RichText. Markup:
//   <COLOR VALUE=AARRGGBB [OUTLINE=AARRGGBB OUTLINE_W=n]>..</COLOR>   (the value may be written 0x.. or {..})
//   <FONT SIZE=n>..</FONT>   <BR [SPACING=n]>   <LINESPACE VALUE=n>
//   <FACEIMG IMG=path TEXRECT={x,y,w,h} SCALE=f>      <LINK ..>..</LINK> (drawn as text)
// Anything else between angle brackets is ordinary text. Lines wrap at the given width (<= 0: never).
class RichNode : public cocos2d::Node {
public:
    static RichNode* create(const std::string& font, float width, float fontSize);
    void setText(const std::string& markup) { if (markup != text_) { text_ = markup; layout(); } }
    const std::string& text() const { return text_; }
    void setWidth(float width) { width_ = width; layout(); }
    void setFontSize(float size) { size_ = size; layout(); }
    void setSpacing(float spacing) { spacing_ = spacing; layout(); }
    void setColorDefault(const cocos2d::Color4B& color) { color_ = color; layout(); }
    void setStroke(bool on) { stroke_ = on; layout(); }
    void setStrokeColor(const cocos2d::Color4B& color) { strokeColor_ = color; stroke_ = true; layout(); }
    // background drawn behind the text: nine-patch of the image; box = (left, bottom, extra width, extra height)
    void setBubble(cocos2d::Texture2D* tex, const cocos2d::Rect& source, const cocos2d::Rect& inner, const cocos2d::Rect& box);

private:
    struct Style {
        cocos2d::Color4B color, outline;
        float outlineWidth = 0, size = 20;
    };
    struct Piece {                               // one run of equally styled text, or one image, on a line
        cocos2d::Node* node = nullptr;
        float x = 0, width = 0, height = 0;
    };
    struct Line {
        std::vector<Piece> pieces;
        float width = 0, height = 0, gap = 0;
    };

    void layout();
    void placeBubble();
    float advance(const std::string& ch, float size);
    cocos2d::Label* label(const std::string& s, float size);
    void addText(std::vector<Line>& lines, const std::string& s, const Style& style);
    void addImage(std::vector<Line>& lines, const std::string& tag);
    void newLine(std::vector<Line>& lines, float gap);

    std::string font_, text_;
    float width_ = -1, size_ = 20, spacing_ = 0;
    cocos2d::Color4B color_ = cocos2d::Color4B::WHITE, strokeColor_ = cocos2d::Color4B::BLACK;
    bool stroke_ = false;
    NinePatch* bubble_ = nullptr;
    cocos2d::Rect bubbleBox_;
};

}  // namespace ui
}  // namespace mc
