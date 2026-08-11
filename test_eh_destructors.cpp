#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Base {
  const char* name;
  Base(const char* n) : name(n) { std::printf("  base %s ctor\n", name); }
  virtual ~Base() { std::printf("  base %s dtor\n", name); }
  virtual void ping() = 0;
};

struct Derived : Base {
  std::string payload;
  Derived(const char* n) : Base(n), payload(std::string(n) + "-payload") {}
  ~Derived() override { std::printf("  derived %s dtor (payload %s)\n", name, payload.c_str()); }
  void ping() override { std::printf("  derived %s ping\n", name); }
};

static int destroyed_count = 0;

struct Counter {
  const char* name;
  explicit Counter(const char* n) : name(n) { std::printf("  counter %s ctor\n", name); }
  ~Counter() { std::printf("  counter %s dtor\n", name); destroyed_count++; }
};

__attribute__((noinline)) void use_vectors()
{
  Counter c("vec");
  std::vector<std::string> v;
  v.push_back("one");
  v.push_back("two");
  v.push_back("three");
  v.push_back("four");
  v.push_back("five");
  std::printf("  vector size=%zu\n", v.size());
  throw std::string("from vector fn");
}

__attribute__((noinline)) void use_unique_ptr()
{
  Counter c("unique");
  std::unique_ptr<Derived> d(new Derived("d"));
  d->ping();
  throw 7;
}

__attribute__((noinline)) void use_shared_ptr()
{
  Counter c("shared");
  std::shared_ptr<Derived> s(new Derived("s"));
  std::shared_ptr<Derived> s2 = s;  // shared ownership
  s->ping();
  throw 3.14;
}

int main()
{
  int before = destroyed_count;
  try { use_vectors(); } catch (const std::string& s) { std::printf("caught string: %s\n", s.c_str()); }
  try { use_unique_ptr(); } catch (int i) { std::printf("caught int: %d\n", i); }
  try { use_shared_ptr(); } catch (double d) { std::printf("caught double: %g\n", d); }
  std::printf("destroyed_count=%d (before=%d)\n", destroyed_count, before);
  if (destroyed_count != before + 3)
    {
      std::printf("DTOR LEAK: expected %d, got %d\n", before + 3, destroyed_count);
      return 1;
    }
  std::printf("all destructors ran\n");
  return 0;
}
