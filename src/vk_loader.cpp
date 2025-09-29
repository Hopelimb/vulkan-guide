
#include <vk_loader.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <vk_engine.h>
#include <vk_initializers.h>
#include <vk_types.h>
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

VkFilter extract_filter(fastgltf::Filter filter);
VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);
std::optional<AllocatedImage> load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image);
void create_image_from_data(unsigned char* data, int width, int height, AllocatedImage& newImage, VulkanEngine* engine);

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

std::optional<AllocatedImage> load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image)
{
	AllocatedImage newImage{};

	int width, height, nrChannels;

	std::visit(
		fastgltf::visitor{
			[](auto& arg) {},
			[&](fastgltf::sources::URI& filePath) {
				assert(filePath.fileByteOffset == 0);
				assert(filePath.uri.isLocalPath());

				const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());
				unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
				create_image_from_data(data, width, height, newImage, engine);
			},
			[&](fastgltf::sources::Vector& vector) {
				unsigned char* data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()),
					&width, &height, &nrChannels, 4);
				create_image_from_data(data, width, height, newImage, engine);
			},
			[&](fastgltf::sources::BufferView& view) {
				auto& bufferView = asset.bufferViews[view.bufferViewIndex];
				auto& buffer = asset.buffers[bufferView.bufferIndex];
				std::visit(
					fastgltf::visitor{
						[](auto& arg) {},
						[&](fastgltf::sources::Vector& vector) {
							unsigned char* data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset, static_cast<int>(bufferView.byteLength),
								&width, &height, &nrChannels, 4);
							create_image_from_data(data, width, height, newImage, engine);
						},
					}
					,buffer.data);
			},
		}
		,image.data);

	if (newImage.image == VK_NULL_HANDLE)
	{
		return {};
	}
	else {
		return newImage;
	}
}

void create_image_from_data(unsigned char* data, int width, int height, AllocatedImage& newImage, VulkanEngine* engine)
{
	if (data) {
		VkExtent3D imagesize{
			.width = static_cast<uint32_t>(width),
			.height = static_cast<uint32_t>(height),
			.depth = static_cast<uint32_t>(1),
		};

		newImage = engine->create_image(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);
		stbi_image_free(data);
	}
}


