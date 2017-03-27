// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 15.4.4.22-1-13
description: Array.prototype.reduceRight applied to the JSON object
---*/

        var accessed = false;

        function callbackfn(prevVal, curVal, idx, obj) {
            accessed = true;
            return ('[object JSON]' === Object.prototype.toString.call(obj));
        }

            JSON.length = 1;
            JSON[0] = 1;

assert(Array.prototype.reduceRight.call(JSON, callbackfn, 1), 'Array.prototype.reduceRight.call(JSON, callbackfn, 1) !== true');
assert(accessed, 'accessed !== true');

reportCompare(0, 0);
