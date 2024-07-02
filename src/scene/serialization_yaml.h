// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once
#include "scene/scene.h"
#include "core/camera.h"
#include "rendering/main_renderer.h"

namespace era_engine
{
	void serializeSceneToYAMLFile(editor_scene& scene, const renderer_settings& rendererSettings);
	bool deserializeSceneFromYAMLFile(editor_scene& scene, renderer_settings& rendererSettings, std::string& environmentName);
	bool deserializeSceneFromCurrentYAMLFile(const fs::path& path, editor_scene& scene, renderer_settings& rendererSettings, std::string& environmentName);
}