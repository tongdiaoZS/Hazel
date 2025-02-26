#include "hzpch.h"
#include "glad/glad.h"
#include "OpenGLShader.h"
#include "Hazel/Utils/Utils.h"
#include "Hazel/Renderer/Renderer.h"
#include "Hazel/Renderer/RenderCommand.h"

#include <shaderc/shaderc.hpp>

#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include <Renderer/RenderCommandRegister.h>

namespace Hazel
{
	static unsigned int StringToGLShaderType(const std::string& name)
	{
		if (name == "vertex")
			return GL_VERTEX_SHADER;
		if (name == "fragment")
			return GL_FRAGMENT_SHADER;
		if (name == "geometry")
			return GL_GEOMETRY_SHADER;
		if (name == "compute")
			return GL_COMPUTE_SHADER;

		HAZEL_CORE_ASSERT(0, "Unsupported shader type: " + name);
		return -1;
	}

	float quadVertices[] = {   // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
		// positions	     // texCoords
		-1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
		-1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
					   
		-1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
		 1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f,  1.0f, 1.0f
	};

	const char* vShaderCode = "#version 410 core\n"
		"layout (location = 0) in vec3 aPos;\n"
		"layout (location = 1) in vec2 aTex;\n"
		"layout (location = 0) out vec2 v_TexCoord;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
		"	v_TexCoord = aTex;\n "
		"}\0";

	const char* fShaderCode = "#version 410 core\n"
		"layout(location = 0) in vec2 v_TexCoord;\n"
		"uniform sampler2D screenTexture;\n"
		"out vec4 FragColor;\n"
		"void main()\n"
		"{\n"
		"   FragColor = texture(screenTexture, v_TexCoord);\n"
		"   //FragColor.x = 0.0f;\n"
		"   //FragColor = vec4(v_TexCoord.x, v_TexCoord.y, 0, 1.0f);\n"
		"}\n\0";


	OpenGLShader::OpenGLShader(const std::string & path) : m_FilePath(path)
	{
		// 1. 读取文件内容, Get ShaderSources
		std::ifstream in(path);
		// g是get的意思, seekg会把读取位置放到文件末尾
		in.seekg(0, std::ios::end);
		size_t size = in.tellg();
		// 创建一个含有size个' '字符的字符串
		std::string result(size, ' ');
		in.seekg(0);
		in.read(&result[0], size);


		// 分隔这个result, 获取多个Shader(对C++字符串不熟悉, 这段代码写的比较混乱)
		// TODO: optimization
		std::unordered_map<ShaderType, std::string> shaderSources;
		while (result.find("#type ", 0) != std::string::npos)
		{
			// 先获取#type后面的shader字符串类型
			// 注意, 转义字符是\r\n, windows上代表换行, \r是光标移到行首, \n是光标跳到下一行
			// sizeof("\r\n") = 3, sizeof("/r/n") = 5
			size_t p = result.find_first_of("#type ", 0);
			int c = sizeof("\r\n") - 1;
			size_t pEnd = result.find_first_of("\r\n", p) + 2;// /r/n是四个字符
			size_t typeLength = pEnd - 2 - p - (sizeof("#type ") - 1);
			std::string type = result.substr(p + (sizeof("#type ") - 1), typeLength);
			unsigned int typeID = StringToGLShaderType(type);

			// 然后移除#type这一行以及这一行之前的空白行(如果有的话)
			size = size - pEnd;
			result = result.substr(pEnd, size);
			
			// 找到下一个Shader的开头
			size_t newP = result.find("#type ", 0);

			std::string shaderSrc;
			if (newP == std::string::npos)
				shaderSrc = result;
			else
			{
				shaderSrc = result.substr(0, newP - 1);
				size_t newSize = result.size() - newP;
				result = result.substr(newP, newSize);
			}

			ShaderType t = Utils::GetShaderTypeFromString(type);
			shaderSources[t] = shaderSrc;
		}

		// 2. 编译Shader或直接读取Shader Binary Cache
		CompileOrGetVulkanBinaries(shaderSources);
		CompileOrGetOpenGLBinaries();

		// 3. 创建ShaderProgram
		m_RendererID = glCreateProgram();

		std::vector<GLuint> shaderIDs;
		for (auto&& [stage, spirv] : m_OpenGLSPIRVCache)
		{
			GLuint shaderID = shaderIDs.emplace_back(glCreateShader(Utils::ShaderTypeToOpenGL(stage)));
			glShaderBinary(1, &shaderID, GL_SHADER_BINARY_FORMAT_SPIR_V, spirv.data(), (int)(spirv.size() * sizeof(uint32_t)));
			glSpecializeShader(shaderID, "main", 0, nullptr, nullptr);
			glAttachShader(m_RendererID, shaderID);
		}

		// Link our program
		glLinkProgram(m_RendererID);

		// Note the different functions here: glGetProgram* instead of glGetShader*.
		GLint isLinked = 0;
		glGetProgramiv(m_RendererID, GL_LINK_STATUS, (int *)&isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::unique_ptr<GLchar[]>infoLog = std::make_unique<GLchar[]>(maxLength);
			glGetProgramInfoLog(m_RendererID, maxLength, &maxLength, &infoLog[0]);

			// We don't need the program anymore.
			glDeleteProgram(m_RendererID);
		
			// Don't leak shaders either.
			for (GLuint item: shaderIDs)
			{
				glDeleteShader(item);
			}

			// Use the infoLog as you see fit.
			CORE_LOG_ERROR("Link Shaders Failed!:{0}", infoLog);// TODO这里应该是错的
			HAZEL_ASSERT(false, "Link  Shaders Error Stopped Debugging!");

			// In this simple program, we'll just leave
			return;
		}

		// Always detach shaders after a successful link.
		for (GLuint item : shaderIDs)
		{
			glDetachShader(m_RendererID, item);
		}
	}

