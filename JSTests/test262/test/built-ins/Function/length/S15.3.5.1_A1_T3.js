// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    The value of the length property is usually an integer that indicates the
    'typical' number of arguments expected by the function
es5id: 15.3.5.1_A1_T3
description: >
    Checking length property of
    Function("arg1,arg2,arg3","arg1,arg2","arg3", null)
---*/

var f = new Function("arg1,arg2,arg3", "arg1,arg2", "arg3", null);

//CHECK#1
if (!(f.hasOwnProperty('length'))) {
  throw new Test262Error('#1: the function has length property.');
}

//CHECK#2
if (f.length !== 6) {
  throw new Test262Error('#2: The value of the length property is usually an integer that indicates the "typical" number of arguments expected by the function');
}
