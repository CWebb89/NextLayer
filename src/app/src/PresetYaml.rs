//! The one place a preset value becomes YAML text.
//!
//! Ported from `src/qt/src/PresetYaml.hpp`, which is header-only: four free
//! functions that write the scalar spellings the engine's preset reader accepts.
//! `quoted` and `number` are the format contract for a preset's start and end
//! G-code, its enum spellings and its `x xy` points, so every answer here is the
//! byte string `QString::number(value, 'g', 15)` and the C++ escapes produce —
//! not the shortest text a Rust formatter would prefer.
//!
//! `number` therefore carries its own exact decimal expansion rather than calling
//! `format!`. Three things about `%g` at precision 15 are not what any Rust
//! formatter does: a tie at the sixteenth significant digit rounds away from zero
//! (Rust rounds to even), the exponent is always signed and at least two digits
//! (`1e+20`, `1e-07`), and `-0.0` prints as `0`.

const PRECISION: usize = 15;
const LANE: u64 = 1_000_000_000;

/// One YAML scalar the engine's preset reader accepts. Single-quoted style is
/// used everywhere it can be, which keeps names, enum spellings and `x xy` points
/// verbatim; a multi-line string (start/end G-code) has to switch to the
/// double-quoted style so its newlines survive as escapes.
pub fn quoted(text: &str) -> String {
    let mut escaped = String::with_capacity(text.len() + 2);
    if text.contains('\n') || text.contains('\r') {
        escaped.push('"');
        for character in text.chars() {
            match character {
                '\\' => escaped.push_str("\\\\"),
                '"' => escaped.push_str("\\\""),
                '\n' => escaped.push_str("\\n"),
                '\r' => escaped.push_str("\\r"),
                '\t' => escaped.push_str("\\t"),
                other => escaped.push(other),
            }
        }
        escaped.push('"');
        return escaped;
    }
    escaped.push('\'');
    for character in text.chars() {
        match character {
            '\'' => escaped.push_str("''"),
            other => escaped.push(other),
        }
    }
    escaped.push('\'');
    escaped
}

/// `QString::number(value, 'g', 15)`: fifteen significant decimal digits, trailing
/// zeros dropped, a decimal point in the C locale, and the exponent spelling the
/// engine's reader has always been fed.
pub fn number(value: f64) -> String {
    if value.is_nan() {
        return "nan".to_owned();
    }
    if value.is_infinite() {
        let spelled = if value.is_sign_negative() {
            "-inf"
        } else {
            "inf"
        };
        return spelled.to_owned();
    }
    if value == 0.0 {
        return "0".to_owned();
    }

    let (mut digits, mut point) = exact_digits(value.abs());

    if digits.len() > PRECISION {
        let round_up = digits[PRECISION] >= 5;
        digits.truncate(PRECISION);
        if round_up {
            let mut carry = 1u8;
            for digit in digits.iter_mut().rev() {
                let sum = *digit + carry;
                *digit = sum % 10;
                carry = sum / 10;
                if carry == 0 {
                    break;
                }
            }
            if carry > 0 {
                digits.insert(0, carry);
                point += 1;
                digits.truncate(PRECISION);
            }
        }
    }

    while digits.len() > 1 && digits[digits.len() - 1] == 0 {
        digits.pop();
    }

    let mut text = String::with_capacity(digits.len() + 8);
    if value < 0.0 {
        text.push('-');
    }
    if point > -4 && point <= PRECISION as i32 {
        if point <= 0 {
            text.push_str("0.");
            for _ in 0..-point {
                text.push('0');
            }
            push_digits(&mut text, &digits);
        } else if point as usize >= digits.len() {
            push_digits(&mut text, &digits);
            for _ in digits.len()..point as usize {
                text.push('0');
            }
        } else {
            let split = point as usize;
            push_digits(&mut text, &digits[..split]);
            text.push('.');
            push_digits(&mut text, &digits[split..]);
        }
    } else {
        push_digits(&mut text, &digits[..1]);
        if digits.len() > 1 {
            text.push('.');
            push_digits(&mut text, &digits[1..]);
        }
        let exponent = point - 1;
        text.push('e');
        text.push(if exponent < 0 { '-' } else { '+' });
        let magnitude = exponent.unsigned_abs();
        if magnitude < 10 {
            text.push('0');
        }
        text.push_str(&magnitude.to_string());
    }
    text
}

