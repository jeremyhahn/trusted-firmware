# Code review

> **Status: NORMATIVE** · Scope: `.github/workflows/code-review.yml`
> (code-review-template v5.12.6) · Last verified: 2026-11-21

Label a pull request `code-review` and `task:ready` and Codex reviews it in
CI, then approves it or requests changes as the reviewer: `codex` on Gitea,
the login in the repository variable `CODEX_REVIEWER` on GitHub
(code-review.yml lines 738-741). Design: dept-software#17; the runner side:
dept-infra#24.

## Trigger

The workflow runs on `pull_request_target`, on `opened`, `reopened`,
`synchronize`, `labeled` and `ready_for_review`, and its definition always
comes from the base branch, never the PR head (code-review.yml lines 715-724).
It reviews a pull request that carries both `code-review` and `task:ready`
(lines 1-2): the review job's `if:` tests both labels in the event payload,
and that the PR is open, not `task:in-progress` and has its head in this
repository (lines 1227-1239). The guard then reads the **live** pull request
again and stays the authority, because a re-run replays the payload it was
created with (lines 1223-1224). A `labeled` event starts a review only for
`code-review`, `task:ready` or an `agent:`, `model:` or `effort:` label
(lines 1233-1238).

| Label | Effect |
| --- | --- |
| `code-review` | request the review |
| `model:<slug>` | the Codex model; default `gpt-6.1-sol` (line 1737) |
| `effort:low\|medium\|high` | reasoning effort; default `medium` |

A model must be in the runner's live catalog (`codex debug models`); a bare
family name is accepted only when exactly one slug carries it. Anything else,
two labels of one kind, or an unknown effort fails the job. Nothing falls back
to the default (line 1738). Adding a `model:` or `effort:` label re-reviews the
same head.

## Jobs

| Job | Check name | `needs` | What it does |
| --- | --- | --- | --- |
| `scope` | Review scope (InfoSec guard) | none | runs while the PR is open; a PR that changes only workflow files gets no review, and only when InfoSec's guard passes (lines 781-798) |
| `review` | Codex review | `scope` | the Codex review on the `codex` runner, under the `if:` in Trigger (lines 1207-1259) |
| `review-opencode` | Code Review (opencode) | `review` | an advisory review when routed to `agent:opencode`, on Gitea only (lines 3134-3138) |
| `gate` | Code Review (codex) | `scope`, `review` | `if: always()`; requires a Codex approval of the head, or a no-review change; one concurrency group per PR and head, a newer gate cancels an older one, 10-minute timeout (lines 3570-3582, 3608) |

## Credentials

There is no `secrets.*` in the workflow and no bot token in the repository
(line 680); the scope, review and gate jobs read the API with `github.token`
(lines 831, 1282, 3588).

- The Codex login is mounted at `/root/.codex` by the runner, root-only; codex
  runs as uid 1000 with a copy whose refresh token is blanked (lines 682,
  688-692).
- The reviewer's token is mounted read-only, root 0400, at
  `/run/reviewer/token`. It is read once into a mode-600 curl header file and
  never put on a command line (lines 681-685, 742). On Gitea the job asks whose
  token it is and refuses to continue unless the answer is `$REVIEWER`; on
  GitHub, whose installation token cannot read `/user`, it proves the identity
  with a `PENDING` review probe instead (lines 1524-1530, 1714-1729).

Codex runs with `-s danger-full-access`: its own sandbox cannot start inside the
job container, so the throwaway container is the boundary (lines 702-705). Two
consequences are accepted, not solved, here:

- A run can exfiltrate codex's short-lived access token and use it until it
  expires; no refresh token leaves the host (lines 697-698). The workflow never
  lets the model post: it computes the verdict itself, and the gate requires
  that the reviewer's latest standing verdict on the head is an approval
  (lines 3708-3709).
- `pull_request_target` takes the definition from the base branch, so a PR that
  edits `code-review.yml` is reviewed by the base branch's version, not its own
  edit (line 716). Who may push branches, and which repositories the runner
  serves, is the control (dept-infra#24).

## Verdict

Codex's final message is forced into a JSON schema: a summary and a list of
findings, each with a priority `P0`-`P3`, a `path:line` location and a reason.
The workflow maps it (lines 11-15):

| Findings | Review posted, pinned to `commit_id` = head |
| --- | --- |
| any P0 or P1 | `REQUEST_CHANGES`, listing the findings |
| none | `APPROVED`, listing any P2/P3 as advisory |
| codex fails, or its message is not that shape | an issue comment; no review |

## Fail-closed rules

- **The required check is the gate.** The jobs carry job-level `if:`s
  (lines 783, 1227, 3138), and a skipped job is a green status; the required
  context is the gate (line 1254), which runs `if: always()` and fails unless
  the head has a Codex approval or the change needs no review (lines 3573,
  3608).
- **The token file is required.** A missing token fails the job: there is no
  fallback credential (lines 684-685).
- **The gate's status follows the verdict.** It reads the reviews back and
  passes only on the reviewer's latest standing verdict on this exact head;
  `PENDING`, `COMMENTED` and dismissed reviews are no verdict (lines 3708-3709).
  A `codex` `REQUEST_CHANGES` on the head turns it red.
- **A rejection stands until the head moves.** A run that finds one fails with
  it instead of reviewing again, and an approval is never posted over one.
- **One review per head.** The review job's concurrency group is the PR and
  its head sha: one review runs, one waits, and a newer waiting run replaces an
  older one (lines 1252-1258). A run that finds an approval already on its head
  stops, unless a `model:`/`effort:` label was just added.
- **A pipeline error is a comment, never an approval.** It names the cause; push
  a new commit, or remove and re-add `code-review`, to try again.

No file under `.github/workflows/` merges a pull request: code-review.yml only
produces the review and the `Code Review (codex)` check (lines 3574-3576).
