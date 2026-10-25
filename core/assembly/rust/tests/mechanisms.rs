// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Mechanisms against closed-form positions (RFC 0013 gate G3). Tolerances are explicit.
use nightfall_assembly::{
    Assembly, AssemblyError, Frame, Joint, JointKind, Occurrence, Placement, SolveOptions, Status,
    joint_coordinates, solve,
};
use std::f64::consts::PI;

const CLOSE_MM: f64 = 1e-9;
const CLOSE_RAD: f64 = 1e-9;

fn planar(origin: [f64; 2], angle: f64) -> Placement {
    Placement::from_axis_angle([0.0, 0.0, 1.0], angle, [origin[0], origin[1], 0.0]).unwrap()
}
fn occ(id: &str, grounded: bool, placement: Placement) -> Occurrence {
    Occurrence {
        id: id.into(),
        grounded,
        placement,
    }
}
fn revolute(id: &str, a: &str, at_a: [f64; 3], b: &str, at_b: [f64; 3]) -> Joint {
    Joint::new(
        id,
        JointKind::Revolute,
        a,
        Frame::at(at_a),
        b,
        Frame::at(at_b),
    )
}
fn dist(p: [f64; 3], q: [f64; 2]) -> f64 {
    ((p[0] - q[0]).powi(2) + (p[1] - q[1]).powi(2) + p[2].powi(2)).sqrt()
}
fn assert_solved(s: &nightfall_assembly::Solution) {
    assert_eq!(s.report.status, Status::Solved, "{:?}", s.report);
    for r in &s.report.residuals {
        assert!(r.length_mm <= CLOSE_MM && r.angle_rad <= CLOSE_RAD, "{r:?}");
    }
}

/// Circle–circle intersection of |P - b| = rb and |P - d| = rd nearest `near`.
fn intersect(b: [f64; 2], rb: f64, d: [f64; 2], rd: f64, near: [f64; 2]) -> [f64; 2] {
    let (dx, dy) = (d[0] - b[0], d[1] - b[1]);
    let l = (dx * dx + dy * dy).sqrt();
    let a = (rb * rb - rd * rd + l * l) / (2.0 * l);
    let h = (rb * rb - a * a).sqrt();
    let p = [b[0] + a * dx / l, b[1] + a * dy / l];
    let c1 = [p[0] - h * dy / l, p[1] + h * dx / l];
    let c2 = [p[0] + h * dy / l, p[1] - h * dx / l];
    let n = |c: [f64; 2]| (c[0] - near[0]).hypot(c[1] - near[1]);
    if n(c1) <= n(c2) { c1 } else { c2 }
}

// Grashof crank-rocker: ground 100, crank 40, coupler 120, rocker 80 mm (40 + 120 < 100 + 80).
const GROUND: f64 = 100.0;
const CRANK: f64 = 40.0;
const COUPLER: f64 = 120.0;
const ROCKER: f64 = 80.0;

fn four_bar(theta: f64, c: [f64; 2], drive: Option<f64>) -> Assembly {
    let b = [CRANK * theta.cos(), CRANK * theta.sin()];
    let d = [GROUND, 0.0];
    let mut crank = revolute("crank_pivot", "ground", [0.0; 3], "crank", [0.0; 3]);
    crank.values = vec![drive];
    Assembly {
        occurrences: vec![
            occ("ground", true, Placement::IDENTITY),
            occ("crank", false, planar([0.0, 0.0], theta)),
            occ(
                "coupler",
                false,
                planar(b, (c[1] - b[1]).atan2(c[0] - b[0])),
            ),
            occ("rocker", false, planar(d, (c[1] - d[1]).atan2(c[0] - d[0]))),
        ],
        joints: vec![
            crank,
            revolute(
                "crank_coupler",
                "crank",
                [CRANK, 0.0, 0.0],
                "coupler",
                [0.0; 3],
            ),
            revolute(
                "coupler_rocker",
                "coupler",
                [COUPLER, 0.0, 0.0],
                "rocker",
                [ROCKER, 0.0, 0.0],
            ),
            revolute(
                "rocker_pivot",
                "ground",
                [GROUND, 0.0, 0.0],
                "rocker",
                [0.0; 3],
            ),
        ],
    }
}

