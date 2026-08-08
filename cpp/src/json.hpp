#pragma once
#include "common.hpp"

// ------------------------------- JSON --------------------------------
// A tiny JSON value + parser + string escaper, shared by the language
// server (lsp.hpp) and the debug adapter (dap.hpp). Both protocols are
// Content-Length-framed JSON over stdio.
struct Json {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false; double num = 0; std::string s;
    std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
    const Json* get(const std::string& k) const { if (t != OBJ) return nullptr; for (auto& p : o) if (p.first == k) return &p.second; return nullptr; }
    std::string str(const std::string& d = "") const { return t == STR ? s : d; }
    double n(double d = 0) const { return t == NUM ? num : d; }
    const Json* at(size_t i) const { return (t == ARR && i < a.size()) ? &a[i] : nullptr; }
};
struct JParse {
    const std::string& src; size_t i = 0;
    JParse(const std::string& s) : src(s) {}
    void ws() { while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) i++; }
    Json parse() { return val(); }
    Json val() {
        ws(); if (i >= src.size()) return {};
        char c = src[i];
        if (c == '{') return objp(); if (c == '[') return arrp(); if (c == '"') return strp();
        if (c == 't' || c == 'f') return boolp(); if (c == 'n') { i += 4; return {}; }
        return nump();
    }
    Json strp() {
        Json j; j.t = Json::STR; i++;                     // opening quote
        while (i < src.size() && src[i] != '"') {
            char c = src[i++];
            if (c == '\\' && i < src.size()) {
                char e = src[i++];
                switch (e) { case 'n': j.s += '\n'; break; case 't': j.s += '\t'; break; case 'r': j.s += '\r'; break;
                    case 'b': j.s += '\b'; break; case 'f': j.s += '\f'; break; case '/': j.s += '/'; break;
                    case '"': j.s += '"'; break; case '\\': j.s += '\\'; break;
                    case 'u': { if (i + 4 <= src.size()) { int cp = (int)strtol(src.substr(i, 4).c_str(), nullptr, 16); i += 4;
                        if (cp < 0x80) j.s += (char)cp; else if (cp < 0x800) { j.s += (char)(0xC0 | (cp >> 6)); j.s += (char)(0x80 | (cp & 0x3F)); }
                        else { j.s += (char)(0xE0 | (cp >> 12)); j.s += (char)(0x80 | ((cp >> 6) & 0x3F)); j.s += (char)(0x80 | (cp & 0x3F)); } } break; }
                    default: j.s += e; }
            } else j.s += c;
        }
        if (i < src.size()) i++;                          // closing quote
        return j;
    }
    Json nump() { size_t st = i; while (i < src.size() && (isdigit((unsigned char)src[i]) || src[i] == '-' || src[i] == '+' || src[i] == '.' || src[i] == 'e' || src[i] == 'E')) i++; Json j; j.t = Json::NUM; j.num = strtod(src.substr(st, i - st).c_str(), nullptr); return j; }
    Json boolp() { Json j; j.t = Json::BOOL; if (src[i] == 't') { j.b = true; i += 4; } else { j.b = false; i += 5; } return j; }
    Json arrp() { Json j; j.t = Json::ARR; i++; ws(); if (i < src.size() && src[i] == ']') { i++; return j; } while (i < src.size()) { j.a.push_back(val()); ws(); if (i < src.size() && src[i] == ',') { i++; continue; } break; } ws(); if (i < src.size() && src[i] == ']') i++; return j; }
    Json objp() { Json j; j.t = Json::OBJ; i++; ws(); if (i < src.size() && src[i] == '}') { i++; return j; } while (i < src.size()) { ws(); Json k = strp(); ws(); if (i < src.size() && src[i] == ':') i++; Json v = val(); j.o.emplace_back(k.s, std::move(v)); ws(); if (i < src.size() && src[i] == ',') { i++; continue; } break; } ws(); if (i < src.size() && src[i] == '}') i++; return j; }
};
static std::string jesc(const std::string& s) {
    std::string r; for (char c : s) {
        switch (c) { case '"': r += "\\\""; break; case '\\': r += "\\\\"; break; case '\n': r += "\\n"; break;
            case '\t': r += "\\t"; break; case '\r': r += "\\r"; break;
            default: if ((unsigned char)c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); r += buf; } else r += c; }
    }
    return r;
}
static std::string jstr(const std::string& s) { return "\"" + jesc(s) + "\""; }
