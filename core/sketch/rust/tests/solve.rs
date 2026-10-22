// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! The RFC 0012 spike corpus as tests, plus the cases the evaluator relies on. Every expected
//! coordinate is analytic; tolerances are explicit.

use nightfall_sketch::{Constraint as C, Entity, Error, Ref, Role, Sketch, Status, solve};
use std::f64::consts::{FRAC_PI_2, PI};

// mm or rad on solved geometry: the vendored solver converges every residual to 1e-9.
const TOL: f64 = 1e-9;

fn point(id: &str, x: f64, y: f64) -> (String, Entity) {
    (id.into(), Entity::Point { at: [x, y] })
}
fn line(id: &str, a: [f64; 2], b: [f64; 2]) -> (String, Entity) {
    (id.into(), Entity::Line { start: a, end: b })
}
fn e(id: &str) -> Ref {
    Ref::entity(id)
}
fn start(id: &str) -> Ref {
    Ref::point(id, Role::Start)
}
fn end(id: &str) -> Ref {
    Ref::point(id, Role::End)
}
fn get<'a>(entities: &'a [(String, Entity)], id: &str) -> &'a Entity {
    entities
        .iter()
        .find(|(n, _)| n == id)
        .map(|(_, e)| e)
        .unwrap_or_else(|| panic!("no entity {id}"))
}
fn near(a: [f64; 2], b: [f64; 2]) -> bool {
    (a[0] - b[0]).abs() <= TOL && (a[1] - b[1]).abs() <= TOL
}
fn line_ends(entities: &[(String, Entity)], id: &str) -> ([f64; 2], [f64; 2]) {
    match get(entities, id) {
        Entity::Line { start, end } => (*start, *end),
        other => panic!("{id} is {other:?}"),
    }
}

/// A closed four-line loop from rough corners, as an agent might sketch it.
fn rough_square() -> Sketch {
    let (a, b, c, d) = ([0.2, -0.1], [9.0, 0.5], [9.5, 9.0], [0.3, 10.2]);
    Sketch {
        entities: vec![
            line("bottom", a, b),
            line("right", b, c),
            line("top", c, d),
            line("left", d, a),
        ],
        constraints: vec![
            C::Coincident(end("bottom"), start("right")),
            C::Coincident(end("right"), start("top")),
            C::Coincident(end("top"), start("left")),
            C::Coincident(end("left"), start("bottom")),
            C::Horizontal(e("bottom")),
            C::Vertical(e("right")),
            C::Horizontal(e("top")),
            C::Vertical(e("left")),
            C::Distance(start("bottom"), end("bottom"), 10.0),
            C::Distance(start("right"), end("right"), 10.0),
        ],
    }
}

#[test]
fn closes_a_square_with_one_corner_free_to_translate() {
    let solved = solve(&rough_square()).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved);
    // Nothing pins the square to the origin: two translational degrees of freedom remain.
    assert_eq!(solved.report.dof, 2);
    assert_eq!(solved.report.underconstrained.len(), 4);
    let (a, b) = line_ends(&solved.entities, "bottom");
    let (_, c) = line_ends(&solved.entities, "right");
    let (_, d) = line_ends(&solved.entities, "top");
    assert!((b[0] - a[0] - 10.0).abs() <= TOL && (b[1] - a[1]).abs() <= TOL);
    assert!((c[1] - b[1] - 10.0).abs() <= TOL && (c[0] - b[0]).abs() <= TOL);
    assert!(near(d, [a[0], a[1] + 10.0]));
    assert!(solved.report.max_residual <= TOL);
}

#[test]
fn a_fixed_corner_removes_the_last_freedom() {
    let mut sketch = rough_square();
    sketch.entities.push(point("origin", 0.0, 0.0));
    sketch.constraints.push(C::Fixed(e("origin")));
    sketch
        .constraints
        .push(C::Coincident(start("bottom"), e("origin")));
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved);
    assert_eq!(solved.report.dof, 0);
    assert!(solved.report.underconstrained.is_empty());
    let (a, b) = line_ends(&solved.entities, "bottom");
    let (_, c) = line_ends(&solved.entities, "right");
    assert!(near(a, [0.0, 0.0]) && near(b, [10.0, 0.0]) && near(c, [10.0, 10.0]));
}

