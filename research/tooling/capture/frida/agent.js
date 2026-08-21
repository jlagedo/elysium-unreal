'use strict';

const SCHEMA = 'elysium.research.frida.events.v1';
const listeners = [];
const callerCounts = new Map();
const uniqueCallers = new Set();
const traceCounts = new Map();
const cappedTargets = new Set();
let observer = null;
let config = null;
let stopped = false;
let traceSequence = 0;
let traceDropped = 0;

function emit(kind, fields = {}) {
  send({
    schema: SCHEMA,
    kind,
    timestamp_ms: Date.now(),
    ...fields,
  });
}

function hexBytes(text) {
  if (text.length % 2 !== 0) {
    throw new Error(`odd expected-byte string length: ${text.length}`);
  }
  const result = [];
  for (let index = 0; index < text.length; index += 2) {
    result.push(parseInt(text.slice(index, index + 2), 16));
  }
  return result;
}

function relocatedExpectedBytes(profile, target, module) {
  const result = hexBytes(target.expected_bytes);
  const relocation = target.relocated_operand || { offset: 0, size: 0 };
  if (relocation.size === 0) {
    return result;
  }
  if (relocation.size !== 4 || relocation.offset + 4 > result.length) {
    throw new Error(
      `${target.semantic_label}: unsupported relocated operand ` +
      `offset=${relocation.offset} size=${relocation.size}`
    );
  }
  const offset = relocation.offset;
  let value = (
    result[offset] |
    (result[offset + 1] << 8) |
    (result[offset + 2] << 16) |
    (result[offset + 3] << 24)
  ) >>> 0;
  const loadDelta = (
    module.base.toUInt32() - profile.pe.preferred_image_base
  ) >>> 0;
  value = (value + loadDelta) >>> 0;
  result[offset] = value & 0xff;
  result[offset + 1] = (value >>> 8) & 0xff;
  result[offset + 2] = (value >>> 16) & 0xff;
  result[offset + 3] = (value >>> 24) & 0xff;
  return result;
}

function bytesEqual(address, expected) {
  const actual = new Uint8Array(address.readByteArray(expected.length));
  if (actual.length !== expected.length) {
    return false;
  }
  for (let index = 0; index < expected.length; ++index) {
    if (actual[index] !== expected[index]) {
      return false;
    }
  }
  return true;
}

function normalizeAddress(address) {
  const module = Process.findModuleByAddress(address);
  if (module === null) {
    return { address: address.toString(), module: null, rva: null };
  }
  return {
    address: address.toString(),
    module: module.name,
    rva: `0x${address.sub(module.base).toUInt32().toString(16)}`,
  };
}

function stackWords(stackPointer, maximum) {
  const words = [];
  for (let index = 0; index < maximum; ++index) {
    try {
      words.push(stackPointer.add(index * Process.pointerSize).readPointer().toString());
    } catch (error) {
      words.push(null);
      break;
    }
  }
  return words;
}

function installCallerTraceAt(module, label, address) {
  const maximumUnique = config.recipe.max_unique_callers || 256;
  const stackWordCount = config.recipe.stack_words || 8;
  const listener = Interceptor.attach(address, {
    onEnter() {
      const caller = normalizeAddress(this.returnAddress);
      const key = `${label}|${caller.module}|${caller.rva}`;
      callerCounts.set(key, (callerCounts.get(key) || 0) + 1);
      if (uniqueCallers.has(key) || uniqueCallers.size >= maximumUnique) {
        return;
      }
      uniqueCallers.add(key);
      let accurate = [];
      let fuzzy = [];
      try {
        accurate = Thread.backtrace(this.context, Backtracer.ACCURATE)
          .map(normalizeAddress);
      } catch (error) {
        emit('warning', {
          operation: 'accurate_backtrace',
          target: label,
          reason: String(error),
        });
      }
      try {
        fuzzy = Thread.backtrace(this.context, Backtracer.FUZZY)
          .map(normalizeAddress);
      } catch (error) {
        emit('warning', {
          operation: 'fuzzy_backtrace',
          target: label,
          reason: String(error),
        });
      }
      emit('caller', {
        target: label,
        thread_id: this.threadId,
        depth: this.depth,
        caller,
        ecx: this.context.ecx.toString(),
        esp: this.context.esp.toString(),
        stack_words: stackWords(this.context.esp, stackWordCount),
        accurate_backtrace: accurate,
        fuzzy_backtrace: fuzzy,
      });
    },
  });
  listeners.push(listener);
  emitHookInstalled(module, label, address);
}

