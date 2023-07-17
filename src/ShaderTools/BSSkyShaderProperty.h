#pragma once

namespace RE
{
	class BSSkyShaderProperty : public BSShaderProperty
	{
	public:
		enum SkyObject
		{
			SO_SUN = 0x0,
			SO_SUN_GLARE = 0x1,
			SO_ATMOSPHERE = 0x2,
			SO_CLOUDS = 0x3,
			SO_SKYQUAD = 0x4,
			SO_STARS = 0x5,
			SO_MOON = 0x6,
			SO_MOON_SHADOW = 0x7,
		};

		NiColorA kBlendColor;
		NiSourceTexture* pBaseTexture;
		NiSourceTexture* pBlendTexture;
		char _pad0[0x10];
		float fBlendValue;
		uint16_t usCloudLayer;
		bool bFadeSecondTexture;
		uint32_t uiSkyObjectType;
	};
	static_assert(sizeof(BSSkyShaderProperty) == 0xC8);
}  // namespace RE