#[test]
fn four_bar_closes_on_its_branch_over_a_full_crank_turn() {
    let theta0 = PI / 3.0;
    let b0 = [CRANK * theta0.cos(), CRANK * theta0.sin()];
    let c0 = intersect(b0, COUPLER, [GROUND, 0.0], ROCKER, [80.0, 120.0]);
    let mut asm = four_bar(theta0, c0, None);
    let free = solve(&asm, &SolveOptions::default()).unwrap();
    assert_solved(&free);
    assert_eq!(free.report.dof, 1);
    assert_eq!(free.report.loops, 1);
    assert!(
        free.report.redundant.is_empty(),
        "{:?}",
        free.report.redundant
    );

    let mut c = c0;
    let (mut worst_mm, mut worst_rad, mut iterations) = (0.0_f64, 0.0_f64, 0);
    let started = std::time::Instant::now();
    for step in 1..=72 {
        let theta = theta0 + step as f64 * 2.0 * PI / 72.0;
        asm.joints[0].values = vec![Some(theta)];
        let s = solve(&asm, &SolveOptions::default()).unwrap();
        assert_solved(&s);
        assert_eq!(s.report.dof, 0);
        let b = [CRANK * theta.cos(), CRANK * theta.sin()];
        c = intersect(b, COUPLER, [GROUND, 0.0], ROCKER, c);
        let coupler_end = s.placements[2].apply_point([COUPLER, 0.0, 0.0]);
        let rocker_end = s.placements[3].apply_point([ROCKER, 0.0, 0.0]);
        let crank_end = s.placements[1].apply_point([CRANK, 0.0, 0.0]);
        assert!(
            dist(coupler_end, c) <= CLOSE_MM,
            "step {step}: {coupler_end:?} vs {c:?}"
        );
        assert!(dist(rocker_end, c) <= CLOSE_MM, "step {step}");
        assert!(dist(crank_end, b) <= CLOSE_MM, "step {step}");
        for r in &s.report.residuals {
            (worst_mm, worst_rad) = (worst_mm.max(r.length_mm), worst_rad.max(r.angle_rad));
        }
        iterations += s.report.iterations;
        for (o, p) in asm.occurrences.iter_mut().zip(&s.placements) {
            o.placement = *p;
        }
    }
    eprintln!(
        "four-bar: 72 steps, worst residual {worst_mm:.3e} mm / {worst_rad:.3e} rad, {iterations} iterations, {:.3} ms",
        started.elapsed().as_secs_f64() * 1e3
    );
}

