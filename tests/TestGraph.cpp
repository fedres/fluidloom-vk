#include "VulkanFixture.hpp"
#include "graph/DependencyGraph.hpp"
#include "stencil/StencilDefinition.hpp"

#include <catch2/catch_all.hpp>

/**
 * Test Suite: Execution Graph (Dependency Scheduling)
 *
 * Verifies DAG construction, topological sorting, and cycle detection.
 */
TEST_CASE("DAG creation with single node", "[graph][dag]")
{
    graph::DependencyGraph dag;

    // Add single stencil
    stencil::StencilDefinition def;
    def.name = "advect";
    def.inputs = {"velocity"};
    def.outputs = {"density_new"};

    dag.addNode(def.name, def.inputs, def.outputs);

    REQUIRE(dag.getNodeCount() == 1);
}

TEST_CASE("DAG with linear dependency chain", "[graph][dag]")
{
    graph::DependencyGraph dag;

    // Create chain: A -> B -> C
    dag.addNode("A", {}, {"x"});
    dag.addNode("B", {"x"}, {"y"});
    dag.addNode("C", {"y"}, {"z"});

    REQUIRE(dag.getNodeCount() == 3);

    // Get execution order
    auto schedule = dag.getExecutionOrder();

    REQUIRE(schedule.size() == 3);
    // Should execute in order: A, B, C
    REQUIRE(schedule[0] == "A");
    REQUIRE(schedule[1] == "B");
    REQUIRE(schedule[2] == "C");
}

TEST_CASE("DAG with parallel operations", "[graph][dag]")
{
    graph::DependencyGraph dag;

    // Both A and B write different outputs, can run in parallel
    dag.addNode("A", {"velocity"}, {"density_new"});
    dag.addNode("B", {"velocity"}, {"pressure_new"});

    auto schedule = dag.getExecutionOrder();

    REQUIRE(schedule.size() == 2);
    // Order doesn't matter, but both should be present
    REQUIRE((schedule[0] == "A" || schedule[0] == "B"));
    REQUIRE((schedule[1] == "A" || schedule[1] == "B"));
}

TEST_CASE("DAG with diamond dependency", "[graph][dag]")
{
    graph::DependencyGraph dag;

    // Diamond:
    //   A
    //  / \
    // B   C
    //  \ /
    //   D

    dag.addNode("A", {}, {"x"});
    dag.addNode("B", {"x"}, {"y"});
    dag.addNode("C", {"x"}, {"z"});
    dag.addNode("D", {"y", "z"}, {"result"});

    auto schedule = dag.getExecutionOrder();

    REQUIRE(schedule.size() == 4);

    // Verify constraints
    auto posA = std::find(schedule.begin(), schedule.end(), "A");
    auto posB = std::find(schedule.begin(), schedule.end(), "B");
    auto posC = std::find(schedule.begin(), schedule.end(), "C");
    auto posD = std::find(schedule.begin(), schedule.end(), "D");

    // A must come before B and C
    REQUIRE(posA < posB);
    REQUIRE(posA < posC);

    // B and C must come before D
    REQUIRE(posB < posD);
    REQUIRE(posC < posD);
}

TEST_CASE("Cycle detection - simple cycle", "[graph][dag][cycle]")
{
    graph::DependencyGraph dag;

    // Create a cycle: A writes x, B reads x writes y, A reads y
    dag.addNode("A", {"y"}, {"x"});
    dag.addNode("B", {"x"}, {"y"});

    // Should detect the cycle
    bool hasCycle = dag.hasCycle();
    REQUIRE(hasCycle);
}

TEST_CASE("Cycle detection - no cycle", "[graph][dag][cycle]")
{
    graph::DependencyGraph dag;

    dag.addNode("A", {}, {"x"});
    dag.addNode("B", {"x"}, {"y"});
    dag.addNode("C", {"y"}, {"z"});

    bool hasCycle = dag.hasCycle();
    REQUIRE_FALSE(hasCycle);
}

TEST_CASE("Cycle detection - complex cycle", "[graph][dag][cycle]")
{
    graph::DependencyGraph dag;

    // Create cycle: A -> B -> C -> A
    dag.addNode("A", {"z"}, {"x"});
    dag.addNode("B", {"x"}, {"y"});
    dag.addNode("C", {"y"}, {"z"});

    bool hasCycle = dag.hasCycle();
    REQUIRE(hasCycle);
}

TEST_CASE("Self-loop detection", "[graph][dag][cycle]")
{
    graph::DependencyGraph dag;

    // A both reads and writes the same field
    dag.addNode("A", {"x"}, {"x"});

    // This is technically a self-loop (in-place operation)
    // Should be allowed for in-place computations
    // But dependency system should detect the read->write ordering requirement

    auto schedule = dag.getExecutionOrder();
    REQUIRE(schedule.size() == 1);
}

TEST_CASE("DAG export to DOT format", "[graph][visualization]")
{
    graph::DependencyGraph dag;

    dag.addNode("A", {}, {"x"});
    dag.addNode("B", {"x"}, {"y"});

    std::string dotString = dag.toDot();

    // Verify basic DOT format
    REQUIRE(dotString.find("digraph") != std::string::npos);
    REQUIRE(dotString.find("A") != std::string::npos);
    REQUIRE(dotString.find("B") != std::string::npos);
}

TEST_CASE("Empty DAG handling", "[graph][dag]")
{
    graph::DependencyGraph dag;

    auto schedule = dag.getExecutionOrder();
    REQUIRE(schedule.empty());

    bool hasCycle = dag.hasCycle();
    REQUIRE_FALSE(hasCycle);
}

TEST_CASE("Large DAG scheduling", "[graph][performance]")
{
    graph::DependencyGraph dag;

    // Create a chain of 100 nodes
    for (int i = 0; i < 100; ++i) {
        std::string nodeName = "node_" + std::to_string(i);
        std::vector<std::string> inputs;
        std::vector<std::string> outputs{"field_" + std::to_string(i)};

        if (i > 0) {
            inputs.push_back("field_" + std::to_string(i - 1));
        }

        dag.addNode(nodeName, inputs, outputs);
    }

    auto schedule = dag.getExecutionOrder();

    REQUIRE(schedule.size() == 100);
    REQUIRE_FALSE(dag.hasCycle());

    // Verify order
    for (size_t i = 0; i < schedule.size() - 1; ++i) {
        int cur = std::stoi(schedule[i].substr(5));
        int next = std::stoi(schedule[i + 1].substr(5));
        REQUIRE(cur < next);
    }
}
