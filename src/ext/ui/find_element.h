// src/ext/ui/find_element.h -- Ext::FindElement<T>: an element of our own dialog pages by name
//
// NOT RETAIL. The one lookup every extension's page uses (the Enhanced tab's rows, a toolbar
// page): docs/extensions/DESIGN.md §3, Names table, "element lookup" (it replaced the Enhanced tab's own lookup, 2026-10-20).
#pragma once

#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "ui/Element.h"

namespace Ext {

// An element of type T by name in `page`; null when absent or of another type.
template <class T>
T* FindElement(UI::Element* page, const char* name) {
    UI::Element* e = page->FindElement(System::String(name)).Get();
    return ::Object::IsDerivedFromNullSafe(e, T::TypeId()) ? static_cast<T*>(e) : nullptr;
}

}  // namespace Ext
