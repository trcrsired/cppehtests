/* Test the _Unwind_* getter APIs directly, no C++ EH.
   Verifies _Unwind_FindEnclosingFunction works, which exercises
   RtlLookupFunctionEntry (i.e. the SEH pdata/xdata tables).  */
#include <unwind.h>
#include <stdio.h>

static void __attribute__((noinline)) func_a(void) { }
static void __attribute__((noinline)) func_b(void) { }
static int __attribute__((noinline)) func_c(void) { return 7; }
static int __attribute__((noinline)) func_d(int x) { return x + 1; }

int main(void)
{
  _Unwind_Ptr a = (_Unwind_Ptr)func_a;
  _Unwind_Ptr b = (_Unwind_Ptr)func_b;
  _Unwind_Ptr c = (_Unwind_Ptr)func_c;
  _Unwind_Ptr d = (_Unwind_Ptr)func_d;
  void *ra, *rb, *rc, *rd;

  printf("main: start\n");
  fflush(stdout);

  ra = _Unwind_FindEnclosingFunction((void *)a);
  rb = _Unwind_FindEnclosingFunction((void *)b);
  rc = _Unwind_FindEnclosingFunction((void *)c);
  rd = _Unwind_FindEnclosingFunction((void *)d);

  printf("func_a: expected=%p got=%p %s\n", (void *)a, ra,
         ra == (void *)a ? "OK" : "MISMATCH");
  printf("func_b: expected=%p got=%p %s\n", (void *)b, rb,
         rb == (void *)b ? "OK" : "MISMATCH");
  printf("func_c: expected=%p got=%p %s\n", (void *)c, rc,
         rc == (void *)c ? "OK" : "MISMATCH");
  printf("func_d: expected=%p got=%p %s\n", (void *)d, rd,
         rd == (void *)d ? "OK" : "MISMATCH");
  fflush(stdout);

  printf("main: done\n");
  return 0;
}
