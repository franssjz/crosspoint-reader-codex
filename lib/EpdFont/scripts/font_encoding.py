"""Lossless encodings shared by the generator and existing-header optimizer."""
import re
import zlib


def deflate_raw(data, use_zopfli=False):
    data = bytes(data)
    compressor = zlib.compressobj(level=9, wbits=-15)
    result = compressor.compress(data) + compressor.flush()
    if use_zopfli:
        import zopfli.zlib
        candidate = zopfli.zlib.compress(data)[2:-4]
        if zlib.decompress(candidate, -15) != data:
            raise ValueError("Zopfli round-trip failed")
        # Small groups can be smaller with zlib; never grow them needlessly.
        if len(candidate) < len(result):
            result = candidate
    if zlib.decompress(result, -15) != data:
        raise ValueError("DEFLATE round-trip failed")
    return result


def sparse_kerning(matrix, left_count, right_count):
    if not 0 < left_count <= 255 or not 0 < right_count <= 255:
        raise ValueError("Kerning class counts must fit uint8_t")
    if len(matrix) != left_count * right_count:
        raise ValueError("Kerning matrix dimensions do not match")
    offsets, columns, values = [0], [], []
    for row in range(left_count):
        for col in range(right_count):
            value = matrix[row * right_count + col]
            if not -128 <= value <= 127:
                raise ValueError("Kerning adjustment must fit int8_t")
            if value:
                columns.append(col)
                values.append(value)
        offsets.append(len(columns))
    if len(columns) > 65535:
        raise ValueError("Sparse kerning offsets must fit uint16_t")
    return offsets, columns, values


def array(kind, name, values):
    lines = [f"static const {kind} {name}[] = {{"]
    for start in range(0, len(values), 16):
        lines.append("    " + ", ".join(str(v) for v in values[start:start + 16]) + ",")
    lines.append("};\n")
    return "\n".join(lines)


def bitmap_array(name, values):
    # Retain the generator's explicit byte count and hex representation, also
    # consumed by verify_compression.py and font tooling outside this script.
    lines = [f"static const uint8_t {name}[{len(values)}] = {{"]
    for start in range(0, len(values), 16):
        lines.append("    " + " ".join(f"0x{v:02X}," for v in values[start:start + 16]))
    return "\n".join(lines) + "\n};"


def kern_arrays(name, left, right, matrix, left_count, right_count):
    offsets, columns, values = sparse_kerning(matrix, left_count, right_count)
    # Dense is preferable for an unusually full matrix. Split maps still avoid
    # unaligned packed loads, and lookup supports both representations.
    sparse = bool(values) and 2 * (len(offsets) + len(values)) < len(matrix)
    result = []
    for side, entries in (("Left", left), ("Right", right)):
        if any(cp > 65535 or not 1 <= cls <= 255 for cp, cls in entries):
            raise ValueError("Class map entry exceeds its field range")
        result.append(array("uint16_t", name + "Kern" + side + "Codepoints", [cp for cp, _ in entries]))
        result.append(array("uint8_t", name + "Kern" + side + "ClassIds", [cls for _, cls in entries]))
    if sparse:
        result.append(array("uint16_t", name + "KernRowOffsets", offsets))
        result.append(array("uint8_t", name + "KernSparseCols", columns))
        result.append(array("int8_t", name + "KernSparseValues", values))
    else:
        result.append(array("int8_t", name + "KernMatrix", matrix))
    return "\n".join(result), sparse


def numbers(text):
    text = re.sub(r"//[^\n]*", "", text)
    return [int(value, 0) for value in re.findall(r"-?0x[\da-fA-F]+|-?\d+", text)]


def optimize_header(text, use_zopfli=False):
    """Re-encode an existing generated header without rasterizing any glyph."""
    font = re.search(r"static const EpdFontData (\w+) = \{(.*?)\n\};", text, re.S)
    if not font:
        return text, 0
    name = font.group(1)
    fields = [part.strip() for part in re.sub(r"//[^\n]*", "", font.group(2)).split(",") if part.strip()]
    original_fields = fields[:]
    saved = 0

    def find_array(suffix):
        return re.search(r"static const \w+ " + re.escape(name + suffix) + r"\[[^\]]*\] = \{(.*?)\n\};", text, re.S)

    groups = find_array("Groups")
    if use_zopfli and groups:
        bitmap = find_array("Bitmaps")
        old_bitmap = bytes(numbers(bitmap.group(1)))
        groups_values = numbers(groups.group(1))
        new_bitmap, new_groups = bytearray(), []
        if len(groups_values) % 5:
            raise ValueError("Invalid compressed group table")
        for i in range(0, len(groups_values), 5):
            offset, size, expanded, count, first = groups_values[i:i + 5]
            old = old_bitmap[offset:offset + size]
            raw = zlib.decompress(old, -15)
            if len(raw) != expanded:
                raise ValueError("Compressed group size does not match")
            encoded = deflate_raw(raw, True)
            if len(encoded) > len(old):
                encoded = old
            new_groups.append(f"    {{ {len(new_bitmap)}, {len(encoded)}, {expanded}, {count}, {first} }},")
            new_bitmap.extend(encoded)
        saved += len(old_bitmap) - len(new_bitmap)
        new_bitmap_text = bitmap_array(name + "Bitmaps", new_bitmap)
        new_groups_text = f"static const EpdFontGroup {name}Groups[] = {{\n" + "\n".join(new_groups) + "\n};"
        text = text.replace(bitmap.group(0), new_bitmap_text).replace(groups.group(0), new_groups_text)

    left, right, matrix = find_array("KernLeftClasses"), find_array("KernRightClasses"), find_array("KernMatrix")
    if left and right and matrix:
        left_values, right_values, dense = numbers(left.group(1)), numbers(right.group(1)), numbers(matrix.group(1))
        left_entries = list(zip(left_values[::2], left_values[1::2]))
        right_entries = list(zip(right_values[::2], right_values[1::2]))
        left_count, right_count = int(fields[16]), int(fields[17])
        emitted, sparse = kern_arrays(name, left_entries, right_entries, dense, left_count, right_count)
        fields[11:14] = ["nullptr", "nullptr", "nullptr" if sparse else name + "KernMatrix"]
        fields += ["nullptr"] * (23 - len(fields))  # Preserve optional on-demand callbacks.
        fields += [name + "KernLeftCodepoints", name + "KernLeftClassIds", name + "KernRightCodepoints",
                   name + "KernRightClassIds"]
        fields += ([name + "KernRowOffsets", name + "KernSparseCols", name + "KernSparseValues"]
                   if sparse else ["nullptr"] * 3)
        if sparse:
            offsets, columns, values = sparse_kerning(dense, left_count, right_count)
            restored = [0] * len(dense)
            for row in range(left_count):
                for entry in range(offsets[row], offsets[row + 1]):
                    restored[row * right_count + columns[entry]] = values[entry]
            if restored != dense:
                raise ValueError("Sparse kerning round-trip failed")
            saved += len(dense) - 2 * (len(offsets) + len(values))
        # Seven additional pointers are emitted per generated font on the C3.
        saved -= 7 * 4
        text = text.replace(matrix.group(0), "").replace(right.group(0), "").replace(left.group(0), emitted.rstrip())

    if fields != original_fields:
        new_font = f"static const EpdFontData {name} = {{\n" + "".join(f"    {v},\n" for v in fields) + "};"
        text = text.replace(font.group(0), new_font)
    return text, saved
