/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstddef>
#include <memory>

#include <jsi/jsi.h>

namespace facebook {
namespace hermes {

/// An opaque, runtime-specific description of a fixed list of property names,
/// created by IHermesObjectShapes::createObjectShape(). Like a jsi::PropNameID
/// it belongs to the runtime that created it, may only be used with it, and
/// must not outlive it (being released with a host object, host function or
/// native state of that runtime when the runtime is destroyed is fine). It may
/// be released on any thread.
class IObjectShape {
 public:
  virtual ~IObjectShape() = default;
  /// The number of names the shape was created with.
  virtual size_t size() const = 0;
};

/// Optional bulk property access, obtained with
/// `jsi::castInterface<IHermesObjectShapes>(&runtime)`.
///
/// Native code that moves structured data across JSI (records, rows, typed
/// lists) reads and creates many objects that all have the same keys. Doing it
/// one jsi::Object::getProperty / setProperty at a time pays the full per-call
/// cost for every field. A shape lets the runtime resolve the keys once per
/// object layout and then move all the values in one call.
class IHermesObjectShapes : public jsi::ICast {
 public:
  static constexpr jsi::UUID uuid{
      0x13711e78,
      0xe74f,
      0x11f1,
      0x95ae,
      0xc9b4e4380d4a};

  /// Create a shape for \p count property names. Names must be distinct.
  /// \return nullptr if the names cannot form a shape: duplicates, or more
  /// than 64 names (HiddenClass::kDictionaryThreshold, beyond which an object
  /// with these properties would be in dictionary mode).
  virtual std::shared_ptr<IObjectShape> createObjectShape(
      const jsi::PropNameID *names,
      size_t count) = 0;

  /// Equivalent to `out[i] = obj.getProperty(rt, names[i])` for every name in
  /// \p shape, in order (getters run, missing properties read as undefined).
  /// \p out must hold shape.size() undefined (e.g. default-constructed)
  /// values; the results are constructed in place.
  /// Fastest for objects that have every name as an own data property and
  /// share one or two layouts (hidden classes) per shape; other objects are
  /// read one property at a time.
  virtual void getShapedProperties(
      const jsi::Object &obj,
      const IObjectShape &shape,
      jsi::Value *out) = 0;

  /// Equivalent to evaluating the object literal
  /// `{[names[0]]: values[0], [names[1]]: values[1], ...}`: a new plain object
  /// whose prototype is Object.prototype, with enumerable, writable,
  /// configurable data properties defined in order (no setters run).
  virtual jsi::Object createObjectFromShape(
      const IObjectShape &shape,
      const jsi::Value *values) = 0;

  /// Equivalent to `out[i] = arr.getValueAtIndex(rt, start + i)` for i from
  /// 0 to count - 1, in order: getters run in index order, and an index that
  /// is out of bounds when it is reached throws as getValueAtIndex does, after
  /// the elements before it were stored in \p out. \p out must hold \p count
  /// undefined values, as above.
  virtual void getArrayElements(
      const jsi::Array &arr,
      size_t start,
      size_t count,
      jsi::Value *out) = 0;

 protected:
  ~IHermesObjectShapes() = default;
};

} // namespace hermes
} // namespace facebook
