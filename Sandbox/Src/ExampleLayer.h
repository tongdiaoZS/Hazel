#pragma once
#include "Hazel/Layer.h"
#include "Hazel/Renderer/Shader.h"
#include "Hazel/Renderer/VertexArray.h"
#include "Hazel/Renderer/OrthographicCamera.h"
#include "Hazel/Renderer/Texture.h"
#include "Hazel/Renderer/ShaderLibrary.h"

// 这里的Camera是属于Layer的, 没有存在Application或Sandbox类里
class ExampleLayer : public Hazel::Layer
{
public:
	ExampleLayer();

private:
	void OnAttach() override;
	void OnDettach() override;
	void OnEvent(Hazel::Event& e) override;
	void OnUpdate(const Timestep& step) override;
	void OnImGuiRender() override;

private:
	Hazel::OrthographicCamera m_Camera;
	std::shared_ptr<Hazel::ShaderLibrary> m_ShaderLibrary;
	std::shared_ptr<Hazel::Texture2D> m_TextureOne;
	std::shared_ptr<Hazel::Texture2D> m_TextureTwo;

	std::shared_ptr<Hazel::VertexArray> m_QuadVertexArray;
	glm::vec4 m_FlatColor = glm::vec4(0.2, 0.3, 0.8, 1.0);
};
