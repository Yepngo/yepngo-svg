#!/usr/bin/env node
/*
  Generate PNG goldens with Chromium for SVG fixtures.

  Usage:
    node Scripts/render_chromium_goldens.js

  Requirements:
    npm i playwright
*/

const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

async function main() {
  const root = path.resolve(__dirname, '..');
  const fixtureDir = path.join(root, 'Fixtures', 'svg');
  const outDir = path.join(root, 'Fixtures', 'golden', 'chromium');

  const fixtureFiles = fs
    .readdirSync(fixtureDir)
    .filter((name) => name.endsWith('.svg'))
    .sort();

  if (!fs.existsSync(outDir)) {
    fs.mkdirSync(outDir, { recursive: true });
  }

  const browser = await chromium.launch({ headless: true });
  const page = await browser.newPage({ viewport: { width: 1024, height: 768 } });

  for (const fileName of fixtureFiles) {
    const fixturePath = path.join(fixtureDir, fileName);
    const svg = fs.readFileSync(fixturePath, 'utf8');

    await page.setContent(`
      <!doctype html>
      <html>
        <head>
          <meta charset="utf-8" />
          <style>
            html, body { margin: 0; padding: 0; background: transparent; }
            body { display: inline-block; }
            svg { display: block; }
          </style>
        </head>
        <body>${svg}</body>
      </html>
    `);

    const element = await page.$('svg');
    if (!element) {
      console.error(`Skipping ${fileName}: no <svg> root`);
      continue;
    }

    const outputName = `${path.basename(fileName, '.svg')}.png`;
    const outputPath = path.join(outDir, outputName);

    await element.screenshot({
      path: outputPath,
      omitBackground: true,
      type: 'png',
    });

    console.log(`Wrote ${outputPath}`);
  }

  await browser.close();
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
