# Claude's memory for Onyx (backup)

A copy, taken on 2026-11-08, of Claude Code's per-project memory
(`C:\Users\troll\.claude\projects\C--Projects-Onyx\memory\`): one fact per file, `MEMORY.md` is the index.

To restore it after a fresh clone (the memory folder's name follows the checkout's path, here `C:\Projects\Onyx`):

```bash
mkdir -p ~/.claude/projects/C--Projects-Onyx/memory
cp docs/claude-memory/*.md ~/.claude/projects/C--Projects-Onyx/memory/
rm ~/.claude/projects/C--Projects-Onyx/memory/README.md
```

If the checkout is somewhere else, the folder is named after its path (`C:\X\Y` → `C--X-Y`).
