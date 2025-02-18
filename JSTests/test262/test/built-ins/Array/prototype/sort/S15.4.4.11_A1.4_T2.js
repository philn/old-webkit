// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    If [[Get]] ToString(j) is undefined, return 1.
    If [[]Get] ToString(k) is undefined, return -1
esid: sec-array.prototype.sort
description: If comparefn is not undefined
---*/

var myComparefn = function(x, y) {
  if (x === undefined) return -1;
  if (y === undefined) return 1;
  return 0;
}

var x = new Array(undefined, 1);
x.sort(myComparefn);

//CHECK#1
if (x.length !== 2) {
  throw new Test262Error('#1: var x = new Array(undefined, 1); x.sort(myComparefn); x.length === 2. Actual: ' + (x.length));
}

//CHECK#2
if (x[0] !== 1) {
  throw new Test262Error('#2: var x = new Array(undefined, 1); x.sort(myComparefn); x[0] === 1. Actual: ' + (x[0]));
}

//CHECK#3
if (x[1] !== undefined) {
  throw new Test262Error('#3: var x = new Array(undefined, 1); x.sort(myComparefn); x[1] === undefined. Actual: ' + (x[1]));
}

var x = new Array(1, undefined);
x.sort(myComparefn);

//CHECK#4
if (x.length !== 2) {
  throw new Test262Error('#4: var x = new Array(1, undefined); x.sort(myComparefn); x.length === 2. Actual: ' + (x.length));
}

//CHECK#5
if (x[0] !== 1) {
  throw new Test262Error('#5: var x = new Array(1, undefined); x.sort(myComparefn); x[0] === 1. Actual: ' + (x[0]));
}

//CHECK#6
if (x[1] !== undefined) {
  throw new Test262Error('#6: var x = new Array(1, undefined); x.sort(myComparefn); x[1] === undefined. Actual: ' + (x[1]));
}
