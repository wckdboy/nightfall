// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Spike adapter: read the ezpz textual subset used by the Nightfall corpus, build the same
// problem with SolidCraft's sketch API, solve with full analysis and print one JSON line.
use std::collections::HashMap;
use solidcraft_geom::Vec2;
use solidcraft_sketch::{ConstraintKind, CurveKind, Sketch, SolveStatus, solve};

struct Builder {
    sk: Sketch,
    points: HashMap<String, usize>,
    circles: HashMap<String, usize>,
    lines: HashMap<(usize, usize), usize>,
}

fn num(s: &str) -> Result<f64, String> { s.trim().parse::<f64>().map_err(|_| format!("bad number {s:?}")) }
fn args(s: &str) -> Vec<String> {
    let inner = &s[s.find('(').unwrap_or(0) + 1..s.rfind(')').unwrap_or(s.len())];
    inner.split(',').map(|a| a.trim().to_string()).collect()
}
fn pair(s: &str) -> Result<Vec2, String> {
    let a = args(s);
    if a.len() != 2 { return Err(format!("expected (x, y) in {s:?}")); }
    Ok(Vec2::new(num(&a[0])?, num(&a[1])?))
}

impl Builder {
    fn point(&self, name: &str) -> Result<usize, String> {
        if let Some((circle, rest)) = name.split_once('.') {
            if rest == "center" {
                let ci = *self.circles.get(circle).ok_or(format!("unknown circle {circle}"))?;
                if let CurveKind::Circle { c, .. } = self.sk.curves[ci].kind { return Ok(c); }
            }
        }
        self.points.get(name).copied().ok_or(format!("unknown point {name}"))
    }
    fn line(&mut self, a: &str, b: &str) -> Result<usize, String> {
        let (pa, pb) = (self.point(a)?, self.point(b)?);
        if let Some(l) = self.lines.get(&(pa, pb)).or(self.lines.get(&(pb, pa))) { return Ok(*l); }
        let l = self.sk.add_line_pts(pa, pb, None).map_err(|e| e.to_string())?;
        self.lines.insert((pa, pb), l);
        Ok(l)
    }
    fn anchor(&mut self, at: Vec2) -> Result<usize, String> {
        let p = self.sk.add_point(at, None).map_err(|e| e.to_string())?;
        self.c(ConstraintKind::Fix { p })?;
        Ok(p)
    }
    fn c(&mut self, k: ConstraintKind) -> Result<(), String> {
        self.sk.add_constraint(k, None).map(|_| ()).map_err(|e| e.to_string())
    }
}

