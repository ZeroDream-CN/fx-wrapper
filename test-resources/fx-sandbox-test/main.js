'use strict';

const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { Worker } = require('worker_threads');

const TAG = '[fx-sandbox-test]';

function logResult(name, passed, detail) {
    const status = passed ? 'PASS' : 'FAIL';
    const suffix = detail ? ` ${detail}` : '';
    console.log(`${TAG} ${name}:${status}${suffix}`);
}

function runSpawnTest() {
    if (process.platform === 'linux') {
        return new Promise((resolve) => {
            const helperPath = `${GetResourcePath(GetCurrentResourceName())}/spawn_helper.js`;
            const child = cp.fork(helperPath, [], {
                stdio: ['ignore', 'pipe', 'pipe', 'ipc'],
            });

            let output = '';
            child.stdout.on('data', (chunk) => {
                output += chunk.toString();
            });
            child.stderr.on('data', (chunk) => {
                output += chunk.toString();
            });

            child.on('error', (error) => {
                logResult('SPAWN', false, error.message);
                resolve(false);
            });

            child.on('exit', (code) => {
                const passed = code === 0 && output.includes('sandbox_child_ok');
                logResult('SPAWN', passed, passed ? '' : `code=${code} output=${output.trim() || '<empty>'}`);
                resolve(passed);
            });
        });
    }

    return new Promise((resolve) => {
        const command = process.platform === 'win32' ? 'cmd.exe' : '/bin/sh';
        const args =
            process.platform === 'win32'
                ? ['/c', 'echo sandbox_child_ok']
                : ['-c', 'echo sandbox_child_ok'];

        const child = cp.spawn(command, args, {
            stdio: ['ignore', 'pipe', 'pipe'],
            windowsHide: true,
        });

        let stdout = '';
        let stderr = '';

        child.stdout.on('data', (chunk) => {
            stdout += chunk.toString();
        });
        child.stderr.on('data', (chunk) => {
            stderr += chunk.toString();
        });

        child.on('error', (error) => {
            logResult('SPAWN', false, error.message);
            resolve(false);
        });

        child.on('exit', (code) => {
            const output = `${stdout}${stderr}`.trim();
            const passed = code === 0 && output.includes('sandbox_child_ok');
            logResult('SPAWN', passed, passed ? '' : `code=${code} output=${output || '<empty>'}`);
            resolve(passed);
        });
    });
}

function runFilesystemTest() {
    try {
        const filePath = path.join(os.tmpdir(), `fx-sandbox-test-${process.pid}.txt`);
        fs.writeFileSync(filePath, 'sandbox_fs_ok');
        const contents = fs.readFileSync(filePath, 'utf8');
        fs.unlinkSync(filePath);
        const passed = contents === 'sandbox_fs_ok';
        logResult('FS_WRITE', passed, passed ? '' : `contents=${contents}`);
        return passed;
    } catch (error) {
        logResult('FS_WRITE', false, error.message);
        return false;
    }
}

function runWorkerTest() {
    return new Promise((resolve) => {
        let settled = false;
        const finish = (passed, detail) => {
            if (settled) {
                return;
            }
            settled = true;
            logResult('WORKER', passed, detail);
            resolve(passed);
        };

        const worker = new Worker(
            `const { parentPort } = require('worker_threads'); parentPort.postMessage('sandbox_worker_ok');`,
            { eval: true }
        );

        const timer = setTimeout(() => {
            worker.terminate().catch(() => {});
            finish(false, 'timeout');
        }, 5000);

        worker.on('message', (message) => {
            clearTimeout(timer);
            worker.terminate().catch(() => {});
            finish(message === 'sandbox_worker_ok', message === 'sandbox_worker_ok' ? '' : `message=${message}`);
        });

        worker.on('error', (error) => {
            clearTimeout(timer);
            finish(false, error.message);
        });
    });
}

async function main() {
    console.log(`${TAG} START platform=${process.platform}`);

    const spawnOk = await runSpawnTest();
    const fsOk = runFilesystemTest();
    const workerOk = await runWorkerTest();

    const allOk = spawnOk && fsOk && workerOk;
    logResult('ALL', allOk, allOk ? '' : 'one or more checks failed');
}

setImmediate(() => {
    main().catch((error) => {
        logResult('ALL', false, error.message);
    });
});
