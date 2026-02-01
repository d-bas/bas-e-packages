import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

export type SerializedString = string;
export type ReplacerCallback = (nextValue: unknown) => void;
export type Replacer = (value: unknown, replace: ReplacerCallback) => void;
export type Reviver = (value: unknown) => unknown;
export type StringifyOptions = {
  replacer?: Replacer;
  circularReferences?: boolean;
};
export type ParseOptions = {
  reviver?: Reviver;
};

type NativeModule = {
  stringify: (value: unknown, options?: StringifyOptions) => string;
  parse: (text: string, options?: ParseOptions) => unknown;
};

const require = createRequire(import.meta.url);
const __dirname = dirname(fileURLToPath(import.meta.url));
const nativePath = join(__dirname, '..', 'build', 'Release', 'bas_serde.node');

let nativeModule: NativeModule | null = null;

function loadNative(): NativeModule {
  if (nativeModule) {
    return nativeModule;
  }

  try {
    nativeModule = require(nativePath) as NativeModule;
    return nativeModule;
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    throw new Error(
      `Native addon not found. Run "node-gyp rebuild" in packages/serialization. Details: ${message}`
    );
  }
}

export function stringify(value: unknown, options?: StringifyOptions): SerializedString {
  return loadNative().stringify(value, options);
}

export function parse(text: SerializedString, options?: ParseOptions): unknown {
  return loadNative().parse(text, options);
}
