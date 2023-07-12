#pragma once
namespace RE
{
	namespace BSGraphics
	{
		class Texture
		{
		public:
			ID3D11Texture2D* m_Texture;
			char _pad0[0x8];
			ID3D11ShaderResourceView* m_ResourceView;
		};
	}
}