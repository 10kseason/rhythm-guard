#include "../src/tab_filter.h"
#include <cstdio>
#include <cstdlib>

int checks = 0;
void Expect(bool actual, bool expected, const char* reason) {
    ++checks;
    if (actual != expected) {
        std::fprintf(stderr, "FAIL: %s\n", reason);
        std::exit(1);
    }
}

int main() {
    TabFilter filter;
    Expect(filter.Suppress(true, false), false, "ordinary Tab down passes");
    Expect(filter.Suppress(true, false), false, "ordinary Tab repeat passes");
    Expect(filter.Suppress(false, false), false, "ordinary Tab up passes");
    Expect(filter.Suppress(true, true), true, "Alt+Tab down is blocked");
    Expect(filter.Suppress(true, true), true, "Alt+Tab repeat is blocked");
    Expect(filter.Suppress(false, true), true, "Alt+Tab up is blocked");
    Expect(filter.Suppress(true, true), true, "new blocked press");
    Expect(filter.Suppress(true, false), true, "held Tab cannot leak after releasing Alt");
    Expect(filter.Suppress(false, false), true, "release Alt before Tab still blocks up");
    Expect(filter.Suppress(true, false), false, "next normal press recovers");
    Expect(filter.Suppress(true, true), true, "Alt added while Tab held blocks repeat");
    Expect(filter.Suppress(false, true), false, "forwarded down must have matching up");
    filter.Reset(true);
    Expect(filter.Suppress(true, true), true, "Tab held before enabling: Alt repeat blocked");
    Expect(filter.Suppress(false, true), false, "Tab held before enabling: release passes");
    Expect(filter.Suppress(false, true), false, "unmatched up never swallowed");
    Expect(filter.Suppress(true, true), true, "fresh chord after unmatched up blocked");
    filter.Reset();
    Expect(filter.Suppress(true, false), false, "reset does not retain suppression");
    Expect(filter.Suppress(false, false), false, "reset releases normal press");
    std::printf("PASS: %d key-sequence checks\n", checks);
}
