# @bas-e/serialization

Native JSON-like serialization with support for complex types.

## Features
- JSON-compatible string output
- Supports: Set, Map, BigInt, Date, RegExp, Buffer, ArrayBuffer, TypedArray, DataView
- Preserves NaN, Infinity, -Infinity, and undefined
- Detects circular references

## Install
The native addon is built with node-gyp.

```sh
npm run --workspace @bas-e/serialization build:native
```

## Usage

```ts
import { stringify, parse } from '@bas-e/serialization';

const value = {
  id: 1n,
  tags: new Set(['a', 'b']),
  map: new Map([[{ k: 1 }, 2]]),
  date: new Date('2024-01-01T00:00:00.000Z'),
  buf: Buffer.from('hello'),
};

const encoded = stringify(value);
const decoded = parse(encoded);
```

## Replacer

```ts
const encoded = stringify(value, {
  replacer(current, replace) {
    if (typeof current === 'number' && current === 42) {
      replace(undefined);
    }
  },
});
```

The replacer is called for every entity before serialization. Call `replace(newValue)`
to override the current value (including `undefined`). If you don't call `replace`, the
original value is used.

## Reviver

```ts
const decoded = parse(encoded, {
  reviver(value) {
    if (value === 2) return 20;
    return value;
  },
});
```

## Notes
- Objects that contain the key "$$type" may conflict with the internal wrapper format.
- Functions and Symbols are not supported.
- Circular references throw a TypeError.
