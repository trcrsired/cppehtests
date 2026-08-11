/* Minimal test of the libgcc _Unwind_* APIs, no iostream.
   Tests whether _Unwind_RaiseException triggers the personality
   and runs __attribute__((cleanup)) cleanups during phase 2.  */
#include <unwind.h>
#include <stdio.h>
#include <string.h>

static void my_cleanup(int *p)
{
  printf("CLEANUP ran, value=%d\n", *p);
  fflush(stdout);
}

static void __attribute__((noinline)) inner(void)
{
  int guard __attribute__((cleanup(my_cleanup))) = 42;
  struct _Unwind_Exception exc;
  _Unwind_Reason_Code rc;

  printf("inner: before _Unwind_RaiseException\n");
  fflush(stdout);

  memset(&exc, 0, sizeof(exc));
  exc.exception_class = 0x47434331; /* 'GCC1' */

  rc = _Unwind_RaiseException(&exc);
  printf("inner: _Unwind_RaiseException returned rc=%d (END_OF_STACK=%d)\n",
         (int)rc, (int)_URC_END_OF_STACK);
  fflush(stdout);
}

int main(void)
{
  printf("main: start\n");
  fflush(stdout);
  inner();
  printf("main: after inner (should be reached if unwinding works)\n");
  fflush(stdout);
  return 0;
}
