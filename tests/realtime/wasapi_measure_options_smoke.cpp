#include "tools/wasapi_measure_options.h"

#include <cstdint>
#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

struct ParseCase {
  int argc = 0;
  char** argv = nullptr;
};

ParseCase make_case(char** argv, int argc) {
  return {argc, argv};
}

bool parse_case(ParseCase parse_case, sar::tools::WasapiMeasureOptions& options) {
  return sar::tools::parse_wasapi_measure_options(
      parse_case.argc, parse_case.argv, options);
}

}  // namespace

int main() {
  {
    char program[] = "sar_measure_wasapi_render_loop";
    char* argv[] = {program};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(parse_case(make_case(argv, 1), options),
                                    "Expected default parse success")) {
      return failure;
    }
    if (const auto failure = expect(options.duration_ms == 250,
                                    "Expected default duration")) {
      return failure;
    }
    if (const auto failure = expect(options.timeout_ms == 10,
                                    "Expected default timeout")) {
      return failure;
    }
    if (const auto failure = expect(!options.require_healthy,
                                    "Expected default health mode off")) {
      return failure;
    }
    if (const auto failure = expect(!options.show_help,
                                    "Expected default help off")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char help[] = "--help";
    char duration[] = "--duration-ms";
    char value[] = "999";
    char* argv[] = {program, help, duration, value};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(parse_case(make_case(argv, 4), options),
                                    "Expected long help parse success")) {
      return failure;
    }
    if (const auto failure = expect(options.show_help,
                                    "Expected long help flag")) {
      return failure;
    }
    if (const auto failure = expect(options.duration_ms == 250,
                                    "Expected help to stop option parsing")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_duplex_loop";
    char help[] = "-h";
    char unknown[] = "--unknown-after-help";
    char* argv[] = {program, help, unknown};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(parse_case(make_case(argv, 3), options),
                                    "Expected short help parse success")) {
      return failure;
    }
    if (const auto failure = expect(options.show_help,
                                    "Expected short help flag")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char duration[] = "--duration-ms";
    char duration_value[] = "1000";
    char timeout[] = "--timeout-ms";
    char timeout_value[] = "25";
    char require[] = "--require-healthy";
    char* argv[] = {
        program, duration, duration_value, timeout, timeout_value, require};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(parse_case(make_case(argv, 6), options),
                                    "Expected full option parse success")) {
      return failure;
    }
    if (const auto failure = expect(options.duration_ms == 1000,
                                    "Expected parsed duration")) {
      return failure;
    }
    if (const auto failure = expect(options.timeout_ms == 25,
                                    "Expected parsed timeout")) {
      return failure;
    }
    if (const auto failure = expect(options.require_healthy,
                                    "Expected require healthy flag")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char timeout[] = "--timeout-ms";
    char timeout_value[] = "0";
    char duration[] = "--duration-ms";
    char duration_value[] = "0";
    char* argv[] = {program, timeout, timeout_value, duration, duration_value};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(parse_case(make_case(argv, 5), options),
                                    "Expected zero values parse success")) {
      return failure;
    }
    if (const auto failure = expect(options.timeout_ms == 0,
                                    "Expected zero timeout")) {
      return failure;
    }
    if (const auto failure = expect(options.duration_ms == 0,
                                    "Expected zero duration")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char duration[] = "--duration-ms";
    char first[] = "100";
    char second_duration[] = "--duration-ms";
    char second[] = "200";
    char timeout[] = "--timeout-ms";
    char timeout_value[] = "4294967295";
    char* argv[] = {
        program, duration, first, second_duration, second, timeout, timeout_value};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(parse_case(make_case(argv, 7), options),
                                    "Expected repeated option parse success")) {
      return failure;
    }
    if (const auto failure = expect(options.duration_ms == 200,
                                    "Expected repeated duration to use last value")) {
      return failure;
    }
    if (const auto failure = expect(options.timeout_ms == 4294967295U,
                                    "Expected max u32 timeout")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char duration[] = "--duration-ms";
    char* argv[] = {program, duration};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(!parse_case(make_case(argv, 2), options),
                                    "Expected missing duration value failure")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char timeout[] = "--timeout-ms";
    char* argv[] = {program, timeout};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(!parse_case(make_case(argv, 2), options),
                                    "Expected missing timeout value failure")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char duration[] = "--duration-ms";
    char value[] = "abc";
    char* argv[] = {program, duration, value};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(!parse_case(make_case(argv, 3), options),
                                    "Expected non-numeric duration failure")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char timeout[] = "--timeout-ms";
    char value[] = "-1";
    char* argv[] = {program, timeout, value};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(!parse_case(make_case(argv, 3), options),
                                    "Expected negative timeout failure")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char timeout[] = "--timeout-ms";
    char value[] = "4294967296";
    char* argv[] = {program, timeout, value};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(!parse_case(make_case(argv, 3), options),
                                    "Expected overflowing timeout failure")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char timeout[] = "--timeout-ms";
    char value[] = "";
    char* argv[] = {program, timeout, value};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(!parse_case(make_case(argv, 3), options),
                                    "Expected empty timeout failure")) {
      return failure;
    }
  }

  {
    char program[] = "sar_measure_wasapi_render_loop";
    char unknown[] = "--definitely-not-a-real-option";
    char* argv[] = {program, unknown};
    sar::tools::WasapiMeasureOptions options;
    if (const auto failure = expect(!parse_case(make_case(argv, 2), options),
                                    "Expected unknown option failure")) {
      return failure;
    }
  }

  std::cout << "WASAPI measure options smoke test passed\n";
  return 0;
}