function emitHookInstalled(module, label, address) {
  emit('hook_installed', {
    target: label,
    module: module.name,
    base: module.base.toString(),
    rva: `0x${address.sub(module.base).toUInt32().toString(16)}`,
  });
}

// A named field turns an ordered trace into readable state: a recipe declares
// where a value lives relative to the call and the agent decodes it in place.
// A field the process cannot supply is reported as an error string on the
// record rather than dropped, so a bad displacement is visible in the capture.
function fieldBase(field, context, words) {
  const base = field.base;
  if (base === 'module') {
    const module = Process.findModuleByName(field.module);
    if (module === null) {
      throw new Error(`module not loaded: ${field.module}`);
    }
    return module.base.add(field.rva);
  }
  if (base === 'register') {
    const value = context[field.register];
    if (value === undefined) {
      throw new Error(`unknown register: ${field.register}`);
    }
    return value;
  }
  if (base === 'argument') {
    const word = words[field.index];
    if (word === null || word === undefined) {
      throw new Error(`argument ${field.index} is unreadable`);
    }
    return ptr(word);
  }
  throw new Error(`unknown field base: ${base}`);
}

function readTyped(address, type) {
  switch (type) {
    case 'u8': return address.readU8();
    case 'i32': return address.readS32();
    case 'u32': return address.readU32();
    case 'f32': return address.readFloat();
    case 'ptr': return address.readPointer().toString();
    case 'vec3': return [
      address.readFloat(),
      address.add(4).readFloat(),
      address.add(8).readFloat(),
    ];
    default: throw new Error(`unknown field type: ${type}`);
  }
}

function readFields(declarations, context, words) {
  const result = {};
  for (const field of declarations) {
    try {
      let address = fieldBase(field, context, words);
      for (const step of field.deref || []) {
        address = address.add(step).readPointer();
      }
      result[field.label] = readTyped(
        address.add(field.offset || 0),
        field.type
      );
    } catch (error) {
      result[field.label] = { error: String(error) };
    }
  }
  return result;
}

function fieldsFor(label, phase) {
  const declared = (config.recipe.field_reads || {})[label] || [];
  return declared.filter((field) => (field.when || 'enter') === phase);
}

// A trace keeps every call in order rather than the first hit per unique
// caller, so a recipe can read a producer/consumer sequence instead of a
// caller census. Backtraces are omitted because the ordering is the answer
// and unwinding every call is what makes a hook expensive.
function installTraceAt(module, label, address) {
  const stackWordCount = config.recipe.stack_words || 8;
  const maximumEvents = config.recipe.max_trace_events || 4096;
  const wantsLeave = ((config.recipe.field_reads || {})[label] || [])
    .some((field) => (field.when || 'enter') === 'leave');
  const callbacks = {
    onEnter() {
      const observed = (traceCounts.get(label) || 0) + 1;
      traceCounts.set(label, observed);
      if (observed > maximumEvents) {
        this.sequence = null;
        traceDropped += 1;
        if (!cappedTargets.has(label)) {
          cappedTargets.add(label);
          emit('warning', {
            operation: 'trace_cap_reached',
            target: label,
            max_trace_events: maximumEvents,
          });
        }
        return;
      }
      this.sequence = ++traceSequence;
      const words = stackWords(this.context.esp, stackWordCount);
      const record = {
        sequence: this.sequence,
        target: label,
        thread_id: this.threadId,
        depth: this.depth,
        caller: normalizeAddress(this.returnAddress),
        ecx: this.context.ecx.toString(),
        esp: this.context.esp.toString(),
        stack_words: words,
      };
      const declared = fieldsFor(label, 'enter');
      if (declared.length !== 0) {
        record.fields = readFields(declared, this.context, words);
      }
      this.words = words;
      if (wantsLeave) {
        // A leave-phase register base means the value the call was entered
        // with: ECX is caller-saved, so reading it after the return would
        // decode whatever the callee left behind.
        this.entryRegisters = {
          eax: this.context.eax, ebx: this.context.ebx,
          ecx: this.context.ecx, edx: this.context.edx,
          esi: this.context.esi, edi: this.context.edi,
          ebp: this.context.ebp, esp: this.context.esp,
        };
      }
      emit('call', record);
    },
  };
  if (config.recipe.capture_returns === true || wantsLeave) {
    callbacks.onLeave = function onLeave(returnValue) {
      if (this.sequence === null || this.sequence === undefined) {
        return;
      }
      const record = {
        sequence: this.sequence,
        target: label,
        thread_id: this.threadId,
        eax: this.context.eax.toString(),
        return_value: returnValue.toString(),
      };
      const declared = fieldsFor(label, 'leave');
      if (declared.length !== 0) {
        record.fields = readFields(
          declared,
          this.entryRegisters || this.context,
          this.words || []
        );
      }
      emit('return', record);
    };
  }
  listeners.push(Interceptor.attach(address, callbacks));
  emitHookInstalled(module, label, address);
}

