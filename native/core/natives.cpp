#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>

#include "host.h"
#include "rlog.h"

namespace mc {
namespace {

Host& H(lua_State* L) { return *hostOf(L); }

int ret0(lua_State*) { return 0; }
int retTrue(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int retFalse(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int retEmptyStr(lua_State* L) { lua_pushstring(L, ""); return 1; }
int retPackage(lua_State* L) { lua_pushstring(L, "com.emti.mangaclash"); return 1; }
int retStr2(lua_State* L) { lua_pushstring(L, "2"); return 1; }
int retDeviceId(lua_State* L) { lua_pushstring(L, "ios-mangaclash"); return 1; }
int retAppVersion(lua_State* L) { lua_pushstring(L, "2.20.141113"); return 1; }
int retIosVersion(lua_State* L) { lua_pushstring(L, "17"); return 1; }
int retModel(lua_State* L) { lua_pushstring(L, "iPhone"); return 1; }
int retOne(lua_State* L) { lua_pushnumber(L, 1); return 1; }
int retSdkName(lua_State* L) { lua_pushstring(L, "apple"); return 1; }
int nowMsEpoch(lua_State* L) {
    using namespace std::chrono;
    lua_pushnumber(L, static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()));
    return 1;
}
int nowSecEpoch(lua_State* L) {
    using namespace std::chrono;
    lua_pushnumber(L, static_cast<double>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count()));
    return 1;
}

// ---- GameLib.ResManager (methods use ':' so arg 1 is self)
int resGetSystemType(lua_State* L) { lua_pushstring(L, H(L).platform.c_str()); return 1; }
int resGetSetting(lua_State* L) {
    auto& s = H(L).settings();
    auto it = s.find(luaL_checkstring(L, 2));
    lua_pushstring(L, it == s.end() ? "" : it->second.c_str());
    return 1;
}
int resSetSetting(lua_State* L) {
    std::string& slot = H(L).settings()[luaL_checkstring(L, 2)];
    std::string value = luaL_checkstring(L, 3);
    if (slot != value) { slot = value; H(L).saveSettings(); }
    return 0;
}

// ---- GameLib.SceneManager
float g_screenW = 960, g_screenH = 640;
int sceneGetSize(lua_State* L) {          // fills a COCOSize table {width,height} passed as arg 2
    if (lua_istable(L, 2)) {
        lua_pushnumber(L, g_screenW); lua_setfield(L, 2, "width");
        lua_pushnumber(L, g_screenH); lua_setfield(L, 2, "height");
    }
    return 0;
}
int sceneGetScale(lua_State* L) { lua_pushnumber(L, 1.0); return 1; }
int sceneGetRunningScene(lua_State* L) { lua_newtable(L); markMissingTable(L, "Scene"); return 1; }

int cocosSize(lua_State* L) {            // COCOSize([width, height])
    double w = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0, h = lua_isnumber(L, 2) ? lua_tonumber(L, 2) : 0;
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, w); lua_setfield(L, -2, "width");
    lua_pushnumber(L, h); lua_setfield(L, -2, "height");
    return 1;
}

int getCocosVersion(lua_State* L) { lua_pushnumber(L, 3.2); return 1; }
int nowMs(lua_State* L) {
    using namespace std::chrono;
    lua_pushnumber(L, duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    return 1;
}

int sufCompress(lua_State* L) { lua_pushstring(L, ".sdz"); return 1; }
int sufMusic(lua_State* L) { lua_pushstring(L, ".sdzm"); return 1; }
int sufSound(lua_State* L) { lua_pushstring(L, ".sdzs"); return 1; }
int sufSkeleton(lua_State* L) { lua_pushstring(L, ".ssdz"); return 1; }

uint32_t g_seed = 12345;

// RFC 1321 MD5 -> lowercase hex (GameLib.Md5, used for the password "sn" sent to the platform server)
struct Md5Ctx { uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476; };
void md5Block(Md5Ctx& x, const unsigned char* p) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int R[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                              4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) m[i] = p[i*4] | (p[i*4+1] << 8) | (p[i*4+2] << 16) | (static_cast<uint32_t>(p[i*4+3]) << 24);
    uint32_t a = x.a, b = x.b, c = x.c, d = x.d;
    for (int i = 0; i < 64; ++i) {
        uint32_t f; int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3*i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7*i) & 15; }
        uint32_t t = d; d = c; c = b;
        uint32_t v = a + f + K[i] + m[g];
        b = b + ((v << R[i]) | (v >> (32 - R[i])));
        a = t;
    }
    x.a += a; x.b += b; x.c += c; x.d += d;
}
int gameMd5(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_optlstring(L, 1, "", &n);
    Md5Ctx x;
    std::string msg(s, n);
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back(0);
    uint64_t bits = static_cast<uint64_t>(n) * 8;
    for (int i = 0; i < 8; ++i) msg.push_back(static_cast<char>((bits >> (8 * i)) & 0xff));
    for (size_t o = 0; o < msg.size(); o += 64) md5Block(x, reinterpret_cast<const unsigned char*>(msg.data()) + o);
    char out[33];
    uint32_t w[4] = {x.a, x.b, x.c, x.d};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) std::snprintf(out + i*8 + j*2, 3, "%02x", (w[i] >> (8*j)) & 0xff);
    lua_pushstring(L, out);
    return 1;
}


