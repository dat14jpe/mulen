
#define SSBO_VOXEL_NODES         0
#define SSBO_VOXEL_UPLOAD        1
#define SSBO_VOXEL_UPLOAD_BRICKS 2
#define NodeArity 8
const uint InvalidIndex = 0xffffffffu;
#define BrickRes 9

struct Node
{
    uint children, neighbours[3];
};
struct NodeGroup
{
    uint info, parent;
    Node nodes[NodeArity];
};
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

/*void AabbIntersection(out float tmin, out float tmax, vec3 bmin, vec3 bmax, vec3 o, vec3 d)
{
    vec3 dinv = vec3(1) / d;
    
    float t1 = (bmin[0] - o[0]) * dinv[0];
    float t2 = (bmax[0] - o[0]) * dinv[0];

    tmin = min(t1, t2);
    tmax = max(t1, t2);

    for (int i = 1; i < 3; ++i)
    {
        t1 = (bmin[i] - o[i]) * dinv[i];
        t2 = (bmax[i] - o[i]) * dinv[i];
        tmin = max(tmin, min(t1, t2));
        tmax = min(tmax, max(t1, t2));
    }
}*/
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


// - to do: move to noise.glsl?

float random(vec2 st) { return fract(sin(dot(st.xy, vec2(12.9898,78.233)))*43758.5453123); }
