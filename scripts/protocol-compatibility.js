'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const repositoryRoot = path.resolve(__dirname, '..');
const installedRoot = process.env.CHAT_VCPKG_INSTALLED_ROOT
    ? path.resolve(process.env.CHAT_VCPKG_INSTALLED_ROOT)
    : path.join(repositoryRoot, 'vcpkg_installed');
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
/** 在已安装的固定 triplet 中定位协议工具，缺失时返回默认路径供明确报错。 */
function resolvePinnedTool(relativePath) {
    const triplets = [
        process.env.VCPKG_HOST_TRIPLET,
        'x64-windows',
        'x64-windows-chat',
        'x64-windows-chat-release'
    ].filter(Boolean);
    for (const triplet of [...new Set(triplets)]) {
        const candidate = path.join(installedRoot, triplet, relativePath);
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return path.join(installedRoot, 'x64-windows', relativePath);
}

const protoc = resolvePinnedTool(path.join('tools', 'protobuf', 'protoc.exe'));
const grpcCppPlugin = resolvePinnedTool(path.join('tools', 'grpc', 'grpc_cpp_plugin.exe'));
const canonicalFiles = ['varify.proto', 'status.proto', 'chat.proto'];
const initialReleaseProtoBlob = 'a8a34ebd17d5378376cf611762e5943a4c1bff45';
const generatedFiles = canonicalFiles.flatMap(/** 从权威协议源名生成应有的 C++ 消息与 gRPC 文件名。 */ (file) => {
    const stem = path.basename(file, '.proto');
    return [`${stem}.pb.h`, `${stem}.pb.cc`, `${stem}.grpc.pb.h`, `${stem}.grpc.pb.cc`];
});
const legacyEditableProtoFiles = [
    path.join('GateServer', 'GateServer', 'message.proto'),
    path.join('StatusServer', 'StatusServer', 'message.proto'),
    path.join('ChatServer', 'ChatServer', 'message.proto'),
    path.join('VarifyServer', 'message.proto')
];

/** 以给定诊断中止协议检查。 */
function fail(message) {
    throw new Error(message);
}

/** 在仓库根运行工具并收集输出，启动或非零退出时传播失败。 */
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

/** 要求路径存在且是普通文件，否则报告仓库相对路径。 */
function requireFile(file) {
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) {
        fail(`Required file is missing: ${path.relative(repositoryRoot, file)}`);
    }
}

/** 核对权威协议源集合；按需拒绝服务目录中的旧可编辑副本。 */
function verifyCanonicalSources(sourceRoot = protoRoot, checkLegacyCopies = false) {
    const actualProtoFiles = fs.readdirSync(sourceRoot)
        .filter(/** 筛选 proto 源文件。 */ (file) => file.endsWith('.proto'))
        .sort();
    const expectedProtoFiles = [...canonicalFiles].sort();
    const unexpected = actualProtoFiles.filter(/** 筛出未登记的协议源文件。 */ (file) => !expectedProtoFiles.includes(file));
    if (unexpected.length > 0) {
        fail(`unregistered canonical proto source: ${unexpected.join(', ')}`);
    }
    for (const file of canonicalFiles) {
        requireFile(path.join(sourceRoot, file));
    }
    if (checkLegacyCopies) {
        const legacyCopies = legacyEditableProtoFiles.filter(/** 筛出仍存在的旧协议副本。 */ (file) =>
            fs.existsSync(path.join(repositoryRoot, file))
        );
        if (legacyCopies.length > 0) {
            fail(`service-local editable proto authority remains: ${legacyCopies.join(', ')}`);
        }
    }
}

/** 读取 package-lock 中指定 Node 包的固定版本，未锁定时失败。 */
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

