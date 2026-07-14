#include "core/platform/wasapi_recovery_policy.h"

#include <cstdint>
#include <iostream>
#include <type_traits>

namespace {

using sar::platform::WasapiFailureClass;
using sar::platform::WasapiRecoveryPolicy;
using sar::platform::WasapiRecoveryState;
using sar::platform::classify_wasapi_failure_code;

int expect(bool condition, const char* message) {
  if (condition) {
    return 0;
  }
  std::cerr << message << '\n';
  return 1;
}

int expect_state(const WasapiRecoveryPolicy& policy,
                 WasapiRecoveryState expected,
                 const char* message) {
  return expect(policy.state() == expected, message);
}

}  // namespace

int main() {
  static_assert(std::is_trivially_copyable_v<WasapiRecoveryPolicy>);
  static_assert(noexcept(WasapiRecoveryPolicy{}.tick(0)));

  {
    if (expect(classify_wasapi_failure_code("wasapi_device_lookup_failed") ==
                   WasapiFailureClass::DeviceInvalidated,
               "Device lookup failure must trigger endpoint recovery") ||
        expect(classify_wasapi_failure_code("wasapi_probe_format_changed") ==
                   WasapiFailureClass::DeviceInvalidated,
               "Probe drift must trigger endpoint recovery") ||
        expect(classify_wasapi_failure_code("wasapi_render_buffer_failed") ==
                   WasapiFailureClass::Transient,
               "Runtime buffer failure must be retryable") ||
        expect(classify_wasapi_failure_code("wasapi_duplex_event_wait_failed") ==
                   WasapiFailureClass::Transient,
               "Runtime event failure must be retryable") ||
        expect(classify_wasapi_failure_code("graph_sample_rate_mismatch") ==
                   WasapiFailureClass::Fatal,
               "Graph configuration failure must not retry") ||
        expect(classify_wasapi_failure_code("sample_conversion_failed") ==
                   WasapiFailureClass::Fatal,
               "Sample conversion failure must not retry") ||
        expect(classify_wasapi_failure_code("future_unclassified_error") ==
                   WasapiFailureClass::Unknown,
               "Unknown failures must remain fail-closed")) {
      return 1;
    }
  }

  {
    WasapiRecoveryPolicy policy;
    if (expect_state(policy, WasapiRecoveryState::Stopped,
                     "Policy must begin stopped")) {
      return 1;
    }
    policy.request_start(100);
    if (expect_state(policy, WasapiRecoveryState::Opening,
                     "Start must enter opening")) {
      return 1;
    }
    policy.on_open_succeeded(110);
    if (expect_state(policy, WasapiRecoveryState::Running,
                     "Open success must enter running")) {
      return 1;
    }
    policy.request_stop();
    if (expect_state(policy, WasapiRecoveryState::Quiescing,
                     "Running stop must enter quiescing")) {
      return 1;
    }
    policy.on_quiesced(110);
    if (expect_state(policy, WasapiRecoveryState::Stopped,
                     "Quiesce completion must enter stopped")) {
      return 1;
    }
  }

  {
    WasapiRecoveryPolicy policy;
    policy.request_start(0);
    policy.on_open_succeeded(0);
    policy.on_failure(WasapiFailureClass::DeviceInvalidated, 100);
    if (expect_state(policy, WasapiRecoveryState::Quiescing,
                     "Running failure must enter quiescing") ||
        expect(policy.attempt_count() == 0,
               "Quiescing must not consume an attempt")) {
      return 1;
    }
    policy.tick(100);
    if (expect_state(policy, WasapiRecoveryState::Quiescing,
                     "Tick must not reopen while quiescing")) {
      return 1;
    }
    policy.on_quiesced(100);
    if (expect_state(policy, WasapiRecoveryState::Backoff,
                     "Quiesce completion must enter backoff") ||
        expect(policy.attempt_count() == 1,
               "First recovery attempt must consume one attempt") ||
        expect(policy.next_attempt_at_ms() == 100,
               "First recovery attempt must have zero delay") ||
        expect(policy.recovery_deadline_at_ms() == 5100,
               "Recovery deadline must be five seconds from first failure")) {
      return 1;
    }
    policy.tick(100);
    if (expect_state(policy, WasapiRecoveryState::Opening,
                     "Zero-delay backoff must open on the same timestamp")) {
      return 1;
    }

    policy.on_failure(WasapiFailureClass::Transient, 120);
    if (expect(policy.attempt_count() == 2,
               "Second failure must consume the second attempt") ||
        expect(policy.next_attempt_at_ms() == 370,
               "Second recovery attempt must wait 250 ms")) {
      return 1;
    }
    policy.tick(369);
    if (expect_state(policy, WasapiRecoveryState::Backoff,
                     "Second backoff must not open early")) {
      return 1;
    }
    policy.tick(370);
    policy.on_failure(WasapiFailureClass::Transient, 400);
    if (expect(policy.attempt_count() == 3,
               "Third failure must consume the final attempt") ||
        expect(policy.next_attempt_at_ms() == 1650,
               "Third recovery attempt must wait 1250 ms")) {
      return 1;
    }
    policy.tick(1649);
    if (expect_state(policy, WasapiRecoveryState::Backoff,
                     "Third backoff must not open early")) {
      return 1;
    }
    policy.tick(1650);
    policy.on_failure(WasapiFailureClass::Transient, 1700);
    if (expect_state(policy, WasapiRecoveryState::Faulted,
                     "A fourth recovery request must fault")) {
      return 1;
    }
  }

  {
    WasapiRecoveryPolicy policy;
    policy.request_start(0);
    policy.on_open_succeeded(0);
    policy.on_failure(WasapiFailureClass::Transient, 10);
    policy.on_quiesced(10);
    policy.tick(10);
    policy.on_open_succeeded(10);
    policy.on_failure(WasapiFailureClass::Transient, 5009);
    if (expect_state(policy, WasapiRecoveryState::Quiescing,
                     "Unstable running failure must quiesce before retry") ||
        expect(policy.attempt_count() == 1,
               "Pending recovery must preserve the consumed budget")) {
      return 1;
    }
    policy.on_quiesced(5009);
    if (expect(policy.attempt_count() == 2,
               "Attempt budget must survive an unstable running period")) {
      return 1;
    }
  }

  {
    WasapiRecoveryPolicy policy;
    policy.request_start(0);
    policy.on_open_succeeded(0);
    policy.on_failure(WasapiFailureClass::Transient, 10);
    policy.on_quiesced(10);
    policy.tick(10);
    policy.on_open_succeeded(10);
    policy.tick(5009);
    if (expect(policy.attempt_count() == 1,
               "Stability budget must not reset before five seconds")) {
      return 1;
    }
    policy.tick(5010);
    if (expect(policy.attempt_count() == 0,
               "Five stable seconds must reset the attempt budget") ||
        expect(policy.recovery_deadline_at_ms() == 0,
               "Stability reset must clear the recovery deadline")) {
      return 1;
    }
    policy.on_failure(WasapiFailureClass::Transient, 5010);
    if (expect_state(policy, WasapiRecoveryState::Quiescing,
                     "Fresh running failure must quiesce") ||
        expect(policy.attempt_count() == 0,
               "Fresh attempt must wait for quiesce completion") ||
        expect(policy.recovery_deadline_at_ms() == 10010,
               "Fresh recovery must receive a fresh deadline")) {
      return 1;
    }
    policy.on_quiesced(5010);
    if (expect(policy.attempt_count() == 1,
               "Failure after stability must start a fresh budget") ||
        expect(policy.recovery_deadline_at_ms() == 10010,
               "Quiescing must preserve the fresh deadline")) {
      return 1;
    }
  }

  {
    WasapiRecoveryPolicy policy;
    policy.request_start(1000);
    policy.on_failure(WasapiFailureClass::Transient, 1000);
    policy.tick(1000);
    policy.on_failure(WasapiFailureClass::Transient, 1100);
    policy.tick(6000);
    if (expect_state(policy, WasapiRecoveryState::Faulted,
                     "Recovery must fault exactly at its hard deadline")) {
      return 1;
    }

    policy.request_stop();
    if (expect_state(policy, WasapiRecoveryState::Stopped,
                     "Stop must clear a faulted policy")) {
      return 1;
    }
    policy.request_start(7000);
    policy.on_failure(WasapiFailureClass::Transient, 7000);
    policy.request_stop();
    if (expect_state(policy, WasapiRecoveryState::Stopped,
                     "Stop must interrupt backoff immediately")) {
      return 1;
    }
    policy.tick(8000);
    if (expect_state(policy, WasapiRecoveryState::Stopped,
                     "Interrupted backoff must stay stopped")) {
      return 1;
    }
  }

  {
    WasapiRecoveryPolicy policy;
    policy.request_start(0);
    policy.on_open_succeeded(1);
    policy.on_failure(WasapiFailureClass::Transient, 2);
    policy.request_stop();
    policy.tick(1000);
    if (expect_state(policy, WasapiRecoveryState::Quiescing,
                     "Explicit stop must keep the loop quiescing")) {
      return 1;
    }
    policy.on_quiesced(1000);
    if (expect_state(policy, WasapiRecoveryState::Stopped,
                     "Explicit stop must override pending recovery") ||
        expect(policy.attempt_count() == 0,
               "Explicit stop must clear the recovery budget")) {
      return 1;
    }
  }

  {
    if (expect(sar::platform::wasapi_failure_is_recoverable(
                   WasapiFailureClass::Transient),
               "Transient failures must be recoverable") ||
        expect(sar::platform::wasapi_failure_is_recoverable(
                   WasapiFailureClass::DeviceInvalidated),
               "Device invalidation must be recoverable") ||
        expect(!sar::platform::wasapi_failure_is_recoverable(
                   WasapiFailureClass::Fatal),
               "Fatal failures must not be recoverable") ||
        expect(!sar::platform::wasapi_failure_is_recoverable(
                   WasapiFailureClass::Unknown),
               "Unknown failures must fail closed")) {
      return 1;
    }

    WasapiRecoveryPolicy fatal_policy;
    fatal_policy.request_start(0);
    fatal_policy.on_failure(WasapiFailureClass::Fatal, 1);
    if (expect_state(fatal_policy, WasapiRecoveryState::Faulted,
                     "Fatal failure must fault immediately") ||
        expect(fatal_policy.attempt_count() == 0,
               "Fatal failure must not consume a recovery attempt")) {
      return 1;
    }

    WasapiRecoveryPolicy unknown_policy;
    unknown_policy.request_start(0);
    unknown_policy.on_failure(WasapiFailureClass::Unknown, 1);
    if (expect_state(unknown_policy, WasapiRecoveryState::Faulted,
                     "Unknown failure must fault immediately") ||
        expect(unknown_policy.attempt_count() == 0,
               "Unknown failure must not consume a recovery attempt")) {
      return 1;
    }
  }

  std::cout << "WASAPI recovery policy smoke test passed\n";
  return 0;
}
