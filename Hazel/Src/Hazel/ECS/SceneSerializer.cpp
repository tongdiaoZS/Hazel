#include "hzpch.h"
#include "SceneSerializer.h"
#include "Scene.h"
#include "Components/Transform.h"

namespace Hazel 
{
	void SceneSerializer::Serialize(std::shared_ptr<Scene> scene, const char* savePath)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Untitled";
		out << YAML::Key << "GameObjects" << YAML::Value << YAML::BeginSeq;

		auto& gos = scene->GetGameObjects();
		for (size_t i = 0; i < gos.size(); i++)
			SerializeGameObject(out, gos[i]);

		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(savePath);
		fout << out.c_str();
	}

	bool SceneSerializer::Deserialize(std::shared_ptr<Scene> scene, const char* filepath)
	{
		std::ifstream stream(filepath);
		std::stringstream strStream;
		strStream << stream.rdbuf();

		// YAML里的数据也是用Tree形式读取得到的
		// data的[]重载会创建并返回一个Node对象, 所以这里不需要用Node&
		YAML::Node data = YAML::Load(strStream.str());

		// 找到data里对应的Scene的数据
		if (!data["Scene"])
			return false;

		// TODO: 准确的说, 这部分内容不应该在Deserialize时调用, 而应该在PlayMode下调用
		Physics2D::Init();

		std::string sceneName = data["Scene"].as<std::string>();

		YAML::Node entities = data["GameObjects"];
		if (entities)
		{
			for (auto entity : entities)
			{
				std::string name = entity["Name"].as<std::string>();

				// TODO, 这里的uuid需要赋值给deserializedEntity, 但是底层register又会Create一个id
				uint64_t instanceId = entity["InstanceID"].as<uint32_t>();
				auto uuidEnt = entity["UUID"];
				

				GameObject deserializedEntity;
				
				if(uuidEnt)
				{
					uint64_t uuid = uuidEnt.as<uint64_t>();
					deserializedEntity = scene->CreateGameObjectInSceneWithUUID(scene, uuid, name);
				}
				else
					deserializedEntity = scene->CreateGameObjectInScene(scene, name);
						

				auto transformComponent = entity["TransformComponent"];
				if (transformComponent)
				{
					// Entities always have transforms
					Transform& tc = deserializedEntity.GetComponent<Transform>();
					tc.Translation = transformComponent["Translation"].as<glm::vec3>();
					tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
					tc.Scale = transformComponent["Scale"].as<glm::vec3>();
				}

				auto cameraComponent = entity["CameraComponent"];
				if (cameraComponent)
				{
					auto& cc = deserializedEntity.AddComponent<CameraComponent>();

					// TODO: 这些as全应该像radio一样做check
					cc.SetProjectionType((CameraComponent::ProjectionType)cameraComponent["ProjectionType"].as<int>());

					cc.SetPerspectiveVerticalFOV(cameraComponent["PerspectiveFOV"].as<float>());
					cc.SetPerspectiveNearClip(cameraComponent["PerspectiveNear"].as<float>());
					cc.SetPerspectiveFarClip(cameraComponent["PerspectiveFar"].as<float>());

					cc.SetOrthographicSize(cameraComponent["OrthographicSize"].as<float>());
					cc.SetOrthographicNearClip(cameraComponent["OrthographicNear"].as<float>());
					cc.SetOrthographicFarClip(cameraComponent["OrthographicFar"].as<float>());

					//cc.Primary = cameraComponent["Primary"].as<bool>();
					bool& radio = cc.GetFixedAspectRatio();
					auto node = cameraComponent["FixedAspectRatio"];
					if(node)
						radio = node.as<bool>();
				}

				auto spriteRendererComponent = entity["SpriteRendererComponent"];
				if (spriteRendererComponent)
				{
					auto& src = deserializedEntity.AddComponent<SpriteRenderer>();
					glm::vec4& col = src.GetTintColor();

					auto node = spriteRendererComponent["Color"];
					if (node)
						col = node.as<glm::vec4>();
				}

				auto rigidbody2DComponent = entity["Rigidbody2DComponent"];
				if (rigidbody2DComponent)
				{
					// TODO: ASSERT
					Transform& tc = deserializedEntity.GetComponent<Transform>();
					tc.Translation = transformComponent["Translation"].as<glm::vec3>();
					tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();

					auto& src = deserializedEntity.AddComponent<Rigidbody2D>(tc.Translation.x, tc.Translation.y, tc.Rotation.z);
					Rigidbody2DType type = src.GetType();
					auto node = rigidbody2DComponent["Type"];
					if(node)
						src.SetType((Rigidbody2DType)node.as<int>());

					glm::vec2& extents = src.GetExtents();
					auto extentsNode = rigidbody2DComponent["Extents"];
					if (extentsNode)
						src.SetExtents(extentsNode.as<glm::vec2>());
				}
			}
		}

		return true;
	}

	void SceneSerializer::SerializeGameObject(YAML::Emitter& out, const GameObject& go)
	{
		// Map代表的映射关系pair
		out << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << go.ToString();
		out << YAML::Key << "InstanceID" << YAML::Value << go.GetInstanceId();
		out << YAML::Key << "UUID" << YAML::Value << go.GetUUID();

		if (go.HasComponent<Transform>())
		{
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap;

			auto& tc = go.GetComponent<Transform>();
			out << YAML::Key << "Translation" << YAML::Value << tc.Translation;
			out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
			out << YAML::Key << "Scale" << YAML::Value << tc.Scale;

			out << YAML::EndMap;
		}

		if (go.HasComponent<CameraComponent>())
		{
			out << YAML::Key << "CameraComponent";
			out << YAML::BeginMap; // CameraComponent

			auto& camera = go.GetComponent<CameraComponent>();

			out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
			out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetPerspectiveVerticalFOV();
			out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
			out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
			out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
			out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
			out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();

			//out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
			out << YAML::Key << "FixedAspectRatio" << YAML::Value << camera.GetFixedAspectRatio();

			out << YAML::EndMap; // CameraComponent
		}

		if (go.HasComponent<SpriteRenderer>())
		{
			out << YAML::Key << "SpriteRendererComponent";
			out << YAML::BeginMap;

			auto& spriteRendererComponent = go.GetComponent<SpriteRenderer>();
			out << YAML::Key << "Color" << YAML::Value << spriteRendererComponent.GetTintColor();

			out << YAML::EndMap;
		}

		if (go.HasComponent<Rigidbody2D>())
		{
			out << YAML::Key << "Rigidbody2DComponent";
			out << YAML::BeginMap;
			auto& rigidbody2DComponent = go.GetComponent<Rigidbody2D>();
			out << YAML::Key << "Type" << YAML::Value << (int)rigidbody2DComponent.GetType();
			out << YAML::Key << "Extents" << YAML::Value << rigidbody2DComponent.GetExtents();
			out << YAML::EndMap;
		}

		out << YAML::EndMap;
	}

	GameObject SceneSerializer::DeserializeGameObject(YAML::Emitter& out)
	{
		return GameObject();
	}
}
