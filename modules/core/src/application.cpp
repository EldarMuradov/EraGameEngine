// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#include "application.h"

#include "core/random.h"
#include "core/color.h"
#include "core/imgui.h"
#include "core/threading.h"
#include "core/coroutine.h"
#include "core/event_queue.h"
#include "core/ejson_serializer.h"

#include "geometry/mesh_builder.h"

#include "dx/dx_texture.h"
#include "dx/dx_context.h"
#include "dx/dx_profiling.h"

#include "physics/physics.h"
#include "physics/ragdoll.h"
#include "physics/vehicle.h"

#include "rendering/outline.h"
#include "rendering/mesh_shader.h"
#include "rendering/shadow_map.h"
#include "rendering/debug_visualization.h"

#include "scene/scene_rendering.h"

#include "audio/audio.h"

#include "terrain/terrain.h"
#include "terrain/heightmap_collider.h"
#include "terrain/proc_placement.h"
#include "terrain/grass.h"
#include "terrain/water.h"
#include "terrain/tree.h"

#include "animation/skinning.h"

#include "asset/model_asset.h"
#include "asset/file_registry.h"

#include "px/core/px_tasks.h"
#include "px/core/px_aggregate.h"

#include "px/physics/px_collider_component.h"
#include "px/physics/px_joint.h"
#include "px/physics/px_character_controller_component.h"
#include "px/physics/px_soft_body.h"

#include "px/features/px_ragdoll.h"
#include "px/features/px_particles.h"
#include "px/features/cloth/px_clothing_factory.h"
#include "px/features/px_vehicle_component.h"

#include "px/blast/px_blast_destructions.h"

#include "ai/navigation.h"
#include "ai/navigation_component.h"

#include "scripting/script.h"
#include "scripting/native_scripting_linker.h"

#include "ecs/visitor/visitor.h"
#include "ecs/world.h"
#include "ecs/entity.h"
#include "ecs/base_components/base_components.h"
#include "ecs/rendering/mesh_component.h"
#include "ecs/rendering/world_renderer.h"
#include "ecs/reflection_utils.h"
#include "ecs/editor/entity_editor_utils.h"

namespace era_engine
{
	static raytracing_object_type defineBlasFromMesh(const ref<multi_mesh>& mesh)
	{
		if (dxContext.featureSupport.raytracing())
		{
			raytracing_blas_builder blasBuilder;
			std::vector<ref<pbr_material>> raytracingMaterials;

			for (auto& sm : mesh->submeshes)
			{
				blasBuilder.push(mesh->mesh.vertexBuffer, mesh->mesh.indexBuffer, sm.info);
				raytracingMaterials.push_back(sm.material);
			}

			ref<raytracing_blas> blas = blasBuilder.finish();
			raytracing_object_type type = pbr_raytracer::defineObjectType(blas, raytracingMaterials);
			return type;
		}
		else
		{
			return {};
		}
	}

	void addRaytracingComponentAsync(Entity entity, ref<multi_mesh> mesh)
	{
		struct add_ray_tracing_data
		{
			Entity entity;
			ref<multi_mesh> mesh;
		};

		add_ray_tracing_data data = { entity, mesh };

		lowPriorityJobQueue.createJob<add_ray_tracing_data>([](add_ray_tracing_data& data, job_handle)
			{
				data.mesh->loadJob.waitForCompletion();

				struct create_component_data
				{
					Entity entity;
					raytracing_object_type blas;
				};

				create_component_data createData = { data.entity, defineBlasFromMesh(data.mesh) };

				mainThreadJobQueue.createJob<create_component_data>([](create_component_data& data, job_handle)
					{
						data.entity.add_component<RaytraceComponent>(data.blas);
					}, createData).submitNow();

			}, data).submitNow();
	}

	struct update_scripting_data
	{
		float deltaTime{};
		ref<dotnet::enative_scripting_linker> core;
		ref<World> world;
		const user_input& input;
	};

	spin_lock scriptingSync;

