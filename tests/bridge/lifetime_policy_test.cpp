#include "../src/runtime/lifetime_policy.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
using namespace cc2::lifetime;
static int checks = 0;
static void Check(bool value, const char* label) {
  ++checks; if (!value) { std::printf("FAIL: %s\n", label); std::exit(1); }
}
int main() {
  EpochQueue queue; std::int8_t steps[EpochQueue::Capacity]{};
  Epoch first{0x1000, 1}, reused{0x1000, 2};
  Check(queue.Push(1, {}) == EnqueueResult::Unavailable, "no server rejects request");
  queue.Enable(true); queue.Begin(first);
  Check(queue.Push(1, first) == EnqueueResult::Queued, "current generation queues");
  queue.Begin(first); Check(queue.size() == 1, "duplicate boundary is idempotent");
  queue.End(first); Check(queue.dropped == 1 && !queue.active, "teardown discards pending work");
  queue.Begin(reused);
  Check(queue.Push(1, first) == EnqueueResult::Unavailable, "delayed old producer rejected on ABA");
  Check(queue.Push(-1, reused) == EnqueueResult::Queued, "new same-address object queues");
  Check(queue.Drain(first, steps) == 0 && queue.size() == 1, "old in-flight consumer cannot steal new commands");
  queue.End(first); Check(queue.active == reused, "late old cleanup cannot end new generation");
  Check(queue.Drain(reused, steps) == 1 && steps[0] == -1, "only new work reaches new world");
  for (unsigned i=0; i<EpochQueue::Capacity; ++i) queue.Push(1, reused);
  Check(queue.Push(1, reused) == EnqueueResult::Full, "bounded queue");
  queue.Enable(false); Check(!queue.active && queue.size() == 0, "partial hooks fail closed");
  queue.Enable(true); queue.Begin(first); queue.Begin(reused);
  Check(queue.ambiguous && !queue.active, "overlapping servers fail closed, never guess current root");
  // Model enqueue vs cleanup under the same lock used by integration.
  EpochQueue concurrent; concurrent.Enable(true); concurrent.Begin(first);
  std::mutex lock;
  std::thread producer([&] { for(int i=0;i<10000;++i) {
    std::lock_guard<std::mutex> guard(lock); concurrent.Push(1, first);
  }});
  { std::lock_guard<std::mutex> guard(lock); concurrent.End(first); concurrent.Begin(reused); }
  producer.join();
  Check(concurrent.Drain(reused, steps) == 0, "concurrent stale producer cannot leak old requests");
  std::printf("PASS: %d lifetime checks (local fixtures, not game evidence)\n", checks);
}
