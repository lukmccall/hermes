/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/Support/UTF16Stream.h"
#include "hermes/Support/UTF8.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

using namespace hermes;

namespace {

// Verify correct behavior of convertUTF16ToUTF8StringWithSingleSurrogates().
TEST(StringTest, UTF16ToUTF8StringWithSingleSurrogates) {
  auto convert = [](std::initializer_list<char16_t> cs) -> std::vector<char> {
    std::string out;
    std::vector<char16_t> vec(cs);
    convertUTF16ToUTF8WithSingleSurrogates(out, cs);
    return std::vector<char>(out.begin(), out.end());
  };
  auto bytes =
      [](std::initializer_list<unsigned char> cs) -> std::vector<char> {
    return std::vector<char>(cs.begin(), cs.end());
  };
  EXPECT_EQ(bytes({}), convert({}));
  EXPECT_EQ(bytes({'a'}), convert({'a'}));
  EXPECT_EQ(bytes({'a', 'b', 'c', 0, 'd'}), convert({'a', 'b', 'c', 0, 'd'}));
  EXPECT_EQ(bytes({'a', 'b', 'c', 0, 'd'}), convert({'a', 'b', 'c', 0, 'd'}));
  EXPECT_EQ(bytes({'e', 0xCC, 0x81}), convert({'e', 0x0301}));
  EXPECT_EQ(bytes({0xE2, 0x98, 0x83}), convert({0x2603}));

  // UTF-16 encoded U+1F639
  EXPECT_EQ(
      bytes({0xED, 0xA0, 0xBD, 0xED, 0xB8, 0xB9}), convert({0xD83D, 0xDE39}));

  // Unpaired surrogate halves
  EXPECT_EQ(bytes({0xED, 0xA0, 0xBD}), convert({0xD83D}));
  EXPECT_EQ(bytes({'a', 0xED, 0xA0, 0xBD, 'b'}), convert({'a', 0xD83D, 'b'}));
  EXPECT_EQ(bytes({'a', 0xED, 0xB8, 0xB9, 'b'}), convert({'a', 0xDE39, 'b'}));

  // Unpaired trailing surrogate halves
  EXPECT_EQ(bytes({'a', 0xED, 0xA0, 0xBD}), convert({'a', 0xD83D}));
  EXPECT_EQ(bytes({'a', 0xED, 0xB8, 0xB9}), convert({'a', 0xDE39}));
}

// Verify correct behavior of convertUTF16ToUTF8StringWithReplacements().
TEST(StringTest, UTF16ToUTF8StringWithReplacements) {
  auto convert = [](std::initializer_list<char16_t> cs) -> std::vector<char> {
    std::string out;
    std::vector<char16_t> vec(cs);
    convertUTF16ToUTF8WithReplacements(out, cs);
    return std::vector<char>(out.begin(), out.end());
  };
  auto convertWithMaxChars =
      [](std::initializer_list<char16_t> cs,
         size_t maxChars) -> std::pair<std::vector<char>, bool> {
    std::string out;
    std::vector<char16_t> vec(cs);
    bool fullyWritten = convertUTF16ToUTF8WithReplacements(out, cs, maxChars);
    return std::make_pair(
        std::vector<char>(out.begin(), out.end()), fullyWritten);
  };
  auto bytes =
      [](std::initializer_list<unsigned char> cs) -> std::vector<char> {
    return std::vector<char>(cs.begin(), cs.end());
  };
  EXPECT_EQ(bytes({}), convert({}));
  EXPECT_EQ(bytes({'a'}), convert({'a'}));
  EXPECT_EQ(bytes({'a', 'b', 'c', 0, 'd'}), convert({'a', 'b', 'c', 0, 'd'}));
  EXPECT_EQ(bytes({'a', 'b', 'c', 0, 'd'}), convert({'a', 'b', 'c', 0, 'd'}));
  EXPECT_EQ(bytes({'e', 0xCC, 0x81}), convert({'e', 0x0301}));
  EXPECT_EQ(bytes({0xE2, 0x98, 0x83}), convert({0x2603}));

  // UTF-16 encoded U+1F639
  EXPECT_EQ(bytes({0xF0, 0x9F, 0x98, 0xB9}), convert({0xD83D, 0xDE39}));

  // Unpaired surrogate halves
  EXPECT_EQ(bytes({0xEF, 0xBF, 0xBD}), convert({0xD83D}));
  EXPECT_EQ(bytes({'a', 0xEF, 0xBF, 0xBD, 'b'}), convert({'a', 0xD83D, 'b'}));
  EXPECT_EQ(bytes({'a', 0xEF, 0xBF, 0xBD, 'b'}), convert({'a', 0xDE39, 'b'}));

  // Unpaired trailing surrogate halves
  EXPECT_EQ(bytes({'a', 0xEF, 0xBF, 0xBD}), convert({'a', 0xD83D}));
  EXPECT_EQ(bytes({'a', 0xEF, 0xBF, 0xBD}), convert({'a', 0xDE39}));

  // Check that strings are truncated when maxChars is specified.
  // First with normal ASCII strings.
  auto strAndFullyWritten = convertWithMaxChars({'a', 'b', 'c', 0, 'd'}, 3);
  EXPECT_EQ(bytes({'a', 'b', 'c'}), strAndFullyWritten.first);
  EXPECT_EQ(false, strAndFullyWritten.second);

  // Check when maxCharacters is higher than the number of available characters,
  // that it only writes out the max string.
  strAndFullyWritten = convertWithMaxChars({'a', 'b', 'c', 0, 'd'}, 6);
  EXPECT_EQ(bytes({'a', 'b', 'c', 0, 'd'}), strAndFullyWritten.first);
  EXPECT_EQ(true, strAndFullyWritten.second);

  // Make sure it counts the number of Unicode code points, not the number of
  // individual octets.
  strAndFullyWritten = convertWithMaxChars({0xD83D, 0xDE39, 0xD83D, 0xDE39}, 1);
  EXPECT_EQ(bytes({0xF0, 0x9F, 0x98, 0xB9}), strAndFullyWritten.first);
  EXPECT_EQ(false, strAndFullyWritten.second);

  // Check that strings equal to the max length are correctly reported.
  strAndFullyWritten = convertWithMaxChars({'a', 'b', 'c'}, 3);
  EXPECT_EQ(bytes({'a', 'b', 'c'}), strAndFullyWritten.first);
  EXPECT_EQ(true, strAndFullyWritten.second);
}

// Without a character limit, convertUTF16ToUTF8WithReplacements writes into a
// preallocated buffer. Its output must match the bounded path's exactly.
TEST(StringTest, UTF16ToUTF8UnboundedMatchesBounded) {
  const char16_t units[] = {
      u'a', u'Z', 0x7F, 0x80, 0xFF, 0x100, 0x7FF, 0x800, 0xFFFF,
      0xD800, 0xDBFF, 0xDC00, 0xDFFF, 0xD83D, 0xDE00, 0x017C};
  const size_t numUnits = sizeof(units) / sizeof(units[0]);
  uint32_t seed = 12345;
  auto next = [&seed]() {
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) & 0x7FFF;
  };
  // Lengths around the short/long buffer boundary and beyond.
  for (size_t len : {0, 1, 2, 3, 7, 64, 127, 128, 129, 200, 1000}) {
    for (int trial = 0; trial < 50; ++trial) {
      std::u16string s(len, u'x');
      for (auto &c : s) {
        uint32_t r = next() % 4;
        c = r == 0 ? u'x' : units[next() % numUnits];
      }
      llvh::ArrayRef<char16_t> input(s.data(), s.size());
      std::string unbounded, bounded;
      EXPECT_TRUE(convertUTF16ToUTF8WithReplacements(unbounded, input));
      // A limit above the number of characters takes the bounded path.
      EXPECT_TRUE(convertUTF16ToUTF8WithReplacements(bounded, input, len + 1));
      EXPECT_EQ(unbounded, bounded) << "length " << len << " trial " << trial;
    }
  }
}

