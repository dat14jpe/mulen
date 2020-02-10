

const float PI = 3.14159265358979323846;

// - to do: UBO
uniform mat4 viewProjMat, invViewMat, invProjMat, invViewProjMat, worldMat;
uniform uint rootGroupIndex;
uniform float time;
uniform float Fcoef_half;
uniform float stepSize;
uniform float atmosphereRadius, planetRadius, atmosphereScale, atmosphereHeight;
uniform vec3 planetLocation;
uniform vec3 lightDir;
uniform vec3 sun; // distance, radius, intensity (already attenuated)
uniform float Rt, Rg; // atmosphere top and bottom (ground) radii

const int TransmittanceWidth = 256, TransmittanceHeight = 64;

// Physical values:
uniform vec3 betaR;
uniform float HR, HM, betaMEx, betaMSca, mieG;

// Sample scaling:
uniform float offsetR, scaleR, offsetM, scaleM;

uniform layout(binding=0) sampler3D brickTexture;
uniform layout(binding=1) sampler3D brickLightTexture;
uniform layout(binding=2) usampler3D octreeMapTexture;
uniform layout(binding=3) sampler2D depthTexture;
uniform layout(binding=5) sampler2D transmittanceTexture;


#define SSBO_VOXEL_NODES         0
#define SSBO_VOXEL_UPLOAD        1
#define SSBO_VOXEL_UPLOAD_BRICKS 2
#define NodeArity 8
const uint InvalidIndex = 0xffffffffu;
#define BrickRes 8

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
    uint genData;
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

uniform uvec3 uBricksRes;
uniform vec3 bricksRes;
uvec3 BrickIndexTo3D(uint brickIndex)
{
    const uvec3 bres = uBricksRes;
    uvec3 p;
    p.z = brickIndex / (bres.x * bres.y);
    p.y = (brickIndex / bres.x) % bres.y;
    p.x = brickIndex % bres.x;
    return p;
}
vec3 BrickSampleCoordinates(vec3 brick3D, vec3 localCoords)
{
    return (brick3D + (vec3(0.5) + localCoords * vec3(float(BrickRes - 1u))) / float(BrickRes)) / vec3(bricksRes);
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


// - to do: add "desired size" parameter to allow early stop
// p components in [-1, 1] range
uint OctreeDescend(vec3 p, out vec3 nodeCenter, out float nodeSize, out uint nodeDepth)
{
    uint depth = 0u - 1u;
    uint ni = InvalidIndex;
    uint gi = rootGroupIndex;
    vec3 center = vec3(0.0);
    float size = 1.0;
    while (InvalidIndex != gi)
    {
        ivec3 ioffs = clamp(ivec3(p - center + 1.0), ivec3(0), ivec3(1));
        uint child = uint(ioffs.x) + uint(ioffs.y) * 2u + uint(ioffs.z) * 4u;
        ni = gi * NodeArity + child;
        size *= 0.5;
        center += (vec3(ioffs) * 2.0 - 1.0) * size;
        gi = nodeGroups[gi].nodes[child].children;
        ++depth;
    }
    nodeCenter = center;
    nodeSize = size;
    nodeDepth = depth;
    return ni;
}
uint OctreeDescendMaxDepth(vec3 p, out vec3 nodeCenter, out float nodeSize, out uint nodeDepth, uint maxDepth)
{
    uint depth = 0u - 1u;
    uint ni = InvalidIndex;
    uint gi = rootGroupIndex;
    vec3 center = vec3(0.0);
    float size = 1.0;
    while (InvalidIndex != gi)
    {
        ivec3 ioffs = clamp(ivec3(p - center + 1.0), ivec3(0), ivec3(1));
        uint child = uint(ioffs.x) + uint(ioffs.y) * 2u + uint(ioffs.z) * 4u;
        ni = gi * NodeArity + child;
        size *= 0.5;
        center += (vec3(ioffs) * 2.0 - 1.0) * size;
        gi = nodeGroups[gi].nodes[child].children;
        ++depth;
        if (depth == maxDepth) break;
    }
    nodeCenter = center;
    nodeSize = size;
    nodeDepth = depth;
    return ni;
}


const uint DepthBits = 5u, ChildBits = 8u;
const uint IndexBits = 32u - DepthBits - ChildBits;

uint OctreeDescendMap(vec3 p, out vec3 nodeCenter, out float nodeSize, out uint nodeDepth)
{
    vec3 pp = p * 0.5 + 0.5;
    uint info = texture(octreeMapTexture, pp).x;
    uint gi = info & ((1u << IndexBits) - 1u);
    uint depth = (info >> (IndexBits + ChildBits)) & ((1u << DepthBits) - 1u);
    float nodesAtDepth = float(1u << (depth + 1u));
    vec3 pn = vec3(ivec3(pp * nodesAtDepth)) / nodesAtDepth;
    float size = 1.0 / nodesAtDepth;
    vec3 center = pn * 2.0 - 1.0 + vec3(size);
    uint ni = InvalidIndex;
    
    // - maybe have a loopless variant, possibly using a handful of nested low-res map textures? To do
    while (InvalidIndex != gi)
    {
        ivec3 ioffs = clamp(ivec3(p - center + 1.0), ivec3(0), ivec3(1));
        uint child = uint(ioffs.x) + uint(ioffs.y) * 2u + uint(ioffs.z) * 4u;
        ni = gi * NodeArity + child;
        size *= 0.5;
        center += (vec3(ioffs) * 2.0 - 1.0) * size;
        gi = nodeGroups[gi].nodes[child].children;
        ++depth;
    }
    
    nodeCenter = center;
    nodeSize = size;
    nodeDepth = depth;
    return ni;
}


float RayleighDensityFromSample(float v)
{
    return exp(offsetR + scaleR * v);
}
float MieDensityFromSample(float v)
{
    return exp(offsetM + scaleM * v);
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
vec3 GetTransmittanceToSun(float r, float mu_s)
{
    const float sun_angular_radius = 0.00935 / 2.0;
    float sin_theta_h = Rg / r;
    float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));
    return GetTransmittanceToAtmosphereTop(r, mu_s) *
      smoothstep(-sin_theta_h * sun_angular_radius,
                 sin_theta_h * sun_angular_radius,
                 mu_s - cos_theta_h);
}
// - to do: a variant accounting for planet shadow, analytically