/// The inverse of `quoted` for one scalar: the catalog writes `id: 'Anycubic'`
/// where the engine's own bundles write `id: Anycubic`, and a comparison that does
/// not strip the quotes silently finds no bundle at all.
pub fn unquoted(text: &str) -> String {
    let value = text.trim();
    if let Some(body) = quoted_body(value, '\'') {
        return body.replace("''", "'");
    }
    if let Some(body) = quoted_body(value, '"') {
        return body.to_owned();
    }
    value.to_owned()
}

/// One `x xy` point as the preset files spell it: the separator is inside the
/// quotes, so a bed shape reads `- '0x0'` and `- '350x0'`.
pub fn point(x: f64, y: f64) -> String {
    quoted(&format!("{}x{}", number(x), number(y)))
}

/// The text between a matched pair of `quote`, or nothing when the value carries
/// only one of them: a lone `'` is left for the caller to return untouched.
fn quoted_body(value: &str, quote: char) -> Option<&str> {
    let body = value.strip_prefix(quote)?;
    body.strip_suffix(quote)
}

/// The exact decimal expansion of a positive finite magnitude: its digits with no
/// leading zero, and how many of them sit before the decimal point.
fn exact_digits(magnitude: f64) -> (Vec<u8>, i32) {
    let bits = magnitude.to_bits();
    let biased = ((bits >> 52) & 0x7ff) as i32;
    let fraction = bits & ((1u64 << 52) - 1);
    let (mantissa, exponent) = if biased == 0 {
        (fraction, -1074)
    } else {
        (fraction | (1u64 << 52), biased - 1075)
    };

    let mut lanes = decimal_lanes(mantissa);
    if exponent >= 0 {
        let mut remaining = exponent.unsigned_abs();
        while remaining >= 29 {
            multiply(&mut lanes, 1u64 << 29);
            remaining -= 29;
        }
        if remaining > 0 {
            multiply(&mut lanes, 1u64 << remaining);
        }
    } else {
        let mut remaining = exponent.unsigned_abs();
        while remaining >= 13 {
            multiply(&mut lanes, 5u64.pow(13));
            remaining -= 13;
        }
        if remaining > 0 {
            multiply(&mut lanes, 5u64.pow(remaining));
        }
    }

    let mut text = String::with_capacity(lanes.len() * 9);
    for lane in lanes.iter().rev() {
        if text.is_empty() {
            text.push_str(&lane.to_string());
        } else {
            text.push_str(&format!("{lane:09}"));
        }
    }
    let digits: Vec<u8> = text.bytes().map(|byte| byte - b'0').collect();
    let point = if exponent >= 0 {
        digits.len() as i32
    } else {
        digits.len() as i32 + exponent
    };
    (digits, point)
}

/// The mantissa as base-10^9 lanes, least significant first.
fn decimal_lanes(mut mantissa: u64) -> Vec<u32> {
    let mut lanes = Vec::with_capacity(3);
    while mantissa > 0 {
        lanes.push((mantissa % LANE) as u32);
        mantissa /= LANE;
    }
    lanes
}

fn multiply(lanes: &mut Vec<u32>, factor: u64) {
    let mut carry = 0u64;
    for lane in lanes.iter_mut() {
        let wide = u64::from(*lane) * factor + carry;
        *lane = (wide % LANE) as u32;
        carry = wide / LANE;
    }
    while carry > 0 {
        lanes.push((carry % LANE) as u32);
        carry /= LANE;
    }
}

