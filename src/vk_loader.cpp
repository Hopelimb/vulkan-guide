
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
void ExtractMaterialData(RenderObjectData& resultRef, fastgltf::Asset& gltf, std::vector<std::shared_ptr<EngineMaterial>>& materials, VulkanEngine* engine, std::vector<AllocatedImage>& images);



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
	// まずはgltfデータをメモリにロードする
	fastgltf::Asset originalGltfData{};
	if (!LoadGLTF(engine, filePath, originalGltfData))
	{
		fmt::println("failed to load GLTF : {}", filePath);
		return {};
	}

	// ここからメモリにロード済みのgltfデータから、VkEngineの描画処理にに必要な部分のみを抽出する
	std::shared_ptr<RenderObjectData> result = std::make_shared<RenderObjectData>();
	result->creator = engine;
	RenderObjectData& resultRef = *result.get();

	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> ratio =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
	};
	resultRef.descriptorPool.init(engine->_device, originalGltfData.materials.size(), ratio);

	// convert the gltf samplers to VkSamplers which's format is compatible with vulkan
	for (fastgltf::Sampler& sampler : originalGltfData.samplers) {
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

		resultRef.samplers.push_back(newSampler);
	}

	std::vector<std::shared_ptr<MeshAsset>> loadedMeshes;
	std::vector<std::shared_ptr<Node>> loadedNodes;
	std::vector<AllocatedImage> loadedImages;
	std::vector<std::shared_ptr<EngineMaterial>> loadedMaterials;

	for (fastgltf::Node& node : originalGltfData.nodes) {
		std::shared_ptr<Node> newNode{};

		if (node.meshIndex.has_value()) {
			newNode = std::make_shared<MeshNode>();
			newNode->meshIndex = *node.meshIndex;
		}
		else {
			newNode = std::make_shared<Node>();
		}

		loadedNodes.push_back(newNode);
		resultRef.nodes[node.name.c_str()];


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
	for (int i = 0; i < originalGltfData.nodes.size(); i++) {
		std::shared_ptr<Node>& sceneNode = loadedNodes[i];
		fastgltf::Node& node = originalGltfData.nodes[i];

		for (auto& c : node.children) {
			sceneNode->children.push_back(loadedNodes[c]);
			loadedNodes[c]->parent = sceneNode;
		}
	}


	// if there are no samplers defined, we need at least one default sampler which indicates as an error
	for (fastgltf::Image& image : originalGltfData.images) {
		std::optional<AllocatedImage> img = load_image(engine, originalGltfData, image);
		if (img.has_value()) {
			loadedImages.push_back(*img);
			resultRef.images[image.name.c_str()] = *img;
		}
		else {
			loadedImages.push_back(engine->_errorCheckerboardImage);
			fmt::print("Failed to load image: {}\n", image.name);
		}
	}



	// 後でデータ充填しやすくために、マテリアルバッファの参照ビューを作る
	ExtractMaterialData(resultRef, originalGltfData, loadedMaterials, engine, loadedImages);

	for (fastgltf::Mesh& mesh : originalGltfData.meshes) {
		std::shared_ptr<MeshAsset> newMesh = std::make_shared<MeshAsset>();
		loadedMeshes.push_back(newMesh);
		resultRef.meshes[mesh.name.c_str()] = newMesh;
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
			fastgltf::Accessor& indexaccessor = originalGltfData.accessors[p.indicesAccessor.value()];
			GeoSurface newSurface{
				.startIndex = static_cast<uint32_t>(indices.size()),
				.count = static_cast<uint32_t>(indexaccessor.count),
			};

			size_t initial_vtx = vertices.size();
			fastgltf::iterateAccessor<uint32_t>(originalGltfData, indexaccessor, 
				[&](uint32_t idx) {
					indices.push_back(idx + initial_vtx);
				});
#pragma endregion

#pragma region load vertex positions info
			fastgltf::Accessor& posAccessor = originalGltfData.accessors[p.findAttribute("POSITION")->second];
			vertices.resize(vertices.size() + posAccessor.count);

			fastgltf::iterateAccessorWithIndex<glm::vec3>(originalGltfData, posAccessor,
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
				fastgltf::iterateAccessorWithIndex<glm::vec3>(originalGltfData, originalGltfData.accessors[(*normalAttribute).second],
					[&](glm::vec3 normal, size_t index) {
						vertices[initial_vtx + index].normal = normal;
					}
				);
			}
#pragma endregion

#pragma region load UVs info
			auto uvAttribute = p.findAttribute("TEXCOORD_0");
			if (uvAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec2>(originalGltfData, originalGltfData.accessors[(*uvAttribute).second],
					[&](glm::vec2 uv, size_t index) {
						vertices[initial_vtx + index].uv_x = uv.x;
						vertices[initial_vtx + index].uv_y = uv.y;
					}
				);
			}
#pragma endregion

			auto jointAttribute = p.findAttribute("JOINTS_0");
			if (jointAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(originalGltfData, originalGltfData.accessors[(*jointAttribute).second],
					[&](glm::vec4 joints, size_t index) {
						vertices[initial_vtx + index].joints = joints;
					}
				);
			}

#pragma region load color info
			auto colorAttribute = p.findAttribute("WEIGHTS_0");
			if (colorAttribute != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(originalGltfData, originalGltfData.accessors[(*colorAttribute).second],
					[&](glm::vec4 weights, size_t index) {
						vertices[initial_vtx + index].weights = weights;
					}
				);
			}
#pragma endregion



			if (p.materialIndex.has_value()) {
				newSurface.material = loadedMaterials[p.materialIndex.value()];
			}
			else {
				newSurface.material = loadedMaterials[0];
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
			resultRef.topNodes.push_back(node);
			node->refreshTransform(glm::mat4(1.f));
		}
		if (node->meshIndex != -1)
		{
			auto& mesh = loadedMeshes[node->meshIndex];
			mesh->meshBuffers = engine->uploadMesh(mesh->indices, mesh->vertices, mesh->jointMatrices);
			
			static_cast<MeshNode*>(node.get())->mesh = loadedMeshes[node->meshIndex];
		}
	}
	return result;
}

