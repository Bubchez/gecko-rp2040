# T06–T07 deterministic simulation and read-only sessions

Milestone: 2026-11-15, based on portable-core commit `d7d300b`. Host-only C++17;
no physical adapter, SDK, network, HTTP implementation or deployment. T08 has not
started. All scenarios and inbound envelopes remain **synthetic**. No upstream
implementation code was copied; the existing scoped license and no-reuse decision
apply to these new modules.

Historical T06–T07 record: subsequent T08 work now adds the separate loopback
HTTP adapter and explicitly acknowledged script retirement for long-running mock
operation. See [HOST_MOCK.md](HOST_MOCK.md) and [HTTP_API_V1.md](HTTP_API_V1.md).
The T06–T07 stop boundary below is historical; the current stop boundary is T09.

## Public interfaces and ownership

- `include/gecko/transport.hpp`: bounded transaction/RX scripts, phase-aware
  outcomes, role trace, queue/loss statistics and `SimulatedTransport`.
- `include/gecko/session.hpp`: explicit `SessionConfig`, `ReadOnlySession` and
  side-effect-free `Snapshot` queries.
- `assembly.hpp`: complete assemblies now carry first-fragment and completion
  timestamps from the synthetic transport metadata, not from the pack.

The transport and session are noncopyable/nonmovable. One session exclusively
drives one transport; the transport must outlive that session. External code must
not simultaneously drive/activate the transport. There are no threads, real-time
clocks, blocking sleeps or real I/O in either implementation. The caller advances
an injected uint64 microsecond clock and polls/ticks explicitly. Script additions
and snapshot queries do not execute bus operations.

Default session configuration is inactive, with unknown profile and strict
documented-only decoding. Synthetic operation requires explicit selection of
`SyntheticReadOnly`, `SyntheticCandidate`, candidate v51 and expected C82/S81.
Those exact versions select a **test model**, not a hardware compatibility grant.
Other profile/version pairs are rejected before GO. Passive mode observes inputs
without simulated master requests or slave read replies; it never becomes ready.

## Deterministic transport model

Register up to 64 `TxPlan`s and 64 chronologically ordered `RxPlan`s. No JSON
scenario parser or HTTP route is implemented. Scenario definitions are executable,
independently authored C++ fixtures in `tests/scenario_tests.cpp`.

Each TX plan requires exact expected bytes and an inclusive submission-time window.
No wildcard or implicit byte mask is applied. Request IDs and session epochs are
local metadata, not invented protocol bytes. Every request must specify address
0x17, two read bytes, repeated START and final STOP; zero IDs, expired deadlines
and inconsistent metadata reject before consuming a plan.

Normal role sequence:

```text
Listen -> MasterWrite -> MasterRead -> Stop -> Restore -> Listen
```

Four separately injected durations control write, read, STOP and restoration.
Results report correlated request/session IDs, phase, error, write-count certainty,
read bytes/count and whether listening was restored. Two read bytes are checked
for **count**, not assumed to encode an application-level acceptance response.
Their values are copied from the plan. RX repeated-read behavior is represented
by Receive/Reply/Listen trace events; the role trace is not an electrical waveform
or a byte-level timing validation of a slave implementation.

At equal timestamps, TX phase completion or deadline is processed before RX.
An exact transaction deadline expires before completion. Clock stretching/delayed
STOP can be modeled by a long duration; the overall transaction deadline still
expires. A write-phase timeout has an unknown transferred count and is marked
possibly applied, even if the known byte count is zero. Address NACK with explicit
zero transfer is distinguishable from a partial or potentially applied write.

Address/data NACK, arbitration loss, short read, timeout, stuck SDA/SCL and failed
restore are scriptable. Restoration failure leaves the transport released. Failure
abort/release is atomic in the synthetic model; **zero modeled recovery duration
does not establish a safe hardware recovery sequence**. RX during master ownership
is explicitly dropped and increments the loss generation. This intentionally
simple collision model does not claim actual multi-master arbitration behavior.

RX events carry explicit session, loss-generation and optional synthetic fragment
index metadata. These values are not decoded from unknown wire fields. Queue
capacity is 32 events. Overflow drops complete queued events, records counts and
returns a priority loss fault before any remaining frames can advance a session.
The 512-entry trace is also bounded: trace exhaustion makes the scenario fail;
there is no silent trace truncation. `scenario_complete()` checks all installed
expectations/arrivals were consumed and there is no pending transaction/event or
sticky scenario error. It applies to the finite scripted horizon, not an endlessly
running session's future keepalives. Explicit cancellation can end an in-flight
transaction; submitted/completed counts distinguish it from completion.

The synthetic unit fixtures deliberately use 10 µs phase durations and 100 µs
handshake spacing to test scheduling cheaply. These are shorter than real
100 kHz bus wire times and must **never** be used as hardware timing evidence.
Memory is bounded but host-oriented; script/trace arrays are not an RP2040 RAM budget.

## Session and freshness rules

```text
Disabled / passive Disconnected
  -> GoPending -> AwaitingFiles -> AwaitingClock -> AwaitingLo -> Ready
       any bounded failure -> Backoff -> new epoch and GO
       terminal fault / retry exhaustion -> Fault (transport inactive)
```

- GO completion requires exact write count, two read bytes and restored listening.
  The two XML filenames, clock and LO then advance the synthetic handshake in
  order. ACK submission alone never advances it: each ACK must complete first.
- Default file order is config then status. `EitherSynthetic` explicitly permits
  the alternate fixture order only as a test assumption. Exact repeated files or
  the same clock while awaiting LO receive bounded idempotent ACKs. Duplicates do
  not renew the progress deadline; contradictory/unsolicited handshake messages
  do not create readiness. Unknown versions invalidate the candidate binding.
