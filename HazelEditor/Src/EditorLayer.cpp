#include "EditorLayer.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Renderer/RenderCommandRegister.h"
#include "Math/Random.h"
#include <filesystem>
#include "ECS/Components/Transform.h"
#include "ECS/SceneSerializer.h"
#include "Utils/PlatformUtils.h"
#include "Hazel/Scripting/Scripting.h"
#include "ImGuizmo.h"

namespace Hazel
{
	bool hasEnding(std::string const& fullString, std::string const& ending) 
	{
		if (fullString.length() >= ending.length()) 
		{
			return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
		}
		else 
			return false;
	}

	EditorLayer::EditorLayer(const std::string& name):
		m_EditorCameraController(45.0f, 1.6667f, 0.1f, 100.0f)
	{
		Hazel::RenderCommandRegister::Init();

		//std::string texturePath = std::filesystem::current_path().string() + "\\Resources\\HeadIcon.jpg";
		std::string texturePath = std::filesystem::current_path().string() + "\\Resources\\TextureAtlas.png";
		m_Texture2D = Hazel::Texture2D::Create(texturePath);
	}

	// 16行9列
	static const char s_MapTiles[] =
	{
		// 这种写法其实代表一个长字符串, D代表Dirt土地Tile, W代表Water Tile, S代表路标Tile
		// 注意第一个Tile为D, 虽然在数组里坐标为(0,0), 但是在屏幕上对应的坐标应该是(0,1)
		"DDWWWWWWWWWWWWWW"
		"DDWWWWWWWWWWWWWW"
		"DDDDDDDDDDDWWWWW"
		"DDDDDSDDDDDWWWWW"
		"DDDDDDDDDDDWWWWW"
		"DDWWWWWWWWWWWWWW"
		"DDWWWWWWWWWDDSDD"
		"DDWWWWWWWWWWWWWW"
		"DDWWWWWWWWWWWWWW"
	};

	static std::unordered_map<char, std::shared_ptr<Hazel::SubTexture2D>> s_Map;

	EditorLayer::~EditorLayer()
	{
		Hazel::RenderCommandRegister::Shutdown();
	}

	void EditorLayer::OnAttach()
	{
		CORE_LOG("Init Layer");

		Hazel::SubTexture2D subT1(m_Texture2D, { 0.7f, 5.0f / 9.0f }, { 0.75f, 6.0f / 9.0f });
		std::shared_ptr<Hazel::SubTexture2D> waterTileTex = std::make_shared<Hazel::SubTexture2D>(subT1);
		s_Map['W'] = waterTileTex;

		Hazel::SubTexture2D subT2(m_Texture2D, { 0.1f, 2.0f / 9.0f }, { 0.15f, 3.0f / 9.0f });
		std::shared_ptr<Hazel::SubTexture2D> dirtTileTex = std::make_shared<Hazel::SubTexture2D>(subT2);
		s_Map['D'] = dirtTileTex;

		Hazel::SubTexture2D subT3(m_Texture2D, { 0.3f, 4.0f / 9.0f }, { 0.35f, 5.0f / 9.0f });
		std::shared_ptr<Hazel::SubTexture2D> roadSignTex = std::make_shared<Hazel::SubTexture2D>(subT3);
		s_Map['S'] = roadSignTex;

		Hazel::FramebufferSpecification spec;
		spec.colorAttachmentCnt = 2;
		spec.depthAttachmentCnt = 1;
		spec.enableMSAA = m_EnableMSAATex;
		m_ViewportFramebuffer = Hazel::Framebuffer::Create(spec);
		m_ViewportFramebuffer->SetShader(Hazel::RenderCommandRegister::GetCurrentShader());

		m_Scene = std::make_shared<Hazel::Scene>();

		// TODO: 暂时默认绑定到第一个CameraComponent上, 实际应该是点谁, 就绑定到谁
		Hazel::FramebufferSpecification camSpec;
		camSpec.width = 350;
		camSpec.height = 350;
		camSpec.enableMSAA = m_EnableMSAATex;
		m_CameraComponentFramebuffer = Hazel::Framebuffer::Create(camSpec);

		SceneSerializer::Deserialize(m_Scene, "Physics.scene");
		//SceneSerializer::Deserialize(m_Scene, "DefaultScene.scene");

		m_SceneHierarchyPanel.SetContext(m_Scene);
		m_ContentBrowserPanel.Init();

		Scripting s;
		MonoAssembly* p = s.LoadCSharpAssembly("../Hazel-ScriptCore/Build/Hazel-ScriptCore.dll");
		s.PrintAssemblyTypes(p);
		MonoClass* p2 = s.GetClassInAssembly(p, "MyNamespace", "Program");
		MonoObject* objP = s.CreateInstance(p2);
		s.CallMethod(p2, objP, "PrintFloatVar");

		s.CallMethod(p2, nullptr, "PrintVarStatic");

		MonoClassField* fieldP = s.GetFieldRef(objP, "MyPublicFloatVar");

		float val = s.GetFieldValue<float>(objP, fieldP);
		// 很奇怪, 直接这么打log, 输出的反而是0, 用std::cout都不会出这个问题, 感觉是spdlog的问题
		//LOG("float: {:.2f}", s.GetFieldValue<float>(objP, fieldP));
		LOG("float: {:03.2f}", val);

		m_IconPlay = Texture2D::Create("Resources/Icons/PlayButton.png");
		m_IconStop = Texture2D::Create("Resources/Icons/StopButton.png");

		if (m_EnableMSAATex)
			m_ViewportFramebuffer->SetUpMSAAContext();
	}

