
#define SSBO_VOXEL_NODES 0
#define SSBO_VOXEL_UPLOAD 1
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
struct UploadNode
{
    uint nodeIndex;
    Node node;
};

layout(std430, binding = SSBO_VOXEL_NODES) buffer nodeBuffer
{
    NodeGroup nodeGroups[];
};
layout(std430, binding = SSBO_VOXEL_UPLOAD) buffer uploadBuffer
{
    UploadNode uploadNodes[];
};


/*uint GetNodeNeighbour(Index node, uint neighbour)
{
    uint d = neighbour >> 1u, s = neighbour & 1u, o = GetNodeOctant(node);
    if ((((node >> d) & 1u) ^ s) == 1u) return node ^ (1u << d);
    return nodes[node].neighbours[d];
}*/
