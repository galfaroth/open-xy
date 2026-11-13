#pragma once
// Lab by Veff (Veff fork): the commit panel's choices and the commit file
// names, with no stock calls, so the host tests cover them.
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace lab {
// Lab's fixed tracks (0-based): T7 always holds the newest commit as a
// Sampler; T8 is the palette, a Drum Sampler filled pad by pad.
const u32 kNewestTrack = 6, kPaletteTrack = 7, kPads = 24;
// Commit sources: replay the last note, or record bars of the pattern.
enum Source : u8 { LastNote = 0, Sequence = 1 };
const u8 kBars[4] = {1, 2, 4, 8};

// The commit panel (Stop + Sample): E1 source, E2 the note's track or the
// bars, E3 before or after the master, E4 turned T7 or T7 + palette, E4
// clicked starts. Choices stay as last set; the panel opens on them.
struct Commit {
    bool open;
    u8 source;       // Source
    u8 track;        // LastNote: the track the note was played on, 0..7
    u8 bars;         // Sequence: index into kBars
    bool post_master;
    bool palette;    // also onto the palette's next free pad
    void begin(u32 last_track) { open = true; if (last_track < 8) track = static_cast<u8>(last_track); }
    void cancel() { open = false; }
    void turn(u32 encoder, i32 delta) {
        if (!open || !delta) return;
        const i32 step = delta > 0 ? 1 : -1;
        switch (encoder % 4) {
        case 0: source = delta > 0 ? Sequence : LastNote; break;
        case 1:
            if (source == LastNote) {
                const i32 next = static_cast<i32>(track) + step;
                track = static_cast<u8>(next < 0 ? 0 : next > 7 ? 7 : next);
            } else {
                const i32 next = static_cast<i32>(bars) + step;
                bars = static_cast<u8>(next < 0 ? 0 : next > 3 ? 3 : next);
            }
            break;
        case 2: post_master = delta > 0; break;
        default: palette = delta > 0; break;
        }
    }
    // The panel's controls line, up to 47 characters.
    void controls(char (&out)[48]) const {
        const char* parts[4];
        char second[8] = {};
        if (source == LastNote) { second[0] = 'T'; second[1] = static_cast<char>('1' + track); }
        else {
            const u32 n = kBars[bars < 4 ? bars : 0];
            second[0] = static_cast<char>('0' + n); second[1] = ' ';
            second[2] = 'B'; second[3] = 'A'; second[4] = 'R'; second[5] = n > 1 ? 'S' : 0;
        }
        parts[0] = source == LastNote ? "E1 NOTE" : "E1 SEQ";
        parts[1] = second;
        parts[2] = post_master ? "E3 POST" : "E3 PRE";
        parts[3] = palette ? "E4 T7+PALETTE" : "E4 T7";
        u32 n = 0;
        for (u32 p = 0; p < 4; ++p) {
            if (p) { out[n++] = ' '; out[n++] = '/'; out[n++] = ' '; }
            if (p == 1) { out[n++] = 'E'; out[n++] = '2'; out[n++] = ' '; }
            for (const char* c = parts[p]; *c && n < 47; ++c) out[n++] = *c;
        }
        out[n < 48 ? n : 47] = 0;
    }
};

// A commit's file stem: the project's name (at most 12 characters, letters
// and digits lowercased, anything else a dash), then its lineage, one two-digit
// segment per generation counting down from 99 so the newest sorts first:
// "song-99", "song-98", and from the pad holding "song-98", "song-98-99".
// `parent` is the stem of the sample the commit came from when it is one of
// Lab's own (the same project prefix), else null for a new root. `exists`
// answers whether a stem is taken. False when every number is taken or the
// stem would not fit.
const u32 kStemCapacity = 64;
inline u32 project_prefix(const char* project, char* out) {
    u32 n = 0;
    for (u32 i = 0; project && project[i] && n < 12; ++i) {
        char c = project[i];
        if (c == '.') break;   // "song.xy"
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        out[n++] = keep ? c : '-';
    }
    if (!n) { const char* fallback = "lab"; while (fallback[n]) { out[n] = fallback[n]; ++n; } }
    out[n] = 0;
    return n;
}
inline bool lineage_of(const char* prefix, const char* parent) {
    if (!parent) return false;
    u32 i = 0;
    for (; prefix[i]; ++i) if (parent[i] != prefix[i]) return false;
    if (parent[i] != '-') return false;
    // Then only "NN" segments joined by dashes.
    for (++i;; i += 3) {
        const char a = parent[i], b = a ? parent[i + 1] : 0;
        if (a < '0' || a > '9' || b < '0' || b > '9') return false;
        if (!parent[i + 2]) return true;
        if (parent[i + 2] != '-') return false;
    }
}
template <class Exists>
inline bool next_stem(const char* project, const char* parent, Exists exists, char (&out)[kStemCapacity]) {
    char prefix[16];
    project_prefix(project, prefix);
    const char* base = lineage_of(prefix, parent) ? parent : prefix;
    u32 n = 0;
    while (base[n]) { if (n + 4 >= kStemCapacity) return false; out[n] = base[n]; ++n; }
    out[n] = '-';
    for (u32 number = 99; number >= 1; --number) {
        out[n + 1] = static_cast<char>('0' + number / 10);
        out[n + 2] = static_cast<char>('0' + number % 10);
        out[n + 3] = 0;
        if (!exists(static_cast<const char*>(out))) return true;
    }
    out[0] = 0;
    return false;
}
}}}
