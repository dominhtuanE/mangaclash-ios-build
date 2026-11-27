#include "rich_text.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "common.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

// bytes of the UTF-8 character starting at s[i]
size_t charLength(const std::string& s, size_t i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    return std::min(n, s.size() - i);
}

// position just after "KEY=" (and an optional "{" / "0x"), or npos
size_t valueOf(const std::string& tag, const char* key) {
    std::string k = std::string(key) + "=";
    size_t p = tag.find(k);
    if (p == std::string::npos) return p;
    p += k.size();
    if (p < tag.size() && tag[p] == '{') ++p;
    if (tag.compare(p, 2, "0x") == 0 || tag.compare(p, 2, "0X") == 0) p += 2;
    return p;
}

float number(const std::string& tag, const char* key, float def) {
    size_t p = valueOf(tag, key);
    return p == std::string::npos ? def : static_cast<float>(std::atof(tag.c_str() + p));
}

void readColor(const std::string& tag, const char* key, Color4B& out) {
    size_t p = valueOf(tag, key);
    if (p == std::string::npos || p + 8 > tag.size()) return;
    unsigned long v = std::strtoul(tag.substr(p, 8).c_str(), nullptr, 16);
    out = Color4B((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255);
}

bool isTag(const std::string& body) {
    static const char* names[] = {"COLOR", "/COLOR", "FONT", "/FONT", "BR", "FACEIMG", "LINK", "/LINK", "LOCATEX", "LINESPACE"};
    size_t end = body.find_first_of(" =/", body.empty() || body[0] != '/' ? 0 : 1);
    std::string name = body.substr(0, end);
    for (const char* n : names) if (name == n) return true;
    return false;
}

}  // namespace

RichNode* RichNode::create(const std::string& font, float width, float fontSize) {
    auto* n = new RichNode();
    n->init();
    n->font_ = font;
    n->width_ = width;
    n->size_ = fontSize;
    n->setAnchorPoint(Vec2(0.5f, 0.5f));
    n->setCascadeColorEnabled(true);
    n->setCascadeOpacityEnabled(true);
    n->autorelease();
    return n;
}

Label* RichNode::label(const std::string& s, float size) {
    Label* l = Label::createWithTTF(s, font_, size);
    return l ? l : Label::createWithSystemFont(s, "Arial", size);
}

// width of one character, measured once per size (labels are rebuilt often: the story text is typed letter by letter)
float RichNode::advance(const std::string& ch, float size) {
    static std::map<std::pair<std::string, int>, float> cache;
    auto key = std::make_pair(ch, static_cast<int>(size * 4));
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    float w = ch == " " ? label("a a", size)->getContentSize().width - label("aa", size)->getContentSize().width
                        : label(ch, size)->getContentSize().width;
    cache[key] = w;
    return w;
}

void RichNode::newLine(std::vector<Line>& lines, float gap) {
    lines.back().gap = gap;
    lines.push_back(Line());
}

void RichNode::addText(std::vector<Line>& lines, const std::string& s, const Style& style) {
    std::string run;
    float runWidth = 0;
    auto flush = [&]() {
        if (run.empty()) return;
        Label* l = label(run, style.size);
        l->setTextColor(style.color);
        if (style.outlineWidth > 0) l->enableOutline(style.outline, static_cast<int>(style.outlineWidth));
        Piece p;
        p.node = l;
        p.x = lines.back().width;
        p.width = l->getContentSize().width;
        p.height = l->getContentSize().height;
        lines.back().width += p.width;
        lines.back().height = std::max(lines.back().height, p.height);
        lines.back().pieces.push_back(p);
        run.clear();
        runWidth = 0;
    };
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\n') { flush(); newLine(lines, 0); ++i; continue; }
        // next word, with the spaces that follow it
        size_t j = i;
        float word = 0;
        while (j < s.size() && s[j] != ' ' && s[j] != '\n') { size_t n = charLength(s, j); word += advance(s.substr(j, n), style.size); j += n; }
        float spaces = 0;
        size_t k = j;
        while (k < s.size() && s[k] == ' ') { spaces += advance(" ", style.size); ++k; }
        if (width_ > 0 && lines.back().width + runWidth + word > width_ + 0.5f) {
            if (lines.back().width + runWidth > 0) { flush(); newLine(lines, 0); }
            if (word > width_) {                              // longer than a whole line: break between characters
                while (i < j) {
                    size_t n = charLength(s, i);
                    float w = advance(s.substr(i, n), style.size);
                    if (runWidth > 0 && runWidth + w > width_) { flush(); newLine(lines, 0); }
                    run += s.substr(i, n);
                    runWidth += w;
                    i += n;
                }
                run += s.substr(j, k - j);
                runWidth += spaces;
                i = k;
                continue;
            }
        }
        run += s.substr(i, k - i);
        runWidth += word + spaces;
        i = k;
    }
    flush();
}

