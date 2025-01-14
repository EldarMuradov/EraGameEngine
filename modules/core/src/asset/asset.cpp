// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#include "asset/asset.h"

#include "core/random.h"

namespace era_engine
{
	static random_number_generator rng = time(0);

	NODISCARD asset_handle asset_handle::generate()
	{
		return asset_handle(rng.randomUint64());
	}
}