#[test]
fn equal_perpendicular_parallel_and_angle_shape_a_rhombus() {
    // Two lines from a fixed origin, 10 mm and equal, 60° apart.
    let sketch = Sketch {
        entities: vec![
            point("o", 0.0, 0.0),
            line("a", [0.0, 0.0], [9.0, 0.5]),
            line("b", [0.0, 0.0], [4.0, 8.0]),
        ],
        constraints: vec![
            C::Fixed(e("o")),
            C::Coincident(start("a"), e("o")),
            C::Coincident(start("b"), e("o")),
            C::Horizontal(e("a")),
            C::Distance(start("a"), end("a"), 10.0),
            C::Equal(e("a"), e("b")),
            C::Angle(e("a"), e("b"), PI / 3.0),
        ],
    };
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved);
    assert_eq!(solved.report.dof, 0);
    let (_, b_end) = line_ends(&solved.entities, "b");
    assert!(near(b_end, [5.0, 5.0 * 3f64.sqrt()]), "{b_end:?}");
}

#[test]
fn perpendicular_and_parallel_lines() {
    let sketch = Sketch {
        entities: vec![
            line("a", [0.0, 0.0], [0.0, 4.0]),
            line("b", [0.0, 0.0], [3.8, 0.4]),
            line("c", [0.0, 6.0], [3.0, 6.5]),
        ],
        constraints: vec![
            C::Fixed(e("a")),
            C::Coincident(start("b"), start("a")),
            C::Perpendicular(e("a"), e("b")),
            C::Distance(start("b"), end("b"), 4.0),
            C::Parallel(e("b"), e("c")),
        ],
    };
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved);
    let (_, b_end) = line_ends(&solved.entities, "b");
    assert!(near(b_end, [4.0, 0.0]), "{b_end:?}");
    // Parallel is an angular constraint: compare the direction, not end-point coordinates.
    let (c0, c1) = line_ends(&solved.entities, "c");
    let angle = (c1[1] - c0[1]).atan2(c1[0] - c0[0]);
    assert!(angle.abs() <= TOL, "c must be horizontal: {angle} rad");
}

#[test]
fn tangent_circle_radius_and_centre() {
    let sketch = Sketch {
        entities: vec![
            line("ground", [0.0, 3.0], [5.0, 3.0]),
            (
                "wheel".into(),
                Entity::Circle {
                    center: [2.5, 1.4],
                    radius: 2.0,
                },
            ),
            point("axle_x", 2.5, 0.0),
        ],
        constraints: vec![
            C::Fixed(e("ground")),
            C::Radius(e("wheel"), 1.5),
            C::Tangent(e("ground"), e("wheel")),
            C::Fixed(e("axle_x")),
            C::Distance(Ref::point("wheel", Role::Center), e("axle_x"), 1.5),
        ],
    };
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved);
    match get(&solved.entities, "wheel") {
        Entity::Circle { center, radius } => {
            assert!((radius - 1.5).abs() <= TOL);
            assert!(near(*center, [2.5, 1.5]), "{center:?}");
        }
        other => panic!("{other:?}"),
    }
}

#[test]
fn symmetric_midpoint_and_point_line_distance() {
    let sketch = Sketch {
        entities: vec![
            line("axis", [0.0, 0.0], [2.0, 2.0]),
            point("a", 0.5, 0.4),
            point("b", 0.3, 0.6),
            point("m", 0.7, 0.7),
            point("p", 3.0, 0.0),
        ],
        constraints: vec![
            C::Fixed(e("axis")),
            C::Fixed(e("a")),
            C::Symmetric(e("a"), e("b"), e("axis")),
            C::Midpoint(e("axis"), e("m")),
            C::Distance(e("p"), e("axis"), 2f64.sqrt()),
            C::Coincident(e("p"), Ref::entity("axis_ext")),
        ],
    };
    // An unknown entity is an input error that names the constraint.
    assert_eq!(
        solve(&sketch).err(),
        Some(Error::UnknownEntity(5, "axis_ext".into()))
    );
    let mut sketch = sketch;
    sketch.constraints.pop();
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved);
    assert!(matches!(get(&solved.entities, "b"), Entity::Point { at } if near(*at, [0.4, 0.5])));
    assert!(matches!(get(&solved.entities, "m"), Entity::Point { at } if near(*at, [1.0, 1.0])));
    // p stays on its side of the axis, at the requested distance.
    match get(&solved.entities, "p") {
        Entity::Point { at } => assert!(
            ((at[0] - at[1]) / 2f64.sqrt() - 2f64.sqrt()).abs() <= TOL,
            "{at:?}"
        ),
        other => panic!("{other:?}"),
    }
    assert_eq!(solved.report.underconstrained, vec!["p".to_string()]);
}

