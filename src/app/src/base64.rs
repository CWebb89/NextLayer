//! Standard base64, because two file formats here need it and the crate has no
//! dependency for it: a plate thumbnail line and the mesh payload of a project
//! file. Alphabet and padding are the RFC 4648 ones, which is what `QByteArray`'s
//! `toBase64` and every host that reads these files use.

const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// `data` as padded base64.
pub fn encode(data: &[u8]) -> String {
    let mut out = String::with_capacity(data.len().div_ceil(3) * 4);
    for chunk in data.chunks(3) {
        let triple = (u32::from(chunk[0]) << 16)
            | (u32::from(chunk.get(1).copied().unwrap_or(0)) << 8)
            | u32::from(chunk.get(2).copied().unwrap_or(0));
        out.push(char::from(ALPHABET[(triple >> 18) as usize & 63]));
        out.push(char::from(ALPHABET[(triple >> 12) as usize & 63]));
        out.push(match chunk.len() {
            1 => '=',
            _ => char::from(ALPHABET[(triple >> 6) as usize & 63]),
        });
        out.push(match chunk.len() {
            3 => char::from(ALPHABET[triple as usize & 63]),
            _ => '=',
        });
    }
    out
}

/// The bytes `text` carries, or `None` when it is not base64.
///
/// Whitespace is ignored, because a wrapped payload is still the same payload.
pub fn decode(text: &str) -> Option<Vec<u8>> {
    let mut out = Vec::with_capacity(text.len() / 4 * 3);
    let mut accumulator = 0u32;
    let mut bits = 0u32;
    let mut padding = false;

    for byte in text.bytes() {
        if byte.is_ascii_whitespace() {
            continue;
        }
        if byte == b'=' {
            padding = true;
            continue;
        }
        if padding {
            return None;
        }
        let value = match byte {
            b'A'..=b'Z' => byte - b'A',
            b'a'..=b'z' => byte - b'a' + 26,
            b'0'..=b'9' => byte - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            _ => return None,
        };
        accumulator = (accumulator << 6) | u32::from(value);
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((accumulator >> bits) as u8);
            accumulator &= (1 << bits) - 1;
        }
    }

    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_alphabet_is_the_standard_one() {
        assert_eq!(encode(b""), "");
        assert_eq!(encode(b"f"), "Zg==");
        assert_eq!(encode(b"fo"), "Zm8=");
        assert_eq!(encode(b"foo"), "Zm9v");
        assert_eq!(encode(b"foob"), "Zm9vYg==");
        assert_eq!(encode(b"fooba"), "Zm9vYmE=");
        assert_eq!(encode(b"foobar"), "Zm9vYmFy");
        assert_eq!(encode(&[0xff, 0xfe, 0xfd]), "//79");
    }

    #[test]
    fn every_byte_survives_a_round_trip() {
        let all: Vec<u8> = (0..=255).collect();
        assert_eq!(decode(&encode(&all)).unwrap(), all);
    }

    #[test]
    fn a_wrapped_payload_decodes_to_the_same_bytes() {
        let wrapped = "Zm9v\nYmFy\n";
        assert_eq!(decode(wrapped).unwrap(), b"foobar");
    }

    #[test]
    fn something_that_is_not_base64_is_refused() {
        assert_eq!(decode("Zm9v!"), None);
        assert_eq!(decode("Zg=A"), None);
    }
}
