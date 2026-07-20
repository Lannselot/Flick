---
name: implement
description: "Implement a piece of work based on a spec or set of tickets."
disable-model-invocation: true
---

Implement the work described by the user in the spec or tickets.

Use /tdd where possible, at pre-agreed seams.

Run typechecking regularly, single test files regularly, and the full test suite once at the end.

Once done, use /code-review to review the work.

Commit your work to the current branch.

After committing, push the current branch to GitHub.

- Verify that the working tree is clean.
- Never force-push.
- If the branch already has an upstream, run `git push`.
- Otherwise, if the `origin` remote exists, run `git push -u origin HEAD`.
- If no suitable GitHub remote exists, authentication fails, or the push is rejected, stop and report the exact blocker. Do not rewrite history or change remotes automatically.
- Report the pushed remote and branch in the final response.