	void updatePhysXCallbacksAndScripting(ref<World> current_world, ref<dotnet::enative_scripting_linker> core, float dt, const user_input& in)
	{
		update_scripting_data data = { dt, core, current_world, in };

		highPriorityJobQueue.createJob<update_scripting_data>([](update_scripting_data& data, job_handle)
			{
				try
				{
					shared_spin_lock lock{ scriptingSync };
					{
						const auto& physicsRef = physics::physics_holder::physicsRef;

						{
							CPU_PROFILE_BLOCK("PhysX collision events step");

							{
								physics::collision_handling_data collData;
								while (physicsRef->collisionQueue.try_dequeue(collData))
								{
									data.core->handle_coll(collData.id1, collData.id2);
								}
							}

							{
								physics::collision_handling_data collData;
								while (physicsRef->collisionExitQueue.try_dequeue(collData))
								{
									data.core->handle_exit_coll(collData.id1, collData.id2);
								}
							}
						}
					}
				}
				catch(std::exception& ex)
				{
					LOG_ERROR(ex.what());
				}
			}, data).submitNow();

		updateScripting(data);

		{
			CPU_PROFILE_BLOCK(".NET 8 Input sync step");
			core->handleInput(reinterpret_cast<uintptr_t>(&in.keyboard[0]));
		}

		//const auto& nav_objects = data.scene.group(component_group<ai::navigation_component, transform_component>);

		//if (nav_objects.size())
		//{
		//	struct nav_process_data
		//	{
		//		decltype(nav_objects) objects;
		//	};

		//	nav_process_data nav_data{ nav_objects };

		//	lowPriorityJobQueue.createJob<nav_process_data>([](nav_process_data& data, job_handle)
		//		{
		//			CPU_PROFILE_BLOCK("Navigation step");
		//			for (auto [entityHandle, nav, transform] : data.objects.each())
		//			{
		//				nav.processPath();
		//			}
		//		}, nav_data).submitNow();
		//}
	}

	void updateScripting(update_scripting_data& data)
	{
		CPU_PROFILE_BLOCK(".NET 8.0 scripting step");
		if (!data.world->registry.size())
			return;

		auto group = data.world->group(components_group<ecs::ScriptsComponent, TransformComponent>);

		if (group.empty())
			return;

		for (auto [entityHandle, script, transform] : group.each())
		{
			const auto& mat = trsToMat4(transform.transform);
			constexpr size_t mat_size = 16;
			float* ptr = new float[mat_size];
			for (size_t i = 0; i < mat_size; i++)
				ptr[i] = mat.m[i];
			data.core->process_trs(reinterpret_cast<uintptr_t>(ptr), (int)entityHandle);
			//delete[] ptr; // CoreRT/CLR clear's it
		}
		data.core->update(data.deltaTime);
	}

	static void initializeAnimationComponentAsync(Entity entity, ref<multi_mesh> mesh)
	{
		struct add_animation_data
		{
			Entity entity;
			ref<multi_mesh> mesh;
		};

		add_animation_data data = { entity, mesh };

		mainThreadJobQueue.createJob<add_animation_data>([](add_animation_data& data, job_handle job)
			{
				data.mesh->loadJob.waitForCompletion();
				data.entity.get_component<animation::AnimationComponent>().initialize(data.mesh->skeleton.clips);
			}, data).submitNow();
	}

	void application::loadCustomShaders()
	{
		if (dxContext.featureSupport.meshShaders())
		{
			initializeMeshShader();
		}
	}

