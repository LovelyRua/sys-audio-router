#include "tools/virtual_asio_soak_options.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  {
    char program[] = "sar_virtual_asio_soak";
    char clients[] = "--clients";
    char client_value[] = "6";
    char blocks[] = "--block-sizes";
    char block_value[] = "64,128,512";
    char duration[] = "--duration-ms";
    char duration_value[] = "5000";
    char queue_limit[] = "--max-queue-failures";
    char queue_limit_value[] = "12";
    char dropout_limit[] = "--max-dropouts";
    char dropout_limit_value[] = "34";
    char* argv[] = {program, clients, client_value, blocks, block_value,
                    duration, duration_value, queue_limit, queue_limit_value,
                    dropout_limit, dropout_limit_value};
    sar::tools::VirtualAsioSoakOptions options;
    assert(sar::tools::parse_virtual_asio_soak_options(11, argv, options));
    assert(options.clients == 6);
    assert((options.block_sizes ==
            std::vector<std::uint32_t>{64, 128, 512}));
    assert(options.duration_ms == 5000);
    assert(options.maximum_queue_failures == 12);
    assert(options.maximum_dropouts == 34);
  }
  {
    char program[] = "sar_virtual_asio_soak";
    char clients[] = "--clients";
    char value[] = "33";
    char* argv[] = {program, clients, value};
    sar::tools::VirtualAsioSoakOptions options;
    assert(!sar::tools::parse_virtual_asio_soak_options(3, argv, options));
  }
  {
    char program[] = "sar_virtual_asio_soak";
    char blocks[] = "--block-sizes";
    char value[] = "64,,256";
    char* argv[] = {program, blocks, value};
    sar::tools::VirtualAsioSoakOptions options;
    assert(!sar::tools::parse_virtual_asio_soak_options(3, argv, options));
  }
  {
    char program[] = "sar_virtual_asio_soak";
    char blocks[] = "--block-sizes";
    char value[] = "64,";
    char* argv[] = {program, blocks, value};
    sar::tools::VirtualAsioSoakOptions options;
    assert(!sar::tools::parse_virtual_asio_soak_options(3, argv, options));
  }
  {
    char program[] = "sar_virtual_asio_soak";
    char help[] = "--help";
    char ignored[] = "--unknown";
    char* argv[] = {program, help, ignored};
    sar::tools::VirtualAsioSoakOptions options;
    assert(sar::tools::parse_virtual_asio_soak_options(3, argv, options));
    assert(options.show_help);
  }
  std::cout << "Virtual ASIO soak option smoke test passed\n";
}
