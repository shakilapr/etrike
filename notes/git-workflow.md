# Git Workflow Notes

This note documents safe everyday Git usage for this repository, including how to rename old commit messages without accidentally committing current work.

## 1. Inspect before changing history

Always confirm the current branch, upstream relationship, and working tree first:

```bash
git status -sb
git branch --show-current
git log --oneline --decorate -15
git remote -v
```

Do not assume that a clean-looking editor means the Git working tree is clean. Preserve unrelated modifications and untracked files.

## 2. Normal commit workflow

Review and stage only the intended paths:

```bash
git diff
git add -- path/to/file-a path/to/file-b
git diff --cached
git commit -m "docs: describe the completed change"
```

Prefer commit subjects that say what changed, for example:

```text
feat(can): generate normalized typed protocol metadata
test(can): consolidate codec coverage and golden-vector extraction
fix(adapter): detect CANalyst receive-worker failure
docs(control-ui): define center-locked vehicle projection
```

Avoid schedule-only subjects such as `Phase 3`, `Phase 5 complete`, or `Finish next phase`. Phase numbering loses meaning when work is reordered or commits are read outside the original plan.

## 3. Renaming old commits

Changing a commit message rewrites that commit. Every descendant receives a new commit hash even when all file contents remain identical.

You do not need to check out another branch when the commits are already ancestors of the current branch. For a small number of commits, use an interactive rebase with `reword`.

### 3.1 Protect current work

Create an optional recovery pointer and stash tracked, staged, unstaged, and untracked work:

```bash
git branch backup-before-reword
git stash push -u -m "before renaming old commits"
git status -sb
```

`-u` includes untracked files. Ignored files are not included; use `-a` only when ignored files genuinely must be stashed.

Do not proceed until `git status` shows that the worktree is clean. If the stash command fails, stop and resolve that problem before rebasing.

### 3.2 Choose the rebase range

If the oldest commit to rename is within the last ten commits:

```bash
git rebase -i HEAD~10
```

For history containing merge commits that must be preserved:

```bash
git rebase -i --rebase-merges HEAD~10
```

Choose a range that includes the oldest target commit. Use `git log --oneline` first; do not guess a very large range unnecessarily.

### 3.3 Mark only target commits as `reword`

In the rebase todo editor, leave unrelated commits as `pick` and change only the desired messages to `reword`:

```text
pick   a12bc34 first commit
reword b23cd45 old phase-based message
pick   c34de56 another commit
reword d45ef67 another phase-based message
```

Save and close. Git opens a commit-message editor for each `reword` entry. Replace the subject with a description of the actual result. Do not use `edit`, `squash`, or `fixup` when the requirement is only to rename messages.

### 3.4 Handle problems during rebase

Check the current state:

```bash
git status
```

If a conflict occurs, resolve it deliberately, stage the resolved files, and continue:

```bash
git add -- resolved/file
git rebase --continue
```

To abandon the rewrite and restore the pre-rebase branch state:

```bash
git rebase --abort
```

Do not start another rebase, pull, reset, or checkout while a rebase is unresolved.

### 3.5 Restore current work

After the rebase succeeds, inspect the rewritten history before restoring the stash:

```bash
git log --oneline --decorate -15
git stash pop --index
git status -sb
```

`--index` attempts to restore which changes were staged. A stash pop can conflict; if it does, the stash is normally retained and the conflicts must be resolved manually. Confirm that staged, unstaged, and untracked files returned as expected.

### 3.6 Verify that only messages changed

Compare the rewritten branch against the recovery pointer:

```bash
git diff --stat backup-before-reword..HEAD
git diff --quiet backup-before-reword..HEAD
```

For a message-only rewrite, `git diff --quiet` should exit successfully and print nothing because the final trees are identical. Also confirm the intended new subjects:

```bash
git log --oneline --decorate
```

The backup branch still points to the old history. Keep it until verification and any required push are complete.

## 4. Pushing rewritten history

If the commits were never pushed, a normal push is sufficient. If they are already on the remote, history replacement is required:

```bash
git fetch origin
git push --force-with-lease origin your-branch
```

Use `--force-with-lease`, not `--force`. The lease refuses to overwrite remote work when the remote branch has changed since the last fetch.

Rewriting a shared or protected default branch disrupts collaborators whose branches are based on the old hashes. Obtain explicit agreement before rewriting it. Prefer a pull request or avoid renaming published commits when repository policy prohibits force pushes.

After a successful push:

```bash
git status -sb
git log -1 --oneline --decorate
```

Remove the temporary recovery branch only when the remote and local histories are verified:

```bash
git branch -D backup-before-reword
```

## 5. Recovering old history

If a rewrite went wrong, the backup branch is the simplest recovery source. Git reflog can also locate previous branch tips:

```bash
git reflog
```

Do not use `git reset --hard` while valuable unstashed work exists. First inspect the target commit and preserve current files.

## 6. When interactive rebase is not appropriate

Interactive `reword` is the preferred approach for a few commits in a manageable recent range. A specialized history-rewrite tool may be more suitable for systematic changes across hundreds of commits or many refs, but it increases the blast radius and requires separate backup, verification, and collaboration planning.

For two or three commit-message changes, use:

```text
inspect → backup → stash -u → rebase -i/reword → verify trees → restore stash → force-with-lease if approved
```

Official references:

- [git-rebase](https://git-scm.com/docs/git-rebase)
- [git-stash](https://git-scm.com/docs/git-stash)
- [git-push and force-with-lease](https://git-scm.com/docs/git-push)