#[test]
fn arcs_keep_their_direction_and_take_point_on_curve() {
    let sketch = Sketch {
        entities: vec![
            (
                "ccw".into(),
                Entity::Arc {
                    center: [0.0, 0.0],
                    radius: 4.0,
                    start_angle: 0.0,
                    sweep_angle: FRAC_PI_2,
                },
            ),
            (
                "cw".into(),
                Entity::Arc {
                    center: [10.0, 0.0],
                    radius: 2.0,
                    start_angle: PI,
                    sweep_angle: -FRAC_PI_2,
                },
            ),
            point("on", 3.0, 3.0),
        ],
        constraints: vec![
            C::Fixed(Ref::point("ccw", Role::Center)),
            C::Radius(e("ccw"), 5.0),
            C::Coincident(e("on"), e("ccw")),
            C::Fixed(e("cw")),
        ],
    };
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved, "{:?}", solved.report);
    match get(&solved.entities, "cw") {
        Entity::Arc {
            center,
            radius,
            start_angle,
            sweep_angle,
        } => {
            assert!(near(*center, [10.0, 0.0]) && (radius - 2.0).abs() <= TOL);
            assert!((start_angle.abs() - PI).abs() <= TOL, "start {start_angle}");
            assert!(
                (sweep_angle + FRAC_PI_2).abs() <= TOL,
                "clockwise sweep kept: {sweep_angle}"
            );
        }
        other => panic!("{other:?}"),
    }
    match (get(&solved.entities, "ccw"), get(&solved.entities, "on")) {
        (Entity::Arc { radius, .. }, Entity::Point { at }) => {
            assert!((radius - 5.0).abs() <= TOL);
            assert!(
                ((at[0] * at[0] + at[1] * at[1]).sqrt() - 5.0).abs() <= TOL,
                "{at:?}"
            );
        }
        other => panic!("{other:?}"),
    }
}

#[test]
fn contradictory_constraints_fail_and_are_named() {
    let sketch = Sketch {
        entities: vec![point("o", 0.0, 0.0), point("p", 1.0, 1.0)],
        constraints: vec![
            C::Fixed(e("o")),
            C::Distance(e("o"), e("p"), 5.0),
            C::Distance(e("o"), e("p"), 7.0),
        ],
    };
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Failed);
    assert!(
        !solved.report.failing.is_empty()
            && solved.report.failing.iter().all(|i| *i == 1 || *i == 2),
        "{:?}",
        solved.report
    );
    // A failed solve publishes the authored geometry unchanged.
    assert_eq!(
        get(&solved.entities, "p"),
        &Entity::Point { at: [1.0, 1.0] }
    );
}

#[test]
fn redundant_constraints_are_named_and_still_hold() {
    let mut sketch = rough_square();
    sketch.constraints.push(C::Parallel(e("bottom"), e("top")));
    let solved = solve(&sketch).expect("valid input");
    assert_eq!(solved.report.status, Status::Solved);
    assert_eq!(solved.report.dof, 2);
    assert!(!solved.report.redundant.is_empty(), "{:?}", solved.report);
    assert!(solved.report.max_residual <= TOL);
}

#[test]
fn a_dimension_edit_keeps_the_branch() {
    // p sits on a circle of 5 around a fixed origin with x pinned; the authored point is above
    // the axis, and editing x from 3 to 4 must keep it above (y = 3, not -3).
    let build = |x: f64| Sketch {
        entities: vec![
            point("o", 0.0, 0.0),
            point("p", 3.0, 4.0),
            line("guide", [x, -10.0], [x, 10.0]),
        ],
        constraints: vec![
            C::Fixed(e("o")),
            C::Fixed(e("guide")),
            C::Coincident(e("p"), e("guide")),
            C::Distance(e("o"), e("p"), 5.0),
        ],
    };
    for (x, y) in [(3.0, 4.0), (4.0, 3.0)] {
        let solved = solve(&build(x)).expect("valid input");
        assert_eq!(solved.report.status, Status::Solved);
        assert!(
            matches!(get(&solved.entities, "p"), Entity::Point { at } if near(*at, [x, y])),
            "x = {x}"
        );
    }
}

