#pragma once

#include <string>
#include <vector>
#include <map>
#include <stdexcept>

namespace graph {

/**
 * @brief Directed Acyclic Graph (DAG) of stencil operations
 *
 * Tracks dependencies between stencils based on field reads/writes.
 * Uses topological sorting (Kahn's algorithm) to determine execution order.
 */
class DependencyGraph {
public:
    /**
     * @brief Stencil node in the dependency graph
     */
    struct Node {
        std::string name;
        std::vector<std::string> reads;      // Field names this stencil reads
        std::vector<std::string> writes;     // Field names this stencil writes
        std::vector<std::string> dependencies; // Stencils that must run before this one
    };

    /**
     * Initialize empty dependency graph
     */
    DependencyGraph() = default;

    /**
     * Add a stencil node with its field dependencies
     * @param name Unique stencil name
     * @param reads Fields read by this stencil
     * @param writes Fields written by this stencil
     * @throws std::runtime_error if node already exists
     */
    void addNode(const std::string& name,
                const std::vector<std::string>& reads,
                const std::vector<std::string>& writes);

    /**
     * Build execution schedule using topological sort (Kahn's algorithm)
     * @return Ordered list of stencil names for execution
     * @throws std::runtime_error if circular dependency detected
     */
    std::vector<std::string> buildSchedule();

    /**
     * Get execution order (alias for buildSchedule)
     * @return Ordered list of stencil names for execution
     */
    std::vector<std::string> getExecutionOrder() { return buildSchedule(); }

    /**
     * Export graph as DOT format for visualization (GraphViz)
     * @return DOT format string
     */
    std::string exportDOT() const;

    /**
     * Export to DOT format (alias for exportDOT)
     * @return DOT format string
     */
    std::string toDot() const { return exportDOT(); }

    /**
     * Get total number of nodes in the graph
     */
    size_t getNodeCount() const { return m_nodes.size(); }

    /**
     * Get all nodes in the graph
     */
    const std::map<std::string, Node>& getNodes() const { return m_nodes; }

    /**
     * Check if node exists
     */
    bool hasNode(const std::string& name) const { return m_nodes.count(name) > 0; }

    /**
     * Get in-degree (number of incoming dependencies) for a node
     */
    uint32_t getInDegree(const std::string& name) const;

    /**
     * Get out-degree (number of dependent nodes) for a node
     */
    uint32_t getOutDegree(const std::string& name) const;

    /**
     * Check if the graph has cycles (public accessor for testing)
     */
    bool hasCycle() const;

private:
    std::map<std::string, Node> m_nodes;
    std::map<std::string, std::vector<std::string>> m_adjacencyList; // name -> dependents

    /**
     * Compute dependencies based on read/write conflicts
     */
    void computeDependencies();
};

} // namespace graph
