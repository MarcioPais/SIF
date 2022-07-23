/*
SIF - Simple Image Format

- Based on ideas from QOI by Dominic Szablewski - https://phoboslab.org

Copyright (c) 2022 Marcio Pais

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef SIF_H
#define SIF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* size_t */
#include <stdint.h> /* fixed width integer types */
#include <stdbool.h>

typedef uint32_t ULEB128_t;

typedef struct {
  ULEB128_t width;
  ULEB128_t height;
  uint8_t channels;
  uint8_t flags;
} SIF_content_descriptor_t;

void* SIF_compressImage(const SIF_content_descriptor_t* const image, const void* const src, size_t const srcSize, uint64_t* outSize);

void* SIF_decompressImage(SIF_content_descriptor_t* const image, const void* const src, size_t const srcSize, uint64_t* outSize);

#ifndef SIF_NO_STDIO

uint64_t SIF_write(const char* const filename, const void* const data, size_t const srcSize, const SIF_content_descriptor_t* const descriptor);

void* SIF_read(const char* const filename, SIF_content_descriptor_t* const descriptor, uint64_t* outSize);

#endif /* SIF_NO_STDIO */

#ifdef __cplusplus
}
#endif
#endif /* SIF_H */

#ifdef SIF_IMPLEMENTATION
#include <stdlib.h>
#include <string.h> /* memset */

#ifndef SIF_NO_INLINE
#  if defined(__cplusplus) || (defined(__GNUC__) && !defined(__STRICT_ANSI__) /* non-ANSI C */) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L /* C99 */)
#    define SIF_INLINE inline
#  else
#    define SIF_INLINE
#  endif
#  if defined(_MSC_VER)
#    define SIF_FORCE_INLINE __forceinline
#  elif defined(__GNUC__) || defined(__has_attribute)
#    if defined(__GNUC__) || __has_attribute(always_inline)
#      define SIF_FORCE_INLINE inline  __attribute__((always_inline))
#    else
#      define SIF_FORCE_INLINE
#    endif
#  else
#    define SIF_FORCE_INLINE
#  endif
#else
#  define SIF_INLINE
#  define SIF_FORCE_INLINE
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SIF_LIKELY(x) (__builtin_expect(!!(x), 1))
#  define SIF_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#  define SIF_LIKELY(x) (x)
#  define SIF_UNLIKELY(x) (x)
#endif

#ifndef SIF_ASSERT
#  include <assert.h>
#  define SIF_ASSERT(x) assert(x)
#endif

#ifndef SIF_MALLOC
#  define SIF_MALLOC(size) malloc(size)
#  define SIF_FREE(pointer) free(pointer)
#endif

#define SIF_MAGIC_NUMBER 0x51F0u
#define SIF_END_OF_SLICE_MARKER ((SIF_end_of_slice_marker_t)(0))
#define SIF_MAX_DIMENSION_BIT_LENGTH 29u
#define SIF_MAX_DIMENSION (((ULEB128_t)(1u)) << SIF_MAX_DIMENSION_BIT_LENGTH)
#define SIF_MINIMUM_SLICE_SIZE (sizeof(uint32_t) /*size*/ + 2u * sizeof(uint8_t) /*flags & height*/ + sizeof(SIF_end_of_slice_marker_t))
#define SIF_MINIMUM_IMAGE_SIZE (sizeof(uint16_t) /*magic*/ + 2u * sizeof(uint8_t) /*width & height*/ + SIF_MINIMUM_SLICE_SIZE)

#define SIF_TILE_WIDTH 16u
#define SIF_TILE_HEIGHT_DEFAULT_EXPONENT 4u

#define SIF_FLAGS_MASK_TILE_HEIGHT     0x03u
#define SIF_FLAGS_MASK_PREDICTOR_ID    0x0Cu
#define SIF_FLAGS_MASK_2D_PREDICTOR    0x10u
#define SIF_FLAGS_MASK_CONTEXTUAL_DICT 0x20u
#define SIF_FLAGS_MASK_DELTA_BIAS      0xC0u

#define SIF_FLAGS_SHIFT_TILE_HEIGHT     0u
#define SIF_FLAGS_SHIFT_PREDICTOR_ID    2u
#define SIF_FLAGS_SHIFT_2D_PREDICTOR    4u
#define SIF_FLAGS_SHIFT_CONTEXTUAL_DICT 5u
#define SIF_FLAGS_SHIFT_DELTA_BIAS      6u

#define SIF_RUN_MINIMUM_LENGTH 2u
#define SIF_RUN_CACHE_SIZE 4096u
#define SIF_REDUCED_OFFSET_BIT_LENGTH 6u
#define SIF_DICT_CONTEXT_BIT_LENGTH 5u
#define SIF_DICT_NUM_OF_BUCKETS (1u << SIF_DICT_CONTEXT_BIT_LENGTH)
#define SIF_DICT_ITEMS_PER_BUCKET (1u << SIF_REDUCED_OFFSET_BIT_LENGTH)

#define SIF_SLD_WND_MASK (SIF_TILE_WIDTH * 2u - 1u)

#define SIF_OPCODE_MASK(x) ((0xFFu << (8u - (x))) & 0xFFu)

