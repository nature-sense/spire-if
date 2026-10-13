// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 NatureSense

// ft6336's **host test** — the protocol exercised on this machine, with no panel and no chip.
//
// `ft6336_bus.hpp` resolves to the fake in this directory, because the seam is included with angle
// brackets and this directory is ahead of the component's `include/` on the include path. So this
// controller's real frames go in as *bytes* and what the protocol wrote comes back out. See the fake's
// own comments for the two things it keeps.
//
// The case below is the one every stub passes: a protocol that cannot tell whether the controller is
// there says so, rather than claiming it is. **Replace it** with this controller's own cases as they
// are learned:
//
//   * the reply that proves presence — a chip id with the value the datasheet says;
//   * the register reads `read()` performs, with their expected coordinates;
//   * the report for **no finger** — a successful read with `down == false`, which is not a failure;
//   * and the malformed ones: a short reply, a corrupt one, no reply at all. Those three matter most,
//     because they are the ones that only ever happen on a bench.

#include <ft6336_bus.hpp>
#include "ft6336.hpp"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    ft6336::fake_reset();

    ft6336::Ft6336 touch(nullptr);

    // When `probe()` starts reading a real register, this becomes the case that scripts its answer:
    //
    //   ft6336::fake_answer({0x64});                       // the chip id the datasheet gives
    //   expect(touch.probe(), "probe() accepts the controller's own id");
    //   expect(ft6336::fake_written() == std::vector<uint8_t>{0xA3}, "it asked for the id");
    expect(!touch.probe(), "an unfilled probe() must not claim the controller is there");

    // And the two cases that keep "no finger" apart from "the bus failed":
    //
    //   ft6336::fake_answer({0x00, 0x00, 0x00, 0x00});     // a touch register with no touch
    //   ft6336::Touch point;
    //   expect(touch.read(point) && !point.down, "a panel with nothing on it reads true, down false");
    //
    //   ft6336::fake_reset();                              // the device stays silent
    //   expect(!touch.read(point), "an exchange that failed reads false");
    ft6336::Touch point;
    expect(!touch.read(point), "an unfilled read() must not claim a reading");

    if (failures != 0) {
        std::printf("%d case(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
