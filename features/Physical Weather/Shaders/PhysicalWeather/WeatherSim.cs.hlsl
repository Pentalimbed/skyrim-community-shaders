/*
    MAJOR REFERENCES
        https://github.com/ShaneFX/GAMES201/blob/master/HW01/Smoke3d/smoke_3D.py
        [SF] Stam, J. Stable fluids. in Proceedings of the 26th annual conference on computer graphics and interactive techniques 121–128 (ACM Press/Addison-Wesley Publishing Co., USA, 1999).
        [SS] Hädrich, T. et al. Stormscapes: simulating cloud dynamics in the now. ACM Trans. Graph. 39, (2020).
        [WS] Herrera, J. A. A. et al. Weatherscapes: nowcasting heat transfer and water continuity. ACM Trans. Graph. 40, (2021).
        Minor references are listed next to their code.
*/

#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

#include "PhysicalWeather/Common.hlsli"
#include "Common/Random.hlsli"

Texture2D Tex2d1 : register(t0);
Texture2D Tex2d2 : register(t1);
Texture2D Tex2d3 : register(t2);
Texture2D Tex2d4 : register(t3);

Texture3D Tex3d1 : register(t4);
Texture3D Tex3d2 : register(t5);
Texture3D Tex3d3 : register(t6);
Texture3D Tex3d4 : register(t7);
Texture3D Tex3d5 : register(t8);
Texture3D Tex3d6 : register(t9);
Texture3D Tex3d7 : register(t10);
Texture3D Tex3d8 : register(t11);

RWTexture2D<float4> RWTex2d1 : register(u0);
RWTexture2D<float4> RWTex2d2 : register(u1);

RWTexture3D<float4> RWTex3d1 : register(u2);
RWTexture3D<float4> RWTex3d2 : register(u3);
RWTexture3D<float4> RWTex3d3 : register(u4);
RWTexture3D<float4> RWTex3d4 : register(u5);
RWTexture3D<float4> RWTex3d5 : register(u6);
RWTexture3D<float4> RWTex3d6 : register(u7);
RWTexture3D<float4> RWTex3d7 : register(u8);

cbuffer CB: register(b0)
{
    float dt;
    float3 debugValues;
    float tmpOcean;
    float epsConf;
    float2 globalWind;
};

////////////////////////////////////////////////////////////////////////////////////////

const static float3 CELL_SIZE = CLOUD_RANGE_M / CLOUD_DIM;
const static float3 RCP_CELL_SIZE = rcp(CELL_SIZE);

const static float kelvin0C = 273.15; // kelvin at 0C

const static float qFull = 1.0; // TODO zero irradiance cloud coverage
const static float boltzmann = 5.67e-8;
const static float soilThickness = 10; // m

const static float rUniGas = 8314e-3; // J K^-1 mol^−1, universal gas constant
const static float molAir = 28.96e-3; // kg mol^-1, molar mass of air, [SS]
const static float molVapor = 18.02e-3; // kg mol^-1, molar mass of vapor, [SS]
const static float gammaAir = 1.4; // isentropic exponent of air, [SS]
const static float gammaVapor = 1.33; // isentropic exponent of vapor, [SS]

const static float rouAir = 1.293; // kg m^-3, density of air
const static float rSpecificAir = rUniGas / molAir; // J K^-1 kg^−1, specific gas constant of dry air
const static float diffAir = 1.48e-5;  // m^2 s^-1, diffusivity/kinematic viscosity of air, https://www.engineersedge.com/physics/viscosity_of_air_dynamic_and_kinematic_14483.htm
const static float cpAir = 1005; // J kg^-1 K^-1, specific heat capacity of dry air
const static float latentFusion = 3.34e5; // J kg^-1, latent heat of fusion of water
const static float latentVapor = 3.5e5; // J kg^-1, latent heat of vaporization of water
const static float latentSub = 3.5e5; // J kg^-1, latent heat of sublimation of water

const static float g = 9.8; // m s^-2, gravity constant
const static float pOcean = 101325; // Pa, pressure at ocean level
const static float tmpLapse = -6.5e-3f; // K m^-1, temperature lapse rate below inversion layer
const static float tmpLapseInv = 0; // K m^-1, temperature lapse rate at inversion layer
const static float zInv = 8000; // m, altitude of temperature inversion

////////////////////////////////////////////////////////////////////////////////////////

// define ground properties for sand/silt/clay
#define GMAT(name, vSand, vSilt, vClay) \
const static float name##Sand = vSand; \
const static float name##Silt = vSilt; \
const static float name##Clay = vClay; 

#define GMAT_BLEND(name) (name##Sand * groundComposition.x + name##Silt * groundComposition.y + name##Clay * (1 - groundComposition.x - groundComposition.y))

