# Architecture Decision Records

Template: [ADR-000](ADR-000-template.md). IDs follow the decision register in the
[master spec, section 33](../NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md).

| ADR | Decision | Status |
|---|---|---|
| [001](ADR-001-core-language-and-toolchain.md) | C++23 subset, CMake, MSVC first | Accepted |
| [002](ADR-002-graphics-backend.md) | Vulkan 1.3, single backend, own thin RHI | Accepted |
| [003](ADR-003-editor-gui-framework.md) | Avalonia (C#) for NoorEditor | **Proposed**, spike pending |
| [019](ADR-019-licensing.md) | MIT license | Accepted |
| [021](ADR-021-platform-layer-and-dependencies.md) | SDL3 platform layer, pinned-dependency policy | Accepted |

Still open from the register: 004 scene model, 005 serialization, 006 asset registry,
007 plugin ABI, 008 .NET hosting, 009 Lua, 010 physics (Jolt is the front-runner),
011 audio, 012 PBR model, 013 world coordinates, 014–018, 020 branding.