	void EditorLayer::OnDetach()
	{
		CORE_LOG("Detach Layer");
	}

	void EditorLayer::OnEvent(Hazel::Event& e)
	{
		if (e.GetEventType() == Hazel::EventType::KeyPressed)
		{
			KeyPressedEvent* kpe = dynamic_cast<Hazel::KeyPressedEvent*>(&e);
			if (kpe->GetKeycode() == HZ_KEY_Q)
			{
				m_Option = ToolbarOptions::Default;
				kpe->MarkHandled();
			}
			if (kpe->GetKeycode() == HZ_KEY_W)
			{
				m_Option = ToolbarOptions::Translate;
				kpe->MarkHandled();
			}
			if (kpe->GetKeycode() == HZ_KEY_E)
			{
				m_Option = ToolbarOptions::Rotation;
				kpe->MarkHandled();
			}
			if (kpe->GetKeycode() == HZ_KEY_R)
			{
				m_Option = ToolbarOptions::Scale;
				kpe->MarkHandled();
			}
		}
		
		if (e.GetEventType() == Hazel::EventType::MouseButtonPressed)
		{
			Hazel::MouseButtonPressedEvent* ep = dynamic_cast<Hazel::MouseButtonPressedEvent*>(&e);
			if (ep)
			{
				if (ep->GetMouseButton() == 0 && m_ViewportFocused)
				{
					auto p = ImGui::GetMousePos();
					if (p.x >= m_ViewportMin.x && p.x <= m_ViewportMax.x
						&& p.y >= m_ViewportMin.y && p.y <= m_ViewportMax.y)
					{
						p.x = p.x - m_ViewportMin.x;
						p.y = p.y - m_ViewportMin.y;

						float height = m_ViewportMax.y - m_ViewportMin.y;
						p.y = height - p.y;

						int id = m_ViewportFramebuffer->ReadPixel(1, (int)p.x, (int)p.y);
						if (id > -1)
							m_SceneHierarchyPanel.SetSelectedGameObjectId((uint32_t)id);
					}
				}
				
				//if(ep->GetMouseButton() == 1 && !m_ViewportFocused)// 右键也要focus到viewport创建
				//{
				//	// if click on viewport

				//}

			}
		}

		m_EditorCameraController.OnEvent(e);
	}

