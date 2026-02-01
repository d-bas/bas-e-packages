import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

export type SerializedString = string;

type NativeModule = {
  stringify: (value: unknown) => string;
  parse: (text: string) => unknown;
  debugType: (value: unknown) => Record<string, unknown>;
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

export function stringify(value: unknown): SerializedString {
  return loadNative().stringify(value);
}

export function parse(text: SerializedString): unknown {
  return loadNative().parse(text);
}

export function debugType(value: unknown): Record<string, unknown> {
  return loadNative().debugType(value);
}
