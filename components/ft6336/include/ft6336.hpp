// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 NatureSense

#pragma once

#include <ft6336_bus.hpp>

#include <cstdint>

namespace ft6336 {

/// One report from the panel: where a finger is, and whether there is one at all.
///
/// The coordinates are the **controller's** — the panel's own grid, the same on every board that fits
/// this module — not the display's. Mapping a point onto a widget is a different job, done above this
/// layer; a touch driver that knew about screens would be one that could not be tested without one.
struct Touch {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    bool down = false;  ///< whether a finger is on the panel. `x`/`y` mean nothing when this is false.
};

/// TODO: what this controller is, in the terms a caller thinks in — and what it is not.
///
/// **This is a stub, and the register map is the part no scaffold can write.** The register addresses,
/// the bit that says a touch is present, the byte order of a coordinate and the id that proves the
/// part is there are the device's own; they come off its datasheet rather than out of a guess.
///
/// What is fixed here is the shape, and the shape is worth keeping:
///
///  * it owns its **bus handle** and takes no board facts — the bus, the address, the interrupt line
///    and the reset line are the *board's*, supplied by the application. It takes the **handle**, not
///    the address: adding the device at its address is the application's job;
///  * it holds the **panel's** facts — the coordinate range — because those travel with the module;
///  * it names **no actor, no task and no framework type** — polling the controller, or waking on its
///    interrupt, is the product's business;
///  * it reaches the controller through `bus_write` / `bus_read` / `bus_write_read` and nothing else,
///    so it is testable on a host (`test/`) as well as on the chip;
///  * it reports failure by **returning false**, never by logging. A read that finds *no finger* is not
///    a failure — `read()` returns true with `Touch::down` false — and the two cases must stay apart.
class Ft6336 {
public:
    /// The module's coordinate range, in the panel's own units — the same module on every board.
    static constexpr std::uint16_t kWidth = 240;
    static constexpr std::uint16_t kHeight = 320;

    explicit Ft6336(BusHandle device);

    Ft6336(const Ft6336&) = delete;
    Ft6336& operator=(const Ft6336&) = delete;

    /// TODO: something that answers only if the controller is really there — read its chip id, or a
    /// status register, through `bus_write_read` — rather than trusting that a bus handle exists. A
    /// probe that returns true because a handle was passed is a probe that reports success on a board
    /// where the controller is not fitted.
    bool probe();

    /// TODO: read the controller's touch registers and fill `touch`. Return **false** only when the
    /// exchange failed; return **true** with `Touch::down == false` when the panel was read
    /// successfully and simply has no finger on it. Collapsing those two into one false is how a
    /// dropped I²C transaction becomes a stuck cursor.
    bool read(Touch& touch);

private:
    BusHandle device_;
};

}  // namespace ft6336