// ---- batch of small natives (text length, url encode, push/voice stubs)
std::vector<std::string> utf8Chars(const std::string& s) {
    std::vector<std::string> v;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        v.push_back(s.substr(i, n));
        i += n;
    }
    return v;
}
int scLength(lua_State* L) { lua_pushnumber(L, static_cast<lua_Number>(utf8Chars(luaL_optstring(L, 1, "")).size())); return 1; }
int scAt(lua_State* L) {                                  // unicode_at(str, index0) -> code point
    auto v = utf8Chars(luaL_optstring(L, 1, ""));
    size_t i = static_cast<size_t>(luaL_optnumber(L, 2, 0));
    uint32_t cp = 0;
    if (i < v.size()) {
        const std::string& c = v[i];
        unsigned char b = static_cast<unsigned char>(c[0]);
        if (c.size() == 1) cp = b;
        else if (c.size() == 2) cp = ((b & 0x1f) << 6) | (c[1] & 0x3f);
        else if (c.size() == 3) cp = ((b & 0x0f) << 12) | ((c[1] & 0x3f) << 6) | (c[2] & 0x3f);
        else cp = ((b & 0x07) << 18) | ((c[1] & 0x3f) << 12) | ((c[2] & 0x3f) << 6) | (c[3] & 0x3f);
    }
    lua_pushnumber(L, cp);
    return 1;
}
int scSub(lua_State* L) {                                 // unicode_sub(str, start, end): characters [start, end), 0-based
    auto v = utf8Chars(luaL_optstring(L, 1, ""));
    double from = luaL_optnumber(L, 2, 0), to = luaL_optnumber(L, 3, static_cast<double>(v.size()));
    size_t a = from > 0 ? static_cast<size_t>(from) : 0, b = to > 0 ? static_cast<size_t>(to) : 0;
    std::string out;
    for (size_t i = a; i < v.size() && i < b; ++i) out += v[i];
    lua_pushstring(L, out.c_str());
    return 1;
}
int gameUrlEncode(lua_State* L) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : std::string(luaL_optstring(L, 1, ""))) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    lua_pushstring(L, o.c_str());
    return 1;
}
int retArg2(lua_State* L) { lua_pushvalue(L, 2); return 1; }          // method call: self, value -> value
int retLang(lua_State* L) { lua_pushstring(L, "vn"); return 1; }

// add functions to an existing global class table (creating it when absent)
void ext(lua_State* L, const char* cls, std::initializer_list<std::pair<const char*, lua_CFunction>> fns) {
    lua_getglobal(L, cls);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); markMissingTable(L, cls); lua_pushvalue(L, -1); lua_setglobal(L, cls); }
    for (auto& p : fns) { lua_pushcfunction(L, p.second); lua_setfield(L, -2, p.first); }
    lua_pop(L, 1);
}

