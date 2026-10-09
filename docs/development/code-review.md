# Code review

> **Status: NORMATIVE** · Scope: `.github/workflows/code-review.yml`
> · Last verified: 2026-10-08

Label a pull request `code-review` and Codex reviews it on the CI server, then
approves it or requests changes as the `codex` Gitea user. Design:
dept-software#17; the runner side: dept-infra#24.

## Trigger

The workflow runs on `opened`, `reopened`, `synchronize` and `labeled`, on the
`codex` runner. It reviews only when the pull request **as Gitea reports it
now** carries `code-review`; the event payload is not trusted for that, because
a re-run replays the payload it was created with. Without the label the job
says so and passes: an unlabelled PR merges on green CI (automerge.yml), and
this workflow is not its gate.

| Label | Effect |
| --- | --- |
| `code-review` | request the review |
| `model:<slug>` | the Codex model; default `gpt-5.5` |
| `effort:low\|medium\|high` | reasoning effort; default `medium` |

A model must be in the runner's live catalog (`codex debug models`); a bare
family name is accepted only when exactly one slug carries it. Anything else,
two labels of one kind, or an unknown effort fails the job. Nothing falls back
to the default. Adding a `model:` or `effort:` label re-reviews the same head.

## Credentials

There is no `secrets.*` in the workflow and no bot token in the repository.

- The Codex login is mounted at `/root/.codex` by the runner.
- The reviewer's Gitea token is mounted read-only at `/run/reviewer/token`. It
  is read once into a mode-600 curl header file, removed at the end of the job,
  and never echoed or put on a command line. The job then asks Gitea whose
  token it is and refuses to continue unless the answer is `codex` (the token
  needs `read:user` for that), because automerge accepts approvals from several
  logins.

Codex runs with `-s danger-full-access`: its own sandbox cannot start inside the
job container, so the throwaway container is the boundary. Two consequences are
accepted, not solved, here:

- Codex can read the token file. A prompt injection in the change under review
  can therefore act as `codex` for the length of the job. The workflow never
  lets the model post: it computes the verdict itself, and the final check
  requires that the latest `codex` verdict on the head is the one this run
  posted.
- `pull_request` workflows run from the PR head, so a PR that edits
  `code-review.yml` is reviewed by its own edit, and any PR could add a
  workflow on the `codex` runner that reads the mounts. Who may push branches,
  and which repositories the runner serves, is the control (dept-infra#24).

## Verdict

Codex's final message is forced into a JSON schema: a summary and a list of
findings, each with a priority `P0`-`P3`, a `path:line` location and a reason.
The workflow maps it:

| Findings | Review posted, pinned to `commit_id` = head |
| --- | --- |
| any P0 or P1 | `REQUEST_CHANGES`, listing the findings |
| none | `APPROVED`, listing any P2/P3 as advisory |
| codex fails, or its message is not that shape | an issue comment; no review |

## Fail-closed rules

- **No job-level `if:`.** A skipped job is a green status on Gitea. Every gate
  is a step.
- **The token file is required.** Absent or empty, the job fails and names
  dept-infra#24.
- **The job's status follows the verdict.** The last step reads the reviews back
  and passes only if a submitted, non-dismissed `codex` `APPROVED` stands on
  this exact head. `PENDING` is a draft and does not count. A `codex`
  `REQUEST_CHANGES` on the head turns the job red.
- **A rejection stands until the head moves.** A run that finds one fails with
  it instead of reviewing again, and an approval is never posted over one.
- **One review per head.** A new push cancels the review of the old head
  (concurrency per PR number). A run that finds an approval already on its head
  stops, unless a `model:`/`effort:` label was just added.
- **A pipeline error is a comment, never an approval.** It names the cause; push
  a new commit, or remove and re-add `code-review`, to try again.

The merge itself is automerge.yml's: a `code-review` PR merges once CI is green
and a non-author approver, here `codex`, has approved the current head.
