// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once

#include "core/math.h"
#include "core/camera.h"
#include "core/reflect.h"

#include "ecs/component.h"

#include "light_source.hlsli"

namespace era_engine
{
	class directional_light
	{
	public:

		void updateMatrices(const render_camera& camera);

		vec4 cascadeDistances;
		vec4 bias;
		vec4 blendDistances;
		vec4 shadowMapViewports[MAX_NUM_SUN_SHADOW_CASCADES];

		mat4 viewProjs[MAX_NUM_SUN_SHADOW_CASCADES];

		vec3 color;
		vec3 direction;

		float intensity;
		float negativeZOffset = 750.0f;

		uint32 shadowDimensions = 2048;
		uint32 numShadowCascades;

		bool stabilize;

		ERA_REFLECT
	};

	class PointLightComponent final : public Component
	{
	public:
		PointLightComponent() = default;
		PointLightComponent(ref<Entity::EcsData> _data, const vec3& _color, float _intensity, float _radius, bool _castsShadow = false, uint32 _shadowMapResolution = 2048);
		PointLightComponent(const PointLightComponent&) = default;
		virtual ~PointLightComponent();

		ERA_VIRTUAL_REFLECT(Component)
	public:
		vec3 color{};

		float intensity{};
		float radius{};

		uint32 shadowMapResolution{};

		bool castsShadow{};
	};

	class SpotLightComponent final : public Component
	{
	public:
		SpotLightComponent() = default;
		SpotLightComponent(ref<Entity::EcsData> _data, const vec3& _color, float _intensity, float _distance, float _innerAngle, float _outerAngle, bool _castsShadow = false, uint32 _shadowMapResolution = 2048);		
		SpotLightComponent(const SpotLightComponent&) = default;
		virtual ~SpotLightComponent();

		ERA_VIRTUAL_REFLECT(Component)

	public:
		vec3 color;

		float intensity;
		float distance;
		float innerAngle;
		float outerAngle;

		uint32 shadowMapResolution;

		bool castsShadow;
	};

	NODISCARD mat4 getSpotLightViewProjectionMatrix(const spot_light_cb& sl);
}