int gameRand(lua_State* L) {              // Rand(n) -> [1,n]; Rand(a,b) -> [a,b]; Rand() -> integer >= 0
    g_seed = g_seed * 1664525u + 1013904223u;
    double r = (g_seed >> 8) / 16777216.0;
    int n = lua_gettop(L);
    if (n == 0) lua_pushnumber(L, g_seed >> 1);           // like C rand(): scripts do math.mod(GameLib.Rand(), n) + 1
    else if (n == 1) lua_pushnumber(L, 1 + static_cast<int>(r * luaL_checknumber(L, 1)));
    else { double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2); lua_pushnumber(L, a + static_cast<int>(r * (b - a + 1))); }
    return 1;
}

int cocosLog(lua_State* L) {
    const char* m = lua_tostring(L, 1);
    rlog("[lua] %s", m ? m : "");
    return 0;
}

void fn(lua_State* L, const char* name, lua_CFunction f) { lua_pushcfunction(L, f); lua_setfield(L, -2, name); }

void sub(lua_State* L, const char* name, std::initializer_list<std::pair<const char*, lua_CFunction>> fns) {
    lua_newtable(L);
    for (auto& p : fns) fn(L, p.first, p.second);
    markMissingTable(L, name);
    lua_setfield(L, -2, name);
}

}  // namespace

void setScreenSize(float w, float h) { g_screenW = w; g_screenH = h; }