/** 核对 protoc、gRPC 插件、vcpkg 状态及 Node 协议依赖的固定版本。 */
function verifyToolchain() {
    requireFile(protoc);
    requireFile(grpcCppPlugin);
    const protocVersion = run(protoc, ['--version']).trim();
    assert.equal(protocVersion, 'libprotoc 33.4', `unexpected protoc version: ${protocVersion}`);

    const vcpkgStatus = fs.readFileSync(
        path.join(installedRoot, 'vcpkg', 'status'),
        'utf8'
    );
    assert.match(vcpkgStatus, /Package: protobuf\r?\nVersion: 6\.33\.4\r?\nPort-Version: 1/);
    assert.match(vcpkgStatus, /Package: grpc\r?\nVersion: 1\.76\.0\r?\nPort-Version: 1/);
    assert.equal(lockedPackageVersion('@grpc/grpc-js'), '1.14.3');
    assert.equal(lockedPackageVersion('@grpc/proto-loader'), '0.8.0');
    assert.equal(lockedPackageVersion('protobufjs'), '7.5.5');
}

/** 核对 Node 协议工具存在及锁定版本，返回 protobufjs 模块目录。 */
function verifyNodeProtocolToolchain() {
    const protobufRoot = path.join(repositoryRoot, 'VarifyServer', 'node_modules', 'protobufjs');
    requireFile(path.join(protobufRoot, 'package.json'));
    assert.equal(lockedPackageVersion('@grpc/grpc-js'), '1.14.3');
    assert.equal(lockedPackageVersion('@grpc/proto-loader'), '0.8.0');
    assert.equal(lockedPackageVersion('protobufjs'), '7.5.5');
    return protobufRoot;
}

/** 使用固定 protoc 和插件从权威协议源生成 C++ 文件。 */
function generateCpp(outputRoot) {
    fs.mkdirSync(outputRoot, { recursive: true });
    run(protoc, [
        `--proto_path=${protoRoot}`,
        `--cpp_out=${outputRoot}`,
        `--grpc_out=${outputRoot}`,
        `--plugin=protoc-gen-grpc=${grpcCppPlugin}`,
        ...canonicalFiles.map(/** 将权威协议文件名解析为源路径。 */ (file) => path.join(protoRoot, file))
    ]);
}

/** 为指定源集生成 protobuf 描述符文件。 */
function generateDescriptor(outputPath, sourceRoot = protoRoot, sources = canonicalFiles) {
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
    run(protoc, [
        `--proto_path=${sourceRoot}`,
        `--descriptor_set_out=${outputPath}`,
        ...sources.map(/** 将协议源名解析为当前源目录下的路径。 */ (file) => path.join(sourceRoot, file))
    ]);
}

/** 从指定偏移解析有界 varint，返回值及新偏移；截断或超限失败。 */
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

/** 解析描述符中的支持字段类型，拒绝截断或不支持的线路编码。 */
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

/** 将描述符字段字节解码为 UTF-8 文本。 */
function text(field) {
    return field.value.toString('utf8');
}

/** 读取首个匹配编号的字段值，缺失时返回指定回退值。 */
function first(fields, number, fallback = undefined) {
    const field = fields.find(/** 按字段编号匹配目标字段。 */ (candidate) => candidate.number === number);
    return field ? field.value : fallback;
}

/** 提取字段名称、编号、标签、类型及类型引用。 */
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

/** 解析消息字段及保留编号区间和名称。 */
function parseMessage(buffer) {
    const fields = wireFields(buffer);
    return {
        name: first(fields, 1, Buffer.alloc(0)).toString('utf8'),
        field: fields.filter(/** 筛选消息的字段描述符。 */ (field) => field.number === 2).map(/** 解析单个消息字段描述符。 */ (field) => parseField(field.value)),
        reservedRange: fields.filter(/** 筛选保留编号区间。 */ (field) => field.number === 9).map(/** 解析保留区间的起止边界。 */ (field) => {
            const range = wireFields(field.value);
            return { start: first(range, 1, 0), end: first(range, 2, 0) };
        }),
        reservedName: fields.filter(/** 筛选保留字段名称。 */ (field) => field.number === 10).map(text)
    };
}

