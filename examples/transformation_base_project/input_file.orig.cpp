// This file should be used as input when running the binary `transformation`

#include <array>
#include <vector>
#include <string>

int MkX(int i){
  return 0;
}

int MkX(double i){
  return 0;
}

int main() {
  // Gets detected
  for (int i = 0; i < 10; i++) {
    if (i == 11)
      return 1;
  }

  // Does not get detected
  int inc = 0;
  std::array<int, 3> a{3, 2, 1};
  for (int i : a) {
    ++inc;
  }

  // Does not get detected
  std::vector<int> v{1, 2, 3};
  for (int i : v) {
    ++inc;
  }

  return 0;
}