	static void initTestScene(ref<World> game_world)
	{
		Entity entity1 = game_world->create_entity("Entity1");
		Entity entity2 = game_world->create_entity("Entity2");

		auto defaultmat = createPBRMaterialAsync({ "", "" });
		defaultmat->shader = pbr_material_shader_double_sided;

		mesh_builder builder;

		auto sphereMesh = make_ref<multi_mesh>();
		builder.pushSphere({ });
		sphereMesh->submeshes.push_back({ builder.endSubmesh(), {}, trs::identity, defaultmat });

		auto boxMesh = make_ref<multi_mesh>();
		builder.pushBox({ vec3(0.f), vec3(1.f, 1.f, 2.f) });
		boxMesh->submeshes.push_back({ builder.endSubmesh(), {}, trs::identity, defaultmat });

		pbr_material_desc defaultPlaneMatDesc;
		defaultPlaneMatDesc.albedo = getAssetPath("/resources/assets/uv.jpg");
		defaultPlaneMatDesc.normal = "";
		defaultPlaneMatDesc.roughness = "";
		defaultPlaneMatDesc.uvScale = 15.0f;
		defaultPlaneMatDesc.metallicOverride = 0.35f;
		defaultPlaneMatDesc.roughnessOverride = 0.01f;

		auto defaultPlaneMat = createPBRMaterialAsync(defaultPlaneMatDesc);

		auto groundMesh = make_ref<multi_mesh>();
		builder.pushBox({ vec3(0.f), vec3(30.f, 4.f, 30.f) });
		groundMesh->submeshes.push_back({ builder.endSubmesh(), {}, trs::identity, defaultPlaneMat });

		Entity entity3 = game_world->create_entity("Entity3").add_component<MeshComponent>(sphereMesh, false);
		sphereMesh->mesh = builder.createDXMesh();

		if (auto mesh = loadAnimatedMeshFromFileAsync(getAssetPath("/resources/assets/veribot/source/VERIBOT_final.fbx")))
		{
			auto& en = game_world->create_entity("Veribot")
				.add_component<animation::AnimationComponent>()
				.add_component<MeshComponent>(mesh);

			TransformComponent& transform_component = en.get_component<TransformComponent>();
			transform_component.type = TransformComponent::DYNAMIC;
			transform_component.transform.position = vec3(5.0f);

			initializeAnimationComponentAsync(en, mesh);
			addRaytracingComponentAsync(en, mesh);
		}

		if (auto mesh = loadMeshFromFileAsync(getAssetPath("/resources/assets/Sponza/sponza.obj")))
		{
			auto& sponza = game_world->create_entity("Sponza")
				.add_component<MeshComponent>(mesh);

			TransformComponent& transform_component = sponza.get_component<TransformComponent>();
			transform_component.transform.position = vec3(5.0f, -3.75f, 35.0f);
			transform_component.transform.scale = vec3(0.01f);

			addRaytracingComponentAsync(sponza, mesh);
		}

		Entity plane = game_world->create_entity("Platform")
			.add_component<TransformComponent>(vec3(10, -9.f, 0.f), quat(vec3(1.f, 0.f, 0.f), deg2rad(0.f)), vec3(5.0f, 1.0f, 5.0f))
			//.addComponent<physics::px_plane_collider_component>(vec3(0.f, -5.0, 0.0f))
			.add_component<MeshComponent>(groundMesh);

		{
			TransformComponent& transform_component = plane.get_component<TransformComponent>();
			transform_component.transform.position = vec3(10.0f, -9.0f, 0.0f);
			transform_component.transform.scale = vec3(5.0f, 1.0f, 5.0f);
		}

		auto chainMesh = make_ref<multi_mesh>();

		groundMesh->mesh =
			boxMesh->mesh =
			sphereMesh->mesh =
			chainMesh->mesh =
			builder.createDXMesh();

		//auto& terrain = game_world->create_entity("Terrain")
		//	.add_component<TerrainComponent>(10u, 64.0f, 50.f, defaultPlaneMat, defaultPlaneMat, defaultPlaneMat)
		//	.add_component<GrassComponent>();
		//{
		//	TransformComponent& transform_component = terrain.get_component<TransformComponent>();
		//	transform_component.transform.position = vec3(0.0f, -64.0f, 0.0f);
		//}	

		//auto& water = game_world->create_entity("Water")
		//	.add_component<WaterComponent>();
		//{
		//	TransformComponent& transform_component = water.get_component<TransformComponent>();
		//	transform_component.transform.position = vec3(-3.920f, -48.689f, -85.580f);
		//	transform_component.transform.scale = vec3(90.0f);
		//}
#if 0
		if (auto treeMesh = loadTreeMeshFromFileAsync(getAssetPath("/resources/assets/tree/source/tree.fbx")))
		{
			auto tree = scene.createEntity("Tree")
				.addComponent<transform_component>(vec3(0.0, 0.0f, 0.0f), quat::identity, 0.1f)
				.addComponent<mesh_component>(treeMesh)
				.addComponent<tree_component>();
		}
#endif

#ifndef ERA_RUNTIME
		{

			//model_asset ass = load3DModelFromFile(getAssetPath("/resources/assets/sphere.fbx"));
			//auto& px_sphere_entt = scene.createEntity("SpherePX", (entity_handle)60)
			//	.addComponent<transform_component>(vec3(-10.0f, 5.0f, -3.0f), quat(vec3(0.f, 0.f, 0.f), deg2rad(1.f)), vec3(1.f))
			//	.addComponent<mesh_component>(sphereMesh)
				//.addComponent<physics::px_convex_mesh_collider_component>(&(ass.meshes[0]))
				//.addComponent<physics::px_triangle_mesh_collider_component>(&(ass.meshes[0]))
				//.addComponent<physics::px_bounding_box_collider_component>(&(ass.meshes[0]))
			//	.addComponent<physics::px_sphere_collider_component>(1.0f)
			//	.addComponent<physics::px_dynamic_body_component>();
			//px_sphere_entt.getComponent<physics::px_dynamic_body_component>().setMass(1000.f);
			//px_sphere_entt.getComponent<physics::px_dynamic_body_component>().setCCD(true);
			//px_sphere_entt.getComponent<physics::px_dynamic_body_component>().setFilterMask(1, 2);
			//sphere = px_sphere_entt.handle;

			//auto px_sphere1 = &scene.createEntity("SpherePX1", (entity_handle)59)
			//	.addComponent<transform_component>(vec3(5, 155.f, 5), quat(vec3(0.f, 0.f, 0.f), deg2rad(1.f)), vec3(5.f))
			//	.addComponent<mesh_component>(sphereMesh)
			//	.addComponent<physics::px_sphere_collider_component>(5.0f)
			//	.addComponent<physics::px_dynamic_body_component>();
			//px_sphere1->getComponent<physics::px_dynamic_body_component>().setCCD(true);
			//px_sphere1->getComponent<physics::px_dynamic_body_component>().setMass(500.0f);
			//px_sphere1->getComponent<physics::px_dynamic_body_component>().setFilterMask(2, 1 | 3);

			//px_sphere1->getComponent<physics::px_dynamic_body_component>().setFilterMask(3, 1);
			//if (auto mesh = loadAnimatedMeshFromFileAsync(getAssetPath("/resources/assets/veribot/source/VERIBOT_final.fbx")))
			//{
			//	auto& en = scene.createEntity("Veribot", (entity_handle)15)
			//		.addComponent<transform_component>(vec3(0.f), quat::identity)
			//		.addComponent<animation_component>()
			//		.addComponent<dynamic_transform_component>()
			//		.addComponent<mesh_component>(mesh);
			//	initializeAnimationComponentAsync(en, mesh);
			//	addRaytracingComponentAsync(en, mesh);
			//}

			/*if (auto mesh = loadAnimatedMeshFromFileAsync(getAssetPath("/resources/assets/resident-evil-tyrant/source/UmodelExport.fbx")))
			{
				auto& en = scene.createEntity("Ragdoll")
					.addComponent<transform_component>(vec3(0.0f), quat::identity, vec3(0.1f))
					.addComponent<animation::animation_component>()
					.addComponent<dynamic_transform_component>()
					.addComponent<mesh_component>(mesh)
					.addComponent<physics::px_ragdoll_component>(vec3(0.0f));
				initializeAnimationComponentAsync(en, mesh);
				addRaytracingComponentAsync(en, mesh);

				struct ragdoll_load_data
				{ 
					entity_handle handle;
					escene& scene;
				} ragdoll_data = {en.handle, scene};

				mainThreadJobQueue.createJob<ragdoll_load_data>([](ragdoll_load_data& data, job_handle handle) 
					{
						eentity rag{ data.handle, &data.scene.registry };

						rag.getComponent<physics::px_ragdoll_component>().initRagdoll(make_ref<animation::animation_skeleton>(rag.getComponent<mesh_component>().mesh->skeleton));
					},
					ragdoll_data).submitAfter(mesh->loadJob);
			}*/


#if PX_BLAST_ENABLE

			//{
			//	if (auto mesh = loadMeshFromFileAsync(getAssetPath("/resources/assets/obj/untitled.obj")))
			//	{
			//		model_asset ass = load3DModelFromFile(getAssetPath("/resources/assets/obj/untitled.obj"));

			//		auto& px_blast_entt1 = scene.createEntity("BlastPXTest")
			//			.addComponent<transform_component>(vec3(0.0f, 5.0f, 0.0f), quat::identity, vec3(1.0f))
			//			.addComponent<mesh_component>(mesh);

			//		physics::fracture fracture;
			//		auto ref = make_ref<submesh_asset>(ass.meshes[0].submeshes[0]);
			//		unsigned int seed = 7249U;
			//		fracture.fractureGameObject(ref, px_blast_entt1, physics::anchor::anchor_none, seed, 15, defaultmat, defaultmat, 20.0f, 1.0f);
			//		scene.deleteEntity(px_blast_entt1.handle);
			//	}
			//}

#endif

			//{
			//	model_asset ass = load3DModelFromFile(getAssetPath("/resources/assets/box.fbx"));
			//	auto& px_sphere_entt1 = scene.createEntity("BoxPXTest")
			//		.addComponent<transform_component>(vec3(0, 0.f, 0.f), quat(vec3(0.f, 0.f, 0.f), deg2rad(1.f)), vec3(1.0f))
			//		.addComponent<mesh_component>(boxMesh);

			//	physics::fracture fracture;
			//	auto ref = make_ref<submesh_asset>(ass.meshes[0].submeshes[0]);
			//	unsigned int seed = 7249U;
			//	fracture.fractureGameObject(ref, px_sphere_entt1, physics::anchor::anchor_bottom, seed, 50, defaultmat, defaultmat, 0.75f, 0.1f);
			//	scene.deleteEntity(px_sphere_entt1.handle);
			//}

			//auto& vehicle = scene.createEntity("Vehicle")
			//	.addComponent<transform_component>(vec3(2.f, 10.0f, 5.0f), quat::identity, vec3(1.f))
			//	.addComponent<physics::px_4_wheels_vehicle_component>(vec3(2.f));

			//vehicle.getComponent<physics::px_4_wheels_vehicle_component>().setupVehicle();

			//auto soft_body = &scene.createEntity("SoftBody")
			//	.addComponent<transform_component>(vec3(0.f), quat::identity, vec3(1.f))
			//	.addComponent<physics::px_soft_body_component>();

			//px_sphere1->addChild(*px_sphere);

			//{
			//	for (int i = 0; i < 10; i++)
			//	{
			//		for (int j = 0; j < 10; j++)
			//		{
			//			for (int k = 0; k < 10; k++)
			//			{
			//				auto sphr = &scene.createEntity((std::to_string(i) + std::to_string(j) + std::to_string(k)).c_str())
			//					.addComponent<transform_component>(vec3(2.0f * i, 5 + 2.0f * j + 5, 2.0f * k), quat(vec3(0.f, 0.f, 0.f), deg2rad(1.f)), vec3(1.f))
			//					.addComponent<mesh_component>(sphereMesh)
			//					.addComponent<physics::px_sphere_collider_component>(1.0f)
			//					.addComponent<physics::px_dynamic_body_component>();
			//			}
			//		}
			//	}
			//}

			//auto px_cct = &scene.createEntity("CharacterControllerPx")
			//	.addComponent<transform_component>(vec3(20.f, 5, -5.f), quat(vec3(0.f, 0.f, 0.f), deg2rad(1.f)), vec3(1.f))
			//	.addComponent<physics::px_box_cct_component>(1.0f, 0.5f, 1.0f);

			//auto& particles = scene.createEntity("ParticlesPX")
			//	.addComponent<transform_component>(vec3(0.f, 10.0f, 0.0f), quat::identity, vec3(1.f))
			//	.addComponent<physics::px_particles_component>(vec3(0.f, 10.0f, 0.0f), 10, 10, 10, false);

			//auto& cloth = scene.createEntity("ClothPX")
			//	.addComponent<transform_component>(vec3(0.f, 15.0f, 0.0f), eulerToQuat(vec3(0.0f, 0.0f, 0.0f)), vec3(1.f))
			//	.addComponent<physics::px_cloth_component>(100, 100, vec3(0.f, 15.0f, 0.0f));
		}
#endif
#if 0
		fireParticleSystem.initialize(10000, 50.f, getAssetPath("/resources/assets/particles/fire_explosion.png"), 6, 6, vec3(0, 1, 30));
		smokeParticleSystem.initialize(10000, 500.f, getAssetPath("/resources/assets/particles/smoke1.tif"), 5, 5, vec3(0, 1, 15));
		//boidParticleSystem.initialize(10000, 2000.f);
		debrisParticleSystem.initialize(10000);
#endif
	}

