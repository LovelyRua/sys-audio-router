#include "tools/physical_asio_measure_options.h"

#include <cassert>
#include <initializer_list>
#include <string>

namespace {

sar::tools::PhysicalAsioMeasureOptionsResult parse(
    std::initializer_list<const char*> arguments) {
  return sar::tools::parse_physical_asio_measure_options(
      static_cast<int>(arguments.size()), arguments.begin());
}

}  // namespace

int main() {
  const auto defaults = parse({"measure", "--driver", "MOTU"});
  assert(defaults.ok());
  assert(defaults.options.driver == "MOTU");
  assert(defaults.options.sample_rate == 48000);
  assert(defaults.options.block_frames == 0);
  assert(defaults.options.duration_ms == 5000);

  const auto complete = parse({"measure", "--driver", "{CLSID}",
                               "--sample-rate", "96000", "--block-frames",
                               "256", "--duration-ms", "12000"});
  assert(complete.ok());
  assert(complete.options.sample_rate == 96000);
  assert(complete.options.block_frames == 256);
  assert(complete.options.duration_ms == 12000);

  assert(parse({"measure", "--help"}).ok());
  assert(parse({"measure"}).error == "--driver is required");
  assert(!parse({"measure", "--driver"}).ok());
  assert(!parse({"measure", "--driver", "MOTU", "--duration-ms", "0"}).ok());
  assert(!parse({"measure", "--driver", "MOTU", "--sample-rate", "abc"}).ok());
  assert(!parse({"measure", "--driver", "MOTU", "--block-frames", "-1"}).ok());
  assert(!parse({"measure", "--driver", "MOTU", "--unknown", "1"}).ok());
  assert(std::string(sar::tools::physical_asio_measure_usage()).find(
             "--driver NAME-OR-CLSID") != std::string::npos);
}
