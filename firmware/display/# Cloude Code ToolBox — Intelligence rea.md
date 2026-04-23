# Cloude Code ToolBox — Intelligence readiness

Run commands from the Command Palette (`Cloude Code ToolBox: …`) or the **MCP & skills** hub.

## ✓ claude-md
`CLAUDE.md` looks populated.

Suggested: `CloudeCodeToolBox.openInstructionsPicker`

## ✓ claude-rules
No `.claude/rules/*.md` files (optional scoped rules).

Suggested: `CloudeCodeToolBox.syncCursorRules`

## ✓ agents-md
No `AGENTS.md` (optional agent-oriented instructions).

Suggested: `CloudeCodeToolBox.openInstructionsPicker`

## ✓ memory-bank
No `memory-bank/` directory (optional long-lived project memory).

Suggested: `CloudeCodeToolBox.initMemoryBank`

## ○ mcp-json
Workspace `.vscode/mcp.json` missing.

Suggested: `CloudeCodeToolBox.portCursorMcp`

## ○ cursorrules
No `.cursorrules` or `.cursor/rules` files detected.

Suggested: `CloudeCodeToolBox.createCursorrulesTemplate`

## ✓ copilot-instructions-legacy
Legacy Copilot instructions appear merged into `CLAUDE.md` (toolbox marker present).

Suggested: `CloudeCodeToolBox.openInstructionsPicker`

## ✓ mcp-vs-claude-session
VS Code **`.vscode/mcp.json`** powers editor MCP; **Claude Code** uses **`/mcp`** in the Claude panel and may use `~/.claude/settings.json`. Configure both if you use MCP in each surface.

Suggested: `CloudeCodeToolBox.openClaudeUserSettingsJson`