// https://structx.com/Soil_Properties_007.html
// m s^-1, hydraulic conductivity
GMAT(ksat, 1.76e-4, 7.19e-6, 1.28e-6)
// https://agupubs.onlinelibrary.wiley.com/doi/full/10.1002/2013WR014872
// m^2 s^-1, water diffusitivity
GMAT(diff, 961e-6 / 3600 / 24, 394e-6 / 3600 / 24, 334e-6 / 3600 / 24)
// https://agupubs.onlinelibrary.wiley.com/doi/full/10.1002/2013WR014872
// s, evaporative ground coefficient
GMAT(phi, 5.107 * 3600 * 24, 29.674 * 3600 * 24, 9.363 * 3600 * 24)
// http://www.frontiersin.org/journals/earth-science/articles/10.3389/feart.2022.864548/full
// https://www.sciencedirect.com/science/article/pii/S1537511003001120
// J kg^-1 K^-1, specific heat capacity
GMAT(cp, 1e3, 1.35e3, 1.7e3);
// https://www.sciencepartners.info/smsp/module05/m05-a17c.html
// albedo
GMAT(albedo, 0.3, 0.25, 0.2);
// https://www.sciencedirect.com/science/article/pii/S1674775516300944
// emissivity
GMAT(eps, 0.9, 0.93, 0.95);
// https://structx.com/Soil_Properties_002.html
// kg m^-3, density
GMAT(rou, 1430, 1380, 1330);

////////////////////////////////////////////////////////////////////////////////////////

// absolute to potential temperature
float Abs2PotTmp(float tmp, float p){
    return tmp * pow(pOcean / p, 0.286); 
}
// potential to absolute temperature
float Pot2AbsTmp(float theta, float p){
    return theta * pow(p / pOcean , 0.286); 
}
// temperature profile by altitude, [WS] eq. 1
float TmpProfile(float z, float z0, float tmp0) { 
    return tmp0 + (z > zInv ? tmpLapse * (zInv - z0) + tmpLapseInv * (z - zInv) : tmpLapse * (z - z0));
}
// pressure profile by altitude, [WS] eq. 2 (It's different from [SS]?)
// let's just follow wiki: https://en.wikipedia.org/wiki/Barometric_formula
float PressureProfile(float z, float z0, float tmp0) {
    return pOcean * pow(1 - tmpLapse / tmp0 * (z - z0), g / rSpecificAir / tmpLapse);
}
// temperature in the rising thermal, [SS] eq. 10
float TmpThermal(float p, float tmp0, float p0, float gammaThermal) {
    return tmp0 * pow(p / p0, (gammaThermal - 1) / gammaThermal);
}
// wind scaling profile by altitude, fit of [SS] fig. 8
float WindProfile(float z){
    float zKm = z * 1e-3;
    return -0.00025 * zKm * zKm * zKm - 0.01 * zKm * zKm + 0.275 * zKm + 0.5;
}
// terminal speed of rain, [WS] eq. 45 + Appendix B
float SpeedTermRain(float q){
    if (q < 1e-10)
        return 0;
    float lambda = pow(Math::PI * 0.99 * 8e-2 / rouAir * 1e3 / q, 0.25); // cm-1
    float u = 2115 * 17.8379 / (6 * pow(lambda, 0.8)) * sqrt(0.99 / rouAir * 1e3); // cm/s
    return u * 1e-2;
}
// terminal speed of snow, [WS] eq. 46 + Appendix B
float SpeedTermSnow(float q){
    if (q < 1e-10)
        return 0;
    float lambda = pow(Math::PI * 0.11 * 3e-2 / rouAir * 1e3 / q, 0.25); // cm-1
    float u = 152.93 * 8.28509 / (6 * pow(lambda, 0.25)) * sqrt(0.11 / rouAir * 1e3); // cm/s
    return u * 1e-2;
}
// terminal speed of ice, [WS] eq. 47 + Appendix B
float SpeedTermIce(float q){
    if (q < 1e-10)
        return 0;
    float lambda = pow(Math::PI * 0.91 * 4e-2 / rouAir * 1e3 / q, 0.25); // cm-1
    float u = sqrt(4 / 1.8) * 1.590155 / (6 * pow(lambda, 0.5)) * sqrt(0.91 / rouAir * 1e3); // cm/s
    return u * 1e-2;
}
// equilibrium vapor pressure over liquid water, [WS] eq. 15
float EqVpWater(float tmp){
    tmp -= kelvin0C;
    return 611.2 * exp(17.62 * tmp / (tmp + 243.12));
}
// equilibrium vapor pressure over ice, [WS] eq. 24
float EqVpIce(float tmp){
    tmp -= kelvin0C;
    return 611.2 * exp(24.46 * tmp / (tmp + 272.62));
}
// saturation mixing ratio of water vapor, [WS] eq. 16  
float SatMixingRatioWater(float tmp, float p) {
    return 0.622 / p * EqVpWater(tmp);
}
// saturation mixing ratio of ice, [WS] eq. 27  
float SatMixingRatioIce(float tmp, float p) {
    return 0.622 / p * EqVpIce(tmp);
}