#[test]
fn slider_crank_matches_the_closed_form_slider_position() {
    let (r, l) = (40.0, 120.0);
    let along_x = || Frame::new([0.0; 3], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]).unwrap();
    let theta0 = 0.4_f64;
    let x0 = r * theta0.cos() + (l * l - (r * theta0.sin()).powi(2)).sqrt();
    let b0 = [r * theta0.cos(), r * theta0.sin()];
    let mut crank = revolute("crank_pivot", "ground", [0.0; 3], "crank", [0.0; 3]);
    crank.values = vec![None];
    let mut asm = Assembly {
        occurrences: vec![
            occ("ground", true, Placement::IDENTITY),
            occ("crank", false, planar([0.0, 0.0], theta0)),
            occ("rod", false, planar(b0, (-b0[1]).atan2(x0 - b0[0]))),
            occ("slider", false, planar([x0, 0.0], 0.0)),
        ],
        joints: vec![
            crank,
            revolute("crank_rod", "crank", [r, 0.0, 0.0], "rod", [0.0; 3]),
            revolute("rod_slider", "rod", [l, 0.0, 0.0], "slider", [0.0; 3]),
            Joint::new(
                "guide",
                JointKind::Slider,
                "ground",
                along_x(),
                "slider",
                along_x(),
            ),
        ],
    };
    let free = solve(&asm, &SolveOptions::default()).unwrap();
    assert_solved(&free);
    assert_eq!(free.report.dof, 1);
    assert_eq!(free.report.loops, 1);
    for step in 1..=72 {
        let theta = theta0 + step as f64 * 2.0 * PI / 72.0;
        asm.joints[0].values = vec![Some(theta)];
        let s = solve(&asm, &SolveOptions::default()).unwrap();
        assert_solved(&s);
        let x = r * theta.cos() + (l * l - (r * theta.sin()).powi(2)).sqrt();
        let slider = s.placements[3].apply_point([0.0; 3]);
        assert!(
            dist(slider, [x, 0.0]) <= CLOSE_MM,
            "step {step}: {slider:?} vs {x}"
        );
        let travel = joint_coordinates(&asm, &s.placements).unwrap()[3][0];
        assert!(
            (travel - x).abs() <= CLOSE_MM,
            "step {step}: slider coordinate {travel}"
        );
        for (o, p) in asm.occurrences.iter_mut().zip(&s.placements) {
            o.placement = *p;
        }
    }
}

#[test]
fn degrees_of_freedom_follow_the_joint_kinds() {
    let chain = Assembly {
        occurrences: vec![
            occ("ground", true, Placement::IDENTITY),
            occ("upper", false, planar([0.0, 0.0], 0.3)),
            occ(
                "lower",
                false,
                planar([50.0 * 0.3_f64.cos(), 50.0 * 0.3_f64.sin()], 0.9),
            ),
        ],
        joints: vec![
            revolute("shoulder", "ground", [0.0; 3], "upper", [0.0; 3]),
            revolute("elbow", "upper", [50.0, 0.0, 0.0], "lower", [0.0; 3]),
        ],
    };
    let s = solve(&chain, &SolveOptions::default()).unwrap();
    assert_solved(&s);
    assert_eq!((s.report.dof, s.report.loops), (2, 0));

    let ball = Assembly {
        occurrences: vec![
            occ("ground", true, Placement::IDENTITY),
            occ("head", false, planar([0.0, 0.0], 0.2)),
        ],
        joints: vec![Joint::new(
            "socket",
            JointKind::Ball,
            "ground",
            Frame::at([0.0; 3]),
            "head",
            Frame::at([0.0; 3]),
        )],
    };
    assert_eq!(
        solve(&ball, &SolveOptions::default()).unwrap().report.dof,
        3
    );

    for (kind, dof) in [
        (JointKind::Rigid, 0),
        (JointKind::Revolute, 1),
        (JointKind::Slider, 1),
        (JointKind::Cylindrical, 2),
        (JointKind::PinSlot, 2),
        (JointKind::Planar, 3),
        (JointKind::Ball, 3),
    ] {
        let asm = Assembly {
            occurrences: vec![
                occ("ground", true, Placement::IDENTITY),
                occ("part", false, Placement::IDENTITY),
            ],
            joints: vec![Joint::new(
                "j",
                kind,
                "ground",
                Frame::at([0.0; 3]),
                "part",
                Frame::at([0.0; 3]),
            )],
        };
        let s = solve(&asm, &SolveOptions::default()).unwrap();
        assert_solved(&s);
        assert_eq!(s.report.dof, dof, "{kind:?}");
    }
}

#[test]
fn a_duplicate_joint_is_named_redundant() {
    let asm = Assembly {
        occurrences: vec![
            occ("ground", true, Placement::IDENTITY),
            occ("arm", false, planar([0.0, 0.0], 0.5)),
        ],
        joints: vec![
            revolute("hinge", "ground", [0.0; 3], "arm", [0.0; 3]),
            revolute("hinge_again", "ground", [0.0; 3], "arm", [0.0; 3]),
        ],
    };
    let s = solve(&asm, &SolveOptions::default()).unwrap();
    assert_solved(&s);
    assert_eq!(s.report.redundant, vec!["hinge_again".to_string()]);
    assert_eq!(s.report.dof, 1);
}

