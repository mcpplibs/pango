#pragma once
// glib-mkenums, reproduced for pango.
//
// This is the SAME generator the glib fork carries, and it is copied rather
// than shared because the two forks are separate repositories — a shared file
// would be a submodule or a package, and neither is worth it for one
// text transformation. What is NOT copied is the input set: pango points
// mkenums at its own `pango_headers`.
//
// ⚠️ THE TWO PREFIX RULE, which shipped wrong once in gnome.gobject 2.82.5 and
// is the reason this file is worth reading rather than skimming:
//
//     enum_prefix    from the ENUMERATORS   (PANGO_ALIGN_)  -> the nicks
//     @ENUMPREFIX@   from the TYPE NAME     (PANGO)         -> the macro
//
// Conflating them gives `PANGO_ALIGNMENT_TYPE` where upstream has
// `PANGO_TYPE_ALIGNMENT`. Every FUNCTION name is still right, so it compiles,
// links, and passes any test that checks the function or the nick.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path g_root, g_up, g_inc;

// ⚠️ Flush stdout before writing to stderr: build.mcpp's stdout IS the
// directive protocol and mcpp reads both through one pipe.
template <class... A>
void note(const char *fmt, A... a)
{
    std::fflush(stdout);
    std::fprintf(stderr, fmt, a...);
    std::fflush(stderr);
}

