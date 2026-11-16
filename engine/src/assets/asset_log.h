#pragma once

#include <core/log.h>

// The log area of the asset code (importers, the database, the packer, the asset manager).
#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)
