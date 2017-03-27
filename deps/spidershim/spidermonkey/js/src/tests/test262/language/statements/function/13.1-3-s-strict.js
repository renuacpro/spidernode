'use strict';
// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: >
    Refer 13.1; 
    It is a SyntaxError if the Identifier "eval" or the Identifier "arguments" occurs within a FormalParameterList
    of a strict mode FunctionDeclaration or FunctionExpression.
es5id: 13.1-3-s
description: >
    Strict Mode - SyntaxError is thrown if the identifier 'arguments'
    appears within a FormalParameterList of a strict mode
    FunctionDeclaration
flags: [onlyStrict]
---*/


assert.throws(SyntaxError, function() {
            eval("function _13_1_3_fun(arguments) { }");
});

reportCompare(0, 0);
