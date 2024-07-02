// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once

#include "raytracing.h"

namespace era_engine
{
	struct raytracing_instance_handle
	{
		uint32 instanceIndex;
	};

	struct raytracing_tlas
	{
		void initialize(raytracing_as_rebuild_mode rebuildMode = raytracing_as_rebuild);

		// Call these each frame to rebuild the structure
		void reset();
		raytracing_instance_handle instantiate(raytracing_object_type type, const trs& transform);
		void build(struct dx_command_list* cl);

		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> allInstances;

		raytracing_as_rebuild_mode rebuildMode;

		ref<dx_buffer> scratch;
		ref<dx_buffer> tlas;
	};
}