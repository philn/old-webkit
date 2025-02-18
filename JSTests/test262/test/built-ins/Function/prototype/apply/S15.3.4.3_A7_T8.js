// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    If argArray is either an array or an arguments object,
    the function is passed the (ToUint32(argArray.length)) arguments argArray[0], argArray[1],...,argArray[ToUint32(argArray.length)-1]
es5id: 15.3.4.3_A7_T8
description: >
    argArray is (this, arguments), inside function call without
    declaration used
---*/

(function() {
  Function("a1,a2,a3", "this.shifted=a1+a2+a3;").apply(this, arguments);
})("", 4, 2);

//CHECK#2
if (this["shifted"] !== "42") {
  throw new Test262Error('#2: If argArray is either an array or an arguments object, the function is passed the...');
}
