# Object-Oriented Architecture Philosophy

This document expands the architecture guidance in the neighboring `AGENTS.md`. Apply it when designing or changing object models, classes, relationships, and architecture. Repository-specific constraints and existing framework conventions remain important.

## 1. Human–AI design goal

Architecture must be understandable to both humans and AI. The object-message paradigm is the preferred conceptual model: objects have identity, state, responsibilities, and behavior; clients send messages by invoking methods. AI must learn and navigate this model, rather than pushing a flatter or more procedural architecture for its own convenience.

Favor recognizable domain objects and cohesive behavior. Explain designs in terms of which object is responsible for what, which messages it accepts, and what guarantees it provides.

## 2. Hierarchy and relationships

### Inheritance: is-a

- Prefer **single inheritance** for a genuine substitution relationship: a derived object should satisfy the promises made by its base class.
- Preserve meaningful conceptual hierarchies, including deeper hierarchies when each level expresses a useful concept or contract.
- Hierarchy depth is not by itself a reason to refactor.
- Avoid speculative base classes, misleading is-a relationships, and overrides that violate inherited contracts.
- Prefer explicit overrides and clearly documented invariants and extension points.

### Composition: has-a and independent capabilities

- Use **composition** for ownership, containment, delegation, or capabilities not naturally part of a single is-a chain.
- **Avoid multiple inheritance** for implementation reuse. Prefer one principal base class plus composed collaborators when several capabilities are needed.
- Multiple inheritance may be required by existing frameworks or justified by a documented, concrete benefit. Such exceptions must be deliberate and clearly explained.
- Do not create a tangle of tiny collaborators that scatter behavior belonging to one coherent domain object.

Do not confuse *class inheritance* (what an object is) with *object containment* (what an object has). Both may be displayed as trees, but they carry different semantics.

## 3. Required class-analysis workflow

Before a change that affects class behavior:

1. Locate the class declaration and implementation, not just its immediate method.
2. Traverse relevant base classes to identify inherited state, APIs, invariants, and pre/postconditions.
3. Determine virtual dispatch, method hiding, overloads, and override behavior.
4. Inspect relevant derived classes to detect effects on polymorphic callers.
5. Trace ownership and lifetime of referenced objects, including construction and destruction.
6. Examine collaborators and the significant incoming/outgoing call paths.
7. Check serialization, persistence, ABI/API contracts, and framework registration when applicable.
8. Identify existing tests and propose focused tests for the changed contract.

Use compiler indexes, language-server navigation, call hierarchy, and targeted searches when available. Do not assume a symbol's implementation from its name or an incomplete snippet.

## 4. Encapsulation and responsibility

- Keep state changes behind interfaces that maintain object invariants.
- Give each class a cohesive responsibility; avoid both god objects and needless fragmentation.
- Model ownership explicitly. Distinguish owning references from observing references and define lifetimes.
- Keep interfaces expressive; minimize external manipulation of object internals.
- Design failures explicitly: rejected operations should not silently leave objects partly updated.

## 5. Dependencies and extensibility

- Prefer understandable, directed dependencies; minimize cycles.
- Separate **mechanism** (reusable implementation) from **policy** (application decisions) without forcing them into non-object-oriented code.
- Keep domain objects conceptually distinct from numerical solution strategies. Numerical algorithms can themselves be strategy objects with explicit messages and contracts.
- Extend existing framework patterns when they already solve the problem. Do not introduce new architectural patterns without clear benefits.

## 6. Architectural change authority

Codex may suggest redesigns, but must get human approval before substantially changing inheritance trees, object responsibilities, public interfaces, persistence schemas, or fundamental paradigms. A recommendation should state:

- current objects, messages, and relationships;
- proposed objects, messages, and relationships;
- concrete benefits, tradeoffs, compatibility effects, and migration cost;
- alternatives, including retaining the current design.

Routine local changes that preserve the agreed object model do not require separate approval.

## 7. Domain examples (illustrative, not mandatory class definitions)

- `MbDPart` may be a specialized domain object in a meaningful single-inheritance chain.
- `MbDAssembly` *has* parts and joints; these containment relationships are not necessarily class inheritance.
- A flexible-body capability may be modeled through a specialized subtype or a composed model depending on whether it changes the object's identity/contract or adds an independent responsibility.
- A solver algorithm may be a replaceable strategy object while the part/assembly object model remains stable.

The guiding question is: **What organization best expresses the domain's real relationships while preserving contracts and making the behavior easy to trace?**