// Literal expectations for inputs past the stack buffer (the counted, heap
// path), so it is not only checked against the bounded path.
TEST(StringTest, UTF16ToUTF8UnboundedLongLiterals) {
  // 130 surrogate pairs (260 code units): U+1F639 is F0 9F 98 B9.
  std::u16string pairs;
  std::string pairsUTF8;
  for (int i = 0; i < 130; ++i) {
    pairs += u"\U0001F639";
    pairsUTF8 += "\xF0\x9F\x98\xB9";
  }
  std::string out;
  EXPECT_TRUE(convertUTF16ToUTF8WithReplacements(
      out, llvh::ArrayRef<char16_t>(pairs.data(), pairs.size())));
  EXPECT_EQ(out, pairsUTF8);

  // 200 ASCII units, a 2-byte unit, then a lone high surrogate at the end,
  // which becomes U+FFFD (EF BF BD).
  std::u16string tail(200, u'x');
  tail += u'\u00E9';
  tail += (char16_t)0xD83D;
  std::string tailUTF8 = std::string(200, 'x') + "\xC3\xA9\xEF\xBF\xBD";
  out = "previous contents";
  EXPECT_TRUE(convertUTF16ToUTF8WithReplacements(
      out, llvh::ArrayRef<char16_t>(tail.data(), tail.size())));
  EXPECT_EQ(out, tailUTF8);
}

