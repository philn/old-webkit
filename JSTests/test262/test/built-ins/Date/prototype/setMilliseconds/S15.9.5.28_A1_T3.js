// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: The Date.prototype property "setMilliseconds" has { DontEnum } attributes
esid: sec-date.prototype.setmilliseconds
description: Checking DontEnum attribute
---*/

if (Date.prototype.propertyIsEnumerable('setMilliseconds')) {
  throw new Test262Error('#1: The Date.prototype.setMilliseconds property has the attribute DontEnum');
}

for (var x in Date.prototype) {
  if (x === "setMilliseconds") {
    throw new Test262Error('#2: The Date.prototype.setMilliseconds has the attribute DontEnum');
  }
}