typedef enum {
  SIF_opcode_delta_15b            = 0x00, /* 0xxx xxxx */
  SIF_opcode_reduced_offset       = 0x80, /* 10xx xxxx */
  SIF_opcode_run_delta_8b         = 0xC0, /* 110x xxxx */
  SIF_opcode_delta_20b            = 0xE0, /* 1110 xxxx */
  SIF_opcode_3chn_mask_delta_8bpc = 0xF0, /* 1111 0xxx */
  SIF_opcode_3chn_run_delta0      = 0xF8  /* 1111 1xxx */
} SIF_opcodes;

typedef enum {
  SIF_predictor_direct                 = 0,
  SIF_predictor_decorrelate_from_red   = 1,
  SIF_predictor_decorrelate_from_green = 2,
  SIF_predictor_decorrelate_from_blue  = 3,
} SIF_predictors;

typedef enum {
  SIF_delta_red_bias   = 0,
  SIF_delta_green_bias = 1,
  SIF_delta_blue_bias  = 2
} SIF_delta_configuration;

typedef union {
  struct { uint8_t r, g, b, a; } rgba;
  struct { int8_t r, g, b, a; } delta;
  uint32_t value;
} SIF_pixel_t;

typedef struct {
  uint16_t magic;
  ULEB128_t width;
  ULEB128_t height;
} SIF_file_header_t;

typedef struct {
  uint32_t size; /* little-endian */
  uint8_t flags;
  ULEB128_t height;
} SIF_slice_header_t;

typedef uint32_t SIF_end_of_slice_marker_t;

SIF_FORCE_INLINE uint32_t SIF_pixelHash(SIF_pixel_t const pixel) {
  return ((pixel.value * 0x9E3779B9u) >> (29u - SIF_REDUCED_OFFSET_BIT_LENGTH)) & (SIF_DICT_ITEMS_PER_BUCKET - 1u);
}

SIF_FORCE_INLINE bool SIF_checkRange(int8_t const value, int8_t const range) {
  SIF_ASSERT(range >= 0);
  return (value >= -range) && (value < range);
}

ULEB128_t SIF_readULEB128(const void* const src, size_t* const position, size_t const size) {
  SIF_ASSERT(src != NULL);
  SIF_ASSERT(position != NULL);
  SIF_ASSERT(*position < size);
  const uint8_t* const src_ = (const uint8_t* const)src;
  size_t offset = 0u;
  ULEB128_t r = 0u;
  int k = 0;
  uint8_t B;
  do {
    B = src_[*position + offset++];
    r |= ((offset < sizeof(ULEB128_t)) ? B & 0x7Fu : B) << k;
    k += 7u;
  } while (((B & 0x80u) > 0u) && (offset < sizeof(ULEB128_t)) && (*position + offset < size));
  *position += offset;
  return r;
}

size_t SIF_writeULEB128(void* const dst, ULEB128_t value) {
  SIF_ASSERT(dst != NULL);
  uint8_t* const dst_ = (uint8_t* const)dst;
  size_t offset = 0u;
  while ((value > 0x7Fu) && (offset < sizeof(ULEB128_t) - 1u)) {
    dst_[offset++] = (uint8_t)(0x80u | (value & 0x7Fu));
    value >>= 7u;
  }
  dst_[offset] = (uint8_t)(value & ((offset < sizeof(ULEB128_t) - 1u) ? 0x7Fu : 0xFFu));
  offset++;
  return offset;
}

size_t SIF_encodeRun(uint8_t* const dst, const uint8_t* const deltas, uint32_t* run, uint32_t* run0) {
  SIF_ASSERT(dst != NULL);
  SIF_ASSERT(deltas != NULL);
  SIF_ASSERT(run != NULL);
  SIF_ASSERT(run0 != NULL);
  SIF_ASSERT(*run >= *run0);
  size_t position = 0u, offset = 0u, zeros = 0u;
  if ((*run0 > 1u) && (*run0 <= 32u)) {
    *run -= *run0;
    offset = (size_t)*run0;
    do {
      uint32_t const r = (*run0 > 7u) ? 7u : *run0 - 1u;
      dst[position++] = (uint8_t)(SIF_opcode_3chn_run_delta0 | r);
      *run0 -= r + 1u;
    } while (*run0 > 0u);
  }
  if (*run > 0u) {
    size_t const len = (*run)--;
    dst[position++] = SIF_opcode_run_delta_8b | ((*run > 0xFu) ? 0x10u : 0u) | (*run & 0xFu);
    *run >>= 4u;
    if (*run > 0) {
      SIF_ASSERT(*run <= 0xFFu);
      dst[position++] = (uint8_t)*run;
      *run = 0u;
    }
    for (size_t i = 0u; i < len; i++) {
      uint8_t const B = deltas[offset + i];
      if (B > 0u) {
#define SIF_OUTPUT_ZERO_RUN \
        while (*run > 0u) { \
          dst[position++] = 0u; \
          (*run)--; \
          if (++zeros == SIF_RUN_MINIMUM_LENGTH) { \
            uint32_t const r = (*run > 0xFFu) ? 0xFFu : *run; \
            dst[position++] = (uint8_t)r; \
            *run -= r; \
            zeros = 0u; \
          } \
        }
        SIF_OUTPUT_ZERO_RUN
        zeros = 0u;
        dst[position++] = B;
      }
      else
        (*run)++;
    }
    SIF_OUTPUT_ZERO_RUN
#undef SIF_OUTPUT_ZERO_RUN
  }
  SIF_ASSERT(*run == 0u);
  *run0 = 0u;
  return position;
}