/** 提取 RPC 名称、请求响应类型及流式标志。 */
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

/** 提取服务名称及方法列表。 */
function parseService(buffer) {
    const fields = wireFields(buffer);
    return {
        name: first(fields, 1, Buffer.alloc(0)).toString('utf8'),
        method: fields.filter(/** 筛选服务内的方法描述符。 */ (field) => field.number === 2).map(/** 解析单个 RPC 方法描述符。 */ (field) => parseMethod(field.value))
    };
}

/** 解析文件描述符集合中的包、消息及服务定义。 */
function parseDescriptorSet(buffer) {
    return wireFields(buffer).filter(/** 筛选文件描述符。 */ (field) => field.number === 1).map(/** 解析单个文件的包名、消息及服务。 */ (file) => {
        const fields = wireFields(file.value);
        return {
            package: first(fields, 2, Buffer.alloc(0)).toString('utf8'),
            messageType: fields.filter(/** 筛选文件中的消息类型。 */ (field) => field.number === 4).map(/** 解析单个消息类型。 */ (field) => parseMessage(field.value)),
            service: fields.filter(/** 筛选文件中的服务定义。 */ (field) => field.number === 6).map(/** 解析单个服务定义。 */ (field) => parseService(field.value))
        };
    });
}

/** 将描述符字节建立为按完整名称、字段身份及 RPC 索引的兼容性模型。 */
function descriptorModelFromBuffer(buffer) {
    const set = { file: parseDescriptorSet(buffer) };
    const messages = new Map();
    const services = new Map();

    for (const descriptorFile of set.file) {
        const prefix = descriptorFile.package ? `.${descriptorFile.package}` : '';
        for (const message of descriptorFile.messageType) {
            const fullName = `${prefix}.${message.name}`;
            messages.set(fullName, {
                fields: new Map(message.field.map(/** 按字段编号建立索引条目。 */ (field) => [field.number, field])),
                fieldsByName: new Map(message.field.map(/** 按字段名称建立索引条目。 */ (field) => [field.name, field])),
                reservedNames: new Set(message.reservedName),
                reservedRanges: message.reservedRange
            });
        }
        for (const service of descriptorFile.service) {
            const fullName = `${prefix}.${service.name}`;
            services.set(fullName, new Map(service.method.map(/** 按 RPC 名称建立索引条目。 */ (method) => [method.name, method])));
        }
    }
    return { messages, services };
}

/** 读取描述符文件并构造兼容性模型。 */
function descriptorModel(file) {
    return descriptorModelFromBuffer(fs.readFileSync(file));
}

/** 使用固定 Node 工具解析协议并规范化类型全名，返回描述符字节。 */
function nodeDescriptor(sourceRoot) {
    const protobufRoot = verifyNodeProtocolToolchain();
    const protobuf = require(protobufRoot);
    require(path.join(protobufRoot, 'ext', 'descriptor'));
    const root = new protobuf.Root();
    root.loadSync(canonicalFiles.map(/** 定位 Node 工具需要读取的权威协议源。 */ (file) => path.join(sourceRoot, file)), { keepCase: true });
    root.resolveAll();
    const descriptor = root.toDescriptor('proto3');
    for (const file of descriptor.file) {
        const prefix = file.package ? `.${file.package}.` : '.';
        for (const message of file.messageType || []) {
            for (const field of message.field || []) {
                if (field.typeName && !field.typeName.startsWith('.')) {
                    field.typeName = `${prefix}${field.typeName}`;
                }
            }
        }
        for (const service of file.service || []) {
            for (const method of service.method || []) {
                if (method.inputType && !method.inputType.startsWith('.')) {
                    method.inputType = `${prefix}${method.inputType}`;
                }
                if (method.outputType && !method.outputType.startsWith('.')) {
                    method.outputType = `${prefix}${method.outputType}`;
                }
            }
        }
    }
    return Buffer.from(descriptor.$type.encode(descriptor).finish());
}