inline std::string slurp(const fs::path &p)
{
    std::ifstream in(p);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// ── gobject/glib-mkenums, for the one header it is pointed at ───────────────
//
// Upstream's glib-mkenums is 816 lines of Python that scans headers for enum
// declarations, honours `/*< … >*/` trigraph annotations, and expands a
// template. This build needs it for exactly ONE input — `glib/gunicode.h`,
// which contains four enums — so what is reproduced here is the subset that
// covers those four, not the whole tool.
//
// The tool is CALLED TWICE with different templates, and the templates are read
// from the tree rather than hard-coded, so a change upstream makes to them is
// picked up.
struct enum_def {
    std::string name;          // GUnicodeType            — as written
    std::string prefix;        // G_UNICODE_             — from the ENUMERATORS, for nicks
    std::string macro_prefix;  // G                      — from the TYPE NAME, for @ENUMPREFIX@
    std::string shortname;     // UNICODE_TYPE           — @ENUMSHORT@
    std::string long_name;     // G_UNICODE_TYPE         — @ENUMNAME@
    std::string sym_name;      // g_unicode_type         — @enum_name@
    std::vector<std::string> values;
    // Per-enumerator nick, when `/*< nick=… >*/` overrides the derived one.
    // Empty means "derive it": prefix stripped, lower-cased, underscores to
    // hyphens. gio uses the override 17 times, including `nick=none` for the
    // zero value of a flags type, where the derived nick would be the whole
    // name.
    std::vector<std::string> nicks;
    bool flags = false;
};

// ⚠️ `/*< private >*/`, `/*< public >*/` and `/*< protected >*/` are GTK-DOC
// annotations for STRUCT MEMBERS, not mkenums annotations — and gio has 144 of
// them against 25 real ones. A scanner that treated every `/*< … >*/` as
// meaningful would read them, and `private` contains no `=`, so the damage
// would be quiet: an enumerator silently skipped rather than an error.
bool is_mkenums_annotation(const std::string &body)
{
    for (const char *a : {"flags", "skip", "nick", "underscore_name", "prefix", "since"})
        if (body.find(a) != std::string::npos) return true;
    return false;
}

// The text inside `/*< … >*/` on a line, or empty.
std::string annotation_of(const std::string &line)
{
    const std::size_t a = line.find("/*<");
    if (a == std::string::npos) return {};
    const std::size_t b = line.find(">*/", a);
    if (b == std::string::npos) return {};
    return line.substr(a + 3, b - a - 3);
}

// `nick=supports-uris` → `supports-uris`. Empty if the key is absent.
std::string annotation_value(const std::string &ann, const std::string &key)
{
    const std::size_t k = ann.find(key + "=");
    if (k == std::string::npos) return {};
    std::size_t s = k + key.size() + 1;
    std::size_t e = s;
    while (e < ann.size() && !std::isspace((unsigned char)ann[e]) && ann[e] != ',') ++e;
    return ann.substr(s, e - s);
}

// `GUnicodeBreakType` → prefix `G_UNICODE_BREAK`, short `TYPE`.
// mkenums derives these by splitting the CamelCase name into words, taking the
// longest prefix shared with the enumerators' common prefix. The four enums in
// gunicode.h all follow the plain rule, so the split is computed from the
// values themselves — which is what mkenums does and is why it needs no table.
// ⚠️ TWO DIFFERENT PREFIXES, and conflating them is how this generator shipped
// four wrong macro names in gnome.gobject 2.82.5.
//
//   enum_prefix       from the ENUMERATORS.  Drives the nicks.
//   enumname_prefix   from the TYPE NAME.    Drives @ENUMPREFIX@.
//
// glib-mkenums computes the second as the leading namespace segment of the
// type name — `^([A-Z][a-z]*)` of `GTlsChannelBindingType` is just `G` — so
// `@ENUMPREFIX@_TYPE_@ENUMSHORT@` renders `G_TYPE_TLS_CHANNEL_BINDING_TYPE`,
// the familiar GObject spelling.
//
// Deriving it from the enumerators instead (which is what this did) gives
// `G_TLS_CHANNEL_BINDING_TLS` and therefore
// `G_TLS_CHANNEL_BINDING_TLS_TYPE_...`. Every function name stays right, so it
// compiles, links, and passes any test that calls `g_..._get_type()` — the
// macro a consumer actually writes is the only thing that is wrong. Measured
// against a distribution's own glib-enumtypes.h:
//
//   generated here            upstream
//   G_UNICODE_TYPE_TYPE       G_TYPE_UNICODE_TYPE
//   G_NORMALIZE_TYPE_MODE     G_TYPE_NORMALIZE_MODE
//
// The rule that follows: when reproducing a generator, READ ITS ALGORITHM.
// A rule inferred from its output on four inputs fit all four and was still
// wrong.
void derive_names(enum_def &e)
{
    if (e.values.empty() || e.name.empty()) return;

    // ── the nick prefix, from the enumerators ────────────────────────────
    // Longest common prefix of the value names, then trimmed so it ends in an
    // underscore: mkenums' `re.sub(r'_[^_]*$', '_', prefix)`.
    //
    // An explicit `/*< prefix=… >*/` overrides it, upper-cased with hyphens
    // turned to underscores and a trailing underscore ensured.
    if (e.prefix.empty()) {
        std::string lcp = e.values[0];
        for (const auto &v : e.values) {
            std::size_t i = 0;
            while (i < lcp.size() && i < v.size() && lcp[i] == v[i]) ++i;
            lcp.resize(i);
        }
        const std::size_t cut = lcp.rfind('_');
        e.prefix = (cut == std::string::npos) ? std::string() : lcp.substr(0, cut + 1);
    } else {
        for (auto &c : e.prefix)
            c = (c == '-') ? '_' : char(std::toupper((unsigned char)c));
        if (!e.prefix.empty() && e.prefix.back() != '_') e.prefix += '_';
    }

    // ── the macro prefix and short name, from the type name ──────────────
    // enspace = ^([A-Z][a-z]*)  →  "G" for GTlsChannelBindingType,
    //                              "G" for GIOStream (the [a-z]* matches none)
    std::size_t i = 0;
    std::string enspace;
    if (i < e.name.size() && std::isupper((unsigned char)e.name[i])) enspace += e.name[i++];
    while (i < e.name.size() && std::islower((unsigned char)e.name[i])) enspace += e.name[i++];

    std::string rest = e.name.substr(enspace.size());

    // ([^A-Z])([A-Z]) → \1_\2  :  "TlsChannelBindingType" → "Tls_Channel_Binding_Type"
    std::string s1;
    for (std::size_t k = 0; k < rest.size(); ++k) {
        if (k > 0 && std::isupper((unsigned char)rest[k])
            && !std::isupper((unsigned char)rest[k - 1]))
            s1 += '_';
        s1 += rest[k];
    }
    // ([A-Z][A-Z])([A-Z][0-9a-z]) → \1_\2  :  "IOStream" → "IO_Stream"
    std::string s2;
    for (std::size_t k = 0; k < s1.size(); ++k) {
        if (k >= 2 && k + 1 < s1.size()
            && std::isupper((unsigned char)s1[k - 2]) && std::isupper((unsigned char)s1[k - 1])
            && std::isupper((unsigned char)s1[k])
            && (std::islower((unsigned char)s1[k + 1]) || std::isdigit((unsigned char)s1[k + 1])))
            s2 += '_';
        s2 += s1[k];
    }
    for (auto &c : s2) c = char(std::toupper((unsigned char)c));

    e.shortname       = s2;
    e.macro_prefix    = enspace;
    for (auto &c : e.macro_prefix) c = char(std::toupper((unsigned char)c));
    e.long_name       = e.macro_prefix + "_" + e.shortname;
    e.sym_name        = e.long_name;
    for (auto &c : e.sym_name) c = char(std::tolower((unsigned char)c));
}

std::vector<enum_def> scan_enums(const fs::path &header)
{
    std::vector<enum_def> out;
    std::ifstream in(header);
    std::string line;
    bool inside = false;
    enum_def cur;
    while (std::getline(in, line)) {
        const std::string ann = annotation_of(line);
        if (!inside) {
            if (line.find("typedef enum") != std::string::npos) {
                inside = true;
                cur = enum_def{};
                // `/*< flags >*/` marks a flags type, which mkenums renders
                // with g_flags_register_static instead of g_enum_register_static.
                cur.flags = !ann.empty() && ann.find("flags") != std::string::npos;
                // `/*< prefix=G_APPLICATION >*/` overrides the derived prefix.
                // gio uses it once, on GApplicationFlags, where the enumerators
                // share G_APPLICATION_ but the type name would derive
                // G_APPLICATION_FLAGS.
                cur.prefix = annotation_value(ann, "prefix");
            }
            continue;
        }
        const std::size_t close = line.find('}');
        if (close != std::string::npos) {
            // ⚠️ The FIRST identifier after `}`, not everything up to the `;`.
            // gio writes `} GTlsRehandshakeMode GIO_DEPRECATED_TYPE_IN_2_60;`
            // — the deprecation decoration follows the name — and taking the
            // whole span produced a type called
            // `GTlsRehandshakeMode GIO_DEPRECATED_TYPE_IN_2_60`, which the
            // name manglers then turned into
            // `_g_i_o__d_e_p_r_e_c_a_t_e_d__t_y_p_e__i_n_2_60_get_type`.
            std::size_t s = line.find_first_not_of(" \t", close + 1);
            std::size_t e = s;
            while (e != std::string::npos && e < line.size()
                   && (std::isalnum((unsigned char)line[e]) || line[e] == '_')) ++e;
            if (s != std::string::npos && e > s) {
                cur.name = line.substr(s, e - s);
                derive_names(cur);
                if (!cur.name.empty() && !cur.values.empty()) out.push_back(cur);
            }
            inside = false;
            continue;
        }
        // An enumerator: the first identifier on the line, if the line is not a
        // comment, a preprocessor directive, or an annotation of its own.
        const std::size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        if (line[s] == '/' || line[s] == '*' || line[s] == '#') continue;
        // `/*< skip >*/` on an enumerator removes it from the registered type.
        // gio never uses it; glib's other headers do, and honouring it costs a
        // line.
        if (!ann.empty() && is_mkenums_annotation(ann)
            && ann.find("skip") != std::string::npos) continue;
        std::size_t e = s;
        while (e < line.size() && (std::isalnum((unsigned char)line[e]) || line[e] == '_')) ++e;
        if (e > s) {
            cur.values.push_back(line.substr(s, e - s));
            cur.nicks.push_back(is_mkenums_annotation(ann) ? annotation_value(ann, "nick")
                                                           : std::string());
        }
    }
    return out;
}

// Expand one of upstream's `.template` files. The format is a sequence of
// `/*** BEGIN <section> ***/ … /*** END <section> ***/` blocks; mkenums emits
// file-header once, then file-production and value-header per input file and
// per enum, then file-tail.
std::string section(const std::string &tpl, const std::string &name)
{
    const std::string b = "/*** BEGIN " + name + " ***/";
    const std::string e = "/*** END " + name + " ***/";
    const std::size_t s = tpl.find(b);
    if (s == std::string::npos) return {};
    const std::size_t bodyStart = tpl.find('\n', s);
    const std::size_t bodyEnd = tpl.find(e, bodyStart);
    if (bodyStart == std::string::npos || bodyEnd == std::string::npos) return {};
    return tpl.substr(bodyStart + 1, bodyEnd - bodyStart - 1);
}

std::string subst(std::string s, const std::string &k, const std::string &v)
{
    for (std::size_t p = s.find(k); p != std::string::npos; p = s.find(k, p + v.size()))
        s.replace(p, k.size(), v);
    return s;
}

// Expand one of upstream's `.template` files over a set of enums, grouped by
// the header each came from.
//
// Shared by glib's four enums and gio's eighty-one: the template format is the
// same (`file-header`, `file-production` per input file, `value-header` /
// `value-production` / `value-tail` per enum, `file-tail`), and the only thing
// that differs is how many there are and which directory the templates sit in.
void write_enumtypes(const fs::path &tplPath, const fs::path &out,
                     const std::vector<std::pair<std::string, std::vector<enum_def>>> &per_file)
{
    const std::string tpl = slurp(tplPath);
    mcpp::rerun_if_changed(tplPath.string().c_str());

    std::ostringstream o;
    o << "/* GENERATED by build.mcpp, replacing gobject/glib-mkenums. */\n";
    o << section(tpl, "file-header");
    for (const auto &[filename, enums] : per_file) {
        o << subst(section(tpl, "file-production"), "@filename@", filename);
        for (const auto &e : enums) {
            std::string vh = section(tpl, "value-header");
            vh = subst(vh, "@enum_name@", e.sym_name);
            vh = subst(vh, "@EnumName@", e.name);
            vh = subst(vh, "@ENUMNAME@", e.long_name);
            vh = subst(vh, "@ENUMPREFIX@", e.macro_prefix);
            vh = subst(vh, "@ENUMSHORT@", e.shortname);
            vh = subst(vh, "@type@", e.flags ? "flags" : "enum");
            vh = subst(vh, "@Type@", e.flags ? "Flags" : "Enum");

            std::string body;
            for (std::size_t vi = 0; vi < e.values.size(); ++vi) {
                const std::string &v = e.values[vi];
                std::string vp = section(tpl, "value-production");
                if (vp.empty()) break;
                vp = subst(vp, "@VALUENAME@", v);
                vp = subst(vp, "@valuenick@", [&] {
                    // ⚠️ An explicit `/*< nick=… >*/` WINS. The nick is public
                    // API — g_flags_get_value_by_nick reads it — and the
                    // derived guess is wrong where upstream annotated:
                    // G_CONVERTER_NO_FLAGS would derive "no-flags" and
                    // upstream says "none".
                    if (vi < e.nicks.size() && !e.nicks[vi].empty()) return e.nicks[vi];
                    std::string n = (v.size() > e.prefix.size()
                                     && v.compare(0, e.prefix.size(), e.prefix) == 0)
                                        ? v.substr(e.prefix.size()) : v;
                    for (auto &c : n) c = (c == '_') ? '-' : char(std::tolower((unsigned char)c));
                    return n;
                }());
                vp = subst(vp, "@valuenum@", v);
                body += vp;
            }

            std::string vt = section(tpl, "value-tail");
            vt = subst(vt, "@EnumName@", e.name);
            vt = subst(vt, "@enum_name@", e.sym_name);
            vt = subst(vt, "@ENUMNAME@", e.long_name);
            vt = subst(vt, "@ENUMPREFIX@", e.macro_prefix);
            vt = subst(vt, "@ENUMSHORT@", e.shortname);
            vt = subst(vt, "@type@", e.flags ? "flags" : "enum");
            vt = subst(vt, "@Type@", e.flags ? "Flags" : "Enum");
            o << vh << body << vt;
        }
    }
    o << section(tpl, "file-tail");

    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::ofstream(out) << o.str();
}

} // namespace
