import { describe, it, expect } from 'vitest';
import { stringify, parse } from '../src/index.js';

function canUseNative(): boolean {
  try {
    stringify(1);
    return true;
  } catch {
    return false;
  }
}

const run = canUseNative() ? describe : describe.skip;

run('serialization', () => {
  it('roundtrips primitives and special numbers', () => {
    const input = {
      str: 'hello',
      bool: true,
      nil: null,
      nan: NaN,
      inf: Infinity,
      negInf: -Infinity,
      undef: undefined,
      bigint: 123n,
    };

    const output = parse(stringify(input)) as typeof input;

    expect(output.str).toBe('hello');
    expect(output.bool).toBe(true);
    expect(output.nil).toBeNull();
    expect(Number.isNaN(output.nan)).toBe(true);
    expect(output.inf).toBe(Infinity);
    expect(output.negInf).toBe(-Infinity);
    expect(output.undef).toBeUndefined();
    expect(output.bigint).toBe(123n);
  });

  it('roundtrips Set and Map', () => {
    const set = new Set([1, 2, 3]);
    const map = new Map([[{ k: 1 }, 2]]);

    const output = parse(stringify({ set, map })) as {
      set: Set<number>;
      map: Map<{ k: number }, number>;
    };

    expect(output.set instanceof Set).toBe(true);
    expect(Array.from(output.set.values())).toEqual([1, 2, 3]);

    expect(output.map instanceof Map).toBe(true);
    expect(Array.from(output.map.entries())).toEqual([[{ k: 1 }, 2]]);
  });

  it('roundtrips Date and RegExp', () => {
    const date = new Date('2024-01-01T00:00:00.000Z');
    const regex = /test/gi;

    const output = parse(stringify({ date, regex })) as {
      date: Date;
      regex: RegExp;
    };

    expect(output.date instanceof Date).toBe(true);
    expect(output.date.toISOString()).toBe('2024-01-01T00:00:00.000Z');

    expect(output.regex instanceof RegExp).toBe(true);
    expect(output.regex.source).toBe('test');
    expect(output.regex.flags.split('').sort().join('')).toBe('gi');
  });

  it('roundtrips Buffer and ArrayBuffer', () => {
    const buf = Buffer.from('hello');
    const arrayBuf = new Uint8Array([1, 2, 3]).buffer;

    const output = parse(stringify({ buf, arrayBuf })) as {
      buf: Buffer;
      arrayBuf: ArrayBuffer;
    };

    expect(Buffer.isBuffer(output.buf)).toBe(true);
    expect(output.buf.toString('utf8')).toBe('hello');

    const arrayView = new Uint8Array(output.arrayBuf);
    expect(Array.from(arrayView)).toEqual([1, 2, 3]);
  });

  it('roundtrips TypedArray and DataView', () => {
    const typed = new Uint16Array([500, 1000]);
    const viewBuffer = new ArrayBuffer(4);
    const view = new DataView(viewBuffer);
    view.setUint8(0, 7);
    view.setUint8(1, 9);

    const output = parse(stringify({ typed, view })) as {
      typed: Uint16Array;
      view: DataView;
    };

    expect(output.typed instanceof Uint16Array).toBe(true);
    expect(Array.from(output.typed.values())).toEqual([500, 1000]);

    expect(output.view instanceof DataView).toBe(true);
    expect(output.view.getUint8(0)).toBe(7);
    expect(output.view.getUint8(1)).toBe(9);
  });

  it('preserves array holes', () => {
    const input: Array<number> = [];
    input[2] = 5;

    const output = parse(stringify(input)) as Array<number>;

    expect(output.length).toBe(3);
    expect(0 in output).toBe(false);
    expect(1 in output).toBe(false);
    expect(output[2]).toBe(5);
  });

  it('throws on circular references', () => {
    const obj: Record<string, unknown> = {};
    obj.self = obj;

    expect(() => stringify(obj)).toThrow(TypeError);
  });

  it('throws on unsupported values', () => {
    expect(() => stringify(() => {})).toThrow(TypeError);
    expect(() => stringify(Symbol('x'))).toThrow(TypeError);
  });
});
