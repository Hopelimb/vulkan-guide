
#include <vk_loader.h>
#include "stb_image.h"
#include <iostream>
#include <vk_engine.h>
#include <vk_initializers.h>
#include <vk_types.h>
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(VulkanEngine* engine, std::filesystem::path filePath)
{

	std::cout << "Loading GLTF: " << filePath << std::endl;


	fastgltf::GltfDataBuffer data;
	data.loadFromFile(filePath);

	constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
	fastgltf::Asset gltf;
	fastgltf::Parser parser{};

	auto load = parser.loadBinaryGLTF(&data, filePath.parent_path(), gltfOptions);
	if (load) {
		gltf = std::move(load.get());
	}
	else {
		fmt::println("Failed to load glTF: {}", fastgltf::to_underlying(load.error()));
	}


	std::vector<std::shared_ptr<MeshAsset>> meshes;

	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;


	for (auto& mesh : gltf.meshes) {
		MeshAsset newMesh{};
		newMesh.name = mesh.name;
		 
		indices.clear();
		vertices.clear();

		for (auto&& p : mesh.primitives)
		{
			size_t initial_vtx = vertices.size();
		
			
			// load indexes
			auto accessorIndex = p.indicesAccessor.value();
			GeoSurface newSurface{
				.startIndex = static_cast<uint32_t>(indices.size()),
				.count = static_cast<uint32_t>(gltf.accessors[accessorIndex].count)
			};

			fastgltf::Accessor& indexaccessor = gltf.accessors[accessorIndex];
			indices.reserve(indices.size() + indexaccessor.count);

			fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
				[&](std::uint32_t idx) {
					indices.push_back(idx + initial_vtx);
				});


			// load vertex positions
			auto positionIndex = p.findAttribute("POSITION")->second;
			fastgltf::Accessor& posAccessor = gltf.accessors[positionIndex];
			vertices.resize(vertices.size() + posAccessor.count);

			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
				[&](glm::vec3 v, size_t index) {
					Vertex newvtx{
						.position = v,
						.uv_x = 0,
						.normal = {1,0,0},
						.uv_y = 0,
						.color = glm::vec4{1.f},
					};
					vertices[initial_vtx + index] = newvtx;
				});


			auto normalAttribute = p.findAttribute("NORMAL");
			if (normalAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normalAttribute->second],
					[&](glm::vec3 normal, size_t index) {
						vertices[initial_vtx + index].normal = normal;
					});
			}

			auto texcoordAttribute = p.findAttribute("TEXCOORD_0");
			if (texcoordAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[texcoordAttribute->second],
					[&](glm::vec2 uv, size_t index) {
						vertices[initial_vtx + index].uv_x = uv.x;
						vertices[initial_vtx + index].uv_y = uv.y;
					});
			}

			auto colorAttribute = p.findAttribute("COLOR_0");
			if (colorAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colorAttribute->second],
					[&](glm::vec4 color, size_t index) {
						vertices[initial_vtx + index].color = color;
					});
			}


			newMesh.surfaces.push_back(newSurface);
		}

		constexpr bool OverrideColors = false;

		if (OverrideColors) {
			for (auto& vtx : vertices) {
				vtx.color = glm::vec4(vtx.normal, 1.f);
			}
		}

		newMesh.meshBuffers = engine->uploadMesh(indices, vertices);
		meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newMesh)));
	}


	return meshes;
}
