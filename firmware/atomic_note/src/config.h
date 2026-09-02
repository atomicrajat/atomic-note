// Build-time switches.
//
// Keep this file small. Anything a user should be able to change belongs in
// settings (and later the web app), not here.
#pragma once

namespace config {

// Development: hold the device awake so the USB CDC never drops.
//
// Deep sleep powers down the native USB peripheral, so the board disappears
// from /dev mid-session and cannot be flashed or talked to until someone
// presses a button. That is correct product behaviour and badly wrong for
// bring-up, so it is switchable.
//
// SET THIS TO false BEFORE ANY BATTERY-LIFE MEASUREMENT OR RELEASE BUILD.
constexpr bool kDevKeepAwake = false;

// Development: the bring-up and documentation commands on the serial console.
//
// `shot` and `screen` exist to generate the screenshots in the README, and
// `buzz`, `motion`, `dist`, `tof`, `rail` and `rot` exist to poke at hardware
// while bringing a part up. None of them is a product feature: a shipped
// device has no reason to be able to dump its framebuffer or drive its motor
// on command, and a serial console that can jump to any screen is a way to
// confuse a device rather than to use one.
//
// Left as constexpr rather than a #define so the compiler still type-checks
// the code it then discards — the branches fold away and the bodies go with
// them, which is why a release build is smaller.
//
// Turn ON to regenerate docs/screens/ with ./tools/screenshot.py, then OFF.
constexpr bool kDevTools = false;

// Development: log a heartbeat and time every panel refresh.
//
// The heartbeat is what distinguishes "the device hung" from "the USB link
// dropped" — without it a silent port is ambiguous. Panel timings are useful
// for spotting a refresh that has started taking too long.
constexpr bool kDevVerbose = false;

}  // namespace config