function installHookAt(module, label, address) {
  if (config.recipe.mode === 'trace') {
    installTraceAt(module, label, address);
    return;
  }
  installCallerTraceAt(module, label, address);
}

function installCallerTrace(module, profile, target) {
  const address = module.base.add(target.rva);
  const expected = relocatedExpectedBytes(profile, target, module);
  if (!bytesEqual(address, expected)) {
    emit('failure', {
      operation: 'install_hook',
      target: target.semantic_label,
      module: module.name,
      rva: `0x${target.rva.toString(16)}`,
      reason: 'unexpected-prologue',
    });
    return;
  }
  installHookAt(module, target.semantic_label, address);
}

function moduleMatches(profile, module) {
  if (profile.module.toLowerCase() !== module.name.toLowerCase()) {
    return false;
  }
  if (module.size !== profile.pe.size_of_image) {
    emit('failure', {
      operation: 'validate_module',
      module: module.name,
      reason: 'image-size-mismatch',
      expected: profile.pe.size_of_image,
      observed: module.size,
    });
    return false;
  }
  if (
    profile.approved_path &&
    profile.approved_path.replaceAll('\\', '/').toLowerCase() !==
      module.path.replaceAll('\\', '/').toLowerCase()
  ) {
    emit('failure', {
      operation: 'validate_module',
      module: module.name,
      reason: 'module-path-mismatch',
      expected: profile.approved_path,
      observed: module.path,
    });
    return false;
  }
  return true;
}

function onModuleAdded(module) {
  emit('module_added', {
    name: module.name,
    path: module.path,
    base: module.base.toString(),
    size: module.size,
  });
  for (const declaration of config.recipe.export_hooks || []) {
    if (declaration.module.toLowerCase() !== module.name.toLowerCase()) {
      continue;
    }
    try {
      installHookAt(
        module,
        declaration.label,
        module.getExportByName(declaration.export)
      );
    } catch (error) {
      emit('failure', {
        operation: 'install_export_hook',
        target: declaration.label,
        module: module.name,
        export: declaration.export,
        reason: String(error),
      });
    }
  }
  for (const profile of config.profiles) {
    if (!moduleMatches(profile, module)) {
      continue;
    }
    for (const target of profile.targets) {
      if (!config.recipe.targets.includes(target.semantic_label)) {
        continue;
      }
      try {
        installCallerTrace(module, profile, target);
      } catch (error) {
        emit('failure', {
          operation: 'install_hook',
          target: target.semantic_label,
          module: module.name,
          reason: String(error),
        });
      }
    }
  }
}

function buildSummary() {
  return {
    mode: config === null ? null : config.recipe.mode || 'callers',
    callers: Object.fromEntries(callerCounts),
    unique_callers: uniqueCallers.size,
    trace_calls: Object.fromEntries(traceCounts),
    trace_events: traceSequence,
    trace_dropped: traceDropped,
  };
}

rpc.exports = {
  initialize(initialConfig) {
    if (config !== null) {
      throw new Error('Frida agent already initialized');
    }
    config = initialConfig;
    if (config.require_ia32 && (Process.arch !== 'ia32' || Process.pointerSize !== 4)) {
      throw new Error(
        `expected ia32/pointer-size 4, observed ${Process.arch}/${Process.pointerSize}`
      );
    }
    observer = Process.attachModuleObserver({
      onAdded: onModuleAdded,
      onRemoved(module) {
        emit('module_removed', {
          name: module.name,
          path: module.path,
          base: module.base.toString(),
          size: module.size,
        });
      },
    });
    const ready = {
      arch: Process.arch,
      pointer_size: Process.pointerSize,
      platform: Process.platform,
      pid: Process.id,
    };
    emit('ready', ready);
    return ready;
  },

  stop() {
    if (stopped) {
      return buildSummary();
    }
    stopped = true;
    for (const listener of listeners.splice(0)) {
      try {
        listener.detach();
      } catch (error) {
        emit('warning', { operation: 'detach_hook', reason: String(error) });
      }
    }
    if (observer !== null) {
      observer.detach();
      observer = null;
    }
    const summary = buildSummary();
    emit('summary', summary);
    return summary;
  },
};
