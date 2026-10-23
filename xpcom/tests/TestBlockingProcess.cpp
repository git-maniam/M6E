#  include <unistd.h>

int main() {
  // Block until killed. The bound keeps a stray process from outliving the test
  // harness if a kill is ever lost.
  for (int i = 0; i < 60; ++i) {
    sleep(10);
  }
  return 0;
}
