'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const repositoryRoot = path.resolve(__dirname, '..');
const protoRoot = path.join(repositoryRoot, 'proto');
const generatedRoot = path.join(repositoryRoot, 'generated', 'proto', 'cpp');
const baselinePath = path.join(
    repositoryRoot,
    'tests',
    'server',
    'protocol',
    'fixtures',
    'initial-release-descriptor.pb'
);
function resolvePinnedTool(relativePath) {
    const triplets = [
        process.env.VCPKG_HOST_TRIPLET,
        'x64-windows',
        'x64-windows-chat',
        'x64-windows-chat-release'
    ].filter(Boolean);
    for (const triplet of [...new Set(triplets)]) {
        const candidate = path.join(repositoryRoot, 'vcpkg_installed', triplet, relativePath);
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return path.join(repositoryRoot, 'vcpkg_installed', 'x64-windows', relativePath);
}

const protoc = resolvePinnedTool(path.join('tools', 'protobuf', 'protoc.exe'));
const grpcCppPlugin = resolvePinnedTool(path.join('tools', 'grpc', 'grpc_cpp_plugin.exe'));
const canonicalFiles = ['varify.proto', 'status.proto', 'chat.proto'];
const initialReleaseProtoBlob = 'a8a34ebd17d5378376cf611762e5943a4c1bff45';
const generatedFiles = canonicalFiles.flatMap((file) => {
    const stem = path.basename(file, '.proto');
    return [`${stem}.pb.h`, `${stem}.pb.cc`, `${stem}.grpc.pb.h`, `${stem}.grpc.pb.cc`];
});
const legacyEditableProtoFiles = [
    path.join('GateServer', 'GateServer', 'message.proto'),
    path.join('StatusServer', 'StatusServer', 'message.proto'),
    path.join('ChatServer', 'ChatServer', 'message.proto'),
    path.join('VarifyServer', 'message.proto')
];

function fail(message) {
    throw new Error(message);
}

function run(command, args, options = {}) {
    const result = spawnSync(command, args, {
        cwd: repositoryRoot,
        encoding: options.encoding === undefined ? 'utf8' : options.encoding,
        input: options.input,
        maxBuffer: 16 * 1024 * 1024
    });
    if (result.error) {
        throw result.error;
    }
    if (result.status !== 0) {
        fail(`${path.basename(command)} failed (${result.status}):\n${result.stdout || ''}${result.stderr || ''}`);
    }
    return result.stdout;
}

function requireFile(file) {
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) {
        fail(`Required file is missing: ${path.relative(repositoryRoot, file)}`);
    }
}

function verifyCanonicalSources(sourceRoot = protoRoot, checkLegacyCopies = false) {
    const actualProtoFiles = fs.readdirSync(sourceRoot)
        .filter((file) => file.endsWith('.proto'))
        .sort();
    const expectedProtoFiles = [...canonicalFiles].sort();
    const unexpected = actualProtoFiles.filter((file) => !expectedProtoFiles.includes(file));
    if (unexpected.length > 0) {
        fail(`unregistered canonical proto source: ${unexpected.join(', ')}`);
    }
    for (const file of canonicalFiles) {
        requireFile(path.join(sourceRoot, file));
    }
    if (checkLegacyCopies) {
        const legacyCopies = legacyEditableProtoFiles.filter((file) =>
            fs.existsSync(path.join(repositoryRoot, file))
        );
        if (legacyCopies.length > 0) {
            fail(`service-local editable proto authority remains: ${legacyCopies.join(', ')}`);
        }
    }
}

function lockedPackageVersion(packageName) {
    const lock = JSON.parse(fs.readFileSync(
        path.join(repositoryRoot, 'VarifyServer', 'package-lock.json'),
        'utf8'
    ));
    const entry = lock.packages[`node_modules/${packageName}`];
    if (!entry || !entry.version) {
        fail(`package-lock.json does not pin ${packageName}`);
    }
    return entry.version;
}