////////////////////////////////////////////////////////////////////////////////////////

float4 pad4(float x) { return float4(x, 0, 0, 0); }
float4 pad4(float3 x) { return float4(x, 0); }

interface IFetcher2D
{
    float4 Fetch(Texture2D tex, int2 px);
};

interface IFetcher3D
{
    float4 Fetch(Texture3D tex, int3 px);
};

// Linear sampling simulation for 2D textures
float4 LinearSample2D(IFetcher2D fetcher, Texture2D tex, float2 uv)
{
    float2 pixel = uv * CLOUD_DIM.xy - 0.5;
    int2 base = int2(floor(pixel));
    float2 f = frac(pixel);

    float4 c00 = fetcher.Fetch(tex, base + int2(0, 0));
    float4 c10 = fetcher.Fetch(tex, base + int2(1, 0));
    float4 c01 = fetcher.Fetch(tex, base + int2(0, 1));
    float4 c11 = fetcher.Fetch(tex, base + int2(1, 1));

    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

// Linear sampling simulation for 3D textures
float4 LinearSample3D(IFetcher3D fetcher, Texture3D tex, float3 uv)
{
    float3 pixel = uv * CLOUD_DIM.xyz - 0.5;
    int3 base = int3(floor(pixel));
    float3 f = frac(pixel);

    float4 c000 = fetcher.Fetch(tex, base + int3(0, 0, 0));
    float4 c100 = fetcher.Fetch(tex, base + int3(1, 0, 0));
    float4 c010 = fetcher.Fetch(tex, base + int3(0, 1, 0));
    float4 c110 = fetcher.Fetch(tex, base + int3(1, 1, 0));
    float4 c001 = fetcher.Fetch(tex, base + int3(0, 0, 1));
    float4 c101 = fetcher.Fetch(tex, base + int3(1, 0, 1));
    float4 c011 = fetcher.Fetch(tex, base + int3(0, 1, 1));
    float4 c111 = fetcher.Fetch(tex, base + int3(1, 1, 1));

    float4 c00 = lerp(c000, c100, f.x);
    float4 c10 = lerp(c010, c110, f.x);
    float4 c01 = lerp(c001, c101, f.x);
    float4 c11 = lerp(c011, c111, f.x);

    float4 c0 = lerp(c00, c10, f.y);
    float4 c1 = lerp(c01, c11, f.y);

    return lerp(c0, c1, f.z);
}

// Velocity boundary conditions:
// Bottom: no-slip
// Top: slip-free
// Sides: global wind profile
class VelocityFetcher : IFetcher3D {
    float z0; // height of terrain
    
    float4 Fetch(Texture3D tex, int3 px){
        if ((px.z + 1) * CELL_SIZE.z <= z0) // allowing voxels that's partially ground
            return 0;
        const bool overtop = px.z >= (int)CLOUD_DIM.z;
        const float z = (px.z + 0.5) * CELL_SIZE.z;
        const int3 new_px = clamp(px, 0, CLOUD_DIM - 1);
        float3 retval = any(px != new_px) ? WindProfile(z) * float3(globalWind, 0) : tex[new_px].xyz;
        if (overtop)
            retval.z = 0;
        return pad4(retval);
    }
};

// qv boundary conditions:
// Bottom: zeroed
// Top: zeroed
// Sides: periodic
class VaporFetcher : IFetcher3D {
    float z0; // height of terrain

    float4 Fetch(Texture3D tex, int3 px){
        if ((px.z + 1) * CELL_SIZE.z <= z0 || px.z >= (int)CLOUD_DIM.z) 
            return 0;
        px = px % CLOUD_DIM;
        return tex[px];
    }
};

// qj boundary conditions: pad 0
class ContentFetcher : IFetcher3D {
    float z0; // height of terrain

    float4 Fetch(Texture3D tex, int3 px){
        if ((px.z + 1) * CELL_SIZE.z <= z0 || any(px.xy < 0)  || any(px >= int3(CLOUD_DIM))) 
            return 0;
        px = clamp(px, 0, CLOUD_DIM - 1);
        return tex[px];
    }
};

// theta boundary conditions: ambient temperature
class ThetaFetcher : IFetcher3D {
    float z0; // height of terrain

    float4 Fetch(Texture3D tex, int3 px){
        if ((px.z + 1) * CELL_SIZE.z <= z0 || any(px.xy < 0) || any(px >= int3(CLOUD_DIM))) {
            float z = (px.z + 0.5) * CELL_SIZE.z;
            return pad4(Abs2PotTmp(TmpProfile(z, 0, tmpOcean), PressureProfile(z, 0, tmpOcean)));
        }
        px = clamp(px, 0, CLOUD_DIM - 1);
        return tex[px];
    }
};

class PadFetcher : IFetcher3D{
    float z0; // height of terrain
    float4 vPad;

    float4 Fetch(Texture3D tex, int3 px){
        if ((px.z + 1) * CELL_SIZE.z <= z0 || any(px.xy < 0)  || any(px >= int3(CLOUD_DIM))) 
            return vPad;
        px = clamp(px, 0, CLOUD_DIM - 1);
        return tex[px];
    }
};

// free surface: 0 / dirichlet
// wall: neumann
class PressureFetcher : IFetcher3D {
    float z0; // height of terrain
    float4 Fetch(Texture3D tex, int3 px){
        // TODO: issue with free bound
        // if (any(px.xy < 0) || any(px.xy >= CLOUD_DIM.xy)) 
        //     return 0;
        px = clamp(px, 0, CLOUD_DIM - 1);
        return tex[px];
    }
};

////////////////////////////////////////////////////////////////////////////////////////

// second-order runge kutta backtrace
float3 BacktraceRungeKutta2(Texture3D texU, IFetcher3D fetcher, float3 uvw, float dt){
    float3 mid = uvw - 0.5 * dt * LinearSample3D(fetcher, texU, uvw).xyz / CLOUD_RANGE_M;
    float3 coord = uvw - dt * LinearSample3D(fetcher, texU, mid).xyz / CLOUD_RANGE_M;
    return coord;
};

////////////////////////////////////////////////////////////////////////////////////////

// INTIALIZERS

// initialize buffers that can't be filled
// 3d out 1: u
// 3d out 2: theta
[numthreads(8, 8, 1)] void initStates(int3 tid : SV_DispatchThreadID){
    float z = (tid.z + 0.5) * CELL_SIZE.z;
    RWTex3d1[tid] = pad4(WindProfile(z) * float3(globalWind, 0));
    RWTex3d2[tid] = pad4(Abs2PotTmp(TmpProfile(z, 0, tmpOcean), PressureProfile(z, 0, tmpOcean)));
}

////////////////////////////////////////////////////////////////////////////////////////

// GROUND UPDATES
// TODO

// 2d in 1: ground composition
// 3d in 1: qc
// 3d in 2: qw
// 2d inout 1: ground temperature
// [numthreads(8, 8, 1)] void updTempGround(int3 tid : SV_DispatchThreadID) {
//     // local cloud covering fraction L_C, [WS] eq. 60
//     // TODO: parallel sum
//     float qSum = 0;
//     for (int i = 0; i < CLOUD_DIM.z; i++)
//         qSum += Tex3d1[int3(tid.xy, i)].x + Tex3d2[int3(tid.xy, i)].x;
//     float lC = min(qSum / qFull, 1);

//     // float2 groundComp = Tex2d1[tid.xy].xy;
//     float fracSand = groundComposition.x, fracSilt = groundComposition.y, fracClay = 1 - groundComposition.x - groundComposition.y;
//     float albedo = GMAT_BLEND(albedo);
//     float emissivity = GMAT_BLEND(eps);
//     float rou = GMAT_BLEND(rou);
//     float cp = GMAT_BLEND(cp);
    
//     // ground temperature update, [WS] eq. 63
//     float temp = RWTex2d1[tid.xy].x;
//     RWTex2d1[tid.xy] = temp + dt * (1 - lC) * ((1 - albedo) * irradiance - emissivity * boltzmann * pow(temp, 4)) / (soilThickness * rou * cp);
// }

// https://scienceweb.whoi.edu/oaflux/papers/YU-evp-JC2007.pdf
// ground evaporation
// 2d in 1: z
// 2d in 2: ground evaporation rate
// 3d in 1: theta
// 3d inout 1: qv
[numthreads(8, 8, 1)] void groundEva(int3 tid : SV_DispatchThreadID){
    float zGround = Tex2d1[tid.xy].x;
    int groundPx = int(floor(zGround * RCP_CELL_SIZE.z));
    int3 vxId = int3(tid.xy, groundPx);

    float p = PressureProfile(zGround, 0, tmpOcean);
    float tmp = Pot2AbsTmp(Tex3d1[vxId].x, p);
    float qSat = SatMixingRatioWater(tmp, p);

    // TODO use actual evaporation map
    // float eva = Tex2d2[tid.xy].x;
    float eva = 0.001 * (Random::perlinNoise((vxId + 0.5) * 0.1) * 0.5 + 1);
    RWTex3d1[vxId] = pad4(min(qSat, RWTex3d1[vxId].x + eva * dt * qSat));
}

////////////////////////////////////////////////////////////////////////////////////////

// VOLUME UPDATES


// advect velocity
// 2d in 1: height map
// 3d in 1: u
// 3d out 1: new u, advected
[numthreads(8, 8, 1)] void advectVel(int3 tid : SV_DispatchThreadID){
    VelocityFetcher fetcher;
    fetcher.z0 = Tex2d1[tid.xy].x;

    float3 uv = (tid + 0.5) / CLOUD_DIM;
    float3 uvAdv = BacktraceRungeKutta2(Tex3d1, fetcher, uv, dt);
    RWTex3d1[tid] = LinearSample3D(fetcher, Tex3d1, uvAdv);
}

// diffuse velocity (one iteration)
// 2d in 1: height map
// 3d in 1: u at last time frame
// 3d in 2: new u, last iteration
// 3d out 1: new u, updated
[numthreads(8, 8, 1)] void diffVelIter(int3 tid : SV_DispatchThreadID){
    VelocityFetcher fetcher;
    fetcher.z0 = Tex2d1[tid.xy].x;

    float a = dt * diffAir * RCP_CELL_SIZE.x * RCP_CELL_SIZE.x;

    float3 vx0 = fetcher.Fetch(Tex3d2, tid + int3(-1, 0, 0)).xyz;
    float3 vx1 = fetcher.Fetch(Tex3d2, tid + int3(1, 0, 0)).xyz;
    float3 vy0 = fetcher.Fetch(Tex3d2, tid + int3(0, -1, 0)).xyz;
    float3 vy1 = fetcher.Fetch(Tex3d2, tid + int3(0, 1, 0)).xyz;
    float3 vz0 = fetcher.Fetch(Tex3d2, tid + int3(0, 0, -1)).xyz;
    float3 vz1 = fetcher.Fetch(Tex3d2, tid + int3(0, 0, 1)).xyz;

    RWTex3d1[tid] = pad4((Tex3d1[tid].xyz + a * (vx0 + vx1 + vy0 + vy1 + vz0 + vz1)) / (1 + 6 * a));
}

// calculate vorticity/curl
// 2d in 1: height map
// 3d in 1: u
// 3d out 1: vorticity
[numthreads(8, 8, 1)] void vorticity(int3 tid : SV_DispatchThreadID){
    VelocityFetcher fetcher;
    fetcher.z0 = Tex2d1[tid.xy].x;

    float3 vx0 = fetcher.Fetch(Tex3d1, tid + int3(-1, 0, 0)).xyz;
    float3 vx1 = fetcher.Fetch(Tex3d1, tid + int3(1, 0, 0)).xyz;
    float3 vy0 = fetcher.Fetch(Tex3d1, tid + int3(0, -1, 0)).xyz;
    float3 vy1 = fetcher.Fetch(Tex3d1, tid + int3(0, 1, 0)).xyz;
    float3 vz0 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, -1)).xyz;
    float3 vz1 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, 1)).xyz;

    float dVxDy = (vy1.x - vy0.x) * RCP_CELL_SIZE.y;
    float dVxDz = (vz1.x - vz0.x) * RCP_CELL_SIZE.z;
    float dVyDx = (vx1.y - vx0.y) * RCP_CELL_SIZE.x;
    float dVyDz = (vz1.y - vz0.y) * RCP_CELL_SIZE.z;
    float dVzDx = (vx1.z - vx0.z) * RCP_CELL_SIZE.x;
    float dVzDy = (vy1.z - vy0.z) * RCP_CELL_SIZE.y;

    float3 vorticity = float3(dVzDy - dVyDz, dVxDz - dVzDx, dVyDx - dVxDy) * 0.5;

    RWTex3d1[tid] = pad4(vorticity);
}