SIF_INLINE uint64_t SIF_compressSliceBound(const SIF_content_descriptor_t* const slice) {
  SIF_ASSERT(slice != NULL);
  return ((uint64_t)slice->width) * ((uint64_t)slice->height) * ((uint64_t)slice->channels + 1u) + sizeof(SIF_end_of_slice_marker_t);
}

size_t SIF_compressSlice(const SIF_content_descriptor_t* const slice, void* const dst, size_t const dstCapacity, const void* const src, size_t const srcSize) {
  SIF_ASSERT(slice != NULL);
  SIF_ASSERT((slice->width > 0u) && (slice->width <= SIF_MAX_DIMENSION));
  SIF_ASSERT((slice->height > 0u) && (slice->height <= SIF_MAX_DIMENSION));
  SIF_ASSERT(slice->channels == 3u);
  SIF_ASSERT(SIF_compressSliceBound(slice) <= (uint64_t)dstCapacity);
  SIF_ASSERT(dst != NULL);
  SIF_ASSERT(src != NULL);
  SIF_ASSERT(srcSize >= ((size_t)slice->width * slice->height * slice->channels));

  uint8_t run_cache[SIF_RUN_CACHE_SIZE] = { 0 };
  SIF_pixel_t dict[SIF_DICT_NUM_OF_BUCKETS * SIF_DICT_ITEMS_PER_BUCKET] = { 0 };
  SIF_pixel_t sld_wnd[SIF_TILE_WIDTH * 2u] = { 0 };
  const uint8_t* const src_ = (const uint8_t* const)src;
  uint8_t* const dst_ = (uint8_t* const)dst;

  size_t const predictor_id = (slice->flags & SIF_FLAGS_MASK_PREDICTOR_ID) >> SIF_FLAGS_SHIFT_PREDICTOR_ID;
  SIF_pixel_t prediction = { 0 }, prev_pixel = { 0 }, pixel = { 0 }, range_8b, run_mask, range_20b, mask_20b;
  switch ((slice->flags & SIF_FLAGS_MASK_DELTA_BIAS) >> SIF_FLAGS_SHIFT_DELTA_BIAS) {
    case SIF_delta_red_bias:
    default: {
      range_8b.delta.r = 2;
      range_8b.delta.g = 4;
      range_8b.delta.b = 4;
      break;
    }
    case SIF_delta_green_bias: {
      range_8b.delta.r = 4;
      range_8b.delta.g = 2;
      range_8b.delta.b = 4;
      break;
    }
    case SIF_delta_blue_bias: {
      range_8b.delta.r = 4;
      range_8b.delta.g = 4;
      range_8b.delta.b = 2;
      break;
    }
  }
  range_20b.value = range_8b.value << 4u;
  run_mask.delta.r = (range_8b.delta.r << 1) - 1;
  run_mask.delta.g = (range_8b.delta.g << 1) - 1;
  run_mask.delta.b = (range_8b.delta.b << 1) - 1;
  mask_20b.value = (run_mask.value << 4u) | (0x0F0F0Fu);
  int const run_shift_g = 2 + (range_8b.delta.b > 2), run_shift_r = run_shift_g + 2 + (range_8b.delta.g > 2);
  int const delta_20b_shift_g = run_shift_g + 4, delta_20b_shift_r = run_shift_r + 8;

  bool const use_contextual_dict = (slice->flags & SIF_FLAGS_MASK_CONTEXTUAL_DICT) >> SIF_FLAGS_SHIFT_CONTEXTUAL_DICT;
  bool const use_2d_prediction = (predictor_id != SIF_predictor_direct) && (((slice->flags & SIF_FLAGS_MASK_2D_PREDICTOR) >> SIF_FLAGS_SHIFT_2D_PREDICTOR) != 0u);

  size_t position = 0u;
  uint32_t run = 0u, run0 = 0u, sld_offset = 0u;

  size_t const SIF_tile_height = (1u << (SIF_TILE_HEIGHT_DEFAULT_EXPONENT + ((slice->flags & SIF_FLAGS_MASK_TILE_HEIGHT) << SIF_FLAGS_SHIFT_TILE_HEIGHT))) - 1u;
  size_t const grid_width_in_tiles = (slice->width + (SIF_TILE_WIDTH - 1u)) / SIF_TILE_WIDTH;
  size_t const grid_height_in_tiles = (slice->height + (SIF_tile_height - 1u)) / SIF_tile_height;
  size_t const remaining_columns = slice->width - (grid_width_in_tiles - 1u) * SIF_TILE_WIDTH;
  size_t const remaining_lines = slice->height - (grid_height_in_tiles - 1u) * SIF_tile_height;
  size_t const channels = slice->channels;
  size_t const stride = slice->width * channels;
  size_t const tile_stride = SIF_tile_height * stride;
  size_t const tile_inner_stride = SIF_TILE_WIDTH * channels;
  size_t const last_pixel = (slice->width * slice->height - ((remaining_lines & 1u) ? 1u : remaining_columns)) * channels;

  for (size_t tile_y = 0u, tile_initial_line = 0u; tile_y < grid_height_in_tiles; tile_y++, tile_initial_line += tile_stride) {
    size_t const pixels_v = (tile_y < grid_height_in_tiles - 1u) ? SIF_tile_height : remaining_lines;
    for (size_t i = 0u; i < grid_width_in_tiles; i++) {
      size_t const tile_x = (tile_y & 1u) ? (grid_width_in_tiles - 1u) - i : i;
      size_t const pixels_h = (tile_x < grid_width_in_tiles - 1u) ? SIF_TILE_WIDTH : remaining_columns;
      size_t const tile_initial_offset = tile_initial_line + (tile_x * tile_inner_stride);
      bool const tile_x_odd = (tile_x & 1u);
      for (size_t y = 0u; y < pixels_v; y++) {
        size_t const y_ = (tile_x_odd) ? pixels_v - 1u - y : y;
        size_t const offset = tile_initial_offset + (y_ * stride);
        bool const right_to_left = ((tile_y ^ y_) & 1u);
        for (size_t x = 0u; x < pixels_h; x++) {
          size_t const x_ = (right_to_left) ? pixels_h - 1u - x : x;
          size_t const pixel_pos = offset + (x_ * channels);

          pixel.rgba.r = src_[pixel_pos + 0u];
          pixel.rgba.g = src_[pixel_pos + 1u];
          pixel.rgba.b = src_[pixel_pos + 2u];

          prediction = prev_pixel;
          switch (predictor_id) {
            case SIF_predictor_direct:
            default: {
              break;
            }
            case SIF_predictor_decorrelate_from_red: {
              if (use_2d_prediction && (y > 0))
                prediction.rgba.r = (prediction.rgba.r * 7u + sld_wnd[(sld_offset - (x << 1u) - 1u) & SIF_SLD_WND_MASK].rgba.r) >> 3u;
              int8_t const delta = pixel.rgba.r - prediction.rgba.r;
              prediction.rgba.g += delta;
              prediction.rgba.b += delta;
              break;
            }
            case SIF_predictor_decorrelate_from_green: {
              if (use_2d_prediction && (y > 0))
                prediction.rgba.g = (prediction.rgba.g * 7u + sld_wnd[(sld_offset - (x << 1u) - 1u) & SIF_SLD_WND_MASK].rgba.g) >> 3u;
              int8_t const delta = pixel.rgba.g - prediction.rgba.g;
              prediction.rgba.r += delta;
              prediction.rgba.b += delta;
              break;
            }
            case SIF_predictor_decorrelate_from_blue: {
              if (use_2d_prediction && (y > 0))
                prediction.rgba.b = (prediction.rgba.b * 7u + sld_wnd[(sld_offset - (x << 1u) - 1u) & SIF_SLD_WND_MASK].rgba.b) >> 3u;
              int8_t const delta = pixel.rgba.b - prediction.rgba.b;
              prediction.rgba.r += delta;
              prediction.rgba.g += delta;
              break;
            }
          }
          prediction.delta.r = pixel.rgba.r - prediction.rgba.r;
          prediction.delta.g = pixel.rgba.g - prediction.rgba.g;
          prediction.delta.b = pixel.rgba.b - prediction.rgba.b;

          int const similar = SIF_checkRange(prediction.delta.r, range_8b.delta.r) && SIF_checkRange(prediction.delta.g, range_8b.delta.g) && SIF_checkRange(prediction.delta.b, range_8b.delta.b);
          if (similar) {
            uint8_t const delta = ((prediction.delta.r & run_mask.delta.r) << run_shift_r) | ((prediction.delta.g & run_mask.delta.g) << run_shift_g) | (prediction.delta.b & run_mask.delta.b);
            if ((run == run0) && (delta == 0u))
              run0++;
            run_cache[run++] = delta;
            if (SIF_UNLIKELY((run == SIF_RUN_CACHE_SIZE) || (pixel_pos == last_pixel)))
              position += SIF_encodeRun(&dst_[position], &run_cache[0u], &run, &run0);
          }
          else {
            if (run > 0u)
              position += SIF_encodeRun(&dst_[position], &run_cache[0u], &run, &run0);
            size_t offset = (size_t)SIF_pixelHash(pixel);
            if (use_contextual_dict)
              offset |= ((prev_pixel.rgba.r + prev_pixel.rgba.g) >> (9u - SIF_DICT_CONTEXT_BIT_LENGTH)) << SIF_REDUCED_OFFSET_BIT_LENGTH;
            SIF_ASSERT(offset < sizeof(dict));
            if (dict[offset].value == pixel.value)
              dst_[position++] = SIF_opcode_reduced_offset | (offset & (SIF_DICT_ITEMS_PER_BUCKET - 1u));
            else {
              dict[offset] = pixel;
              if (SIF_checkRange(prediction.delta.r, 16) && SIF_checkRange(prediction.delta.g, 16) && SIF_checkRange(prediction.delta.b, 16)) {
                uint32_t const value = (SIF_opcode_delta_15b << 8u) | ((prediction.delta.r & 0x1Fu) << 10u) | ((prediction.delta.g & 0x1Fu) << 5u) | (prediction.delta.b & 0x1Fu);
                dst_[position++] = (uint8_t)(value >> 8u);
                dst_[position++] = (uint8_t)(value);
              }
              else if (
                SIF_checkRange(prediction.delta.r, range_20b.delta.r) &&
                SIF_checkRange(prediction.delta.g, range_20b.delta.g) &&
                SIF_checkRange(prediction.delta.b, range_20b.delta.b) &&
                (((prediction.delta.r != 0) + (prediction.delta.g != 0) + (prediction.delta.b != 0)) > 1)
              ) {
                uint32_t const value = (SIF_opcode_delta_20b << 16u) | ((prediction.delta.r & mask_20b.delta.r) << delta_20b_shift_r) | ((prediction.delta.g & mask_20b.delta.g) << delta_20b_shift_g) | (prediction.delta.b & mask_20b.delta.b);
                dst_[position++] = (uint8_t)(value >> 16u);
                dst_[position++] = (uint8_t)(value >>  8u);
                dst_[position++] = (uint8_t)(value);
              }
              else {
                uint8_t* const B = &dst_[position++], C = SIF_opcode_3chn_mask_delta_8bpc;
                if (prediction.delta.r != 0) { dst_[position++] = (uint8_t)prediction.delta.r; C |= 0x04u; }
                if (prediction.delta.g != 0) { dst_[position++] = (uint8_t)prediction.delta.g; C |= 0x02u; }
                if (prediction.delta.b != 0) { dst_[position++] = (uint8_t)prediction.delta.b; C |= 0x01u; }
                SIF_ASSERT((C & 0x07u) > 0u);
                *B = C;
              }
            }
          }
          prev_pixel = pixel;
          if (use_2d_prediction)
            sld_wnd[sld_offset++ & SIF_SLD_WND_MASK] = pixel;
        }
      }
    }
  }
  if (run > 0)
    position += SIF_encodeRun(&dst_[position], &run_cache[0u], &run, &run0);
  *((SIF_end_of_slice_marker_t*)&dst_[position]) = SIF_END_OF_SLICE_MARKER;
  return position += sizeof(SIF_end_of_slice_marker_t);
}

