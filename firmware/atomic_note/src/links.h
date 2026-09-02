// The links the QR screen offers.
//
// Phase 5 moves these into settings so they can be edited from the web app;
// until then, changing a link means a reflash.
//
// Keep them SHORT. Payload length drives the QR version, and past roughly 90
// characters the modules get too fine to scan off a 200px panel. The `links`
// console command reports the encoded size of each one.
#pragma once

namespace links {

struct Link {
  const char* label;  // shown under the code
  const char* url;
};

// Seeded into NVS on first boot and owned by the user afterwards — edit them
// in the web app rather than here. These are only what a freshly flashed
// device shows before anyone has set their own.
constexpr Link kLinks[] = {
    {"Project", "https://github.com/atomicrajat/atomic-note"},
    {"Docs", "https://github.com/atomicrajat/atomic-note/tree/main/docs"},
};
constexpr int kLinkCount = sizeof(kLinks) / sizeof(kLinks[0]);

}  // namespace links
