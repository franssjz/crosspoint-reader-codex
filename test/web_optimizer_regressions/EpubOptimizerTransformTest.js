const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '../../src/network/html/FilesPage.html'), 'utf8');
for (const [, script] of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)) {
  new vm.Script(script, { filename: 'FilesPage.html' });
}

const context = { TextEncoder };
vm.createContext(context);
const commentsStart = html.indexOf('function stripComments(');
const commentsEnd = html.indexOf('/** Escape a string', commentsStart);
assert.ok(commentsStart > 0 && commentsEnd > commentsStart);
vm.runInContext(html.slice(commentsStart, commentsEnd), context);
const imageStart = html.indexOf('function imageMimeType(');
const imageEnd = html.indexOf('// Convert EPUB file', imageStart);
assert.ok(imageStart > 0 && imageEnd > imageStart);
vm.runInContext(html.slice(imageStart, imageEnd), context);

const { stripComments, imageMimeType, imageOutputPathsConflict, getConvertedImageOutputPath } = context;
const comment = '<!-- corazón 日本語 -->';
const simple = stripComments(`<p>one${comment} two</p>`);
assert.equal(simple.text, '<p>one two</p>');
assert.equal(simple.count, 1);
assert.equal(simple.bytes, Buffer.byteLength(comment));

const declaration = '<?xml version="1.0"?>\n  ';
const doctype = '<!DOCTYPE html [<!-- ]> this is in the subset --> <!ENTITY x "<!-- keep -->">]>\n';
const cdata = '<![CDATA[a <!-- keep --> b]]>';
const pi = '<?sample value="<!-- keep -->"?>';
const preserved = `${declaration}${doctype}<html>${cdata}${pi}<p title="<!-- literal -->">x</p></html>`;
assert.equal(stripComments(preserved).text, preserved);
assert.equal(stripComments(preserved).count, 0);
assert.equal(stripComments(`${preserved}<!-- remove -->`).text, preserved);
assert.equal(stripComments('<p>text</p><!-- not finished').text, '<p>text</p><!-- not finished');
assert.equal(stripComments('<![CDATA[<!-- not finished').text, '<![CDATA[<!-- not finished');
assert.equal(stripComments('<?xml value="<!-- unfinished').text, '<?xml value="<!-- unfinished');
assert.equal(stripComments('<p>&lt;!-- authored text --&gt;</p>').count, 0);
assert.equal(stripComments('<p>A</p>' + '<!-- big -->'.repeat(2000) + '<p>B</p>').text, '<p>A</p><p>B</p>');

assert.equal(imageMimeType('OEBPS/diagram.SVG'), 'image/svg+xml');
assert.equal(imageMimeType('cover.png'), 'image/png');
assert.equal(imageMimeType('cover.jpg'), 'image/jpeg');
assert.equal(getConvertedImageOutputPath('OEBPS/diagram.svg', false), 'OEBPS/diagram.jpg');
assert.equal(getConvertedImageOutputPath('OEBPS/diagram.svg', true), 'OEBPS/diagram.svg');
assert.equal(getConvertedImageOutputPath('OEBPS/cover.png', true), 'OEBPS/cover.png');
assert.equal(imageOutputPathsConflict('images/figure.svg', [{ suffix: '' }], { 'images/figure.jpg': {} }, {}), true);
assert.equal(imageOutputPathsConflict('images/figure.svg', [{ suffix: '' }], {}, { 'images/figure.jpg': {} }), true);
assert.equal(imageOutputPathsConflict('images/figure.svg', [{ suffix: '_part1' }, { suffix: '_part2' }],
  { 'images/figure_part2.jpg': {} }, {}), true);
assert.equal(imageOutputPathsConflict('images/figure.svg', [{ suffix: '' }], { 'other/figure.jpg': {} }, {}), false);
assert.equal(imageOutputPathsConflict('images/figure.jpg', [{ suffix: '' }], { 'images/figure.jpg': {} }, {}), false);

console.log('Optimizer transforms: XML preservation, SVG MIME/fallback, image collisions and inline JS syntax passed');
