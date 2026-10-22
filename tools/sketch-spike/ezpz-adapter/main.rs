// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Spike adapter: solve one ezpz problem file with freedom analysis and print one JSON line.
use std::str::FromStr;
use ezpz::textual::Problem;

fn main() {
    let path = std::env::args().nth(1).expect("problem path");
    let text = std::fs::read_to_string(&path).expect("read problem");
    let problem = match Problem::from_str(&text) {
        Ok(p) => p,
        Err(e) => { println!("{{\"ok\":false,\"stage\":\"parse\",\"error\":{:?}}}", e.to_string()); return; }
    };
    let system = match problem.to_constraint_system() {
        Ok(s) => s,
        Err(e) => { println!("{{\"ok\":false,\"stage\":\"system\",\"error\":{:?}}}", e.to_string()); return; }
    };
    let start = std::time::Instant::now();
    let result = system.solve_with_config_analysis(Default::default());
    let first_ms = start.elapsed().as_secs_f64() * 1e3;
    // Median of 21 plain solves for the performance metric.
    let mut times = Vec::new();
    for _ in 0..21 {
        let t = std::time::Instant::now();
        let _ = std::hint::black_box(system.solve());
        times.push(t.elapsed().as_secs_f64() * 1e3);
    }
    times.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let median_ms = times[times.len() / 2];
    match result {
        Ok(out) => {
            let o = &out.outcome;
            let mut points = Vec::new();
            for (label, p) in &o.points {
                points.push(format!("{:?}:[{},{}]", label, p.x, p.y));
            }
            println!(
                "{{\"ok\":true,\"converged\":{},\"iterations\":{},\"unsatisfied\":{},\"num_vars\":{},\"num_eqs\":{},\"underconstrained_vars\":{},\"first_ms\":{},\"median_ms\":{},\"warnings\":{},\"points\":{{{}}}}}",
                o.converged, o.iterations, o.unsatisfied.len(), o.num_vars, o.num_eqs,
                out.analysis.underconstrained().len(), first_ms, median_ms, o.warnings.len(), points.join(",")
            );
        }
        Err(f) => println!(
            "{{\"ok\":false,\"stage\":\"solve\",\"error\":{:?},\"num_vars\":{},\"num_eqs\":{},\"first_ms\":{}}}",
            f.error.to_string(), f.num_vars, f.num_eqs, first_ms
        ),
    }
}
