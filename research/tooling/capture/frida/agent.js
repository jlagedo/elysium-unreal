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
let dedupDropped = 0;
let sampledOut = 0;
let depthFiltered = 0;
const lastChangeKeys = new Map();
const hitCounts = new Map();

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

// A module lookup is a search over every loaded image, and a hook that runs thousands of
// times a second cannot afford one per call. Modules are page-aligned and never move, so one
// lookup per 64 KB page answers every address in it.
const modulePages = new Map();

function moduleForAddress(address) {
  const page = address.shr(16).toUInt32();
  if (modulePages.has(page)) {
    return modulePages.get(page);
  }
  const module = Process.findModuleByAddress(address);
  modulePages.set(page, module);
  return module;
}

function normalizeAddress(address) {
  const module = moduleForAddress(address);
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
function fieldBase(field, context, words, returnValue) {
  const base = field.base;
  if (base === 'return') {
    if (returnValue === null || returnValue === undefined) {
      throw new Error('a return base is only readable at leave');
    }
    return returnValue;
  }
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

// An array read resolves its length from a value already decoded in the same
// phase, because the count that matters is the model's own bone count and it
// arrives as a separate field rather than as a constant. `max_count` is the
// ceiling that keeps a wrong or hostile count from walking the address space;
// it is required, so a recipe cannot declare an unbounded read by omission.
function arrayLength(field, sofar) {
  const declared = field.count_from === undefined
    ? field.count
    : (sofar || {})[field.count_from];
  const count = typeof declared === 'number' ? declared : Number.NaN;
  if (!Number.isFinite(count) || count < 0) {
    throw new Error(`array length is not a number: ${field.count_from || field.count}`);
  }
  // The counted thing and the read thing are rarely the same unit: a pose is
  // counted in bones and read in floats, twelve to a matrix3x4_t.
  const scale = field.count_scale === undefined ? 1 : field.count_scale;
  return Math.min(Math.floor(count) * scale, field.max_count);
}

function readTyped(address, field, sofar) {
  switch (field.type) {
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
    // A model path and a sequence label are the answer this capture is for, so
    // a string is read in place under the recipe's own bound rather than being
    // reconstructed from a pointer after the fact. The bound is a ceiling, not a
    // length: studiohdr Name@12 is a char[128] whose tail past the terminator is
    // whatever the compiler left there, and reading the field whole would carry
    // that into the record and make the path unmatchable against the install.
    case 'cstr': {
      const limit = field.max_length || 128;
      const bytes = new Uint8Array(address.readByteArray(limit));
      let text = '';
      for (let index = 0; index < bytes.length && bytes[index] !== 0; ++index) {
        text += String.fromCharCode(bytes[index]);
      }
      return text;
    }
    // A pose is an array of matrix3x4_t, which is 12 floats a bone, and the
    // whole point of capturing one is to compare it against an evaluated pose
    // offline -- so it is read as a flat float run and shaped by the reader.
    // Rounded on the way out: the record is JSON, a full double costs about
    // seventeen characters against nine, and the fifth decimal of an inch is
    // already two orders below anything a bone comparison can resolve.
    case 'f32array': {
      const count = arrayLength(field, sofar);
      const scale = Math.pow(10, field.decimals === undefined ? 5 : field.decimals);
      const bytes = address.readByteArray(count * 4);
      const view = new Float32Array(bytes);
      const out = new Array(count);
      for (let index = 0; index < count; ++index) {
        out[index] = Math.round(view[index] * scale) / scale;
      }
      return out;
    }
    // The same run left raw, for a window whose type is what is being decided.
    case 'u32array': {
      const count = arrayLength(field, sofar);
      const bytes = address.readByteArray(count * 4);
      const view = new Uint32Array(bytes);
      const out = new Array(count);
      for (let index = 0; index < count; ++index) {
        out[index] = view[index];
      }
      return out;
    }
    default: throw new Error(`unknown field type: ${field.type}`);
  }
}

function readFields(declarations, context, words, returnValue) {
  const result = {};
  for (const field of declarations) {
    try {
      let address = fieldBase(field, context, words, returnValue);
      for (const step of field.deref || []) {
        address = address.add(step).readPointer();
      }
      result[field.label] = readTyped(address.add(field.offset || 0), field, result);
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
//
// A per-frame target repeats one answer for as long as nothing changes, which
// spends a whole session's event budget on the first seconds of it. A recipe
// names, per target, the values whose tuple *is* the answer; the agent emits
// only when that tuple differs from the last one the target produced. Every
// suppressed call is counted into the summary, so a quiet target is a fact in
// the record rather than an absence.
function changeKeySpec(label) {
  const spec = (config.recipe.on_change || {})[label];
  if (Array.isArray(spec) && spec.length !== 0) {
    return { names: spec, everSeen: false };
  }
  const first = (config.recipe.on_first || {})[label];
  if (Array.isArray(first) && first.length !== 0) {
    return { names: first, everSeen: true };
  }
  return null;
}

// `on_change` compares against the last answer the target gave, so a value that
// alternates re-emits every time it flips -- which is what a weighted draw is
// for. `on_first` compares against every answer the target has ever given, which
// is what a census is for: interleaved entities each resolving their own model
// per frame never repeat consecutively, so `on_change` would suppress none of
// them and spend the whole budget restating the same few tuples.
const seenKeys = new Map();

function keyIsNew(label, spec, key) {
  if (spec.everSeen) {
    let seen = seenKeys.get(label);
    if (seen === undefined) {
      seen = new Set();
      seenKeys.set(label, seen);
    }
    if (seen.has(key)) {
      return false;
    }
    seen.add(key);
    return true;
  }
  if (lastChangeKeys.get(label) === key) {
    return false;
  }
  lastChangeKeys.set(label, key);
  return true;
}

function changeKey(spec, record, fields, returnValue) {
  const parts = [];
  for (const name of spec.names) {
    if (name === 'ecx') {
      parts.push(record.ecx);
      continue;
    }
    if (name === 'return_value') {
      parts.push(returnValue === undefined ? null : String(returnValue));
      continue;
    }
    const word = /^word(\d+)$/.exec(name);
    if (word !== null) {
      const words = record.stack_words || [];
      parts.push(words[Number(word[1])] === undefined ? null : words[Number(word[1])]);
      continue;
    }
    const value = (fields || {})[name];
    parts.push(JSON.stringify(value === undefined ? null : value));
  }
  return parts.join('|');
}

// The budget counts what reaches the capture. A call suppressed as unchanged
// never spent one, which is the whole point of declaring the key.
function admitTraceEvent(label, maximumEvents) {
  const observed = (traceCounts.get(label) || 0) + 1;
  traceCounts.set(label, observed);
  if (observed > maximumEvents) {
    traceDropped += 1;
    if (!cappedTargets.has(label)) {
      cappedTargets.add(label);
      emit('warning', {
        operation: 'trace_cap_reached',
        target: label,
        max_trace_events: maximumEvents,
      });
    }
    return null;
  }
  return ++traceSequence;
}

// What a suppressed call costs is what a session's frame rate is made of. The change key is
// therefore evaluated FIRST, off the smallest read that can answer it -- only the stack words
// the key names, only the fields the key names -- and a call that fails it returns before a
// record, a full stack window or a module lookup is ever built. The expensive shape is
// assembled only for a call that survives.
function keyPlan(spec, declared) {
  if (spec === null) {
    return { words: 0, fields: [] };
  }
  const labels = new Set(spec.names);
  const fields = declared.filter(
    (field) => (field.when || 'enter') === 'enter' && labels.has(field.label)
  );
  let words = 0;
  for (const name of spec.names) {
    const word = /^word(\d+)$/.exec(name);
    if (word !== null) {
      words = Math.max(words, Number(word[1]) + 1);
    }
  }
  for (const field of fields) {
    if (field.base === 'argument') {
      words = Math.max(words, field.index + 1);
    }
  }
  return { words, fields };
}

// Some targets answer per entity per frame and are asked thousands of times a second for an
// answer that changes once a session. Their cost is per CALL, not per record, so no key can
// reduce it -- only not running. A sampled target processes one call in N and returns from the
// rest before touching the CPU context at all, which is what makes the early return cheap.
// A census still converges: an entity on screen for a second is asked far more than N times.
// Sampling is only ever correct for a census; an ordered target must never carry one.
function installTraceAt(module, label, address) {
  const sample = Math.max(1, (config.recipe.sample || {})[label] || 1);
  const maxDepth = (config.recipe.max_depth || {})[label];
  let sampleCounter = 0;
  const stackWordCount = config.recipe.stack_words || 8;
  const maximumEvents = config.recipe.max_trace_events || 4096;
  const declaredFields = (config.recipe.field_reads || {})[label] || [];
  const wantsLeave = declaredFields
    .some((field) => (field.when || 'enter') === 'leave');
  const changeSpec = changeKeySpec(label);
  // A key that cannot be decided on the way in defers the whole record to the return, and the
  // pair is emitted or dropped together. Two things make a key undecidable at enter: the return
  // value itself, and any declared field read at LEAVE -- an out-parameter is the common one, and
  // it is exactly the field a selection's answer arrives through. Deferring only on
  // `return_value` silently drops a leave-phase key part instead: `keyPlan` filters it out, the
  // key stringifies the absent value to "null", and the key collapses to a constant that
  // suppresses every distinct answer the target ever gives.
  const deferredLabels = new Set(
    declaredFields
      .filter((field) => (field.when || 'enter') === 'leave')
      .map((field) => field.label)
  );
  const deferred = changeSpec !== null
    && (changeSpec.names.indexOf('return_value') !== -1
      || changeSpec.names.some((name) => deferredLabels.has(name)));
  const plan = keyPlan(deferred ? null : changeSpec, declaredFields);
  // A deferred target must read its stack window on the way IN. The argument area sits below
  // the caller's ESP once the call returns, which makes it free stack that the interceptor's
  // own return path writes into, so reading it at leave decodes the trampoline rather than the
  // arguments. Sampling is what makes a per-frame deferred target affordable, not deferring
  // the read.

  function buildRecord(context, threadId, depth, returnAddress) {
    const words = stackWords(context.esp, stackWordCount);
    const record = {
      sequence: null,
      target: label,
      thread_id: threadId,
      depth,
      rawReturn: returnAddress,
      ecx: context.ecx.toString(),
      esp: context.esp.toString(),
      stack_words: words,
    };
    const declared = fieldsFor(label, 'enter');
    if (declared.length !== 0) {
      record.fields = readFields(declared, context, words);
    }
    return record;
  }

  function emitCall(record) {
    // The module lookup is deferred to here: a suppressed call never pays for it.
    record.caller = normalizeAddress(record.rawReturn);
    delete record.rawReturn;
    emit('call', record);
  }

  const callbacks = {
    onEnter() {
      this.sequence = null;
      this.pending = null;
      hitCounts.set(label, (hitCounts.get(label) || 0) + 1);
      if (maxDepth !== undefined && this.depth > maxDepth) {
        depthFiltered += 1;
        return;
      }
      if (sample !== 1) {
        sampleCounter += 1;
        if (sampleCounter % sample !== 0) {
          sampledOut += 1;
          return;
        }
      }
      if (changeSpec !== null && !deferred) {
        const keyWords = plan.words === 0
          ? []
          : stackWords(this.context.esp, plan.words);
        const keyFields = plan.fields.length === 0
          ? null
          : readFields(plan.fields, this.context, keyWords);
        const probe = { ecx: this.context.ecx.toString(), stack_words: keyWords };
        if (!keyIsNew(label, changeSpec, changeKey(changeSpec, probe, keyFields))) {
          dedupDropped += 1;
          return;
        }
      }
      const record = buildRecord(
        this.context, this.threadId, this.depth, this.returnAddress
      );
      this.words = record.stack_words;
      if (wantsLeave || deferred) {
        // A leave-phase register base means the value the call was entered with: ECX is
        // caller-saved, so reading it after the return would decode whatever the callee left.
        this.entryRegisters = {
          eax: this.context.eax, ebx: this.context.ebx,
          ecx: this.context.ecx, edx: this.context.edx,
          esi: this.context.esi, edi: this.context.edi,
          ebp: this.context.ebp, esp: this.context.esp,
        };
      }
      if (deferred) {
        this.pending = record;
        return;
      }
      this.sequence = admitTraceEvent(label, maximumEvents);
      if (this.sequence === null) {
        return;
      }
      record.sequence = this.sequence;
      emitCall(record);
    },
  };
  if (config.recipe.capture_returns === true || wantsLeave || deferred) {
    callbacks.onLeave = function onLeave(returnValue) {
      const registers = this.entryRegisters || this.context;
      const declared = fieldsFor(label, 'leave');
      if (deferred) {
        const pending = this.pending;
        this.pending = null;
        if (pending === null || pending === undefined) {
          return;
        }
        const record = pending;
        const leaveFields = declared.length === 0
          ? {}
          : readFields(declared, registers, this.words || [], returnValue);
        const merged = Object.assign({}, record.fields || {}, leaveFields);
        const key = changeKey(changeSpec, record, merged, returnValue);
        if (!keyIsNew(label, changeSpec, key)) {
          dedupDropped += 1;
          return;
        }
        const sequence = admitTraceEvent(label, maximumEvents);
        if (sequence === null) {
          return;
        }
        record.sequence = sequence;
        emitCall(record);
        const returnRecord = {
          sequence,
          target: label,
          thread_id: this.threadId,
          eax: this.context.eax.toString(),
          return_value: returnValue.toString(),
        };
        if (declared.length !== 0) {
          returnRecord.fields = leaveFields;
        }
        emit('return', returnRecord);
        return;
      }
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
      if (declared.length !== 0) {
        record.fields = readFields(
          declared, registers, this.words || [], returnValue
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
    dedup_dropped: dedupDropped,
    sampled_out: sampledOut,
    depth_filtered: depthFiltered,
    // Every hook entry, emitted or not. A target whose hits dwarf its events is what a
    // session's frame rate is being spent on.
    hits: Object.fromEntries(hitCounts),
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
