
#include "flags.glsl"
#include "../geometry.glsl"


const float PI = 3.14159265358979323846;


#define UBO_GLOBAL               0
#define SSBO_VOXEL_NODES         0
#define SSBO_VOXEL_UPLOAD        1
#define SSBO_VOXEL_UPLOAD_BRICKS 2
#define SSBO_VOXEL_GEN_DATA      3
#define NodeArity 8
#define BrickRes 8
const uint IndexMask     = 0x00ffffffu;
const uint InvalidIndex  = 0xffffffffu & IndexMask;
const uint EmptyBrickBit = 0x80000000u;


layout(std140, binding = UBO_GLOBAL) 
uniform GlobalUniforms
{
    mat4  viewProjMat, invViewMat, invProjMat, invViewProjMat, worldMat, prevViewProjMat;
    vec4  planetLocation;
    vec4  lightDir;
    vec4  sun; // distance, radius, intensity (already attenuated)
    vec4 betaR;
    
    uint  rootGroupIndex;
    float time, animationTime, animationAlpha;
    float Fcoef_half;
    float stepSize;
    float atmosphereRadius, planetRadius, atmosphereScale, atmosphereHeight;
    float Rt, Rg; // atmosphere top and bottom (ground) radii
    
    // Physical values:
    float HR, HM;
    float betaMEx, betaMSca, mieG;

    // Sample scaling:
    float offsetR, scaleR, offsetM, scaleM;
};

const int TransmittanceWidth = 256, TransmittanceHeight = 64;

const float ScatterRSize = 32, ScatterMuSize = 128, ScatterMuSSize = 32, ScatterNuSize = 8;
const float MuSMin = -0.2; // cos(102 degrees), which was chosen for Earth in particular

const float mieMul = 0.25 * 100.0 * 4.0 * 4.0; // - to do: tune, put elsewhere


uniform layout(binding=0) sampler3D  brickTexture;
uniform layout(binding=1) sampler3D  nextBrickTexture;
uniform layout(binding=2) usampler3D octreeMapTexture;
uniform layout(binding=3) sampler2D  depthTexture;
uniform layout(binding=5) sampler2D  transmittanceTexture;
uniform layout(binding=6) sampler3D  scatterTexture;
uniform layout(binding=7) usampler3D frustumOctreeMap;

uniform vec3 mapPosition, mapScale;


struct Node
{
    uint children, neighbours[3];
};
struct NodeGroup
{
    uint info, parent;
    Node nodes[NodeArity];
};

uint DepthFromInfo(uint info) { return info & ((1u << 5u) - 1u); }

struct UploadNodeGroup
{
    uint groupIndex, genData;
    NodeGroup nodeGroup;
};
struct UploadBrick
{
    uint nodeIndex, brickIndex;
    uint genDataOffset, genDataSize;
    vec4 nodeLocation; // size in w
};

layout(std430, binding = SSBO_VOXEL_NODES) buffer nodeBuffer
{
    NodeGroup nodeGroups[];
};
layout(std430, binding = SSBO_VOXEL_UPLOAD) buffer uploadBuffer
{
    UploadNodeGroup uploadNodes[];
};
layout(std430, binding = SSBO_VOXEL_UPLOAD_BRICKS) buffer uploadBricksBuffer
{
    UploadBrick uploadBricks[];
};
// - or maybe let each generator shader define this in their custom formats (could be more convenient)
layout(std430, binding = SSBO_VOXEL_GEN_DATA) buffer genDataBuffer
{
    uint genData[];
};

layout(std430, binding = SSBO_VOXEL_GEN_DATA) buffer splitNodesBuffer
{
    uint splitNodes[];
};

uniform uvec3 uBricksRes;
uniform vec3 bricksRes;
uvec3 IndexTo3D(uint index, uvec3 bres)
{
    uvec3 p;
    p.z = index / (bres.x * bres.y);
    p.y = (index / bres.x) % bres.y;
    p.x = index % bres.x;
    return p;
}
uvec3 BrickIndexTo3D(uint brickIndex)
{
    return IndexTo3D(brickIndex, uBricksRes);
}
vec3 BrickSampleCoordinates(vec3 brick3D, vec3 localCoords)
{
    return (brick3D + (vec3(0.5) + localCoords * vec3(float(BrickRes - 1u))) / float(BrickRes)) / vec3(bricksRes);
}