	OpenGLShader::OpenGLShader(const std::string& vertSource, const std::string& fragSource)
	{
		// Create an empty vertex shader handle
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);

		// Send the vertex shader source code to GL
		// Note that std::string's .c_str is NULL character terminated.
		const GLchar *source = (const GLchar *)vertSource.c_str();
		glShaderSource(vertexShader, 1, &source, 0);

		// Compile the vertex shader
		glCompileShader(vertexShader);

		GLint isCompiled = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetShaderInfoLog(vertexShader, maxLength, &maxLength, &infoLog[0]);

			// We don't need the shader anymore.
			glDeleteShader(vertexShader);

			// Use the infoLog as you see fit.
			CORE_LOG_ERROR("Compile Vertex Shader Failed!:{0}", &infoLog[0]);
			HAZEL_ASSERT(false, "Compile Vertex Shader Error Stopped Debugging!");

			// In this simple program, we'll just leave
			return;
		}

		// Create an empty fragment shader handle
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

		// Send the fragment shader source code to GL
		// Note that std::string's .c_str is NULL character terminated.
		source = (const GLchar *)fragSource.c_str();
		glShaderSource(fragmentShader, 1, &source, 0);

		// Compile the fragment shader
		glCompileShader(fragmentShader);

		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &infoLog[0]);

			// We don't need the shader anymore.
			glDeleteShader(fragmentShader);
			// Either of them. Don't leak shaders.
			glDeleteShader(vertexShader);

			// Use the infoLog as you see fit.
			CORE_LOG_ERROR("Compile Fragment Shader Failed!:{0}", &infoLog[0]);
			HAZEL_ASSERT(false, "Compile Fragment Shader Error Stopped Debugging!");

			// In this simple program, we'll just leave
			return ;
		}

		// Vertex and fragment shaders are successfully compiled.
		// Now time to link them together into a program.
		// Get a program object.
		m_RendererID = glCreateProgram();

		// Attach our shaders to our program
		glAttachShader(m_RendererID, vertexShader);
		glAttachShader(m_RendererID, fragmentShader);

		// Link our program
		glLinkProgram(m_RendererID);

		// Note the different functions here: glGetProgram* instead of glGetShader*.
		GLint isLinked = 0;
		glGetProgramiv(m_RendererID, GL_LINK_STATUS, (int *)&isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(m_RendererID, maxLength, &maxLength, &infoLog[0]);

			// We don't need the program anymore.
			glDeleteProgram(m_RendererID);
			// Don't leak shaders either.
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);

			// Use the infoLog as you see fit.
			CORE_LOG_ERROR("Link Shaders Failed!:{0}", infoLog[0]);// TODO这里应该是错的
			HAZEL_ASSERT(false, "Link  Shaders Error Stopped Debugging!");

			// In this simple program, we'll just leave
			return;
		}

		// Always detach shaders after a successful link.
		glDetachShader(m_RendererID, vertexShader);
		glDetachShader(m_RendererID, fragmentShader);
	}

	OpenGLShader::~OpenGLShader()
	{
		glDeleteProgram(m_RendererID);
	}

	void Hazel::OpenGLShader::Bind()
	{
		glGetIntegerv(GL_CURRENT_PROGRAM, &m_ProgramIdBeforeBind);
		glUseProgram(m_RendererID);
	}

	void OpenGLShader::Unbind()
	{
		glUseProgram(m_ProgramIdBeforeBind);
	}

	// shaderSources的每个pair对应一个shader, 比如fragment shader, 本函数会为每个pair生成对应的vulkan类型的binary文件
	void OpenGLShader::CompileOrGetVulkanBinaries(const std::unordered_map<ShaderType, std::string>& shaderSources)
	{
		GLuint program = glCreateProgram();

		// 1. 创建编译器: shaderc提供的glslang编译器
		shaderc::Compiler compiler;
		shaderc::CompileOptions options;

		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
		const bool optimize = true;
		if (optimize)
			options.SetOptimizationLevel(shaderc_optimization_level_performance);

		std::filesystem::path cacheDirectory = Utils::GetShaderCacheRelativeDirectory();

		std::unordered_map<ShaderType, std::vector<uint32_t>>& shaderData = m_VulkanSPIRVCache;
		shaderData.clear();

		std::filesystem::path shaderPath = m_FilePath;
		const std::string& shaderName = shaderPath.replace_extension().filename().string();

		// 2. 遍历并编译每个shader, 比如vert或frag, 为其生成一个单独的binary文件
		for (auto&& [stage, source] : shaderSources)
		{
			std::filesystem::path shaderFilePath = m_FilePath;
			std::filesystem::path cachedPath = cacheDirectory.string() + "\\" + shaderName + "\\" + Utils::ShaderTypeToString(stage) + ".vulkan_shadercache";
			
			// 传入的是相对路径, 且如果不存在文件会自动创建空文件
			std::ifstream in(cachedPath, std::ios::in | std::ios::binary);
			// 如果有现成的缓存文件, 那么直接读取该文件, 存到data里
			if (in.is_open())
			{
				in.seekg(0, std::ios::end);
				auto size = in.tellg();
				in.seekg(0, std::ios::beg);

				auto& data = shaderData[stage];
				data.resize(size / sizeof(uint32_t));
				in.read((char*)data.data(), size);
			}
			// 否则编译出新的文件
			else
			{
				// 把glsl文件编译为Spir-v
				shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, Utils::ShaderTypeToShaderCKind(stage), m_FilePath.c_str(), options);
				if (module.GetCompilationStatus() != shaderc_compilation_status_success)
				{
					CORE_LOG_ERROR(module.GetErrorMessage());
					HAZEL_CORE_ASSERT(false, Utils::ShaderTypeToString(stage) + std::string("Shader Compile to SpirV error"));
				}

				shaderData[stage] = std::vector<uint32_t>(module.cbegin(), module.cend());


				std::filesystem::path f = cachedPath;
				std::filesystem::path dir = f.parent_path();
				if (!std::filesystem::exists(dir))
					std::filesystem::create_directories(dir);

				std::ofstream out(cachedPath, std::ios::out | std::ios::binary);
				if (out.is_open())
				{
					auto& data = shaderData[stage];
					out.write((char*)data.data(), data.size() * sizeof(uint32_t));
					out.flush();
					out.close();
				}
			}
		}

		// 3. 所有的shader都读到shaderData, 目前只是从compiler里获取每个shader里的信息而已(严格来说, 这段代码不应该出现在编译shader函数里)
		for (auto&& [stage, data] : shaderData)
			Reflect(stage, data);
	}


	void OpenGLShader::CompileOrGetOpenGLBinaries()
	{
		auto& shaderData = m_OpenGLSPIRVCache;

		// 一些谷歌提供的shaderc的写法
		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);
		const bool optimize = false;
		if (optimize)
			options.SetOptimizationLevel(shaderc_optimization_level_performance);

		// 获取shader缓存目录
		std::filesystem::path cacheDirectory = Utils::GetShaderCacheRelativeDirectory();
		shaderData.clear();
		
		std::filesystem::path shaderPath = m_FilePath;
		const std::string& shaderName = shaderPath.replace_extension().filename().string();

		// 直接利用先刚刚OpenGL编译出的Spir-V, 再返回得到OpenGL版本的binary, 这样应该能避免反复编译shader代码
		// 毕竟从vulkan版本的binary转换到opengl的binary应该会更快
		for (auto&& [stage, spirv] : m_VulkanSPIRVCache)
		{
			std::filesystem::path shaderFilePath = m_FilePath;
			std::filesystem::path cachedPath = cacheDirectory.string() + "\\" + shaderName + "\\" + Utils::ShaderTypeToString(stage) + ".opengl_shadercache";

			std::ifstream in(cachedPath, std::ios::in | std::ios::binary);
			if (in.is_open())
			{
				in.seekg(0, std::ios::end);
				auto size = in.tellg();
				in.seekg(0, std::ios::beg);

				auto& data = shaderData[stage];
				data.resize(size / sizeof(uint32_t));
				in.read((char*)data.data(), size);
			}
			else
			{
				spirv_cross::CompilerGLSL glslCompiler(spirv);
				m_OpenGLSourceCode[stage] = glslCompiler.compile();
				auto& source = m_OpenGLSourceCode[stage];

				shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, Utils::ShaderTypeToShaderCKind(stage), m_FilePath.c_str());

				if (module.GetCompilationStatus() != shaderc_compilation_status_success)
				{
					CORE_LOG_ERROR(module.GetErrorMessage());
					HAZEL_CORE_ASSERT(false, "");
				}

				shaderData[stage] = std::vector<uint32_t>(module.cbegin(), module.cend());

				std::filesystem::path f = cachedPath;
				std::filesystem::path dir = f.parent_path();
				if (!std::filesystem::exists(dir))
					std::filesystem::create_directories(dir);

				std::ofstream out(cachedPath, std::ios::out | std::ios::binary);
				if (out.is_open())
				{
					auto& data = shaderData[stage];
					out.write((char*)data.data(), data.size() * sizeof(uint32_t));
					out.flush();
					out.close();
				}
			}
		}
	}


	void OpenGLShader::Reflect(ShaderType stage, const std::vector<uint32_t>& shaderData)
	{
		spirv_cross::Compiler compiler(shaderData);
		// 通过编译器获取ShaderResources
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		//CORE_LOG("OpenGLShader::Reflect - {0} {1}", Utils::GLShaderStageToString(stage), m_FilePath);
		CORE_LOG("    {0} uniform buffers", resources.uniform_buffers.size());
		CORE_LOG("    {0} resources", resources.sampled_images.size());

		CORE_LOG("Uniform buffers:");
		for (const auto& resource : resources.uniform_buffers)
		{
			const auto& bufferType = compiler.get_type(resource.base_type_id);
			size_t bufferSize = compiler.get_declared_struct_size(bufferType);
			size_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			size_t memberCount = bufferType.member_types.size();

			CORE_LOG("  {0}", resource.name);
			CORE_LOG("    Size = {0}", bufferSize);
			CORE_LOG("    Binding = {0}", binding);
			CORE_LOG("    Members = {0}", memberCount);
		}
	}

	void checkCompileErrors(unsigned int shader, std::string type)
	{
		int success;
		char infoLog[1024];
		if (type != "PROGRAM")
		{
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				glGetShaderInfoLog(shader, 1024, NULL, infoLog);
				std::cout << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
			}
		}
		else
		{
			glGetProgramiv(shader, GL_LINK_STATUS, &success);
			if (!success)
			{
				glGetProgramInfoLog(shader, 1024, NULL, infoLog);
				std::cout << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
			}
		}
	}

	void OpenGLShader::UploadUniformMat4(const std::string & uniformName, const glm::mat4& matrix)
	{
		// glm::value_ptr返回的类型是GLfloat*, 应该是个数组
		glUniformMatrix4fv(
			glGetUniformLocation(m_RendererID, uniformName.c_str()),
			1, GL_FALSE, glm::value_ptr(matrix));
	}

	void OpenGLShader::UploadUniformVec4(const std::string & uniformName, const glm::vec4& vec4)
	{
		glUniform4fv(
			glGetUniformLocation(m_RendererID, uniformName.c_str()),
			1, glm::value_ptr(vec4)
		);
	}

	void OpenGLShader::UploadUniformI1(const std::string & uniformName, int id)
	{
		glUniform1i(glGetUniformLocation(m_RendererID, uniformName.c_str()), id); // set it manually
	}

	void OpenGLShader::UploadUniformF1(const std::string & uniformName, float number)
	{
		glUniform1f(glGetUniformLocation(m_RendererID, uniformName.c_str()), number); // set it manually
	}

	void OpenGLShader::UploadUniformIntArr(const std::string & uniformName, int count, int* values)
	{
		glUniform1iv(glGetUniformLocation(m_RendererID, uniformName.c_str()), count, values);
	}

	void OpenGLShader::CreateDownScaleFramebuffer()
	{
		GLuint last_array_buffer; 
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint*)&last_array_buffer);


		unsigned int vertex, fragment;
		// vertex shader
		vertex = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertex, 1, &vShaderCode, NULL);
		glCompileShader(vertex);
		checkCompileErrors(vertex, "VERTEX");
		// fragment Shader
		fragment = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragment, 1, &fShaderCode, NULL);
		glCompileShader(fragment);
		checkCompileErrors(fragment, "FRAGMENT");
		// shader Program
		m_TempProgramId = glCreateProgram();
		glAttachShader(m_TempProgramId, vertex);
		glAttachShader(m_TempProgramId, fragment);
		glLinkProgram(m_TempProgramId);
		checkCompileErrors(m_TempProgramId, "PROGRAM");
		// delete the shaders as they're linked into our program now and no longer necessary
		glDeleteShader(vertex);
		glDeleteShader(fragment);


		// configure second post-processing framebuffer
		glGenFramebuffers(1, &resolveFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO);

		// create first color attachment texture
		glGenTextures(1, &screenTexture);
		glBindTexture(GL_TEXTURE_2D, screenTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 800, 600, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexture, 0);	// we only need a color buffer


		// create second color attachment texture
		glGenTextures(1, &instanceIdTexture);
		glBindTexture(GL_TEXTURE_2D, instanceIdTexture);
		// R32I应该是代表32位interger, 意思是这32位都只存一个integer
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, 800, 600, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, instanceIdTexture, 0);	// we only need a color buffer

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			CORE_LOG_ERROR("ERROR::FRAMEBUFFER:: Intermediate framebuffer is not complete!");

		unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glDrawBuffers(2, attachments);

		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// setup screen VAO
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

		glUseProgram(m_TempProgramId);
		glUniform1i(glGetUniformLocation(m_TempProgramId, "screenTexture"), 0); // set it manually


		// 由于在RenderCommand调用DrawCall时只绑定了VAO, 没绑定VBO, 这里需要重新绑定一下
		glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);

		return;
	}
	
	void OpenGLShader::DrawDownScaleFramebuffer(uint32_t MSAAbuffer, uint32_t buffer, uint32_t width, uint32_t height)
	{
		GLuint last_array_buffer;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint*)&last_array_buffer);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
		// draw Screen quad
		glUseProgram(m_TempProgramId);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glActiveTexture(GL_TEXTURE0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, screenTexture); // use the now resolved color attachment as the quad's texture
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// 2. now blit multisampled buffer(s) to normal colorbuffer of intermediate FBO. Image is stored in screenTexture
		glBindFramebuffer(GL_READ_FRAMEBUFFER, MSAAbuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffer);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, MSAAbuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffer);
		glDrawBuffer(GL_COLOR_ATTACHMENT1);
		glReadBuffer(GL_COLOR_ATTACHMENT1);
		glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);


		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glUseProgram(0);


		// 由于在RenderCommand调用DrawCall时只绑定了VAO, 没绑定VBO, 这里需要重新绑定一下
		glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
	}
}
