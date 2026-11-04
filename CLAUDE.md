@AGENTS.md

## Navigation: use Graphify first

Navigate this codebase with the Graphify MCP tools (`mcp__graphify__*`, repository `tessie1993/geode-app`) before grep or file search: `graphify_find` for definitions, `query_graph` for "how does X work", `graphify_callers` / `graphify_callees`, `graphify_file_neighbors` for a file's dependencies, `graphify_impact` / `impact_and_risk` before a change, `graphify_tests_for` for tests. Use grep only for literal strings, config values and files the graph lacks.

The graph is built from the default branch at one commit (`graph_stats` names it). Before you trust a result, run `git diff --name-status <indexed-commit> HEAD`; for any file in that list, read the current file instead of the graph.
