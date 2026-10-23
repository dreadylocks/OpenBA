// src/ext/high_limits/high_limits.cpp -- ext.high_limits' numbers (high_limits.h)
//
// NOT RETAIL. Standard C++ (the unit tests link it with registry.cpp).

#include "ext/high_limits/high_limits.h"

#include "ext/registry.h"

namespace Ext {
namespace HighLimits {

bool Active() { return Ext::Ext().high_limits; }   // class B: the game's locked value

int32_t MaxValue() { return Active() ? kHighMax : kRetailMax; }

int32_t MaxDigits() { return Active() ? kHighDigits : kRetailDigits; }

int32_t MinWidth() { return Active() ? kHighMinWidth : kRetailMinWidth; }

}  // namespace HighLimits
}  // namespace Ext
