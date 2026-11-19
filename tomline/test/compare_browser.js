// Checks the JavaScript port in tomline/browser/tomline.html against the
// firmware's C++ voice (word dumps from tomline/test/words.cc). The port is
// meant to be bit-exact, so any difference fails.
// Run from the eurorack root: node tomline/test/compare_browser.js
const fs = require('fs');
const html = fs.readFileSync('tomline/browser/tomline.html', 'utf8');
const code = html.slice(html.indexOf('/* BEGIN FIRMWARE PORT */'), html.indexOf('/* END FIRMWARE PORT */'));
const { computeParams, TomlineVoice, FW_RATE } = new Function(code + '\nreturn { computeParams, TomlineVoice, FW_RATE };')();

const roller = [12, -1, 12, 24, -1, 15, 12, 10, 12, -1, 12, 19, 17, 15, -1, 10];
const VOCT_K = Math.floor(12 * 65536 * 256 / 3972);
const cases = [
  ['build/tomline_words_bt.bin', 0, 1, [29491, 40632, 32768, 19661], [0, 16384, 0, 0]],
  ['build/tomline_words_lt.bin', 1, 1, [29491, 40632, 32768, 19661], [0, 16384, 0, 0]],
  ['build/tomline_words_glide.bin', 0, 0, [40000, 20000, 30000, 40000], [30000, 50000, 40000, 30000]],
  ['build/tomline_words_extreme.bin', 1, 3, [10000, 60000, 50000, 65535], [65535, 65535, 65535, 0]],
  ['build/tomline_words_lfo_tri.bin', 0, 1, [29491, 40632, 32768, 19661], [0, 16384, 0, 30000], [50000, 40000, 0, 0], 0],
  ['build/tomline_words_lfo_ramp.bin', 1, 2, [40000, 30000, 20000, 0], [0, 30000, 40000, 0], [20000, 65535, 0, 0], 1],
  ['build/tomline_words_lfo_tri_free.bin', 0, 1, [29491, 40632, 32768, 19661], [0, 16384, 0, 30000], [50000, 40000, 0, 0], 0, 1],
  ['build/tomline_words_lfo_ramp_free.bin', 1, 2, [40000, 30000, 20000, 0], [0, 30000, 40000, 0], [20000, 65535, 0, 0], 1, 1],
  ['build/tomline_words_wave_tri.bin', 0, 1, [29491, 60000, 32768, 19661], [0, 16384, 12000, 0]],
  ['build/tomline_words_wave_fold.bin', 1, 2, [45000, 60000, 32768, 0], [0, 20000, 50000, 0]],
  ['build/tomline_words_wave_square.bin', 0, 0, [50000, 50000, 20000, 40000], [0, 0, 65535, 20000]],
  ['build/tomline_words_crush_rate.bin', 0, 1, [40000, 60000, 32768, 19661], [20000, 16384, 0, 0]],
  ['build/tomline_words_crush_bits.bin', 1, 2, [50000, 65535, 32768, 30000], [55000, 16384, 30000, 0]],
];
let failed = false;
for (const [path, engine, snap, main, second, lfo = [0, 0, 0, 0], lfoShape = 0, lfoFree = 0] of cases) {
  const buf = fs.readFileSync(path);
  const ref = new Uint16Array(buf.buffer, buf.byteOffset, buf.length / 2);
  const p = computeParams(engine, snap, main, second, lfo, lfoShape, lfoFree);
  const v = new TomlineVoice();
  const sps = FW_RATE * 60 / 122 / 4;
  let next = 0, step = -1, counts = 0, pitch = 0;
  let maxG = 0, sumG = 0, sumRef = 0, maxF = 0;
  const n = ref.length / 2;
  for (let i = 0; i < n; i++) {
    let trig = false;
    if (i >= next) {
      step++; next += sps;
      const note = roller[step % 16];
      if (note >= 0) { counts = Math.trunc(note * 3972 / 12); trig = true; }
    }
    const semis = (counts * VOCT_K) >> 8;
    pitch += (semis - pitch) >> 5;
    v.process(p, trig, pitch);
    const dg = v.gainWord - ref[2 * i], df = v.freqWord - ref[2 * i + 1];
    maxG = Math.max(maxG, Math.abs(dg)); sumG += dg * dg; sumRef += (ref[2 * i] - 16384) ** 2;
    maxF = Math.max(maxF, Math.abs(df));
  }
  const errDb = sumG ? 10 * Math.log10(sumG / sumRef) : -Infinity;
  const ok = maxG === 0 && maxF === 0;
  if (!ok) failed = true;
  console.log(`${path}: ${n} samples, VCA word max diff ${maxG}, filter word max diff ${maxF}  ${ok ? 'IDENTICAL' : 'DIFFERENT (' + errDb.toFixed(1) + ' dB)'}`);
}
process.exit(failed ? 1 : 0);