void registerCore(Host& h) {
    lua_State* L = h.L();
    lua_newtable(L);                                     // GameLib
    sub(L, "ResManager", {{"GetSystemType", resGetSystemType}, {"GetSetting", resGetSetting}, {"SetSetting", resSetSetting},
                          {"SetPlatAndResURL", ret0}, {"SetGameURL", ret0}, {"GetUpdateFileList", ret0}, {"UpdateFileList", ret0}, {"CancelUpdateFileList", ret0}});
    sub(L, "SceneManager", {{"GetSize", sceneGetSize}, {"GetScaleSize", sceneGetScale}, {"GetRunningScene", sceneGetRunningScene},
                            {"SetGameFrame", ret0}, {"SetKeypadEnabled", ret0}, {"SetLuaBackKey", ret0}, {"SetLuaTouchDown", ret0}});
    sub(L, "FontManager", {{"registerFilterChar", ret0}, {"registerFont", ret0}});
    sub(L, "SoundManager", {{"SetBGMusicVolume", ret0}, {"StopBGMusic", ret0}, {"UnloadEffect", ret0}, {"StopEffect", ret0},
                            {"SetEffectsVolume", ret0}, {"PreloadEffect", ret0}, {"PlayEffect", ret0}, {"PlayBGMusic", ret0},
                            {"GetBGMusicVolume", retOne}});
    lua_getfield(L, -1, "ResHelper");
    lua_pop(L, 1);
    sub(L, "SystemTools", {{"GetIdentifierForAdvertisers", retEmptyStr}, {"GetPackageName", retPackage}, {"IsApptorePackage", retFalse},
                           {"GetDeviceId", retDeviceId}, {"GetMacAddress", retEmptyStr}, {"GetAppVersion", retAppVersion},
                           {"GetIosVersion", retIosVersion}, {"GetDeviceModel", retModel}, {"GetAppPath", retEmptyStr},
                           {"SupportParticle", retFalse}, {"IsSupport", retFalse}, {"GetMoneySymbol", retEmptyStr}});
    sub(L, "SdkTools", {{"GetSdkName", retSdkName}, {"IsInited", retTrue}, {"IsThirdAccountPlat", retFalse}, {"SetLuaPayCallback", ret0},
                        {"SetLuaCommonHandleCallback", ret0}, {"SetLuaMemoryWarnCallback", ret0},
                        {"GetSdkVersion", retStr2}, {"IsLimitPackage", retFalse}, {"GetChannelId", retEmptyStr}, {"IsSelfLoginSystem", retTrue},
                        {"IsYingYongBao", retFalse}, {"GetCampuuidInfo", retEmptyStr}, {"GetPhpUserInfoUrl", retEmptyStr},
                        {"GetUserCenterStr", retEmptyStr}, {"GetLogoImagePath", retEmptyStr}, {"GetLoginImagePath", retEmptyStr}, {"SetLuaLoginCallback", ret0}, {"ReportOpenChargePanel", ret0}, {"CommonHandle", ret0}, {"ReSaveSdkCallbackInfo", ret0}});
    sub(L, "GameEngine", {{"GetNowTime", nowMsEpoch}, {"GetTimeTicket", nowSecEpoch}, {"CloseApp", ret0}});
    sub(L, "MsgPush", {{"StartMsgPush", ret0}, {"SetTimerTask", ret0}, {"CancelTimerTask", ret0}, {"DelegateSocket", ret0}, {"SetAlias", ret0}});
    sub(L, "FaceConvHelper", {{"registerFace", ret0}, {"convertToFaceStr", retArg2}});
    for (const char* t : {"NodeUnity", "ActionManager", "UIListViewEx", "UIPageViewEx",
                          "UIScrollViewEx", "ExtraComponet", "MessageMem"})
        sub(L, t, {});
    sub(L, "ResHelper", {{"GetCompressSuffix", sufCompress}, {"GetMusicSuffix", sufMusic}, {"GetSoundSuffix", sufSound},
                         {"GetSkeletonSuffix", sufSkeleton}});
    for (const char* cls : {
#include "gamelib_classes.inc"
         }) {
        lua_getfield(L, -1, cls);
        bool have = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (have) continue;
        lua_newtable(L);
        markMissingTable(L, cls);
        lua_setfield(L, -2, cls);
    }
    fn(L, "GetCocosVersion", getCocosVersion);
    fn(L, "Rand", gameRand);
    fn(L, "Md5", gameMd5);
    fn(L, "UrlEncode", gameUrlEncode);
    fn(L, "COCOLog", cocosLog);
    fillResManager(L);
    lua_setglobal(L, "GameLib");
    for (const char* cls : {
#include "native_classes.inc"
         }) {
        lua_getglobal(L, cls);
        bool have = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (have) continue;
        lua_newtable(L);
        markMissingTable(L, cls);
        lua_setglobal(L, cls);
    }
    ext(L, "StringConvHelper", {{"unicode_length", scLength}, {"unicode_at", scAt}, {"unicode_sub", scSub}});
    ext(L, "MsgPush", {{"SetTimerTask", ret0}, {"CancelTimerTask", ret0}, {"DelegateSocket", ret0}, {"SetAlias", ret0}, {"StartMsgPush", ret0}});
    ext(L, "FaceConvHelper", {{"convertToFaceStr", retArg2}, {"registerFace", ret0}});
    ext(L, "VoiceManager", {{"SaveMsgAsFile", ret0}, {"StartRecord", ret0}, {"StopRecord", ret0}, {"StartPlay", ret0}, {"StopPlay", ret0},
                            {"DeleteFileByVoiceId", ret0}, {"ReleaseVoice", ret0}});
    ext(L, "SkeletonDataCahce", {{"AddSsdzSkeletonDataAsync", ret0}, {"AddSkeletonDataAsync", ret0}, {"LoadSsdzSkeletonData", ret0},
                                 {"IsSkeletonDataInCache", retFalse}, {"IsSSdzDataInCache", retFalse}});
    ext(L, "RichText", {{"SetNewlineChar", ret0}});
    ext(L, "FontLabel", {{"SetStrokeOpt", ret0}});
    ext(L, "EngineAdapter", {{"GetLanguage", retLang}});
    ext(L, "Location", {{"StartLocation", ret0}, {"StopLocation", ret0}});
    ext(L, "UIScrollViewEx", {});
    lua_getglobal(L, "UIScrollViewEx");                    // constants (cocos ui::ScrollView::Direction: VERTICAL=1, HORIZONTAL=2)
    lua_pushnumber(L, 1); lua_setfield(L, -2, "ScrollDirVertical");
    lua_pushnumber(L, 2); lua_setfield(L, -2, "ScrollDirHorizontal");
    lua_pop(L, 1);
    lua_newtable(L);
    lua_setglobal(L, "GameConfig");
    lua_pushcfunction(L, cocosSize);
    lua_setglobal(L, "COCOSize");
}

}  // namespace mc