// vorticity confinement
// 2d in 1: height map
// 3d in 1: vorticity
// 3d inout 1: u
[numthreads(8, 8, 1)] void vortConfine(int3 tid : SV_DispatchThreadID){
    PadFetcher fetcher;
    fetcher.z0 = Tex2d1[tid.xy].x;
    fetcher.vPad = 0;

    float3 vx0 = fetcher.Fetch(Tex3d1, tid + int3(-1, 0, 0)).xyz;
    float3 vx1 = fetcher.Fetch(Tex3d1, tid + int3(1, 0, 0)).xyz;
    float3 vy0 = fetcher.Fetch(Tex3d1, tid + int3(0, -1, 0)).xyz;
    float3 vy1 = fetcher.Fetch(Tex3d1, tid + int3(0, 1, 0)).xyz;
    float3 vz0 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, -1)).xyz;
    float3 vz1 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, 1)).xyz;

    float3 vorticity = Tex3d1[tid].xyz;
    float3 n = float3(length(vx1) - length(vx0), length(vy1) - length(vy0), length(vz1) - length(vz0)); // assert uniform cell size, 0.5 RCP_CELL_SIZE omitted by normalize
    n /= max(1e-8, length(n));
    RWTex3d1[tid] = pad4(RWTex3d1[tid].xyz + epsConf * cross(n, vorticity) * dt);
}

