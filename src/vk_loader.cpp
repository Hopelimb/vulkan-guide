
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
#include <glm/gtx/transform.hpp>

VkFilter extract_filter(fastgltf::Filter filter);
VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);
std::optional<AllocatedImage> load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image);
void create_image_from_data(unsigned char* data, int width, int height, AllocatedImage& newImage, VulkanEngine* engine);
bool LoadGLTF(VulkanEngine* engine, std::string filePath, fastgltf::Asset& gltf);



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
				unsigned char* data = stbi_load(("G://Projects//NativeProjects//vulkan-guide//vulkan-guide//assets//" + path).c_str(), &width, &height, &nrChannels, 4);
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


std::optional<std::shared_ptr<RenderObjectData>> GetRenderObjectDataFromGltf(VulkanEngine* engine, std::string filePath) {
	fmt::print("Loading GLTF: {}\n", filePath);
	std::shared_ptr<RenderObjectData> scene = std::make_shared<RenderObjectData>();
	scene->creator = engine;
	RenderObjectData& file = *scene.get();
	fastgltf::Asset gltf{};

	if (!LoadGLTF(engine, filePath, gltf))
	{
		fmt::println("failed to load GLTF : {}", filePath);
		return {};
	}

	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> ratio =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
	};
	file.descriptorPool.init(engine->_device, gltf.materials.size(), ratio);

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

	std::vector<std::shared_ptr<MeshAsset>> loadedMeshes;
	std::vector<std::shared_ptr<Node>> loadedNodes;
	std::vector<AllocatedImage> images;
	std::vector<std::shared_ptr<GLTFMaterial>> materials;

	for (fastgltf::Node& node : gltf.nodes) {
		std::shared_ptr<Node> newNode{};

		if (node.meshIndex.has_value()) {
			newNode = std::make_shared<MeshNode>();
			newNode->meshIndex = *node.meshIndex;
		}
		else {
			newNode = std::make_shared<Node>();
		}

		loadedNodes.push_back(newNode);
		file.nodes[node.name.c_str()];


		fastgltf::visitor visitors{
			[&](fastgltf::Node::TransformMatrix matrix) {
				memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
			},
			[&](fastgltf::Node::TRS transform) {
				glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
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
		std::shared_ptr<Node>& sceneNode = loadedNodes[i];
		fastgltf::Node& node = gltf.nodes[i];

		for (auto& c : node.children) {
			sceneNode->children.push_back(loadedNodes[c]);
			loadedNodes[c]->parent = sceneNode;
		}
	}


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
		//newMat->data = engine->metalRoughMaterial.write_material2(engine->_device, passType, data_index, materialResources, file.descriptorPool);
		data_index++;
	}

//#pragma region Animation
//	// とりあえず０番目の固定ポージングにする
//	auto& anim = gltf.animations[0];
//
//	std::vector<glm::mat4> bornTransforms;
//	bornTransforms.resize(gltf.nodes.size());
//	glm::vec3 tl{0,0,0};
//	glm::quat rot = glm::quat();
//	glm::vec3 sc{ 1,1,1 };
//	std::vector<std::vector<glm::mat4>> animData;
//	std::vector<glm::vec3> translateData;
//	std::vector<glm::quat> rotationData;
//	std::vector<glm::vec3> scaleData;
//	animData.resize(gltf.nodes.size());
//	for (size_t i = 0; i < anim.channels.size(); i++)
//	{
//		auto& channel = anim.channels[i];
//		const auto& sampler = anim.samplers[channel.samplerIndex];
//		int nodeIndex = channel.nodeIndex;
//		auto path = channel.path;
//
//
//		auto& inputAccessor = gltf.accessors[sampler.inputAccessor];
//		auto& outputAccessor = gltf.accessors[sampler.outputAccessor];
//		animData[nodeIndex].resize(inputAccessor.count);
//		translateData.resize(inputAccessor.count);
//		rotationData.resize(inputAccessor.count);
//		scaleData.resize(inputAccessor.count);
//
//		float t0 = 0;
//		fastgltf::iterateAccessorWithIndex<float>(gltf, inputAccessor,
//			[&](float time, size_t index) {
//				if (index == 0) {
//					t0 = time;
//				}
//			});
//		if (path == fastgltf::AnimationPath::Translation)
//		{
//			glm::vec3 value;
//			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, outputAccessor,
//				[&](glm::vec3 v, size_t index) {
//					{
//						translateData[index] = v;
//					}
//				});
//		}
//		else if (path == fastgltf::AnimationPath::Rotation)
//		{
//			glm::quat value;
//			fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, outputAccessor,
//				[&](glm::vec4 v, size_t index) {
//					{
//						rot = glm::quat(v[3], v[0], v[1], v[2]);
//						rotationData[index] = rot;
//					}
//				});
//		}
//		else if (path == fastgltf::AnimationPath::Scale)
//		{
//			glm::vec3 value;
//			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, outputAccessor,
//				[&](glm::vec3 v, size_t index) {
//					{
//						scaleData[index] = sc;
//					}
//				});
//		}
//
//		for (auto i = 0; i < animData[nodeIndex].size(); i++)
//		{
//			auto mt = glm::translate(translateData[i]);
//			auto mr = glm::toMat4(rotationData[i]);
//			auto ms = glm::mat4(1.f);
//
//			animData[nodeIndex][i] = ms * mr * mt;
//		}
//
//		//if (i % 3 == 0)
//		//{
//		//	glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
//		//	glm::mat4 rm = glm::toMat4(rot);
//		//	glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);
//		//	bornTransforms[nodeIndex] = glm::mat4(1.f);
//		//}
//	}
//
//	for (auto i = 0; i < animData.size(); i++)
//	{
//		if (animData[i].size() > 0)
//		{
//			loadedNodes[i]->localTransform = animData[i][100];
//		}
//		loadedNodes[i]->refreshTransform(loadedNodes[i]->localTransform);
//	}
//#pragma endregion


	for (fastgltf::Mesh& mesh : gltf.meshes) {
		std::shared_ptr<MeshAsset> newMesh = std::make_shared<MeshAsset>();
		loadedMeshes.push_back(newMesh);
		file.meshes[mesh.name.c_str()] = newMesh;
		newMesh->name = mesh.name;
		auto& vertices = newMesh->vertices;
		auto& indices = newMesh->indices;
		auto& worldTransforms = newMesh->jointMatrices;

		indices.clear();
		vertices.clear();
		worldTransforms.clear();
		worldTransforms.push_back(glm::mat4(1.f));

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
						.joints = glm::vec4{1.f},
						.weights = glm::vec4{1.f},
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
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uvAttribute).second],
					[&](glm::vec2 uv, size_t index) {
						vertices[initial_vtx + index].uv_x = uv.x;
						vertices[initial_vtx + index].uv_y = uv.y;
					}
				);
			}
