/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// RUN: %hermes %s | %FileCheck --match-full-lines %s
// RUN: %hermes -O0 %s | %FileCheck --match-full-lines %s
// RUN: %hermes -lazy %s | %FileCheck --match-full-lines %s
// RUN: %hermes -gc-sanitize-handles=1 %s | %FileCheck --match-full-lines %s

// GetById/PutById call getters and setters found on the object itself, or on
// its immediate prototype through a side cache. Every read and write by name
// must agree with the same access by computed key, which does neither.

print('accessor-side-cache');
// CHECK-LABEL: accessor-side-cache

var failures = 0;
function check(got, want, what) {
  if (!Object.is(got, want)) {
    failures++;
    if (failures < 10) print('MISMATCH', what, String(got), String(want));
  }
}
var log = [];
// The key for computed accesses comes from Object.keys, so the compiler cannot
// fold `o[KEY]` into a by-name access: it stays GetByVal/PutByVal.
var KEY = Object.keys({prop: 0})[0];
function rd(o) { try { return o.prop; } catch (e) { return 'threw ' + e.constructor.name; } }
function rdC(o) { try { return o[KEY]; } catch (e) { return 'threw ' + e.constructor.name; } }
function wrS(o, v) { 'use strict'; try { o.prop = v; return 'ok'; } catch (e) { return 'threw ' + e.constructor.name; } }
function wrSC(o, v) { 'use strict'; try { o[KEY] = v; return 'ok'; } catch (e) { return 'threw ' + e.constructor.name; } }
function wrL(o, v) { try { o.prop = v; return 'ok'; } catch (e) { return 'threw ' + e.constructor.name; } }
function wrLC(o, v) { try { o[KEY] = v; return 'ok'; } catch (e) { return 'threw ' + e.constructor.name; } }
function own(o) {
  return JSON.stringify(Object.getOwnPropertyNames(o).map(
      k => [k, Object.getOwnPropertyDescriptor(o, k).value]));
}
function mkProto(tag, noGet, noSet) {
  var p = {method() { return 'm' + tag; }};
  var d = {configurable: true, enumerable: true};
  if (!noGet) d.get = function() { return tag + ':' + this.x; };
  if (!noSet) d.set = function(v) { log.push(tag + '<-' + v); };
  return Object.defineProperty(p, 'prop', d);
}

for (var round = 0; round < 60; round++) {
  var pA = mkProto('A' + round), pB = mkProto('B' + round);
  var pGet = mkProto('G', false, true), pSet = mkProto('S', true, false);
  var grand = Object.create(pA);
  var makers = [
    () => { var o = Object.create(pA); o.x = 1; return o; },
    () => { var o = Object.create(pB); o.x = 1; return o; },
    () => { var o = Object.create(pGet); o.x = 2; return o; },
    () => { var o = Object.create(pSet); o.x = 3; return o; },
    () => { var o = Object.create(grand); o.x = 4; return o; },
    () => { var o = Object.create(pA); o.x = 5; o.y = 1; delete o.y; return o; },
    () => Object.create(new Proxy(pA, {})),
    () => { var o = Object.create(pA); o.x = 9; return new Proxy(o, {}); },
    () => { var o = Object.create(pA); o.x = 6; return Object.freeze(o); },
    () => Object.defineProperty({x: 7}, 'prop',
        {get() { return 'own' + this.x; }, set(v) { log.push('own<-' + v); },
         configurable: true}),
    () => Object.defineProperty({x: 8}, 'prop',
        {get() { throw new RangeError('boom'); }, configurable: true}),
  ];
  for (var pass = 0; pass < 3; pass++) {
    for (var m = 0; m < makers.length; m++) {
      var a = makers[m](), b = makers[m]();
      check(rd(a), rdC(b), 'get ' + m);
      var n = log.length;
      var ra = pass & 1 ? wrS(a, round) : wrL(a, round);
      var la = log.slice(n).join('|');
      n = log.length;
      var rb = pass & 1 ? wrSC(b, round) : wrLC(b, round);
      var lb = log.slice(n).join('|');
      check(ra, rb, 'set ' + m);
      check(la, lb, 'setter calls ' + m);
      check(own(a), own(b), 'own props ' + m);
    }
    if (pass === 1) {
      Object.defineProperty(pA, 'prop', {get() { return 'redef'; },
          set(v) { log.push('R<-' + v); }, configurable: true});
      Object.defineProperty(pB, 'prop',
          {value: 'data', writable: true, configurable: true});
      delete pGet.prop;
    }
  }
}

