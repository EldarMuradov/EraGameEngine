#include "physics/shape_component.h"
#include "physics/core/physics.h"
#include "physics/core/physics_utils.h"
#include "physics/body_component.h"
#include "physics/shape_utils.h"

#include "asset/model_asset.h"

#include <rttr/registration>

namespace era_engine::physics
{

    RTTR_REGISTRATION
    {
        using namespace rttr;
        rttr::registration::class_<ShapeComponent>("ShapeComponent")
            .constructor<>();

        rttr::registration::class_<BoxShapeComponent>("BoxShapeComponent")
            .constructor<>();

        rttr::registration::class_<SphereShapeComponent>("SphereShapeComponent")
            .constructor<>();

        rttr::registration::class_<CapsuleShapeComponent>("CapsuleShapeComponent")
            .constructor<>();

        rttr::registration::class_<TriangleMeshShapeComponent>("TriangleMeshShapeComponent")
            .constructor<>();

        rttr::registration::class_<ConvexMeshShapeComponent>("ConvexMeshShapeComponent")
            .constructor<>();
    }

    ShapeComponent::ShapeComponent(ref<Entity::EcsData> _data)
        : Component(_data)
    {
        register_shape();
    }

    ShapeComponent::~ShapeComponent()
    {
    }

    physx::PxShape* ShapeComponent::get_shape() const
    {
        return shape;
    }

    const physx::PxGeometry& ShapeComponent::get_geometry() const
    {
        ASSERT(shape != nullptr);
        return shape->getGeometry();
    }

    void era_engine::physics::ShapeComponent::release()
    {
        PhysicsHolder::physics_ref->remove_shape_from_entity_data(this);

        PhysicsUtils::get_body_component(component_data)->detach_shape(shape);

        PX_RELEASE(shape)
    }

    void ShapeComponent::register_shape()
    {
        PhysicsHolder::physics_ref->add_shape_to_entity_data(this);
    }

    physx::PxShape* ShapeComponent::create_shape()
    {
        return nullptr;
    }

    BoxShapeComponent::BoxShapeComponent(ref<Entity::EcsData> _data, const vec3& _extents)
        : ShapeComponent(_data), extents(_extents)
    {
    }

    BoxShapeComponent::~BoxShapeComponent()
    {
    }

    physx::PxShape* BoxShapeComponent::create_shape()
    {
        using namespace physx;

        ref<Physics> physics = PhysicsHolder::physics_ref;

        shape = physics->get_physics()->createShape(PxBoxGeometry(extents.x, extents.y, extents.z), *physics->get_default_material(), true);
        return shape;
    }

    SphereShapeComponent::SphereShapeComponent(ref<Entity::EcsData> _data, const float _radius)
        : ShapeComponent(_data), radius(_radius)
    {
    }

    SphereShapeComponent::~SphereShapeComponent()
    {
    }

    physx::PxShape* SphereShapeComponent::create_shape()
    {
        using namespace physx;

        ref<Physics> physics = PhysicsHolder::physics_ref;

        shape = physics->get_physics()->createShape(PxSphereGeometry(radius), *physics->get_default_material(), true);
        return shape;
    }

    CapsuleShapeComponent::CapsuleShapeComponent(ref<Entity::EcsData> _data, const float _radius, const float _half_height)
        : ShapeComponent(_data), radius(_radius), half_height(_half_height)
    {
    }

    CapsuleShapeComponent::~CapsuleShapeComponent()
    {
    }

    physx::PxShape* CapsuleShapeComponent::create_shape()
    {
        using namespace physx;

        ref<Physics> physics = PhysicsHolder::physics_ref;

        shape = physics->get_physics()->createShape(PxCapsuleGeometry(radius, half_height), *physics->get_default_material(), true);
        return shape;
    }

    TriangleMeshShapeComponent::TriangleMeshShapeComponent(ref<Entity::EcsData> _data, ref<mesh_asset> _asset, const vec3& _size)
        : ShapeComponent(_data), asset(_asset), size(_size)
    {
    }

    TriangleMeshShapeComponent::~TriangleMeshShapeComponent()
    {
    }

    physx::PxShape* TriangleMeshShapeComponent::create_shape()
    {
        using namespace physx;
        PxTriangleMesh* mesh = ShapeUtils::build_triangle_mesh(asset.get(), size);
        ref<Physics> physics = PhysicsHolder::physics_ref;

        shape = physics->get_physics()->createShape(PxTriangleMeshGeometry(mesh, PxMeshScale(createPxVec3(size))), *physics->get_default_material(), true);
        return shape;
    }

    ConvexMeshShapeComponent::ConvexMeshShapeComponent(ref<Entity::EcsData> _data, ref<mesh_asset> _asset, const vec3& _size)
        : ShapeComponent(_data), asset(_asset), size(_size)
    {
    }

    ConvexMeshShapeComponent::~ConvexMeshShapeComponent()
    {
    }

    physx::PxShape* ConvexMeshShapeComponent::create_shape()
    {
        using namespace physx;
        PxConvexMesh* mesh = ShapeUtils::build_convex_mesh(asset.get(), size);
        ref<Physics> physics = PhysicsHolder::physics_ref;

        shape = physics->get_physics()->createShape(PxConvexMeshGeometry(mesh, PxMeshScale(createPxVec3(size))), *physics->get_default_material(), true);
        return shape;
    }

}