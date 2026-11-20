//! `; thumbnail` blocks in an exported G-code file.
//!
//! Ported from `src/qt/src/PlateThumbnail.cpp`. Mainsail, Fluidd, PrusaLink and
//! OctoPrint read these blocks to show a preview of a print without slicing it, so
//! the shape is theirs, not ours: a `; thumbnail begin WxH <bytes>` line, the PNG
//! as base64 wrapped at 78 characters, and a `; thumbnail end` line. The blocks go
//! at the very top of the file, which is where the reference slicers put them and
//! what a host that stops reading after the first kilobyte needs.
//!
//! The image work stays in the C++ adapter, because a `QImage` is Qt's; this file
//! takes the encoded PNGs and owns the file rewrite.

use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Write};
use std::path::{Path, PathBuf};

/// The two edges Qt is asked to render at, in the order the blocks are written.
const EDGES: [u32; 2] = [512, 32];

/// How much base64 one `; ` line carries.
const LINE_CHARS: usize = 78;

/// The sizes the caller has to encode at, in the order [`embed_plate_thumbnails`]
/// expects them.
pub fn edges() -> Vec<u32> {
    EDGES.to_vec()
}

/// Replaces any `; thumbnail` block in `path` with the two given PNGs, written at
/// the top of the file.
///
/// Returns an empty string on success, or why the file could not be rewritten.
pub fn embed_plate_thumbnails(path: &str, png_primary: &[u8], png_secondary: &[u8]) -> String {
    let mut blocks = Vec::new();
    for (edge, png) in EDGES.iter().zip([png_primary, png_secondary]) {
        if png.is_empty() {
            return "the plate render could not be encoded as PNG".to_string();
        }
        push_block(&mut blocks, *edge, png);
    }

    match rewrite(path, &blocks) {
        Ok(()) => String::new(),
        Err(message) => message,
    }
}

fn push_block(out: &mut Vec<u8>, edge: u32, png: &[u8]) {
    let encoded = crate::base64::encode(png);
    out.extend_from_slice(format!("; thumbnail begin {edge}x{edge} {}\n", png.len()).as_bytes());
    for chunk in encoded.as_bytes().chunks(LINE_CHARS) {
        out.extend_from_slice(b"; ");
        out.extend_from_slice(chunk);
        out.extend_from_slice(b"\n");
    }
    out.extend_from_slice(b"; thumbnail end\n");
}

/// Streams the file into a sibling staging file, blocks first and any existing
/// thumbnail block dropped, then renames it over the original.
///
/// Streamed rather than read whole: a day-long print is a few hundred megabytes,
/// and holding it in memory twice to insert twenty kilobytes at the top is how a
/// thumbnail addition runs a machine out of RAM.
fn rewrite(path: &str, blocks: &[u8]) -> Result<(), String> {
    let source = Path::new(path);
    let read_failed = || format!("cannot read {path}");
    let write_failed = || format!("cannot write {path}");

    let input = File::open(source).map_err(|_| read_failed())?;
    let staging = staging_path(source);
    let rewritten = stream_copy(BufReader::new(input), &staging, blocks, path);

    match rewritten {
        Ok(()) => fs::rename(&staging, source).map_err(|_| {
            let _ = fs::remove_file(&staging);
            write_failed()
        }),
        Err(message) => {
            let _ = fs::remove_file(&staging);
            Err(message)
        }
    }
}

fn stream_copy(
    mut input: BufReader<File>,
    staging: &Path,
    blocks: &[u8],
    path: &str,
) -> Result<(), String> {
    let read_failed = || format!("cannot read {path}");
    let write_failed = || format!("cannot write {path}");
    let mut output = BufWriter::new(File::create(staging).map_err(|_| write_failed())?);
    output.write_all(blocks).map_err(|_| write_failed())?;

    let mut line = Vec::new();
    let mut inside_thumbnail = false;
    loop {
        line.clear();
        if std::io::BufRead::read_until(&mut input, b'\n', &mut line).map_err(|_| read_failed())? == 0
        {
            break;
        }
        if line.starts_with(b"; thumbnail begin") {
            inside_thumbnail = true;
            continue;
        }
        if inside_thumbnail {
            if line.starts_with(b"; thumbnail end") {
                inside_thumbnail = false;
            }
            continue;
        }
        output.write_all(&line).map_err(|_| write_failed())?;
    }

    output.flush().map_err(|_| write_failed())?;
    output
        .into_inner()
        .map_err(|_| write_failed())?
        .sync_all()
        .map_err(|_| write_failed())
}

fn staging_path(source: &Path) -> PathBuf {
    let mut staged = source.as_os_str().to_owned();
    staged.push(".thumbnails");
    PathBuf::from(staged)
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Scratch {
        path: PathBuf,
    }

    impl Scratch {
        fn new(name: &str, body: &str) -> Self {
            let path = std::env::temp_dir().join(format!(
                "next-layer-thumbnail-{}-{name}.gcode",
                std::process::id()
            ));
            std::fs::write(&path, body).unwrap();
            Self { path }
        }

        fn text(&self) -> String {
            std::fs::read_to_string(&self.path).unwrap()
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = std::fs::remove_file(&self.path);
        }
    }

    #[test]
    fn blocks_go_at_the_top_and_the_body_is_untouched() {
        let file = Scratch::new("top", "; generated by next-layer\nG28\nG1 X10 E1\n");
        let error = embed_plate_thumbnails(file.path.to_str().unwrap(), b"big", b"small");
        assert_eq!(error, "");

        let text = file.text();
        assert!(text.starts_with("; thumbnail begin 512x512 3\n"));
        assert!(text.contains("\n; thumbnail begin 32x32 5\n"));
        assert!(text.ends_with(
            "; thumbnail end\n; generated by next-layer\nG28\nG1 X10 E1\n"
        ));
    }

    #[test]
    fn a_second_embed_replaces_rather_than_appends() {
        let file = Scratch::new("idempotent", "; generated by next-layer\nG28\n");
        let path = file.path.to_str().unwrap().to_string();
        embed_plate_thumbnails(&path, b"first", b"first");
        let once = file.text();
        embed_plate_thumbnails(&path, b"first", b"first");
        assert_eq!(file.text(), once);
        assert_eq!(once.matches("; thumbnail begin").count(), 2);
    }

    #[test]
    fn a_missing_file_is_reported() {
        let missing = std::env::temp_dir().join("next-layer-thumbnail-absent-9.gcode");
        let message = embed_plate_thumbnails(missing.to_str().unwrap(), b"big", b"small");
        assert_eq!(message, format!("cannot read {}", missing.display()));
        assert!(!missing.exists());
    }

    #[test]
    fn an_empty_png_is_refused_before_the_file_is_touched() {
        let file = Scratch::new("empty", "; generated by next-layer\n");
        let error = embed_plate_thumbnails(file.path.to_str().unwrap(), b"", b"small");
        assert_eq!(error, "the plate render could not be encoded as PNG");
        assert_eq!(file.text(), "; generated by next-layer\n");
    }

    #[test]
    fn the_longest_line_is_the_wrap_width() {
        let file = Scratch::new("wrap", "");
        embed_plate_thumbnails(file.path.to_str().unwrap(), &[0u8; 96], b"small");
        let longest = file
            .text()
            .lines()
            .filter(|line| line.starts_with("; ") && !line.starts_with("; thumbnail"))
            .map(str::len)
            .max()
            .unwrap();
        assert_eq!(longest, 2 + LINE_CHARS);
    }
}