std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine, std::string filePath) {
	fmt::print("Loading GLTF: {}\n", filePath);

	std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
	scene->creator = engine;
	LoadedGLTF& file = *scene.get();

	fastgltf::Parser parser;
	
	constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember |
		fastgltf::Options::AllowDouble |
		fastgltf::Options::LoadGLBBuffers |
		fastgltf::Options::LoadExternalBuffers;

	fastgltf::GltfDataBuffer data;
	data.loadFromFile(filePath);

	fastgltf::Asset gltf;

	std::filesystem::path path = filePath;

	auto type = fastgltf::determineGltfFileType(&data);
	if (type == fastgltf::GltfType::glTF) {
		auto load = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
		if (load) {
			gltf = std::move(load.get());
		}
		else {
			std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
			return {};
		}
	}
	else if (type == fastgltf::GltfType::GLB) {
		auto load = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
		if (load) {
			gltf = std::move(load.get());
		}
		else {
			std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
			return {};
		}
	}
	else {
		std::cerr << "Failed to determine glTF container" << std::endl;
		return {};
	}

	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
	};
	file.descriptorPool.init(engine->_device, gltf.materials.size(), sizes);

	// convert the gltf samplers to VkSamplers which's format is compatible with vulkan
	for (fastgltf::Sampler& sampler : gltf.samplers) {
		VkSamplerCreateInfo samplerInfo
		{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.pNext = nullptr,
			.minLod = 0.f,
			.maxLod = VK_LOD_CLAMP_NONE,
		};
		samplerInfo.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		samplerInfo.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
		samplerInfo.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		VkSampler newSampler;
		vkCreateSampler(engine->_device, &samplerInfo, nullptr, &newSampler);

		file.samplers.push_back(newSampler);
	}

	std::vector<std::shared_ptr<MeshAsset>> meshes;
	std::vector<std::shared_ptr<Node>> nodes;
	std::vector<AllocatedImage> images;
	std::vector<std::shared_ptr<GLTFMaterial>> materials;

	// if there are no samplers defined, we need at least one default sampler which indicates as an error
	for (fastgltf::Image& image : gltf.images) {
		std::optional<AllocatedImage> img = load_image(engine, gltf, image);
		if (img.has_value()) {
			images.push_back(*img);
			file.images[image.name.c_str()] = *img;
		}
		else {
			images.push_back(engine->_errorCheckerboardImage);
			fmt::print("Failed to load image: {}\n", image.name);
		}
	}

	file.materialDataBuffer = engine->create_buffer(
		sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU
	);
	int data_index = 0;
	GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants =
		static_cast<GLTFMetallic_Roughness::MaterialConstants*> (file.materialDataBuffer.allocationInfo.pMappedData);

	for (fastgltf::Material& material : gltf.materials) {
		std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
		materials.push_back(newMat);
		file.materials[material.name.c_str()] = newMat;

		GLTFMetallic_Roughness::MaterialConstants constants{
			.colorFactors = glm::vec4(
				material.pbrData.baseColorFactor[0],
				material.pbrData.baseColorFactor[1],
				material.pbrData.baseColorFactor[2],
				material.pbrData.baseColorFactor[3]
			),
			.metal_rough_factors = glm::vec4(
				material.pbrData.metallicFactor,
				material.pbrData.roughnessFactor,
				0, 0
			)
		};
		sceneMaterialConstants[data_index] = constants;

		MaterialPass passType = MaterialPass::MainColor;
		if (material.alphaMode == fastgltf::AlphaMode::Blend) {
			passType = MaterialPass::Transparent;
		}

		GLTFMetallic_Roughness::MaterialResources materialResources{
			.colorImage = engine->_whiteImage,
			.colorSampler = engine->_defaultSamplerLinear,
			.metalRoughImage = engine->_whiteImage,
			.metalRoughSampler = engine->_defaultSamplerLinear,
		};
		materialResources.dataBuffer = file.materialDataBuffer.buffer;
		materialResources.dataBufferOffset = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);

		if (material.pbrData.baseColorTexture.has_value()) {
			auto textureIndex = material.pbrData.baseColorTexture.value().textureIndex;
			size_t imageIndex = gltf.textures[textureIndex].imageIndex.value();
			size_t samplerIndex = gltf.textures[textureIndex].samplerIndex.value();

			materialResources.colorImage = images[imageIndex];
			materialResources.colorSampler = file.samplers[samplerIndex];
		}

		newMat->data = engine->metalRoughMaterial.write_material(engine->_device, passType, materialResources, file.descriptorPool);
		data_index++;
	}

	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;

	for (fastgltf::Mesh& mesh : gltf.meshes) {
		std::shared_ptr<MeshAsset> newMesh = std::make_shared<MeshAsset>();
		meshes.push_back(newMesh);
		file.meshes[mesh.name.c_str()] = newMesh;
		newMesh->name = mesh.name;
		indices.clear();
		vertices.clear();

		for (auto& p : mesh.primitives) {
#pragma region load indeces info
			fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
			GeoSurface newSurface{
				.startIndex = static_cast<uint32_t>(indices.size()),
				.count = static_cast<uint32_t>(indexaccessor.count),
			};

			size_t initial_vtx = vertices.size();
			fastgltf::iterateAccessor<uint32_t>(gltf, indexaccessor, 
				[&](uint32_t idx) {
					indices.push_back(idx + initial_vtx);
				});
#pragma endregion

#pragma region load vertex positions info
			fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
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
#pragma endregion

#pragma region load normal info
			auto normalAttribute = p.findAttribute("NORMAL");
			if (normalAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normalAttribute).second],
					[&](glm::vec3 normal, size_t index) {
						vertices[initial_vtx + index].normal = normal;
					}
				);
			}
#pragma endregion