function verifyToolchain() {
    requireFile(protoc);
    requireFile(grpcCppPlugin);
    const protocVersion = run(protoc, ['--version']).trim();
    assert.equal(protocVersion, 'libprotoc 33.4', `unexpected protoc version: ${protocVersion}`);

    const vcpkgStatus = fs.readFileSync(
        path.join(repositoryRoot, 'vcpkg_installed', 'vcpkg', 'status'),
        'utf8'
    );
    assert.match(vcpkgStatus, /Package: protobuf\r?\nVersion: 6\.33\.4\r?\nPort-Version: 1/);
    assert.match(vcpkgStatus, /Package: grpc\r?\nVersion: 1\.76\.0\r?\nPort-Version: 1/);
    assert.equal(lockedPackageVersion('@grpc/grpc-js'), '1.14.3');
    assert.equal(lockedPackageVersion('@grpc/proto-loader'), '0.8.0');
    assert.equal(lockedPackageVersion('protobufjs'), '7.5.5');
}

function generateCpp(outputRoot) {
    fs.mkdirSync(outputRoot, { recursive: true });
    run(protoc, [
        `--proto_path=${protoRoot}`,
        `--cpp_out=${outputRoot}`,
        `--grpc_out=${outputRoot}`,
        `--plugin=protoc-gen-grpc=${grpcCppPlugin}`,
        ...canonicalFiles.map((file) => path.join(protoRoot, file))
    ]);
}

function generateDescriptor(outputPath, sourceRoot = protoRoot, sources = canonicalFiles) {
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
    run(protoc, [
        `--proto_path=${sourceRoot}`,
        `--descriptor_set_out=${outputPath}`,
        ...sources.map((file) => path.join(sourceRoot, file))
    ]);
}

function readVarint(buffer, offset) {
    let value = 0;
    let shift = 0;
    while (offset < buffer.length && shift <= 49) {
        const byte = buffer[offset++];
        value += (byte & 0x7f) * (2 ** shift);
        if ((byte & 0x80) === 0) {
            return { value, offset };
        }
        shift += 7;
    }
    fail('invalid descriptor varint');
}

function wireFields(buffer) {
    const result = [];
    let offset = 0;
    while (offset < buffer.length) {
        const key = readVarint(buffer, offset);
        offset = key.offset;
        const number = Math.floor(key.value / 8);
        const wireType = key.value % 8;
        if (wireType === 0) {
            const decoded = readVarint(buffer, offset);
            offset = decoded.offset;
            result.push({ number, wireType, value: decoded.value });
        } else if (wireType === 2) {
            const length = readVarint(buffer, offset);
            offset = length.offset;
            const end = offset + length.value;
            if (end > buffer.length) {
                fail('truncated descriptor field');
            }
            result.push({ number, wireType, value: buffer.subarray(offset, end) });
            offset = end;
        } else if (wireType === 1) {
            offset += 8;
        } else if (wireType === 5) {
            offset += 4;
        } else {
            fail(`unsupported descriptor wire type: ${wireType}`);
        }
    }
    return result;
}

function text(field) {
    return field.value.toString('utf8');
}

function first(fields, number, fallback = undefined) {
    const field = fields.find((candidate) => candidate.number === number);
    return field ? field.value : fallback;
}

function parseField(buffer) {
    const fields = wireFields(buffer);
    return {
        name: first(fields, 1, Buffer.alloc(0)).toString('utf8'),
        number: first(fields, 3, 0),
        label: first(fields, 4, 0),
        type: first(fields, 5, 0),
        typeName: first(fields, 6, Buffer.alloc(0)).toString('utf8')
    };
}

function parseMessage(buffer) {
    const fields = wireFields(buffer);
    return {
        name: first(fields, 1, Buffer.alloc(0)).toString('utf8'),
        field: fields.filter((field) => field.number === 2).map((field) => parseField(field.value)),
        reservedRange: fields.filter((field) => field.number === 9).map((field) => {
            const range = wireFields(field.value);
            return { start: first(range, 1, 0), end: first(range, 2, 0) };
        }),
        reservedName: fields.filter((field) => field.number === 10).map(text)
    };
}

