#pragma once
// ============================================================================
// src/engine/object/external_visit.h -- the resource-reference visit of ONE
//   ExternalPtr (block VISITPASS, NOT RETAIL in its shortcut, exact)
// ============================================================================
// `ExternalPtr<T>::Serialize` under a VisitorStream (Encoding kind 3) is
// retail's `FindOrLoad(cur->GetPath())` with a discarded `IsDerivedFrom`
// (external_ptr_serialize.cpp, mode 1). external_visit.cpp has that walk and,
// in front of it, the shortcut that reaches the same row of
// `ExternalFactory::_resources` from the object itself; its banner holds the
// exactness argument.
// ============================================================================

#include <cstdint>

namespace Object {
class External;
class BaseTypeInfo;
}

namespace RtObject {

// The visit of `cur` (non-null): what retail's mode-1 arm does, by the shortcut
// when it is known exact, by the path walk otherwise.
void VisitExternal(::Object::External* cur, const ::Object::BaseTypeInfo& typeInfo);

// The `[VisitPass]` exit line (installed with the `[ExtAdd]` one).
void DumpVisitCounters();

}  // namespace RtObject