	void application::initialize(main_renderer* renderer, editor_panels* editorPanels)
	{
		this->renderer = renderer;

		world_scene = make_ref<EditorScene>();

		if (dxContext.featureSupport.raytracing())
		{
			raytracingTLAS.initialize();
		}

		world_scene->camera.initializeIngame(vec3(0.f, 1.f, 5.f), quat::identity, deg2rad(70.f), 0.2f);
		world_scene->editor_camera.initializeIngame(vec3(0.f, 1.f, 5.f), quat::identity, deg2rad(70.f), 0.2f);
		world_scene->environment.setFromTexture(getAssetPath("/resources/assets/sky/sunset_in_the_chalk_quarry_4k.hdr"));
		world_scene->environment.lightProbeGrid.initialize(vec3(-20.f, -1.f, -20.f), vec3(40.f, 20.f, 40.f), 1.5f);

		physics::physics_holder::physicsRef = make_ref<physics::px_physics_engine>();

		escene& scene = this->scene.getCurrentScene();

		ref<World> world = world_scene->get_current_world();

		{
			CPU_PROFILE_BLOCK("Binding for scripting initialization");
			linker = make_ref<era_engine::dotnet::enative_scripting_linker>(&this->scene.runtimeScene);
			linker->init();
		}

#ifndef ERA_RUNTIME

		editor.initialize(world_scene, renderer, editorPanels);
		editor.app = this;

#else

		rt.initialize(&this->scene, renderer);

#endif

		initTestScene(world);

		world_scene->sun.direction = normalize(vec3(-0.6f, -1.f, -0.3f));
		world_scene->sun.color = vec3(1.f, 0.93f, 0.76f);
		world_scene->sun.intensity = 11.1f;
		world_scene->sun.numShadowCascades = 3;
		world_scene->sun.shadowDimensions = 2048;
		world_scene->sun.cascadeDistances = vec4(9.f, 25.f, 50.f, 10000.f);
		world_scene->sun.bias = vec4(0.000588f, 0.000784f, 0.000824f, 0.0035f);
		world_scene->sun.blendDistances = vec4(5.f, 10.f, 10.f, 10.f);
		world_scene->sun.stabilize = true;

		for (uint32 i = 0; i < NUM_BUFFERED_FRAMES; ++i)
		{
			pointLightBuffer[i] = createUploadBuffer(sizeof(point_light_cb), 512, 0);
			spotLightBuffer[i] = createUploadBuffer(sizeof(spot_light_cb), 512, 0);
			decalBuffer[i] = createUploadBuffer(sizeof(pbr_decal_cb), 512, 0);

			spotLightShadowInfoBuffer[i] = createUploadBuffer(sizeof(spot_shadow_info), 512, 0);
			pointLightShadowInfoBuffer[i] = createUploadBuffer(sizeof(point_shadow_info), 512, 0);

			SET_NAME(pointLightBuffer[i]->resource, "Point lights");
			SET_NAME(spotLightBuffer[i]->resource, "Spot lights");
			SET_NAME(decalBuffer[i]->resource, "Decals");

			SET_NAME(spotLightShadowInfoBuffer[i]->resource, "Spot light shadow infos");
			SET_NAME(pointLightShadowInfoBuffer[i]->resource, "Point light shadow infos");
		}

		stackArena.initialize();

		physics::physics_holder::physicsRef->start();
	}

#if 0
	bool editFireParticleSystem(fire_particle_system& particleSystem)
	{
		bool result = false;
		if (ImGui::BeginTree("Fire particles"))
		{
			if (ImGui::BeginProperties())
			{
				result |= ImGui::PropertySlider("Emit rate", particleSystem.emitRate, 0.f, 1000.f);
				ImGui::EndProperties();
			}

			result |= ImGui::Spline("Size over lifetime", ImVec2(200, 200), particleSystem.settings.sizeOverLifetime);
			ImGui::Separator();
			result |= ImGui::Spline("Atlas progression over lifetime", ImVec2(200, 200), particleSystem.settings.atlasProgressionOverLifetime);
			ImGui::Separator();
			result |= ImGui::Spline("Intensity over lifetime", ImVec2(200, 200), particleSystem.settings.intensityOverLifetime);

			ImGui::EndTree();
		}
		return result;
	}