// Fixed cases for the unbounded path: long ASCII runs (with NULs), inputs
// that need the full 3 bytes per code unit, unpaired surrogates, either side
// of the stack-buffer limit, and a destination that already holds data.
TEST(StringTest, UTF16ToUTF8UnboundedFixedCases) {
  auto bounded = [](const std::u16string &s) {
    std::string out;
    convertUTF16ToUTF8WithReplacements(
        out, llvh::ArrayRef<char16_t>(s.data(), s.size()), s.size() + 1);
    return out;
  };
  for (size_t len : {0, 1, 127, 128, 129, 400}) {
    std::u16string ascii(len, u'q'), threeByte(len, 0x20AC),
        lone(len, 0xD800), withNul(len, u'a');
    for (size_t i = 0; i < len; i += 7)
      withNul[i] = 0;
    for (const std::u16string &s : {ascii, threeByte, lone, withNul}) {
      // A destination with previous contents must be replaced, not appended.
      std::string out = "previous contents";
      EXPECT_TRUE(convertUTF16ToUTF8WithReplacements(
          out, llvh::ArrayRef<char16_t>(s.data(), s.size())));
      EXPECT_EQ(out, bounded(s)) << "length " << len;
    }
    // All three-byte units: exactly 3 bytes each.
    std::string out;
    convertUTF16ToUTF8WithReplacements(
        out, llvh::ArrayRef<char16_t>(threeByte.data(), threeByte.size()));
    EXPECT_EQ(out.size(), 3 * len);
  }
}

TEST(StringTest, IsAllASCIITest) {
  std::vector<uint8_t> ascii = {32, 23, 18};
  std::vector<uint8_t> notAscii = {234, 1, 0};
  EXPECT_TRUE(isAllASCII(ascii));
  EXPECT_FALSE(isAllASCII(notAscii));
  // Check overloads.
  uint8_t asciiArr[] = {1, 3, 14, 54, 19, 124, 13, 43, 127, 19, 0};
  uint8_t partialAsciiArr[] = {1, 3, 14, 54, 219, 124, 13, 43, 127, 19};
  uint8_t noAsciiArr[] = {129, 153, 175, 201, 219, 231, 214, 255, 255, 130};
  EXPECT_TRUE(isAllASCII(std::begin(asciiArr), std::end(asciiArr)));
  EXPECT_FALSE(
      isAllASCII(std::begin(partialAsciiArr), std::end(partialAsciiArr)));
  EXPECT_FALSE(isAllASCII(std::begin(noAsciiArr), std::end(noAsciiArr)));
  EXPECT_TRUE(
      isAllASCII((char *)std::begin(asciiArr), (char *)std::end(asciiArr)));
  EXPECT_FALSE(isAllASCII(
      (char *)std::begin(partialAsciiArr), (char *)std::end(partialAsciiArr)));
  EXPECT_FALSE(
      isAllASCII((char *)std::begin(noAsciiArr), (char *)std::end(noAsciiArr)));
  // Torture test of all possible alignments and lengths.
  for (size_t start = 0; start <= sizeof asciiArr / sizeof *asciiArr; start++) {
    for (size_t end = start; end <= sizeof asciiArr / sizeof *asciiArr; end++) {
      EXPECT_TRUE(isAllASCII(&asciiArr[start], &asciiArr[end]));
    }
  }
  for (size_t start = 0; start <= sizeof noAsciiArr / sizeof *noAsciiArr;
       start++) {
    for (size_t end = start; end <= sizeof noAsciiArr / sizeof *noAsciiArr;
         end++) {
      // Only zero-length substrings are ASCII.
      EXPECT_EQ(start == end, isAllASCII(&noAsciiArr[start], &noAsciiArr[end]));
    }
  }
}

TEST(UTF16StreamTest, EmptyUTF16InputTest) {
  UTF16Stream stream(llvh::ArrayRef<char16_t>{});
  EXPECT_FALSE(stream.hasChar());
}