fn push_digits(text: &mut String, digits: &[u8]) {
    for digit in digits {
        text.push(char::from(b'0' + digit));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_number_is_fifteen_significant_digits_with_the_zeros_chomped() {
        assert_eq!(number(1.0), "1");
        assert_eq!(number(20.0), "20");
        assert_eq!(number(325.0), "325");
        assert_eq!(number(0.25), "0.25");
        assert_eq!(number(0.95), "0.95");
        assert_eq!(number(1.0 / 3.0), "0.333333333333333");
        assert_eq!(number(0.1 + 0.2), "0.3");
    }

    #[test]
    fn a_number_uses_an_exponent_the_way_the_engine_was_fed_one() {
        assert_eq!(number(0.0001), "0.0001");
        assert_eq!(number(1e-5), "1e-05");
        assert_eq!(number(1e-7), "1e-07");
        assert_eq!(number(1e15), "1e+15");
        assert_eq!(number(1e20), "1e+20");
        assert_eq!(number(1234567890123456.0), "1.23456789012346e+15");
        assert_eq!(number(9007199254740993.0), "9.00719925474099e+15");
        assert_eq!(number(f64::MAX), "1.79769313486232e+308");
        assert_eq!(number(5e-324), "4.94065645841247e-324");
    }

    #[test]
    fn a_number_rounds_a_tie_away_from_zero_like_double_conversion() {
        assert_eq!(number(123456789012344.5), "123456789012345");
        assert_eq!(number(123456789012345.5), "123456789012346");
    }

    #[test]
    fn a_number_prints_zero_negative_zero_and_the_specials() {
        assert_eq!(number(0.0), "0");
        assert_eq!(number(-0.0), "0");
        assert_eq!(number(f64::INFINITY), "inf");
        assert_eq!(number(f64::NEG_INFINITY), "-inf");
        assert_eq!(number(f64::NAN), "nan");
        assert_eq!(number(-0.25), "-0.25");
    }

    #[test]
    fn one_line_is_single_quoted_and_an_apostrophe_is_doubled() {
        assert_eq!(quoted("PETG-CF"), "'PETG-CF'");
        assert_eq!(
            quoted("Voron 2.4 350 (0.6 nozzle)"),
            "'Voron 2.4 350 (0.6 nozzle)'"
        );
        assert_eq!(quoted("it's"), "'it''s'");
        assert_eq!(quoted("''"), "''''''");
        assert_eq!(quoted("a\\b"), "'a\\b'");
        assert_eq!(quoted(""), "''");
    }

    #[test]
    fn a_tab_stays_raw_because_only_a_newline_switches_the_style() {
        assert_eq!(quoted("a\tb"), "'a\tb'");
        assert_eq!(quoted(" leading and trailing "), "' leading and trailing '");
    }

    #[test]
    fn a_newline_switches_to_double_quotes_and_five_escapes() {
        assert_eq!(quoted("PRINT_END\n"), "\"PRINT_END\\n\"");
        assert_eq!(quoted("a\rb"), "\"a\\rb\"");
        assert_eq!(quoted("a\tb\nc"), "\"a\\tb\\nc\"");
        assert_eq!(quoted("a\\b\nc"), "\"a\\\\b\\nc\"");
        assert_eq!(quoted("say \"hi\"\n"), "\"say \\\"hi\\\"\\n\"");
        assert_eq!(
            quoted("M190 S[first_layer_bed_temperature]\nM109 S[first_layer_temperature]\n"),
            "\"M190 S[first_layer_bed_temperature]\\nM109 S[first_layer_temperature]\\n\""
        );
    }

    #[test]
    fn unquoting_strips_one_matched_pair_and_undoes_the_doubling() {
        assert_eq!(unquoted("  'Anycubic'  "), "Anycubic");
        assert_eq!(unquoted("'it''s'"), "it's");
        assert_eq!(unquoted("\"Anycubic\""), "Anycubic");
        assert_eq!(unquoted("Anycubic"), "Anycubic");
        assert_eq!(unquoted("''"), "");
        assert_eq!(unquoted("''''"), "'");
        assert_eq!(unquoted("'"), "'");
        assert_eq!(unquoted("'abc"), "'abc");
        assert_eq!(unquoted("abc'"), "abc'");
        assert_eq!(unquoted("\"abc'"), "\"abc'");
    }

    #[test]
    fn a_point_carries_its_separator_inside_the_quotes() {
        assert_eq!(point(0.0, 0.0), "'0x0'");
        assert_eq!(point(350.0, 0.0), "'350x0'");
        assert_eq!(point(0.5, 0.25), "'0.5x0.25'");
        assert_eq!(point(1e-5, 2.0), "'1e-05x2'");
    }
}