// buoyancy and external force
// 3d in 1: qv
// 3d in 2: theta
// 3d inout 1: u
[numthreads(8, 8, 1)] void buoyExtForce(int3 tid : SV_DispatchThreadID){
    float z = (tid.z + 0.5) * CELL_SIZE.z;

    // buoyancy
    float qVapor = Tex3d1[tid].x;
    float theta = Tex3d2[tid].x;
    float thetaRef = Abs2PotTmp(TmpProfile(z, 0, tmpOcean), PressureProfile(z, 0, tmpOcean));
    float3 fBuoyancy = float3(0, 0, g * (theta / thetaRef - 1 + 0.61 * qVapor)) * 0.05;

    // TODO 
    // wind
    float3 fWind = 0;

    RWTex3d1[tid] = pad4(RWTex3d1[tid].xyz + (fBuoyancy + fWind) * dt);
}

// div of velocity and init of project
// 2d in 1: height map
// 3d in 1: u
// 3d out 1: div
// 3d out 2: p
[numthreads(8, 8, 1)] void divVel(int3 tid : SV_DispatchThreadID){
    VelocityFetcher fetcher;
    fetcher.z0 = Tex2d1[tid.xy].x;

    float vx0 = fetcher.Fetch(Tex3d1, tid + int3(-1, 0, 0)).x;
    float vx1 = fetcher.Fetch(Tex3d1, tid + int3(1, 0, 0)).x;
    float vy0 = fetcher.Fetch(Tex3d1, tid + int3(0, -1, 0)).y;
    float vy1 = fetcher.Fetch(Tex3d1, tid + int3(0, 1, 0)).y;
    float vz0 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, -1)).z;
    float vz1 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, 1)).z;

    RWTex3d1[tid] = pad4((vx1 - vx0 + vy1 - vy0 + vz1 - vz0) * 0.5 * RCP_CELL_SIZE.x);
    RWTex3d2[tid] = 0;
}

