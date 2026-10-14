// ini.h - minimal single-section INI reader that preserves comments by editing values in-place.
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <utility>

struct CfgVal {
    std::string key;
    std::string val;
    size_t vs = std::string::npos;   // value start in raw
    size_t ve = std::string::npos;   // value end in raw
};

class IniFile {
public:
    std::string path;
    std::string raw;
    std::vector<CfgVal> vals;

    bool Load(const std::string& p) {
        path = p;
        FILE* f = nullptr;
        if (fopen_s(&f, p.c_str(), "rb")) { raw.clear(); vals.clear(); return false; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        raw.resize(n > 0 ? (size_t)n : 0);
        if (n > 0) fread(&raw[0], 1, (size_t)n, f);
        fclose(f);
        return Parse();
    }

    // key=value lines -> vals, each remembering where its value lives in raw
    bool Parse() {
        vals.clear();
        size_t i = 0, N = raw.size();
        while (i < N) {
            size_t ln = i;
            while (ln < N && (raw[ln] == ' ' || raw[ln] == '\t')) ln++;
            size_t nl = ln;
            while (nl < N && raw[nl] != '\n' && raw[nl] != '\r') nl++;
            size_t eq = ln;
            while (eq < nl && raw[eq] != '=') eq++;
            if (eq < nl) {
                size_t ke = eq;
                while (ke > ln && (raw[ke - 1] == ' ' || raw[ke - 1] == '\t')) ke--;
                size_t vs = eq + 1;
                while (vs < nl && (raw[vs] == ' ' || raw[vs] == '\t')) vs++;
                size_t ve = nl;
                while (ve > vs && (raw[ve - 1] == ' ' || raw[ve - 1] == '\t' || raw[ve - 1] == '\r')) ve--;
                CfgVal c;
                c.key.assign(raw, ln, ke - ln);
                c.val.assign(raw, vs, ve - vs);
                c.vs = vs; c.ve = ve;
                vals.push_back(std::move(c));
            }
            i = nl;
            while (i < N && (raw[i] == '\n' || raw[i] == '\r')) i++;
        }
        return !vals.empty();
    }

    CfgVal* Find(const char* key) {
        for (auto& v : vals) if (_stricmp(v.key.c_str(), key) == 0) return &v;
        return nullptr;
    }
    std::string Get(const char* key, const std::string& def) {
        CfgVal* v = Find(key); return v ? v->val : def;
    }
    double GetD(const char* key, double def) { CfgVal* v = Find(key); return v ? atof(v->val.c_str()) : def; }
    int    GetI(const char* key, int def)    { CfgVal* v = Find(key); return v ? atoi(v->val.c_str()) : def; }

    // edit the value of an existing key in-place, write to disk, then re-parse so the
    // other entries' offsets stay valid after the length change.
    bool SetAndSave(const char* key, const std::string& val) {
        CfgVal* v = Find(key);
        if (!v || v->val == val) return true;
        raw.replace(v->vs, v->ve - v->vs, val);
        bool ok = Save();
        Parse();
        return ok;
    }
    bool Save() {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb")) return false;
        fwrite(raw.data(), 1, raw.size(), f);
        fclose(f);
        return true;
    }
};
