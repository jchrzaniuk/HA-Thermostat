#include "ClimateModel.h"
#include <cassert>
int main() {
  ClimateModel m;
  assert(!m.adjustable());
  m.available = m.singleTarget = true; m.target = 21;
  assert(m.adjust(21, 1) == 21.5f);
  assert(m.adjust(35, 2) == 35);
  assert(m.adjust(5, -2) == 5);
  m.minimum = 45; m.maximum = 95; m.step = 1; m.target = 70;
  assert(m.adjust(70, -1) == 69);
  assert(m.decimals(70) == 0 && m.decimals(70.5f) == 1);
  m.step = 0.5f; assert(m.decimals(21) == 1);
  m.step = 0; assert(!m.adjustable());
  m.step = 0.5f; m.available = false; assert(!m.adjustable());
  m.available = true; m.target = NAN; assert(!m.adjustable());
}
