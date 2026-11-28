# Security policy

## Reporting a vulnerability

Report vulnerabilities privately through GitHub: the **Security** tab of this repository, **Report a vulnerability**.
Please do not open a public issue for a vulnerability.

Include what is affected (firmware, client, app; version or commit), how to reproduce it and what an attacker gains.

What happens next:

- We acknowledge the report within 5 working days.
- We assess it, agree a disclosure date with you and fix it. The target is a fix within 90 days, sooner for severe issues.
- We publish a GitHub security advisory with a CVE identifier, credit you unless you prefer otherwise, and release fixed
  firmware and clients.
- An actively exploited vulnerability is handled first and reported as the EU Cyber Resilience Act requires.

## Supported versions

Security fixes are made to the latest release. The support period for each hardware release is stated in its release
notes. Updates are free.

## How we track vulnerabilities

- Every release carries a software bill of materials (SBOM, CycloneDX) listing the third-party components and versions.
- CI checks the dependencies against public vulnerability databases on every push and every night. A new finding opens
  an issue.
- See [docs/compliance.md](docs/compliance.md) for how the project maps to the EU Cyber Resilience Act, US and UK requirements.
