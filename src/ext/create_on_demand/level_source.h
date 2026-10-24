#pragma once
// src/ext/create_on_demand/level_source.h -- ext.create_on_demand's level reader (NOT RETAIL)
//
// Video::ImageBuffer::_extLevelSource while the row is on (level_source.cpp has the rules,
// ImageBuffer.h the seam's contract).

#include <cstdint>

#include "video/ImageBuffer.h"

namespace IO { class Serializer; }

namespace Ext { namespace CreateOnDemand {

bool ReadLevels(IO::Serializer& ser, Video::ImageBuffer& image, int32_t count, Video::ImageBuffer::LevelArray& levels);

}}  // namespace Ext::CreateOnDemand