// pressure projection iterative update
// 2d in 1: height map
// 3d in 1: div
// 3d in 2: p, last iteration
// 3d out 1: p
[numthreads(8, 8, 1)] void projectIter(int3 tid : SV_DispatchThreadID){
    PressureFetcher fetcher;
    fetcher.z0 = Tex2d1[tid.xy].x;

    float a = -CELL_SIZE.x * CELL_SIZE.x;

    float px0 = fetcher.Fetch(Tex3d2, tid + int3(-1, 0, 0)).x;
    float px1 = fetcher.Fetch(Tex3d2, tid + int3(1, 0, 0)).x;
    float py0 = fetcher.Fetch(Tex3d2, tid + int3(0, -1, 0)).x;
    float py1 = fetcher.Fetch(Tex3d2, tid + int3(0, 1, 0)).x;
    float pz0 = fetcher.Fetch(Tex3d2, tid + int3(0, 0, -1)).x;
    float pz1 = fetcher.Fetch(Tex3d2, tid + int3(0, 0, 1)).x;

    RWTex3d1[tid] = pad4((px0 + px1 + py0 + py1 + pz0 + pz1 + a * Tex3d1[tid].x) / 6);
}

// pressure projection
// 2d in 1: height map
// 3d in 1: p
// 3d inout 1: u
[numthreads(8, 8, 1)] void project(int3 tid : SV_DispatchThreadID){
    PressureFetcher fetcher;
    fetcher.z0 = Tex2d1[tid.xy].x;

    float px0 = fetcher.Fetch(Tex3d1, tid + int3(-1, 0, 0)).x;
    float px1 = fetcher.Fetch(Tex3d1, tid + int3(1, 0, 0)).x;
    float py0 = fetcher.Fetch(Tex3d1, tid + int3(0, -1, 0)).x;
    float py1 = fetcher.Fetch(Tex3d1, tid + int3(0, 1, 0)).x;
    float pz0 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, -1)).x;
    float pz1 = fetcher.Fetch(Tex3d1, tid + int3(0, 0, 1)).x;

    RWTex3d1[tid] = pad4(RWTex3d1[tid].xyz - float3(px1 - px0, py1 - py0, pz1 - pz0) * 0.5 * RCP_CELL_SIZE.x);
}

