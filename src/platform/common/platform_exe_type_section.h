// ============================================================================
// src/platform/common/platform_exe_type_section.h -- how the exe-type record section is
// spelled on a non-Windows target (src/game/Object/exe_type.h, factory_registration.cpp).
//
// The records' slots go to one section and the walk reads it between two markers. On ELF the
// linker defines __start_<section> / __stop_<section> for a section whose name is a C
// identifier. Mach-O has no such symbols: the linker answers section$start$<seg>$<sect> and
// section$end$<seg>$<sect> labels instead, and a section name needs its segment ("__DATA,x").
// This header is the one place that difference is spelled, so game code carries no vendor #if.
// ============================================================================
#pragma once

namespace Object {
struct BaseTypeInfo;
}

#if defined(__APPLE__)
#define PLATFORM_EXE_TYPE_SECTION "__DATA,exe_type_records"
extern "C" ::Object::BaseTypeInfo* __start_exe_type_records[] __asm("section$start$__DATA$exe_type_records");
extern "C" ::Object::BaseTypeInfo* __stop_exe_type_records[] __asm("section$end$__DATA$exe_type_records");
#else
#define PLATFORM_EXE_TYPE_SECTION "exe_type_records"
// Weak: a link with no record at all (a unit test) gets two nulls, an empty walk.
extern "C" __attribute__((weak)) ::Object::BaseTypeInfo* __start_exe_type_records[];
extern "C" __attribute__((weak)) ::Object::BaseTypeInfo* __stop_exe_type_records[];
#endif
