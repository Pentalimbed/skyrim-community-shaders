Texture2D<half4> srcGI : register(t0);
Texture2D<half4> srcAlbedo : register(t1);

RWTexture2D<half3> outGI : register(u0);

[numthreads(8, 8, 1)] void main(uint2 dtid
								: SV_DispatchThreadID) {
	half3 o = outGI[dtid];
	half4 i = srcGI[dtid];
	half3 gi = i.rgb * srcAlbedo[dtid].rgb;
	outGI[dtid] = o * i.w + gi;
}