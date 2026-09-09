#pragma once
// MSVC has no GNU packed attribute. These tests construct both representations
// with the same compiler and exercise lookups, never the binary .cpfont loader.
#if defined(_MSC_VER)
#define __attribute__(value)
#endif
