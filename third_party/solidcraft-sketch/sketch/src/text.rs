// SPDX-FileCopyrightText: Copyright (c) 2026 SolidCraft contributors
// SPDX-License-Identifier: MIT OR Apache-2.0
// Vendored from SolidCraft 6f5d0038abffbd3e1fc26069b1b96ca89984058f; modified by Nightfall's
// third_party/solidcraft-sketch/vendor.py: serde, thiserror, font and outline code removed.
//! Text style data kept for `LinkSource::Text`; the outline builder and its fonts are
//! not vendored (RFC 0012: the fonts are OFL-1.1).

/// Where each line of a text sits against its anchor (`at`, or a point of its path).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum TextAlign {
    /// The line starts at the anchor.
    #[default]
    Left,
    /// The line is centred on it.
    Center,
    /// The line ends at it.
    Right,
}

impl TextAlign {
    fn is_left(&self) -> bool {
        *self == TextAlign::Left
    }
}

/// How a text is set: the style of its font, where its lines sit, the space between its letters
/// and lines, and the sketch curve its baseline follows (instead of the straight line from `at`).
#[derive(Clone, Debug, PartialEq)]
pub struct TextStyle {
    pub bold: bool,
    pub italic: bool,
    pub align: TextAlign,
    /// Extra space between letters (mm; negative brings them closer).
    pub letter_spacing: f64,
    /// The distance between lines as a multiple of the font's own.
    pub line_spacing: f64,
    /// The id of a curve of the same sketch the baseline follows (`at` and the angle then do not
    /// place the text).
    pub path: Option<String>,
    /// Along the path the other way round, so the text sits on its other side.
    pub reverse: bool,
}

impl Default for TextStyle {
    fn default() -> Self {
        TextStyle { bold: false, italic: false, align: TextAlign::Left, letter_spacing: 0.0, line_spacing: 1.0, path: None, reverse: false }
    }
}

impl TextStyle {
    /// Nothing but the defaults (what a text saved before styles had).
    pub fn is_plain(&self) -> bool {
        *self == TextStyle::default()
    }
}

#[allow(dead_code)]
fn is_false(b: &bool) -> bool {
    !*b
}

#[allow(dead_code)]
fn is_zero(x: &f64) -> bool {
    *x == 0.0
}

#[allow(dead_code)]
fn one() -> f64 {
    1.0
}

#[allow(dead_code)]
fn is_one(x: &f64) -> bool {
    *x == 1.0
}
