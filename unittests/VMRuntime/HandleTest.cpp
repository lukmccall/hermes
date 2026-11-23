/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "VMRuntimeTestHelpers.h"
#include "hermes/BCGen/HBC/Bytecode.h"
#include "hermes/VM/DummyObject.h"
#include "hermes/VM/Runtime.h"
#include "hermes/VM/StringPrimitive.h"

#include "gtest/gtest.h"

using namespace hermes::vm;
using namespace hermes::vm::testhelpers;

namespace {

using HandleTest = RuntimeTestFixture;
using HandleDeathTest = RuntimeTestFixture;

TEST_F(HandleTest, AddAndRemoveTest) {
  auto str1 = StringPrimitive::createNoThrow(runtime, createUTF16Ref(u"str1"));
  auto str2 = StringPrimitive::createNoThrow(runtime, createUTF16Ref(u"str2"));
  auto str3 = StringPrimitive::createNoThrow(runtime, createUTF16Ref(u"str3"));

  {
    auto h1 = runtime.makeHandle<StringPrimitive>(str1.get());
    {
      GCScope scope(runtime);
      auto h2 = runtime.makeHandle<StringPrimitive>(str2.get());
      auto h3 = runtime.makeHandle<StringPrimitive>(str3.get());

      ASSERT_EQ(str1, h1);
      ASSERT_EQ(str2, h2);
      ASSERT_EQ(str3, h3);
    }
    ASSERT_EQ(str1, h1);

    {
      GCScope scope(runtime);
      // Check operator=.
      Handle<StringPrimitive> h3 = h1;
      MutableHandle<StringPrimitive> h4(runtime, h1.get());
      Handle<StringPrimitive> h5 = h4;

      ASSERT_EQ(str1, h3);
      ASSERT_EQ(str1, h4);
      ASSERT_EQ(str1, h5);
      h4 = h1.get();

      ASSERT_EQ(str1, h4);
    }
    {
      GCScope scope(runtime);
      // Check copy constructor.
      Handle<StringPrimitive> h3(h1);
      Handle<StringPrimitive> h4(h1);

      ASSERT_EQ(str1, h3);
      ASSERT_EQ(str1, h4);
    }

    {
      GCScope scope(runtime);
      // Check mov constructor.
      auto h5 = runtime.makeHandle<StringPrimitive>(str2.get());
      ASSERT_EQ(str2, h5);
    }
  }
}

TEST_F(HandleTest, ValueAddAndRemoveTest) {
  auto str1 = runtime.makeHandle(HermesValue::encodeTrustedNumberValue(1));
  auto str2 = runtime.makeHandle(HermesValue::encodeTrustedNumberValue(2));
  auto str3 = runtime.makeHandle(HermesValue::encodeTrustedNumberValue(3));

  {
    MutableHandle<> h1(runtime, str1.get());
    {
      MutableHandle<> h2(runtime, str2.get());
      auto h3 = runtime.makeHandle(str3.get());

      ASSERT_EQ(str1, h1);
      ASSERT_EQ(str2, h2);
      ASSERT_EQ(str3, h3);

      h2.set(h3.get());
      ASSERT_EQ(str3, h2);
    }
    // h2 is out of scope, so it should be gone.

    {
      // Check operator=.
      Handle<> h3 = h1;
      MutableHandle<> h4(runtime, h1.get());
      Handle<> h5 = h4;

      ASSERT_EQ(str1, h3);
      ASSERT_EQ(str1, h4);
      ASSERT_EQ(str1, h5);
      h4 = h1.get();

      ASSERT_EQ(str1, h4);
    }
    {
      // Check copy constructor.
      Handle<> h3(h1);
      Handle<> h4(h1);

      ASSERT_EQ(str1, h3);
      ASSERT_EQ(str1, h4);
    }

    {
      // Check mov constructor.
      MutableHandle<> h5(runtime, h1.get());
      MutableHandle<> h6(std::move(h5));
      ASSERT_EQ(str1, h6);
    }
  }
}

TEST_F(HandleTest, MarkTest) {
  GCScope gcScope{runtime};
  auto v1 = runtime.makeHandle(HermesValue::encodeTrustedNumberValue(1));
  (void)v1;
  auto v2 = runtime.makeHandle(HermesValue::encodeTrustedNumberValue(2));
  (void)v2;
  ASSERT_EQ(2u, gcScope.getHandleCountDbg());

  auto marker = gcScope.createMarker();

  auto v3 = runtime.makeHandle(HermesValue::encodeTrustedNumberValue(3));
  (void)v3;
  ASSERT_EQ(3u, gcScope.getHandleCountDbg());

  gcScope.flushToMarker(marker);
  ASSERT_EQ(2u, gcScope.getHandleCountDbg());

  auto v4 = runtime.makeHandle(HermesValue::encodeTrustedNumberValue(4));
  (void)v4;
  ASSERT_EQ(3u, gcScope.getHandleCountDbg());

  gcScope.flushToMarker(marker);
  ASSERT_EQ(2u, gcScope.getHandleCountDbg());
}

/// Make sure that related Handle-s can be assigned.
TEST_F(HandleTest, ScopedPointerConstructorTest) {
  auto function = runtime.makeHandle<JSFunction>(
      JSFunction::create(runtime, domain, runtime.makeNullHandle<JSObject>()));
  Handle<JSObject> obj = function;
  ASSERT_EQ(function.get(), obj.get());
}

/// Leave nested scopes holding 0, 1, 16 (one full chunk) and 17+ (several
/// chunks) handles, and a multi-chunk scope cleared before it is left. After
/// each, the parent must be the top scope again and its handles must still be
/// roots across a collection. In builds without HERMES_SLOW_DEBUG the
/// single-chunk exits take the inline path in ~GCScope.
TEST_F(HandleTest, NestedScopeExitTest) {
  GCScope &outer = gcScope;
  // Above the default debug limit, so scopes can grow several chunks.
  constexpr unsigned kLimit = 256;
  // \return whether a new handle is allocated in \p scope, i.e. whether it
  // is the top scope. The probe handle is released again, so the scope's
  // handle count is unchanged (but a probe at a chunk boundary leaves a new
  // chunk allocated).
  auto isTop = [&](GCScope &scope) {
    GCScopeMarkerRAII marker(scope);
    size_t before = scope.getHandleCountDbg();
    runtime.makeHandle(HermesValue::encodeTrustedNumberValue(0));
    return scope.getHandleCountDbg() == before + 1;
  };

  // Strings of distinct lengths, so a stale or overwritten root shows up.
  std::vector<Handle<StringPrimitive>> parents;
  for (unsigned i = 0; i < 4; ++i)
    parents.push_back(
        StringPrimitive::createNoThrow(runtime, std::string(i + 1, 'p')));
  // The helpers return false on a mismatch, so callers can ASSERT on them
  // (an ASSERT inside a lambda would only return from the lambda).
  auto checkParents = [&]() {
    runtime.collect("test");
    for (unsigned i = 0; i < parents.size(); ++i) {
      if (parents[i]->getStringLength() != i + 1 || parents[i]->at(0) != u'p')
        return false;
    }
    return true;
  };

  auto fillScope = [&](unsigned count) {
    std::vector<Handle<StringPrimitive>> handles;
    for (unsigned i = 0; i < count; ++i)
      handles.push_back(
          StringPrimitive::createNoThrow(runtime, std::string(i + 1, 'c')));
    runtime.collect("test");
    for (unsigned i = 0; i < count; ++i) {
      if (handles[i]->getStringLength() != i + 1)
        return false;
    }
    return true;
  };

  for (unsigned count :
       {0u,
        1u,
        (unsigned)GCScope::getChunkSize(),
        (unsigned)GCScope::getChunkSize() + 1,
        (unsigned)GCScope::getChunkSize() * 3 + 5}) {
    {
      GCScope child(runtime, "child", kLimit);
      ASSERT_TRUE(isTop(child));
      ASSERT_TRUE(fillScope(count));
      {
        GCScope grandchild(runtime, "grandchild", kLimit);
        ASSERT_TRUE(isTop(grandchild));
        ASSERT_TRUE(fillScope(count));
        // Leave it with exactly `count` handles: 0, 1 and 16 fit the inline
        // chunk (the inline exit), 17 and 53 need more chunks.
        ASSERT_EQ(count, grandchild.getHandleCountDbg());
      }
      // A probe of a full inline chunk would allocate a second chunk and turn
      // the child's exit into a multi-chunk one; the grandchild covers 16.
      if (count != GCScope::getChunkSize())
        ASSERT_TRUE(isTop(child));
      ASSERT_EQ(count, child.getHandleCountDbg());
    }
    ASSERT_TRUE(isTop(outer));
    ASSERT_TRUE(checkParents());
  }

  // A scope that grew several chunks and was then cleared still owns those
  // chunks when it is left.
  {
    GCScope child(runtime, "child", kLimit);
    ASSERT_TRUE(fillScope(GCScope::getChunkSize() * 2 + 3));
    child.clearAllHandles();
    ASSERT_TRUE(fillScope(2));
  }
  ASSERT_TRUE(isTop(outer));
  ASSERT_TRUE(checkParents());
  {
    GCScope child(runtime, "child", kLimit);
    ASSERT_TRUE(fillScope(GCScope::getChunkSize() * 2 + 3));
    child.flushToSmallCount(0);
    ASSERT_TRUE(fillScope(GCScope::getChunkSize() + 1));
  }
  ASSERT_TRUE(isTop(outer));
  ASSERT_TRUE(checkParents());

  // The parent scope keeps working after its children are gone.
  parents.push_back(StringPrimitive::createNoThrow(runtime, "ppppp"));
  ASSERT_TRUE(checkParents());
}

TEST_F(HandleTest, PinnedValueHV32Test) {
  struct : Locals {
    PinnedValue<DummyObject> obj;
    PinnedValue<SmallHermesValue> pshv;
    PinnedValue<StringPrimitive> str;
    PinnedValue<SymbolID> sym;
  } lv;
  LocalsRAII lraii{runtime, &lv};

  lv.obj = testhelpers::DummyObject::create(runtime.getHeap(), runtime);
  lv.pshv = SmallHermesValue::encodeObjectValue(*lv.obj, runtime);
  runtime.collect("test");
  {
    auto shv = lv.pshv.getSmallHermesValue();
    ASSERT_TRUE(shv.isObject());
    auto *obj = static_cast<DummyObject *>(shv.getObject(runtime));
    ASSERT_TRUE(obj->hvBool.getBool());
  }

  lv.obj = nullptr;
  // Now lv.pshv is the only reference that holds the object alive.
  runtime.collect("test");
  {
    auto shv = lv.pshv.getSmallHermesValue();
    ASSERT_TRUE(shv.isObject());
    auto *obj = static_cast<testhelpers::DummyObject *>(shv.getObject(runtime));
    ASSERT_TRUE(obj->hvBool.getBool());
  }

  lv.str = StringPrimitive::createNoThrow(runtime, "I'm a string");
  auto sym =
      runtime.getIdentifierTable().createNotUniquedSymbol(runtime, lv.str);
  ASSERT_FALSE(isException(sym));
  lv.sym = *sym;
  lv.pshv = SmallHermesValue::encodeSymbolValue(*lv.sym);
  runtime.collect("test");
  {
    auto shv = lv.pshv.getSmallHermesValue();
    ASSERT_TRUE(shv.isSymbol());
    ASSERT_TRUE(shv.getSymbol().isValid());
  }

  lv.sym = SymbolID{};
  runtime.collect("test");
  {
    auto shv = lv.pshv.getSmallHermesValue();
    ASSERT_TRUE(shv.isSymbol());
    ASSERT_TRUE(shv.getSymbol().isValid());
  }
}

#if defined(HERMES_SLOW_DEBUG) && defined(ASSERT_DEATH)
TEST_F(HandleDeathTest, UseFlushedHandle) {
  auto marker = gcScope.createMarker();
  auto handle = StringPrimitive::createNoThrow(runtime, "hello");
  // This flush should destroy the handle created after the marker, using it
  // afterwards should trigger an assertion failure.
  gcScope.flushToMarker(marker);
  ASSERT_DEATH(
      { handle->getStringLength(); }, "Assertion.*Reading from flushed handle");
}

TEST_F(HandleDeathTest, UseRAIIFlushedHandle) {
  GCScopeMarkerRAII marker(gcScope);
  auto handle = StringPrimitive::createNoThrow(runtime, "hello");
  // This flush should destroy the handle created after the marker, using it
  // afterwards should trigger an assertion failure.
  marker.flush();
  ASSERT_DEATH(
      { handle->getStringLength(); }, "Assertion.*Reading from flushed handle");
}

TEST_F(HandleDeathTest, FlushToSmallCount) {
  auto handle = StringPrimitive::createNoThrow(runtime, "hello");
  // This flush should destroy the handle created after the marker, using it
  // afterwards should trigger an assertion failure.
  gcScope.flushToSmallCount(0);
  ASSERT_DEATH(
      { handle->getStringLength(); }, "Assertion.*Reading from flushed handle");
}
#endif

} // namespace
