#include "ecs/rendering/mesh_component.h"
#include "ecs/world.h"

#include <rttr/registration>

namespace era_engine
{
	RTTR_REGISTRATION
	{
		using namespace rttr;
		rttr::registration::class_<MeshComponent>("MeshComponent")
			.constructor<ref<Entity::EcsData>, ref<multi_mesh>, bool>()
			.property("mesh", &MeshComponent::mesh)
			.property("is_hidden", &MeshComponent::is_hidden);
	}

	MeshComponent::MeshComponent(ref<Entity::EcsData> _data, ref<multi_mesh> _mesh, bool _is_hidden)
		: Component(_data), mesh(_mesh), is_hidden(_is_hidden)
	{
	}

	MeshComponent::~MeshComponent()
	{
	}

	Entity MeshUtils::load_entity_mesh_from_file(ref<World> world, const fs::path& filename, uint32 flags, mesh_load_callback cb)
	{
		ref<multi_mesh> mesh = loadMeshFromFile(filename, flags, cb);
		return world->create_entity().add_component<MeshComponent>(mesh, false);
	}

	Entity MeshUtils::load_entity_mesh_from_handle(ref<World> world, asset_handle handle, uint32 flags, mesh_load_callback cb)
	{
		ref<multi_mesh> mesh = loadMeshFromHandle(handle, flags, cb);
		return world->create_entity().add_component<MeshComponent>(mesh, false);
	}

	Entity MeshUtils::load_entity_mesh_from_file_async(ref<World> world, const fs::path& filename, uint32 flags, mesh_load_callback cb, job_handle parent_job)
	{
		ref<multi_mesh> mesh = loadMeshFromFileAsync(filename, flags, parent_job, cb);
		return world->create_entity().add_component<MeshComponent>(mesh, false);
	}

	Entity MeshUtils::load_entity_mesh_from_handle_async(ref<World> world, asset_handle handle, uint32 flags, mesh_load_callback cb, job_handle parent_job)
	{
		ref<multi_mesh> mesh = loadMeshFromHandleAsync(handle, flags, parent_job, cb);
		return world->create_entity().add_component<MeshComponent>(mesh, false);
	}

}