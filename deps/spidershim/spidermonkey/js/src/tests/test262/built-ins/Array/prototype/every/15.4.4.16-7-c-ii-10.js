// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 15.4.4.16-7-c-ii-10
description: >
    Array.prototype.every - callbackfn is called with 1 formal
    parameter
---*/

        var called = 0;

        function callbackfn(val) {
            called++;
            return val > 10;
        }

assert([11, 12].every(callbackfn), '[11, 12].every(callbackfn) !== true');
assert.sameValue(called, 2, 'called');

reportCompare(0, 0);
