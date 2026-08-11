#include <cstdio>
#include <stdexcept>
#include <string>

struct Tracker {
  static int alive;
  static int constructed;
  static int destroyed;
  int id;
  Tracker(int i) : id(i) { alive++; constructed++; }
  Tracker(const Tracker&) : id(-1) { alive++; constructed++; }
  ~Tracker() { alive--; destroyed++; }
};
int Tracker::alive = 0;
int Tracker::constructed = 0;
int Tracker::destroyed = 0;

struct ScopeGuard {
  const char* name;
  ScopeGuard(const char* n) : name(n) { std::printf("  enter %s\n", name); }
  ~ScopeGuard() { std::printf("  exit  %s\n", name); }
};

__attribute__((noinline)) void level4()
{
  ScopeGuard g("level4");
  Tracker t(4);
  std::printf("level4: throwing\n");
  throw std::runtime_error("deep error");
}

__attribute__((noinline)) void level3()
{
  ScopeGuard g("level3");
  Tracker t(3);
  level4();
}

__attribute__((noinline)) void level2()
{
  ScopeGuard g("level2");
  Tracker t(2);
  try {
    level3();
  } catch (const std::runtime_error& e) {
    std::printf("level2 caught: %s\n", e.what());
    throw;  // rethrow
  }
}

__attribute__((noinline)) void level1()
{
  ScopeGuard g("level1");
  Tracker t(1);
  level2();
}

int main()
{
  std::printf("alive=%d constructed=%d destroyed=%d\n",
              Tracker::alive, Tracker::constructed, Tracker::destroyed);
  try {
    level1();
  } catch (const std::runtime_error& e) {
    std::printf("main caught: %s\n", e.what());
  }
  std::printf("after catch: alive=%d constructed=%d destroyed=%d\n",
              Tracker::alive, Tracker::constructed, Tracker::destroyed);
  if (Tracker::alive != 0 || Tracker::destroyed != Tracker::constructed)
    {
      std::printf("LEAK DETECTED\n");
      return 1;
    }
  std::printf("all destructors ran correctly\n");
  return 0;
}
