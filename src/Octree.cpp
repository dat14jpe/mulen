#include "Octree.hpp"
#include <functional>
#include <iostream>
#include <cassert>

namespace Mulen {
    bool Octree::Init(size_t numNodes, size_t numBricks)
    {
        return nodes.Init(numNodes) && bricks.Init(numBricks);
    }

    NodeIndex Octree::RequestRoot()
    {
        const auto index = nodes.Allocate();
        if (InvalidIndex == index) return InvalidIndex;
        auto& group = GetGroup(index);
        group.info = 0u; // - to do
        group.parent = InvalidIndex;
        for (auto ci = 0u; ci < NodeArity; ++ci)
        {
            auto& node = group.nodes[ci];
            node.children = InvalidIndex;
            for (auto& neighbour : node.neighbours)
            {
                neighbour = InvalidIndex;
            }
        }
        return index;
    }
    
    void Octree::SetNeighbour(NodeIndex ni, unsigned direction, NodeIndex neighbour)
    {
        if (InvalidIndex == ni) return;
        const auto depth = GetGroup(NodeToGroup(neighbour)).GetDepth();
        const auto dirBit = ni & (1u << direction);

        // - if we're going to stick to requiring nodes have same-depth neighbours before being split,
        // this doesn't actually require recursion. But better safe than sorry.
        unsigned callDepth = 0u;
        std::function<void(NodeIndex)> setNeighbour = [&](NodeIndex ni)
        {
            auto& node = GetNode(ni);
            const auto d0 = GetGroup(NodeToGroup(ni)).GetDepth();
            if (d0 >= depth)
            {
                node.neighbours[direction] = neighbour;
            }
            if (InvalidIndex != node.children)
            {
                for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
                {
                    if (0u == (ci & dirBit)) continue; // wrong half (not on the right face)
                    ++callDepth;
                    setNeighbour(GroupAndChildToNode(node.children, ci));
                    --callDepth;
                }
            }
        };
        setNeighbour(ni);
    }

    void Octree::Split(NodeIndex index)
    {
        const auto parentDepth = GetGroup(NodeToGroup(index)).GetDepth();
        auto& parent = GetNode(index);
        parent.children = nodes.Allocate();
        assert(parent.children != InvalidIndex);
        auto& group = GetGroup(parent.children);
        group.info = 0u;
        group.SetDepth(parentDepth + 1u);
        group.parent = index;
        for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
        {
            auto childIndex = GroupAndChildToNode(parent.children, ci);
            auto& node = group.nodes[ci];
            node.children = InvalidIndex;
            for (auto d = 0u; d < 3u; ++d) // iterate over the axes to set up neighbours
            {
                const auto bit = 1u << d;
                const auto test = (index ^ ci) & bit;
                auto& neighbourIndex = node.neighbours[d];
                neighbourIndex = test == 0u
                    ? neighbourIndex = parent.neighbours[d] // neighbour outside parent group
                    : neighbourIndex = index ^ bit; // neighbour within parent group

                if (InvalidIndex == neighbourIndex) continue;

                // Is the neighbour too low-depth to link back to the new nodes?
                if (GetGroup(NodeToGroup(neighbourIndex)).GetDepth() < parentDepth) continue;

                // The entire parent neighbour's face neighbours should not be replaced with just one octant,
                // so go down one level here, first.
                auto& neighbour = GetNode(neighbourIndex);
                if (InvalidIndex == neighbour.children) continue; // nothing high-depth enough to link back
                neighbourIndex = GroupAndChildToNode(neighbour.children, ci ^ bit); // same octant except opposite in direction d
                SetNeighbour(neighbourIndex, d, childIndex);
            }
        }
    }

    void Octree::Merge(NodeIndex index)
    {
        auto& parent = GetNode(index);
        auto& group = GetGroup(parent.children);
        for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
        {
            auto childIndex = GroupAndChildToNode(parent.children, ci);
            auto& node = group.nodes[ci];
            for (auto d = 0u; d < 3u; ++d)
            {
                SetNeighbour(node.neighbours[d], d, index);
            }
        }
        nodes.Free(parent.children);
        parent.children = InvalidIndex;
    }
}
