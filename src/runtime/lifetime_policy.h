#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cc2::lifetime {
// Values are identities, never an authorization to dereference an address.
enum class Domain { Process, OwnedLuaVm, ServerWorker, Callback };
enum class Execution { LuaHostThread, CurrentCarrierUpdate, ServerWorkerBoundary };
struct Epoch {
  std::uintptr_t identity = 0;
  std::uint64_t generation = 0;
  explicit operator bool() const { return identity && generation; }
};
inline bool operator==(Epoch a, Epoch b) {
  return a.identity == b.identity && a.generation == b.generation;
}
enum class EnqueueResult { Queued, Unavailable, Full };

// Caller serializes all methods, including applying a drained batch. No
// allocation, object access, game-specific state guesses or internal mutex.
class EpochQueue {
 public:
  static constexpr std::size_t Capacity = 64;
  struct Command { Epoch epoch; std::int8_t step; };
  Epoch active{};
  bool enabled = false;
  bool ambiguous = false;
  std::uint64_t accepted = 0, applied = 0, dropped = 0, unavailable = 0;
  std::uint64_t stale = 0, transitions = 0;

  void Enable(bool value) { enabled = value; if (!value) Invalidate(); }
  void Begin(Epoch epoch) {
    if (!epoch || ambiguous || active == epoch) return;
    if (active) { ambiguous = true; Invalidate(); return; }
    active = epoch; ++transitions;
  }
  void End(Epoch epoch) { if (active == epoch) { Invalidate(); ++transitions; } }
  EnqueueResult Push(int step, Epoch expected) {
    if (!enabled || ambiguous || !active || !(expected == active)) {
      ++unavailable; return EnqueueResult::Unavailable;
    }
    if (count_ == Capacity) return EnqueueResult::Full;
    commands_[(head_ + count_) % Capacity] = {active, static_cast<std::int8_t>(step)};
    ++count_; ++accepted; return EnqueueResult::Queued;
  }
  std::size_t Drain(Epoch callbackEpoch, std::int8_t* output) {
    if (!enabled || !active || !(active == callbackEpoch)) { ++stale; return 0; }
    std::size_t n = 0;
    while (count_) {
      const auto command = commands_[head_]; head_ = (head_ + 1) % Capacity; --count_;
      if (command.epoch == callbackEpoch) output[n++] = command.step;
      else { ++dropped; ++stale; }
    }
    return n;
  }
  std::size_t size() const { return count_; }
 private:
  void Invalidate() { dropped += count_; count_ = 0; head_ = 0; active = {}; }
  std::array<Command, Capacity> commands_{};
  std::size_t head_ = 0, count_ = 0;
};
} // namespace cc2::lifetime
