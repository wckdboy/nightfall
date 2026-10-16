# SMLIB-001: Use-after-free in booleans on solids with closed edges

> **Status: DEFERRED — marked for later.** Not reported upstream and not patched, by
> maintainer decision (2026-10-16). Revisit before RFC 0001 is accepted, before SMLib
> ships in any Nightfall build, and on every new usd-brep release.

| | |
| --- | --- |
| Component | SMLib (usd-brep) 1.0.0, commit `c9c5979f04cea8c38213004d4f04df0dcfb40d05` |
| Found | 2026-10-16, ASan + UBSan build of upstream's SM_API suite (#4, RFC 0001 F3) |
| Severity | High: memory-safety bug in the most common CAD operation (drilling a hole) |
| Upstream KNOWN_ISSUES.md | Not listed |
| Tracking | [#15](https://github.com/wckdboy/nightfall/issues/15) (label `status: deferred`) |

## Symptom

Any boolean (difference, union or intersection) where an operand has a **closed edge**,
an edge that starts and ends at the same vertex, as a cylinder's circular edges do,
writes to freed memory while the operation cleans up. Under AddressSanitizer it aborts:

```
ERROR: AddressSanitizer: heap-use-after-free
  #0 SmProtoEdge::RemoveProtoVertex(SmProtoVertex*)          SmProtoTopology.cpp:3371
  #1 SmProtoVertex::~SmProtoVertex()                         SmProtoTopology.cpp:1893
  ...
  #7 SmProtoTopologyManager::~SmProtoTopologyManager()      SmProtoTopology.cpp:3573
  #10 SmTopologyIntersector::IntersectInsertRelate()        SmTopologyIntersector.cpp:1267
  #11 SmMerge::ManifoldBoolean(...)                          SmMerge.cpp:2661
  #12 SmApiBooleanDifference(...)                            SmApiBrep.cpp:341
freed by: SmProtoEdge::~SmProtoEdge()                        SmProtoTopology.cpp:2646
```

Without sanitizers the operation returns `SM_SUCCESS` and upstream's tests pass (115/115),
because the stray write usually lands in memory nobody has reused yet. With a different
allocation pattern it can corrupt the heap or crash.

| Operands (each in its own process, ASan) | Difference | Union | Intersection |
| --- | --- | --- | --- |
| Box ∘ box | clean | clean | clean |
| Box ∘ cylinder | **use-after-free** | **use-after-free** | **use-after-free** |

## Minimal reproduction

Build SMLib with the `asan` preset and `-DNF_WITH_SMLIB=ON`, then compile and run:

```cpp
// One through-hole in a 12 x 12 x 1.5 mm plate.
#include <SmApiBrep.h>
#include <SmApiPrimitives.h>
#include <SmBrep.h>
#include <SmVector3d.h>
int main() {
  SmBrep *plate = nullptr, *tool = nullptr, *result = nullptr;
  SmApiCreateBox(SmVector3d(0, 0, 0), 12.0, 12.0, 1.5, plate);
  SmApiCreateCylinder(SmVector3d(1.5, 1.5, -1.0), 0.75, 3.5, tool);
  return SmApiBooleanDifference(plate, tool, result) == SM_SUCCESS ? 0 : 1;
}
```

```sh
B=build/asan-smlib/third_party/smlib; U=third_party/src/usd-brep/source
clang++ -std=c++17 -g -fsanitize=address,undefined -I$U/SMLib/inc -I$U/SM_API/inc repro.cpp \
  -L$B -lSM_API -lSMLib -Wl,-rpath,$B -o repro && ./repro
```

## Root cause

Vertex↔edge links are kept in two places that disagree for closed edges:

1. It is a documented design rule. `SmProtoTopology.h:107`: *"'DD1': For the
   ProtoVertex's list of ProtoEdge's: if the PE is closed, list it twice, even though
   they're the same."* `SmProtoVertex::AddProtoEdge` implements it by appending
   unconditionally (`SmProtoTopology.h:502`).
2. When the intersector creates an edge, it links both ends
   (`SmProtoTopology.cpp:4246–4247`):
   `pStartPV->AddProtoEdge( rpNewPE) ; pEndPV->AddProtoEdge( rpNewPE) ;`
   For a closed edge `pStartPV == pEndPV`, so **the vertex lists the edge twice**.
3. `SmProtoEdge::~SmProtoEdge` breaks DD1: it unlinks itself **once**, removing from the
   start vertex and skipping the end vertex when it equals the start
   (`SmProtoTopology.cpp:2657–2659`).
   `SmProtoVertex::RemoveProtoEdge` removes only the first match.
4. The vertex is left holding a dangling pointer to the freed edge.
   `SmProtoTopologyManager`'s destructor frees edges before vertices (cleanup objects are
   destroyed in reverse declaration order), so `~SmProtoVertex` then calls
   `RemoveProtoVertex` through that dangling pointer.

Reordering the manager's destructor alone would hide the symptom without fixing the
inconsistent links.

## Candidate fix (not applied)

Remove every occurrence of the edge from its vertices, matching the "even if already
there" rule in `AddProtoEdge`:

```diff
--- a/source/SMLib/src/SmProtoTopology.cpp
+++ b/source/SMLib/src/SmProtoTopology.cpp
@@ SmProtoEdge::~SmProtoEdge()
-  if(    m_pStartPV)              { m_pStartPV->RemoveProtoEdge(this) ; }
+  // A closed edge is listed twice on its single vertex (AddProtoEdge, DD1); remove every entry.
+  if(    m_pStartPV)              { while( m_pStartPV->RemoveProtoEdge(this) == SM_SUCCESS ) {} }
   if(    m_pEndPV 
-     && (m_pEndPV != m_pStartPV)) { m_pEndPV->RemoveProtoEdge(this) ; }
+     && (m_pEndPV != m_pStartPV)) { while( m_pEndPV->RemoveProtoEdge(this) == SM_SUCCESS ) {} }
```

Verified so far, in a local experiment that was then reverted:

- All six operand/operation combinations above run clean under ASan with the patch.
- The full upstream SM_API suite under ASan + UBSan with the patch ran for 846 s with no
  further AddressSanitizer reports, then UBSan halted it in **test code**:
  `TestSmCurves.cpp:938: load of value 4294967295, which is not a valid value for type
  'const SmCurveParameterizationType'` (the test feeds an out-of-range enum, which is UB in
  C++). The suite's progress output was lost at the abort, so how much of the suite ran
  is unknown.

Still to do before relying on it: rerun the suite with UBSan set to recover
(`-fsanitize-recover=undefined`) so it completes, and check whether other code paths
(for example `SmProtoVertex::CombineProtoVertex`, which appends edge lists) create
duplicates that need the same treatment.

## Impact on Nightfall and options

- The kernel runs out of process (XPC), so a crash takes down the kernel service, not the
  app. Silent heap corruption is the worse outcome: it can produce wrong geometry.
- Through-holes, bosses, pins and fillets on cylinders all create closed edges, so this
  path runs in almost every real part.
- **Options when revisited:** carry the patch in our usd-brep fork (with a regression
  test built from the repro), report upstream, or count it against SMLib in the bake-off.
  The bake-off harness (#6) already runs both kernels under ASan and reports
  memory-safety errors per 1,000 operations.
