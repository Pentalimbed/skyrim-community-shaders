#pragma once

namespace Hooks
{
	struct BSShaderManager_GetTexture
	{
		static void thunk(const char* a_path, bool a_arg1, RE::NiPointer<RE::NiSourceTexture>& a_texture, bool a_arg3);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void Install();
}