vec4 RetrieveVoxelData(vec3 brickOffs, vec3 lc, sampler3D brickTexture)
{
    vec3 tc = lc * 0.5 + 0.5;
    tc = clamp(tc, vec3(0.0), vec3(1.0)); // - should this really be needed? Currently there can be artefacts without this
    tc = BrickSampleCoordinates(brickOffs, tc);
    vec4 voxelData = texture(brickTexture, tc);
    return voxelData;
}

vec4 RetrieveAnimatedVoxelData(vec3 brickOffs, vec3 lc)
{
    vec3 tc = lc * 0.5 + 0.5;
    tc = clamp(tc, vec3(0.0), vec3(1.0)); // - should this really be needed? Currently there can be artefacts without this
    tc = BrickSampleCoordinates(brickOffs, tc);
                
    vec4 voxelData = texture(nextBrickTexture, tc);
                
    if (interpolateAnimation) voxelData = mix(texture(brickTexture, tc), voxelData, animationAlpha);
    
    return voxelData;
}


/*uint GetNodeNeighbour(Index node, uint neighbour)
{
    uint d = neighbour >> 1u, s = neighbour & 1u, o = GetNodeOctant(node);
    if ((((node >> d) & 1u) ^ s) == 1u) return node ^ (1u << d);
    return nodes[node].neighbours[d];
}*/

float min_component(vec3 v) { return min(v.x, min(v.y, v.z)); }
float max_component(vec3 v) { return max(v.x, max(v.y, v.z)); }

void AabbIntersection(out float tmin_out, out float tmax_out, vec3 bmin, vec3 bmax, vec3 o, vec3 d)
{
    vec3 dinv = 1.0 / d; // - problem when d components are zero?
    dinv = sign(d) / abs(d);
    vec3 t0 = (bmin - o) * dinv;
    vec3 t1 = (bmax - o) * dinv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    tmin_out = max_component(tmin);
    tmax_out = min_component(tmax);
}
bool IsIntersection(float tmin, float tmax)
{
    return tmax >= max(tmin, float(0.0));
}



struct OctreeTraversalData
{
    vec3 p;
    uint depth;
    uint ni, gi;
    vec3 center;
    float size;
    uint flags;
};

void OctreeTraversalIteration(inout OctreeTraversalData o)
{
    ivec3 ioffs = clamp(ivec3(o.p - o.center + 1.0), ivec3(0), ivec3(1));
    uint child = uint(ioffs.x) + uint(ioffs.y) * 2u + uint(ioffs.z) * 4u;
    o.ni = o.gi * NodeArity + child;
    o.size *= 0.5;
    o.center += (vec3(ioffs) * 2.0 - 1.0) * o.size;
    o.gi = nodeGroups[o.gi].nodes[child].children;
    o.flags = o.gi & ~IndexMask;
    o.gi &= IndexMask;
    ++o.depth;
}


// - to do: add "desired size" parameter to allow early stop
// p components in [-1, 1] range
void OctreeDescendInit(inout OctreeTraversalData o)
{
    o.depth = 0u - 1u;
    o.ni = InvalidIndex;
    o.gi = rootGroupIndex;
    o.center = vec3(0.0);
    o.size = 1.0;
}
void OctreeDescendLoop(inout OctreeTraversalData o)
{
    while (InvalidIndex != o.gi)
    {
        OctreeTraversalIteration(o);
    }
}
void OctreeDescendLoopMaxDepth(inout OctreeTraversalData o, uint maxDepth)
{
    while (InvalidIndex != o.gi)
    {
        OctreeTraversalIteration(o);
        if (o.depth >= maxDepth) break;
    }
}
void OctreeDescend(inout OctreeTraversalData o)
{
    OctreeDescendInit(o);
    OctreeDescendLoop(o);
}
void OctreeDescendMaxDepth(inout OctreeTraversalData o, uint maxDepth)
{
    OctreeDescendInit(o);
    OctreeDescendLoopMaxDepth(o, maxDepth);
}


const uint DepthBits = 5u, ChildBits = 8u;
const uint IndexBits = 32u - DepthBits - ChildBits;

