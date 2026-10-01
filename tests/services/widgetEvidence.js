'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const widgetCases = ['initTestCase', 'cleanupTestCase', 'localHistorySearchWidgets', 'localDirectorySearchWidgets',
    'friendRemarkOutcomeWidgets', 'conversationAttentionWidgets', 'groupResourceCardWidgets', 'groupMembershipControlsWidgets',
    'groupPanelRejectionVisible', 'groupPanelExternalRevocation', 'groupPanelSnapshotRequired',
    'groupPanelPaginationFailure', 'groupPanelReopenRetryIdentity', 'groupPanelTcpRefreshRevision',
    'avatarPublicationIsolationAndRestore', 'incomingAttachmentAndPageLifetime',
    'resourceModelKeepsTextAndAttachmentsSeparate', 'resumeUploadAndDownload', 'rejectsEmptyFile'];
const storeCases = ['ResumeAfterStoreRecreation', 'RejectsWrongOffsetWithoutChangingFile', 'RejectsOtherOwner',
    'IncompleteUploadCannotComplete', 'DigestMismatchCannotPublish', 'DeclaredLengthAndBufferAreBounded',
    'PathTraversalRejected', 'MediaSignatureIsChecked'];

/** 校验实际测试报告的完整名称集合及失败、禁用、跳过状态。 */
function validateReport(file, expected, gtest = false) {
    const xml = fs.readFileSync(file, 'utf8');
    assert.match(xml, /<testsuite\b/);
    assert.ok(!/<(?:failure|error|skipped)\b/.test(xml));
    assert.ok(!/\b(?:failures|errors|skipped|disabled)="[1-9][0-9]*"/.test(xml));
    const cases = [...xml.matchAll(/<testcase\b([^>]*)>/g)];
    const names = cases.map(/** 从测试记录提取名称，同时拒绝未执行的 GTest 用例。 */ match => {
        const name = /\bname="([^"]+)"/.exec(match[1]);
        assert.ok(name);
        if (gtest) {
            assert.match(match[1], /\bstatus="run"/);
            assert.match(match[1], /\bresult="completed"/);
        }
        return name[1];
    });
    assert.deepEqual(names.sort(), [...expected].sort());
}

/** 校验同源码的真实控件和存储报告、完整截图；不把它们声明为人工桌面验收。 */
function validate(root, sha) {
    assert.match(sha || '', /^[a-f0-9]{40}$/);
    assert.equal(fs.readFileSync(path.join(root, 'source-sha.txt'), 'utf8').trim(), sha);
    validateReport(path.join(root, 'widgets.xml'), widgetCases);
    validateReport(path.join(root, 'resource-store.xml'), storeCases, true);
    const png = fs.readFileSync(path.join(root, 'group-panel.png'));
    assert.ok(png.length >= 45);
    assert.equal(png.subarray(0, 8).toString('hex'), '89504e470d0a1a0a');
    assert.equal(png.subarray(12, 16).toString(), 'IHDR');
    assert.ok(png.readUInt32BE(16) > 0 && png.readUInt32BE(20) > 0);
    assert.equal(png.subarray(-8, -4).toString(), 'IEND');
}
module.exports = { validate, validateReport, widgetCases, storeCases };
if (require.main === module) {
    try { validate(process.argv[2], process.argv[3]); }
    catch { process.stderr.write('Widget acceptance evidence incomplete\n'); process.exitCode = 1; }
}