size_t SIF_decompressSlice(const SIF_content_descriptor_t* const slice, void* const dst, size_t const dstCapacity, const void* const src, size_t const srcSize) {
  SIF_ASSERT(slice != NULL);
  SIF_ASSERT((slice->width > 0u) && (slice->width <= SIF_MAX_DIMENSION));
  SIF_ASSERT((slice->height > 0u) && (slice->height <= SIF_MAX_DIMENSION));
  SIF_ASSERT(slice->channels == 3u);
  SIF_ASSERT(dst != NULL);
  SIF_ASSERT(src != NULL);
  SIF_ASSERT(dstCapacity >= ((size_t)slice->width * slice->height * slice->channels));
  SIF_ASSERT(srcSize > sizeof(SIF_end_of_slice_marker_t));

  uint8_t run_cache[SIF_RUN_CACHE_SIZE] = { 0 };
  SIF_pixel_t dict[SIF_DICT_NUM_OF_BUCKETS * SIF_DICT_ITEMS_PER_BUCKET] = { 0 };
  SIF_pixel_t sld_wnd[SIF_TILE_WIDTH * 2u] = { 0 };
  const uint8_t* const src_ = (const uint8_t* const)src;
  uint8_t* const dst_ = (uint8_t* const)dst;
  size_t const srcEnd = srcSize - sizeof(SIF_end_of_slice_marker_t);

  size_t const predictor_id = (slice->flags & SIF_FLAGS_MASK_PREDICTOR_ID) >> SIF_FLAGS_SHIFT_PREDICTOR_ID;
  SIF_pixel_t prediction = { 0 }, prev_pixel = { 0 }, pixel = { 0 }, range_8b, run_mask, mask_20b;
  switch ((slice->flags & SIF_FLAGS_MASK_DELTA_BIAS) >> SIF_FLAGS_SHIFT_DELTA_BIAS) {
    case SIF_delta_red_bias:
    default: {
      range_8b.delta.r = 2;
      range_8b.delta.g = 4;
      range_8b.delta.b = 4;
      break;
    }
    case SIF_delta_green_bias: {
      range_8b.delta.r = 4;
      range_8b.delta.g = 2;
      range_8b.delta.b = 4;
      break;
    }
    case SIF_delta_blue_bias: {
      range_8b.delta.r = 4;
      range_8b.delta.g = 4;
      range_8b.delta.b = 2;
      break;
    }
  }
  run_mask.delta.r = (range_8b.delta.r << 1) - 1;
  run_mask.delta.g = (range_8b.delta.g << 1) - 1;
  run_mask.delta.b = (range_8b.delta.b << 1) - 1;
  mask_20b.value = (run_mask.value << 4u) | (0x0F0F0Fu);
  int const run_shift_g = 2 + (range_8b.delta.b > 2), run_shift_r = run_shift_g + 2 + (range_8b.delta.g > 2);
  int const delta_20b_shift_g = run_shift_g + 4, delta_20b_shift_r = run_shift_r + 8;

  bool const use_contextual_dict = (slice->flags & SIF_FLAGS_MASK_CONTEXTUAL_DICT) >> SIF_FLAGS_SHIFT_CONTEXTUAL_DICT;
  bool const use_2d_prediction = (predictor_id != SIF_predictor_direct) && (((slice->flags & SIF_FLAGS_MASK_2D_PREDICTOR) >> SIF_FLAGS_SHIFT_2D_PREDICTOR) != 0u);

  size_t position = 0u, cache_index = 0u;
  uint32_t run = 0u, run0 = 0u, sld_offset = 0u;

  size_t const SIF_tile_height = (1u << (SIF_TILE_HEIGHT_DEFAULT_EXPONENT + ((slice->flags & SIF_FLAGS_MASK_TILE_HEIGHT) << SIF_FLAGS_SHIFT_TILE_HEIGHT))) - 1u;
  size_t const grid_width_in_tiles = (slice->width + (SIF_TILE_WIDTH - 1u)) / SIF_TILE_WIDTH;
  size_t const grid_height_in_tiles = (slice->height + (SIF_tile_height - 1u)) / SIF_tile_height;
  size_t const remaining_columns = slice->width - (grid_width_in_tiles - 1u) * SIF_TILE_WIDTH;
  size_t const remaining_lines = slice->height - (grid_height_in_tiles - 1u) * SIF_tile_height;
  size_t const channels = slice->channels;
  size_t const stride = slice->width * channels;
  size_t const tile_stride = SIF_tile_height * stride;
  size_t const tile_inner_stride = SIF_TILE_WIDTH * channels;

  for (size_t tile_y = 0u, tile_initial_line = 0u; tile_y < grid_height_in_tiles; tile_y++, tile_initial_line += tile_stride) {
    size_t const pixels_v = (tile_y < grid_height_in_tiles - 1u) ? SIF_tile_height : remaining_lines;
    for (size_t i = 0u; i < grid_width_in_tiles; i++) {
      size_t const tile_x = (tile_y & 1u) ? (grid_width_in_tiles - 1u) - i : i;
      size_t const pixels_h = (tile_x < grid_width_in_tiles - 1u) ? SIF_TILE_WIDTH : remaining_columns;
      size_t const tile_initial_offset = tile_initial_line + (tile_x * tile_inner_stride);
      bool const tile_x_odd = (tile_x & 1u);
      for (size_t y = 0u; y < pixels_v; y++) {
        size_t const y_ = (tile_x_odd) ? pixels_v - 1u - y : y;
        size_t const offset = tile_initial_offset + (y_ * stride);
        bool const right_to_left = ((tile_y ^ y_) & 1u);
        for (size_t x = 0u; x < pixels_h; x++) {
          size_t const x_ = (right_to_left) ? pixels_h - 1u - x : x;
          size_t const pixel_pos = offset + (x_ * channels);

          bool must_add_to_dict = false;
          if (run0 > 0) {
            pixel.value = 0u;
            run0--;
          }
          else if (run > 0) {
            uint8_t const delta = run_cache[cache_index++];
            pixel.delta.r = (((int8_t)(delta & (run_mask.delta.r << run_shift_r))) >> run_shift_r);
            pixel.delta.g = (((int8_t)(((delta >> run_shift_g) & run_mask.delta.g) << (8 - run_shift_r + run_shift_g))) >> (8 - run_shift_r + run_shift_g));
            pixel.delta.b = (((int8_t)((delta & run_mask.delta.b) << (8 - run_shift_g))) >> (8 - run_shift_g));
            run--;
          }
          else if (position < srcEnd) {
            uint8_t op = src_[position++];
            if ((op & SIF_OPCODE_MASK(3)) == SIF_opcode_run_delta_8b) {
              run = op & (~SIF_OPCODE_MASK(3));
              if (run > 0xFu) {
                run &= 0xFu;
                if (position < srcEnd)
                  run |= src_[position++] << 4u;
              }
              run++;
              SIF_ASSERT(run <= SIF_RUN_CACHE_SIZE);
              cache_index = 0u;
              while ((position < srcEnd) && (cache_index < run)) {
                uint8_t const B = src_[position++];
                run_cache[cache_index++] = B;
                run0 = (B > 0u) ? 0u : run0 + 1u;
                if ((run0 == SIF_RUN_MINIMUM_LENGTH) && (position < srcEnd)) {
                  run0 = src_[position++];
                  while ((cache_index < run) && (run0 > 0u)) {
                    run_cache[cache_index++] = 0u;
                    run0--;
                  }
                }
              }
              SIF_ASSERT(cache_index == run);
              x--;
              cache_index = 0u;
              run0 = 0u;
              continue;
            }
            else if ((op & SIF_OPCODE_MASK(5)) == SIF_opcode_3chn_run_delta0) {
              run0 = op ^ SIF_opcode_3chn_run_delta0;
              pixel.value = 0;
            }
            else if ((op & SIF_OPCODE_MASK(2)) == SIF_opcode_reduced_offset) {
              size_t offset = (size_t)(op ^ SIF_opcode_reduced_offset);
              if (use_contextual_dict)
                offset |= ((prev_pixel.rgba.r + prev_pixel.rgba.g) >> (9u - SIF_DICT_CONTEXT_BIT_LENGTH)) << SIF_REDUCED_OFFSET_BIT_LENGTH;
              pixel = dict[offset];
              goto output_pixel;
            }
            else {
              must_add_to_dict = true;
              if ((op & SIF_OPCODE_MASK(1)) == SIF_opcode_delta_15b) {
                uint16_t delta = ((op ^ SIF_opcode_delta_15b) << 8u);
                delta |= src_[position++];

                pixel.delta.r = (((int8_t)((delta >> 10u) << 3)) >> 3);
                pixel.delta.g = (((int8_t)((delta >> 5u) << 3)) >> 3);
                pixel.delta.b = (((int8_t)(delta << 3u)) >> 3);
              }
              else if ((op & SIF_OPCODE_MASK(4)) == SIF_opcode_delta_20b) {
                uint32_t delta = (op ^ SIF_opcode_delta_20b) << 16u;
                delta |= (src_[position++] << 8u);
                delta |= src_[position++];

                pixel.delta.r = (((int8_t)(((delta >> delta_20b_shift_r) & mask_20b.delta.r) << (delta_20b_shift_r - 12))) >> (delta_20b_shift_r - 12));
                pixel.delta.g = (((int8_t)(((delta >> delta_20b_shift_g) & mask_20b.delta.g) << (8 + delta_20b_shift_g - delta_20b_shift_r))) >> (8 + delta_20b_shift_g - delta_20b_shift_r));
                pixel.delta.b = (((int8_t)((delta & mask_20b.delta.b) << (8 - delta_20b_shift_g))) >> (8 - delta_20b_shift_g));
              }
              else { /* SIF_opcode_3chn_mask_delta_8bpc */
                pixel.delta.r = ((op & 0x04u) > 0u) ? (int8_t)src_[position++] : 0;
                pixel.delta.g = ((op & 0x02u) > 0u) ? (int8_t)src_[position++] : 0;
                pixel.delta.b = ((op & 0x01u) > 0u) ? (int8_t)src_[position++] : 0;
              }
            }
          }

          prediction = prev_pixel;
          switch (predictor_id) {
            case SIF_predictor_direct:
            default: {
              pixel.rgba.r = prediction.rgba.r + pixel.delta.r;
              pixel.rgba.g = prediction.rgba.g + pixel.delta.g;
              pixel.rgba.b = prediction.rgba.b + pixel.delta.b;
              break;
            }
            case SIF_predictor_decorrelate_from_red: {
              if (use_2d_prediction && (y > 0))
                prediction.rgba.r = (prediction.rgba.r * 7u + sld_wnd[(sld_offset - (x << 1u) - 1u) & SIF_SLD_WND_MASK].rgba.r) >> 3u;
              int8_t const delta = pixel.delta.r;
              pixel.rgba.r = prediction.rgba.r + delta;
              pixel.rgba.g = prediction.rgba.g + pixel.delta.g + delta;
              pixel.rgba.b = prediction.rgba.b + pixel.delta.b + delta;
              break;
            }
            case SIF_predictor_decorrelate_from_green: {
              if (use_2d_prediction && (y > 0))
                prediction.rgba.g = (prediction.rgba.g * 7u + sld_wnd[(sld_offset - (x << 1u) - 1u) & SIF_SLD_WND_MASK].rgba.g) >> 3u;
              int8_t const delta = pixel.delta.g;
              pixel.rgba.g = prediction.rgba.g + delta;
              pixel.rgba.r = prediction.rgba.r + pixel.delta.r + delta;
              pixel.rgba.b = prediction.rgba.b + pixel.delta.b + delta;
              break;
            }
            case SIF_predictor_decorrelate_from_blue: {
              if (use_2d_prediction && (y > 0))
                prediction.rgba.b = (prediction.rgba.b * 7u + sld_wnd[(sld_offset - (x << 1u) - 1u) & SIF_SLD_WND_MASK].rgba.b) >> 3u;
              int8_t const delta = pixel.delta.b;
              pixel.rgba.b = prediction.rgba.b + delta;
              pixel.rgba.r = prediction.rgba.r + pixel.delta.r + delta;
              pixel.rgba.g = prediction.rgba.g + pixel.delta.g + delta;
              break;
            }
          }

          if (must_add_to_dict) {
            size_t offset = (size_t)SIF_pixelHash(pixel);
            if (use_contextual_dict)
              offset |= ((prev_pixel.rgba.r + prev_pixel.rgba.g) >> (9u - SIF_DICT_CONTEXT_BIT_LENGTH)) << SIF_REDUCED_OFFSET_BIT_LENGTH;
            dict[offset] = pixel;
          }
output_pixel:
          dst_[pixel_pos + 0u] = pixel.rgba.r;
          dst_[pixel_pos + 1u] = pixel.rgba.g;
          dst_[pixel_pos + 2u] = pixel.rgba.b;
          prev_pixel = pixel;
          if (use_2d_prediction)
            sld_wnd[sld_offset++ & SIF_SLD_WND_MASK] = pixel;
        }
      }
    }
  }
  return position;
}