// sampleLoc somewhere on range [0, 1]
void OctreeDescendMapInit(in usampler3D mapTexture, in vec3 sampleLoc, inout OctreeTraversalData o)
{
    uint info = texture(mapTexture, sampleLoc).x;
    o.depth = (info >> (IndexBits + ChildBits)) & ((1u << DepthBits) - 1u);
    o.gi = info & ((1u << IndexBits) - 1u);
    float nodesAtDepth = float(1u << (o.depth + 1u));
    vec3 pn = vec3(ivec3((o.p * 0.5 + 0.5) * nodesAtDepth)) / nodesAtDepth;
    o.size = 1.0 / nodesAtDepth;
    o.center = pn * 2.0 - 1.0 + vec3(o.size);
    o.ni = InvalidIndex;
}
void OctreeDescendMap(in usampler3D mapTexture, in vec3 sampleLoc, inout OctreeTraversalData o)
{
    OctreeDescendMapInit(mapTexture, sampleLoc, o);
    OctreeDescendLoop(o);
}
void OctreeDescendMap(inout OctreeTraversalData o)
{
    vec3 sampleLoc = o.p * 0.5 + 0.5;
    OctreeDescendMap(octreeMapTexture, sampleLoc, o);
}
void OctreeDescendMapMaxDepth(inout OctreeTraversalData o, uint maxDepth)
{
    //OctreeDescendMap(o); return;
    // - to do: find out why this doesn't quite work
    
    vec3 sampleLoc = o.p * 0.5 + 0.5;
    OctreeDescendMapInit(octreeMapTexture, sampleLoc, o);
    OctreeDescendLoopMaxDepth(o, maxDepth);
}


float RayleighDensityFromSample(float v)
{
    return exp(offsetR + scaleR * v);
}
float MieDensityFromSample(float v)
{
    return exp(offsetM + scaleM * v);
}

float RayleighDensityFromH(float H)
{
    return exp(-H / HR);
}
float MieDensityFromH(float H)
{
    return exp(-H / HM);
}

float PhaseRayleigh(float v)
{
    return (3.0 / (16.0 * PI)) * (1.0 + v * v);
}

float PhaseMie(float v)
{
    return 1.5 * 1.0 / (4.0 * PI) * (1.0 - mieG*mieG) * pow(1.0 + (mieG*mieG) - 2.0*mieG*v, -3.0/2.0) * (1.0 + v * v) / (2.0 + mieG*mieG);
    /*return (3.0 / (8.0 * PI))
        * (1.0 - mieG * mieG)
        * (1.0 + v * v)
        * pow(1.0 + mieG * mieG - 2.0 * mieG * v, -1.5)
        / (2.0 + mieG * mieG);*/
}


struct AtmosphereIntersection
{
    float outerR, innerR; // - to do: probably make these uniforms instead
    float outerMin, outerMax, innerMin, innerMax;
    bool intersectsOuter, intersectsInner;
};
AtmosphereIntersection IntersectAtmosphere(vec3 ori, vec3 dir)
{
    AtmosphereIntersection ai;
    ai.outerR = Rt; // upper atmosphere radius (not *cloud* level, but far above it)
    ai.innerR = planetRadius + atmosphereHeight * 0.5; // - to do: make uniform
    ai.intersectsOuter = IntersectSphere(ori, dir, planetLocation.xyz, ai.outerR, ai.outerMin, ai.outerMax);
    ai.intersectsInner = IntersectSphere(ori, dir, planetLocation.xyz, ai.innerR, ai.innerMin, ai.innerMax);
    return ai;
}



// see https://github.com/ebruneton/precomputed_atmospheric_scattering/blob/master/atmosphere/functions.glsl
// (accessed 20200210)

// r: distance to planet center
// mu: dot product of ray direction and normalized position

float DistanceToAtmosphereTop(float r, float mu)
{
    return max(0.0, -r*mu + sqrt(max(0.0, Rt*Rt + r*r*(mu*mu - 1.0))));
}
float DistanceToAtmosphereBottom(float r, float mu)
{
    return max(0.0, -r*mu - sqrt(max(0.0, Rg*Rg + r*r*(mu*mu - 1.0))));
}
float DistanceToNearestAtmosphereBoundary(float r, float mu, bool intersectsGround)
{
    return intersectsGround ? DistanceToAtmosphereBottom(r, mu) : DistanceToAtmosphereTop(r, mu);
}

// x on [0, 1], texSize texture size in texels
float UnitCoordToTextureCoord(float x, float texSize)
{
    return 0.5 / texSize + x * (1.0 - 1.0 / texSize);
}
float TextureCoordToUnitCoord(float x, float texSize)
{
    return (x - 0.5 / texSize) / (1.0 - 1.0 / texSize);
}

