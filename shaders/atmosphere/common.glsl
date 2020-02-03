

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

// Physical values:
uniform vec3 betaR;
uniform float HR;

// Sample scaling:
uniform float offsetR, scaleR;

uniform layout(binding=0) sampler3D brickTexture;
uniform layout(binding=1) sampler3D brickLightTexture;
uniform layout(binding=2) usampler3D octreeMapTexture;
uniform layout(binding=3) sampler2D depthTexture;


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

float PhaseRayleigh(float v)
{
    return (3.0 / (16.0 * PI)) * (1.0 + v * v);
}


// - to do: move to noise.glsl?

float rand(vec2 st) { return fract(sin(dot(st.xy, vec2(12.9898,78.233)))*43758.5453123); }
float rand(vec2 co, float l) {return rand(vec2(rand(co), l));}
float rand(vec2 co, float l, float t) {return rand(vec2(rand(co, l), t));}

// http://www.science-and-fiction.org/rendering/noise.html 20200131
float rand3D(in vec3 co){
    return fract(sin(dot(co.xyz ,vec3(12.9898,78.233,144.7272))) * 43758.5453);
}
