# Fields and spatial machinery

States and the graph keep all meaning. The new machinery introduces no state
kind, scene hierarchy, ownership relation or runtime graph. A solver's bodies,
sources and indices must be derived from its owning state's elements and
params; results are written by that state's arrow. Data crossing a state
boundary still travels through a declared functor, lens or embedding.

`sg/spatial` contains double precision vectors/matrices, full affine transforms,
AABBs, rays, half-spaces, convex volumes, finite-surface projection and a BVH.
Rigid and renderer compatibility types remain available. Physics broadphase,
field support queries and rendering each keep their own index. Numbered BVH
leaves refer to the caller's current input array; they own no objects.

Rigid computations compile separately: `Broadphase.cpp`, `Collision.cpp`,
`Queries.cpp`, `RayQueries.cpp`, `RigidSolver.cpp` and `Joints.cpp`. The existing
sweep handles small worlds; the BVH handles 64 or more bodies. Contact ordering,
warm starting, sleeping, driven supports, joints and continuous collision
retain their existing algorithms.

## Field data

`sg::field::Source` describes a named channel, a transform, optional local AABB
and convex support, and an evaluator. Directional, radial and plane evaluators
are analytic. A `Backend::sample(local_position, time) const` can expose an
analytic surface, sampled flow, fluid simulation, diffusion or PDE result. It
does not have to use the rigid solver or a particular numerical method. Backend
data must be immutable/state-derived, reproducible when the state is restored,
and must not capture another state's mutable data or a private clock.

Samples contain a scalar and a vector. Receiver data maps a channel to
acceleration, force, torque, scalar or vector readings and a gain (for example
charge or susceptibility). Sources add in declaration order. Rigid consumes
only acceleration, force and torque; its owning state's other arrows may use
the readings for temperature, pressure, sound intensity or gameplay effects.
The supplied time comes from the state's drive. A field solver neither advances
time nor writes a response into a state.

Sources on a body are local to that body and placed from its current pose every
substep. Bodies may emit, receive, do both or neither; self responses are skipped.
Default bodies receive the `gravity` acceleration channel. Editing field data
that affects a sleeping body must also wake it through its owning state arrow.
The upright walker retains its y-up constraint and consumes only the vertical
gravity component; this refactor does not introduce arbitrary-surface walking.

Ordinary gravity is `Source::directional("gravity", {0, -9.81, 0})` in
`World::fields`. Clear that vector for no ambient field. Add negative-strength
radial sources for attraction to a point, or plane sources for attraction to
either side of a surface. Radial/plane exponent controls distance attenuation;
softening keeps the sample finite at the source. At a centre/on the plane the
direction is zero. Source scalar values can be attenuated the same way.

**API migration:** replace `world.gravity = v` with
`world.fields = {sg::field::Source::directional("gravity", v)}`. Existing
`rigid::V3`, `M3` and math names alias the spatial implementation. Pass the
beginning of the driven interval as `world.step(dt, time - dt)` when an event's
`time` is the end of that interval. The one-argument step remains valid for
time-independent fields. Explicit field edits and their wake policy belong to
the state, not to cache invalidation.

## Views and projected portals

`ViewPlan.cpp` constructs camera/clipping data, queries graph declarations and
builds draw candidates. `Visibility` uses its own BVH for mesh/wall candidates;
the original conservative sphere test remains the final test, preserving draw
order and edge behavior. `src/gl/World.cpp` draws the plan. The implicit room,
terrain, lamps and portal panels retain their existing drawing behavior.

World bindings require an open graph embedding or a declared seam at that
boundary. A feed requires an open embedding and the host's declared `feed`
panel. Bindings only provide drawing resources: they cannot grant access to a
state, reopen an embedding or create an overlap.
Raster panels retain their existing closed pictures and may show application
outputs reached through their declared embeddings and functors. Their raster
and texture revisions are presentation caches, not semantic state changes.

`examples/fields.sg` declares a projector, a finite wall, a projected portal, a
Physics state, a radial field and two nested 3D domains. Its native computations
are in `examples/Fields.cpp`. The host's projection arrow intersects a convex
cone with its wall, then sets the existing portal's `open`, position, aperture
and crop data. The engine follows that portal through its already declared
embedding. This example has no seam or traversal transition. It demonstrates a
rectangular aperture; arbitrary oblique polygons are available from `project`
for a state to encode in whatever declared surface representation it supports.

Run `ctest --test-dir build -R 'sg_fields|sg_spatial|sg_projected|sg_rigid'` for
the analytic fields, specialized backend, composition, supports, many bodies,
projection and nested views. `sg_projected_gl` additionally verifies that real
GL rendering does not change any state or graph facts.

The backend-neutral view functions also serve `sg/web/WebGPU.hpp`. Shared
geometry, portal framing, lights, shadow projections and spatial math remain
discardable answers to queries on StateGraph. GPU resources and stale bindings
cannot create relationships. Shader time reads declared `own_time` or the
state's Temporal drive; a state without either stays still. Frame intervals
only move transient Look blend weights. See [browser.md](browser.md).
