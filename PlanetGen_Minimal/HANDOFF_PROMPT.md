# Handoff: PlanetGen (UE 5.7 Procedural Planet Generation, C++)

You're picking up an existing, actively-developed Unreal Engine 5.7 C++ project: a
runtime procedural spherical planet system (cube-sphere terrain, LOD streaming,
layered noise terrain features, biome material blending, ocean/atmosphere, foliage,
hand-placed static buildings, and a custom AI navigation system). This is a long-running
project with a lot of accumulated, hard-won detail — **read `README.md` in full before
making any changes.** It documents the architecture, the reasoning behind
non-obvious decisions, several previously-fixed bugs whose root causes are worth
knowing before you accidentally reintroduce them, and current open items.

## Two packages, same source

- `PlanetGen/` — full package (adds ocean, glow shell, atmosphere on top of the core).
- `PlanetGen_Minimal/` — terrain/streaming/noise/foliage/POI/navigation only.

They're kept in sync file-for-file wherever functionality overlaps. **If you fix a bug
or add a feature in one, check whether it applies to the other too** — this has been
the standing practice throughout development, and letting them drift would create a
confusing, hard-to-maintain split. Each package has its own `README.md`; read the one
matching whichever package you're actually working in (they're near-identical except
for the ocean/atmosphere-specific sections).

## Before touching any code, internalize these (all detailed further in the README)

1. **Any header (`.h`) change requires a full clean wipe** — delete
   `Intermediate/`, `Binaries/`, `Saved/`, `.vs/`, regenerate project files, then
   rebuild. `.cpp`-only changes are safe for incremental rebuilds. This has been
   unconditionally true throughout the project's history.
2. **Every noise/height sampling function must be a pure function of its inputs** —
   no LOD-dependent behavior, no hidden state, nothing non-deterministic. This single
   invariant is why chunk boundaries are seamless and why foliage/POI placement is
   reproducible. Breaking it (even subtly) reintroduces visible seams — this has
   happened multiple times and is always the first thing to check when a seam bug
   reappears.
3. **`RF_Transient` stops an object from being *saved*, not from being duplicated
   into PIE.** Anything spawned in the editor that must not exist in PIE needs
   `bIsEditorOnlyActor = true` as well. Anything that must exist correctly in PIE
   regardless of what got inherited across the editor/PIE boundary should
   unconditionally clear-and-respawn in `BeginPlay` rather than checking "do I
   already have one" via a pointer-validity check — those checks are unreliable
   across that boundary (a cross-world reference reads as valid but points at the
   wrong world's object). This exact bug has hit the ocean shell and POI buildings
   independently; watch for it in anything new that spawns transient content.
4. **The primary debugging/tuning loop is: click Regenerate Planet, look at the
   result.** Use `ENoiseDebugView` on the manager to inspect noise layers directly
   (paints the selected signal as grayscale into vertex color), wire raw
   `VertexColor -> BaseColor` in the material to isolate material vs. geometry vs.
   lighting causes, use Unlit viewmode to rule out lighting entirely. Prefer this
   over reasoning from code alone when something looks visually wrong — it's how
   essentially every bug in this project's history was actually diagnosed.
5. **Vertex positions are absolute world-space; actor transforms are always
   `FVector::ZeroVector`.** Anything new that spawns at `GetActorLocation()` instead
   of `ZeroVector` will double-offset for any planet not at world origin.

## Current state (see README "Known Open Items" for the full list)

Everything through Phase 1 static POIs and Phase 1 navigation is implemented and
working. The most likely next requests, roughly in order of what's been discussed as
"next" but not yet built:

- **Cross-face navigation** — `FPlanetNavGrid` currently only pathfinds within a
  single cube face by design; stitching across faces is unsolved.
- **POI procedural/algorithmic scatter** — currently hand-placed only via
  `UPlanetPOIMarkerComponent` + "Bake POIs From Markers."
- **POI-awareness in the nav grid** — the nav grid samples raw, unflattened noise
  height, so it doesn't yet know a building's flattened pad isn't steep terrain.
- **Distance-based streaming for POI buildings** — currently always-spawned
  regardless of distance; fine at the counts tested so far, untested at scale.
- **Enemy AI character setup** — the player-character gravity-redirection pattern
  (`SetGravityDirection`, documented in the README's Character Setup section)
  generalizes directly to AI; hasn't been built out into an actual AI Controller yet.

## How to start

1. Read the relevant package's `README.md` completely.
2. If the person hasn't stated what they want yet, ask — don't assume the "next
   logical" open item is what they want worked on next.
3. When implementing: match the existing code's comment density and reasoning style
   (comments here consistently explain *why*, not just *what* — that's been
   deliberate and has repeatedly saved re-deriving context later). Apply changes to
   both packages when the change applies to both. Repackage into the same output ZIP
   locations the project has been using, and remember the full-clean-wipe rule
   whenever a header changes.
