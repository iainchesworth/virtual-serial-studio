The GUI now has a narrow backend adapter between every screen and
`vss::studio`, with `StudioBackend`, `EngineStudioBackend` and
`FakeStudioBackend`. The adapter exposes ports, devices, links, owner
arbitration, sends, control lines, document snapshots, setup facts and a
tri-state peer value, and records the engine capabilities the design needs.
A coalescing event bridge marshals hub events to the GUI thread through a
`shared_ptr` queue, so an in-flight delivery after `unsubscribe` cannot touch
freed memory; it batches by the refresh interval and caps the queue with a
dropped count. The capability table in `docs/gui-design/implementation-plan.md`
marks peer-open detection, line wiring, fault injection, rename and benchmark
as unavailable so later screens can degrade visibly instead of simulating
them. Nine backend tests cover the fake, link shapes, owner validation,
snapshot round trips, coalescing, the queue cap and the shutdown race.
