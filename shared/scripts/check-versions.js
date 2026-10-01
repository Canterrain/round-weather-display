// The project has one version, carried in several files. Fails if they
// disagree, so a release can't bump one and forget the others. To release,
// bump all of them together:
//   - package.json                  (repo)
//   - targets/pi/package.json       (`npm version X.Y.Z --no-git-tag-version`
//   - targets/pi/package-lock.json   in targets/pi updates both)
//   - docs/firmware/manifest.json   (ESP32-P4 browser installer)
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '../..');
const read = (file) => JSON.parse(fs.readFileSync(path.join(root, file), 'utf8'));

const piLock = read('targets/pi/package-lock.json');
const versions = {
  'package.json': read('package.json').version,
  'targets/pi/package.json': read('targets/pi/package.json').version,
  'targets/pi/package-lock.json': piLock.version,
  'targets/pi/package-lock.json (packages[""])': piLock.packages?.['']?.version,
  'docs/firmware/manifest.json': read('docs/firmware/manifest.json').version
};

const distinct = new Set(Object.values(versions));
for (const [file, version] of Object.entries(versions)) {
  console.log(`${version}  ${file}`);
}

if (distinct.size !== 1) {
  console.error('\nVersion mismatch: bump every file listed above to the same version.');
  process.exit(1);
}
console.log(`\nAll versions match: ${[...distinct][0]}`);