// advection of properties
// 2d in 1: height map
// 3d in 1: u
// 3d in 2: qv
// 3d in 3: qw
// 3d in 4: qc
// 3d in 5: qr
// 3d in 6: qs
// 3d in 7: qi
// 3d in 8: theta
// 3d out 1-7: new props above
// TODO: less memory requirement? perchance
[numthreads(8, 8, 1)] void advectProps(int3 tid : SV_DispatchThreadID){
    VelocityFetcher uFetcher;
    uFetcher.z0 = Tex2d1[tid.xy].x;

    VaporFetcher qvFetcher;
    qvFetcher.z0 = Tex2d1[tid.xy].x;
    
    ContentFetcher qjFetcher;
    qjFetcher.z0 = Tex2d1[tid.xy].x;
    
    ThetaFetcher thetaFetcher;
    thetaFetcher.z0 = Tex2d1[tid.xy].x;

    float3 uv = (tid + 0.5) / CLOUD_DIM;
    float3 uvAdv = BacktraceRungeKutta2(Tex3d1, uFetcher, uv, dt);
    float3 dt0 = dt / CLOUD_RANGE_M;
    float3 velocity = Tex3d1[tid].xyz;

    {
        RWTex3d1[tid] = LinearSample3D(qvFetcher, Tex3d2, uvAdv); // qv
        RWTex3d2[tid] = LinearSample3D(qjFetcher, Tex3d3, uvAdv); // qw
        RWTex3d3[tid] = LinearSample3D(qjFetcher, Tex3d4, uvAdv); // qc
        RWTex3d7[tid] = LinearSample3D(thetaFetcher, Tex3d8, uvAdv); // theta
    }
    {
        float3 dispos = (velocity * float3(1, 1, 0.05) + SpeedTermRain(Tex3d2[tid].x)) * dt0;
        RWTex3d4[tid] = LinearSample3D(qjFetcher, Tex3d5, uv - dispos); // qr
    }
    {
        float3 dispos = (velocity * float3(1, 1, 0.05) + SpeedTermSnow(Tex3d2[tid].x)) * dt0;
        RWTex3d5[tid] = LinearSample3D(qjFetcher, Tex3d6, uv - dispos); // qs
    }
    {
        float3 dispos = (velocity * float3(1, 1, 0.05) + SpeedTermIce(Tex3d2[tid].x)) * dt0;
        RWTex3d6[tid] = LinearSample3D(qjFetcher, Tex3d7, uv - dispos); // qi
    }
}

