/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>

typedef unsigned nsresult;
typedef unsigned uint32_t;
typedef unsigned nsXPCVariant;

#  define NS_IMETHOD virtual nsresult
#  define NS_IMETHODIMP nsresult

class base {
 public:
  NS_IMETHOD ignored() = 0;
};

class foo : public base {
 public:
  NS_IMETHOD callme1(int i, int j) = 0;
  NS_IMETHOD callme2(int i, int j) = 0;
  NS_IMETHOD callme3(int i, int j) = 0;
};

class bar : public foo {
 public:
  NS_IMETHOD ignored() override;
  NS_IMETHOD callme1(int i, int j) override;
  NS_IMETHOD callme2(int i, int j) override;
  NS_IMETHOD callme3(int i, int j) override;
};

class baz : public base {
 public:
  NS_IMETHOD ignored() override;
  NS_IMETHOD callme1() override;
  NS_IMETHOD callme2() override;
  NS_IMETHOD callme3() override;
  void setfoo(foo* f) { other = f; }

  foo* other;
};
NS_IMETHODIMP baz::ignored() { return 0; }

NS_IMETHODIMP bar::ignored() { return 0; }

NS_IMETHODIMP bar::callme1(int i, int j) {
  printf("called bar::callme1 with: %d %d\n", i, j);
  return 15;
}

NS_IMETHODIMP bar::callme2(int i, int j) {
  printf("called bar::callme2 with: %d %d\n", i, j);
  return 25;
}

NS_IMETHODIMP bar::callme3(int i, int j) {
  printf("called bar::callme3 with: %d %d\n", i, j);
  return 35;
}

void docall(foo* f, int i, int j) { f->callme1(i, j); }

/***************************************************************************/
/***************************************************************************/
// just Linux_x86 now. Add other later...

static int PrepareAndDispatch(baz* self, uint32_t methodIndex, uint32_t* args) {
  foo* a = self->other;
  int p1 = (int)*args;
  int p2 = (int)*(args + 1);
  switch (methodIndex) {
    case 1:
      a->callme1(p1, p2);
      break;
    case 2:
      a->callme2(p1, p2);
      break;
    case 3:
      a->callme3(p1, p2);
      break;
  }
  return 1;
}

#  define STUB_ENTRY(n)                                      \
    nsresult baz::callme##n() {                              \
      void* method = PrepareAndDispatch;                     \
      nsresult result;                                       \
      __asm__ __volatile__(                                  \
          "leal   0x0c(%%ebp), %%ecx\n\t" /* args */         \
          "pushl  %%ecx\n\t"                                 \
          "pushl  $" #n                                      \
          "\n\t"                          /* method index */ \
          "movl   0x08(%%ebp), %%ecx\n\t" /* this */         \
          "pushl  %%ecx\n\t"                                 \
          "call   *%%edx" /* PrepareAndDispatch */           \
          : "=a"(result)  /* %0 */                           \
          : "d"(method)   /* %1 */                           \
          : "memory");                                       \
      return result;                                         \
    }

/***************************************************************************/

STUB_ENTRY(1)
STUB_ENTRY(2)
STUB_ENTRY(3)

int main() {
  foo* a = new bar();
  baz* b = new baz();

  /* here we make the global 'check for alloc failure' checker happy */
  if (!a || !b) return 1;

  foo* c = (foo*)b;

  b->setfoo(a);
  c->callme1(1, 2);
  c->callme2(2, 4);
  c->callme3(3, 6);

  return 0;
}