- Ready state means handshake completion; `snapshot.ready` additionally requires
  a complete fresh decoded status in the **current** session/profile. Before the
  first status it stays false. Missing status has a separate deadline.
- Status is published by copy with a monotonically increasing local revision.
  Freshness uses the oldest fragment acquisition time; completion age is reported
  separately. Entire assemblies already stale when completed are rejected.
  Optional invalid temperature fields remain absent as in the portable core.
- Noise, GO/ACK echoes, program/command frames, malformed messages, old-epoch data,
  partial assemblies and repeated GET-like snapshot queries do not refresh status.
  An interleaved unrelated frame aborts a partial assembly. Assembly gaps, invalid
  ordering, session/loss change and overflow discard the entire message.
- Freshness expiry invalidates the session/profile and starts bounded recovery.
  Previous observed values/revision remain visible as stale; they cannot make a
  new session ready. Snapshot reads are copies with no traffic or timer side effects.
- GO cadence cannot overlap an active handshake. Periodic renewal creates a new
  epoch and requires new profile/handshake/status evidence. Late results/RX from
  an older epoch are ignored. Exact deadline handling is conservative: a late
  poll cannot revive an already expired handshake using queued historical RX.
- Exponential backoff is capped, with jitter from an injected seeded xorshift
  generator. The failed-attempt budget resets only on a fresh validated status,
  not on TX ACK or LO. Exhaustion and fatal faults stop participation. A failed
  restore requires explicit host-local `start()` and a full new handshake.
- Checked deadlines, elapsed-time arithmetic and clock-regression detection avoid
  wraparound retries. A revision or deadline overflow faults rather than wraps.

Defaults (23 s GO, 25 ms TX timeout, 1 s handshake/status deadline, 60 s stale,
100 ms–1 s backoff, three failed attempts, two duplicates) are **model policy**,
not measured pack limits. The independent assembler retains 250 ms gap / 1 s total.
For future T08 integration, `status_age_us` is the conservative oldest-fragment
age and `completed_age_us` is time since completion; do not silently substitute one
for the other. This milestone does not implement or revise an HTTP contract.

## Read-only enforcement

`ReadOnlySession::request_control` always returns ReadOnly and never submits work,
even when a fresh snapshot is ready. Independently, the transport validates bytes
and accepts only the exact GO and fixed handshake ACK literals. Temperature,
program, switch, malformed and arbitrary packets are rejected even when submitted
directly to the transport; no purpose label can bypass that check. There is no
write-enable flag, raw write endpoint, heater override or automatic clock setter.

`physical_communication_enabled` is a constant false; no alternative transport
implementation can be substituted into the session. Terminal faults deactivate
the simulated participant so it cannot continue even implicit read replies.
Nothing in this model authorizes physical GO, ACK, slave replies or actuator work.

## Core review and regression fixes

The existing raw decoder/bounds tests remain passing; no new raw-parser out-of-bounds
defect was reproduced. Review of integration requirements found these blockers:

| Finding | Reproduced regression | Resolution |
|---|---|---|
| Assemblies lacked acquisition timestamps; a final part could make old prefix data appear fresh | `scenario.freshness`: a 150 µs freshness budget accepted an assembly spanning 200 µs | Add first/completion acquisition metadata in the core; freshness uses first fragment and rejects already-stale completed assemblies. |
| The first session integration ignored a GO echo without aborting an existing prefix | `scenario.loss_recovery`: an interleaved echo allowed a mixed partial stream to complete | Abort assembly on unrelated/ignored nonfragment families as well as malformed input. A new explicit index zero is required. |
| Terminal session faults inhibited requests but left simulated slave replies active | `scenario.temporal_boundaries`: clock regression left transport listening | Fault deactivates participation; only explicit local restart re-enables the synthetic model. |
| Unknown write count was initially represented as zero | Review of deadline-in-write classification | Add count certainty; an in-progress write timeout is possibly applied. No actuator can be sent or retried. |
| Queue overflow did not report all discarded RX counts | Review of saturation diagnostics | Count all discarded queue events/RX, including the flush after overflow. |
| Copying a live session/transport could duplicate counters and ownership state | Interface review | Make both noncopyable/nonmovable; enforce this with compile-time tests. Results are nodiscard. |
| A delayed `start()` changed participation before advancing the clock, recording activation in the past | `scenario.temporal_boundaries`: start at 1000 µs recorded Listen at 0 µs | Advance the old transport mode to the activation instant before enabling the new mode. Old inactive-interval arrivals cannot receive retroactive replies. |

The first three regression tests were run against the initial integration and all
three failed. They pass after these fixes. These are implementation/model fixes;
they add no evidence about the real Gecko bus.
The activation-time regression was also run before its fix and failed, then passed
in the final full suites. Repeated polls of an already terminal sticky scenario
error are tested not to count the same failure repeatedly.

## Automated scenario coverage

Fourteen new CTest groups supplement the original 16 core groups. Coverage includes
normal phase/handshake traces; every handshake TX failing at eight outcomes
(32 cases); recovery through a complete new epoch for all eight outcomes; all four
arbitration/deadline phases; partial/unknown outcomes; duplicate and order handling;
no-status/staleness; periodic renewal; queue/collision loss; expired/interleaved
assemblies; stale events; conservative acquisition age; fixed-seed determinism;
read-only enforcement in both layers; passive/default modes and uint64 boundaries.
Detailed test output and case names are committed with [VALIDATION.md](VALIDATION.md).

Remaining evidence blockers: real inbound routing/integrity, C/S-to-pack layout,
wire counters/order, deadlines and RP2040/electrical behavior. Upstream source
reuse remains blocked by missing permissions. **Stop before T08.**