#[test]
fn an_unclosable_loop_fails_names_its_joints_and_keeps_the_placements() {
    // Ground 100, crank 80, coupler 30, rocker 40: at a crank angle of 180 degrees the crank tip
    // is 180 mm from the rocker pivot, beyond 30 + 40.
    let asm = {
        let theta = 0.0_f64;
        let b = [80.0 * theta.cos(), 0.0];
        let mut crank = revolute("crank_pivot", "ground", [0.0; 3], "crank", [0.0; 3]);
        crank.values = vec![Some(PI)];
        Assembly {
            occurrences: vec![
                occ("ground", true, Placement::IDENTITY),
                occ("crank", false, planar([0.0, 0.0], theta)),
                occ("coupler", false, planar(b, 1.2)),
                occ("rocker", false, planar([100.0, 0.0], 2.0)),
            ],
            joints: vec![
                crank,
                revolute(
                    "crank_coupler",
                    "crank",
                    [80.0, 0.0, 0.0],
                    "coupler",
                    [0.0; 3],
                ),
                revolute(
                    "coupler_rocker",
                    "coupler",
                    [30.0, 0.0, 0.0],
                    "rocker",
                    [40.0, 0.0, 0.0],
                ),
                revolute(
                    "rocker_pivot",
                    "ground",
                    [100.0, 0.0, 0.0],
                    "rocker",
                    [0.0; 3],
                ),
            ],
        }
    };
    let s = solve(&asm, &SolveOptions::default()).unwrap();
    assert_eq!(s.report.status, Status::Failed);
    assert!(!s.report.conflicting.is_empty());
    let input: Vec<Placement> = asm.occurrences.iter().map(|o| o.placement).collect();
    assert_eq!(s.placements, input);
}

#[test]
fn a_value_edit_keeps_the_elbow_up_branch() {
    let theta = PI / 3.0;
    let b = [CRANK * theta.cos(), CRANK * theta.sin()];
    let up = intersect(b, COUPLER, [GROUND, 0.0], ROCKER, [80.0, 200.0]);
    let mut asm = four_bar(theta, up, Some(theta));
    let s = solve(&asm, &SolveOptions::default()).unwrap();
    for (o, p) in asm.occurrences.iter_mut().zip(&s.placements) {
        o.placement = *p;
    }
    let edited = 2.0 * PI / 3.0;
    asm.joints[0].values = vec![Some(edited)];
    let s = solve(&asm, &SolveOptions::default()).unwrap();
    assert_solved(&s);
    let b = [CRANK * edited.cos(), CRANK * edited.sin()];
    let up = intersect(b, COUPLER, [GROUND, 0.0], ROCKER, [80.0, 200.0]);
    let down = intersect(b, COUPLER, [GROUND, 0.0], ROCKER, [80.0, -200.0]);
    let c = s.placements[3].apply_point([ROCKER, 0.0, 0.0]);
    assert!(dist(c, up) <= CLOSE_MM, "{c:?} vs elbow-up {up:?}");
    assert!(dist(c, down) > 1.0);
}

#[test]
fn limits_hold_an_undriven_joint_at_its_bound() {
    let mut hinge = revolute("hinge", "ground", [0.0; 3], "arm", [0.0; 3]);
    hinge.limits = vec![Some((-0.5, 0.5))];
    let asm = Assembly {
        occurrences: vec![
            occ("ground", true, Placement::IDENTITY),
            occ("arm", false, planar([0.0, 0.0], 1.0)),
        ],
        joints: vec![hinge],
    };
    let s = solve(&asm, &SolveOptions::default()).unwrap();
    assert_solved(&s);
    let angle = joint_coordinates(&asm, &s.placements).unwrap()[0][0];
    assert!((angle - 0.5).abs() <= CLOSE_RAD, "{angle}");
}

