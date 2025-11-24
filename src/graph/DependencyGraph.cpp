#include "graph/DependencyGraph.hpp"
#include "core/Logger.hpp"

#include <queue>
#include <set>
#include <algorithm>
#include <sstream>

namespace graph {

void DependencyGraph::addNode(const std::string& name,
                              const std::vector<std::string>& reads,
                              const std::vector<std::string>& writes) {
    LOG_DEBUG("Adding node to dependency graph: '{}'", name);

    // Check for duplicates
    if (m_nodes.count(name) > 0) {
        throw std::runtime_error("Node already exists: " + name);
    }

    // Create node
    Node node;
    node.name = name;
    node.reads = reads;
    node.writes = writes;

    m_nodes[name] = node;

    LOG_DEBUG("Node '{}' added: reads {} fields, writes {} fields",
              name, reads.size(), writes.size());
}

void DependencyGraph::computeDependencies() {
    LOG_DEBUG("Computing dependencies based on read/write conflicts");

    // Clear adjacency list
    m_adjacencyList.clear();

    // For each node
    for (auto& [name, node] : m_nodes) {
        node.dependencies.clear();

        // For each field this node reads
        for (const auto& readField : node.reads) {
            // Find all nodes that write this field
            for (const auto& [otherName, otherNode] : m_nodes) {
                if (otherName == name) continue; // Skip self

                // If other node writes a field we read, it's a dependency
                if (std::find(otherNode.writes.begin(), otherNode.writes.end(), readField)
                    != otherNode.writes.end()) {
                    // Add dependency
                    if (std::find(node.dependencies.begin(), node.dependencies.end(), otherName)
                        == node.dependencies.end()) {
                        node.dependencies.push_back(otherName);
                        LOG_DEBUG("  '{}' depends on '{}' (read '{}')", name, otherName, readField);
                    }
                }
            }
        }

        // For each field this node writes
        for (const auto& writeField : node.writes) {
            // Find all nodes that read this field
            for (const auto& [otherName, otherNode] : m_nodes) {
                if (otherName == name) continue; // Skip self

                // If other node reads a field we write, add edge to adjacency list
                if (std::find(otherNode.reads.begin(), otherNode.reads.end(), writeField)
                    != otherNode.reads.end()) {
                    // We must run before otherNode
                    if (std::find(m_adjacencyList[name].begin(), m_adjacencyList[name].end(), otherName)
                        == m_adjacencyList[name].end()) {
                        m_adjacencyList[name].push_back(otherName);
                        LOG_DEBUG("  '{}' must run before '{}' (write '{}')", name, otherName, writeField);
                    }
                }
            }
        }
    }

    LOG_DEBUG("Dependency computation complete");
}

std::vector<std::string> DependencyGraph::buildSchedule() {
    LOG_INFO("Building execution schedule from dependency graph");

    // Compute dependencies if not already done
    computeDependencies();

    // Kahn's algorithm for topological sort
    std::map<std::string, uint32_t> inDegree;

    // Calculate in-degrees
    for (const auto& [name, node] : m_nodes) {
        inDegree[name] = node.dependencies.size();
    }

    // Initialize queue with zero in-degree nodes
    std::queue<std::string> queue;
    for (const auto& [name, degree] : inDegree) {
        if (degree == 0) {
            queue.push(name);
            LOG_DEBUG("Zero in-degree node: '{}'", name);
        }
    }

    std::vector<std::string> schedule;

    // Process queue
    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop();
        schedule.push_back(current);

        LOG_DEBUG("Schedule: {}", current);

        // Process all nodes that depend on current
        for (const auto& dependent : m_adjacencyList[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                queue.push(dependent);
            }
        }
    }

    // Check for cycles
    if (schedule.size() != m_nodes.size()) {
        LOG_ERROR("Circular dependency detected! Only {} of {} nodes scheduled",
                  schedule.size(), m_nodes.size());
        throw std::runtime_error("Circular dependency detected in stencil graph");
    }

    LOG_INFO("Execution schedule built: {} stencils", schedule.size());
    return schedule;
}

uint32_t DependencyGraph::getInDegree(const std::string& name) const {
    auto it = m_nodes.find(name);
    if (it == m_nodes.end()) {
        throw std::runtime_error("Node not found: " + name);
    }
    return static_cast<uint32_t>(it->second.dependencies.size());
}

uint32_t DependencyGraph::getOutDegree(const std::string& name) const {
    auto it = m_adjacencyList.find(name);
    if (it == m_adjacencyList.end()) {
        return 0;
    }
    return static_cast<uint32_t>(it->second.size());
}

std::string DependencyGraph::exportDOT() const {
    std::stringstream ss;

    ss << "digraph StencilDependencies {\n";
    ss << "    rankdir=LR;\n";
    ss << "    node [shape=box, style=rounded];\n\n";

    // Add nodes
    for (const auto& [name, node] : m_nodes) {
        ss << "    \"" << name << "\" [label=\"" << name << "\"];\n";
    }

    ss << "\n";

    // Add edges (dependencies)
    for (const auto& [name, node] : m_nodes) {
        for (const auto& dep : node.dependencies) {
            ss << "    \"" << dep << "\" -> \"" << name << "\";\n";
        }
    }

    ss << "}\n";

    return ss.str();
}

bool DependencyGraph::hasCycle() const {
    // Color-based cycle detection (white=0, gray=1, black=2)
    std::map<std::string, int> color;
    for (const auto& [name, _] : m_nodes) {
        color[name] = 0; // white
    }

    std::function<bool(const std::string&)> hasCycleDFS = [&](const std::string& node) -> bool {
        color[node] = 1; // gray

        for (const auto& dependent : m_adjacencyList.at(node)) {
            if (color[dependent] == 1) {
                return true; // Back edge (cycle)
            }
            if (color[dependent] == 0 && hasCycleDFS(dependent)) {
                return true;
            }
        }

        color[node] = 2; // black
        return false;
    };

    for (const auto& [name, _] : m_nodes) {
        if (color[name] == 0) {
            if (hasCycleDFS(name)) {
                return true;
            }
        }
    }

    return false;
}

} // namespace graph
