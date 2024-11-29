#include "ecs/entity.h"
#include "ecs/world.h"
#include "ecs/component.h"

#include "ecs/editor/editor_scene.h"

#include <rttr/registration>

namespace era_engine
{

	Entity Entity::Null = Entity(nullptr);

	RTTR_REGISTRATION
	{
		using namespace rttr;

		rttr::registration::class_<IReleasable>("IReleasable")
			.constructor<>();

		rttr::registration::class_<Entity>("Entity")
			.constructor<>()
			.constructor<ref<Entity::EcsData>>();
	}

	void IReleasable::release()
	{
	}

	entt::registry& get_registry(World* _world)
	{
		return _world->get_registry();
	}

	Entity::Entity(const Entity& _entity) noexcept
		: internal_data(_entity.internal_data)
	{
	}

	Entity::Entity(Entity&& _entity) noexcept
	{
		internal_data = std::move(_entity.internal_data);
	}


	Entity::Entity(ref<EcsData> _data)
		: internal_data(_data)
	{
	}

	Entity::~Entity() noexcept
	{
	}

	Entity& Entity::operator=(const Entity& _entity)  noexcept
	{
		internal_data = _entity.internal_data;
		return *this;
	}

	Entity& Entity::operator=(Entity&& _entity) noexcept
	{
		internal_data = std::move(_entity.internal_data);
		return *this;
	}

	bool Entity::operator==(const Entity& _other) const
	{
		return internal_data == _other.internal_data;
	}

	bool Entity::operator!=(const Entity& _other) const
	{
		return !(*this == _other);
	}

	bool Entity::operator==(Entity::Handle _handle) const
	{
		return internal_data->entity_handle == _handle;
	}

	bool Entity::is_valid() const noexcept
	{
		return internal_data != nullptr &&
			   internal_data->entity_handle != Entity::NullHandle &&
			   internal_data->world != nullptr;
	}

	World* Entity::get_world() const
	{
		return internal_data->world;
	}

	Entity::Handle Entity::get_handle() const
	{
		return internal_data->entity_handle;
	}

	void EntityContainer::emplace_pair(Entity::Handle parent, Entity::Handle child)
	{
		lock l(sync);

		if (container.find(parent) == container.end())
		{
			container.emplace(std::make_pair(parent, EntityNode()));
		}

		container.at(parent).childs.push_back(child);
	}

	void EntityContainer::erase(Entity::Handle parent)
	{
		lock l(sync);

		if (container.find(parent) == container.end())
		{
			return;
		}

		container.erase(parent);
	}

	void EntityContainer::erase_pair(Entity::Handle parent, Entity::Handle child)
	{
		lock l(sync);

		if (container.find(parent) == container.end())
		{
			return;
		}

		auto iter = container.at(parent).childs.begin();
		const auto& end = container.at(parent).childs.end();

		for (; iter != end; ++iter)
		{
			if (*iter == child)
			{
				container.at(parent).childs.erase(iter);
			}
		}
	}

	std::vector<Entity::Handle> EntityContainer::get_childs(Entity::Handle parent)
	{
		if (container.find(parent) == container.end())
		{
			return std::vector<Entity::Handle>();
		}

		return container.at(parent).childs;
	}

	std::vector<Entity> EntityContainer::get_entity_childs(ref<World> world, Entity::Handle parent)
	{
		if (container.find(parent) == container.end())
		{
			return std::vector<Entity>();
		}

		std::vector<Entity> result = std::vector<Entity>(container.at(parent).childs.size());

		for (const Entity::Handle handle : container.at(parent).childs)
		{
			result.emplace_back(world->get_entity(handle));
		}

		return result;
	}

}