vec2 RMuToTransmittanceUv(float r, float mu)
{
    /*const float Rt = 6426000.0;
    const float Rg = 6371000.0;*/
    
    float H = sqrt(Rt*Rt - Rg*Rg); // distance to atmosphere top for ground level horizontal ray
    float rho = sqrt(max(0.0, r*r - Rg*Rg)); // distance to horizon
    float d = DistanceToAtmosphereTop(r, mu);
    float d_min = Rt - r, d_max = rho + H;
    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;
    return vec2(UnitCoordToTextureCoord(x_mu, TransmittanceWidth), UnitCoordToTextureCoord(x_r, TransmittanceHeight));
}
void TransmittanceUvToRMu(vec2 uv, out float r, out float mu)
{
    float x_mu = TextureCoordToUnitCoord(uv.x, TransmittanceWidth);
    float x_r = TextureCoordToUnitCoord(uv.y, TransmittanceHeight);
    float H = sqrt(Rt*Rt - Rg*Rg);
    float rho = x_r * H;
    r = sqrt(rho*rho + Rg*Rg);
    float d_min = Rt - r, d_max = rho + H;
    float d = x_mu * (d_max - d_min) + d_min;
    mu = d == 0.0 ? 1.0 : (H*H - rho*rho - d*d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

vec3 GetTransmittanceToAtmosphereTop(float r, float mu)
{
    return vec3(texture(transmittanceTexture, RMuToTransmittanceUv(r, mu)));
}
float GetSunOcclusion(float r, float mu_s)
{
    const float sunAngularRadius = 0.00935 / 2.0;
    float sin_theta_h = Rg / r;
    float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));
    return smoothstep(-sin_theta_h * sunAngularRadius,
                 sin_theta_h * sunAngularRadius,
                 mu_s - cos_theta_h);
}
vec3 GetTransmittanceToSun(float r, float mu_s)
{
    return GetTransmittanceToAtmosphereTop(r, mu_s) * GetSunOcclusion(r, mu_s);
}
vec3 GetTransmittance(float r, float mu, float d, bool intersectsGround)
{
    float r_d = clamp(sqrt(d*d + r*r* + 2.0 * r * mu * d), Rg, Rt);
    float mu_d = clamp((r * mu + d) / r_d, -1.0, 1.0);
    
    if (intersectsGround)
    {
        return min(vec3(1.0),
            GetTransmittanceToAtmosphereTop(r_d, -mu_d) /
            GetTransmittanceToAtmosphereTop(r, -mu));
    }
    else
    {
        return min(vec3(1.0),
            GetTransmittanceToAtmosphereTop(r, mu) /
            GetTransmittanceToAtmosphereTop(r_d, mu_d));
    }
}

// nu: dot product of ray direction and light direction
// d: length along ray
void ComputeSingleScatteringIntegrand
(
    float r, float mu, float mu_s, float nu, float d,
    bool intersectsGround,
    out vec3 rayleigh, out vec3 mie
)
{
    float r_d = clamp(sqrt(d*d + r*r + 2.0 * r * mu * d), Rg, Rt);
    float mu_s_d = clamp((r * mu_s + d * nu) / r_d, -1.0, 1.0);
    vec3 T = 
        GetTransmittance(r, mu, d, intersectsGround) *
        GetTransmittanceToSun(r_d, mu_s_d);
    float H = r_d - Rg;
    rayleigh = T * RayleighDensityFromH(H);
    mie = T * MieDensityFromH(H);
}

void ComputeSingleScattering
(
    float r, float mu, float mu_s, float nu, bool intersectsGround,
    out vec3 rayleigh, out vec3 mie
)
{
    const uint NumSteps = 512u;
    float dx = DistanceToNearestAtmosphereBoundary(r, mu, intersectsGround) / float(NumSteps);
    rayleigh = mie = vec3(0.0);
    for (uint step = 0u; step < NumSteps; ++step)
    {
        float d = dx * float(step);
        vec3 dray, dmie;
        ComputeSingleScatteringIntegrand(r, mu, mu_s, nu, d, intersectsGround, dray, dmie);
        rayleigh += dray;
        mie += dmie;
    }
    rayleigh *= betaR.xyz * dx;
    mie *= betaMSca * dx;
}


