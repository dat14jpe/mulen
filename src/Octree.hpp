#pragma once

#include <limits>
#include <cinttypes>
#include <vector>
#include <array>

namespace Mulen {

    typedef uint32_t NodeIndex;
    static constexpr NodeIndex NodeArity = 8u; // - not liable to change; named to increase readability

    static constexpr uint32_t BrickRes = 7u + 1u; // (+1 since values are at corners)
    struct Voxel
    {
        float density; // - to do: revise, possibly
    };
    
    template<typename V, uint32_t baseRes> struct BrickBase
    {
        static constexpr uint32_t res = baseRes;
        static constexpr uint32_t res2 = res * res;
        static constexpr uint32_t res3 = res * res * res;

        V voxels[res3];
    };
    typedef BrickBase<Voxel, BrickRes> Brick;

    struct Node
    {
        NodeIndex children;                     // group index
        std::array<NodeIndex, 3> neighbours;    // node indices (only non-same-allocation neighbours are stored explicitly)
        // - add brick index if bricks should be allocated separately
    };
    struct NodeGroup
    {
        typedef uint32_t Info;
        Info info; // depth, flags, et cetera
        NodeIndex parent;
        Node nodes[NodeArity];

        static constexpr Info DepthBits = 5u;
        static constexpr Info MaxDepth = (1u << DepthBits) - 1u;
        Info GetDepth() const { return info & MaxDepth; }
        void SetDepth(Info d) { info |= d; }
    };
    static constexpr NodeIndex InvalidIndex = std::numeric_limits<NodeIndex>::max();

    template<typename Data, typename Index> struct Pool
    {
        std::vector<Data> data;
        Index firstFree, numFree;

        Index* GetNextFree(Index i)
        {
            return reinterpret_cast<Index*>(&data[i]);
        }
        
    public:
        bool Init(size_t num)
        {
            data.resize(num);
            numFree = static_cast<Index>(data.size());
            firstFree = 0u;
            for (Index i = 1u; i < numFree; ++i)
            {
                *GetNextFree(i - 1u) = i;
            }
            *GetNextFree(numFree - 1u) = InvalidIndex;
            return true;
        }

        Index Allocate()
        {
            if (!numFree) return InvalidIndex;
            const auto i = firstFree;
            firstFree = *GetNextFree(i);
            --numFree;
            return i;
        }
        void Free(Index i)
        {
            *GetNextFree(i) = firstFree;
            firstFree = i;
            ++numFree;
        }
        Index GetSize() const { return data.size(); }
        Index GetNumFree() const { return numFree; }
        Index GetNumUsed() const { return GetSize() - GetNumFree(); }
        Data& operator[](Index i) { return data[i]; }
        const Data& operator[](Index i) const { return data[i]; }
    };

    class Octree
    {
        void SetNeighbour(NodeIndex ni, unsigned direction, NodeIndex newNeighbour);

    public:
        Pool<NodeGroup, NodeIndex> nodes;
        //Pool<Brick, NodeIndex> bricks;

        bool Init(size_t numNodes, size_t numBricks);
        NodeIndex RequestRoot(); // returns InvalidIndex on failure, or the first of NodeArity root node indices on success
        void Split(NodeIndex);
        void Merge(NodeIndex);

        NodeGroup& GetGroup(NodeIndex i)
        {
            return nodes[i];
        }
        Node& GetNode(NodeIndex i)
        {
            return nodes[i / NodeArity].nodes[i % NodeArity];
        }

        static NodeIndex NodeToGroup(NodeIndex ni) { return ni / NodeArity; }
        static NodeIndex GroupAndChildToNode(NodeIndex gi, NodeIndex ci) { return gi * NodeArity + ci; }

        // - to do: template traversal function
    };
}