void RichNode::addImage(std::vector<Line>& lines, const std::string& tag) {
    size_t p = tag.find("IMG=", 4);                          // the first "IMG=" after the tag name
    if (p == std::string::npos) return;
    p += 4;
    std::string path = tag.substr(p, tag.find_first_of(" ,", p) - p);
    Texture2D* tex = textureFor(path);
    if (!tex) return;
    Rect r(Vec2::ZERO, tex->getContentSize());
    size_t t = valueOf(tag, "TEXRECT");
    float x, y, w, h;
    if (t != std::string::npos && std::sscanf(tag.c_str() + t, "%f,%f,%f,%f", &x, &y, &w, &h) == 4 && w > 0 && h > 0) r = Rect(x, y, w, h);
    float scale = number(tag, "SCALE", 1);
    Sprite* s = Sprite::createWithTexture(tex, r);
    s->setScale(scale);
    Piece piece;
    piece.node = s;
    piece.width = r.size.width * scale;
    piece.height = r.size.height * scale;
    if (width_ > 0 && lines.back().width > 0 && lines.back().width + piece.width > width_) newLine(lines, 0);
    piece.x = lines.back().width;
    lines.back().width += piece.width;
    lines.back().height = std::max(lines.back().height, piece.height);
    lines.back().pieces.push_back(piece);
}

void RichNode::layout() {
    for (Node* c : Vector<Node*>(getChildren())) if (c != bubble_) removeChild(c);

    Style base;
    base.color = color_;
    base.outline = strokeColor_;
    base.outlineWidth = stroke_ ? 2 : 0;
    base.size = size_;
    std::vector<Style> stack(1, base);
    std::vector<Line> lines(1);
    std::string plain;
    auto emit = [&]() { if (!plain.empty()) { addText(lines, plain, stack.back()); plain.clear(); } };

    for (size_t i = 0; i < text_.size();) {
        size_t e = text_[i] == '<' ? text_.find('>', i) : std::string::npos;
        std::string tag = e == std::string::npos ? std::string() : text_.substr(i + 1, e - i - 1);
        if (tag.empty() || !isTag(tag)) { plain += text_[i++]; continue; }
        emit();
        i = e + 1;
        if (tag.compare(0, 5, "COLOR") == 0) {
            Style s = stack.back();
            readColor(tag, "VALUE", s.color);
            if (tag.find("OUTLINE=") != std::string::npos) {
                readColor(tag, "OUTLINE", s.outline);
                s.outlineWidth = number(tag, "OUTLINE_W", 2);
            }
            stack.push_back(s);
        } else if (tag.compare(0, 4, "FONT") == 0) {
            Style s = stack.back();
            s.size = number(tag, "SIZE", s.size);
            stack.push_back(s);
        } else if (tag == "/COLOR" || tag == "/FONT") {
            if (stack.size() > 1) stack.pop_back();
        } else if (tag.compare(0, 2, "BR") == 0) {
            if (lines.back().height <= 0) lines.back().height = label("Ag", stack.back().size)->getContentSize().height;
            newLine(lines, number(tag, "SPACING", 0));
        } else if (tag.compare(0, 9, "LINESPACE") == 0) {
            spacing_ = number(tag, "VALUE", spacing_);
        } else if (tag.compare(0, 7, "FACEIMG") == 0) {
            addImage(lines, tag);
        }
    }
    emit();
    while (lines.size() > 1 && lines.back().pieces.empty()) lines.pop_back();

    float width = 0, height = 0;
    for (size_t n = 0; n < lines.size(); ++n) {
        width = std::max(width, lines[n].width);
        height += lines[n].height + (n + 1 < lines.size() ? spacing_ + lines[n].gap : 0);
    }
    if (width_ > 0) width = width_;
    setContentSize(Size(width, height));
    float top = height;
    for (const Line& line : lines) {
        for (const Piece& p : line.pieces) {
            p.node->setAnchorPoint(Vec2(0, 0.5f));
            p.node->setPosition(p.x, top - line.height / 2);
            addChild(p.node, 1);
        }
        top -= line.height + spacing_ + line.gap;
    }
    placeBubble();
}

void RichNode::setBubble(Texture2D* tex, const Rect& source, const Rect& inner, const Rect& box) {
    if (!bubble_) {
        bubble_ = NinePatch::create();
        bubble_->setAnchorPoint(Vec2::ZERO);
        addChild(bubble_, 0);
    }
    bubble_->setSource(tex, source);
    bubble_->setInner(inner);
    bubbleBox_ = box;
    placeBubble();
}

void RichNode::placeBubble() {
    if (!bubble_) return;
    Size s = getContentSize();
    bubble_->setVisible(!text_.empty());
    bubble_->setPosition(bubbleBox_.origin);
    bubble_->stretch(std::max(s.width + bubbleBox_.size.width, 1.0f), std::max(s.height + bubbleBox_.size.height, 1.0f));
}

}  // namespace ui
}  // namespace mc