void ExtractMaterialData(RenderObjectData& resultRef, fastgltf::Asset& gltf, std::vector<std::shared_ptr<EngineMaterial>>& materials, VulkanEngine* engine, std::vector<AllocatedImage>& images)
{
	size_t materialConstantsSize = sizeof(MaterialTemplate_PBR::MaterialConstants);
	resultRef.materialDataBuffer = engine->create_buffer(
		materialConstantsSize * gltf.materials.size(),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU
	);

	std::span<MaterialTemplate_PBR::MaterialConstants> sceneMaterialConstantsRef(
		static_cast<MaterialTemplate_PBR::MaterialConstants*> (resultRef.materialDataBuffer.allocationInfo.pMappedData),
		gltf.materials.size()
	);

	for (size_t materialIndex = 0; materialIndex < gltf.materials.size(); materialIndex++) {
		auto& gltfMaterialData = gltf.materials[materialIndex];
		std::shared_ptr<EngineMaterial> newMat = std::make_shared<EngineMaterial>();
		materials.push_back(newMat);
		//resultRef.materials[gltfMaterialData.name.c_str()] = newMat;

		MaterialTemplate_PBR::MaterialConstants constants{
			.colorFactors = glm::vec4(
				gltfMaterialData.pbrData.baseColorFactor[0],
				gltfMaterialData.pbrData.baseColorFactor[1],
				gltfMaterialData.pbrData.baseColorFactor[2],
				gltfMaterialData.pbrData.baseColorFactor[3]
			),
				.metal_rough_factors = glm::vec4(
					gltfMaterialData.pbrData.metallicFactor,
					gltfMaterialData.pbrData.roughnessFactor,
					0, 0
				)
		};
		sceneMaterialConstantsRef[materialIndex] = constants;

		MaterialPass passType = MaterialPass::MainColor;
		if (gltfMaterialData.alphaMode == fastgltf::AlphaMode::Blend) {
			passType = MaterialPass::Transparent;
		}

		MaterialTemplate_PBR::MaterialResources materialResources{
			.colorImage = engine->_whiteImage,
			.colorSampler = engine->_defaultSamplerLinear,
			.metalRoughImage = engine->_whiteImage,
			.metalRoughSampler = engine->_defaultSamplerLinear,
		};
		materialResources.dataBuffer = resultRef.materialDataBuffer.buffer;
		materialResources.dataBufferOffset = materialIndex * materialConstantsSize;

		// モデルデータにPBRマテリアルのベースカラーの情報が含まれる場合、それを適用する
		if (gltfMaterialData.pbrData.baseColorTexture.has_value()) {
			size_t textureIndex = gltfMaterialData.pbrData.baseColorTexture.value().textureIndex;
			size_t imageIndex = gltf.textures[textureIndex].imageIndex.value();
			size_t samplerIndex = gltf.textures[textureIndex].samplerIndex.value();
			materialResources.colorImage = images[imageIndex];
			materialResources.colorSampler = resultRef.samplers[samplerIndex];
		}

		// マテリアルの情報に基づき、DescriptorSetを更新する
		newMat->data = engine->pbrTamplate.createMaterialInstance(engine->_device, passType, materialResources, resultRef.descriptorPool);
	}
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