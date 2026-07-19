#include "core/platform/windows_virtual_asio_object_names.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

bool has_error_code(
    const sar::platform::WindowsVirtualAsioObjectNamesResult& result,
    std::string_view code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

void expect_local_object_name(std::wstring_view name) {
  assert(name.starts_with(L"Local\\SAR.VirtualASIO.v1."));
  assert(name.find(L"Global\\") == std::wstring_view::npos);
  assert(name.find(L'\\', std::wstring_view(L"Local\\").size()) ==
         std::wstring_view::npos);
  assert(name.find(L'/') == std::wstring_view::npos);
}

}  // namespace

int main() {
  const auto names = sar::platform::make_windows_virtual_asio_object_names(
      "studio-main", "reaper_1", 0x0123456789abcdefULL);
  assert(names.ok());
  assert(names.errors().empty());
  assert(names.names().mapping ==
         L"Local\\SAR.VirtualASIO.v1.endpoint.studio-main.client.reaper_1."
         L"generation.0123456789abcdef.mapping");
  assert(names.names().input_event ==
         L"Local\\SAR.VirtualASIO.v1.endpoint.studio-main.client.reaper_1."
         L"generation.0123456789abcdef.input-event");
  assert(names.names().output_event ==
         L"Local\\SAR.VirtualASIO.v1.endpoint.studio-main.client.reaper_1."
         L"generation.0123456789abcdef.output-event");
  assert(names.names().shutdown_event ==
         L"Local\\SAR.VirtualASIO.v1.endpoint.studio-main.client.reaper_1."
         L"generation.0123456789abcdef.shutdown-event");
  expect_local_object_name(names.names().mapping);
  expect_local_object_name(names.names().input_event);
  expect_local_object_name(names.names().output_event);
  expect_local_object_name(names.names().shutdown_event);

  const auto repeated = sar::platform::make_windows_virtual_asio_object_names(
      "studio-main", "reaper_1", 0x0123456789abcdefULL);
  assert(repeated.ok());
  assert(repeated.names() == names.names());

  const auto next_generation =
      sar::platform::make_windows_virtual_asio_object_names(
          "studio-main", "reaper_1", 0x0123456789abcdf0ULL);
  assert(next_generation.ok());
  assert(next_generation.names().mapping != names.names().mapping);

  const std::string maximum_token(
      sar::platform::kWindowsVirtualAsioMaxObjectTokenBytes, 'a');
  assert(sar::platform::make_windows_virtual_asio_object_names(
             maximum_token, maximum_token, 1)
             .ok());

  const auto empty = sar::platform::make_windows_virtual_asio_object_names(
      "", "", 0);
  assert(!empty.ok());
  assert(has_error_code(empty, "empty_virtual_asio_endpoint_token"));
  assert(has_error_code(empty, "empty_virtual_asio_client_token"));
  assert(has_error_code(empty,
                        "invalid_virtual_asio_connection_generation"));

  const auto too_long = sar::platform::make_windows_virtual_asio_object_names(
      std::string(sar::platform::kWindowsVirtualAsioMaxObjectTokenBytes + 1,
                  'e'),
      std::string(sar::platform::kWindowsVirtualAsioMaxObjectTokenBytes + 1,
                  'c'),
      1);
  assert(!too_long.ok());
  assert(has_error_code(too_long,
                        "virtual_asio_endpoint_token_too_long"));
  assert(has_error_code(too_long, "virtual_asio_client_token_too_long"));

  const auto namespace_injection =
      sar::platform::make_windows_virtual_asio_object_names(
          "Global\\outside", "..\\client", 1);
  assert(!namespace_injection.ok());
  assert(has_error_code(namespace_injection,
                        "invalid_virtual_asio_endpoint_token"));
  assert(has_error_code(namespace_injection,
                        "invalid_virtual_asio_client_token"));

  const auto invalid_ascii =
      sar::platform::make_windows_virtual_asio_object_names(
          "Studio.Main", "client:name", 1);
  assert(!invalid_ascii.ok());
  assert(has_error_code(invalid_ascii,
                        "invalid_virtual_asio_endpoint_token"));
  assert(has_error_code(invalid_ascii,
                        "invalid_virtual_asio_client_token"));

  const std::string non_ascii("client\x80", 7);
  const auto invalid_non_ascii =
      sar::platform::make_windows_virtual_asio_object_names(
          "endpoint", non_ascii, 1);
  assert(!invalid_non_ascii.ok());
  assert(has_error_code(invalid_non_ascii,
                        "invalid_virtual_asio_client_token"));
}