#[test]
fn hostile_input_is_an_error_and_never_panics() {
    let good = || Assembly {
        occurrences: vec![
            occ("ground", true, Placement::IDENTITY),
            occ("arm", false, Placement::IDENTITY),
        ],
        joints: vec![revolute("hinge", "ground", [0.0; 3], "arm", [0.0; 3])],
    };
    let check = |asm: Assembly, expected: AssemblyError| {
        assert_eq!(solve(&asm, &SolveOptions::default()), Err(expected));
    };
    check(Assembly::default(), AssemblyError::Empty);
    let mut a = good();
    a.occurrences[1].placement.translation[0] = f64::NAN;
    check(a, AssemblyError::InvalidPlacement { occurrence: 1 });
    let mut a = good();
    a.occurrences[1].placement.rotation[0] = [2.0, 0.0, 0.0];
    check(a, AssemblyError::InvalidPlacement { occurrence: 1 });
    let mut a = good();
    a.joints[0].frame_b.z = [0.0; 3];
    check(a, AssemblyError::DegenerateFrame { joint: 0 });
    let mut a = good();
    a.joints[0].frame_a.origin[1] = f64::INFINITY;
    check(a, AssemblyError::DegenerateFrame { joint: 0 });
    let mut a = good();
    a.joints[0].b = "nowhere".into();
    check(a, AssemblyError::UnknownOccurrence { joint: 0 });
    let mut a = good();
    a.joints[0].b = "ground".into();
    check(a, AssemblyError::SameOccurrence { joint: 0 });
    let mut a = good();
    a.joints[0].values = vec![];
    check(a, AssemblyError::ValueCount { joint: 0 });
    let mut a = good();
    a.joints[0].values = vec![Some(f64::NAN)];
    check(a, AssemblyError::InvalidValue { joint: 0 });
    let mut a = good();
    a.joints[0].limits = vec![Some((1.0, -1.0))];
    check(a, AssemblyError::InvalidValue { joint: 0 });
    let mut a = good();
    a.joints[0].limits = vec![Some((-1.0, 1.0))];
    a.joints[0].values = vec![Some(2.0)];
    check(a, AssemblyError::ValueOutsideLimits { joint: 0 });
    let mut a = good();
    a.occurrences.push(a.occurrences[1].clone());
    check(a, AssemblyError::DuplicateOccurrence { index: 2 });
    let mut a = good();
    a.joints.push(a.joints[0].clone());
    check(a, AssemblyError::DuplicateJoint { index: 1 });
    assert!(Frame::new([0.0; 3], [0.0; 3], [1.0, 0.0, 0.0]).is_err());
    assert!(Frame::new([0.0; 3], [0.0, 0.0, 1.0], [0.0, 0.0, 2.0]).is_err());
    assert!(Placement::from_axis_angle([0.0; 3], 1.0, [0.0; 3]).is_none());
    // A huge finite angle is reduced to one turn, so the placement is a valid rotation.
    let huge = Placement::from_axis_angle([0.0, 0.0, 1.0], 1e308, [0.0; 3]).unwrap();
    assert!(huge.rotation.iter().flatten().all(|v| v.is_finite()));
    let mut b = good();
    b.occurrences[1].placement = huge;
    assert!(solve(&b, &SolveOptions::default()).is_ok());
    // The invalid placement is named by its own index.
    let mut bad = vec![Placement::IDENTITY; 2];
    bad[0].translation[0] = f64::NAN;
    assert_eq!(
        joint_coordinates(&good(), &bad),
        Err(AssemblyError::InvalidPlacement { occurrence: 0 })
    );
    // Huge but finite inputs solve or fail as values; they never panic.
    let mut a = good();
    a.occurrences[1].placement.translation = [1e150, -1e150, 1e150];
    let _ = solve(&a, &SolveOptions::default());
}