// The receiver's parent changes to one with a different layout.
var p1 = mkProto('P1');
var p2 = {aa: 1, bb: 2};
Object.defineProperty(p2, 'prop', {get() { return 'P2'; },
    set(v) { log.push('P2<-' + v); }, configurable: true});
for (var i = 0; i < 20; i++) {
  var o = Object.create(p1); o.x = i;
  check(rd(o), 'P1:' + i, 'before swap');
  Object.setPrototypeOf(o, p2);
  check(rd(o), 'P2', 'after swap');
  var n = log.length; wrL(o, i);
  check(log[n], 'P2<-' + i, 'setter after swap');
}

// A dictionary-mode receiver gains an own property without changing class.
var pd = mkProto('D');
for (var i = 0; i < 20; i++) {
  var o = Object.create(pd); o.x = i; o.y = 0; delete o.y;
  check(rd(o), 'D:' + i, 'dictionary');
  Object.defineProperty(o, 'prop', {value: 'late', writable: true, configurable: true});
  check(rd(o), 'late', 'dictionary own');
}

// Dictionary-mode holders whose accessor becomes a data property.
for (var i = 0; i < 40; i++) {
  // A prototype in dictionary mode after one delete (its class can still be
  // cached) or two (it cannot, and is updated in place from then on).
  var pdict = mkProto('PD');
  pdict.extra = 1;
  delete pdict.extra;
  if (i & 1) {
    pdict.extra2 = 1;
    delete pdict.extra2;
  }
  var o = Object.create(pdict); o.x = i;
  for (var j = 0; j < 3; j++) {
    check(rd(o), 'PD:' + i, 'dictionary proto');
    var n = log.length; wrL(o, j);
    check(log[n], 'PD<-' + j, 'dictionary proto setter');
  }
  Object.defineProperty(pdict, 'prop',
      {value: 'pdata' + i, writable: true, configurable: true});
  check(rd(o), 'pdata' + i, 'dictionary proto now data');
  check(wrS(o, 1), 'ok', 'dictionary proto now data write');
  check(own(o), own(Object.assign(Object.create(null), {x: i, prop: 1})),
      'dictionary proto now data own props');
  // An own accessor on a dictionary-mode receiver.
  var od = {x: i, y: 0};
  delete od.y;
  Object.defineProperty(od, 'prop', {get() { return 'od' + this.x; },
      set(v) { log.push('od<-' + v); }, configurable: true});
  check(rd(od), 'od' + i, 'dictionary own');
  n = log.length; wrL(od, i);
  check(log[n], 'od<-' + i, 'dictionary own setter');
  Object.defineProperty(od, 'prop',
      {value: 'odata', writable: false, configurable: true});
  check(rd(od), 'odata', 'dictionary own now data');
  check(wrS(od, 2), 'threw TypeError', 'dictionary own now read-only');
  check(wrL(od, 2), 'ok', 'dictionary own now read-only sloppy');
  check(rd(od), 'odata', 'dictionary own read-only unchanged');
}

