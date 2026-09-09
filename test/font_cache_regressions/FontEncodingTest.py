"""Semantic compatibility of all shipped CPR fonts and header encoding."""
import hashlib
import json
from pathlib import Path
import re
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lib/EpdFont/scripts"))
from font_encoding import deflate_raw, kern_arrays, numbers, optimize_header, sparse_kerning


def semantic_digest(text):
    declaration = re.search(r"static const EpdFontData (\w+) = \{(.*?)\n\};", text, re.S)
    name = declaration.group(1)

    def data(suffix):
        found = re.search(r"static const \w+ " + re.escape(name + suffix) + r"\[[^\]]*\] = \{(.*?)\n\};", text, re.S)
        return numbers(found.group(1)) if found else None

    fields = [v.strip() for v in re.sub(r"//[^\n]*", "", declaration.group(2)).split(",") if v.strip()]
    bitmaps = bytes(data("Bitmaps"))
    groups = data("Groups")
    decompressed, group_metrics = [], []
    if groups:
        for index in range(0, len(groups), 5):
            offset, length, expanded, count, first = groups[index:index + 5]
            decoded = zlib.decompress(bitmaps[offset:offset + length], -15)
            if len(decoded) != expanded:
                raise ValueError("Expanded bitmap length mismatch")
            decompressed.append(decoded.hex())
            group_metrics.append([expanded, count, first])
    else:
        decompressed.append(bitmaps.hex())

    left, right = data("KernLeftClasses"), data("KernRightClasses")
    if left is None and data("KernLeftCodepoints") is not None:
        left = [v for pair in zip(data("KernLeftCodepoints"), data("KernLeftClassIds")) for v in pair]
        right = [v for pair in zip(data("KernRightCodepoints"), data("KernRightClassIds")) for v in pair]
    matrix = data("KernMatrix")
    if matrix is None and data("KernRowOffsets") is not None:
        lc, rc = int(fields[16]), int(fields[17])
        offsets, columns, values = data("KernRowOffsets"), data("KernSparseCols"), data("KernSparseValues")
        matrix = [0] * (lc * rc)
        if len(offsets) != lc + 1 or offsets[-1] != len(columns) or len(columns) != len(values):
            raise ValueError("Invalid sparse kerning lengths")
        for row in range(lc):
            if offsets[row] > offsets[row + 1]:
                raise ValueError("Non-monotonic sparse row offsets")
            previous = -1
            for i in range(offsets[row], offsets[row + 1]):
                if not previous < columns[i] < rc:
                    raise ValueError("Invalid sparse column order")
                previous = columns[i]
                matrix[row * rc + columns[i]] = values[i]
    canonical = [fields[:11], fields[14:20], data("Glyphs"), data("Intervals"), data("GlyphToGroup"),
                 left, right, matrix, data("LigaturePairs"), group_metrics, decompressed]
    return hashlib.sha256(json.dumps(canonical, separators=(",", ":")).encode()).hexdigest()


class FontEncodingTest(unittest.TestCase):
    def test_all_cpr_fonts_keep_original_pixels_metrics_and_kerning(self):
        baseline = json.loads((Path(__file__).parent / "builtin_semantics.json").read_text())
        self.assertEqual(len(baseline), 77)
        for filename, expected in baseline.items():
            with self.subTest(font=filename):
                text = (ROOT / "lib/EpdFont/builtinFonts" / filename).read_text(encoding="utf-8")
                self.assertEqual(semantic_digest(text), expected)

    def test_sparse_roundtrip_including_empty_rows_and_int8_extremes(self):
        matrix = [-128, 0, 127, 0, 0, 0, 0, -7, 0]
        self.assertEqual(sparse_kerning(matrix, 3, 3), ([0, 2, 2, 3], [0, 2, 1], [-128, 127, -7]))

    def test_invalid_dimensions_or_narrow_field_overflow_are_rejected(self):
        for matrix, left, right in (([0], 256, 1), ([0], 1, 256), ([0], 2, 2), ([128], 1, 1)):
            with self.subTest(left=left, right=right):
                with self.assertRaises(ValueError):
                    sparse_kerning(matrix, left, right)

    def test_dense_form_retained_if_sparse_would_grow(self):
        emitted, sparse = kern_arrays("tiny", [(65, 1)], [(97, 1)], [-7], 1, 1)
        self.assertFalse(sparse)
        self.assertIn("tinyKernMatrix", emitted)
        self.assertNotIn("KernSparse", emitted)

    def test_raw_deflate_roundtrip_and_header_optimizer_idempotent(self):
        for data in (b"", b"abc", bytes(range(256)) * 17):
            self.assertEqual(zlib.decompress(deflate_raw(data), -15), data)
        text = (ROOT / "lib/EpdFont/builtinFonts/bookerly_12_regular.h").read_text(encoding="utf-8")
        self.assertEqual(optimize_header(text)[0], text)


if __name__ == "__main__":
    unittest.main()
