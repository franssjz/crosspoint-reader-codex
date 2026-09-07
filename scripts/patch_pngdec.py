"""PlatformIO pre-build patch: split PNGdec's scanline buffer from the decoder object.

PNGdec 1.1.6 embeds PNG_MAX_BUFFERED_PIXELS directly in PNGIMAGE. With CPR-vCodex's
wide-image setting that makes every PNG object about 59 KB and requires one block of
that size. After leaving a reader the ESP32-C3 can have ample total heap but no block
larger than about 49 KB. The patch keeps the same configured scanline capacity while
allocating it only for decode(), as a second, much smaller block.
"""

Import("env")  # noqa: F821

import os
import sys


HEADER_OLD = "    uint8_t ucPixels[PNG_MAX_BUFFERED_PIXELS];"
HEADER_NEW = """    // CPR-vCodex patch: allocate scanlines separately during decode so the
    // decoder does not require one ~59 KB contiguous heap block.
    uint8_t *ucPixels;
    int iPixelsSize;"""

DECODE_OLD = """int PNG::decode(void *pUser, int iOptions)
{
    return DecodePNG(&_png, pUser, iOptions);
} /* decode() */"""

DECODE_NEW = """int PNG::decode(void *pUser, int iOptions)
{
    // CPR-vCodex patch: the fixed scanline array made PNG itself too large for
    // fragmented ESP32-C3 heap after leaving a reader. Keep the same capacity,
    // but allocate current/previous rows as a separate temporary block.
    const int iScanlineBytes = ((_png.iPitch + 1) * 2) + 32;
    if (iScanlineBytes <= 0 || iScanlineBytes > PNG_MAX_BUFFERED_PIXELS) {
        _png.iError = PNG_TOO_BIG;
        return _png.iError;
    }
    _png.iPixelsSize = iScanlineBytes + ((iOptions & PNG_FAST_PALETTE) ? 512 : 0);
    _png.ucPixels = (uint8_t *)malloc(_png.iPixelsSize);
    if (_png.ucPixels == NULL) {
        _png.iPixelsSize = 0;
        _png.iError = PNG_MEM_ERROR;
        return _png.iError;
    }

    const int rc = DecodePNG(&_png, pUser, iOptions);
    free(_png.ucPixels);
    _png.ucPixels = NULL;
    _png.iPixelsSize = 0;
    return rc;
} /* decode() */"""


def replace_once(path, old, new, label):
    with open(path, "r", encoding="utf-8") as source:
        content = source.read()
    if new in content:
        return False
    if content.count(old) != 1:
        raise RuntimeError("PNGdec %s patch does not match %s" % (label, path))
    content = content.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as destination:
        destination.write(content)
    return True


def patch_pngdec():
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_dir):
        return

    for environment in os.listdir(libdeps_dir):
        source_dir = os.path.join(libdeps_dir, environment, "PNGdec", "src")
        header_path = os.path.join(source_dir, "PNGdec.h")
        cpp_path = os.path.join(source_dir, "PNGdec.cpp")
        inline_path = os.path.join(source_dir, "png.inl")
        if not all(os.path.isfile(path) for path in (header_path, cpp_path, inline_path)):
            continue

        changed = False
        changed |= replace_once(header_path, HEADER_OLD, HEADER_NEW, "header")
        changed |= replace_once(cpp_path, DECODE_OLD, DECODE_NEW, "decode")

        with open(inline_path, "r", encoding="utf-8") as source:
            inline_content = source.read()
        old_size = "sizeof(pPage->ucPixels)-512"
        new_size = "pPage->iPixelsSize-512"
        if old_size in inline_content:
            if inline_content.count(old_size) != 2:
                raise RuntimeError("PNGdec palette patch expected two matches in %s" % inline_path)
            inline_content = inline_content.replace(old_size, new_size)
            with open(inline_path, "w", encoding="utf-8", newline="") as destination:
                destination.write(inline_content)
            changed = True
        elif inline_content.count(new_size) != 2:
            raise RuntimeError("PNGdec palette patch does not match %s" % inline_path)

        if changed:
            print("Applied CPR-vCodex split-allocation patch to PNGdec (%s)" % environment)


try:
    patch_pngdec()
except (OSError, RuntimeError) as error:
    sys.stderr.write("ERROR: %s\n" % error)
    raise SystemExit(1)