	bool editBoidParticleSystem(boid_particle_system& particleSystem)
	{
		bool result = false;
		if (ImGui::BeginTree("Boid particles"))
		{
			if (ImGui::BeginProperties())
			{
				result |= ImGui::PropertySlider("Emit rate", particleSystem.emitRate, 0.f, 5000.f);
				result |= ImGui::PropertySlider("Emit radius", particleSystem.settings.radius, 5.f, 100.f);
				ImGui::EndProperties();
			}

			ImGui::EndTree();
		}
		return result;
	}

#endif

	void application::resetRenderPasses()
	{
		opaqueRenderPass.reset();
		transparentRenderPass.reset();
		ldrRenderPass.reset();
		sunShadowRenderPass.reset();
		computePass.reset();

		for (uint32 i = 0; i < arraysize(spotShadowRenderPasses); ++i)
		{
			spotShadowRenderPasses[i].reset();
		}

		for (uint32 i = 0; i < arraysize(pointShadowRenderPasses); ++i)
		{
			pointShadowRenderPasses[i].reset();
		}
	}

	void application::submitRendererParams(uint32 numSpotLightShadowPasses, uint32 numPointLightShadowPasses)
	{
		{
			CPU_PROFILE_BLOCK("Sort render passes");

			opaqueRenderPass.sort();
			transparentRenderPass.sort();
			ldrRenderPass.sort();

			for (uint32 i = 0; i < sunShadowRenderPass.numCascades; ++i)
			{
				sunShadowRenderPass.cascades[i].sort();
			}

			for (uint32 i = 0; i < numSpotLightShadowPasses; ++i)
			{
				spotShadowRenderPasses[i].sort();
			}

			for (uint32 i = 0; i < numPointLightShadowPasses; ++i)
			{
				pointShadowRenderPasses[i].sort();
			}
		}

		renderer->submitRenderPass(&opaqueRenderPass);
		renderer->submitRenderPass(&transparentRenderPass);
		renderer->submitRenderPass(&ldrRenderPass);
		renderer->submitComputePass(&computePass);

		renderer->submitShadowRenderPass(&sunShadowRenderPass);

		for (uint32 i = 0; i < numSpotLightShadowPasses; ++i)
		{
			renderer->submitShadowRenderPass(&spotShadowRenderPasses[i]);
		}

		for (uint32 i = 0; i < numPointLightShadowPasses; ++i)
		{
			renderer->submitShadowRenderPass(&pointShadowRenderPasses[i]);
		}
	}

