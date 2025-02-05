#include "animation/animation_system.h"

#include "core/cpu_profiling.h"
#include "core/memory.h"

#include "engine/engine.h"

#include "ecs/update_groups.h"

#include "application.h"

#include "ecs/rendering/mesh_component.h"
#include "ecs/base_components/transform_component.h"
#include "animation/animation.h"

#include <rttr/policy.h>
#include <rttr/registration>

namespace era_engine::animation
{

	RTTR_REGISTRATION
	{
		using namespace rttr;

		rttr::registration::class_<AnimationSystem>("AnimationSystem")
			.constructor<World*>()(policy::ctor::as_raw_ptr)
			.method("update", &AnimationSystem::update)(metadata("update_group", update_types::RENDER));
	}

	AnimationSystem::AnimationSystem(World* _world)
		: System(_world)
	{
	}

	AnimationSystem::~AnimationSystem()
	{
		allocator->reset();
	}

	void AnimationSystem::init()
	{
		allocator = get_transient_object<Allocator>();
	}

	void AnimationSystem::update(float dt)
	{
		MemoryMarker marker = allocator->get_marker();
		for (auto [entityHandle, anim, mesh, transform] : world->group(components_group<AnimationComponent, MeshComponent, TransformComponent>).each())
		{
			anim.update(mesh.mesh, *allocator, dt, &transform.transform);
			
			if (anim.draw_sceleton)
			{
				anim.draw_current_skeleton(mesh.mesh, transform.transform, &globalApp.ldrRenderPass);
			}
		}
		allocator->reset_to_marker(marker);
	}

}