#pragma endregion

			auto jointAttribute = p.findAttribute("JOINTS_0");
			if (jointAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*jointAttribute).second],
					[&](glm::vec4 joints, size_t index) {
						vertices[initial_vtx + index].joints = joints;
					}
				);
			}

#pragma region load color info
			auto colorAttribute = p.findAttribute("WEIGHTS_0");
			if (colorAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colorAttribute).second],
					[&](glm::vec4 weights, size_t index) {
						vertices[initial_vtx + index].weights = weights;
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
	}

	for (auto i = 0; i < loadedNodes.size(); i++) {
		auto& node = loadedNodes[i];
		if (node->parent.lock() == nullptr) {
			file.topNodes.push_back(node);
			node->refreshTransform(glm::mat4(1.f));
		}
		if (node->meshIndex != -1)
		{
			auto& mesh = loadedMeshes[node->meshIndex];
			mesh->meshBuffers = engine->uploadMesh(mesh->indices, mesh->vertices, mesh->jointMatrices);
			
			static_cast<MeshNode*>(node.get())->mesh = loadedMeshes[node->meshIndex];
		}
	}
	return scene;
}

bool LoadGLTF(VulkanEngine* engine, std::string filePath, fastgltf::Asset& gltf)
{
	fastgltf::Parser parser;
	constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember |
		fastgltf::Options::AllowDouble |
		fastgltf::Options::LoadGLBBuffers |
		fastgltf::Options::LoadExternalBuffers;

	fastgltf::GltfDataBuffer data;
	data.loadFromFile(filePath);

	std::filesystem::path path = filePath;

	auto type = fastgltf::determineGltfFileType(&data);
	if (type == fastgltf::GltfType::glTF) {
		auto load = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
		if (load) {
			gltf = std::move(load.get());
		}
		else {
			std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
			return false;
		}
	}
	else if (type == fastgltf::GltfType::GLB) {
		auto load = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
		if (load) {
			gltf = std::move(load.get());
		}
		else {
			std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
			return false;
		}
	}
	else {
		std::cerr << "Failed to determine glTF container" << std::endl;
		return false;
	}

	return true;
}


void RenderObjectData::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	for (auto& n : topNodes) {
		n->Draw(topMatrix, ctx);
	}
}

void RenderObjectData::clearAll()
{
	VkDevice dv = creator->_device;
	descriptorPool.destroy_pools(dv);
	creator->destroy_buffer(materialDataBuffer);
	
	for (auto& [k, v] : meshes) {
		creator->destroy_buffer(v->meshBuffers.indexBuffer);
		creator->destroy_buffer(v->meshBuffers.vertexBuffer);
		creator->destroy_buffer(v->meshBuffers.jointMatrixBuffer);
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