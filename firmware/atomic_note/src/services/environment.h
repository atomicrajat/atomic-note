// SHTC3 temperature and humidity sensor.
//
// The sensor idles in a sleep state drawing well under a microamp, so each
// reading is a wake / measure / sleep cycle. That suits us — the dashboard
// samples it once a minute, not continuously.
#pragma once

#include <stdint.h>

namespace services {
namespace environment {

struct Reading {
  float celsius;
  float humidity;  // percent RH
  bool valid;
};

bool begin();

// Wake, measure, sleep. Takes roughly 15 ms. Returns an invalid Reading if the
// sensor does not respond or the CRC fails.
Reading measure();

}  // namespace environment
}  // namespace services
