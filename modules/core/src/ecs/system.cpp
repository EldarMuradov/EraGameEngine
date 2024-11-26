#include "ecs/system.h"
#include "ecs/world.h"
#include "ecs/entity.h"

#include <rttr/registration>

namespace era_engine
{

	RTTR_REGISTRATION
	{
		using namespace rttr;

		rttr::registration::class_<System>("System")
			.constructor<World*>();
	}

	System::System(World* _world)
		: world(_world)
	{
	}

	System::~System()
	{
	}

	void System::init()
	{
	}

	void System::update(float dt)
	{
	}

}