#[test]
fn hostile_input_is_an_error_never_a_panic() {
    let nan = Sketch {
        entities: vec![point("p", f64::NAN, 0.0)],
        constraints: vec![],
    };
    assert!(matches!(solve(&nan), Err(Error::InvalidValue { .. })));
    let dup = Sketch {
        entities: vec![point("p", 0.0, 0.0), point("p", 1.0, 0.0)],
        constraints: vec![],
    };
    assert_eq!(solve(&dup).err(), Some(Error::DuplicateId("p".into())));
    let zero = Sketch {
        entities: vec![(
            "c".into(),
            Entity::Circle {
                center: [0.0, 0.0],
                radius: 0.0,
            },
        )],
        constraints: vec![],
    };
    assert!(matches!(solve(&zero), Err(Error::InvalidValue { .. })));
    let full_turn = Sketch {
        entities: vec![(
            "a".into(),
            Entity::Arc {
                center: [0.0, 0.0],
                radius: 1.0,
                start_angle: 0.0,
                sweep_angle: 2.0 * PI,
            },
        )],
        constraints: vec![],
    };
    assert!(matches!(solve(&full_turn), Err(Error::InvalidValue { .. })));
    let wrong_kind = Sketch {
        entities: vec![point("p", 0.0, 0.0)],
        constraints: vec![C::Horizontal(e("p"))],
    };
    assert!(matches!(solve(&wrong_kind), Err(Error::Unsupported(0, _))));
    let role = Sketch {
        entities: vec![line("l", [0.0, 0.0], [1.0, 0.0])],
        constraints: vec![C::Fixed(Ref::point("l", Role::Center))],
    };
    assert!(matches!(solve(&role), Err(Error::Unsupported(0, _))));
    let bad_value = Sketch {
        entities: vec![point("a", 0.0, 0.0), point("b", 1.0, 0.0)],
        constraints: vec![C::Distance(e("a"), e("b"), f64::INFINITY)],
    };
    assert!(matches!(solve(&bad_value), Err(Error::InvalidValue { .. })));
    let degenerate = Sketch {
        entities: vec![line("l", [1.0, 1.0], [1.0, 1.0])],
        constraints: vec![],
    };
    assert!(solve(&degenerate).is_err());
    assert_eq!(solve(&Sketch::default()).map(|s| s.report.dof), Ok(0));
}

#[test]
fn two_hundred_constraints_solve_well_inside_a_frame() {
    // The spike's strip of 40 squares sharing edges: 203 constraints, fully determined.
    let n = 40;
    let mut entities = vec![point("anchor", 0.0, 0.0)];
    let mut constraints = vec![C::Fixed(e("anchor"))];
    for i in 0..=n {
        let x = 10.0 * i as f64 + 0.3 * ((i * 7 % 3) as f64 - 1.0);
        entities.push(line(&format!("v{i}"), [x, 0.2], [x + 0.1, 9.8]));
        constraints.push(C::Vertical(e(&format!("v{i}"))));
        constraints.push(C::Distance(
            start(&format!("v{i}")),
            end(&format!("v{i}")),
            10.0,
        ));
    }
    constraints.push(C::Coincident(start("v0"), e("anchor")));
    for i in 0..n {
        for (name, role) in [("h", Role::Start), ("t", Role::End)] {
            let id = format!("{name}{i}");
            entities.push(line(
                &id,
                [10.0 * i as f64, 0.0],
                [10.0 * (i + 1) as f64, 0.0],
            ));
            constraints.push(C::Horizontal(e(&id)));
            constraints.push(C::Coincident(
                start(&id),
                Ref::point(&format!("v{i}"), role),
            ));
            constraints.push(C::Coincident(
                end(&id),
                Ref::point(&format!("v{}", i + 1), role),
            ));
        }
        constraints.push(C::Distance(
            start(&format!("v{i}")),
            start(&format!("v{}", i + 1)),
            10.0,
        ));
    }
    assert!(constraints.len() >= 200);
    let started = std::time::Instant::now();
    let solved = solve(&Sketch {
        entities,
        constraints,
    })
    .expect("valid input");
    let elapsed = started.elapsed();
    assert_eq!(solved.report.status, Status::Solved);
    assert_eq!(solved.report.dof, 0);
    let (_, top_last) = line_ends(&solved.entities, &format!("v{n}"));
    assert!(near(top_last, [400.0, 10.0]), "{top_last:?}");
    // Generous for debug builds; release measured 0.41 ms in the RFC 0012 spike.
    assert!(elapsed.as_secs_f64() < 0.5, "{elapsed:?}");
}
