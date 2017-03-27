// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 15.4.4.20-9-c-ii-13
description: >
    Array.prototype.filter - callbackfn that uses arguments object to
    get parameter value
---*/

        function callbackfn() {
            return arguments[2][arguments[1]] === arguments[0];
        }
        var newArr = [11].filter(callbackfn);

assert.sameValue(newArr.length, 1, 'newArr.length');
assert.sameValue(newArr[0], 11, 'newArr[0]');

reportCompare(0, 0);