#pragma region load UVs info
			auto uvAttribute = p.findAttribute("TEXCOORD_0");
			if (uvAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*uvAttribute).second],
					[&](glm::vec4 uv, size_t index) {
						vertices[initial_vtx + index].uv_x = uv.x;
						vertices[initial_vtx + index].uv_y = uv.y;
					}
				);
			}
#pragma endregion

#pragma region load color info
			auto colorAttribute = p.findAttribute("COLOR_0");
			if (colorAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colorAttribute).second],
					[&](glm::vec4 color, size_t index) {
						vertices[initial_vtx + index].color = color;
					}
				);
			}
#pragma endregion

			if (p.materialIndex.has_value()) {
				newSurface.material = materials[p.materialIndex.value()];
			}
			else {
				newSurface.material = materials[0];
			}

			glm::vec3 minpos = vertices[initial_vtx].position;
			glm::vec3 maxpos = vertices[initial_vtx].position;
			for (size_t i = initial_vtx; i < vertices.size(); i++) {
				minpos = glm::min(minpos, vertices[i].position);
				maxpos = glm::max(maxpos, vertices[i].position);
			}

			newSurface.bounds.origin = (maxpos + minpos) / 2.f;
			newSurface.bounds.extents = (maxpos - minpos) / 2.f;
			newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);

			newMesh->surfaces.push_back(newSurface);
		}

		newMesh->meshBuffers = engine->uploadMesh(indices, vertices);
	}

	for (fastgltf::Node& node : gltf.nodes) {
		std::shared_ptr<Node> newNode{};

		if (node.meshIndex.has_value()) {
			newNode = std::make_shared<MeshNode>();
			static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
		}
		else {
			newNode = std::make_shared<Node>();
		}

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()];


		fastgltf::visitor visitors{
			[&](fastgltf::Node::TransformMatrix matrix) {
				memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
			},
			[&](fastgltf::Node::TRS transform) {
				glm::vec3 tl(transform.translation[0], transform. translation[1], transform.translation[2]);
				glm::quat rot(transform.rotation[3] ,transform.rotation[0], transform.rotation[1], transform.rotation[2]);
				glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

				glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
				glm::mat4 rm = glm::toMat4(rot);
				glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

				newNode->localTransform = tm * rm * sm;
			}
		};
		std::visit(visitors, node.transform);
	}
	for (int i = 0; i < gltf.nodes.size(); i++) {
		std::shared_ptr<Node>& sceneNode = nodes[i];
		fastgltf::Node& node = gltf.nodes[i];

		for (auto& c : node.children) {
			sceneNode->children.push_back(nodes[c]);
			nodes[c]->parent = sceneNode;
		}
	}

	for (auto& node : nodes) {
		if (node->parent.lock() == nullptr) {
			file.topNodes.push_back(node);
			node->refreshTransform(glm::mat4(1.f));
		}
	}
	return scene;
}

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	for (auto& n : topNodes) {
		n->Draw(topMatrix, ctx);
	}
}

void LoadedGLTF::clearAll()
{
	VkDevice dv = creator->_device;
	descriptorPool.destroy_pools(dv);
	creator->destroy_buffer(materialDataBuffer);
	
	for (auto& [k, v] : meshes) {
		creator->destroy_buffer(v->meshBuffers.indexBuffer);
		creator->destroy_buffer(v->meshBuffers.vertexBuffer);
	}

	for (auto& [k, v] : images) {
		if (v.image == creator->_errorCheckerboardImage.image) {
			continue;
		}
		else {
			creator->destroy_image(v);
		}
	}

	for (auto& sampler : samplers) {
		vkDestroySampler(dv, sampler, nullptr);
	}
}


VkFilter extract_filter(fastgltf::Filter filter) {
	switch (filter) {
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:

		return VK_FILTER_NEAREST;
	case fastgltf::Filter::Linear:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::LinearMipMapLinear:

		return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter) {
	switch (filter) {
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::LinearMipMapNearest:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case fastgltf::Filter::NearestMipMapLinear:
	case fastgltf::Filter::LinearMipMapLinear:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}