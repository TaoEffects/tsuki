#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
extern const uint8_t g_font_serif_start[];
extern const uint8_t g_font_serif_end[];
extern const uint8_t g_font_serif_it_start[];
extern const uint8_t g_font_serif_it_end[];
extern const uint8_t g_font_sans_start[];
extern const uint8_t g_font_sans_end[];
extern const uint8_t g_font_semi_start[];
extern const uint8_t g_font_semi_end[];
}

namespace assets {

inline const uint8_t *serifData() { return g_font_serif_start; }
inline size_t serifSize() { return static_cast<size_t>(g_font_serif_end - g_font_serif_start); }

inline const uint8_t *serifItalicData() { return g_font_serif_it_start; }
inline size_t serifItalicSize() { return static_cast<size_t>(g_font_serif_it_end - g_font_serif_it_start); }

inline const uint8_t *sansData() { return g_font_sans_start; }
inline size_t sansSize() { return static_cast<size_t>(g_font_sans_end - g_font_sans_start); }

inline const uint8_t *semiData() { return g_font_semi_start; }
inline size_t semiSize() { return static_cast<size_t>(g_font_semi_end - g_font_semi_start); }

}  // namespace assets
