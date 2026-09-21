'use strict';

const { test } = require('node:test');
const { faultCases } = require('./smtpFaults');

for (const entry of faultCases) test(`${entry.id} ${entry.name}`, { timeout: 6000 }, entry.action);