/** 判断字段编号是否处于某个保留区间。 */
function numberIsReserved(message, number) {
    return message.reservedRanges.some(/** 按左闭右开区间判断保留编号。 */ (range) => number >= range.start && number < range.end);
}

/** 比较旧新协议模型，拒绝删除、重用或改变既有线路与 RPC 合同。 */
function compareCompatibilityModels(baseline, current) {
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

/** 从两个描述符文件加载模型并检查向后兼容。 */
function compareCompatibility(baselineFile, currentFile) {
    compareCompatibilityModels(descriptorModel(baselineFile), descriptorModel(currentFile));
}

/** 将基线文件与当前描述符字节比较兼容性。 */
function compareCompatibilityBuffer(baselineFile, currentBuffer) {
    compareCompatibilityModels(
        descriptorModel(baselineFile),
        descriptorModelFromBuffer(currentBuffer)
    );
}

/** 核对生成文件集合及逐文件字节，拒绝多余或漂移产物。 */
function compareGenerated(expectedRoot, actualRoot) {
    const actualGeneratedFiles = fs.readdirSync(actualRoot)
        .filter(/** 筛选 C++ 消息及 gRPC 生成文件。 */ (file) => /(?:\.grpc)?\.pb\.(?:cc|h)$/.test(file))
        .sort();
    const expectedGeneratedFiles = [...generatedFiles].sort();
    const unexpected = actualGeneratedFiles.filter(/** 筛出未登记的生成文件。 */ (file) => !expectedGeneratedFiles.includes(file));
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

/** 核对生成目录精确包含全部已登记的权威协议消费者。 */
function verifyGeneratedConsumers() {
    const actualGeneratedFiles = fs.readdirSync(generatedRoot)
        .filter(/** 筛选应纳入注册检查的生成文件。 */ (file) => /(?:\.grpc)?\.pb\.(?:cc|h)$/.test(file))
        .sort();
    const expectedGeneratedFiles = [...generatedFiles].sort();
    assert.deepEqual(
        actualGeneratedFiles,
        expectedGeneratedFiles,
        'generated/proto/cpp must contain exactly the registered canonical consumers'
    );
    for (const file of expectedGeneratedFiles) {
        requireFile(path.join(generatedRoot, file));
    }
}

/** 从指定历史 Git blob 在临时目录生成初始发布基线，并保证清理。 */
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

/** 核对固定工具链、生成漂移和初始版本兼容性，结束后清理临时文件。 */
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

/** 无需原生生成工具地核对源注册、消费者集合与 Node 描述符兼容性。 */
function checkContract() {
    verifyCanonicalSources(protoRoot, true);
    requireFile(baselinePath);
    verifyGeneratedConsumers();
    compareCompatibilityBuffer(baselinePath, nodeDescriptor(protoRoot));
    process.stdout.write('Protocol contract and generated consumer registration checks passed.\n');
}

/** 对指定协议源目录生成 Node 描述符并核对初始发布兼容性。 */
function checkCompatibilitySource(sourceRoot) {
    if (!sourceRoot) {
        fail('check-compatibility requires a proto source directory');
    }
    requireFile(baselinePath);
    verifyCanonicalSources(sourceRoot);
    compareCompatibilityBuffer(baselinePath, nodeDescriptor(sourceRoot));
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
    } else if (task === 'check-contract') {
        checkContract();
    } else if (task === 'check-compatibility') {
        checkCompatibilitySource(process.argv[3]);
    } else {
        process.stderr.write('Usage: node scripts/protocol-compatibility.js <generate|create-initial-baseline|check|check-contract|check-compatibility> [path-or-ref]\n');
        process.exitCode = 2;
    }
}

module.exports = { descriptorModel };
