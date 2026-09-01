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

// Development: log a heartbeat and time every panel refresh.
//
// The heartbeat is what distinguishes "the device hung" from "the USB link
// dropped" — without it a silent port is ambiguous. Panel timings are useful
// for spotting a refresh that has started taking too long.
constexpr bool kDevVerbose = false;

}  // namespace config
