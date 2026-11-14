#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include "lab_model.hpp"

using namespace opxy;
using namespace opxy::platform::lab;
void require(bool condition, const char* message) {
    if (!condition) { std::puts(message); std::exit(1); }
}
std::set<std::string> taken;
bool exists(const char* stem) { return taken.count(stem) != 0; }

int main() {
    Commit c{};
    char line[48];
    c.begin(2);
    require(c.open && c.source == LastNote && c.track == 2 && !c.post_master && !c.palette, "defaults: last note, pre master, T7 only");
    c.controls(line);
    require(!std::strcmp(line, "E1 NOTE / E2 T3 / E3 PRE / E4 T7"), line);
    char popup[24];
    c.summary(popup);
    require(!std::strcmp(popup, "NOTE T3 PRE T7"), popup);
    c.turn(1, 1); c.turn(1, 1); c.turn(1, 100);
    require(c.track == 5, "E2 moves the note's track one at a time");
    c.turn(0, 1); c.turn(1, 1); c.turn(1, 1); c.turn(2, 1); c.turn(3, 1);
    c.controls(line);
    require(!std::strcmp(line, "E1 SEQ / E2 4 BARS / E3 POST / E4 T7+PALETTE"), line);
    c.summary(popup);
    require(!std::strcmp(popup, "SEQ 4BAR POST T7+PAL"), popup);
    c.turn(1, 5); c.turn(1, 5);
    require(c.bars == 3, "bars clamp at 8");
    c.cancel(); c.begin(9);
    require(c.open && c.track == 5 && c.source == Sequence, "choices stay as last set");

    Screen screen;
    Commit d{}; d.begin(0);
    describe(d, 4, "my-song-97", nullptr, screen);
    require(!std::strcmp(screen.rows[0], "E1 SOURCE: LAST NOTE") && !std::strcmp(screen.rows[1], "E2 TRACK: T1") &&
            !std::strcmp(screen.rows[2], "E3 MASTER: PRE") && !std::strcmp(screen.rows[3], "E4 TO: T7"), "rows name each encoder");
    require(!std::strcmp(screen.rows[4], "REPLAYS THE LAST NOTE PLAYED ON T1"), screen.rows[4]);
    require(!std::strcmp(screen.rows[6], "T7 ALWAYS PLAYS THE NEWEST SAMPLE"), screen.rows[6]);
    require(!std::strcmp(screen.file, "FILE vefflab/my-song-97.wav") &&
            !std::strcmp(screen.controls, "CLICK E4: COMMIT / STOP: CLOSE"), "file and controls");
    d.turn(3, 1); d.turn(0, 1); d.turn(1, 1);
    describe(d, 11, "", "RECORDING 2 BARS / STOP CANCELS", screen);
    require(!std::strcmp(screen.rows[1], "E2 LENGTH: 2 BARS") && screen.lit == 1, "the turned row is lit");
    require(!std::strcmp(screen.rows[4], "PLAYS THE PATTERN FROM ITS START FOR 2 BARS"), screen.rows[4]);
    require(!std::strcmp(screen.rows[6], "T7, AND T8 PALETTE PAD 12 (NEXT FREE)"), screen.rows[6]);
    require(!std::strcmp(screen.controls, "RECORDING 2 BARS / STOP CANCELS") && !screen.file[0], "status replaces controls");
    describe(d, -1, "", nullptr, screen);
    require(!std::strcmp(screen.rows[6], "PALETTE FULL: T7 ONLY, SAVE THE KIT FIRST"), screen.rows[6]);

    char stem[kStemCapacity];
    require(next_stem("My Song.xy", nullptr, exists, stem) && !std::strcmp(stem, "my-song-99"), stem);
    taken.insert(stem);
    require(next_stem("My Song.xy", nullptr, exists, stem) && !std::strcmp(stem, "my-song-98"), "next root counts down");
    require(next_stem("My Song.xy", "my-song-98", exists, stem) && !std::strcmp(stem, "my-song-98-99"), "a child extends its parent");
    taken.insert(stem);
    require(next_stem("My Song.xy", "my-song-98", exists, stem) && !std::strcmp(stem, "my-song-98-98"), "second child");
    require(next_stem("My Song.xy", "kick", exists, stem) && !std::strcmp(stem, "my-song-98"), "a foreign sample starts a new root");
    require(next_stem("My Song.xy", "my-song-9x", exists, stem) && !std::strcmp(stem, "my-song-98"), "malformed lineage is foreign");
    require(next_stem("averyveryverylongname.xy", nullptr, exists, stem) && !std::strcmp(stem, "averyveryver-99"), "prefix at most 12");
    require(next_stem("", nullptr, exists, stem) && !std::strcmp(stem, "lab-99"), "empty project name");
    for (u32 n = 1; n <= 99; ++n) { char s[16]; std::snprintf(s, sizeof s, "full-%02u", n); taken.insert(s); }
    require(!next_stem("full", nullptr, exists, stem), "every number taken");
    std::puts("lab model checks passed");
    return 0;
}
