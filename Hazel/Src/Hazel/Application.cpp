#include "hzpch.h"
#include "Application.h"
#include "Window.h"
#include "Event/MouseEvent.h"
#include "Event/ApplicationEvent.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "Input.h"
#include "Renderer/Buffer.h"

namespace Hazel
{
	Application* Application::s_Instance = nullptr;
	int indices[3] = { 0,1,2 };

	Application::Application()
	{
		HAZEL_ASSERT(!s_Instance, "Already Exists an application instance");
		s_Instance = this;

		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));
		
		m_ImGuiLayer = new ImGuiLayer();
		m_LayerStack.PushOverlay(m_ImGuiLayer);

		float vertices[] = {
			-0.5, -0.5, 0, 1.0, 0.0, 0.0, 1.0f,
			0.5, -0.5, 0,  0.0, 1.0, 0.0, 1.0f,
			0, 0.5, 0.0	,   0.0, 0.0, 1.0, 1.0f
		};

		// 创建VBO
		m_VertexBuffer = std::unique_ptr<VertexBuffer>(VertexBuffer::Create(vertices, sizeof(vertices)));
		m_VertexBuffer->Bind();

		// 创建Layout，会计算好Stride和Offset
		BufferLayout layout = {
			{ShaderDataType::FLOAT3, "a_Pos" },
			{ShaderDataType::FLOAT4, "a_Color" }
		};

		m_VertexBuffer->SetBufferLayout(layout);
		
		// 创建Vertex Array，把前面算好的东西传入VAO
		m_VertexArray.reset(VertexArray::Create());
		m_VertexArray->Bind();
		m_VertexArray->AddVertexBuffer(m_VertexBuffer);

		m_IndexBuffer = std::unique_ptr<IndexBuffer>(IndexBuffer::Create(indices, sizeof(indices)));
		m_IndexBuffer->Bind();
		m_VertexArray->SetIndexBuffer(m_IndexBuffer);

		std::string vertexSource = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

out vec4 o_color;
void main()
{
	gl_Position = vec4(aPos, 1.0);
	o_color = aColor;
}
		)";

		std::string fragmentSource = R"(
#version 330 core
in vec4 o_color;
out vec4 color;
void main()
{
	//color = vec4(0.8,0.2,0.3,1.0);
	color = o_color;
}
		)";
		m_Shader.reset(new Shader(vertexSource, fragmentSource));
	}

	Application::~Application()
	{
	}

	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
		for (Layer* layer : m_LayerStack)
		{
			layer->OnEvent(e);
		}
	}


	void Application::Run() 
	{
		std::cout << "Run Application" << std::endl;
		while (m_Running)
		{
			// 每帧开始Clear
			glClearColor(0.1f, 0.1f, 0.1f, 1);
			glClear(GL_COLOR_BUFFER_BIT);

			m_Shader->Bind();
			m_VertexArray->Bind();

			glDrawElements(GL_TRIANGLES, m_IndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);

			// Application并不应该知道调用的是哪个平台的window，Window的init操作放在Window::Create里面
			// 所以创建完window后，可以直接调用其loop开始渲染
			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate();
			}

			m_ImGuiLayer->Begin();
			for (Layer* layer : m_LayerStack)
			{
				// 每一个Layer都在调用ImGuiRender函数
				// 目前有两个Layer, Sandbox定义的ExampleLayer和构造函数添加的ImGuiLayer
				layer->OnImGuiRender();
			}
			m_ImGuiLayer->End();
			
			// 每帧结束调用glSwapBuffer
			m_Window->OnUpdate();
		}

		//LOG(w.ToString());
	}

	bool Application::OnWindowClose(WindowCloseEvent &e)
	{
		m_Running = false;
		return true;
	}

	void Application::PushLayer(Layer * layer)
	{
		m_LayerStack.PushLayer(layer);
	}

	Layer* Application::PopLayer()
	{
		return m_LayerStack.PopLayer();
	}
}