fn build(text: &str) -> Result<Builder, String> {
    let (cons, guesses) = text.split_once("# guesses").ok_or("no guesses section")?;
    let mut guess: HashMap<String, Vec2> = HashMap::new();
    let mut radius_guess: HashMap<String, f64> = HashMap::new();
    for line in guesses.lines().map(str::trim).filter(|l| !l.is_empty()) {
        let (name, value) = line.split_once(" roughly ").ok_or(format!("bad guess {line:?}"))?;
        if let Some(c) = name.strip_suffix(".radius") { radius_guess.insert(c.into(), num(value)?); }
        else { guess.insert(name.trim().into(), pair(value)?); }
    }
    let mut b = Builder { sk: Sketch::new(), points: HashMap::new(), circles: HashMap::new(), lines: HashMap::new() };
    let body: Vec<&str> = cons.lines().map(str::trim).filter(|l| !l.is_empty() && !l.starts_with('#')).collect();
    for line in &body {
        if let Some(name) = line.strip_prefix("point ") {
            let at = guess.get(name).copied().unwrap_or(Vec2::new(0.0, 0.0));
            let p = b.sk.add_point(at, None).map_err(|e| e.to_string())?;
            b.points.insert(name.into(), p);
        } else if let Some(name) = line.strip_prefix("circle ") {
            let at = guess.get(&format!("{name}.center")).copied().unwrap_or(Vec2::new(0.0, 0.0));
            let r = radius_guess.get(name).copied().unwrap_or(1.0);
            let ci = b.sk.add_circle(at, r, None, None).map_err(|e| e.to_string())?;
            b.circles.insert(name.into(), ci);
        }
    }
    for line in &body {
        if line.starts_with("point ") || line.starts_with("circle ") { continue; }
        let a = args(line);
        let head = line.split('(').next().unwrap_or("").trim();
        match head {
            "line" => { b.line(&a[0], &a[1])?; }
            "horizontal" => { let (p, q) = (b.point(&a[0])?, b.point(&a[1])?); b.c(ConstraintKind::HorizontalPoints { p, q })?; }
            "vertical" => { let (p, q) = (b.point(&a[0])?, b.point(&a[1])?); b.c(ConstraintKind::VerticalPoints { p, q })?; }
            "distance" => { let (p, q) = (b.point(&a[0])?, b.point(&a[1])?); b.c(ConstraintKind::Distance { p, q, value: num(&a[2])? })?; }
            "lines_equal_length" => { let l1 = b.line(&a[0], &a[1])?; let l2 = b.line(&a[2], &a[3])?; b.c(ConstraintKind::Equal { a: l1, b: l2 })?; }
            "parallel" => { let l1 = b.line(&a[0], &a[1])?; let l2 = b.line(&a[2], &a[3])?; b.c(ConstraintKind::Parallel { a: l1, b: l2 })?; }
            "perpendicular" => { let l1 = b.line(&a[0], &a[1])?; let l2 = b.line(&a[2], &a[3])?; b.c(ConstraintKind::Perpendicular { a: l1, b: l2 })?; }
            "lines_at_angle" => {
                let l1 = b.line(&a[0], &a[1])?; let l2 = b.line(&a[2], &a[3])?;
                let v = a[4].trim();
                let value = if let Some(d) = v.strip_suffix("deg") { num(d)?.to_radians() } else { num(v.trim_end_matches("rad"))? };
                b.c(ConstraintKind::Angle { a: l1, b: l2, value, flip: false })?;
            }
            "midpoint" => { let l = b.line(&a[0], &a[1])?; let m = b.point(&a[2])?; b.c(ConstraintKind::Midpoint { p: m, l })?; }
            "symmetric" => { let l = b.line(&a[0], &a[1])?; let (p, q) = (b.point(&a[2])?, b.point(&a[3])?); b.c(ConstraintKind::Symmetric { p, q, l })?; }
            "radius" => { let c = *b.circles.get(&a[0]).ok_or("unknown circle")?; b.c(ConstraintKind::Radius { c, value: num(&a[1])? })?; }
            "tangent" => { let l = b.line(&a[0], &a[1])?; let c = *b.circles.get(&a[2]).ok_or("unknown circle")?; b.c(ConstraintKind::Tangent { a: l, b: c })?; }
            _ => {
                let (lhs, rhs) = line.split_once('=').ok_or(format!("unsupported {line:?}"))?;
                let lhs = lhs.trim();
                if let Some(name) = lhs.strip_suffix(".x") {
                    let p = b.point(name)?; let anchor = b.anchor(Vec2::new(num(rhs)?, 0.0))?;
                    b.c(ConstraintKind::VerticalPoints { p, q: anchor })?;
                } else if let Some(name) = lhs.strip_suffix(".y") {
                    let p = b.point(name)?; let anchor = b.anchor(Vec2::new(0.0, num(rhs)?))?;
                    b.c(ConstraintKind::HorizontalPoints { p, q: anchor })?;
                } else {
                    let p = b.point(lhs)?; let anchor = b.anchor(pair(rhs.trim())?)?;
                    b.c(ConstraintKind::Coincident { p, q: anchor })?;
                }
            }
        }
    }
    Ok(b)
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_default();
    let text = std::fs::read_to_string(&path).unwrap_or_default();
    let b = match build(&text) {
        Ok(b) => b,
        Err(e) => { println!("{{\"ok\":false,\"stage\":\"build\",\"error\":{e:?}}}"); return; }
    };
    let mut sk = b.sk.clone();
    let t = std::time::Instant::now();
    let report = solve(&mut sk);
    let first_ms = t.elapsed().as_secs_f64() * 1e3;
    let mut times = Vec::new();
    for _ in 0..21 {
        let mut copy = b.sk.clone();
        let t = std::time::Instant::now();
        let _ = std::hint::black_box(solve(&mut copy));
        times.push(t.elapsed().as_secs_f64() * 1e3);
    }
    times.sort_by(|a, b| a.total_cmp(b));
    let mut pts = Vec::new();
    for (name, i) in &b.points {
        if let Some(p) = sk.point(*i) { pts.push(format!("{name:?}:[{},{}]", p.x, p.y)); }
    }
    let mut circles = Vec::new();
    for (name, ci) in &b.circles {
        if let (Some(c), Some(r)) = (sk.center(*ci), sk.radius(*ci)) {
            circles.push(format!("{name:?}:{{\"center\":[{},{}],\"radius\":{}}}", c.x, c.y, r));
        }
    }
    println!(
        "{{\"ok\":{},\"dof\":{},\"iterations\":{},\"max_residual\":{},\"failing\":{},\"redundant\":{},\"folded\":{},\"first_ms\":{},\"median_ms\":{},\"points\":{{{}}},\"circles\":{{{}}}}}",
        matches!(report.status, SolveStatus::Solved), report.dof, report.iterations,
        if report.max_residual.is_finite() { report.max_residual } else { -1.0 },
        report.failing.len(), report.redundant.len(), report.folded.len(),
        first_ms, times[times.len() / 2], pts.join(","), circles.join(",")
    );
}
