// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 NatureSense

#include "ft6336.hpp"
#include <ft6336_bus.hpp>

namespace ft6336 {

Ft6336::Ft6336(BusHandle device) : device_(device) {}

// TODO: the protocol. Every branch below is a datasheet question and none of them can be guessed:
//
//   - the register addresses, and which reads carry an argument (a register pointer) first;
//   - the bit that says a touch is present, and where in a register byte it lives;
//   - the byte order of a coordinate, and which of the two axes the controller calls x;
//   - what the chip id is, and what a short, corrupt or absent reply looks like.
//
// Talk to the controller through `bus_write`, `bus_read` and `bus_write_read` and nothing else — see
// `ft6336_bus.hpp`, which is this component's entire bus interface and the only file in it that names
// IDF. **A register read is `bus_write_read`**, one exchange: asking in two calls puts a stop between
// the halves, which is a different conversation that plenty of devices do not answer.
//
// Keep `probe()` and `read()` apart on purpose: a probe answers *is the part there*, and a read answers
// *what did it see*. A read that finds nothing is a success with an empty report, not a failure — and a
// driver that confuses the two is one whose failures cannot be told from a still finger.
//
// Then **prove it**: a protocol is the one layer of a firmware stack that can be tested on a host,
// against a fake bus answering with recorded bytes — no panel, no IDF, no hardware. `test/` is already
// built that way and already runs; the cases in `test/ft6336_test.cpp` are yours.

bool Ft6336::probe() {
    // TODO: read the chip id through `bus_write_read` and compare it with the datasheet's, rather than
    // trusting that a bus handle exists.
    //
    // Until then: `false`. The component's contract is to report what it knows, and a stub knows
    // nothing yet.
    return false;
}

bool Ft6336::read(Touch&) {
    // TODO: point the controller at its touch registers and read them back in one exchange, then
    // decode presence and coordinates. Return false on an exchange that failed; return true with
    // `down == false` for a panel that was read and has nothing on it.
    return false;
}

}  // namespace ft6336