// Global accessors, read and written through TryGetById/TryPutById.
var GKEY = Object.keys({gAcc: 0})[0];
var gStore = 0;
function gRd() { try { return gAcc; } catch (e) { return 'threw ' + e.constructor.name; } }
function gWr(v) { 'use strict'; try { gAcc = v; return 'ok'; } catch (e) { return 'threw ' + e.constructor.name; } }
for (var i = 0; i < 20; i++) {
  Object.defineProperty(globalThis, 'gAcc', {get() { return 'g' + gStore; },
      set(v) { gStore = v; }, configurable: true});
  for (var j = 0; j < 3; j++) {
    check(gRd(), globalThis[GKEY], 'global get');
    check(gWr(i * 10 + j), 'ok', 'global set');
    check(gStore, i * 10 + j, 'global setter called');
  }
  delete globalThis.gAcc;
  check(gRd(), 'threw ReferenceError', 'deleted global get');
  check(gWr(i), 'threw ReferenceError', 'deleted global set');
  check(gStore, i * 10 + 2, 'deleted global setter not called');
}

// Enough names and classes to evict entries from the cache, with accessors
// redefined in place while it is full.
var NAMES = [];
for (var i = 0; i < 40; i++) NAMES.push('acc' + i);
var rdAll = new Function('o',
    'return [' + NAMES.map(k => 'o.' + k).join(', ') + '];');
var rdAllC = o => NAMES.map(k => o[k]);
var wrAll = new Function('o', 'v', NAMES.map(k => 'o.' + k + ' = v;').join(''));
var wrAllC = (o, v) => NAMES.forEach(k => { o[k] = v; });
function mkWide(tag, j) {
  var p = {};
  p['shape' + j] = j;
  NAMES.forEach(function(k, ki) {
    Object.defineProperty(p, k, {get() { return tag + k + ':' + this.x; },
        set(v) { log.push(tag + k + '<-' + v); }, configurable: true});
  });
  return p;
}
var wideProtos = [];
for (var j = 0; j < 8; j++) wideProtos.push(mkWide('w' + j, j));
var wideObjs = [];
wideProtos.forEach(function(p, j) {
  for (var k = 0; k < 3; k++) {
    var o = Object.create(p);
    o['own' + k] = k;
    o.x = j * 10 + k;
    wideObjs.push(o);
  }
});
for (var pass = 0; pass < 4; pass++) {
  wideObjs.forEach(function(o, oi) {
    check(JSON.stringify(rdAll(o)), JSON.stringify(rdAllC(o)), 'wide get ' + oi);
    var n = log.length; wrAll(o, pass);
    var la = log.slice(n).join('|');
    n = log.length; wrAllC(o, pass);
    check(la, log.slice(n).join('|'), 'wide set ' + oi);
  });
  // Redefine some accessors in place: the classes stay the same.
  var p = wideProtos[pass];
  Object.defineProperty(p, NAMES[pass], {get() { return 'redef' + pass; },
      set(v) { log.push('redef<-' + v); }, configurable: true});
}

// Warm entries, let their classes die, collect, and read through classes
// allocated afterwards, which may reuse the dead classes' memory. Then the
// prototype holds a data property where the dead one held an accessor.
function gcWarm(r) {
  for (var i = 0; i < 64; i++) {
    var p = {};
    p['gcShape' + r + '_' + i] = i;
    Object.defineProperty(p, 'prop', {get() { return 'gc' + this.x; },
        set(v) { log.push('gc<-' + v); }, configurable: true});
    var o = Object.create(p);
    o.x = i;
    check(rd(o), 'gc' + i, 'gc warm get');
    var n = log.length; wrL(o, i);
    check(log[n], 'gc<-' + i, 'gc warm set');
  }
}
function gcFresh(r) {
  for (var i = 0; i < 64; i++) {
    var p = {};
    p['gcShape' + r + '_' + i] = i;
    Object.defineProperty(p, 'prop',
        {value: function() {}, writable: true, configurable: true});
    var o = Object.create(p);
    o.x = i;
    check(rd(o), p.prop, 'gc fresh get');
    check(wrS(o, i), 'ok', 'gc fresh set');
    check(rdC(o), i, 'gc fresh set own');
  }
}
for (var r = 0; r < 8; r++) {
  gcWarm(r);
  gc();
  gcFresh(r);
}

print('failures', failures);
// CHECK-NEXT: failures 0
