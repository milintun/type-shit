const CANVAS_W = 1200;
const CANVAS_H = 800;

let fontsRegistered = false;

async function loadFonts() {
  if (fontsRegistered) return;

  const style = document.createElement('style');
  style.textContent = `
    @font-face {
      font-family: 'decovar';
      src: url('./DecovarAlpha-VF.ttf') format('truetype');
    }
    @font-face {
      font-family: 'amstelvar';
      src: url('./AmstelvarAlpha-VF.ttf') format('truetype');
    }
  `;
  document.head.appendChild(style);

  await Promise.all([
    document.fonts.load("16px 'decovar'"),
    document.fonts.load("16px 'amstelvar'"),
  ]);

  fontsRegistered = true;
}

export async function renderPoster(withFont) {
  await loadFonts();

  // Render directly to the visible poster canvas (must be in the DOM so that
  // CSS font-variation-settings on the element is picked up by ctx.font)
  const canvas = document.getElementById('poster-canvas');
  canvas.width  = CANVAS_W;
  canvas.height = CANVAS_H;
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = '#0f0f0f';
  ctx.fillRect(0, 0, CANVAS_W, CANVAS_H);

  for (const w of withFont) {
    try {
      const fontKey = (w.fontOutput.font || '').toLowerCase().trim() === 'decovar' ? 'decovar' : 'amstelvar';
      const axes    = w.fontOutput.axes || {};

      // font-variation-settings is an inherited CSS property. Setting it on the
      // canvas element and forcing a style recalculation before ctx.font ensures
      // Chrome picks up the axes when resolving the font for fillText.
      canvas.style.fontVariationSettings = Object.keys(axes).length
        ? Object.entries(axes).map(([tag, val]) => `'${tag}' ${val}`).join(', ')
        : 'normal';
      // Force recalc so ctx.font reads the updated computed style
      window.getComputedStyle(canvas).getPropertyValue('font-variation-settings');

      ctx.save();
      ctx.font        = `${w.layout.fontSize}px '${fontKey}'`;
      ctx.fillStyle   = w.fontOutput.color || '#ffffff';
      ctx.globalAlpha = Math.min(1, Math.max(0, w.fontOutput.opacity ?? w.layout.opacity));
      if ((w.fontOutput.shadowBlur ?? 0) > 0) {
        ctx.shadowColor = w.fontOutput.color;
        ctx.shadowBlur  = w.fontOutput.shadowBlur;
      }
      ctx.fillText(w.word, w.layout.x, w.layout.y);
      ctx.restore();
    } catch (e) {
      console.warn('Render failed for', w.word, e);
      ctx.save();
      ctx.font        = `${w.layout.fontSize}px serif`;
      ctx.fillStyle   = w.fontOutput?.color || '#ffffff';
      ctx.globalAlpha = w.layout.opacity;
      ctx.fillText(w.word, w.layout.x, w.layout.y);
      ctx.restore();
    }
  }

  canvas.style.fontVariationSettings = 'normal';
}