SIF_INLINE uint64_t SIF_compressImageBound(const SIF_content_descriptor_t* const image) {
  SIF_ASSERT(image != NULL);
  /* assume worst case expansion, i.e., single line per slice */
  return (uint64_t)sizeof(SIF_file_header_t) + ((uint64_t)image->height) * ((uint64_t)sizeof(SIF_slice_header_t) + ((uint64_t)image->width) * ((uint64_t)image->channels + 1u) + (uint64_t)sizeof(SIF_end_of_slice_marker_t));
}

void* SIF_compressImage(const SIF_content_descriptor_t* const image, const void* const src, size_t const srcSize, uint64_t* outSize) {
  SIF_ASSERT(image != NULL);
  SIF_ASSERT((image->width > 0u) && (image->width <= SIF_MAX_DIMENSION));
  SIF_ASSERT((image->height > 0u) && (image->height <= SIF_MAX_DIMENSION));
  SIF_ASSERT(image->channels == 3u);
  SIF_ASSERT(src != NULL);
  SIF_ASSERT(srcSize >= ((size_t)image->width * image->height * image->channels));
  *outSize = SIF_compressImageBound(image);
  if (*outSize > SIZE_MAX)
    return NULL;
  uint8_t* const dst = (uint8_t*)SIF_MALLOC((size_t)*outSize);
  if (dst == NULL)
    return NULL;
  const uint8_t* const src_ = (const uint8_t* const)src;
  size_t position = 0u;
  dst[position++] = (uint8_t)(SIF_MAGIC_NUMBER >> 8u);
  dst[position++] = (uint8_t)(SIF_MAGIC_NUMBER | image->channels);
  position += SIF_writeULEB128(&dst[position], image->width);
  position += SIF_writeULEB128(&dst[position], image->height);

  SIF_content_descriptor_t slice = *image;
  ULEB128_t total_height_processed = 0u;
  size_t offset = 0u;
  do {
    slice.height = image->height - total_height_processed;
    while (SIF_compressSliceBound(&slice) > (uint64_t)UINT32_MAX)
      slice.height--;

    size_t const slice_size_offset = position;
    position += sizeof(uint32_t);
    dst[position++] = slice.flags;
    position += SIF_writeULEB128(&dst[position], slice.height);

    uint32_t const slice_size = (uint32_t)SIF_compressSlice(&slice, &dst[position], (size_t)*outSize - position, &src_[offset], srcSize - offset);
    position += slice_size;
    *((uint32_t*)&dst[slice_size_offset]) = slice_size;

    offset += slice.width * slice.height * slice.channels;
    total_height_processed += slice.height;
  } while (total_height_processed < image->height);
  *outSize = position;
  return dst;
}

