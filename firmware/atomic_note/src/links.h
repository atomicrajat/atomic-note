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

constexpr Link kLinks[] = {
    {"GitHub", "https://github.com/atomicrajat"},
    {"Website", "https://atomicrajat.in"},
    {"LinkedIn", "https://www.linkedin.com/in/rajatmr/"},
    {"TannaTechBiz", "https://tannatechbiz.com"},
};
constexpr int kLinkCount = sizeof(kLinks) / sizeof(kLinks[0]);

}  // namespace links
