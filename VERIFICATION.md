# Correctness, Verification, and Validation

This document expands the correctness guidance in the neighboring `AGENTS.md`. Read it when implementing behavior, planning tests, debugging numerical results, or claiming correctness.

## 1. Three distinct responsibilities

**Correctness by construction:** Design interfaces and object states to prevent whole classes of errors where practical.

**Verification:** Establish that an implementation conforms to its specification and contracts.

**Validation:** Establish that the specification/model is appropriate for its intended real-world use.

None substitutes for the others. Passing tests provides evidence, not mathematical certainty in general.

## 2. Contracts and invariants

Before implementing a behavior, identify:

- object invariants that must hold after construction and every successful public operation;
- preconditions, postconditions, and allowable state transitions;
- units, coordinate systems, frames of reference, and numerical ranges;
- ownership, object lifetime, and aliasing assumptions;
- error handling, exceptions, and rollback/partial-update behavior.

Enforce these through encapsulation, explicit types, initialization checks, well-defined APIs, and defensive validation where appropriate. Avoid invalid or half-initialized states. Derived classes must honor inherited contracts.

## 3. Verification strategy

Choose the strongest practical combination of:

1. Unit tests for individual object contracts and edge cases.
2. Integration tests for object collaboration, persistence, polymorphism, and interfaces.
3. Regression tests for previously identified bugs.
4. Property-based or metamorphic tests for invariants and transformations.
5. Independently calculated reference results, including exact or analytical solutions.
6. Numerical convergence tests as time steps, tolerances, or mesh sizes change.
7. Conservation laws and mathematical identities, with suitable caveats for modeled sources, dissipation, and numerical error.
8. Comparison with independently implemented solvers and trusted benchmarks.

Independent C++ and Rust implementations that agree are useful cross-checks but can share the same conceptual mistake. Ground tests in mathematical/physical requirements, not merely in either implementation's output.

## 4. Numerical and engineering standards

For numerical work, document and test:

- units, precision, sign conventions, coordinate frames, and initial/boundary conditions;
- accepted absolute/relative tolerances and why they are appropriate;
- stability, conditioning, convergence order, and singular/degenerate cases when relevant;
- deterministic inputs and repeatable test environments as far as practical;
- expected numerical error versus modeling error.

Do not require bit-for-bit agreement across different compilers, architectures, or solvers unless the system specifically promises it. Compare within justified numerical tolerances.

For physical simulation, test basic limiting cases, conservation or balance equations where applicable, and trusted benchmark problems before treating more complex results as validated.

## 5. Model validation

Ask whether the chosen mathematical and physical assumptions describe the actual problem: rigid versus flexible bodies, contact/friction models, linear versus nonlinear response, constraints, material properties, loads, boundary conditions, and ranges of applicability.

Use trusted experimental data or validated benchmarks when available. Document known gaps: a correctly solved wrong model is still the wrong answer.

## 6. Required Codex workflow

For a material change:

1. State or recover the intended behavior and relevant contracts.
2. Inspect the affected objects and relationships as described in `ARCHITECTURE.md`.
3. Identify failure modes and the most informative test cases.
4. Implement the smallest coherent change while preserving invariants.
5. Run targeted checks and wider regression checks when feasible.
6. Report exactly which build/test commands ran and their outcomes.
7. Identify tests that were not run, unresolved assumptions, and remaining risks.

Never invent successful test results or assume that compilation establishes correctness. If a tool, dependency, or environment is unavailable, state the limitation and suggest the next verifiable check.
