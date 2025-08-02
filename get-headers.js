const fs = require('fs');
const path = require('path');
const https = require('https');
const { execSync } = require('child_process');

async function getHeaders(vscodeExec, outPath) {
	// Get VS Code package.json path from command line argument
	if (!vscodeExec || !outPath)
		throw new Error('VS Code executable path and output path required as arguments');

	const vscodePkg = path.join(path.dirname(vscodeExec), 'resources', 'app', 'package.json');
	
	// Read electron version
	const pkg = JSON.parse(fs.readFileSync(vscodePkg, 'utf8'));
	const electronVer = pkg.devDependencies.electron;
	
	// Get releases data
	const releases	= await fetch('https://releases.electronjs.org/releases.json').then(r => r.json());
	const nodeVer	= 'v' + releases.find(r => r.version === electronVer).node;
	
	console.log(`Mapped Electron version: ${electronVer} to Node.js version: ${nodeVer}`);

	const nodejsUrl = `https://nodejs.org/download/release/${nodeVer}`;
	fs.mkdirSync(outPath, { recursive: true });
	
	// Download headers
	const headers = download(`${nodejsUrl}/node-${nodeVer}-headers.tar.gz`, `${outPath}/headers.tar.gz`).then(() => {
		// Extract headers
		execSync(`tar -xf ${outPath}\\headers.tar.gz -C ${outPath} --strip-components 1`);
	});
	
	// Download node.lib
	const lib = download(`${nodejsUrl}/win-x64/node.lib`, `${outPath}/node.lib`);

	await Promise.all([headers, lib]);
	
	console.log(`Headers and node.lib downloaded to ${outPath}`);
	
}

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

getHeaders(process.argv[2], process.argv[3]).catch(console.error);