	static void updateTestScene(float dt, ref<World> world, const user_input& input)
	{
		// Particles
#if 0
		computePass.dt = dt;
		//computePass.updateParticleSystem(&boidParticleSystem);
		computePass.updateParticleSystem(&fireParticleSystem);
		computePass.updateParticleSystem(&smokeParticleSystem);
		computePass.updateParticleSystem(&debrisParticleSystem);

		//boidParticleSystem.render(&transparentRenderPass);
		fireParticleSystem.render(&transparentRenderPass);
		smokeParticleSystem.render(&transparentRenderPass);
		debrisParticleSystem.render(&transparentRenderPass);
#endif
	}

	void application::update(const user_input& input, float dt)
	{
		resetRenderPasses();

		stackArena.reset();

#ifndef ERA_RUNTIME

		bool objectDragged = editor.update(input, &ldrRenderPass, dt);
		editor.render(&ldrRenderPass, dt);

		render_camera& camera = this->world_scene->is_pausable() ? world_scene->editor_camera : world_scene->camera;

#else

		bool objectDragged = false;

		render_camera& camera = world_scene->camera;

		rt.update();

#endif

		directional_light& sun = world_scene->sun;
		pbr_environment& environment = world_scene->environment;

		environment.update(sun.direction);
		sun.updateMatrices(camera);
		setAudioListener(camera.position, camera.rotation, vec3(0.f));
		environment.lightProbeGrid.visualize(&opaqueRenderPass);

		ref<World> world = world_scene->get_current_world();

		float unscaledDt = dt;
		dt *= world_scene->get_timestep_scale();

		bool running = world_scene->is_pausable();

#if PX_VEHICLE
		{
			//if (running)
			//{
			//	CPU_PROFILE_BLOCK("PhysX vehicles step");
			//	for (auto [entityHandle, vehicle, trs] : scene.group(component_group<physics::px_vehicle_base_component, transform_component>).each())
			//	{
			//		vehicleStep(&vehicle, &trs, dt);
			//	}

			//	for (auto [entityHandle, vehicle, trs] : scene.group(component_group<physics::px_4_wheels_vehicle_component, transform_component>).each())
			//	{
			//		vehicleStep(&vehicle, &trs, dt);
			//	}

			//	for (auto [entityHandle, vehicle, trs] : scene.group(component_group<physics::px_tank_vehicle_component, transform_component>).each())
			//	{
			//		vehicleStep(&vehicle, &trs, dt);
			//	}
			//}
		}
#endif

		if (running)
		{
			physics::physics_holder::physicsRef->update(dt);
			updatePhysXCallbacksAndScripting(world, linker, dt, input);
		}

#if PX_VEHICLE
		{
			//if (running)
			//{
			//	CPU_PROFILE_BLOCK("PhysX vehicles post step");
			//	for (auto [entityHandle, vehicle, trs] : scene.group(component_group<physics::px_vehicle_base_component, transform_component>).each())
			//	{
			//		vehiclePostStep(&vehicle, dt);
			//	}

			//	for (auto [entityHandle, vehicle, trs] : scene.group(component_group<physics::px_4_wheels_vehicle_component, transform_component>).each())
			//	{
			//		vehiclePostStep(&vehicle, dt);
			//	}

			//	for (auto [entityHandle, vehicle, trs] : scene.group(component_group<physics::px_tank_vehicle_component, transform_component>).each())
			//	{
			//		vehiclePostStep(&vehicle, dt);
			//	}
			//}
		}
#endif

#ifndef ERA_RUNTIME

		Entity selectedEntity = editor.selectedEntity;

#else

		Entity selectedEntity = Entity::Null;

#endif


#if PX_GPU_BROAD_PHASE
		{
			CPU_PROFILE_BLOCK("PhysX GPU clothes render step");
			for (auto [entityHandle, cloth, render] : scene.group(component_group<physics::px_cloth_component, physics::px_cloth_render_component>).each())
			{
				cloth.update(false, &ldrRenderPass);
			}
		}

		{
			CPU_PROFILE_BLOCK("PhysX GPU particles render step");
			for (auto [entityHandle, particles, render] : scene.group(component_group<physics::px_particles_component, physics::px_particles_render_component>).each())
			{
				particles.update(true, &ldrRenderPass);
			}
		}

#endif
#if PX_BLAST_ENABLE
		//{
		//	CPU_PROFILE_BLOCK("PhysX blast chuncks");
		//	for (auto [entityHandle, cgm, _] : scene.group(component_group<physics::chunk_graph_manager, transform_component>).each())
		//	{
		//		cgm.update();
		//	}
		//}
#endif

		updateTestScene(dt, world, input);

		{
			for (auto [entityHandle, anim, mesh, transform] : world->group(components_group<animation::AnimationComponent, MeshComponent, TransformComponent>, components_group<physics::px_ragdoll_component>).each())
			{
				anim.update(mesh.mesh, stackArena, dt, &transform.transform);

				if (anim.draw_sceleton)
					anim.draw_current_skeleton(mesh.mesh, transform.transform, &ldrRenderPass);
			}

			for (auto [entityHandle, transform, terrain] : world->group(components_group<TransformComponent, TerrainComponent>).each())
			{
				terrain.update();
			}

			scene_lighting lighting;
			lighting.spotLightBuffer = spotLightBuffer[dxContext.bufferedFrameID];
			lighting.pointLightBuffer = pointLightBuffer[dxContext.bufferedFrameID];
			lighting.spotLightShadowInfoBuffer = spotLightShadowInfoBuffer[dxContext.bufferedFrameID];
			lighting.pointLightShadowInfoBuffer = pointLightShadowInfoBuffer[dxContext.bufferedFrameID];
			lighting.spotShadowRenderPasses = spotShadowRenderPasses;
			lighting.pointShadowRenderPasses = pointShadowRenderPasses;
			lighting.maxNumSpotShadowRenderPasses = arraysize(spotShadowRenderPasses);
			lighting.maxNumPointShadowRenderPasses = arraysize(pointShadowRenderPasses);

			render_world(camera, world, stackArena, Entity::NullHandle, sun, lighting, !renderer->settings.cacheShadowMap,
				&opaqueRenderPass, &transparentRenderPass, &ldrRenderPass, &sunShadowRenderPass, &computePass, unscaledDt);

			renderer->setSpotLights(spotLightBuffer[dxContext.bufferedFrameID], world->number_of_components_of_type<SpotLightComponent>(), spotLightShadowInfoBuffer[dxContext.bufferedFrameID]);
			renderer->setPointLights(pointLightBuffer[dxContext.bufferedFrameID], world->number_of_components_of_type<PointLightComponent>(), pointLightShadowInfoBuffer[dxContext.bufferedFrameID]);
		
			if (decals.size())
			{
				updateUploadBufferData(decalBuffer[dxContext.bufferedFrameID], decals.data(), (uint32)(sizeof(pbr_decal_cb) * decals.size()));
				renderer->setDecals(decalBuffer[dxContext.bufferedFrameID], (uint32)decals.size(), decalTexture);
			}
#ifndef ERA_RUNTIME

			if (selectedEntity != Entity::Null)
			{
				TransformComponent& transform = selectedEntity.get_component<TransformComponent>();

				if (PointLightComponent* pl = selectedEntity.get_component_if_exists<PointLightComponent>())
				{
					renderWireSphere(transform.transform.position, pl->radius, vec4(pl->color, 1.f), &ldrRenderPass);
				}
				else if (SpotLightComponent* sl = selectedEntity.get_component_if_exists<SpotLightComponent>())
				{
					renderWireCone(transform.transform.position, transform.transform.rotation * vec3(0.f, 0.f, -1.f),
						sl->distance, sl->outerAngle * 2.f, vec4(sl->color, 1.f), &ldrRenderPass);
				}
				else if (physics::px_capsule_cct_component* cct = selectedEntity.get_component_if_exists<physics::px_capsule_cct_component>())
				{
					renderWireCapsule(transform.transform.position, transform.transform.position + vec3(0, cct->height, 0), cct->radius, vec4(0.107f, 1.0f, 0.0f, 1.0f), &ldrRenderPass);
				}
				else if (physics::px_box_cct_component* cct = selectedEntity.get_component_if_exists<physics::px_box_cct_component>())
				{
					renderWireBox(transform.transform.position, vec3(cct->halfSideExtent, cct->halfHeight * 2, cct->halfSideExtent), transform.transform.rotation, vec4(0.107f, 1.0f, 0.0f, 1.0f), &ldrRenderPass);
				}
			}
#endif
			submitRendererParams(lighting.numSpotShadowRenderPasses, lighting.numPointShadowRenderPasses);
		}

		animation::performSkinning(&computePass);

		if (dxContext.featureSupport.raytracing())
		{
			raytracingTLAS.reset();

			for (auto [entityHandle, transform, raytrace] : world->group(components_group<TransformComponent, RaytraceComponent>).each())
			{
				auto handle = raytracingTLAS.instantiate(raytrace.type, transform.transform);
			}

			renderer->setRaytracingScene(&raytracingTLAS);
		}

		renderer->setEnvironment(environment);
		renderer->setSun(sun);
		renderer->setCamera(camera);

#ifndef ERA_RUNTIME
		editor.visualizePhysics(&ldrRenderPass);
#endif

		executeMainThreadJobs();
	}

	void application::handleFileDrop(const fs::path& filename)
	{
		fs::path path = filename;
		fs::path relative = fs::relative(path, fs::current_path());
		fs::path ext = relative.extension();

		if (isMeshExtension(ext))
		{
			fs::path path = filename;
			path = path.stem();

			ref<World> world = world_scene->get_current_world();
			if (auto mesh = loadAnimatedMeshFromFileAsync(relative.string()))
			{
				auto& entity = world->create_entity("Veribot")
					.add_component<animation::AnimationComponent>()
					.add_component<MeshComponent>(mesh);

				TransformComponent& transform_component = entity.get_component<TransformComponent>();
				transform_component.type = TransformComponent::DYNAMIC;

				initializeAnimationComponentAsync(entity, mesh);
				addRaytracingComponentAsync(entity, mesh);
			}
			else if (auto mesh = loadMeshFromFileAsync(relative.string()))
			{
				auto& entity = world->create_entity("Veribot")
					.add_component<animation::AnimationComponent>()
					.add_component<MeshComponent>(mesh);

				addRaytracingComponentAsync(entity, mesh);
			}
		}
		else if (ext == ".hdr")
		{
			scene.environment.setFromTexture(relative);
		}
	}
}