// OpenShape shim: the tiny subset of Boost.Graph PlaneGCS uses to partition
// the constraint system into independent components. Avoids a Boost
// dependency. Semantics match boost::connected_components on an undirected
// vecS graph: component ids are assigned in order of each component's lowest
// vertex index.
#pragma once

#include <cstddef>
#include <numeric>
#include <vector>

namespace boost {

struct vecS {};
struct undirectedS {};

template <typename OutEdgeList = vecS, typename VertexList = vecS, typename Directed = undirectedS>
class adjacency_list {
public:
    std::size_t addVertex()
    {
        parent_.push_back(parent_.size());
        return parent_.size() - 1;
    }
    void addEdge(std::size_t a, std::size_t b)
    {
        while (parent_.size() <= std::max(a, b))
            addVertex();
        const std::size_t ra = find(a), rb = find(b);
        if (ra != rb)
            parent_[std::max(ra, rb)] = std::min(ra, rb);
    }
    std::size_t size() const { return parent_.size(); }
    std::size_t find(std::size_t v)
    {
        while (parent_[v] != v) {
            parent_[v] = parent_[parent_[v]];
            v = parent_[v];
        }
        return v;
    }

private:
    std::vector<std::size_t> parent_;
};

template <typename A, typename B, typename C>
std::size_t add_vertex(adjacency_list<A, B, C>& g)
{
    return g.addVertex();
}

template <typename A, typename B, typename C, typename V>
void add_edge(V a, V b, adjacency_list<A, B, C>& g)
{
    g.addEdge(static_cast<std::size_t>(a), static_cast<std::size_t>(b));
}

template <typename A, typename B, typename C>
std::size_t num_vertices(const adjacency_list<A, B, C>& g)
{
    return g.size();
}

template <typename A, typename B, typename C, typename OutIt>
int connected_components(adjacency_list<A, B, C>& g, OutIt components)
{
    std::vector<int> label(g.size(), -1);
    int next = 0;
    for (std::size_t v = 0; v < g.size(); ++v) {
        const std::size_t root = g.find(v);
        if (label[root] < 0)
            label[root] = next++;
        components[v] = label[root];
    }
    return next;
}

} // namespace boost
