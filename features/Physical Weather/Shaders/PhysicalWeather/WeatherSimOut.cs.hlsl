
#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

#include "PhysicalWeather/Common.hlsli"


cbuffer SettingsCB: register(b1)
{
    float2 qcRange;
    float2 uRange;
};

cbuffer JumpFloodCB: register(b2)
{
    int3 stride;
    float _pad;
};

Texture3D<float> TexQC : register(t0);
Texture3D<float3> TexU : register(t1);
Texture3D<uint4> TexSite : register(t2);

// w - low to high: is cloud, has seed
Texture3D<uint4> TexOldSite : register(t0);
RWTexture3D<uint4> RWTexSite: register(u0);

RWTexture3D<float3> RWTexCloudMap: register(u0);
RWTexture3D<float> RWTexSDF: register(u1);

//////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(8, 8, 1)] void mask(int3 tid : SV_DispatchThreadID){
    float qc = TexQC[tid].x;
    RWTexSite[tid] = uint4(0, 0, 0, qc >= qcRange.x);
}

[numthreads(8, 8, 1)] void jumpFlood(int3 tid : SV_DispatchThreadID){
    int3 dims;
    RWTexSite.GetDimensions(dims.x, dims.y, dims.z);

    uint4 bestSeed = TexOldSite[tid];
    float bestDist = (bestSeed.w & 2) ? length(bestSeed.xyz - tid) : 1e10;

    for (int dz = -1; dz <= 1; dz++){
        for (int dy = -1; dy <= 1; dy++){
            for (int dx = -1; dx <= 1; dx++)
            {
                if (dx == 0 && dy == 0 && dz == 0)
                    continue;

                int3 offset = int3(dx, dy, dz) * stride;
                int3 samplePos = tid + offset;

                if (any(samplePos < 0) || any(samplePos >= dims))
                    continue;

                uint4 neighborSeed = TexOldSite[samplePos];
                int3 seedPos = neighborSeed.xyz;
                if((bestSeed.w & 1) != (neighborSeed.w & 1))
                    seedPos = samplePos;
                else if(!(neighborSeed.w & 2))
                    continue;

                float dist = length(seedPos - tid);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestSeed.xyz = seedPos;
                    bestSeed.w = bestSeed.w | 2;
                }
            }
        }
    }

    RWTexSite[tid] = bestSeed;
}

[numthreads(8, 8, 1)] void assemble(int3 tid : SV_DispatchThreadID)
{
    const static float dimProfileDepth = 0.3 / 1.428e-5f;  // 300 m

    float qc = TexQC[tid].x;
    float3 u = TexU[tid];
    uint4 seed = TexSite[tid];

    bool isCloud = seed.w & 1;
    bool hasSeed = seed.w & 2;
    float dist = (length(TexSite[tid].xyz - tid) - 1.4) * CLOUD_RANGE.x / CLOUD_DIM.x;

    float dimensionalProfile = isCloud ? (hasSeed ? saturate(dist / dimProfileDepth) : 1) : 0;
    RWTexCloudMap[tid] = float3(dimensionalProfile, Remap(u.z, uRange.x, uRange.y, 1, 0), Remap(qc, qcRange.x, qcRange.y, 0.5, 1));

    // RWTexSDF[tid] = isCloud ? 0 : (hasSeed ? dist : CLOUD_RANGE.x * 0.5);
    RWTexSDF[tid] = 0;
}