vec4 ScatteringUvwzFromRMuMuSNu
(
    float r, float mu, float mu_s, float nu, bool intersectsGround
)
{
    float H = sqrt(Rt*Rt - Rg*Rg); // distance to atmosphere top for a horizontal ray at ground level
    float rho = sqrt(max(0.0, r*r - Rg*Rg)); // distance to the horizon
    float u_r = UnitCoordToTextureCoord(rho / H, ScatterRSize);
    
    float r_mu = r * mu;
    float discriminant = r*mu*r_mu - r*r + Rg*Rg; // of intersection between ray (r, mu) and the ground
    float u_mu;
    if (intersectsGround)
    {
        // Distance to the ground and its min and max over mu.
        float d = -r_mu - sqrt(max(0.0, discriminant));
        float d_min = r - Rg, d_max = rho;
        u_mu = 0.5 - 0.5 * UnitCoordToTextureCoord(d_max == d_min ? 0.0 : (d - d_min) / (d_max - d_min), ScatterMuSize / 2);
    }
    else
    {
        // Distance to the atmosphere top and its min and max over mu.
        float d = -r_mu + sqrt(max(0.0, discriminant + H*H));
        float d_min = Rt - r, d_max = rho + H;
        u_mu = 0.5 + 0.5 * UnitCoordToTextureCoord((d - d_min) / (d_max - d_min), ScatterMuSize / 2);
    }
    
    float d = DistanceToAtmosphereTop(Rg, mu_s);
    float d_min = Rt - Rg, d_max = H;
    float a = (d - d_min) / (d_max - d_min);
    float A = -2.0 * MuSMin * Rg / (d_max - d_min);
    float u_mu_s = UnitCoordToTextureCoord(max(1.0 - a / A, 0.0) / (1.0 + a), ScatterMuSSize);
    float u_nu = (nu + 1.0) / 2.0;
    return vec4(u_nu, u_mu_s, u_mu, u_r);
}

void RMuMuSNuFromScatteringUvwz
(
    vec4 uvwz, out float r, out float mu, out float mu_s, out float nu, out bool intersectsGround
)
{
    float H = sqrt(Rt*Rt - Rg*Rg); // distance to atmosphere top for horizontal ray at ground
    float rho = H * TextureCoordToUnitCoord(uvwz.w, ScatterRSize); // distance to the horizon
    r = sqrt(rho*rho + Rg*Rg);
    
    if (uvwz.z < 0.5)
    {
        // Distance to the ground and its min and max over mu.
        float d_min = r - Rg, d_max = rho;
        float d = d_min + (d_max - d_min) * TextureCoordToUnitCoord(1.0 - 2.0 * uvwz.z, ScatterMuSize / 2);
        mu = d == 0.0 ? -1.0 : clamp(-(rho*rho + d*d) / (2.0 * r * d), -1.0, 1.0);
        intersectsGround = true;
    }
    else
    {
        // Distance to the atmosphere top and its min and max over mu.
        float d_min = Rt - r, d_max = rho + H;
        float d = d_min + (d_max - d_min) * TextureCoordToUnitCoord(2.0 * uvwz.z - 1.0, ScatterMuSize / 2);
        mu = d == 0.0 ? 1.0 : clamp((H*H - rho*rho - d*d) / (2.0 * r * d), -1.0, 1.0);
        intersectsGround = false;
    }
    
    float x_mu_s = TextureCoordToUnitCoord(uvwz.y, ScatterMuSSize);
    float d_min = Rt - Rg, d_max = H;
    float A = -2.0 * MuSMin * Rg / (d_max - d_min);
    float a = (A - x_mu_s * A) / (1.0 + x_mu_s * A);
    float d = d_min + min(a, A) * (d_max - d_min);
    mu_s = d == 0.0 ? 1.0 : clamp((H*H - d*d) / (2.0 * Rg * d), -1.0, 1.0);
    nu = clamp(uvwz.x * 2.0 - 1.0, -1.0, 1.0);
}

vec4 LookUpScattering(float r, float mu, float mu_s, float nu, bool intersectsGround)
{
    vec4 uvwz = ScatteringUvwzFromRMuMuSNu(r, mu, mu_s, nu, intersectsGround);
    float texCoordX = uvwz.x * float(ScatterNuSize - 1);
    float texX = floor(texCoordX);
    float lerp = texCoordX - texX;
    vec3 uvw0 = vec3((texX + uvwz.y) / float(ScatterNuSize), uvwz.z, uvwz.w);
    vec3 uvw1 = vec3((texX + 1.0 + uvwz.y) / float(ScatterNuSize), uvwz.z, uvwz.w);
    return mix(texture(scatterTexture, uvw0), texture(scatterTexture, uvw1), lerp);
}

// Returns single scattering with phase functions included.
vec3 GetScattering(float r, float mu, float mu_s, float nu, bool intersectsGround)
{
    vec4 scatter = LookUpScattering(r, mu, mu_s, nu, intersectsGround);
    vec3 rayleigh = scatter.xyz * PhaseRayleigh(nu);
    vec3 mie = vec3(scatter.www) * PhaseMie(nu);
    return rayleigh + mie;
}
