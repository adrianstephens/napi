const fs = require('fs');
const path = require('path');
const https = require('https');
const { execSync } = require('child_process');
const { stdout } = require('process');

function download(url, dest) {
	console.log(`Downloading ${url} to ${dest}`);

	return new Promise((resolve, reject) => {
		const file = fs.createWriteStream(dest);
		https.get(url, response => {
			response.pipe(file);
			file.on('finish', () => {
				file.close();
				resolve();
			});
		}).on('error', reject);
	});
}

async function VSCodeToElectron(vscodeExec) {
	// Get VS Code package.json path from command line argument

	const vscodePkg = path.join(path.dirname(vscodeExec), 'resources', 'app', 'package.json');
	
	// Read electron version
	const pkg = JSON.parse(fs.readFileSync(vscodePkg, 'utf8'));
	return pkg.devDependencies.electron;
}

async function ElectronToNode(electron) {
	// Get releases data
	const releases	= await fetch('https://releases.electronjs.org/releases.json').then(r => r.json());
	return releases.find(r => r.version === electron).node;
}


async function getHeaders(node, outPath) {
	node = 'v' + node;
	const nodejsUrl = `https://nodejs.org/download/release/${node}`;
	fs.mkdirSync(outPath, { recursive: true });

	// Download headers
	const headers = download(`${nodejsUrl}/node-${node}-headers.tar.gz`, `${outPath}/headers.tar.gz`).then(() => {
		// Extract headers
		execSync(`tar -xf ${outPath}\\headers.tar.gz -C ${outPath} --strip-components 1`);
	});

	// Download node.lib
	const lib = download(`${nodejsUrl}/win-x64/node.lib`, `${outPath}/node.lib`);

	await Promise.all([headers, lib]);

	console.log(`Headers and node.lib downloaded to ${outPath}`);
}

function checkVersionFormat(ver) {
	if (ver[0] === 'v')
		ver = ver.substring(1);
	if (!/^\d+\.\d+\.\d+$/.test(ver))
		throw new Error('Invalid version format. Expected format: X.Y.Z');
	return ver;
}

async function main(argv) {
	if (argv.length < 3) {
		stdout.write('Usage: node get-headers.js <path to vscode.exe> <output path>\n');
		stdout.write('Usage: node get-headers.js electron:<Version> <output path>\n');
		stdout.write('Usage: node get-headers.js node:<Version> <output path>\n');
		stdout.write('Usage: node get-headers.js <output path>\n');
		process.exit(1);
	}

	let node = process.version.slice(1);
	let output = argv[2];

	if (argv.length === 4) {
		output = argv[3];

		if (argv[2].startsWith('node:')) {
			node = checkVersionFormat(argv[2].substring(5));

		} else {
			let electron;
			if (argv[2].startsWith('electron:')) {
				electron = checkVersionFormat(argv[2].substring(9));
			} else {
				electron = await VSCodeToElectron(argv[2]);
				stdout.write(`VSCode is using Electron version ${electron}.\n`);
			}

			node = await ElectronToNode(electron);
			stdout.write(`Electron version ${electron} is using Node.js version ${node}.\n`);
		}
	}

	if (!output)
		throw new Error('Output path required as argument');

	stdout.write(`Downloading Node.js version ${node} to ${output}.\n`);
	getHeaders(node, output);
}

main(process.argv).catch(err => {
	console.error(err);
	process.exit(1);
});