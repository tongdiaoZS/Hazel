#pragma once
#include "Hazel/Renderer/Shader.h"


namespace Hazel
{
	struct glm::mat4;
	struct glm::vec4;

	class OpenGLShader : public Shader
	{
	public:
		OpenGLShader(const std::string& path);
		OpenGLShader(const std::string& vertSource, const std::string& fragSource);
		~OpenGLShader() override;

		void Bind() override;
		void Unbind() override;//其实这个函数一般运行时不会用到，一般用于Debug，因为使用的时候会直接Bind新的，就会自动Unbind这个
		void UploadUniformMat4(const std::string& uniformName, const glm::mat4& matrix) override;
		void UploadUniformVec4(const std::string& uniformName, const glm::vec4& vec4) override;
		void UploadUniformI1(const std::string& uniformName, int id) override;
		void UploadUniformF1(const std::string& uniformName, float number) override;
		void UploadUniformIntArr(const std::string& uniformName, int count, int* number) override;

		void CreateDownScaleFramebuffer();
		void DrawDownScaleFramebuffer(uint32_t MSAAbuffer, uint32_t buffer, uint32_t width, uint32_t height);

	private:
		void CompileOrGetOpenGLBinaries();
		void CompileOrGetVulkanBinaries(const std::unordered_map<ShaderType, std::string>& shaderSources);
		void Reflect(ShaderType stage, const std::vector<uint32_t>& shaderData);

	private:
		// m_VulkanSPIRVCache相当于一个缓存的unordered_map, key是子着色器类型
		// value是一个uint32的数组, 代表着Spir-V对应的编译结果
		std::unordered_map<ShaderType, std::vector<uint32_t>> m_VulkanSPIRVCache;
		std::unordered_map<ShaderType, std::vector<uint32_t>> m_OpenGLSPIRVCache;

		std::unordered_map<ShaderType, std::string> m_OpenGLSourceCode;

		std::string m_FilePath;

		int m_ProgramIdBeforeBind = -1;

	public:
		// TODO: To be refactered
		uint32_t resolveFBO;
		uint32_t quadVAO, quadVBO;
		uint32_t screenTexture;
		uint32_t instanceIdTexture;


		int m_TempProgramId;
	};
}