function parseMethod(buffer) {
    const fields = wireFields(buffer);
    return {
        name: first(fields, 1, Buffer.alloc(0)).toString('utf8'),
        inputType: first(fields, 2, Buffer.alloc(0)).toString('utf8'),
        outputType: first(fields, 3, Buffer.alloc(0)).toString('utf8'),
        clientStreaming: Boolean(first(fields, 5, 0)),
        serverStreaming: Boolean(first(fields, 6, 0))
    };
}

function parseService(buffer) {
    const fields = wireFields(buffer);
    return {
        name: first(fields, 1, Buffer.alloc(0)).toString('utf8'),
        method: fields.filter((field) => field.number === 2).map((field) => parseMethod(field.value))
    };
}

function parseDescriptorSet(buffer) {
    return wireFields(buffer).filter((field) => field.number === 1).map((file) => {
        const fields = wireFields(file.value);
        return {
            package: first(fields, 2, Buffer.alloc(0)).toString('utf8'),
            messageType: fields.filter((field) => field.number === 4).map((field) => parseMessage(field.value)),
            service: fields.filter((field) => field.number === 6).map((field) => parseService(field.value))
        };
    });
}

function descriptorModel(file) {
    const set = { file: parseDescriptorSet(fs.readFileSync(file)) };
    const messages = new Map();
    const services = new Map();

    for (const descriptorFile of set.file) {
        const prefix = descriptorFile.package ? `.${descriptorFile.package}` : '';
        for (const message of descriptorFile.messageType) {
            const fullName = `${prefix}.${message.name}`;
            messages.set(fullName, {
                fields: new Map(message.field.map((field) => [field.number, field])),
                fieldsByName: new Map(message.field.map((field) => [field.name, field])),
                reservedNames: new Set(message.reservedName),
                reservedRanges: message.reservedRange
            });
        }
        for (const service of descriptorFile.service) {
            const fullName = `${prefix}.${service.name}`;
            services.set(fullName, new Map(service.method.map((method) => [method.name, method])));
        }
    }
    return { messages, services };
}

function numberIsReserved(message, number) {
    return message.reservedRanges.some((range) => number >= range.start && number < range.end);
}

function compareCompatibility(baselineFile, currentFile) {
    const baseline = descriptorModel(baselineFile);
    const current = descriptorModel(currentFile);

    for (const [messageName, oldMessage] of baseline.messages) {
        const newMessage = current.messages.get(messageName);
        if (!newMessage) {
            fail(`message removed or renamed: ${messageName}`);
        }
        for (const [fieldNumber, oldField] of oldMessage.fields) {
            const newField = newMessage.fields.get(fieldNumber);
            if (!newField) {
                if (!numberIsReserved(newMessage, fieldNumber) || !newMessage.reservedNames.has(oldField.name)) {
                    fail(`deleted field must reserve number and name: ${messageName}.${oldField.name} = ${fieldNumber}`);
                }
                continue;
            }
            if (newField.name !== oldField.name) {
                fail(`field number reused: ${messageName} ${fieldNumber} (${oldField.name} -> ${newField.name})`);
            }
            if (newField.type !== oldField.type || newField.label !== oldField.label ||
                newField.typeName !== oldField.typeName) {
                fail(`field type/cardinality changed: ${messageName}.${oldField.name}`);
            }
            const sameName = newMessage.fieldsByName.get(oldField.name);
            if (!sameName || sameName.number !== fieldNumber) {
                fail(`field number changed: ${messageName}.${oldField.name}`);
            }
        }
    }

    for (const [serviceName, oldMethods] of baseline.services) {
        const newMethods = current.services.get(serviceName);
        if (!newMethods) {
            fail(`service removed or renamed: ${serviceName}`);
        }
        for (const [methodName, oldMethod] of oldMethods) {
            const newMethod = newMethods.get(methodName);
            if (!newMethod) {
                fail(`RPC removed or renamed: ${serviceName}/${methodName}`);
            }
            if (newMethod.inputType !== oldMethod.inputType || newMethod.outputType !== oldMethod.outputType ||
                newMethod.clientStreaming !== oldMethod.clientStreaming ||
                newMethod.serverStreaming !== oldMethod.serverStreaming) {
                fail(`RPC input/output changed: ${serviceName}/${methodName}`);
            }
        }
    }
}

