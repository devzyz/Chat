'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

/** 在测试自有目录创建一日有效的 localhost 自签证书；不保存或读取用户密钥。 */
function createCertificate(root) {
    if (process.platform === 'win32') {
        const script = `
$ErrorActionPreference = 'Stop'
$root = [Environment]::GetEnvironmentVariable('CHAT_TLS_FIXTURE_ROOT')
$rsa = [Security.Cryptography.RSA]::Create(2048)
try {
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=localhost', $rsa,
        [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    $san.AddDnsName('localhost')
    $request.CertificateExtensions.Add($san.Build())
    $certificate = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddDays(1))
    try {
        [IO.File]::WriteAllText((Join-Path $root 'key.pem'), $rsa.ExportPkcs8PrivateKeyPem())
        [IO.File]::WriteAllText((Join-Path $root 'cert.pem'), $certificate.ExportCertificatePem())
    } finally { $certificate.Dispose() }
} finally { $rsa.Dispose() }
`;
        execFileSync('pwsh', ['-NoProfile', '-NonInteractive', '-Command', script], {
            env: { ...process.env, CHAT_TLS_FIXTURE_ROOT: root }, stdio: 'pipe', timeout: 10000, windowsHide: true
        });
    } else {
        fs.writeFileSync(path.join(root, 'openssl.cnf'), '[req]\ndistinguished_name = dn\n[dn]\n');
        execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
            '-config', path.join(root, 'openssl.cnf'), '-subj', '/CN=localhost',
            '-addext', 'subjectAltName=DNS:localhost', '-keyout', path.join(root, 'key.pem'),
            '-out', path.join(root, 'cert.pem')], { stdio: 'pipe', timeout: 10000 });
    }
}
module.exports = { createCertificate };