void* SIF_decompressImage(SIF_content_descriptor_t* const image, const void* const src, size_t const srcSize, uint64_t* outSize) {
  SIF_ASSERT(image != NULL);
  SIF_ASSERT(src != NULL);
  SIF_ASSERT(outSize != NULL);
  if (srcSize < SIF_MINIMUM_IMAGE_SIZE)
    return NULL;
  const uint8_t* const src_ = (const uint8_t* const)src;
  *outSize = 0u;
  size_t position = 0u;
  SIF_file_header_t file_header;
  file_header.magic = src_[position++] << 8u;
  file_header.magic |= src_[position++];
  image->channels = file_header.magic & 0x0Fu;
  if (((file_header.magic & 0xFFF0u) != SIF_MAGIC_NUMBER) || (image->channels != 3u))
    return NULL;
  file_header.width = SIF_readULEB128(src, &position, srcSize);
  file_header.height = SIF_readULEB128(src, &position, srcSize);
  uint64_t const stride = ((uint64_t)file_header.width) * image->channels;
  *outSize =  stride * file_header.height;
  if ((stride > SIZE_MAX) || (*outSize > SIZE_MAX))
    return NULL;

  uint8_t* const dst = (uint8_t*)SIF_MALLOC((size_t)*outSize);
  if (dst == NULL)
    return NULL;
  
  image->width = file_header.width;
  size_t offset = 0u;
  ULEB128_t total_height_processed = 0u;

  while ((total_height_processed < file_header.height) && (position + SIF_MINIMUM_SLICE_SIZE < srcSize)) {
    SIF_slice_header_t slice_header;
    slice_header.size = *((uint32_t*)&src_[position]);
    position += sizeof(uint32_t);
    slice_header.flags = src_[position++];
    slice_header.height = SIF_readULEB128(src, &position, srcSize);

    if (
      (slice_header.size == 0u) ||
      (slice_header.height == 0u) ||
      (position + slice_header.size > srcSize) || 
      (total_height_processed + slice_header.height > file_header.height)
    ) {
      SIF_FREE(dst);
      return NULL;
    }

    image->height = slice_header.height;
    image->flags = slice_header.flags;

    position += SIF_decompressSlice(image, &dst[offset], (size_t)*outSize - offset, &src_[position], slice_header.size);

    if ((position + sizeof(SIF_end_of_slice_marker_t) > srcSize) || (*((SIF_end_of_slice_marker_t*)&src_[position]) != SIF_END_OF_SLICE_MARKER)) {
      SIF_FREE(dst);
      return NULL;
    }
    position += sizeof(SIF_end_of_slice_marker_t);
    total_height_processed += image->height;
    offset += (size_t)(image->height * stride);
  }
  image->height = file_header.height;
  return dst;
}