TEST(UTF16StreamTest, UTF16InputTest) {
  char16_t str16[] = {1, 123, 1234};
  UTF16Stream stream(llvh::ArrayRef<char16_t>(str16, str16 + 3));
  EXPECT_TRUE(stream.hasChar());
  EXPECT_EQ(1, *stream);
  ++stream;
  EXPECT_TRUE(stream.hasChar());
  EXPECT_EQ(123, *stream);
  ++stream;
  EXPECT_TRUE(stream.hasChar());
  EXPECT_EQ(1234, *stream);
  ++stream;
  EXPECT_FALSE(stream.hasChar());
}

TEST(UTF16StreamTest, UTF8InputTest) {
  {
    UTF16Stream stream(llvh::ArrayRef<uint8_t>{});
    EXPECT_FALSE(stream.hasChar());
  }
  {
    uint8_t str8[] = {0xF0, 0x9F, 0x98, 0xB9};
    UTF16Stream stream(llvh::ArrayRef<uint8_t>(str8, str8 + sizeof(str8)));
    EXPECT_TRUE(stream.hasChar());
    EXPECT_EQ(0xD83D, *stream);
    ++stream;
    EXPECT_TRUE(stream.hasChar());
    EXPECT_EQ(0xDE39, *stream);
    ++stream;
    EXPECT_FALSE(stream.hasChar());
  }
  {
    // Single code point that requires two UTF16 units.
    uint8_t surrogates[] = {0xF0, 0x9F, 0x98, 0xB9};
    // String where every non-empty prefix is an odd number of UTF16 units.
    std::vector<uint8_t> str8{'X'};
    // Make it longer than any reasonable chunk size.
    static const int kReps = 123123;
    for (int i = 0; i < kReps; ++i) {
      str8.insert(str8.end(), surrogates, surrogates + sizeof(surrogates));
    }
    UTF16Stream stream(llvh::ArrayRef<uint8_t>(str8.data(), str8.size()));
    EXPECT_TRUE(stream.hasChar());
    EXPECT_EQ('X', *stream);
    ++stream;
    for (int i = 0; i < kReps; ++i) {
      EXPECT_TRUE(stream.hasChar());
      EXPECT_EQ(0xD83D, *stream);
      ++stream;
      EXPECT_TRUE(stream.hasChar());
      EXPECT_EQ(0xDE39, *stream);
      ++stream;
    }
    EXPECT_FALSE(stream.hasChar());
  }
}

TEST(UTF16StreamTest, UTF16Capture) {
  size_t N = 1000;
  std::vector<char16_t> source(N);
  for (size_t i = 0; i < N; i++) {
    source[i] = i;
  }
  UTF16Stream stream(source);
  stream.beginCapture();
  while (stream.hasChar())
    ++stream;
  auto ref = stream.endCapture();
  for (size_t i = 0; i < ref.size(); i++) {
    EXPECT_EQ(source[i], ref[i]);
  }
}

// Test UTF16Stream can handle short utf8 captures
TEST(UTF16StreamTest, ShortUTF8Capture) {
  size_t N = 100;
  std::vector<uint8_t> source(N);
  for (size_t i = 0; i < N; i++) {
    source[i] = i;
  }
  UTF16Stream stream(source);
  stream.beginCapture();
  while (stream.hasChar())
    ++stream;
  auto ref = stream.endCapture();
  for (size_t i = 0; i < ref.size(); i++) {
    EXPECT_EQ(i % 128, ref[i]);
  }
}

// Test UTF16Stream can handle very long utf8  captures
TEST(UTF16StreamTest, LongUTF8Capture) {
  size_t N = 16384;
  std::vector<uint8_t> source(N);
  for (size_t i = 0; i < N; i++) {
    source[i] = i % 128;
  }
  UTF16Stream stream(source);
  stream.beginCapture();
  while (stream.hasChar())
    ++stream;
  auto ref = stream.endCapture();
  for (size_t i = 0; i < ref.size(); i++) {
    EXPECT_EQ(i % 128, ref[i]);
  }
}

size_t countRemainingCharsInStream(UTF16Stream &&str) {
  size_t size = 0;
  while (str.hasChar()) {
    ++size;
    ++str;
  }
  return size;
}

TEST(UTF16StreamTest, MoveTest) {
  char16_t str16[] = {1, 123, 1234};
  UTF16Stream stream(llvh::ArrayRef<char16_t>(str16, str16 + 3));
  EXPECT_EQ(3, countRemainingCharsInStream(std::move(stream)));
}

TEST(UTF16StreamTest, IsUnicodeIDStartTest) {
  EXPECT_TRUE(isUnicodeIDStart(0x2118));
}

} // end anonymous namespace
