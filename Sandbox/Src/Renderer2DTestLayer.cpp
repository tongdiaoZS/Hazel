#include "Renderer2DTestLayer.h"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Renderer/Renderer2D.h"
#include <filesystem>

Renderer2DTestLayer::Renderer2DTestLayer(const std::string& name) :
m_OrthoCameraController(1.6667f, 1.0f)
{
	Hazel::Renderer2D::Init();

	std::string texturePath = std::filesystem::current_path().string() + "\\Resources\\HeadIcon.jpg";
	m_Texture2D = Hazel::Texture2D::Create(texturePath);
}

Renderer2DTestLayer::~Renderer2DTestLayer()
{
	Hazel::Renderer2D::Shutdown();
}

void Renderer2DTestLayer::OnAttach()
{
	CORE_LOG("Init Layer");
}

void Renderer2DTestLayer::OnDettach()
{
	CORE_LOG("Detach Layer");
}

void Renderer2DTestLayer::OnEvent(Hazel::Event& e )
{
	m_OrthoCameraController.OnEvent(e);
}

void Renderer2DTestLayer::OnUpdate(const Hazel::Timestep& ts)
{
	{
		//// 使用的时候, 需要传入一个profileResult对象
		//Hazel::Timer t("CameraController.OnUpdate", [&](const char* name, float duration)
		//{ 
		//	Hazel::ProfileResult profileResult = {name, duration};
		//	m_ProfileResults.push_back(profileResult); 
		//});
		m_OrthoCameraController.OnUpdate(ts);
	}

	// 每帧开始Clear
	Hazel::RenderCommand::Clear();
	Hazel::RenderCommand::ClearColor(glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));

	Hazel::Renderer2D::BeginScene(m_OrthoCameraController.GetCamera());
	{
		Hazel::Renderer2D::DrawQuad({ -0.4f, 0.1f }, { 1.5f, 1.5f }, { 1.0f, 0.0f, 0.0f, 1.0f });
		Hazel::Renderer2D::DrawQuad({ 0.2f, -0.8f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f });
		Hazel::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.2f }, { 0.4f, 0.4f }, m_Texture2D, 2.0f);   		 
		Hazel::Renderer2D::DrawRotatedQuad({ -0.5f, 0.5f, 0.2f }, { 0.4f, 0.4f }, 45.0f, m_Texture2D, 2.0f);
	}
	Hazel::Renderer2D::EndScene();
}

void Renderer2DTestLayer::OnImGuiRender()
{
	ImGui::Begin("Test");
	ImGui::ColorEdit4("Flat Color Picker", glm::value_ptr(m_FlatColor));

	//for (size_t i = 0; i < m_ProfileResults.size(); i++)
	//{
	//	auto& result = m_ProfileResults[i];
	//	char label[50];
	//	strcpy_s(label, result.Name);
	//	strcat_s(label, ": %.3fms ");		// 保留三位小数
	//	ImGui::Text(label, result.Time);// 打印Profile条目的名字和time
	//}

	m_ProfileResults.clear();
	ImGui::End();
}