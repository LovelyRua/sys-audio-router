## Workstream

- [ ] Engineer A: Windows backend and integration
- [ ] Engineer B: portable engine and control plane
- [ ] Engineer C: diagnostics and lab tooling

## Scope

Owned paths changed:

Cross-workstream handoff or explicitly delegated shared files:

## Verification

- [ ] `git diff --check`
- [ ] Relevant smoke tests
- [ ] WinRM slot used when Windows behavior changed: `engineer-___`
- [ ] `git diff --name-only origin/main...HEAD` stays inside this workstream
