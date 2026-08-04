---
name: git-worktree-branch
description: Create an isolated Git worktree with a new task branch, make and verify focused changes there, commit them, and safely merge them into a base branch while preserving unrelated local modifications. Use when the user asks to work in a separate worktree, create a branch-backed checkout, isolate changes from a dirty working tree, or merge a completed worktree branch back into main.
---

# Git Worktree Branch

Use one branch per worktree and keep the base checkout's local changes intact.

## Inspect before changing state

Run read-only checks from the base checkout:

```powershell
git status -sb
git worktree list
git branch --list <task-branch>
Test-Path -LiteralPath '<absolute-worktree-path>'
```

Choose a concise branch such as `codex/<task>` and a sibling path such as
`<repo>-wt-<task>`. Confirm that neither already exists. Read the repository's
`AGENTS.md` before creating or editing files.

Treat the base branch's committed tip as the worktree starting point. A dirty
base checkout is acceptable, but its uncommitted changes are intentionally not
copied into the new worktree.

## Create the isolated worktree

Create the branch and worktree together:

```powershell
git worktree add -b <task-branch> <absolute-worktree-path> <base-branch>
```

Verify the reported `HEAD`, then perform all task edits, generated setup, builds,
and tests inside the new worktree. Keep third-party or generated dependencies in
ignored project locations. Reuse an existing pinned toolchain safely when the
project supports it instead of modifying tracked setup paths.

## Commit the task branch

Before committing:

1. Run the relevant tests or builds in the isolated worktree.
2. Run `git diff --check` and inspect `git diff`.
3. Run `git status --short` and separate task files from generated artifacts.
4. Stage exact paths with `git add <path>...`; never use a broad add in a dirty
   repository.
5. Verify with `git diff --cached --check` and `git status --short`.
6. Create a concise, focused commit as required by the repository rules.

Finish with a clean task worktree. Do not push unless the user asks.

## Merge into the base branch

Reinspect both checkouts immediately before merging because the base branch may
have advanced:

```powershell
git status -sb
git log --oneline --decorate --graph -8 --all
git diff --name-only <base-branch>...<task-branch>
git diff --name-only
git diff --cached --name-only
```

Record the base checkout's dirty paths. If an uncommitted base change overlaps a
task-branch path, stop and ask the user how to proceed. Do not stash, reset,
restore, or force the merge automatically. Committed changes on both branches
may overlap; let Git's normal merge machinery combine them and inspect the
result.

When uncommitted paths are disjoint, merge from the base checkout:

```powershell
git merge --no-edit <task-branch>
```

Do not require a merge commit: allow Git to fast-forward when possible and use
its normal merge commit when the branches have diverged.

After a successful merge:

1. Confirm that there are no unmerged paths with `git status -sb`.
2. Compare dirty paths with the pre-merge snapshot to ensure user changes remain.
3. Verify `HEAD` and its parents with
   `git log -1 --pretty=format:'%h %p %s'`.
4. Inspect automatically merged source and run proportionate integration tests
   when the merge combined changes in the same files.
5. Report the merge commit or fast-forward target and whether push was performed.

If conflicts occur, preserve the merge state and inspect it. Resolve only clear,
in-scope conflicts; otherwise stop for user direction. Never use destructive
cleanup to hide a failed merge.

## Keep cleanup explicit

Retain the completed worktree and branch unless the user asks to remove them.
Before any requested removal, verify that the worktree is clean and the branch is
merged. Avoid `--force`; remove only the exact validated worktree path.