function compareGenerated(expectedRoot, actualRoot) {
    const actualGeneratedFiles = fs.readdirSync(actualRoot)
        .filter((file) => /(?:\.grpc)?\.pb\.(?:cc|h)$/.test(file))
        .sort();
    const expectedGeneratedFiles = [...generatedFiles].sort();
    const unexpected = actualGeneratedFiles.filter((file) => !expectedGeneratedFiles.includes(file));
    if (unexpected.length > 0) {
        fail(`unexpected generated protocol output: ${unexpected.join(', ')}; remove stale generated files`);
    }

    for (const file of generatedFiles) {
        const expected = path.join(expectedRoot, file);
        const actual = path.join(actualRoot, file);
        requireFile(actual);
        if (!fs.readFileSync(expected).equals(fs.readFileSync(actual))) {
            fail(`generated source drift: generated/proto/cpp/${file}; run GenerateProtocols`);
        }
    }
}

function createInitialBaseline(blob = initialReleaseProtoBlob) {
    const temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-proto-baseline-'));
    try {
        const legacyPath = 'ChatServer/ChatServer/message.proto';
        const legacyBytes = run('git.exe', ['cat-file', 'blob', blob], { encoding: null });
        const temporarySource = path.join(temporaryRoot, 'message.proto');
        fs.writeFileSync(temporarySource, legacyBytes);
        generateDescriptor(baselinePath, temporaryRoot, ['message.proto']);
        process.stdout.write(`Initial descriptor baseline generated from ${legacyPath} blob ${blob}.\n`);
    } finally {
        fs.rmSync(temporaryRoot, { recursive: true, force: true });
    }
}

function check() {
    verifyToolchain();
    verifyCanonicalSources(protoRoot, true);
    requireFile(baselinePath);
    const temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-protocol-check-'));
    try {
        const generated = path.join(temporaryRoot, 'generated');
        const currentDescriptor = path.join(temporaryRoot, 'current-descriptor.pb');
        generateCpp(generated);
        generateDescriptor(currentDescriptor);
        compareGenerated(generated, generatedRoot);
        compareCompatibility(baselinePath, currentDescriptor);
    } finally {
        fs.rmSync(temporaryRoot, { recursive: true, force: true });
    }
    process.stdout.write('Protocol compatibility and generated-source drift checks passed.\n');
}

function checkCompatibilitySource(sourceRoot) {
    if (!sourceRoot) {
        fail('check-compatibility requires a proto source directory');
    }
    verifyToolchain();
    requireFile(baselinePath);
    verifyCanonicalSources(sourceRoot);
    const temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-descriptor-check-'));
    try {
        const candidateDescriptor = path.join(temporaryRoot, 'candidate.pb');
        generateDescriptor(candidateDescriptor, sourceRoot);
        compareCompatibility(baselinePath, candidateDescriptor);
    } finally {
        fs.rmSync(temporaryRoot, { recursive: true, force: true });
    }
    process.stdout.write('Protocol descriptor is compatible with the initial release baseline.\n');
}

if (require.main === module) {
    const task = process.argv[2];
    if (task === 'generate') {
        verifyToolchain();
        verifyCanonicalSources(protoRoot, true);
        generateCpp(generatedRoot);
        process.stdout.write('C++ protocol sources generated from proto/*.proto.\n');
    } else if (task === 'create-initial-baseline') {
        verifyToolchain();
        createInitialBaseline(process.argv[3] || initialReleaseProtoBlob);
    } else if (task === 'check') {
        check();
    } else if (task === 'check-compatibility') {
        checkCompatibilitySource(process.argv[3]);
    } else {
        process.stderr.write('Usage: node scripts/protocol-compatibility.js <generate|create-initial-baseline|check|check-compatibility> [path-or-ref]\n');
        process.exitCode = 2;
    }
}

module.exports = { descriptorModel };
