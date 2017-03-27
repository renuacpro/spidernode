// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 15.4.4.18-4-3
description: Array.prototype.forEach throws TypeError if callbackfn is null
---*/

  var arr = new Array(10);
assert.throws(TypeError, function() {
    arr.forEach(null);
});

reportCompare(0, 0);
