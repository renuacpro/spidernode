// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 15.4.4.21-9-3
description: >
    Array.prototype.reduce doesn't visit deleted elements in array
    after the call
---*/

  function callbackfn(prevVal, curVal, idx, obj)  
  {
    delete arr[3];
    delete arr[4];
    return prevVal + curVal;    
  }

  var arr = ['1',2,3,4,5];

// two elements deleted
assert.sameValue(arr.reduce(callbackfn), "123", 'arr.reduce(callbackfn)');

reportCompare(0, 0);