// water microphysics
// 2d in 1: height map
// 3d in 1: qv
// 3d in 2: qw
// 3d in 3: qc
// 3d in 4: qr
// 3d in 5: qs
// 3d in 6: qi
// 3d in 7: theta
// 3d out 1-7: new props above
[numthreads(8, 8, 1)] void waterMicrophysics(int3 tid : SV_DispatchThreadID){
    float z = (tid.z + 0.5) * CELL_SIZE.z;
    float qV = Tex3d1[tid].x,
          qW = Tex3d2[tid].x,
          qC = Tex3d3[tid].x,
          qR = Tex3d4[tid].x,
          qS = Tex3d5[tid].x,
          qI = Tex3d6[tid].x,
          theta = Tex3d7[tid].x;

    float tmp = Pot2AbsTmp(theta, PressureProfile(z, 0, tmpOcean));
    float tmpCelsius = tmp - kelvin0C;
    float p = PressureProfile(z, 0, tmpOcean);
    float qWaterSat = SatMixingRatioWater(tmp, p);
    float qIceSat = SatMixingRatioIce(tmp, p);

    float xVapor = qV / (qV + 1); // [SS] eq. 9
    float molThermal = lerp(molAir, molVapor, xVapor); // [SS] eq. 7
    float gammaThermal = lerp(gammaAir, gammaVapor, xVapor); // [SS] eq. 11
    float cpThermal = gammaThermal * rUniGas / (molThermal * (gammaThermal - 1));

    float ewMcw = tmpCelsius >= -40 ? min(qWaterSat - qV, qW) : 0; // evaporation - condensation of water, [WS] eq. 17
    // ewMcw *= dt; // instantaneous
    qV += ewMcw;
    qW -= ewMcw;

    // mixed-phase cloud, [WS] sec. 4.2.2
    const static float kA = 2.4e-2;
    const static float rV = 461;
    const static float lS = 2.834e6;
    const static float cap = 0.5; // capacitance for hexagonal crystals

    float eqVpW = EqVpWater(tmp);
    float eqVpI = EqVpIce(tmp);
    float nI = 1e3 * exp(12.96 * (eqVpW - eqVpI) / (eqVpI - 0.639)); // ice crystal number concentration, [WS] eq. 21
    float satHeatCond = lS / (kA * tmp) * (lS / (rV * tmp) - 1); // heat conduction saturation ratio, [WS] eq. 19
    float satVapDiff = rV * tmp * p / 2.21 * eqVpI; // vapor diffusion saturation ratio, [WS] eq. 20
    float cVd = 65.2 * sqrt(nI) * (eqVpW - eqVpI) / (sqrt(rouAir) * (satHeatCond + satVapDiff) * eqVpI); // rate constant for vapor deposition on hexagonal crystals, [WS] eq. 18
    float qTilde = max(qI, 1e-12 * nI / rouAir); // [WS] eq. 23
    float bW = (tmpCelsius >= -40 && tmpCelsius <= 0) ? min(qW, pow((1 - cap) * cVd * dt + pow(qTilde, 1 - cap), rcp(1 - cap))) : 0; // warm cloud to ice cloud, [WS] eq. 22
    // bW *= dt; // dt taking into account above
    qC += bW;
    qW -= bW;
    
    // cold cloud, [WS] sec. 4.2.3
    float fW = tmpCelsius < -40 ? qW : 0; // mixed phase cloud to ice cloud, [WS] eq. 25
    // fW *= dt; // instantaneous
    qC += fW;
    qW -= fW;

    float mC = tmpCelsius > 0 ? qC : 0; // ice cloud to warm cloud, [WS] eq. 26
    // mC *= dt; // instantaneous
    // melting constraint, [WS] eq. 37
    // TODO find out how to apply constraint
    float meltConstraint = cpAir / latentFusion * (tmpCelsius - 0);
    // mC = min(mC, meltConstraint);
    // meltConstraint -= mC;
    qW += mC;
    qC -= mC;

    float dcMsc = tmpCelsius <= 0 ? min(qIceSat - qV, qC) : 0; // deposition - sublimation of ice, [WS] eq. 17
    // dcMsc *= dt; // instantaneous
    qC += dcMsc;
    qV -= dcMsc;

    // precip, [WS] sec. 4.2.4
    // TODO find constants for rW, rS, fR, mI
    float aW = 0.001 * max(qW - 0.001, 0); // autoconversion of rain, [WS] eq. 29, values from https://erf.readthedocs.io/en/latest/theory/Microphysics.html
    aW *= dt;
    aW = min(aW, qW);
    qR += aW;
    qW -= aW;

    float kW = 2.2 * qW * pow(qR, 0.875); // accretion of rain, [WS] eq. 30, modified equation and values from https://erf.readthedocs.io/en/latest/theory/Microphysics.html
    kW *= dt;
    kW = min(kW, qW);
    qR += kW;
    qW -= kW;

    float aC = 1e-3 * exp(0.025 * tmpCelsius) * max(qC - 0.001, 0); // autoconversion of snow, [WS] eq. 31
    aC *= dt;
    aC = min(aC, aC);
    qS += aC;
    qC -= aC;

    float kC = 3.30724 / 0.08 * qC * pow(qS, 1.0705); // accretion of snow, [WS] eq. 33, modified equation and values from https://egusphere.copernicus.org/preprints/2025/egusphere-2024-2464/egusphere-2024-2464.pdf
    kC *= dt;
    kC = min(kC, qC);
    qS += kC;
    qC -= kC;

    float rW = 1.0 * qI * qW; // riming of water, [WS] eq. 34, values from my ass
    rW *= dt;
    rW = min(rW, qW);
    qI += rW;
    qW -= rW;

    float rS = 1.2 * qI * qS; // riming of snow, [WS] eq. 35 (wrong? qW for qI), values from my ass
    rS *= dt;
    rS = min(rS, qS);
    qI += rS;
    qS -= rS;
    
    float fR = tmpCelsius <= -8 ? 0 * pow(tmpCelsius + 8, 2) : 0; // freezing of raindrops, [WS] eq. 36, values from my ass (zeroed out because I have no good reference)
    fR *= dt;
    fR = min(fR, qR);
    qI += fR;
    qR -= fR;

    // melting, [WS] sec. 4.2.5
    float mS = tmpCelsius > 0 ? qS : 0; // melting of snow, [WS] eq. 38
    mS *= dt;
    mS = min(mS, qS);
    // mS = min(mS, meltConstraint);
    // meltConstraint -= mS;
    qR += mS;
    qS -= mS;

    float mI = 0 * max(tmpCelsius, 0); // melting of ice, [WS] eq. 39, values from my ass (zeroed out because I have no good reference)
    mI *= dt;
    mI = min(mI, qI);
    // mI = min(mI, meltConstraint);
    // meltConstraint -= mI;
    qR += mI;
    qI -= mI;

    // evaporation, [WS] sec. 4.2.6
    // eR can be found at https://erf.readthedocs.io/en/latest/theory/Microphysics.html
    // TODO, because I don't have enough values from my ass

    // temperature update, [WS] eq. 50
    tmp += (dcMsc * latentSub - ewMcw * latentVapor + (bW + fW - mC) * latentFusion) / cpThermal;
    theta = Abs2PotTmp(tmp, p);

    RWTex3d1[tid] = pad4(qV);
    RWTex3d2[tid] = pad4(qW);
    RWTex3d3[tid] = pad4(qC);
    RWTex3d4[tid] = pad4(qR);
    RWTex3d5[tid] = pad4(qS);
    RWTex3d6[tid] = pad4(qI);
    RWTex3d7[tid] = pad4(theta);
}

[numthreads(8, 8, 1)] void debugViz(int3 tid : SV_DispatchThreadID){
    int dSlice = int(debugValues.x);

    int3 vxP = int3(tid.xy, dSlice);
    float z = (0.5 + vxP.z) * CELL_SIZE.z;

    float qVapor = Tex3d2[tid].x;
    float theta = Tex3d5[tid].x;
    float3 fBuoyancy = float3(0, 0, g * (theta / Abs2PotTmp(tmpOcean, pOcean) - 1 + 0.61 * qVapor));

    // RWTex2d1[tid.xy] = Remap(RWTex3d1[vxP].z, 0, 0.01, 0, 1);
    // RWTex2d1[tid.xy] = Remap(RWTex3d2[vxP].x, 0, 0.04, 0, 1);
    RWTex2d1[tid.xy] = Remap(qVapor, 0, 0.1, 0, 1);
}