	void EditorLayer::OnUpdate(const Hazel::Timestep& ts)
	{
		if (m_PlayMode == PlayMode::Play)
		{
			// 更新游戏逻辑
			m_Scene->Update(ts);
		}

		if (m_ViewportFocused/* && m_ViewportHovered*/)
			m_EditorCameraController.OnUpdate(ts);

		// 每帧开始Clear

		// This is for the color for default window 
		// 保留原本默认窗口对应framebuffer的颜色, 注意, 一定要先设置ClearColor, 再去Clear
		Hazel::RenderCommand::SetClearColor(glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
		Hazel::RenderCommand::Clear();

		// 先渲染Viewport
		m_ViewportFramebuffer->Bind();
		Hazel::RenderCommand::SetClearColor(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
		Hazel::RenderCommand::Clear();

		Hazel::RenderCommandRegister::BeginScene(m_EditorCameraController.GetCamera());
		Render();
		Hazel::RenderCommandRegister::EndScene();
		
		m_ViewportFramebuffer->Unbind();

		// Resolve to texture2d
		if (m_EnableMSAATex)
			m_ViewportFramebuffer->ResolveMSAATexture((uint32_t)m_LastViewportSize.x, (uint32_t)m_LastViewportSize.y);

		// 再渲染各个CameraComponent
		if (m_ShowCameraComponent)
		{
			m_CameraComponentFramebuffer->Bind();
			Hazel::RenderCommand::SetClearColor(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
			Hazel::RenderCommand::Clear();

			auto gos = m_Scene->GetGameObjectsByComponent<CameraComponent>();

			if (gos.size() > 0)
			{
				const Hazel::GameObject& go = gos[0];
				Hazel::CameraComponent& cam = m_Scene->GetComponentInGameObject<Hazel::CameraComponent>(go);

				Hazel::RenderCommandRegister::BeginScene(cam, go.GetTransformMat());
				Render();
				Hazel::RenderCommandRegister::EndScene();
			}

			m_CameraComponentFramebuffer->Unbind();
		}
	}

	// - Called by Application::Run() as the last layer
	void EditorLayer::OnImGuiRender()
	{
		// 绘制Dockspace的代码
		static bool opt_fullscreen = true;
		static bool opt_padding = false;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->GetWorkPos());
			ImGui::SetNextWindowSize(viewport->GetWorkSize());
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}
		else
		{
			dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
		}

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		if (!opt_padding)
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		static bool s = true;
		static bool* s_bool = &s;
		// 负责主窗口的绘制, 这个窗口的title被隐藏了
		ImGui::Begin("DockSpace Demo", s_bool, window_flags);
		{
			//ImGui::Begin("DockSpace Demo", p_open, window_flags);
			if (!opt_padding)
				ImGui::PopStyleVar();

			if (opt_fullscreen)
				ImGui::PopStyleVar(2);

			// DockSpace
			ImGuiIO& io = ImGui::GetIO();
			if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
			{
				ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
				ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
			}

			// 绘制toolbar
			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Save Scene"))
					{
						// 返回的是绝对路径
						std::optional<std::string> filePath = FileDialogWindowUtils::SaveFile("Hazel Scene (*.scene)\0*.scene\0");

						if (filePath.has_value())
						{
							std::string path = filePath.value();

							if (!hasEnding(path, ".scene"))
								path = path + ".scene";

							if (m_Scene)
								SceneSerializer::Serialize(m_Scene, path.c_str());
						}
					}

					if (ImGui::MenuItem("Load Scene"))
					{
						if (m_Scene)
						{
							std::optional<std::string> filePath = FileDialogWindowUtils::OpenFile("Hazel Scene (*.scene)\0*.scene\0");
							if (filePath.has_value())
							{
								// 前面的Hazel Scene(*.scene)是展示在filter里的text, 后面的*.scene代表显示的文件后缀类型
								if (m_Scene)
								{
									m_Scene->Clear();
									SceneSerializer::Deserialize(m_Scene, filePath.value().c_str());
								}
							}
						}
					}

					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}
		}

		ImGui::Begin("Render Stats");
		{
			auto& stats = Hazel::RenderCommandRegister::GetStatistics();

			ImGui::Text("DrawCalls: %d", stats.DrawCallCnt);
			ImGui::Text("DrawQuads: %d", stats.DrawQuadCnt);
			ImGui::Text("DrawVertices: %d", stats.DrawVerticesCnt());
			ImGui::Text("DrawTiangles: %d", stats.DrawTrianglesCnt());

			ImGui::Checkbox("Show Camera Component Window", &m_ShowCameraComponent);
		}
		ImGui::End();

		// 绘制CameraComponent对应的Window, 感觉放到HazelEditor里比Hazel里更好一点
		if (m_ShowCameraComponent)
		{
			auto wholeWndStartPos = ImGui::GetMainViewport()->Pos;
			auto size = ImGui::GetMainViewport()->Size;

			ImVec2 subWndSize(300, 300);
			ImVec2 subWndStartPos;
			subWndStartPos.x = wholeWndStartPos.x + size.x - subWndSize.x;
			subWndStartPos.y = wholeWndStartPos.y + size.y - subWndSize.y;

			// TODO: optimization, 可以只在resize时才设置窗口大小
			ImGui::SetNextWindowPos(subWndStartPos, ImGuiCond_Always);
			ImGui::SetNextWindowSize(subWndSize);

			ImGui::Begin("Camera View", &m_ShowCameraComponent, ImGuiWindowFlags_NoMove);
			// 好像是出现的ImGui窗口存在Border, 所以要设置280的size
			ImGui::Image(m_CameraComponentFramebuffer->GetColorAttachmentTexture2DId(), {280, 280}, { 0,1 }, { 1,0 });
			ImGui::End();
		}

		ImGui::Begin("Viewport");
		{
			m_ViewportFocused = ImGui::IsWindowFocused();
			m_ViewportHovered = ImGui::IsWindowHovered();

			// 计算viewport窗口内部可视范围的相对坐标(去掉Margin之后的可视部分) 
			auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
			auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
			auto viewportOffset = ImGui::GetWindowPos();
			// 计算Viewport窗口在屏幕坐标系的Rect
			m_ViewportMin = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
			m_ViewportMax = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

			Hazel::Application::Get().GetImGuiLayer()->SetViewportFocusedStatus(m_ViewportFocused);
			Hazel::Application::Get().GetImGuiLayer()->SetViewportHoveredStatus(m_ViewportHovered);

			ImVec2 size = ImGui::GetContentRegionAvail();
			glm::vec2 viewportSize = { size.x, size.y };

			// 当Viewport的Size改变时, 更新Framebuffer的ColorAttachment的Size, 同时调用其他函数
			// 放前面先画, 是为了防止重新生成Framebuffer的ColorAttachment以后, 当前帧渲染会出现黑屏的情况
			if (viewportSize != m_LastViewportSize)
			{
				// 先Resize Framebuffer
				m_ViewportFramebuffer->ResizeColorAttachment((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
				m_EditorCameraController.GetCamera().OnResize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
				m_Scene->OnViewportResized((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
			}

			ImGui::Image(m_ViewportFramebuffer->GetColorAttachmentTexture2DId(), size, { 0,1 }, { 1,0 });


			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM_SCENE"))
				{
					const char* path = (const char*)payload->Data;
					LOG(path);// TODO Load scene files
				}
				ImGui::EndDragDropTarget();
			}

			m_LastViewportSize = viewportSize;

			uint32_t id = m_SceneHierarchyPanel.GetSelectedGameObjectId();
			GameObject selected;
			bool succ = m_Scene->GetGameObjectById(id, selected);
			if (succ)
			{
				bool bOrthographic = m_EditorCameraController.GetCamera().IsOrthographicCamera();
				ImGuizmo::SetOrthographic(bOrthographic);
				ImGuizmo::BeginFrame();

				glm::mat4 v = m_EditorCameraController.GetCamera().GetViewMatrix();
				glm::mat4 p = m_EditorCameraController.GetCamera().GetProjectionMatrix();
				glm::mat4 trans = selected.GetTransformMat();
				EditTransform((float*)(&v), (float*)(&p), (float*)(&trans), true);
				if (trans != selected.GetTransformMat())
					selected.SetTransformMat(trans);
			}
		}
		ImGui::End();

		DrawUIToolbar();	

		ImGui::End();

		//m_ProfileResults.clear();

		// Draw SceneHierarchy
		m_SceneHierarchyPanel.OnImGuiRender();

		// Draw Content Browser
		m_ContentBrowserPanel.OnImGuiRender();
	}


	// 此函数会为每个fbo都调用一次, 比如为Viewport和每个CameraComponent都调用一次
	void EditorLayer::Render()
	{
		std::vector<GameObject> gos = m_Scene->GetGameObjectsByComponent<Hazel::SpriteRenderer>();

		for (size_t i = 0; i < gos.size(); i++)
		{
			Hazel::GameObject& go = gos[i];
			Hazel::SpriteRenderer& sRenderer = go.GetComponent<Hazel::SpriteRenderer>();
			Hazel::Transform& t = go.GetComponent<Hazel::Transform>();
			Hazel::RenderCommandRegister::DrawSpriteRenderer(sRenderer, t.GetTransformMat(), go.GetInstanceId());
		}
	}

	static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
	
	// 思路是绘制一个小窗口, 然后拖到Dock里布局好, 此横向小窗口作为Toolbar, 中间绘制PlayButton
	void EditorLayer::DrawUIToolbar()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		auto& colors = ImGui::GetStyle().Colors;
		const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
		const auto& buttonActive = colors[ImGuiCol_ButtonActive];
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));

		ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			float size = ImGui::GetWindowHeight() - 4.0f;
			std::shared_ptr<Texture2D> icon = m_PlayMode == PlayMode::Edit ? m_IconPlay : m_IconStop;
			ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));
			if (ImGui::ImageButton((ImTextureID)icon->GetTextureId(), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), 0))
			{
				if (m_PlayMode == PlayMode::Edit)
					OnScenePlay();
				else if (m_PlayMode == PlayMode::Play)
					OnSceneStop();
			}

			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(3);
		}
		ImGui::End();
	}

	// 绘制Viewport对应的窗口, 从而绘制gizmos, 传入的是camera的V和P矩阵, matrix的Transform对应的矩阵
	void EditorLayer::EditTransform(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition)
	{
		switch (m_Option)
		{
		case ToolbarOptions::Default:
			break;
		case ToolbarOptions::Translate:
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
			break;
		case ToolbarOptions::Rotation:
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
			break;
		case ToolbarOptions::Scale:
			mCurrentGizmoOperation = ImGuizmo::SCALE;
			break;
		default:
			break;
		}

		static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);
		static bool useSnap = false;
		static float snap[3] = { 1.f, 1.f, 1.f };
		static float bounds[] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
		static float boundsSnap[] = { 0.1f, 0.1f, 0.1f };
		static bool boundSizing = false;
		static bool boundSizingSnap = false;

		ImGuiIO& io = ImGui::GetIO();
		float viewManipulateRight = io.DisplaySize.x;
		float viewManipulateTop = 0;
		static ImGuiWindowFlags gizmoWindowFlags = 0;

		ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImVec2(400, 20), ImGuiCond_Appearing);

		ImGuizmo::SetDrawlist();

		// ImGuizmo的绘制范围应该与Viewport窗口相同, 绘制(相对于显示器的)地点也应该相同
		float windowWidth = (float)ImGui::GetWindowWidth();
		float windowHeight = (float)ImGui::GetWindowHeight();
		ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
		
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		gizmoWindowFlags = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect
			(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;


		//ImGuizmo::DrawGrid(cameraView, cameraProjection, identityMatrix, 100.f);
		//ImGuizmo::DrawCubes(cameraView, cameraProjection, &objectMatrix[0][0], gizmoCount);
		ImGuizmo::Manipulate(cameraView, cameraProjection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix, NULL, 
			useSnap ? &snap[0] : NULL, boundSizing ? bounds : NULL, boundSizingSnap ? boundsSnap : NULL);

		//viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
		//viewManipulateTop = ImGui::GetWindowPos().y;
		//float camDistance = 8.f;
		//ImGuizmo::ViewManipulate(cameraView, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128), 0x10101010);
	}

	void EditorLayer::OnScenePlay()
	{
		m_PlayMode = PlayMode::Play;
		m_Scene->Begin();
	}

	void EditorLayer::OnSceneStop()
	{
		m_PlayMode = PlayMode::Edit;
		m_Scene->Stop();
	}
}	