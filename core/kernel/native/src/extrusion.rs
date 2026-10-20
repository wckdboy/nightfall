// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Exact native extrusion with immutable Rust ownership; geometry remains delegated to OCCT.
use super::{Context, NativeError, NonNull, Rc, c_void, extrusion_ffi as ffi, status, with_native};
use nightfall_math::NurbsCurve2;
/// Sketch frame in mm with unit orthogonal x/normal axes. Geometry validation remains native.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Frame {
    pub origin: [f64; 3],
    pub x_axis: [f64; 3],
    pub normal: [f64; 3],
}
impl Default for Frame {
    fn default() -> Self {
        Self {
            origin: [0.0; 3],
            x_axis: [1.0, 0.0, 0.0],
            normal: [0.0, 0.0, 1.0],
        }
    }
}
/// Explicit connected profile: closed CCW outer chain and closed CW hole chains.
#[derive(Clone)]
pub struct Region {
    pub outer: Vec<NurbsCurve2>,
    pub holes: Vec<Vec<NurbsCurve2>>,
}
/// Borrowed during extrusion; curves/coordinates remain exact and unchanged. Displacement is mm.
#[derive(Clone)]
pub struct Extrusion {
    pub frame: Frame,
    pub regions: Vec<Region>,
    pub displacement: [f64; 3],
}
/// Exact validated nonempty-solid measures in mm units; bounds include backend tolerance.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Measures {
    pub volume: f64,
    pub area: f64,
    pub centroid: [f64; 3],
    pub minimum: [f64; 3],
    pub maximum: [f64; 3],
    pub solids: u64,
    pub faces: u64,
    pub edges: u64,
}
/// Transient source address in this call's inputs; never a persisted selector.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProfileEdge {
    pub region: u64,
    pub loop_index: u64,
    pub curve: u64,
    pub section: u64,
}
/// Native topology kind.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntityKind {
    Face,
    Edge,
}
struct Owner {
    pointer: NonNull<c_void>,
    context: Context,
    info: ffi::Info,
}
impl Drop for Owner {
    fn drop(&mut self) {
        // SAFETY: unique result allocation, matched release; Context drops AFTER this guard ends.
        with_native(|| unsafe { ffi::nf_native_result_release(self.pointer.as_ptr()) });
    }
}
/// Immutable solid snapshot; cloning retains geometry and backend on its creating OS thread.
#[derive(Clone)]
pub struct Shape(Rc<Owner>);
impl Shape {
    /// Owned exact measures remain valid after every facade/result is dropped.
    pub fn measures(&self) -> &Measures {
        &self.0.info.measures
    }
}
/// Retains the complete originating result/snapshot/backend. Never Send or Sync.
#[derive(Clone)]
pub struct Entity {
    owner: Rc<Owner>,
    pointer: NonNull<c_void>,
    kind: EntityKind,
}
impl Entity {
    /// Topology kind copied from the validated native entity.
    pub fn kind(&self) -> EntityKind {
        self.kind
    }
    /// Snapshot/subshape equality; reject foreign contexts before native access.
    pub fn same_as(&self, other: &Self) -> Result<bool, NativeError> {
        if !Rc::ptr_eq(&self.owner.context.0, &other.owner.context.0) {
            return Err(NativeError::InvalidArgument);
        }
        with_native(|| {
            let mut same = 0;
            // SAFETY: both retained immutable owners keep borrowed entities alive on this thread.
            status(unsafe {
                ffi::nf_native_entities_same(
                    self.pointer.as_ptr(),
                    other.pointer.as_ptr(),
                    &mut same,
                )
            })?;
            match same {
                0 => Ok(false),
                1 => Ok(true),
                _ => Err(NativeError::AbiMismatch),
            }
        })
    }
}
/// Generated face, with owned role/source metadata and independently retained entity.
#[derive(Clone)]
pub struct Generated {
    pub role: String,
    pub from: Option<ProfileEdge>,
    pub entity: Entity,
}
/// Prior-input changes; pure extrusion has none. Nonempty unsupported native history is rejected.
pub struct History {
    pub before: Entity,
    pub modified: Vec<Entity>,
    pub generated: Vec<Entity>,
    pub deleted: bool,
}
/// Complete extrusion result; estimates describe native retained geometry, not RSS.
pub struct OperationResult {
    pub shape: Shape,
    pub generated: Vec<Generated>,
    pub history: Vec<History>,
    pub estimated_bytes: u64,
}
fn encode_loop(
    input: &[NurbsCurve2],
    points: &mut Vec<Vec<ffi::Point>>,
    curves: &mut Vec<Vec<ffi::Curve>>,
) -> ffi::Loop {
    let row = input
        .iter()
        .map(|c| {
            points.push(
                c.points()
                    .iter()
                    .map(|p| ffi::Point { x: p[0], y: p[1] })
                    .collect(),
            );
            let p = points.last().unwrap();
            // Checked Rust curve construction bounds the degree at nine; slices stay borrowed.
            ffi::Curve {
                degree: c.knots().degree() as u32,
                knots: c.knots().knots().as_ptr(),
                knot_count: c.knots().knots().len(),
                points: p.as_ptr(),
                point_count: p.len(),
                weights: c.weights().as_ptr(),
                weight_count: c.weights().len(),
            }
        })
        .collect();
    curves.push(row);
    let row = curves.last().unwrap();
    ffi::Loop {
        curves: row.as_ptr(),
        count: row.len(),
    }
}
impl Context {
    /// Exact validated extrusion. Native policy/unsupported failures return no partial result.
    /// No client, XPC, model provider, sampled geometry or feature JSON is involved.
    pub fn extrude(&self, input: &Extrusion) -> Result<OperationResult, NativeError> {
        let (mut points, mut curves, mut holes, mut regions) =
            (Vec::new(), Vec::new(), Vec::new(), Vec::new());
        for r in &input.regions {
            let outer = encode_loop(&r.outer, &mut points, &mut curves);
            holes.push(
                r.holes
                    .iter()
                    .map(|h| encode_loop(h, &mut points, &mut curves))
                    .collect::<Vec<_>>(),
            );
            let h = holes.last().unwrap();
            regions.push(ffi::Region {
                outer,
                holes: h.as_ptr(),
                hole_count: h.len(),
            });
        }
        let request = ffi::Extrusion {
            frame: input.frame,
            displacement: input.displacement,
            regions: regions.as_ptr(),
            region_count: regions.len(),
        };
        let pointer = with_native(|| {
            let mut out = std::ptr::null_mut();
            // SAFETY: all exact input buffers and context remain live/aligned throughout this call.
            status(unsafe { ffi::nf_native_extrude(self.0.0.as_ptr(), &request, &mut out) })?;
            NonNull::new(out).ok_or(NativeError::NativeFailure)
        })?;
        // Construct outside the guard: every subsequent error releases without recursive locking.
        let mut owner = Owner {
            pointer,
            context: self.clone(),
            info: ffi::Info::default(),
        };
        with_native(|| {
            // SAFETY: retained native result and writable aligned output.
            status(unsafe { ffi::nf_native_result_query(pointer.as_ptr(), &mut owner.info) })
        })?;
        if owner.info.history_count != 0 {
            return Err(NativeError::AbiMismatch);
        }
        let owner = Rc::new(owner);
        let mut generated = Vec::new();
        for index in 0..owner.info.generated_count {
            let (g, role) = with_native(|| {
                let mut g = ffi::Generated::default();
                // SAFETY: owner retains result, index is bounded, output is live and aligned.
                status(unsafe {
                    ffi::nf_native_result_generated(pointer.as_ptr(), index, &mut g)
                })?;
                if g.role.is_null() || g.role_bytes > isize::MAX as usize {
                    return Err(NativeError::NativeFailure);
                }
                // SAFETY: this immutable borrowed range remains live through the owned copy.
                let bytes = unsafe { std::slice::from_raw_parts(g.role, g.role_bytes) };
                let role = std::str::from_utf8(bytes)
                    .map_err(|_| NativeError::NativeFailure)?
                    .to_owned();
                Ok((g, role))
            })?;
            let from = match g.has_from {
                0 => None,
                1 => Some(ProfileEdge {
                    region: g.region,
                    loop_index: g.r#loop,
                    curve: g.curve,
                    section: g.section,
                }),
                _ => return Err(NativeError::AbiMismatch),
            };
            let kind = match g.kind {
                1 => EntityKind::Face,
                2 => EntityKind::Edge,
                _ => return Err(NativeError::AbiMismatch),
            };
            let entity = Entity {
                owner: owner.clone(),
                pointer: NonNull::new(g.entity.cast_mut()).ok_or(NativeError::NativeFailure)?,
                kind,
            };
            generated.push(Generated { role, from, entity });
        }
        Ok(OperationResult {
            estimated_bytes: owner.info.estimated_bytes,
            shape: Shape(owner),
            generated,
            history: Vec::new(),
        })
    }
}