#ifndef SIF_NO_STDIO
#include <stdio.h>

uint64_t SIF_write(const char* const filename, const void* const src, size_t const srcSize, const SIF_content_descriptor_t* const descriptor) {
  SIF_ASSERT(filename != NULL);
  FILE* output = fopen(filename, "wb");
  if (!output)
    return 0u;
  uint64_t size;
  void* data = SIF_compressImage(descriptor, src, srcSize, &size);
  if (data == NULL) {
    fclose(output);
    return 0u;
  }
  fwrite(data, sizeof(uint8_t), (size_t)size, output);
  SIF_FREE(data);
  fclose(output);
  return size;
}

void* SIF_read(const char* const filename, SIF_content_descriptor_t* const descriptor, uint64_t* outSize) {
  SIF_ASSERT(filename != NULL);
  FILE* input = fopen(filename, "rb");
  if (!input)
    return NULL;
  fseek(input, 0, SEEK_END);
  size_t const srcSize = ftell(input);
  fseek(input, 0, SEEK_SET);
  uint8_t* src = (uint8_t*)SIF_MALLOC(srcSize);
  if (src == NULL)
    return NULL;
  if (fread(src, sizeof(uint8_t), srcSize, input) != srcSize) {
    fclose(input);
    return NULL;
  }
  fclose(input);
  return SIF_decompressImage(descriptor, src, srcSize, outSize);
}

#endif /* SIF_NO_STDIO */

#endif /* SIF_IMPLEMENTATION */